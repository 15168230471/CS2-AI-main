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

    // ����/����ʱ����
    // 检测复活：从死亡状态(health <= 0)到复活状态(health > 0)
    if (m_last_health <= 0 && gi.controlled_player.health > 0 && !m_just_respawned) {
        // 刚复活，设置延迟
        m_respawn_delay_ticks = RESPAWN_DELAY_FRAMES;
        m_just_respawned = true;
        std::cout << "[Aimbot] Player respawned! Setting delay: " << m_respawn_delay_ticks << " ticks" << std::endl;
    }
    
    // 死亡时重置
    if (gi.controlled_player.health <= 0) {
        m_last_health = 0; // 设置为0表示死亡状态
        m_injured_ticks = 0;
        m_just_respawned = false; // 重置复活标志
        return;
    }
    
    // 复活后延迟，避免立即瞄准
    if (m_respawn_delay_ticks > 0) {
        m_respawn_delay_ticks--;
        static int respawn_debug_counter = 0;
        if (++respawn_debug_counter % 60 == 0) {
            std::cout << "[Aimbot] Respawn delay: " << m_respawn_delay_ticks << " ticks remaining" << std::endl;
        }
        return;
    }

    // ------ ����Ƿ�ո����� ------
    bool just_injured = false;
    if (m_last_health != -1 && gi.controlled_player.health < m_last_health) {
        m_injured_ticks = INJURED_MEMORY_FRAMES; // ÿ�ε�Ѫˢ�����˼���
        just_injured = true;
        /*std::cout << "[Aimbot] Got injured! Injured ticks set: " << m_injured_ticks << std::endl;*/
    }
    else if (m_injured_ticks > 0) {
        m_injured_ticks--;
    }
    m_last_health = gi.controlled_player.health;

    // ------ 受伤后处理逻辑 ------
    if (m_injured_ticks > 0) {
        // 如果不在扫描模式，开始扫描
        if (!m_scanning_mode) {
            m_scanning_mode = true;
            m_scan_start_yaw = gi.controlled_player.view_vec.y;
            m_scan_current_yaw = m_scan_start_yaw;
        }
        
        // 执行扫描
        auto now_tp = std::chrono::steady_clock::now();
        double now_ms = std::chrono::duration<double, std::milli>(now_tp.time_since_epoch()).count();
        static double last_scan_time = 0.0;
        double delta_time = now_ms - last_scan_time;
        
        if (delta_time >= 16.0) { // 约60FPS
            float scan_step = SCAN_SPEED * (delta_time / 1000.0f);
            m_scan_current_yaw += scan_step;
            
            // 检查是否完成扫描
            if (m_scan_current_yaw - m_scan_start_yaw >= SCAN_COMPLETE_ANGLE) {
                m_scanning_mode = false;
                m_injured_ticks = 0;
                return;
            }
            
            // 计算目标角度
            float target_yaw = m_scan_current_yaw;
            if (target_yaw >= 360.0f) target_yaw -= 360.0f;
            if (target_yaw < 0.0f) target_yaw += 360.0f;
            
            // 计算当前角度
            float current_yaw = gi.controlled_player.view_vec.y;
            
            // 计算角度差
            float dyaw = target_yaw - current_yaw;
            if (dyaw > 180.0f) dyaw -= 360.0f;
            if (dyaw < -180.0f) dyaw += 360.0f;
            
            // 移动鼠标
            float mouse_dx = dyaw * scan_sensitivity;
            move_mouse(mouse_dx, 0.0f);
            
            last_scan_time = now_ms;
        }
        return;
    }

    // ------ 寻找目标 ------
    Vec3D<float> my_head = gi.controlled_player.head_position;
    const PlayerInformation* target_enemy = nullptr;

    // ------ 目标选择 ------
    // 只瞄准isSpotted为true的敌人，避免隔墙瞄人
    
    // 调试：检查敌人状态
    static int debug_counter = 0;
    if (++debug_counter % 60 == 0) {  // 每秒输出一次
        int total_enemies = 0;
        int spotted_enemies = 0;
        for (const auto& enemy : gi.other_players) {
            if (enemy.health > 0) {
                total_enemies++;
                if (enemy.isSpotted) {
                    spotted_enemies++;
                }
            }
        }
        std::cout << "[Aimbot] Total enemies: " << total_enemies 
                  << ", Spotted enemies: " << spotted_enemies << std::endl;
    }
    
    // 只选择最近的可见敌人（必须是isSpotted=true）
    if (gi.closest_enemy_player && gi.closest_enemy_player->isSpotted) {
        target_enemy = &(*gi.closest_enemy_player);
        // 减少调试输出频率：每60帧输出一次（约1秒）
        static int target_debug_counter = 0;
        if (++target_debug_counter % 60 == 0) {
            std::cout << "[Aimbot] Selected closest enemy (isSpotted=" << gi.closest_enemy_player->isSpotted << ") - ID: " << gi.closest_enemy_player->pawn_addr 
                      << ", Health: " << gi.closest_enemy_player->health 
                      << ", Position: (" << gi.closest_enemy_player->position.x << "," << gi.closest_enemy_player->position.y << "," << gi.closest_enemy_player->position.z << ")" << std::endl;
        }
    }

    if (!target_enemy) {
        // 减少调试输出频率：每60帧输出一次（约1秒）
        static int no_target_debug_counter = 0;
        if (++no_target_debug_counter % 60 == 0) {
            std::cout << "[Aimbot] No valid target, returning" << std::endl;
        }
        return;
    }

    // 减少调试输出频率：每60帧输出一次（约1秒）
    static int validation_debug_counter = 0;
    if (++validation_debug_counter % 60 == 0) {
        std::cout << "[Aimbot] Target validation passed - proceeding with aim" << std::endl;
    }



    // ��׼�߼�
    float r = m_hit_head_dist(m_rng);
    Vec3D<float> enemy_target_pos = (r < PROB_HEAD)
        ? target_enemy->chest_position
        : target_enemy->position;

    Vec2D<float> target = calc_view_vec_aim_to_head(my_head, enemy_target_pos);
    Vec2D<float> current = gi.controlled_player.view_vec;

    float dy_raw = target.x - current.x;
    float dx_raw = target.y - current.y;
    if (dx_raw > 180.0f) dx_raw -= 360.0f;
    if (dx_raw < -180.0f) dx_raw += 360.0f;

    float error_mag = std::max(std::fabs(dx_raw), std::fabs(dy_raw));

    auto now_tp = std::chrono::steady_clock::now();
    double now_ms = std::chrono::duration<double, std::milli>(now_tp.time_since_epoch()).count();



    // ��Ӧ�ӳ�
    // 反应延迟已移除以提高瞄准效率
    // if (!m_reaction_pending) {
    //     m_reaction_pending = true;
    //     m_reaction_delay_ms = m_delay_dist(m_rng);
    //     m_reaction_start = now_ms;
    // }
    // if (now_ms - m_reaction_start < m_reaction_delay_ms)
    //     return;

    // FAST ģʽ
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

    // SMOOTH ģʽ��Ԥ�� + ���˻�ƽ��
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
