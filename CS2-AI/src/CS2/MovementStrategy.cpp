#include "CS2/MovementStrategy.h"
#include "Utility/Utility.h"
#include "Utility/Vec3D.h"
#include "Utility/json.hpp"
#include "CS2/GameInformationhandler.h"  // ȷ����Ŀ�е�ʵ��·��
#include "Utility/Logging.h"             // �����ڴ�����־
#include "Utility/Dijkstra.h"
#include <algorithm>    // std::replace
#include <fstream>
#include <Windows.h>
#include <iostream>     // std::cout
#include <cfloat>       // FLT_MAX
#include <cmath>        // std::atan2, M_PI

using nlohmann::json;

void MovementStrategy::update(GameInformationhandler* game_info_handler)
{
    const GameInformation game_info = game_info_handler->get_game_information();
    const auto current_time_ms = get_current_time_in_ms();

    // ���� ǰ�ü�� & ��־ ���� 
    std::cout << "=== MovementStrategy::update ===\n";
    std::cout << " time_ms=" << current_time_ms
        << " | delay_time=" << m_delay_time << "\n";
    std::cout << " map=" << game_info.current_map
        << " | navmesh_loaded=" << (m_valid_navmesh_loaded ? "yes" : "no") << "\n";

    handle_navmesh_load(game_info.current_map);
    if (!m_valid_navmesh_loaded)
    {
        std::cout << " Status: NoNavMesh\n\n";
        return;
    }

    if (!game_info.closest_enemy_player)
    {
        m_next_node = nullptr;
        m_delay_time = current_time_ms + 1500;
        std::cout << " Status: NoEnemy_EnteringDelay\n\n";
        return;
    }

    if (!game_info.controlled_player.health)
    {
        m_next_node = nullptr;
        m_delay_time = current_time_ms + 1500;
        std::cout << " Status: Dead_EnteringDelay\n\n";
        return;
    }

    if (current_time_ms < m_delay_time)
    {
        std::cout << " Status: InDelay\n\n";
        game_info_handler->set_player_movement(Movement{});
        return;
    }

    // ���� ·������ ���� 
    const Vec3D<float>& player_pos = game_info.controlled_player.position;
    const Vec3D<float>& enemy_pos = game_info.closest_enemy_player->position;
    constexpr float ARRIVAL_DIST = 15.0f;

    // 1) �״λ�����·�����꣺ѡ A, B ������ȫ��
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
            // û·����
            std::cout << " Status: NoPath_Stuck\n\n";
            game_info_handler->set_player_movement(Movement{});
            return;
        }
    }

    // 2) ���㵽��ǰĿ��ľ���
    float distance = m_next_node->position.distance(player_pos);
    std::cout << " player_pos=("
        << player_pos.x << "," << player_pos.y << "," << player_pos.z << ")\n";
    std::cout << " distance to next_node(" << m_next_node->id << ")=" << distance << "\n";

    // ���� �ƶ��򻻵� ���� 
    Movement mv{};
    std::string status;

    if (distance > ARRIVAL_DIST)
    {
        // ������Ŀ����
        mv = calculate_move_info(game_info, m_next_node);
        status = "MovingToNode";
    }
    else
    {
        // ���ﵱǰ�ڵ㣬�ƽ�����һ��
        if (m_current_route.size() > 2)
        {
            // �����ѵ����ͷ���µ� [1] ��Ϊ next_node
            m_current_route.erase(m_current_route.begin());
            m_next_node = m_current_route[1];
            status = "SwitchingNode_to_" + std::to_string(m_next_node->id);
        }
        else
        {
            // ���һ���ڵ�Ҳ���ͣ�²����
            status = "PathComplete_Stop";
            m_next_node = nullptr;
        }
        // ����˲��ͣһ֡
        mv = Movement{};
    }

    // 3) ��ָ�� & ��״̬
    std::cout << " move flags: "
        << (mv.forward ? "F" : "_")
        << (mv.backward ? "B" : "_")
        << (mv.left ? "L" : "_")
        << (mv.right ? "R" : "_") << "\n";
    std::cout << " Status: " << status << "\n\n";

    game_info_handler->set_player_movement(mv);
}

// ���෽�����ֲ��䡭��

void MovementStrategy::handle_navmesh_load(const std::string& map_name)
{
    if (map_name == m_loaded_map) return;
    if (map_name.empty())
    {
        m_loaded_map.clear();
        m_valid_navmesh_loaded = false;
        return;
    }
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
