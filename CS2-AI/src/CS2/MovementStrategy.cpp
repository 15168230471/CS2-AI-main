#include "CS2/MovementStrategy.h"
#include "Utility/Utility.h"
#include "Utility/Vec3D.h"
#include "Utility/json.hpp"
#include "CS2/GameInformationhandler.h"  // 确认项目中的实际路径
#include "Utility/Logging.h"             // 仅用于错误日志
#include "Utility/Dijkstra.h"
#include <algorithm>    // std::replace
#include <fstream>
#include <Windows.h>
#include <iostream>     // std::cout
#include <cfloat>       // FLT_MAX
#include <cmath>        // std::atan2, M_PI
#include "CS2/Triggerbot.h"
using nlohmann::json;


void MovementStrategy::update(GameInformationhandler* game_info_handler)
{
    const GameInformation game_info = game_info_handler->get_game_information();
    const auto current_time_ms = get_current_time_in_ms();
    auto now = std::chrono::steady_clock::now();

    handle_navmesh_load(game_info.current_map);
    if (!m_valid_navmesh_loaded)
        return;

    if (g_just_fired) {
        m_last_shoot_time = now;
        g_just_fired = false;
        m_next_node = nullptr;
        game_info_handler->set_player_movement(Movement{});
        return;
    }

    int elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_last_shoot_time).count();
    if (elapsed < m_stop_after_shoot_ms) {
        m_next_node = nullptr;
        game_info_handler->set_player_movement(Movement{});
        return;
    }
    if (!game_info.closest_enemy_player) {
        m_next_node = nullptr;
        m_delay_time = current_time_ms + 1500;
        return;
    }
    if (!game_info.controlled_player.health) {
        m_next_node = nullptr;
        m_delay_time = current_time_ms + 1500;
        return;
    }
    if (current_time_ms < m_delay_time) {
        game_info_handler->set_player_movement(Movement{});
        return;
    }

    // 路径管理
    const Vec3D<float>& player_pos = game_info.controlled_player.position;
    const Vec3D<float>& enemy_pos = game_info.closest_enemy_player->position;
    constexpr float ARRIVAL_DIST = 15.0f;

    if (!m_next_node)
    {
        auto start_node = get_closest_node_to_position(player_pos);
        auto end_node = get_closest_node_to_position(enemy_pos);
        m_current_route = Dijkstra::get_route(start_node, end_node);
        if (m_current_route.size() > 1)
        {
            m_next_node = m_current_route[1];
            std::cout << " picked new route, next_node=" << m_next_node->id << "\n";
        }
        else
        {
            game_info_handler->set_player_movement(Movement{});
            return;
        }
    }

    float distance = m_next_node->position.distance(player_pos);
    Movement mv{};
    std::string status;

    if (distance > ARRIVAL_DIST) {
        mv = calculate_move_info(game_info, m_next_node);
        status = "MovingToNode";
    }
    else {
        m_next_node = nullptr;
        mv = Movement{};
    }
    game_info_handler->set_player_movement(mv);

    // --- 卡住检测并切换目标节点 ---
    static Vec3D<float> stuck_prev_pos;
    static auto stuck_prev_time = std::chrono::steady_clock::now();
    static bool stuck_timer_active = false;
    constexpr float STUCK_POS_DELTA = 10.0f;
    constexpr int STUCK_TIMEOUT_MS = 3000;

    float moved = player_pos.distance(stuck_prev_pos);
    int stuck_time = std::chrono::duration_cast<std::chrono::milliseconds>(now - stuck_prev_time).count();

    // 只要不是死亡都做卡住检测
    if (game_info.controlled_player.health > 0) {
        if (!stuck_timer_active) {
            stuck_prev_pos = player_pos;
            stuck_prev_time = now;
            stuck_timer_active = true;
        }
        else {
            if (stuck_time > STUCK_TIMEOUT_MS && moved < STUCK_POS_DELTA) {
                std::cout << "[Movement] STUCK! Picking far node..." << std::endl;

                // 按距离降序排列所有节点
                std::vector<std::shared_ptr<Node>> sorted_nodes = m_nodes;
                std::sort(sorted_nodes.begin(), sorted_nodes.end(),
                    [&player_pos](const std::shared_ptr<Node>& a, const std::shared_ptr<Node>& b) {
                        return a->position.distance(player_pos) > b->position.distance(player_pos);
                    });

                bool switched = false;
                for (const auto& n : sorted_nodes) {
                    if (!m_next_node || n->id != m_next_node->id) {
                        auto route = Dijkstra::get_route(get_closest_node_to_position(player_pos), n);
                        // 只要不是同一个点，并且路径大于一步
                        if (route.size() > 1 && (!m_next_node || route[1]->id != m_next_node->id)) {
                            m_current_route = route;
                            m_next_node = route[1];
                            std::cout << "[Movement] [Stuck] Picked new wander node: " << m_next_node->id << std::endl;
                            switched = true;
                            break;
                        }
                    }
                }
                if (!switched)
                    std::cout << "[Movement] WARNING: No suitable alternative node found!" << std::endl;
                stuck_prev_pos = player_pos;
                stuck_prev_time = now;
            }
            else if (moved >= STUCK_POS_DELTA) {
                stuck_prev_pos = player_pos;
                stuck_prev_time = now;
            }
        }
    }
    else {
        stuck_timer_active = false;
    }
}




// 其余方法保持不变……

void MovementStrategy::handle_navmesh_load(const std::string& map_name)
{
    // 过滤掉常见无效地图名和空字符串
    static const std::vector<std::string> invalid_maps = { "", "SNDLVL_35dB" };
    if (std::find(invalid_maps.begin(), invalid_maps.end(), map_name) != invalid_maps.end()) {
        m_loaded_map.clear();
        m_valid_navmesh_loaded = false;
        return;
    }
    if (map_name == m_loaded_map) return;
    m_loaded_map = map_name;
    std::string processed = map_name;
    std::replace(processed.begin(), processed.end(), '/', '_');
    std::string path = "Navmesh/json/" + processed + ".json";
    if (load_in_navmesh(path))
        m_valid_navmesh_loaded = true;
    else
        m_valid_navmesh_loaded = false;
}


bool MovementStrategy::load_in_navmesh(const std::string& filename)
{
    try
    {
        // 加载新文件前，清空旧内容
        m_nodes.clear();
        // 如果有 m_edges/m_current_route，也一并清空
        m_current_route.clear();
        m_next_node = nullptr;
        std::ifstream ifs(filename);
        m_navmesh_json = json::parse(ifs);
        ifs.close();
        load_nodes(m_navmesh_json);
        load_edges(m_navmesh_json);
       
        
    }
    catch (const std::exception& e)
    {
        Logging::log_error(e.what());
        return false;
    }
    return true;
}


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

Movement MovementStrategy::calculate_move_info(
    const GameInformation& game_info,
    const std::shared_ptr<Node> node)
{
    if (!game_info.closest_enemy_player) return {};

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
    else if (ang <= 112.5f)                 m.left = true;
    else if (ang <= 157.5f)                 m.left = m.backward = true;
    else if (ang <= 202.5f)                 m.backward = true;
    else if (ang <= 247.5f)                 m.backward = m.right = true;
    else if (ang <= 292.5f)                 m.right = true;
    else                                    m.right = m.forward = true;
    return m;
}

void MovementStrategy::load_nodes(const json& js)
{
    for (const auto& n : js["nodes"])
    {
        Vec3D<float> pos{ n["x"], n["y"], n["z"] };
        m_nodes.push_back(std::make_shared<Node>(n["id"], pos));
    }
}

void MovementStrategy::load_edges(const json& js)
{
    for (const auto& e : js["edges"])
    {
        auto from = get_node_by_id(e["from"]);
        auto to = get_node_by_id(e["to"]);
        if (from && to)
            from->edges.push_back(Node::Edge{ e["weight"], to });
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
