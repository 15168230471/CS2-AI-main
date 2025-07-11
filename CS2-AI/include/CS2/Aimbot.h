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
    std::uniform_real_distribution<float> m_jitter_dist{ 0.0f, 0.0f };     // 角度
    std::uniform_real_distribution<float> m_hit_head_dist{ 0.0f, 1.0f };   // 爆头概率分布

    // ===== 预测相关状态 =====
    float m_prevTargetX = 0.0f;
    float m_prevTargetY = 0.0f;
    float m_prevVelX = 0.0f;
    float m_prevVelY = 0.0f;
    float m_prevDist = 0.0f;
    std::chrono::steady_clock::time_point m_prevTime = std::chrono::steady_clock::now();

    // 在Aimbot类的private成员区加上
    int m_last_health = -1;
    int m_injured_ticks = 0;
    static constexpr int INJURED_MEMORY_FRAMES = 100;

    std::unordered_map<uintptr_t, int> m_enemy_fire_ticks;        // 敌人正在开火记忆
    std::unordered_map<uintptr_t, DWORD> m_prev_enemy_shots_fired;
    static constexpr int FIRE_MEMORY_FRAMES = 100;

};

// 可调参数
namespace {
    constexpr float FAST_ENTER_THRESHOLD = 0.1f;
    constexpr float FAST_MAX_ENTER_THRESHOLD = 130.0f;
    constexpr float FAST_MAX_STEP = 10.0f;
    constexpr float FAST_SENSITIVITY = 20.0f;

    constexpr float SMOOTHING_TIME = 0.13f;
    constexpr float MOUSE_SENSITIVITY = 20.0f;
    constexpr float MAX_STEP = 1.2f;

    constexpr float PREDICTION_INTERVAL = 0.0f;
    constexpr float SPEED_CORRECTION_FACTOR = 0.0f;

    // 爆头率
    constexpr float PROB_HEAD =0.95f;
}