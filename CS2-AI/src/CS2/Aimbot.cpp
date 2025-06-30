// CS2/Aimbot.cpp
#include "CS2/Aimbot.h"
#include <Windows.h>
#include <algorithm>
#include <QDebug>

extern volatile bool g_isPaused;

// -------- 可调参数（统一在此处调整） --------
namespace {
    // 快速定位阈值（角度）
    constexpr float FAST_ENTER_THRESHOLD = 3.0f;
    constexpr float FAST_MAX_ENTER_THRESHOLD = 150.0f;
    constexpr float FAST_MAX_STEP = 10.0f;
    constexpr float FAST_SENSITIVITY = 20.0f;

    // 常规平滑参数
    constexpr float SMOOTHING_TIME = 0.1f;
    constexpr float MOUSE_SENSITIVITY = 15.0f;
    constexpr float MAX_STEP = 1.5f;
    constexpr float JITTER_DEADZONE = 5.0f;

    // 运动预测参数
    constexpr float PREDICTION_INTERVAL = 3.0f;  // 预测时间倍率，越大预测越前
    constexpr float SPEED_CORRECTION_FACTOR = 0.5f;  // 速度校正系数
}

// 鼠标实际移动
void Aimbot::move_mouse(float dx, float dy) {
    if (dx == 0.0f && dy == 0.0f)
        return;
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dx = static_cast<LONG>(dx);
    input.mi.dy = static_cast<LONG>(dy);
    input.mi.dwFlags = MOUSEEVENTF_MOVE;
    SendInput(1, &input, sizeof(input));
}

// 计算俯仰与偏航
Vec2D<float> Aimbot::calc_view_vec_aim_to_head(
    const Vec3D<float>& player_head,
    const Vec3D<float>& enemy_head) {
    Vec3D<float> dir = enemy_head - player_head;
    Vec3D<float> up{ 0.0f, 0.0f, 1.0f };
    float cos_theta = up.dot_product(dir) / (up.calc_abs() * dir.calc_abs());
    float pitch = std::acos(cos_theta) * 180.0f / PI - 90.0f;
    float yaw = std::atan2(dir.y, dir.x) * 180.0f / PI + 180.0f;
    if (yaw >= 360.0f) yaw -= 360.0f;
    return { pitch, yaw };
}

// 目标位置预测
Vec2D<float> Aimbot::predictTarget(float tgtX, float tgtY) {
    auto now = std::chrono::steady_clock::now();
    float deltaTime = std::chrono::duration<float>(now - m_prevTime).count();
    if (deltaTime <= 1e-6f) deltaTime = 1e-6f;

    // 与上次目标差值
    float dx = tgtX - m_prevTargetX;
    float dy = tgtY - m_prevTargetY;
    float dist = std::sqrt(dx * dx + dy * dy);
    float maxJump = 180.0f * 0.3f; // 30% 偏移视作跳变

    // 首次或跳变重置
    if (m_prevDist == 0.0f || dist > maxJump) {
        m_prevTargetX = tgtX;
        m_prevTargetY = tgtY;
        m_prevVelX = m_prevVelY = 0.0f;
        m_prevTime = now;
        m_prevDist = dist;
        return { tgtX, tgtY };
    }

    // 计算速度与加速度
    float velX = dx / deltaTime;
    float velY = dy / deltaTime;
    float accX = (velX - m_prevVelX) / deltaTime;
    float accY = (velY - m_prevVelY) / deltaTime;

    // 接近度因子
    float proximity = 1.0f / (dist + 1.0f);
    proximity = std::clamp(proximity, 0.1f, 1.0f);

    // 速度校正
    float speedCorr = 1.0f + (m_prevDist > 0.0f ?
        (std::abs(dist - m_prevDist) / (180.0f + 1e-6f)) * SPEED_CORRECTION_FACTOR
        : 0.0f);

    // 预测步长
    float dtPred = deltaTime * PREDICTION_INTERVAL * proximity * speedCorr;

    // 预测新角度
    float predX = tgtX + velX * dtPred + 0.5f * accX * dtPred * dtPred;
    float predY = tgtY + velY * dtPred + 0.5f * accY * dtPred * dtPred;

    // 更新历史状态
    m_prevTargetX = tgtX;
    m_prevTargetY = tgtY;
    m_prevVelX = velX;
    m_prevVelY = velY;
    m_prevTime = now;
    m_prevDist = dist;

    return { predX, predY };
}

// 主更新函数
void Aimbot::update(GameInformationhandler* info_handler) {
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
    auto now = std::chrono::steady_clock::now();

    // FAST 模式
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

    // SMOOTH 模式：预测 + 平滑
    Vec2D<float> pred = predictTarget(dy_raw, dx_raw);
    float dy_pred = pred.x;
    float dx_pred = pred.y;

    // 限幅 & 灵敏度映射
    float dx = std::clamp(dx_pred, -MAX_STEP, MAX_STEP);
    float dy = std::clamp(dy_pred, -MAX_STEP, MAX_STEP);
    float dx_move = dx * MOUSE_SENSITIVITY;
    float dy_move = dy * MOUSE_SENSITIVITY;

    // 死区滤波
    if (std::fabs(dx_move) < JITTER_DEADZONE) dx_move = 0.0f;
    if (std::fabs(dy_move) < JITTER_DEADZONE) dy_move = 0.0f;

    // 指数平滑
    float dt = std::chrono::duration<float>(now - m_last_time).count();
    float alpha = dt / (SMOOTHING_TIME + dt);
    m_last_time = now;
    m_smoothed_dx += (dx_move - m_smoothed_dx) * alpha;
    m_smoothed_dy += (dy_move - m_smoothed_dy) * alpha;

    qDebug() << "[AIM] SMOOTH move:" << m_smoothed_dx << m_smoothed_dy;
    move_mouse(m_smoothed_dx, m_smoothed_dy);
}
