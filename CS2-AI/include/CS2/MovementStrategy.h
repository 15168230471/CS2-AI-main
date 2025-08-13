#pragma once
#define _USE_MATH_DEFINES
#include <math.h>
#include <fstream>
#include <iostream>
#include <memory>
#include <Windows.h>
#include "Utility/Utility.h"
#include "Utility/Vec3D.h"
#include "Utility/json.hpp"
#include "GameInformationHandler.h"
#include "Utility/Logging.h"
#include "Utility/Dijkstra.h"
#include <chrono>

using nlohmann::json;

class MovementStrategy 
{
public:
	void update(GameInformationhandler* game_info_handler);
	void handle_navmesh_load(const std::string& map_name);
	bool load_in_navmesh(const std::string& filename);
	void set_debug_print_route(bool value);
	void reset_loaded_navmesh();
	bool is_valid_navmesh_loaded() const;

private:
	std::shared_ptr<Node> get_node_by_id(int id) const;
	Movement calculate_move_info(const GameInformation& game_info, const std::shared_ptr<Node> node);
	float calc_angle_between_two_positions(const Vec3D<float>& pos1, const Vec3D<float>& pos2) const;
	float calc_walk_angle(float view_angle, float position_angle) const;
	Movement get_movement_from_walking_angle(float walking_angle) const;
	void load_nodes(const json& json);
	void load_edges(const json& json);
	std::shared_ptr<Node> get_closest_node_to_position(const Vec3D<float>& position);
	std::chrono::steady_clock::time_point m_last_shoot_time = std::chrono::steady_clock::now();
	int m_stop_after_shoot_ms = 0; // 射击后多久恢复移动，单位ms，可调
	json m_navmesh_json;
	std::vector<std::shared_ptr<Node>> m_nodes;
	std::shared_ptr<Node> m_next_node = nullptr;
	std::vector<std::shared_ptr<Node>> m_current_route;
	std::string m_loaded_map;
	bool m_valid_navmesh_loaded;
	long long m_delay_time = 0;
	bool m_debug_print_route = false;
	// === 新增：工具方法声明 ===
	std::shared_ptr<Node> pick_reachable_goal_node_fallback(
		const Vec3D<float>& enemy_pos,
		const std::shared_ptr<Node>& start_node,
		const std::shared_ptr<Node>& end_node);

	bool should_replan_due_to_no_progress(const Vec3D<float>& player_pos);

	void align_view_to(GameInformationhandler* game_info_handler,
		const Vec3D<float>& from_head_pos,
		const Vec3D<float>& to_pos);

	// === 新增：进度锚点成员（steady_clock！）===
	Vec3D<float> m_progress_anchor_pos{ 0.f, 0.f, 0.f };
	std::chrono::steady_clock::time_point m_progress_anchor_time{};

};