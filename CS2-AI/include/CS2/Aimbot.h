// CS2/Aimbot.h
#pragma once

#include <Windows.h>
#include <chrono>
#include <cmath>
#include "GameInformationhandler.h"

class Aimbot {
public:
    void update(GameInformationhandler* info_handler);

private:
    // ����ƶ�
    void move_mouse(float dx, float dy);
    // ��������ͷ��������ͷ�����ӽ����� (pitch, yaw)
    Vec2D<float> calc_view_vec_aim_to_head(const Vec3D<float>& player_head,
        const Vec3D<float>& enemy_head);

    // ----- SMOOTH �˲�״̬ -----
    float m_smoothed_dx = 0.0f;
    float m_smoothed_dy = 0.0f;
    std::chrono::steady_clock::time_point m_last_time = std::chrono::steady_clock::now();

    // ----- Ԥ�����״̬ -----
    float m_prevTargetX = 0.0f;
    float m_prevTargetY = 0.0f;
    float m_prevVelX = 0.0f;
    float m_prevVelY = 0.0f;
    float m_prevDist = 0.0f;
    std::chrono::steady_clock::time_point m_prevTime = std::chrono::steady_clock::now();

    // Ŀ���˶�Ԥ��
    Vec2D<float> predictTarget(float tgtX, float tgtY);
};