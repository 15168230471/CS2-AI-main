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
    //// —— 前置检查 & 日志 —— 
    //std::cout << "=== MovementStrategy::update ===\n";
    //std::cout << " time_ms=" << current_time_ms
    //    << " | delay_time=" << m_delay_time << "\n";
    //std::cout << " map=" << game_info.current_map
    //    << " | navmesh_loaded=" << (m_valid_navmesh_loaded ? "yes" : "no") << "\n";

    handle_navmesh_load(game_info.current_map);
    if (!m_valid_navmesh_loaded)
    {
        /*std::cout << " Status: NoNavMesh\n\n";*/
        return;
    }
    /*std::cout << "[DEBUG] shooting = " << g_just_fired << std::endl;*/
    if (g_just_fired) {
        m_last_shoot_time = now;
        /*std::cout << "[DEBUG] Player is shooting, stop moving." << std::endl;*/
        g_just_fired = false; // 用完清零
        m_next_node = nullptr;
        game_info_handler->set_player_movement(Movement{});
        return;
    }

    // 增加射击后缓冲区
    int elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_last_shoot_time).count();
    if (elapsed < m_stop_after_shoot_ms) {
        /*std::cout << "[DEBUG] Player shot recently (" << elapsed << "ms ago), keep stopped." << std::endl;*/
        m_next_node = nullptr;
        game_info_handler->set_player_movement(Movement{});
        //INPUT keyDown = {};
        //keyDown.type = INPUT_KEYBOARD;
        //keyDown.ki.wVk = 'R';
        //keyDown.ki.dwFlags = 0;
        //SendInput(1, &keyDown, sizeof(INPUT));

        //// 随机短延迟（按住）
        //Sleep(35 + rand() % 40); // 35~75ms

        //// 松开
        //INPUT keyUp = {};
        //keyUp.type = INPUT_KEYBOARD;
        //keyUp.ki.wVk = 'R';
        //keyUp.ki.dwFlags = KEYEVENTF_KEYUP;
        //SendInput(1, &keyUp, sizeof(INPUT));
        return;
    }
    if (!game_info.closest_enemy_player)
    {
        m_next_node = nullptr;
        m_delay_time = current_time_ms + 1500;
        /*std::cout << " Status: NoEnemy_EnteringDelay\n\n";*/
        return;
    }

    if (!game_info.controlled_player.health)
    {
        m_next_node = nullptr;
        m_delay_time = current_time_ms + 1500;
        /*std::cout << " Status: Dead_EnteringDelay\n\n";*/
        return;
    }

    if (current_time_ms < m_delay_time)
    {
        /*std::cout << " Status: InDelay\n\n";*/
        game_info_handler->set_player_movement(Movement{});
        return;
    }

    // —— 路径管理 —— 
    const Vec3D<float>& player_pos = game_info.controlled_player.position;
    const Vec3D<float>& enemy_pos = game_info.closest_enemy_player->position;
    constexpr float ARRIVAL_DIST = 15.0f;

    // 1) 首次或上条路径走完：选 A, B 并计算全程
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
            // 没路可走
            /*std::cout << " Status: NoPath_Stuck\n\n";*/
            game_info_handler->set_player_movement(Movement{});
            return;
        }
    }

    // 2) 计算到当前目标的距离
    float distance = m_next_node->position.distance(player_pos);
    /*std::cout << " player_pos=("
        << player_pos.x << "," << player_pos.y << "," << player_pos.z << ")\n";
    std::cout << " distance to next_node(" << m_next_node->id << ")=" << distance << "\n";*/

    // —— 移动或换点 —— 
    Movement mv{};
    std::string status;

    if (distance > ARRIVAL_DIST)
    {
        // 正在向目标走
        mv = calculate_move_info(game_info, m_next_node);
        status = "MovingToNode";
    }
    else
    {
        // 到达当前节点，推进到下一个
        if (m_current_route.size() > 2)
        {
            // 抛弃已到达的头，新的 [1] 成为 next_node
            m_current_route.erase(m_current_route.begin());
            m_next_node = m_current_route[1];
            status = "SwitchingNode_to_" + std::to_string(m_next_node->id);
        }
        else
        {
            // 最后一个节点也到达，停下并清空
            status = "PathComplete_Stop";
            m_next_node = nullptr;
        }
        // 到达瞬间停一帧
        mv = Movement{};
    }

    // 3) 发指令 & 打状态
    /*std::cout << " move flags: "
        << (mv.forward ? "F" : "_")
        << (mv.backward ? "B" : "_")
        << (mv.left ? "L" : "_")
        << (mv.right ? "R" : "_") << "\n";
    std::cout << " Status: " << status << "\n\n";*/

    game_info_handler->set_player_movement(mv);
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
