// CS2/Aimbot.h
#pragma once

#include <Windows.h>
#include <chrono>
#include <cmath>
#include <random>
#include "GameInformationhandler.h"

class Aimbot {
public:
    void update(GameInformationhandler* info_handler);

private:
    // ����ƶ�
    void move_mouse(float dx, float dy);
    // ���㸩����ƫ������׼ָ��λ��
    Vec2D<float> calc_view_vec_aim_to_head(const Vec3D<float>& player_head,
        const Vec3D<float>& target_pos);
    // Ŀ���˶�Ԥ��
    Vec2D<float> predictTarget(float tgtX, float tgtY);

    // ===== SMOOTH �˲�״̬ =====
    float m_smoothed_dx = 0.0f;
    float m_smoothed_dy = 0.0f;
    std::chrono::steady_clock::time_point m_last_time = std::chrono::steady_clock::now();

    // ===== ��Ӧ�ӳ� =====
    bool   m_reaction_pending = false;
    float  m_reaction_delay_ms = 0.0f;
    double m_reaction_start = 0.0;

    // ===== ������� =====
    std::mt19937 m_rng{ std::random_device{}() };
    std::uniform_real_distribution<float> m_delay_dist{ 800.0f, 2000.0f };  // ms
    std::uniform_real_distribution<float> m_jitter_dist{ -0.2f, 0.2f };     // �Ƕ�
    std::uniform_real_distribution<float> m_hit_head_dist{ 0.0f, 1.0f };   // ��ͷ���ʷֲ�

    // ===== Ԥ�����״̬ =====
    float m_prevTargetX = 0.0f;
    float m_prevTargetY = 0.0f;
    float m_prevVelX = 0.0f;
    float m_prevVelY = 0.0f;
    float m_prevDist = 0.0f;
    std::chrono::steady_clock::time_point m_prevTime = std::chrono::steady_clock::now();
};

// �ɵ�����
namespace {
    constexpr float FAST_ENTER_THRESHOLD = 3.0f;
    constexpr float FAST_MAX_ENTER_THRESHOLD = 150.0f;
    constexpr float FAST_MAX_STEP = 10.0f;
    constexpr float FAST_SENSITIVITY = 20.0f;

    constexpr float SMOOTHING_TIME = 0.1f;
    constexpr float MOUSE_SENSITIVITY = 20.0f;
    constexpr float MAX_STEP = 1.0f;

    constexpr float PREDICTION_INTERVAL = 3.0f;
    constexpr float SPEED_CORRECTION_FACTOR = 0.5f;

    // ��ͷ��
    constexpr float PROB_HEAD = 0.55f;
}