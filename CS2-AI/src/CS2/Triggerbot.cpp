// CS2/Triggerbot.cpp

#include "CS2/Triggerbot.h"
#include <Windows.h>
#include <random>
#include <chrono>
#include <thread>
#include <QDebug>

volatile bool g_just_fired = false;

// ====== 可调参数区 ======
namespace TriggerbotHumanizedConfig {
    constexpr float REACT_PROBABILITY = 0.97f;
    constexpr int   REACT_DELAY_MIN_MS = 60;
    constexpr int   REACT_DELAY_MAX_MS = 100;
    constexpr float HESITATE_PROB = 0.05f;
    constexpr int   HESITATE_MIN_MS = 5;
    constexpr int   HESITATE_MAX_MS = 20;
    constexpr int   HOLD_MIN_MS = 20;
    constexpr int   HOLD_MAX_MS = 50;
    constexpr int   BASE_FIRE_DELAY_MS = 250;
    constexpr int   VAR_FIRE_DELAY_MIN = 20;
    constexpr int   VAR_FIRE_DELAY_MAX = 100;

    // 近距离定义
    constexpr float CLOSE_DIST_THRESHOLD = 600.0f;
    constexpr int   CLOSE_REACT_DELAY_MS = 10;

    // 自动装弹相关
    constexpr int   RELOAD_THRESHOLD = 5;    // 弹夹<=2触发自动装弹
    constexpr int   IDLE_RELOAD_MS = 2000; // 最近3秒没有开枪才允许自动装弹
}

// ===== 文件内静态状态 =====
static std::mt19937& rng() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    return gen;
}
static int randInt(int min, int max) {
    std::uniform_int_distribution<> dist(min, max);
    return dist(rng());
}
static float randProb() {
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    return dist(rng());
}

// 记录最近一次开枪时间（初始化为很久之前，保证启动时可按规则装弹）
static auto g_last_fire_time = std::chrono::steady_clock::now() - std::chrono::hours(1);
// 记录上一次读取的 shots_fired，用于识别“手动开枪”
static int  g_prev_shots_fired = 0;

// ——— 拟人化鼠标点击 ———
static void simulateHumanClick()
{
    INPUT input = {};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    SendInput(1, &input, sizeof(input));

    int holdTime = randInt(TriggerbotHumanizedConfig::HOLD_MIN_MS, TriggerbotHumanizedConfig::HOLD_MAX_MS);
    std::this_thread::sleep_for(std::chrono::milliseconds(holdTime));

    input.mi.dwFlags = MOUSEEVENTF_LEFTUP;
    SendInput(1, &input, sizeof(input));

    // 记录开枪时间
    g_last_fire_time = std::chrono::steady_clock::now();
    g_just_fired = true;
}

// ——— 主逻辑 ———
void Triggerbot::update(GameInformationhandler* handler)
{
    using namespace TriggerbotHumanizedConfig;

    if (!handler)
        return;

    const GameInformation game_info = handler->get_game_information();

    // ====== 同步手动/外部开枪：如 shots_fired 增加，刷新最近开枪时间 ======
    {
        int shots = game_info.controlled_player.shots_fired;
        if (shots > g_prev_shots_fired) {
            g_last_fire_time = std::chrono::steady_clock::now();
            // qDebug() << "[Triggerbot] Detected shot via shots_fired, now=" << shots;
        }
        g_prev_shots_fired = shots;
    }

    // ====== 自动装弹逻辑：3 秒内未开枪才允许 ======
    {
        int  ammo = game_info.controlled_player.weapon_info.ammo_clip1;
        bool hasTarget = bool(game_info.player_in_crosshair);
        int  hp = game_info.controlled_player.health;

        const auto now = std::chrono::steady_clock::now();
        const auto idle = std::chrono::duration_cast<std::chrono::milliseconds>(now - g_last_fire_time).count();
        const bool idleLongEnough = (idle >= IDLE_RELOAD_MS);

        // 仍保持“准星下没有目标”与“未死亡”的限制；新增“空闲≥3秒”
        if (!hasTarget
            && ammo > -1 && ammo <= RELOAD_THRESHOLD
            && hp > 0
            && idleLongEnough)
        {
            // 按下R（VK_R = 0x52）
            INPUT input = {};
            input.type = INPUT_KEYBOARD;
            input.ki.wVk = 'R';
            SendInput(1, &input, sizeof(INPUT));
            // 松开
            input.ki.dwFlags = KEYEVENTF_KEYUP;
            SendInput(1, &input, sizeof(INPUT));

            // qDebug() << "[Triggerbot] 自动装弹: ammo=" << ammo << " idle(ms)=" << idle;
        }
        // else {
        //     qDebug() << "[Triggerbot] 不装弹: hasTarget=" << hasTarget
        //              << " ammo=" << ammo << " idle(ms)=" << idle;
        // }
    }

    // ====== 下面保持原有触发射击逻辑 ======
    if (game_info.controlled_player.shots_fired > 0
        || game_info.controlled_player.health <= 0)
        return;

    if (!game_info.player_in_crosshair)
        return;

    int  targetHealth = game_info.player_in_crosshair->health;
    bool targetImmune = game_info.player_in_crosshair->isImmune;
    if (targetHealth <= 0 || targetHealth >= 200 || targetImmune)
        return;
   
    if (randProb() > REACT_PROBABILITY)
        return;
    
    // 近距离优先快速反应
    float dist = game_info.controlled_player.position.distance(game_info.player_in_crosshair->position);
    int reactDelay;
    if (dist < CLOSE_DIST_THRESHOLD) {
        reactDelay = CLOSE_REACT_DELAY_MS;
        // qDebug() << "[Triggerbot] Close enemy! dist=" << dist << "reactDelay=" << reactDelay;
    }
    else {
        reactDelay = randInt(REACT_DELAY_MIN_MS, REACT_DELAY_MAX_MS);
        // qDebug() << "[Triggerbot] Normal dist=" << dist << "reactDelay=" << reactDelay;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(reactDelay));

    // 犹豫
    if (randProb() < HESITATE_PROB)
        std::this_thread::sleep_for(std::chrono::milliseconds(randInt(HESITATE_MIN_MS, HESITATE_MAX_MS)));

    auto now = std::chrono::steady_clock::now();
    static auto m_nextFireTime = std::chrono::steady_clock::now();
    int fireDelay = BASE_FIRE_DELAY_MS + randInt(VAR_FIRE_DELAY_MIN, VAR_FIRE_DELAY_MAX);

    if (now >= m_nextFireTime)
    {
        simulateHumanClick(); // 内部会刷新 g_last_fire_time
        m_nextFireTime = now + std::chrono::milliseconds(fireDelay);
        // g_just_fired 已在 simulateHumanClick 内置 true
    }
}
