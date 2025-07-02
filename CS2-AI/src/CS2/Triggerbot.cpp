// CS2/Triggerbot.cpp

#include "CS2/Triggerbot.h"
#include <Windows.h>    // SendInput, INPUT, etc.
#include <random>
#include <chrono>
#include <thread>
#include <QDebug>

// ——— 随机工具 ———

// 全局随机引擎
static std::mt19937& rng() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    return gen;
}

// 返回 [min, max] 范围内的随机整数
static int randInt(int min, int max) {
    std::uniform_int_distribution<> dist(min, max);
    return dist(rng());
}

// ——— 模拟“人类”点击 ———

// 按下并随机“按住”一段时间后松开
static void simulateHumanClick()
{
    INPUT input = {};
    input.type = INPUT_MOUSE;

    // 鼠标左键按下
    input.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    SendInput(1, &input, sizeof(input));

    // 随机保持 15–30ms，再按键松开
    int holdTime = randInt(15, 30);
    std::this_thread::sleep_for(std::chrono::milliseconds(holdTime));

    // 鼠标左键松开
    input.mi.dwFlags = MOUSEEVENTF_LEFTUP;
    SendInput(1, &input, sizeof(input));
}

// ——— Triggerbot 更新逻辑 ———

void Triggerbot::update(GameInformationhandler* handler)
{
    if (!handler)
        return;

    const GameInformation game_info = handler->get_game_information();

    // 如果已经开过枪或玩家已死，则跳过
    if (game_info.controlled_player.shots_fired > 0
        || game_info.controlled_player.health <= 0)
    {
        return;
    }

    // 准星内无玩家，跳过
    if (!game_info.player_in_crosshair)
        return;

    int  targetHealth = game_info.player_in_crosshair->health;
    bool targetImmune = game_info.player_in_crosshair->isImmune;

    // 打印 immune 状态
    qDebug() << "[Triggerbot] targetImmune =" << targetImmune;

    if (targetHealth <= 0
        || targetHealth >= 200
        || targetImmune)   // 新增无敌状态检查
    {
        qDebug() << "[Triggerbot] skipping fire: health =" << targetHealth
            << ", immune =" << targetImmune;
        return;
    }

    // 当前高精度时钟
    auto now = std::chrono::steady_clock::now();

    // 基础延迟 20ms，±5ms 随机化
    constexpr int baseDelay = 20;
    int variableDelay = baseDelay + randInt(-5, 5);  // 15–25ms

    // 如果到达下一次可开火时间
    if (now >= m_nextFireTime)
    {
        simulateHumanClick();

        // 计算下一次可开火时间，防止多连点
        m_nextFireTime = now + std::chrono::milliseconds(variableDelay);
    }
}
