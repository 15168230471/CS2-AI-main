// CS2/Aimbot.cpp
#include "CS2/Aimbot.h"
#include <Windows.h>
#include <algorithm>
#include <QDebug>



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

Vec2D<float> Aimbot::calc_view_vec_aim_to_head(
    const Vec3D<float>& player_head,
    const Vec3D<float>& target_pos) {
    Vec3D<float> dir = target_pos - player_head;
    Vec3D<float> up{ 0.0f, 0.0f, 1.0f };
    float cos_theta = up.dot_product(dir) / (up.calc_abs() * dir.calc_abs());
    float pitch = std::acos(cos_theta) * 180.0f / PI - 90.0f;
    float yaw = std::atan2(dir.y, dir.x) * 180.0f / PI + 180.0f;
    if (yaw >= 360.0f) yaw -= 360.0f;
    return { pitch, yaw };
}

Vec2D<float> Aimbot::predictTarget(float tgtX, float tgtY) {
    auto now = std::chrono::steady_clock::now();
    float deltaTime = std::chrono::duration<float>(now - m_prevTime).count();
    deltaTime = (deltaTime <= 1e-6f) ? 1e-6f : deltaTime;

    float dx = tgtX - m_prevTargetX;
    float dy = tgtY - m_prevTargetY;
    float dist = std::sqrt(dx * dx + dy * dy);
    float maxJump = 180.0f * 0.3f;

    if (m_prevDist == 0.0f || dist > maxJump) {
        m_prevTargetX = tgtX;
        m_prevTargetY = tgtY;
        m_prevVelX = m_prevVelY = 0.0f;
        m_prevTime = now;
        m_prevDist = dist;
        return { tgtX, tgtY };
    }

    float velX = dx / deltaTime;
    float velY = dy / deltaTime;
    float accX = (velX - m_prevVelX) / deltaTime;
    float accY = (velY - m_prevVelY) / deltaTime;

    float proximity = 1.0f / (dist + 1.0f);
    proximity = std::clamp(proximity, 0.1f, 1.0f);

    float speedCorr = 1.0f + (m_prevDist > 0.0f ?
        (std::abs(dist - m_prevDist) / (180.0f + 1e-6f)) * SPEED_CORRECTION_FACTOR
        : 0.0f);

    float dtPred = deltaTime * PREDICTION_INTERVAL * proximity * speedCorr;

    float predX = tgtX + velX * dtPred + 0.5f * accX * dtPred * dtPred;
    float predY = tgtY + velY * dtPred + 0.5f * accY * dtPred * dtPred;

    m_prevTargetX = tgtX;
    m_prevTargetY = tgtY;
    m_prevVelX = velX;
    m_prevVelY = velY;
    m_prevTime = now;
    m_prevDist = dist;

    return { predX, predY };
}

void Aimbot::update(GameInformationhandler* info_handler) {
    

    GameInformation gi = info_handler->get_game_information();

    // 死亡/重生时重置
    if (gi.controlled_player.health <= 0) {
        m_last_health = -1;
        m_injured_ticks = 0;
        return;
    }

    // ------ 检查是否刚刚受伤 ------
    bool just_injured = false;
    if (m_last_health != -1 && gi.controlled_player.health < m_last_health) {
        m_injured_ticks = INJURED_MEMORY_FRAMES; // 每次掉血刷新受伤记忆
        just_injured = true;
        /*std::cout << "[Aimbot] Got injured! Injured ticks set: " << m_injured_ticks << std::endl;*/
    }
    else if (m_injured_ticks > 0) {
        m_injured_ticks--;
    }
    m_last_health = gi.controlled_player.health;

    // ------ 寻找目标 ------
    const PlayerInformation* target_enemy = nullptr;
    float min_dist_firing = std::numeric_limits<float>::max();
    const PlayerInformation* firing_enemy = nullptr;

    for (const auto& enemy : gi.other_players) {
        if (enemy.health <= 0) continue;
        uintptr_t id = enemy.pawn_addr;

        // 记录 shots_fired 记忆
        DWORD prev_shots = m_prev_enemy_shots_fired[id];
        if (enemy.shots_fired > prev_shots || enemy.shots_fired > 0) {
            m_enemy_fire_ticks[id] = FIRE_MEMORY_FRAMES;
            /*if (enemy.shots_fired > prev_shots)
                std::cout << "[Aimbot] id=" << id << " | shots_fired INCREASE! (" << prev_shots << "->" << enemy.shots_fired << "), set fire_ticks=" << FIRE_MEMORY_FRAMES << std::endl;*/
        }
        else if (m_enemy_fire_ticks[id] > 0) {
            m_enemy_fire_ticks[id]--;
        }
        m_prev_enemy_shots_fired[id] = enemy.shots_fired;

        float dist = gi.controlled_player.position.distance(enemy.position);

        // 找出最近的正在开火敌人
        if (enemy.shots_fired > 0 || m_enemy_fire_ticks[id] > 0) {
            if (dist < min_dist_firing) {
                min_dist_firing = dist;
                firing_enemy = &enemy;
            }
        }

        //// 可选：打印所有敌人的状态
        //std::cout << "[Aimbot] id=" << id
        //    << " | shots_fired=" << enemy.shots_fired
        //    << " | fire_ticks=" << m_enemy_fire_ticks[id]
        //    << " | dist=" << dist
        //    << std::endl;
    }

    // ------ 目标选择 ------
    // 优先：受伤后的记忆期内，盯着正在开火的最近敌人
    if (m_injured_ticks > 0 && firing_enemy) {
        target_enemy = firing_enemy;
        /*std::cout << "[Aimbot] Picked firing_enemy id=" << firing_enemy->pawn_addr
            << " (after injury), m_enemy_fire_ticks="
            << m_enemy_fire_ticks[firing_enemy->pawn_addr] << std::endl;*/
    }
    // 正常情况：最近的敌人
    else if (gi.closest_enemy_player) {
        target_enemy = &(*gi.closest_enemy_player);
        /*std::cout << "[Aimbot] Picked closest_enemy id=" << gi.closest_enemy_player->pawn_addr 
            << " closest_enemy  m_enemy_fire_ticks=" << m_enemy_fire_ticks[target_enemy->pawn_addr] << std::endl;*/
    }

    if (!target_enemy)
        return;



    // 瞄准逻辑
    float r = m_hit_head_dist(m_rng);
    Vec3D<float> enemy_target_pos = (r < PROB_HEAD)
        ? target_enemy->chest_position
        : target_enemy->position;

    Vec3D<float> my_head = gi.controlled_player.head_position;
    Vec2D<float> target = calc_view_vec_aim_to_head(my_head, enemy_target_pos);
    Vec2D<float> current = gi.controlled_player.view_vec;

    float dy_raw = target.x - current.x;
    float dx_raw = target.y - current.y;
    if (dx_raw > 180.0f) dx_raw -= 360.0f;
    if (dx_raw < -180.0f) dx_raw += 360.0f;

    float error_mag = std::max(std::fabs(dx_raw), std::fabs(dy_raw));

    auto now_tp = std::chrono::steady_clock::now();
    double now_ms = std::chrono::duration<double, std::milli>(now_tp.time_since_epoch()).count();



    // 反应延迟
    if (!m_reaction_pending) {
        m_reaction_pending = true;
        m_reaction_delay_ms = m_delay_dist(m_rng);
        m_reaction_start = now_ms;
    }
    if (now_ms - m_reaction_start < m_reaction_delay_ms)
        return;

    // FAST 模式
    if (error_mag > FAST_ENTER_THRESHOLD && error_mag < FAST_MAX_ENTER_THRESHOLD) {
        float dx_l = std::clamp(dx_raw, -FAST_MAX_STEP, FAST_MAX_STEP);
        float dy_l = std::clamp(dy_raw, -FAST_MAX_STEP, FAST_MAX_STEP);
        float dx_fast = dx_l * FAST_SENSITIVITY;
        float dy_fast = dy_l * FAST_SENSITIVITY;
        move_mouse(dx_fast, dy_fast);
        m_smoothed_dx = dx_fast;
        m_smoothed_dy = dy_fast;
        m_last_time = now_tp;
        return;
    }

    // SMOOTH 模式：预测 + 拟人化平滑
    Vec2D<float> pred = predictTarget(dy_raw, dx_raw);
    float dy_pred = pred.x;
    float dx_pred = pred.y;

    float speedScale = std::clamp(error_mag / 45.0f, 0.5f, 1.5f);
    float dx_step = std::clamp(dx_pred * speedScale, -MAX_STEP * speedScale, MAX_STEP * speedScale);
    float dy_step = std::clamp(dy_pred * speedScale, -MAX_STEP * speedScale, MAX_STEP * speedScale);



    double dt = std::chrono::duration<double>(now_tp - m_last_time).count();
    float alpha = dt / (SMOOTHING_TIME + dt);
    m_smoothed_dx += (dx_step * MOUSE_SENSITIVITY - m_smoothed_dx) * alpha;
    m_smoothed_dy += (dy_step * MOUSE_SENSITIVITY - m_smoothed_dy) * alpha;

    if (error_mag < 2.0f) {
        m_smoothed_dx *= 0.7f;
        m_smoothed_dy *= 0.7f;
    }

    move_mouse(m_smoothed_dx, m_smoothed_dy);
    m_last_time = now_tp;
}
