// CS2/Aimbot.h
#pragma once

#include <Windows.h>
#include <chrono>
#include <cmath>
#include <random>
#include "GameInformationhandler.h"
#include <unordered_map>



class Aimbot {
public:
    void update(GameInformationhandler* info_handler);
    
    // 检查是否处于扫描模式
    bool is_scanning_mode() const { return m_scanning_mode; }

    
private:
    // 鼠标移动
    void move_mouse(float dx, float dy);
    // 计算俯仰与偏航以瞄准指定位置
    Vec2D<float> calc_view_vec_aim_to_head(const Vec3D<float>& player_head,
        const Vec3D<float>& target_pos);
    // 目标运动预测
    Vec2D<float> predictTarget(float tgtX, float tgtY);

    // ===== SMOOTH 滤波状态 =====
    float m_smoothed_dx = 0.0f;
    float m_smoothed_dy = 0.0f;
    std::chrono::steady_clock::time_point m_last_time = std::chrono::steady_clock::now();

    // ===== 反应延迟 =====
    bool   m_reaction_pending = false;
    float  m_reaction_delay_ms = 0.0f;
    double m_reaction_start = 0.0;

    // ===== 随机生成 =====
    std::mt19937 m_rng{ std::random_device{}() };
    std::uniform_real_distribution<float> m_delay_dist{ 800.0f, 1600.0f };  // ms
    std::uniform_real_distribution<float> m_hit_head_dist{ 0.0f, 1.0f };   // 爆头概率分布

    // ===== 预测相关状态 =====
    float m_prevTargetX = 0.0f;
    float m_prevTargetY = 0.0f;
    float m_prevVelX = 0.0f;
    float m_prevVelY = 0.0f;
    float m_prevDist = 0.0f;
    std::chrono::steady_clock::time_point m_prevTime = std::chrono::steady_clock::now();

    // ===== 受伤后转视角逻辑 =====
    bool m_scanning_mode = false;                    // 是否处于转视角模式
    float m_scan_start_yaw = 0.0f;                   // 转视角开始时的yaw角度
    float m_scan_current_yaw = 0.0f;                 // 当前转视角的yaw角度
    static constexpr float SCAN_SPEED = 180.0f;      // 转视角速度（度/秒）
    static constexpr float SCAN_COMPLETE_ANGLE = 360.0f; // 转视角完成角度
    static constexpr float scan_sensitivity = 20.0f; // 转视角灵敏度

    // 在Aimbot类的private成员区加上
    int m_last_health = -1;
    int m_injured_ticks = 0;
    static constexpr int INJURED_MEMORY_FRAMES = 100;
    
    // 复活后延迟
    int m_respawn_delay_ticks = 0;
    static constexpr int RESPAWN_DELAY_FRAMES = 120; // 复活后延迟2秒（120帧）
    bool m_just_respawned = false; // 防止重复触发复活检测


};

// 可调参数
namespace {
    constexpr float FAST_ENTER_THRESHOLD = 0.0f;
    constexpr float FAST_MAX_ENTER_THRESHOLD = 140.0f;
    constexpr float FAST_MAX_STEP = 10.0f;
    constexpr float FAST_SENSITIVITY = 16.0f;

    constexpr float SMOOTHING_TIME = 0.13f;
    constexpr float MOUSE_SENSITIVITY = 16.0f;
    constexpr float MAX_STEP = 3.0f;

    constexpr float PREDICTION_INTERVAL = 0.0f;
    constexpr float SPEED_CORRECTION_FACTOR = 0.0f;

    // 爆头率
    constexpr float PROB_HEAD =0.95f;
}