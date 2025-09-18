// Modified MovementStrategy implementation with additional debug logging and safety checks 

// Original includes and declarations
#include "CS2/MovementStrategy.h"
#include "CS2/Aimbot.h"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <sstream>
#include <Windows.h>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cerrno>
#include <system_error>
#include <queue>
#include <limits>

// 触发器标记 g_just_fired
#include "CS2/Triggerbot.h"

// 如需使用 nlohmann::json，请确保在头文件或此处包含
#include "Utility/json.hpp"

// 防止 Windows 宏污染
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

using SteadyClock = std::chrono::steady_clock;

//
// Debug support:
//
// A simple flag to enable or disable verbose logging.  When set to true,
// MovementStrategy::update will emit detailed information about the current
// player position, enemy position, selected nodes and route lengths.  This
// can be useful when trying to diagnose crashes or unexpected behaviour at
// runtime.  By default the flag is false to avoid spamming the log.
//
static bool g_debug_logging_enabled = false;

// Helper to print debug information.  Uses Logging::log_error since a
// dedicated log_debug function may not exist.  Guarded by
// g_debug_logging_enabled so it can be toggled at runtime if needed.
static void debug_log(const std::string& msg) {
    if (!g_debug_logging_enabled) return;
    Logging::log_error(msg);
}

// ================== 小工具 ==================
static inline bool finite3(const Vec3D<float>& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
static inline float norm_deg_0_360(float a) {
    if (!std::isfinite(a)) return 0.0f;
    a = std::fmod(a, 360.0f);
    if (a < 0.0f) a += 360.0f;
    return a;
}

static std::filesystem::path resolve_navmesh_path(const std::string& processed)
{
    namespace fs = std::filesystem;
    fs::path rel = fs::path("Navmesh") / "json" / (processed + ".json");

    fs::path p1 = fs::current_path() / rel;

#ifdef _WIN32
    wchar_t buf[MAX_PATH]{ 0 };
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    fs::path exeDir = fs::path(buf).parent_path();
    fs::path p2 = exeDir / rel;
#endif

    if (const char* base = std::getenv("CS2_NAVMESH_DIR")) {
        fs::path p3 = fs::path(base) / (processed + ".json");
        if (fs::exists(p3)) return p3;
    }

    if (fs::exists(p1)) return p1;
#ifdef _WIN32
    if (fs::exists(p2)) return p2;
#endif
    return p1;
}

// ================ 辅助：选择“可达的替代目标节点” =================
std::shared_ptr<Node> MovementStrategy::pick_reachable_goal_node_fallback(
    const Vec3D<float>& enemy_pos,
    const std::shared_ptr<Node>& start_node,
    const std::shared_ptr<Node>& end_node)
{
    if (!start_node) return nullptr;

    auto dist_to_enemy = [&](const std::shared_ptr<Node>& n) {
        return (n ? n->position.distance(enemy_pos) : FLT_MAX);
        };

    if (end_node) {
        std::vector<std::shared_ptr<Node>> candidates;
        candidates.reserve(end_node->edges.size());
        for (auto& e : end_node->edges) {
            auto to_node = e.toNode;
            if (to_node && to_node->id != start_node->id) {
                candidates.push_back(to_node);
            }
        }
        candidates.erase(std::remove(candidates.begin(), candidates.end(), nullptr), candidates.end());
        std::sort(candidates.begin(), candidates.end(),
            [&](const std::shared_ptr<Node>& a, const std::shared_ptr<Node>& b) {
                return dist_to_enemy(a) < dist_to_enemy(b);
            });

        for (auto& c : candidates) {
            auto route = Dijkstra::get_route(start_node, c);
            if (route.size() > 1 && route[1]) {
                return c;
            }
        }
    }

    std::vector<std::shared_ptr<Node>> all = m_nodes;
    all.erase(std::remove(all.begin(), all.end(), nullptr), all.end());
    std::sort(all.begin(), all.end(),
        [&](const std::shared_ptr<Node>& a, const std::shared_ptr<Node>& b) {
            return dist_to_enemy(a) < dist_to_enemy(b);
        });
    for (auto& c : all) {
        if (!c || c->id == start_node->id) continue;
        auto route = Dijkstra::get_route(start_node, c);
        if (route.size() > 1 && route[1]) {
            return c;
        }
    }
    return nullptr;
}

// 监控“朝 m_next_node 的靠近进度”
bool MovementStrategy::should_replan_due_to_no_progress(const Vec3D<float>& player_pos)
{
    constexpr int   REPLAN_TIMEOUT_MS = 1800;
    constexpr float MIN_DELTA_MOVE = 14.0f;

    auto now = SteadyClock::now();
    if (!m_next_node) {
        m_progress_anchor_pos = player_pos;
        m_progress_anchor_time = now;
        return false;
    }

    float from_anchor = player_pos.distance(m_progress_anchor_pos);
    int   elapsed_ms = static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now - m_progress_anchor_time).count()
        );

    if (from_anchor >= MIN_DELTA_MOVE) {
        m_progress_anchor_pos = player_pos;
        m_progress_anchor_time = now;
        return false;
    }

    if (elapsed_ms > REPLAN_TIMEOUT_MS) {
        m_progress_anchor_pos = player_pos;
        m_progress_anchor_time = now;
        return true;
    }
    return false;
}

// ================ 核心：Update =================
void MovementStrategy::update(GameInformationhandler* game_info_handler, const Aimbot* aimbot)
{
    // 重入保护：避免多线程/重入带来的竞态
    if (m_in_update) return;
    m_in_update = true;

    try {
        if (!game_info_handler) { m_in_update = false; return; }

        const GameInformation gi = game_info_handler->get_game_information(); // 单帧快照
        const auto now_ms = get_current_time_in_ms();
        auto now = SteadyClock::now();

        // 早期坐标有限性防御
        if (!finite3(gi.controlled_player.position) ||
            !finite3(gi.controlled_player.head_position)) {
            game_info_handler->set_player_movement(Movement{});
            m_in_update = false; return;
        }

        // -------- 导航网格 --------
        bool just_reloaded = handle_navmesh_load(gi.current_map);
        if (!m_valid_navmesh_loaded) {
            m_in_update = false; return;
        }
        if (just_reloaded) {
            // 防止 mid-frame 悬挂
            m_next_node = nullptr;
            game_info_handler->set_player_movement(Movement{});
            m_in_update = false; return;
        }

        // -------- 刚开火的停走保护 --------
        if (g_just_fired) {
            m_last_shoot_time = now;
            g_just_fired = false;
            m_next_node = nullptr;
            game_info_handler->set_player_movement(Movement{});
            m_in_update = false; return;
        }
        {
            int elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_last_shoot_time).count();
            int left = (std::max)(0, m_stop_after_shoot_ms - elapsed);
            if (left > 0) {
                m_next_node = nullptr;
                game_info_handler->set_player_movement(Movement{});
                m_in_update = false; return;
            }
        }

        // -------- 状态与延迟 --------
        static bool last_has_enemy = false;
        static bool last_alive = true;

        bool has_enemy = gi.closest_enemy_player.has_value();
        bool alive = (gi.controlled_player.health > 0);

        // 检测敌人状态变化
        if (last_has_enemy && !has_enemy) {
            m_next_node = nullptr;
            m_delay_time = now_ms + 1200;
        }
        
        // 检测死亡状态变化
        if (last_alive && !alive) {
            // 玩家死亡：立即清移动并延时，避免复活后延续输入
            m_next_node = nullptr;
            m_current_route.clear();
            m_ban_until.clear();
            m_prev_node_id = m_last_node_id = -1;
            m_oscillation_streak = 0;
            m_delay_time = now_ms + 1200;
        }
        
        // 检测复活状态变化
        if (!last_alive && alive) {
            // 玩家复活：重置所有状态并延长延迟时间，避免复活后立即乱走
            m_next_node = nullptr;
            m_current_route.clear();
            m_ban_until.clear();
            m_prev_node_id = m_last_node_id = -1;
            m_oscillation_streak = 0;
            m_delay_time = now_ms + 2000; // 复活后延长延迟到2秒
        }
        
        last_has_enemy = has_enemy;
        last_alive = alive;

        if (!has_enemy || !alive) {
            game_info_handler->set_player_movement(Movement{});
            m_in_update = false; return;
        }
        if (now_ms < m_delay_time) {
            game_info_handler->set_player_movement(Movement{});
            m_in_update = false; return;
        }

        // -------- 关键位置 --------
        const Vec3D<float>& player_pos = gi.controlled_player.position;
        const Vec3D<float>& enemy_pos = gi.closest_enemy_player->position;
        if (!finite3(enemy_pos)) {
            game_info_handler->set_player_movement(Movement{});
            m_in_update = false; return;
        }

        // Debug logging: output key positions and states
        {
            std::ostringstream oss;
            oss << "[MovementStrategy] update: player_pos=(" << player_pos.x << "," << player_pos.y << "," << player_pos.z << ")";
            if (has_enemy) {
                oss << " enemy_pos=(" << enemy_pos.x << "," << enemy_pos.y << "," << enemy_pos.z << ")";
            }
            else {
                oss << " enemy_pos=(N/A)";
            }
            if (m_next_node) {
                oss << " m_next_node_id=" << m_next_node->id;
            }
            else {
                oss << " m_next_node_id=null";
            }
            debug_log(oss.str());
        }

        // -------- 路径规划 --------
        if (!m_next_node)
        {
            auto start_node = get_closest_node_to_position(player_pos);
            // 如果没有任何被观察到的敌人，则避免以敌人位置为目的地，改为选一个与当前视角方向最一致的邻居点
            std::shared_ptr<Node> end_node = nullptr;
            bool any_spotted = false;
            for (const auto& e : gi.other_players) {
                if (e.health > 0 && e.isSpotted) { any_spotted = true; break; }
            }
            if (any_spotted) {
                end_node = get_closest_node_to_position(enemy_pos);
            }

            if (!start_node) {
                game_info_handler->set_player_movement(Movement{});
                m_in_update = false; return;
            }

            std::vector<std::shared_ptr<Node>> route;
            if (end_node) route = Dijkstra::get_route(start_node, end_node);

            if (route.size() <= 1 || (route.size() > 1 && !route[1])) {
                // 仅当存在被观察到的敌人时，才允许用“更接近敌人的候选点”作为替代目标
                bool any_spotted_for_alt = false;
                for (const auto& e : gi.other_players) {
                    if (e.health > 0 && e.isSpotted) { any_spotted_for_alt = true; break; }
                }

                if (any_spotted_for_alt) {
                    auto alt_goal = pick_reachable_goal_node_fallback(enemy_pos, start_node, end_node);
                    if (alt_goal) {
                        m_current_route = Dijkstra::get_route(start_node, alt_goal);
                        if (m_current_route.size() > 1 && m_current_route[1]) {
                            m_next_node = m_current_route[1];
                        }
                        else {
                            // 备用点也没法形成有效第二步：仅停走
                            m_next_node = nullptr;
                            game_info_handler->set_player_movement(Movement{});
                            m_in_update = false; return;
                        }
                    }
                }

                if (!m_next_node) {
                    // 中性选择：按当前视角方向选择与视角夹角最小的邻居点
                    if (!start_node->edges.empty()) {
                        std::shared_ptr<Node> best = nullptr;
                        float best_angle = 1e9f;
                        float view_yaw = norm_deg_0_360(gi.controlled_player.view_vec.y);
                        for (auto& e : start_node->edges) {
                            auto to_node = e.toNode;
                            if (!to_node) continue;
                            float pos_ang = calc_angle_between_two_positions(gi.controlled_player.head_position, to_node->position);
                            pos_ang = norm_deg_0_360(pos_ang);
                            float d = calc_walk_angle(view_yaw, pos_ang); // [0,360]
                            float signed_d = (d > 180.0f) ? (d - 360.0f) : d;
                            float ad = std::fabs(signed_d);
                            if (ad < best_angle) { best_angle = ad; best = to_node; }
                        }
                        if (best) {
                            m_current_route = Dijkstra::get_route(start_node, best);
                            if (m_current_route.size() > 1 && m_current_route[1]) {
                                m_next_node = m_current_route[1];
                            }
                        }
                    }
                    if (!m_next_node) {
                        // 没有可用 next：仅停走，不对齐敌人
                        game_info_handler->set_player_movement(Movement{});
                        m_in_update = false; return;
                    }
                }
            }
            else {
                m_current_route = route;
                m_next_node = (m_current_route.size() > 1 && m_current_route[1] ? m_current_route[1] : nullptr);
                if (!m_next_node) {
                    // 没有可用 next：仅停走，不对齐敌人
                    game_info_handler->set_player_movement(Movement{});
                    m_in_update = false; return;
                }
            }

            m_progress_anchor_pos = player_pos;
            m_progress_anchor_time = SteadyClock::now();
        }

        // -------- 进度监测 --------
        if (should_replan_due_to_no_progress(player_pos)) {
            m_next_node = nullptr;
            game_info_handler->set_player_movement(Movement{});
            m_in_update = false; return;
        }

        // -------- 抖动抑制 + 黑名单（成员变量版本）--------
        auto nowt = SteadyClock::now();
        auto is_banned = [&](int id) -> bool {
            auto it = m_ban_until.find(id);
            return it != m_ban_until.end() && it->second > nowt;
            };
        auto ban_node = [&](int id, int ms) {
            if (id >= 0) m_ban_until[id] = nowt + std::chrono::milliseconds(ms);
            };

        if (m_last_switch_time.time_since_epoch().count() == 0) {
            m_last_switch_time = nowt;
        }

        if (m_next_node) {
            int nid = m_next_node->id;

            if (nid != m_last_node_id) {
                if (nid == m_prev_node_id &&
                    std::chrono::duration_cast<std::chrono::milliseconds>(nowt - m_last_switch_time).count() < 10000) {
                    m_oscillation_streak++;
                }
                else {
                    m_oscillation_streak = 0;
                }
                m_prev_node_id = m_last_node_id;
                m_last_node_id = nid;
                m_last_switch_time = nowt;
            }

            if (m_oscillation_streak >= 2) {
                ban_node(m_last_node_id, 2500);
                ban_node(m_prev_node_id, 2500);
                m_oscillation_streak = 0;

                m_current_route.clear();
                m_next_node = nullptr;
                game_info_handler->set_player_movement(Movement{});
                m_in_update = false; return;
            }

            if (is_banned(nid)) {
                m_current_route.clear();
                m_next_node = nullptr;
                game_info_handler->set_player_movement(Movement{});
                m_in_update = false; return;
            }
        }

        // -------- 执行移动（迟滞 + 步进冷却）--------
        constexpr float ARRIVE_NEAR = 15.0f;  // 增加到达距离，减少过于精确的移动
        constexpr float ARRIVE_FAR = 25.0f;   // 增加离开距离
        constexpr int   STEP_SWITCH_COOLDOWN_MS = 600; // 增加切换冷却时间，减少频繁切换

        static int   arrive_node_id = -1;
        static bool  in_near = false;
        static auto  last_step_switch = SteadyClock::now();

        Movement mv{};

        if (m_next_node) {
            float distance = m_next_node->position.distance(player_pos);

            if (arrive_node_id != m_next_node->id) {
                arrive_node_id = m_next_node->id;
                in_near = false;
            }

            if (!in_near) {
                if (distance > ARRIVE_NEAR) {
                    mv = calculate_move_info(gi, m_next_node);
                }
                else {
                    if (std::chrono::duration_cast<std::chrono::milliseconds>(SteadyClock::now() - last_step_switch).count()
                        >= STEP_SWITCH_COOLDOWN_MS) {

                        auto it = std::find(m_current_route.begin(), m_current_route.end(), m_next_node);
                        if (it != m_current_route.end()) {
                            auto it2 = it; ++it2;
                            if (it2 != m_current_route.end() && *it2) {
                                m_next_node = *it2;
                                last_step_switch = SteadyClock::now();
                                mv = calculate_move_info(gi, m_next_node);
                            }
                            else {
                                m_next_node = nullptr;
                                mv = Movement{};
                            }
                            in_near = true;
                        }
                        else {
                            // 当前节点不在路径中，重置
                            m_next_node = nullptr;
                            mv = Movement{};
                            in_near = false;
                        }
                    }
                    else {
                        mv = Movement{};
                    }
                }
            }
            else {
                if (distance > ARRIVE_FAR) {
                    in_near = false;
                }
            }

        }

        // 添加移动随机化（反检测）
        if (mv.forward || mv.backward || mv.left || mv.right) {
            // 10%概率随机停止移动一帧
            if (m_movement_pause_dist(m_movement_rng) < 10) {
                mv = Movement{};
            }
            // 5%概率随机移除一个移动方向
            else if (m_movement_pause_dist(m_movement_rng) < 5) {
                if (mv.forward && mv.left && m_movement_pause_dist(m_movement_rng) < 50) {
                    mv.left = false;
                } else if (mv.forward && mv.right && m_movement_pause_dist(m_movement_rng) < 50) {
                    mv.right = false;
                } else if (mv.backward && mv.left && m_movement_pause_dist(m_movement_rng) < 50) {
                    mv.left = false;
                } else if (mv.backward && mv.right && m_movement_pause_dist(m_movement_rng) < 50) {
                    mv.right = false;
                }
            }
        }

        game_info_handler->set_player_movement(mv);

        // -------- 视角控制逻辑（与Aimbot协调工作）--------
        // 检查是否有isSpotted的敌人（使用与Aimbot相同的逻辑）
        bool has_spotted_enemy = false;
        
        // 使用更严格的敌人检测逻辑：不仅检查isSpotted，还要检查是否在视野范围内
        // 这样可以避免"隔墙瞄人"的问题
        if (gi.closest_enemy_player && gi.closest_enemy_player->isSpotted && gi.closest_enemy_player->health > 0) {
            // 计算敌人相对于玩家视角的角度差
            Vec3D<float> my_head = gi.controlled_player.head_position;
            Vec3D<float> enemy_pos = gi.closest_enemy_player->position;
            Vec2D<float> current_view = gi.controlled_player.view_vec;
            
            // 计算指向敌人的角度
            Vec3D<float> dir = enemy_pos - my_head;
            float target_yaw = std::atan2(dir.y, dir.x) * 180.0f / static_cast<float>(M_PI) + 180.0f;
            if (target_yaw >= 360.0f) target_yaw -= 360.0f;
            if (target_yaw < 0.0f) target_yaw += 360.0f;
            
            // 计算角度差
            float angle_diff = target_yaw - current_view.y;
            if (angle_diff > 180.0f) angle_diff -= 360.0f;
            if (angle_diff < -180.0f) angle_diff += 360.0f;
            
            // 只有当敌人在视野范围内（±90度）时才认为有可见敌人
            if (std::fabs(angle_diff) <= 90.0f) {
                has_spotted_enemy = true;
            }
        }
        
        // 调试：检查视角控制条件
        static int debug_counter = 0;
        if (++debug_counter % 60 == 0) {  // 每秒输出一次
            bool aimbot_scanning = (aimbot && aimbot->is_scanning_mode());
            bool aimbot_has_target = (aimbot && gi.closest_enemy_player && gi.closest_enemy_player->isSpotted);
            std::cout << "[MovementStrategy] Debug: has_spotted_enemy=" << has_spotted_enemy 
                      << ", aimbot_has_target=" << aimbot_has_target
                      << ", m_next_node=" << (m_next_node ? "exists" : "null")
                      << ", aimbot_scanning=" << aimbot_scanning << std::endl;
        }
        
        // 当没有可见敌人且Aimbot不在扫描模式时，使用完善的视角控制函数跟随移动方向
        // 修复：优先进行移动视角控制，除非Aimbot有明确的有效目标
        // 更准确地判断Aimbot是否真的有有效目标（不仅仅是isSpotted）
        bool aimbot_has_target = false;
        if (aimbot && gi.closest_enemy_player && gi.closest_enemy_player->isSpotted) {
            // 添加更多验证条件，模拟Aimbot的目标验证逻辑
            float distance = gi.controlled_player.head_position.distance(gi.closest_enemy_player->position);
            if (distance > 50.0f && distance < 2000.0f && gi.closest_enemy_player->health > 0) {
                aimbot_has_target = true;
            }
        }
        
        // 优先进行移动视角控制，除非Aimbot有明确的有效目标且不在扫描模式
        // 这样可以避免在正常走路时突然偏转进行aimbot
        if (m_next_node && (!aimbot || !aimbot->is_scanning_mode()) && 
            (!has_spotted_enemy || !aimbot_has_target)) {
            Vec3D<float> head_pos = gi.controlled_player.head_position;
            Vec3D<float> target_pos = m_next_node->position;
            
            // 调试：检查移动方向
            static int move_debug_counter = 0;
            if (++move_debug_counter % 60 == 0) {  // 每秒输出一次
                float move_distance = head_pos.distance(target_pos);
                std::cout << "[MovementStrategy] Move debug: move_distance=" << move_distance 
                          << ", next_node_pos=(" << target_pos.x << "," << target_pos.y << "," << target_pos.z << ")"
                          << ", head_pos=(" << head_pos.x << "," << head_pos.y << "," << head_pos.z << ")" << std::endl;
            }
            
            // 使用完善的视角控制函数，支持快速和平滑两种模式
            align_view_to(gi, head_pos, target_pos);
        }

        // -------- 额外卡住检测（重点加固）--------
        static Vec3D<float> anchor_pos;
        static auto anchor_time = SteadyClock::now();
        static bool anchor_active = false;

        constexpr float POS_DELTA = 10.0f;
        constexpr int   TIMEOUT_MS = 3000;

        float moved = player_pos.distance(anchor_pos);
        int   dtms = (int)std::chrono::duration_cast<std::chrono::milliseconds>(SteadyClock::now() - anchor_time).count();

        if (gi.controlled_player.health > 0) {
            if (!anchor_active) {
                anchor_pos = player_pos;
                anchor_time = SteadyClock::now();
                anchor_active = true;
            }
            else {
                if (dtms > TIMEOUT_MS && moved < POS_DELTA) {
                    // 安全筛选候选点：去空、去坐标无效、去当前/最近反复点/黑名单
                    std::vector<std::shared_ptr<Node>> candidates;
                    candidates.reserve(m_nodes.size());
                    for (const auto& n : m_nodes) {
                        if (!n) continue;
                        if (!finite3(n->position)) continue;
                        if (m_next_node && n->id == m_next_node->id) continue;
                        if (n->id == m_last_node_id || n->id == m_prev_node_id) continue;
                        auto itb = m_ban_until.find(n->id);
                        if (itb != m_ban_until.end() && itb->second > SteadyClock::now()) continue;
                        candidates.push_back(n);
                    }

                    if (!candidates.empty()) {
                        std::sort(candidates.begin(), candidates.end(),
                            [&player_pos](const std::shared_ptr<Node>& a, const std::shared_ptr<Node>& b) {
                                // 近到远
                                return a->position.distance(player_pos) < b->position.distance(player_pos);
                            });

                        // 限制尝试数量，避免极端情况下长时间卡在这儿
                        const size_t MAX_TRY = std::min<size_t>(candidates.size(), 64);
                        for (size_t i = 0; i < MAX_TRY; ++i) {
                            auto n = candidates[i];
                            auto start = get_closest_node_to_position(player_pos);
                            if (!start) break;
                            auto route = Dijkstra::get_route(start, n);
                            std::shared_ptr<Node> step = nullptr;
                            for (size_t j = 1; j < route.size(); ++j) {
                                if (route[j]) { step = route[j]; break; }
                            }
                            if (step) {
                                m_current_route = route;
                                m_next_node = step;
                                break;
                            }
                        }
                    }

                    anchor_pos = player_pos;
                    anchor_time = SteadyClock::now();
                }
                else if (moved >= POS_DELTA) {
                    anchor_pos = player_pos;
                    anchor_time = SteadyClock::now();
                }
            }
        }
        else {
            anchor_active = false;
        }
    }
    catch (const std::exception& e) {
        m_next_node = nullptr;
        if (game_info_handler) game_info_handler->set_player_movement(Movement{});
        Logging::log_error(e.what());
    }
    catch (...) {
        m_next_node = nullptr;
        if (game_info_handler) game_info_handler->set_player_movement(Movement{});
        Logging::log_error("Unknown C++ exception in MovementStrategy::update");
    }

    m_in_update = false;
}

// ============== 鼠标相对移动 ==============
static void move_mouse_relative(float dx, float dy)
{
#ifdef _WIN32
    if (dx == 0.0f && dy == 0.0f) return;
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dx = static_cast<LONG>(dx);
    input.mi.dy = static_cast<LONG>(dy);
    input.mi.dwFlags = MOUSEEVENTF_MOVE;
    SendInput(1, &input, sizeof(input));
#else
    (void)dx; (void)dy;
#endif
}

// 仅在最近敌人 isSpotted != true 时启用；使用 Aimbot 的 FAST 模式参数
void MovementStrategy::align_view_to(
    const GameInformation& gi,
    const Vec3D<float>& from_head_pos,
    const Vec3D<float>& to_pos)
{
    // 入参校验：只在没有isSpotted=true敌人时执行视角控制
    // 这个检查现在由调用方（MovementStrategy::update）处理

    if (!finite3(from_head_pos) || !finite3(to_pos)) return;

    // Debug logging: print vectors involved in aligning the view
    {
        std::ostringstream oss;
        oss << "[MovementStrategy] align_view_to from=(" << from_head_pos.x << "," << from_head_pos.y << "," << from_head_pos.z << ")";
        oss << " to=(" << to_pos.x << "," << to_pos.y << "," << to_pos.z << ")";
        debug_log(oss.str());
    }

    Vec3D<float> dir = to_pos - from_head_pos;
    if (!std::isfinite(dir.x) || !std::isfinite(dir.y)) return;

    float target_yaw = std::atan2(dir.y, dir.x) * 180.0f / static_cast<float>(M_PI) + 180.0f;
    if (target_yaw >= 360.0f) target_yaw -= 360.0f;
    if (target_yaw < 0.0f)    target_yaw += 360.0f;

    float current_yaw = gi.controlled_player.view_vec.y;
    float current_pitch = gi.controlled_player.view_vec.x;

    float dx_raw = target_yaw - current_yaw;
    if (dx_raw > 180.0f)  dx_raw -= 360.0f;
    if (dx_raw < -180.0f) dx_raw += 360.0f;

    // 计算俯仰角调整：保持水平状态（pitch = 0）
    float target_pitch = 0.0f;
    float dy_raw = target_pitch - current_pitch;

    // ---------------------------------------------------------------------------------
    // 使用借鉴自 Aimbot 的瞄准逻辑：根据误差大小在快速模式和平滑模式之间切换。
    // 当 yaw 误差较大时，采用快速模式以最大幅度快速调整；当误差较小时，采用
    // 时间相关的平滑模式逐渐逼近目标，减小抖动。
    // ---------------------------------------------------------------------------------

    float error_mag = std::fabs(dx_raw);

    // 静态变量用于跨帧保存上一次更新时间和平滑后的 dx 和 dy
    static auto last_time = SteadyClock::now();
    static float smoothed_dx = 0.0f;
    static float smoothed_dy = 0.0f;

    // 快速模式阈值和参数：依据 Aimbot 的设定
    // 定义进入和退出快速模式的两个阈值：当误差位于 [FAST_ENTER_THRESHOLD, FAST_MAX_ENTER_THRESHOLD]
    // 区间内时启用快速模式，否则使用平滑模式。通过设置非零的 FAST_ENTER_THRESHOLD 可避免误差极小
    // 时持续抖动。
    constexpr float FAST_ENTER_THRESHOLD = 2.0f;    // 误差低于此值时不启用快速模式
    constexpr float FAST_MAX_ENTER_THRESHOLD = 140.0f; // 误差高于此值时也不启用快速模式（极端情况）
    constexpr float FAST_MAX_STEP = 10.0f;       // 对应 Aimbot::FAST_MAX_STEP
    constexpr float FAST_SENSITIVITY = 16.0f;    // 对应 Aimbot::FAST_SENSITIVITY
    // 快速模式下最大像素移动量，按照 FAST_MAX_STEP * FAST_SENSITIVITY 计算
    constexpr float MAX_PIXEL_MOVE_FAST = FAST_MAX_STEP * FAST_SENSITIVITY; // 约 160 像素

    // 平滑模式参数：依据 Aimbot::SMOOTHING_TIME 与 MOUSE_SENSITIVITY
    constexpr float SMOOTHING_TIME = 0.13f;      // 对应 Aimbot::SMOOTHING_TIME
    constexpr float MOUSE_SENSITIVITY = 16.0f;   // 对应 Aimbot::MOUSE_SENSITIVITY
    // 平滑模式角度步进上限，对应 Aimbot::MAX_STEP，用于限制每帧可偏移的角度幅度
    constexpr float MAX_STEP = 1.2f;
    // 平滑模式下的最大鼠标位移。为了避免平滑模式在大误差时位移过大导致抖动，这里设定为
    // 快速模式上限的四分之一，可根据实际情况微调。例如 FAST_MAX_STEP*FAST_SENSITIVITY/4 ≈ 40
    constexpr float MAX_PIXEL_MOVE_SMOOTH = (MAX_STEP * MOUSE_SENSITIVITY);

    auto now_tp = SteadyClock::now();

    if (error_mag >= FAST_ENTER_THRESHOLD && error_mag <= FAST_MAX_ENTER_THRESHOLD) {
        // 快速模式：误差落在阈值区间内，直接按最大步进移动
        float dx_l = std::clamp(dx_raw, -FAST_MAX_STEP, FAST_MAX_STEP);
        float dx_fast = dx_l * FAST_SENSITIVITY;
        dx_fast = std::clamp(dx_fast, -MAX_PIXEL_MOVE_FAST, MAX_PIXEL_MOVE_FAST);
        
        // 俯仰角调整：保持水平状态
        float dy_l = std::clamp(dy_raw, -FAST_MAX_STEP, FAST_MAX_STEP);
        float dy_fast = dy_l * FAST_SENSITIVITY;
        dy_fast = std::clamp(dy_fast, -MAX_PIXEL_MOVE_FAST, MAX_PIXEL_MOVE_FAST);
        
        move_mouse_relative(dx_fast, dy_fast);
        // 重置平滑变量以避免模式切换时的跳跃
        smoothed_dx = dx_fast;
        smoothed_dy = dy_fast;
        last_time = now_tp;
    }
    else {
        // 平滑模式：误差在阈值范围外，使用时间相关的指数平滑
        double dt = std::chrono::duration<double>(now_tp - last_time).count();
        float alpha = static_cast<float>(dt / (SMOOTHING_TIME + dt));
        // 根据误差大小动态调整速度缩放，误差越大，移动速度越快
        float speedScale = std::clamp(error_mag / 45.0f, 0.5f, 1.5f);
        // 根据 MAX_STEP 限制每帧可偏移的角度幅度
        float dx_deg_step = std::clamp(dx_raw, -MAX_STEP * speedScale, MAX_STEP * speedScale);
        // 将角度步进转换为鼠标位移
        float dx_pix_step = dx_deg_step * MOUSE_SENSITIVITY;
        smoothed_dx += (dx_pix_step - smoothed_dx) * alpha;
        if (error_mag < 2.0f) {
            smoothed_dx *= 0.7f;
        }
        float dx_fast = std::clamp(smoothed_dx, -MAX_PIXEL_MOVE_SMOOTH, MAX_PIXEL_MOVE_SMOOTH);
        
        // 俯仰角平滑调整：保持水平状态
        float dy_deg_step = std::clamp(dy_raw, -MAX_STEP * speedScale, MAX_STEP * speedScale);
        float dy_pix_step = dy_deg_step * MOUSE_SENSITIVITY;
        smoothed_dy += (dy_pix_step - smoothed_dy) * alpha;
        if (std::fabs(dy_raw) < 2.0f) {
            smoothed_dy *= 0.7f;
        }
        float dy_fast = std::clamp(smoothed_dy, -MAX_PIXEL_MOVE_SMOOTH, MAX_PIXEL_MOVE_SMOOTH);
        
        move_mouse_relative(dx_fast, dy_fast);
        last_time = now_tp;
    }
}

// ================== 地图加载 ==================
bool MovementStrategy::handle_navmesh_load(const std::string& map_name)
{
    static const std::vector<std::string> invalid_maps = { "", "SNDLVL_35dB" };

    auto is_invalid = [&](const std::string& m) {
        return std::find(invalid_maps.begin(), invalid_maps.end(), m) != invalid_maps.end();
        };

    static std::string pending_map;
    static auto pending_since = SteadyClock::now();

    if (map_name != pending_map) {
        pending_map = map_name;
        pending_since = SteadyClock::now();
    }
    auto stable_ms = std::chrono::duration_cast<std::chrono::milliseconds>(SteadyClock::now() - pending_since).count();
    if (stable_ms < 250) {
        return false; // 未稳定，不加载
    }

    if (is_invalid(pending_map)) {
        return false;
    }

    if (pending_map == m_loaded_map) return false; // 无变化

    m_loaded_map = pending_map;
    std::string processed = m_loaded_map;
    std::replace(processed.begin(), processed.end(), '/', '_');

    auto full = resolve_navmesh_path(processed);
    if (load_in_navmesh(full.string())) {
        m_valid_navmesh_loaded = true;
    }
    else {
        m_valid_navmesh_loaded = false;
    }
    return true; // 本帧进行了加载尝试（成功与否均算）
}

// ================== Navmesh 读取 ==================
bool MovementStrategy::load_in_navmesh(const std::string& filename)
{
    namespace fs = std::filesystem;
    try
    {
        fs::path p = fs::path(filename);
        std::error_code ec;
        fs::path abs;

        fs::path tmp = fs::weakly_canonical(p, ec);
        if (!ec) abs = tmp;
        else {
            ec.clear();
            fs::path tmp2 = fs::absolute(p, ec);
            abs = (!ec ? tmp2 : p);
        }

        bool exists = fs::exists(abs);
        uintmax_t fsize = 0;
        if (exists) {
            std::error_code ec2;
            fsize = fs::file_size(abs, ec2);
            if (ec2) fsize = 0;
        }
        if (!exists) {
            return false;
        }

        std::string bytes;
#ifdef _WIN32
        FILE* fp = _wfopen(abs.c_str(), L"rb");
        if (!fp) {
            return false;
        }
        if (fsize > 0) bytes.resize(static_cast<size_t>(fsize));
        size_t rd = bytes.empty() ? 0 : fread(bytes.data(), 1, bytes.size(), fp);
        fclose(fp);
        if (fsize > 0 && rd != bytes.size()) {
            return false;
        }
        if (fsize == 0) {
            std::ifstream ifs(abs, std::ios::binary);
            if (!ifs.is_open()) {
                return false;
            }
            bytes.assign(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>());
            ifs.close();
        }
#else
        std::ifstream ifs(abs, std::ios::binary);
        if (!ifs.is_open()) {
            return false;
        }
        bytes.assign(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>());
        ifs.close();
#endif

        m_navmesh_json = nlohmann::json::parse(bytes, nullptr, /*allow_exceptions=*/false, /*ignore_comments=*/true);
        if (m_navmesh_json.is_discarded()) {
            return false;
        }

        m_nodes.clear();
        m_current_route.clear();
        m_next_node = nullptr;

        load_nodes(m_navmesh_json);
        load_edges(m_navmesh_json);

        if (m_nodes.empty()) {
            return false;
        }
    }
    catch (const std::exception& e)
    {
        Logging::log_error(e.what());
        return false;
    }
    catch (...) {
        return false;
    }
    return true;
}

// ================== 调试接口 ==================
void MovementStrategy::set_debug_print_route(bool value)
{
    m_debug_print_route = value;
}

void MovementStrategy::reset_loaded_navmesh()
{
    m_valid_navmesh_loaded = false;
    m_loaded_map.clear();
}

bool MovementStrategy::is_valid_navmesh_loaded() const
{
    return m_valid_navmesh_loaded;
}

// 计算移动时避免“倒退走”：>120° 时先以侧移为主/或停走
Movement MovementStrategy::calculate_move_info(
    const GameInformation& game_info,
    const std::shared_ptr<Node> node)
{
    Movement stop{};
    if (!node) return stop;

    // 添加移动暂停机制（反检测）
    auto now = std::chrono::steady_clock::now();
    if (m_movement_paused) {
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - m_last_movement_pause).count() > 50) {
            m_movement_paused = false;
        } else {
            return stop; // 暂停期间不移动
        }
    }
    
    // 随机暂停（5%概率暂停50ms）
    if (m_movement_pause_dist(m_movement_rng) < 5) {
        m_movement_paused = true;
        m_last_movement_pause = now;
        return stop;
    }

    const auto& head = game_info.controlled_player.head_position;
    const auto& tgt = node->position;

    if (!finite3(head) || !finite3(tgt)) return stop;

    float pos_ang = calc_angle_between_two_positions(head, tgt); // [-180,180]
    pos_ang = norm_deg_0_360(pos_ang);

    float view_yaw = norm_deg_0_360(game_info.controlled_player.view_vec.y);

    float d = calc_walk_angle(view_yaw, pos_ang); // [0,360]
    float signed_d = (d > 180.0f) ? (d - 360.0f) : d;

    // 添加角度随机化，减少过于精确的移动
    float angle_noise = m_movement_noise_dist(m_movement_rng) * 10.0f; // ±1度随机化
    signed_d += angle_noise;

    // 提高侧移阈值，减少过于频繁的AD晃动
    if (std::fabs(signed_d) > 150.0f) { // 进一步提高到150度
        Movement m{};
        if (signed_d > 0) m.left = true; else m.right = true;
        
        // 添加随机性：50%概率不进行侧移，而是停止
        if (m_movement_pause_dist(m_movement_rng) < 50) {
            return stop;
        }
        
        // 即使进行侧移，也有20%概率只侧移很短时间
        if (m_movement_pause_dist(m_movement_rng) < 20) {
            // 添加短暂侧移标记
            static auto last_short_side_move = std::chrono::steady_clock::now();
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_short_side_move).count() > 200) {
                last_short_side_move = now;
                return m; // 允许短暂侧移
            } else {
                return stop; // 侧移时间太短，停止
            }
        }
        
        return m;
    }

    Movement result = get_movement_from_walking_angle(d);
    
    // 添加移动随机化：偶尔不按完美角度移动
    if (m_movement_pause_dist(m_movement_rng) < 15) { // 15%概率
        // 随机移除一个移动方向，让移动看起来更自然
        if (result.forward && result.left && m_movement_pause_dist(m_movement_rng) < 50) {
            result.left = false;
        } else if (result.forward && result.right && m_movement_pause_dist(m_movement_rng) < 50) {
            result.right = false;
        } else if (result.backward && result.left && m_movement_pause_dist(m_movement_rng) < 50) {
            result.left = false;
        } else if (result.backward && result.right && m_movement_pause_dist(m_movement_rng) < 50) {
            result.right = false;
        }
    }
    
    return result;
}

float MovementStrategy::calc_angle_between_two_positions(
    const Vec3D<float>& pos1,
    const Vec3D<float>& pos2) const
{
    Vec3D<float> v = pos2 - pos1;
    if (!std::isfinite(v.x) || !std::isfinite(v.y)) return 0.0f;
    return (std::atan2(v.y, v.x) / static_cast<float>(M_PI) * 180.0f); // [-180,180]
}

float MovementStrategy::calc_walk_angle(float view_angle, float pos_angle) const
{
    float va = norm_deg_0_360(view_angle);
    float pa = norm_deg_0_360(pos_angle);
    float d = pa - va;
    if (d < 0.0f) d += 360.0f;
    return d; // [0,360]
}

Movement MovementStrategy::get_movement_from_walking_angle(float ang)
{
    Movement m{};
    
    // 添加角度随机化，让移动边界不那么精确
    float angle_noise = m_movement_noise_dist(m_movement_rng) * 5.0f; // ±0.5度随机化
    ang += angle_noise;
    
    // 确保角度在有效范围内
    if (ang < 0.0f) ang += 360.0f;
    if (ang >= 360.0f) ang -= 360.0f;
    
    // 0° 前、90° 左、180° 后、270° 右
    // 扩大角度范围，减少过于精确的移动
    if (ang > 340.0f || ang <= 20.0f) m.forward = true;
    else if (ang <= 70.0f)                 m.forward = m.left = true;
    else if (ang <= 110.0f)                m.left = true;
    else if (ang <= 160.0f)                m.left = m.backward = true;
    else if (ang <= 200.0f)                m.backward = true;
    else if (ang <= 250.0f)                m.backward = m.right = true;
    else if (ang <= 290.0f)                m.right = true;
    else                                   m.right = m.forward = true;
    
    // 添加随机性：偶尔不进行完美的对角线移动
    if (m.forward && m.left && m_movement_pause_dist(m_movement_rng) < 20) {
        m.left = false; // 20%概率只向前
    } else if (m.forward && m.right && m_movement_pause_dist(m_movement_rng) < 20) {
        m.right = false; // 20%概率只向前
    } else if (m.backward && m.left && m_movement_pause_dist(m_movement_rng) < 20) {
        m.left = false; // 20%概率只向后
    } else if (m.backward && m.right && m_movement_pause_dist(m_movement_rng) < 20) {
        m.right = false; // 20%概率只向后
    }
    
    return m;
}

// ================== 宽松 JSON 读取 ==================
void MovementStrategy::load_nodes(const json& js)
{
    try {
        if (!js.contains("nodes") || !js["nodes"].is_array()) {
            m_nodes.clear();
            return;
        }

        m_nodes.clear();
        for (const auto& n : js["nodes"]) {
            if (!n.contains("id") || !n.contains("x") || !n.contains("y") || !n.contains("z"))
                continue;

            int id = -1; float x = 0, y = 0, z = 0;
            try { id = n.at("id").get<int>(); }
            catch (...) { continue; }
            try { x = n.at("x").get<float>(); y = n.at("y").get<float>(); z = n.at("z").get<float>(); }
            catch (...) { continue; }

            m_nodes.push_back(std::make_shared<Node>(id, Vec3D<float>{x, y, z}));
        }
    }
    catch (const std::exception& e) {
        Logging::log_error(e.what());
        m_nodes.clear();
        return;
    }
    catch (...) {
        m_nodes.clear();
        return;
    }
}

void MovementStrategy::load_edges(const json& js)
{
    try {
        if (!js.contains("edges") || !js["edges"].is_array()) {
            return;
        }

        for (const auto& e : js["edges"]) {
            if (!e.contains("from") || !e.contains("to") || !e.contains("weight")) continue;

            int from_id = -1, to_id = -1; float w = 0.0f;
            try {
                from_id = e.at("from").get<int>();
                to_id = e.at("to").get<int>();
                w = e.at("weight").get<float>();
            }
            catch (...) { continue; }

            auto from = get_node_by_id(from_id);
            auto to = get_node_by_id(to_id);
            if (from && to) {
                from->edges.push_back(Node::Edge{ w, to });
            }
        }
    }
    catch (const std::exception& e) {
        Logging::log_error(e.what());
        return;
    }
    catch (...) {
        return;
    }
}

std::shared_ptr<Node> MovementStrategy::get_node_by_id(int id) const
{
    for (const auto& n : m_nodes)
        if (n && n->id == id) return n;
    return nullptr;
}

std::shared_ptr<Node> MovementStrategy::get_closest_node_to_position(
    const Vec3D<float>& pos)
{
    std::shared_ptr<Node> best = nullptr;
    float bestd = FLT_MAX;
    for (const auto& n : m_nodes)
    {
        if (!n) continue;
        float d = n->position.distance(pos);
        if (d < bestd) { bestd = d; best = n; }
    }
    return best;
}
