// CS2/Aimbot.cpp
#include "CS2/Aimbot.h"
#include <Windows.h>
#include <algorithm>
#include <QDebug>

extern volatile bool g_isPaused;

namespace {
    // 快速定位阈值（角度）
    constexpr float FAST_ENTER_THRESHOLD = 10.0f;   // 偏差大于此使用快速对齐
    constexpr float FAST_MAX_ENTER_THRESHOLD = 50.0f;  // 偏差过大时不使用快速对齐
    constexpr float FAST_MAX_STEP = 10.0f;  // 快速模式最大步长 (度)
    constexpr float FAST_SENSITIVITY = 30.0f;  // 快速模式灵敏度

    // 常规平滑参数
    constexpr float SMOOTHING_TIME = 0.1f;   // 平滑时间常数 (秒)
    constexpr float MOUSE_SENSITIVITY = 15.0f;  // 常规灵敏度
    constexpr float MAX_STEP = 1.5f;   // 常规最大步长 (度)
    constexpr float JITTER_DEADZONE = 5.0f;   // 抖动死区 (像素)
}

void Aimbot::move_mouse(float dx, float dy)
{
    if (dx == 0.0f && dy == 0.0f)
        return;

    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dx = static_cast<LONG>(dx);
    input.mi.dy = static_cast<LONG>(dy);
    input.mi.dwFlags = MOUSEEVENTF_MOVE;
    SendInput(1, &input, sizeof(input));
}

Vec2D<float> Aimbot::calc_view_vec_aim_to_head(const Vec3D<float>& player_head,
    const Vec3D<float>& enemy_head)
{
    Vec3D<float> dir = enemy_head - player_head;
    Vec3D<float> up{ 0.0f, 0.0f, 1.0f };
    float cos_theta = up.dot_product(dir) / (up.calc_abs() * dir.calc_abs());
    float pitch = std::acos(cos_theta) * 180.0f / PI - 90.0f;
    float yaw = std::atan2(dir.y, dir.x) * 180.0f / PI + 180.0f;
    if (yaw >= 360.0f) yaw -= 360.0f;
    return Vec2D<float>{pitch, yaw};
}

void Aimbot::update(GameInformationhandler* info_handler)
{
    if (g_isPaused || !info_handler)
        return;

    GameInformation gi = info_handler->get_game_information();
    if (!gi.closest_enemy_player)
        return;

    // 计算视角差
    Vec3D<float> my_head = gi.controlled_player.head_position;
    Vec3D<float> enemy_head = gi.closest_enemy_player->head_position;
    Vec2D<float> target = calc_view_vec_aim_to_head(my_head, enemy_head);
    Vec2D<float> current = gi.controlled_player.view_vec;

    float dy_raw = target.x - current.x;
    float dx_raw = target.y - current.y;
    if (dx_raw > 180.0f) dx_raw -= 360.0f;
    if (dx_raw < -180.0f) dx_raw += 360.0f;

    float error_mag = std::max(std::fabs(dx_raw), std::fabs(dy_raw));

    // Debug: 打印关键原始数据
    qDebug() << "[DEBUG] dx_raw:" << dx_raw << "dy_raw:" << dy_raw
        << "error_mag:" << error_mag
        << "FAST_THRESHOLD:" << FAST_ENTER_THRESHOLD;

    auto now = std::chrono::steady_clock::now();

    // 若偏差较大，先做一次快速对齐
    if (error_mag > FAST_ENTER_THRESHOLD && error_mag < FAST_MAX_ENTER_THRESHOLD) {
        float dx_l = std::clamp(dx_raw, -FAST_MAX_STEP, FAST_MAX_STEP);
        float dy_l = std::clamp(dy_raw, -FAST_MAX_STEP, FAST_MAX_STEP);
        float dx_fast = dx_l * FAST_SENSITIVITY;
        float dy_fast = dy_l * FAST_SENSITIVITY;
        qDebug() << "[AIM] FAST move:" << dx_fast << dy_fast;
        move_mouse(dx_fast, dy_fast);
        m_smoothed_dx = dx_fast;
        m_smoothed_dy = dy_fast;
        m_last_time = now;
        return;
    }

    // 常规平滑流程
    float dx = std::clamp(dx_raw, -MAX_STEP, MAX_STEP);
    float dy = std::clamp(dy_raw, -MAX_STEP, MAX_STEP);
    float dx_move = dx * MOUSE_SENSITIVITY;
    float dy_move = dy * MOUSE_SENSITIVITY;

    qDebug() << "[DEBUG] dx_move pre-filter:" << dx_move << "dy_move pre-filter:" << dy_move;

    // 死区滤波
    if (std::fabs(dx_move) < JITTER_DEADZONE) dx_move = 0.0f;
    if (std::fabs(dy_move) < JITTER_DEADZONE) dy_move = 0.0f;

    // 指数平滑滤波
    float dt = std::chrono::duration<float>(now - m_last_time).count();
    float alpha = dt / (SMOOTHING_TIME + dt);
    qDebug() << "[DEBUG] dt:" << dt << "alpha:" << alpha;

    m_last_time = now;
    m_smoothed_dx += (dx_move - m_smoothed_dx) * alpha;
    m_smoothed_dy += (dy_move - m_smoothed_dy) * alpha;

    qDebug() << "[AIM] SMOOTH move:" << m_smoothed_dx << m_smoothed_dy;
    move_mouse(m_smoothed_dx, m_smoothed_dy);
}
