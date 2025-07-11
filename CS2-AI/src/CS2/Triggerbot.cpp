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
    constexpr int REACT_DELAY_MIN_MS = 60;
    constexpr int REACT_DELAY_MAX_MS = 100;
    constexpr float HESITATE_PROB = 0.05f;
    constexpr int HESITATE_MIN_MS = 5;
    constexpr int HESITATE_MAX_MS = 20;
    constexpr int HOLD_MIN_MS = 20;
    constexpr int HOLD_MAX_MS = 50;
    constexpr int BASE_FIRE_DELAY_MS = 350;
    constexpr int VAR_FIRE_DELAY_MIN = 0;
    constexpr int VAR_FIRE_DELAY_MAX = 100;
    // 新增：近距离定义
    constexpr float CLOSE_DIST_THRESHOLD = 1000.0f;
    constexpr int CLOSE_REACT_DELAY_MS = 10;
}

// ===== 随机工具函数 =====
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
}

// ——— 主逻辑 ———
void Triggerbot::update(GameInformationhandler* handler)
{

    using namespace TriggerbotHumanizedConfig;


    if (!handler)
        return;

    const GameInformation game_info = handler->get_game_information();
    // ========== 自动装弹逻辑 ==========
    // 假设gi.controlled_player.weapon_info.ammo_clip1 有弹夹数
    constexpr int RELOAD_THRESHOLD = 2;
    int ammo = game_info.controlled_player.weapon_info.ammo_clip1;
    bool has_target = bool(game_info.player_in_crosshair);
    int myhealth = game_info.controlled_player.health;
    // 每帧都打印这些关键数据
    /*qDebug() << "[Triggerbot] ammo=" << ammo
        << " RELOAD_THRESHOLD=" << RELOAD_THRESHOLD
        << " player_in_crosshair=" << (has_target ? "YES" : "NO")
        << " health=" << myhealth;*/
    // 没敌人/准星没目标/弹夹少，且未死亡时自动装弹
    if (!game_info.player_in_crosshair
        && ammo > -1 && ammo <= RELOAD_THRESHOLD
        && game_info.controlled_player.health > 0)
    {
        // 按下R（VK_R = 0x52）
        INPUT input = {};
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = 'R';
        SendInput(1, &input, sizeof(INPUT));
        // 松开
        input.ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(1, &input, sizeof(INPUT));
        /*qDebug() << "[Triggerbot] 自动装弹! 弹夹剩余:" << ammo;*/
    }

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

    // ========== 近距离优先快速反应 ==========
    float dist = game_info.controlled_player.position.distance(game_info.player_in_crosshair->position);
    int reactDelay;
    if (dist < CLOSE_DIST_THRESHOLD) {
        reactDelay = CLOSE_REACT_DELAY_MS;
        qDebug() << "[Triggerbot] Close enemy! dist=" << dist << "reactDelay=" << reactDelay;
    }
    else {
        reactDelay = randInt(REACT_DELAY_MIN_MS, REACT_DELAY_MAX_MS);
        qDebug() << "[Triggerbot] Normal dist=" << dist << "reactDelay=" << reactDelay;
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
        simulateHumanClick();
        m_nextFireTime = now + std::chrono::milliseconds(fireDelay);
        g_just_fired = true;
    }
    
    
}
