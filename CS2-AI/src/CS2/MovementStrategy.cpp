#include "CS2/MovementStrategy.h" 
#include "Utility/Utility.h"
#include "Utility/Vec3D.h"
#include "Utility/json.hpp"
#include "CS2/GameInformationhandler.h"
#include "Utility/Logging.h"
#include "Utility/Dijkstra.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <cfloat>
#include <cmath>
#include <chrono>
#include <sstream>
#include <vector>
#include <filesystem>
#include <cstdio>
#include <cerrno>
#include <system_error>
#include <queue>
#include <optional>
#include <unordered_map>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifdef _WIN32
#include <windows.h>
#endif

#include "CS2/Triggerbot.h"

using nlohmann::json;

// ================== 打印小工具 ==================
static inline std::string now_ms_str() {
    using namespace std::chrono;
    const auto ms = duration_cast<std::chrono::milliseconds>(steady_clock::now().time_since_epoch()).count();
    return std::to_string(ms);
}
template<typename T>
static inline std::string vec3_to_str(const Vec3D<T>& v) {
    std::ostringstream oss;
    oss << "(" << v.x << ", " << v.y << ", " << v.z << ")";
    return oss.str();
}
static inline void mv_log(const std::string& tag, const std::string& msg) {
    std::cout << "[" << now_ms_str() << "] " << tag << " " << msg << std::endl;
}

// ================== 路径解析（支持 CWD / EXE / 环境变量） ==================
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
        return n ? n->position.distance(enemy_pos) : FLT_MAX;
        };

    // 先尝试：end_node 的邻居里找更靠近敌人的点
    if (end_node) {
        std::vector<std::shared_ptr<Node>> candidates;
        for (auto& e : end_node->edges) {
            auto to_node = e.toNode; // 注意：使用 toNode
            if (to_node && to_node->id != start_node->id) {
                candidates.push_back(to_node);
            }
        }
        std::sort(candidates.begin(), candidates.end(),
            [&](const std::shared_ptr<Node>& a, const std::shared_ptr<Node>& b) {
                return dist_to_enemy(a) < dist_to_enemy(b);
            });

        for (auto& c : candidates) {
            auto route = Dijkstra::get_route(start_node, c);
            if (route.size() > 1) {
                return c; // 找到最近且可达的邻居
            }
        }
    }

    // 兜底：全图按“离敌直线距离”排序，挑第一个可达且不是起点本身
    std::vector<std::shared_ptr<Node>> all = m_nodes;
    std::sort(all.begin(), all.end(),
        [&](const std::shared_ptr<Node>& a, const std::shared_ptr<Node>& b) {
            return dist_to_enemy(a) < dist_to_enemy(b);
        });
    for (auto& c : all) {
        if (!c || c->id == start_node->id) continue;
        auto route = Dijkstra::get_route(start_node, c);
        if (route.size() > 1) {
            return c;
        }
    }
    return nullptr;
}

// 监控“朝 m_next_node 的靠近进度”，若无进展则触发重规划
bool MovementStrategy::should_replan_due_to_no_progress(const Vec3D<float>& player_pos)
{
    using clock = std::chrono::steady_clock;
    constexpr int   REPLAN_TIMEOUT_MS = 1800;  // 1.8s 无明显进展则重规划
    constexpr float MIN_DELTA_MOVE = 14.0f;    // 进步阈值（距离减少）

    auto now = clock::now();
    if (!m_next_node) {
        m_progress_anchor_pos = player_pos;
        m_progress_anchor_time = now;
        return false;
    }

    float from_anchor = player_pos.distance(m_progress_anchor_pos);
    int   elapsed_ms = static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now - m_progress_anchor_time).count()
        );

    // 有进步就刷新锚点
    if (from_anchor >= MIN_DELTA_MOVE) {
        m_progress_anchor_pos = player_pos;
        m_progress_anchor_time = now;
        return false;
    }

    // 长时间没进步 → 建议重规划
    if (elapsed_ms > REPLAN_TIMEOUT_MS) {
        m_progress_anchor_pos = player_pos;
        m_progress_anchor_time = now;
        return true;
    }
    return false;
}

// ================ 核心：Update =================
void MovementStrategy::update(GameInformationhandler* game_info_handler)
{
    try {
        const GameInformation gi = game_info_handler->get_game_information();
        const auto now_ms = get_current_time_in_ms();
        auto now = std::chrono::steady_clock::now();

        // -------- 导航网格 --------
        handle_navmesh_load(gi.current_map);
        if (!m_valid_navmesh_loaded) {
            /*mv_log("[Navmesh]", std::string("blocked: navmesh not loaded for map='") + gi.current_map + "'");*/
            return;
        }

        // -------- 刚开火的停走保护 --------
        if (g_just_fired) {
            m_last_shoot_time = now;
            g_just_fired = false;
            m_next_node = nullptr;
            game_info_handler->set_player_movement(Movement{});
            return;
        }
        {
            int elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_last_shoot_time).count();
            int left = (std::max)(0, m_stop_after_shoot_ms - elapsed);
            if (left > 0) {
                m_next_node = nullptr;
                game_info_handler->set_player_movement(Movement{});
                return;
            }
        }

        // -------- 状态与延迟 --------
        static bool last_has_enemy = false;
        static bool last_alive = true;

        bool has_enemy = gi.closest_enemy_player.has_value();
        bool alive = (gi.controlled_player.health > 0);

        if (last_has_enemy && !has_enemy) {
            m_next_node = nullptr;
            m_delay_time = now_ms + 1200;
        }
        if (last_alive && !alive) {
            m_next_node = nullptr;
            m_delay_time = now_ms + 1200;
        }
        last_has_enemy = has_enemy;
        last_alive = alive;

        if (!has_enemy || !alive) {
            game_info_handler->set_player_movement(Movement{});
            return;
        }
        if (now_ms < m_delay_time) {
            game_info_handler->set_player_movement(Movement{});
            return;
        }

        // -------- 关键位置 --------
        const Vec3D<float>& player_pos = gi.controlled_player.position;
        const Vec3D<float>& enemy_pos = gi.closest_enemy_player->position;

        // -------- 路径规划（加入“同节点/无路兜底”）--------
        if (!m_next_node)
        {
            auto start_node = get_closest_node_to_position(player_pos);
            auto end_node = get_closest_node_to_position(enemy_pos);

            if (!start_node) {
                game_info_handler->set_player_movement(Movement{});
                return;
            }

            // 直接尝试去 end_node
            std::vector<std::shared_ptr<Node>> route;
            if (end_node) route = Dijkstra::get_route(start_node, end_node);

            // route.size()<=1 时，说明“同节点/无路/不可见”，挑选替代目标节点
            if (route.size() <= 1) {
                auto alt_goal = pick_reachable_goal_node_fallback(enemy_pos, start_node, end_node);
                if (alt_goal) {
                    m_current_route = Dijkstra::get_route(start_node, alt_goal);
                    if (m_current_route.size() > 1) {
                        m_next_node = m_current_route[1];
                    }
                    else {
                        m_next_node = nullptr;
                        game_info_handler->set_player_movement(Movement{});
                        return;
                    }
                }
                else {
                    // 实在找不到 → 从 start 的邻居里挑一个更靠近敌人的点
                    if (!start_node->edges.empty()) {
                        std::shared_ptr<Node> best = nullptr;
                        float bestd = FLT_MAX;
                        for (auto& e : start_node->edges) {
                            auto to_node = e.toNode;
                            if (to_node) {
                                float d = to_node->position.distance(enemy_pos);
                                if (d < bestd) { bestd = d; best = to_node; }
                            }
                        }
                        if (best) {
                            m_current_route = Dijkstra::get_route(start_node, best);
                            if (m_current_route.size() > 1) {
                                m_next_node = m_current_route[1];
                            }
                        }
                    }
                    if (!m_next_node) {
                        // 仍然没有 → 停走，但视角对齐“意向点”（敌人附近）
                        align_view_to(game_info_handler, player_pos, enemy_pos);
                        game_info_handler->set_player_movement(Movement{});
                        return;
                    }
                }
            }
            else {
                // 正常路
                m_current_route = route;
                m_next_node = (m_current_route.size() > 1 ? m_current_route[1] : nullptr);
                if (!m_next_node) {
                    align_view_to(game_info_handler, player_pos, enemy_pos);
                    game_info_handler->set_player_movement(Movement{});
                    return;
                }
            }

            // 初始化进度锚点
            m_progress_anchor_pos = player_pos;
            m_progress_anchor_time = std::chrono::steady_clock::now();
        }

        // -------- 进度监测：卡住/无进展则重规划 --------
        if (should_replan_due_to_no_progress(player_pos)) {
            m_next_node = nullptr; // 强制重新走上面的规划逻辑
            game_info_handler->set_player_movement(Movement{});
            return;
        }

        // -------- AB 来回抖动抑制 + 黑名单 --------
        using clock = std::chrono::steady_clock;
        static int last_node_id = -1;
        static int prev_node_id = -1;
        static auto last_switch_time = clock::now();
        static int oscillation_streak = 0; // A->B->A 记一次
        static std::unordered_map<int, clock::time_point> ban_until; // 短时禁用节点

        auto nowt = clock::now();
        auto is_banned = [&](int id) -> bool {
            auto it = ban_until.find(id);
            return it != ban_until.end() && it->second > nowt;
            };
        auto ban_node = [&](int id, int ms) {
            if (id >= 0) ban_until[id] = nowt + std::chrono::milliseconds(ms);
            };

        if (m_next_node) {
            int nid = m_next_node->id;

            if (nid != last_node_id) {
                // 节点发生切换
                if (nid == prev_node_id &&
                    std::chrono::duration_cast<std::chrono::milliseconds>(nowt - last_switch_time).count() < 10000) {
                    // 短时间 A->B->A
                    oscillation_streak++;
                }
                else {
                    oscillation_streak = 0;
                }
                prev_node_id = last_node_id;
                last_node_id = nid;
                last_switch_time = nowt;
            }

            // 连续两次抖动 → 短时禁用这两个节点并重算
            if (oscillation_streak >= 2) {
                ban_node(last_node_id, 2500);
                ban_node(prev_node_id, 2500);
                oscillation_streak = 0;

                m_current_route.clear();
                m_next_node = nullptr;
                game_info_handler->set_player_movement(Movement{});
                return;
            }

            // 当前 next 在黑名单 → 立刻丢弃并重算
            if (is_banned(nid)) {
                m_current_route.clear();
                m_next_node = nullptr;
                game_info_handler->set_player_movement(Movement{});
                return;
            }
        }

        // -------- 执行移动（迟滞 + 步进冷却，避免门口左右蹭）--------
        constexpr float ARRIVE_NEAR = 13.0f;    // 进入半径
        constexpr float ARRIVE_FAR = 22.0f;    // 离开半径（> 进入半径，形成迟滞）
        constexpr int   STEP_SWITCH_COOLDOWN_MS = 450;

        static int   arrive_node_id = -1;
        static bool  in_near = false;
        static auto  last_step_switch = clock::now();

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
                    // 进入“到达区”
                    if (std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - last_step_switch).count()
                        >= STEP_SWITCH_COOLDOWN_MS) {
                        // 允许切下一个
                        auto it = std::find(m_current_route.begin(), m_current_route.end(), m_next_node);
                        if (it != m_current_route.end() && (it + 1) != m_current_route.end()) {
                            m_next_node = *(it + 1);
                            last_step_switch = clock::now();
                            mv = calculate_move_info(gi, m_next_node);
                        }
                        else {
                            m_next_node = nullptr; // 下轮重算
                            mv = Movement{};
                        }
                        in_near = true; // 切换后设置为近区，需要明显离开才解除
                    }
                    else {
                        // 冷却期内不切换，避免刚好在墙角反复左右
                        mv = Movement{};
                    }
                }
            }
            else {
                // 已在“近区”，只有明显离开后才可再次进入，形成迟滞
                if (distance > ARRIVE_FAR) {
                    in_near = false;
                }
                // 近区内不给额外移动指令，维持当前
            }

            // 让视角对齐“行走目标节点”，避免盯着敌人坐标
            align_view_to(game_info_handler, gi.controlled_player.head_position, m_next_node->position);
        }

        game_info_handler->set_player_movement(mv);

        // -------- 额外卡住检测（位置长时间不变则“跳远点”，避开黑名单/来回节点） --------
        static Vec3D<float> anchor_pos;
        static auto anchor_time = clock::now();
        static bool anchor_active = false;

        constexpr float POS_DELTA = 10.0f; // “没动”的阈值
        constexpr int   TIMEOUT_MS = 3000;  // “卡住”的时间阈值

        float moved = player_pos.distance(anchor_pos);
        int   dtms = (int)std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - anchor_time).count();

        if (gi.controlled_player.health > 0) {
            if (!anchor_active) {
                anchor_pos = player_pos;
                anchor_time = clock::now();
                anchor_active = true;
            }
            else {
                if (dtms > TIMEOUT_MS && moved < POS_DELTA) {
                    // 选“更远、未被禁、不是当前/上一个”的跳转目标
                    std::vector<std::shared_ptr<Node>> sorted_nodes = m_nodes;
                    std::sort(sorted_nodes.begin(), sorted_nodes.end(),
                        [&player_pos](const std::shared_ptr<Node>& a, const std::shared_ptr<Node>& b) {
                            return a->position.distance(player_pos) > b->position.distance(player_pos);
                        });

                    for (const auto& n : sorted_nodes) {
                        if (!n) continue;
                        if (is_banned(n->id)) continue;
                        if (m_next_node && n->id == m_next_node->id) continue;
                        if (n->id == last_node_id || n->id == prev_node_id) continue;

                        auto start = get_closest_node_to_position(player_pos);
                        if (!start) break;

                        auto route = Dijkstra::get_route(start, n);
                        // 在 route 中找第一个未被禁用的“下一步”
                        std::shared_ptr<Node> step = nullptr;
                        for (size_t i = 1; i < route.size(); ++i) {
                            if (route[i] && !is_banned(route[i]->id)) { step = route[i]; break; }
                        }
                        if (step) {
                            m_current_route = route;
                            m_next_node = step;
                            break;
                        }
                    }

                    anchor_pos = player_pos;
                    anchor_time = clock::now();
                }
                else if (moved >= POS_DELTA) {
                    anchor_pos = player_pos;
                    anchor_time = clock::now();
                }
            }
        }
        else {
            anchor_active = false;
        }

    }
    catch (const std::exception& e) {
        /*mv_log("[Navmesh]", std::string("update() caught exception: ") + e.what());*/
        m_next_node = nullptr;
        if (game_info_handler) game_info_handler->set_player_movement(Movement{});
    }
    catch (...) {
        /*mv_log("[Navmesh]", "update() caught unknown exception");*/
        m_next_node = nullptr;
        if (game_info_handler) game_info_handler->set_player_movement(Movement{});
    }
}

// ================ 让视角朝向某点（避免盯着敌人不动） ================
void MovementStrategy::align_view_to(GameInformationhandler* game_info_handler,
    const Vec3D<float>& from_head_pos,
    const Vec3D<float>& to_pos)
{
    float pos_ang = calc_angle_between_two_positions(from_head_pos, to_pos);
    (void)game_info_handler;
    (void)pos_ang;
}

// ================== 地图加载（带消抖 + 初始化日志） ==================
void MovementStrategy::handle_navmesh_load(const std::string& map_name)
{
    using clock = std::chrono::steady_clock;
    static const std::vector<std::string> invalid_maps = { "", "SNDLVL_35dB" };

    auto is_invalid = [&](const std::string& m) {
        return std::find(invalid_maps.begin(), invalid_maps.end(), m) != invalid_maps.end();
        };

    static std::string pending_map;
    static auto pending_since = clock::now();

    if (map_name != pending_map) {
        pending_map = map_name;
        pending_since = clock::now();
        /*mv_log("[Navmesh]", std::string("map seen='") + pending_map + "', debouncing...");*/
    }
    auto stable_ms = std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - pending_since).count();
    if (stable_ms < 250) {
        return;
    }

    if (is_invalid(pending_map)) {
        /*mv_log("[Navmesh]", std::string("map invalid '") + pending_map + "' -> keep last navmesh");*/
        return;
    }

    if (pending_map == m_loaded_map) return;

    m_loaded_map = pending_map;
    std::string processed = m_loaded_map;
    std::replace(processed.begin(), processed.end(), '/', '_');

    auto full = resolve_navmesh_path(processed);
    /*mv_log("[Navmesh]", std::string("loading stable map='") + m_loaded_map + "' file='" + full.string() +
        "' (cwd='" + std::filesystem::current_path().string() + "')");*/
    if (load_in_navmesh(full.string())) {
        m_valid_navmesh_loaded = true;
        /*mv_log("[Navmesh]", std::string("loaded OK: nodes=") + std::to_string(m_nodes.size()));*/
    }
    else {
        m_valid_navmesh_loaded = false;
        /*mv_log("[Navmesh]", std::string("load FAILED for '") + full.string() + "'");*/
    }
}

// ================== Navmesh 读取（非抛异常解析 + 诊断） ==================
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

        auto cwd = fs::current_path().string();
        bool exists = fs::exists(abs);
        uintmax_t fsize = 0;
        if (exists) {
            std::error_code ec2;
            fsize = fs::file_size(abs, ec2);
            if (ec2) fsize = 0;
        }

        /*mv_log("[Navmesh]", std::string("probe file path: '") + abs.string() +
            "', cwd='" + cwd + "', exists=" + (exists ? "1" : "0") +
            ", size=" + std::to_string((unsigned long long)fsize));*/

        if (!exists) {
            /*mv_log("[Navmesh]", "file NOT FOUND (check map name / base dir / CWD / env CS2_NAVMESH_DIR)");*/
            return false;
        }

        std::string bytes;
#ifdef _WIN32
        FILE* fp = _wfopen(abs.c_str(), L"rb");
        if (!fp) {
            /*mv_log("[Navmesh]", "wfopen FAILED");*/
            return false;
        }
        if (fsize > 0) bytes.resize(static_cast<size_t>(fsize));
        size_t rd = bytes.empty() ? 0 : fread(bytes.data(), 1, bytes.size(), fp);
        fclose(fp);
        if (fsize > 0 && rd != bytes.size()) {
            /*mv_log("[Navmesh]", std::string("fread size mismatch: read=") + std::to_string(rd) +
                " expect=" + std::to_string(bytes.size()));*/
            return false;
        }
        if (fsize == 0) {
            std::ifstream ifs(abs, std::ios::binary);
            if (!ifs.is_open()) {
                /*mv_log("[Navmesh]", std::string("ifstream open FAILED (fallback)"));*/
                return false;
            }
            bytes.assign(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>());
            ifs.close();
        }
#else
        std::ifstream ifs(abs, std::ios::binary);
        if (!ifs.is_open()) {
            /*mv_log("[Navmesh]", std::string("ifstream open FAILED: errno=") + std::to_string(errno));*/
            return false;
        }
        bytes.assign(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>());
        ifs.close();
#endif

        m_navmesh_json = nlohmann::json::parse(bytes, nullptr, /*allow_exceptions=*/false, /*ignore_comments=*/true);
        if (m_navmesh_json.is_discarded()) {
            /*mv_log("[Navmesh]", "JSON parse FAILED (discarded).");*/
            return false;
        }

        // 读取节点/边
        m_nodes.clear();
        m_current_route.clear();
        m_next_node = nullptr;

        load_nodes(m_navmesh_json);
        load_edges(m_navmesh_json);

        if (m_nodes.empty()) {
            /*mv_log("[Navmesh]", "parsed JSON but nodes list is EMPTY.");*/
            return false;
        }

        /*mv_log("[Navmesh]", std::string("parsed OK: nodes=") + std::to_string(m_nodes.size()));*/
    }
    catch (const std::exception& e)
    {
        Logging::log_error(e.what());
        /*mv_log("[Navmesh]", std::string("exception: ") + e.what());*/
        return false;
    }
    catch (...) {
        /*mv_log("[Navmesh]", "unknown exception during load_in_navmesh()");*/
        return false;
    }
    return true;
}

// ================== 只开放初始化相关打印 ==================
void MovementStrategy::set_debug_print_route(bool value)
{
    m_debug_print_route = value;
}

void MovementStrategy::reset_loaded_navmesh()
{
    m_valid_navmesh_loaded = false;
    m_loaded_map.clear();
    /*mv_log("[Navmesh]", "reset_loaded_navmesh");*/
}

bool MovementStrategy::is_valid_navmesh_loaded() const
{
    return m_valid_navmesh_loaded;
}

Movement MovementStrategy::calculate_move_info(
    const GameInformation& game_info,
    const std::shared_ptr<Node> node)
{
    if (!node) return {};
    float pos_ang = calc_angle_between_two_positions(
        game_info.controlled_player.head_position, node->position);
    float walk_ang = calc_walk_angle(game_info.controlled_player.view_vec.y, pos_ang);
    return get_movement_from_walking_angle(walk_ang);
}

float MovementStrategy::calc_angle_between_two_positions(
    const Vec3D<float>& pos1,
    const Vec3D<float>& pos2) const
{
    Vec3D<float> v = pos2 - pos1;
    return (std::atan2(v.y, v.x) / static_cast<float>(M_PI) * 180.0f);
}

float MovementStrategy::calc_walk_angle(float view_angle, float pos_angle) const
{
    float d = pos_angle - view_angle;
    if (d < 0.0f) d += 360.0f;
    return d;
}

Movement MovementStrategy::get_movement_from_walking_angle(float ang) const
{
    Movement m{};
    if (ang > 337.5f || ang <= 22.5f) m.forward = true;
    else if (ang <= 67.5f)                 m.forward = m.left = true;
    else if (ang <= 112.5f)                m.left = true;
    else if (ang <= 157.5f)                m.left = m.backward = true;
    else if (ang <= 202.5f)                m.backward = true;
    else if (ang <= 247.5f)                m.backward = m.right = true;
    else if (ang <= 292.5f)                m.right = true;
    else                                   m.right = m.forward = true;
    return m;
}

// ================== 宽松且不抛异常的 JSON 读取 ==================
void MovementStrategy::load_nodes(const json& js)
{
    try {
        if (!js.contains("nodes") || !js["nodes"].is_array()) {
            /*mv_log("[Navmesh]", "load_nodes: 'nodes' missing or not an array.");*/
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
       /*mv_log("[Navmesh]", std::string("load_nodes count=") + std::to_string(m_nodes.size()));*/
    }
    catch (const std::exception& e) {
        Logging::log_error(e.what());
        /*mv_log("[Navmesh]", std::string("load_nodes exception: ") + e.what());*/
        m_nodes.clear();
        return;
    }
    catch (...) {
        /*mv_log("[Navmesh]", "load_nodes unknown exception");*/
        m_nodes.clear();
        return;
    }
}

void MovementStrategy::load_edges(const json& js)
{
    size_t edge_cnt = 0;
    try {
        if (!js.contains("edges") || !js["edges"].is_array()) {
            /*mv_log("[Navmesh]", "load_edges: 'edges' missing or not an array.");*/
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
                from->edges.push_back(Node::Edge{ w, to }); // Edge { weight, toNode }
                ++edge_cnt;
            }
        }
        /*mv_log("[Navmesh]", std::string("load_edges count=") + std::to_string(edge_cnt));*/
    }
    catch (const std::exception& e) {
        Logging::log_error(e.what());
        /*mv_log("[Navmesh]", std::string("load_edges exception: ") + e.what())*/;
        return;
    }
    catch (...) {
        /*mv_log("[Navmesh]", "load_edges unknown exception");*/
        return;
    }
}

std::shared_ptr<Node> MovementStrategy::get_node_by_id(int id) const
{
    for (const auto& n : m_nodes)
        if (n->id == id) return n;
    return nullptr;
}

std::shared_ptr<Node> MovementStrategy::get_closest_node_to_position(
    const Vec3D<float>& pos)
{
    std::shared_ptr<Node> best = nullptr;
    float bestd = FLT_MAX;
    for (const auto& n : m_nodes)
    {
        float d = n->position.distance(pos);
        if (d < bestd) { bestd = d; best = n; }
    }
    return best;
}
