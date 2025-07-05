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
    // 反应成功概率
    constexpr float REACT_PROBABILITY = 0.97f;         // 95%概率响应
    // 反应延迟范围（毫秒）
    constexpr int REACT_DELAY_MIN_MS = 80;             // 最低反应时间
    constexpr int REACT_DELAY_MAX_MS = 180;            // 最高反应时间
    // 犹豫再加延迟概率与范围
    constexpr float HESITATE_PROB = 0.05f;             // 10%概率继续犹豫
    constexpr int HESITATE_MIN_MS = 10;                // 犹豫时最小追加时间
    constexpr int HESITATE_MAX_MS = 30;               // 最大追加时间
    // 鼠标点击按住时长
    constexpr int HOLD_MIN_MS = 20;
    constexpr int HOLD_MAX_MS = 50;
    // 两次点击最小间隔（ms）
    constexpr int BASE_FIRE_DELAY_MS = 25;
    constexpr int VAR_FIRE_DELAY_MIN = -8;
    constexpr int VAR_FIRE_DELAY_MAX = 15;
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
    // 鼠标左键按下
    input.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    SendInput(1, &input, sizeof(input));

    // 保持一段时间再松开
    int holdTime = randInt(TriggerbotHumanizedConfig::HOLD_MIN_MS, TriggerbotHumanizedConfig::HOLD_MAX_MS);
    std::this_thread::sleep_for(std::chrono::milliseconds(holdTime));

    // 鼠标左键松开
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

    // 已开枪或已死亡跳过
    if (game_info.controlled_player.shots_fired > 0
        || game_info.controlled_player.health <= 0)
        return;

    // 无准星目标跳过
    if (!game_info.player_in_crosshair)
        return;

    int  targetHealth = game_info.player_in_crosshair->health;
    bool targetImmune = game_info.player_in_crosshair->isImmune;
    if (targetHealth <= 0 || targetHealth >= 200 || targetImmune)
        return;

    // 反应概率
    if (randProb() > REACT_PROBABILITY)
        return;

    // 拟人反应延迟
    int reactDelay = randInt(REACT_DELAY_MIN_MS, REACT_DELAY_MAX_MS);
    std::this_thread::sleep_for(std::chrono::milliseconds(reactDelay));

    // 犹豫：10%再多等一会
    if (randProb() < HESITATE_PROB)
        std::this_thread::sleep_for(std::chrono::milliseconds(randInt(HESITATE_MIN_MS, HESITATE_MAX_MS)));

    auto now = std::chrono::steady_clock::now();

    // 点击间隔
    static auto m_nextFireTime = std::chrono::steady_clock::now();
    int fireDelay = BASE_FIRE_DELAY_MS + randInt(VAR_FIRE_DELAY_MIN, VAR_FIRE_DELAY_MAX);

    if (now >= m_nextFireTime)
    {
        simulateHumanClick();
        m_nextFireTime = now + std::chrono::milliseconds(fireDelay);
        g_just_fired = true;
    }
}
