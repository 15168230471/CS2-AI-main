#pragma once
#define _USE_MATH_DEFINES
#include <math.h>
#include <fstream>
#include <iostream>
#include <memory>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <chrono>
#include <vector>
#include <string>
#include <unordered_map>

#include "Utility/Utility.h"
#include "Utility/Vec3D.h"
#include "Utility/json.hpp"
#include "CS2/GameInformationhandler.h"
#include "Utility/Logging.h"
#include "Utility/Dijkstra.h"

using nlohmann::json;

class MovementStrategy
{
public:
    // 主循环
    void update(GameInformationhandler* game_info_handler, const class Aimbot* aimbot = nullptr);

    // 返回：是否本帧发生了加载/重载（成功或失败都返回 true）
    bool handle_navmesh_load(const std::string& map_name);

    bool load_in_navmesh(const std::string& filename);
    void set_debug_print_route(bool value);
    void reset_loaded_navmesh();
    bool is_valid_navmesh_loaded() const;

private:
    // 工具/算法
    std::shared_ptr<Node> get_node_by_id(int id) const;
    Movement calculate_move_info(const GameInformation& game_info, const std::shared_ptr<Node> node);
    float calc_angle_between_two_positions(const Vec3D<float>& pos1, const Vec3D<float>& pos2) const;
    float calc_walk_angle(float view_angle, float position_angle) const;
    Movement get_movement_from_walking_angle(float walking_angle) const;
    void load_nodes(const json& json);
    void load_edges(const json& json);
    std::shared_ptr<Node> get_closest_node_to_position(const Vec3D<float>& position);

    // 可达目标兜底
    std::shared_ptr<Node> pick_reachable_goal_node_fallback(
        const Vec3D<float>& enemy_pos,
        const std::shared_ptr<Node>& start_node,
        const std::shared_ptr<Node>& end_node);

    // 卡住重规划
    bool should_replan_due_to_no_progress(const Vec3D<float>& player_pos);

    // 用同一帧的 GameInformation 快照，避免竞态
    void align_view_to(const GameInformation& gi,
        const Vec3D<float>& from_head_pos,
        const Vec3D<float>& to_pos);

    // 安全 LONG 转换（饱和 + 有限数检查）
    static long sat_long_from_float(float v, long limit = 32760L);

private:
    // === 运行状态 ===
    std::chrono::steady_clock::time_point m_last_shoot_time = std::chrono::steady_clock::now();
    int  m_stop_after_shoot_ms = 0;            // 射击后多久恢复移动（ms）
    json m_navmesh_json;

    std::vector<std::shared_ptr<Node>> m_nodes;
    std::shared_ptr<Node>              m_next_node = nullptr;
    std::vector<std::shared_ptr<Node>> m_current_route;

    std::string m_loaded_map;
    bool        m_valid_navmesh_loaded = false;

    long long m_delay_time = 0;
    bool      m_debug_print_route = false;

    // === 进度锚点（steady_clock）===
    Vec3D<float> m_progress_anchor_pos{ 0.f, 0.f, 0.f };
    std::chrono::steady_clock::time_point m_progress_anchor_time{};

    // === 抖动抑制 / 黑名单（成员变量版）===
    int  m_last_node_id = -1;
    int  m_prev_node_id = -1;
    int  m_oscillation_streak = 0;
    std::chrono::steady_clock::time_point m_last_switch_time{};
    std::unordered_map<int, std::chrono::steady_clock::time_point> m_ban_until;

    // === 视角对齐限流 ===
    std::chrono::steady_clock::time_point m_last_align_time{};
    int  m_align_min_interval_ms = 5;      // 最小调用间隔，避免过于频繁（~200Hz）
    float m_align_deadzone_deg = 0.02f; // 角差死区，小于它不动，避免抖动

    // === 重入保护 ===
    bool m_in_update = false;
};
