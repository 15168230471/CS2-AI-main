// CS2/Triggerbot.cpp

#include "CS2/Triggerbot.h"
#include <Windows.h>    // SendInput, INPUT, etc.
#include <random>
#include <chrono>
#include <thread>
#include <QDebug>

// ������ ������� ������

// ȫ���������
static std::mt19937& rng() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    return gen;
}

// ���� [min, max] ��Χ�ڵ��������
static int randInt(int min, int max) {
    std::uniform_int_distribution<> dist(min, max);
    return dist(rng());
}

// ������ ģ�⡰���ࡱ��� ������

// ���²��������ס��һ��ʱ����ɿ�
static void simulateHumanClick()
{
    INPUT input = {};
    input.type = INPUT_MOUSE;

    // ����������
    input.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    SendInput(1, &input, sizeof(input));

    // ������� 15�C30ms���ٰ����ɿ�
    int holdTime = randInt(15, 30);
    std::this_thread::sleep_for(std::chrono::milliseconds(holdTime));

    // �������ɿ�
    input.mi.dwFlags = MOUSEEVENTF_LEFTUP;
    SendInput(1, &input, sizeof(input));
}

// ������ Triggerbot �����߼� ������

void Triggerbot::update(GameInformationhandler* handler)
{
    if (!handler)
        return;

    const GameInformation game_info = handler->get_game_information();

    // ����Ѿ�����ǹ�����������������
    if (game_info.controlled_player.shots_fired > 0
        || game_info.controlled_player.health <= 0)
    {
        return;
    }

    // ׼��������ң�����
    if (!game_info.player_in_crosshair)
        return;

    int  targetHealth = game_info.player_in_crosshair->health;
    bool targetImmune = game_info.player_in_crosshair->isImmune;

    // ��ӡ immune ״̬
    qDebug() << "[Triggerbot] targetImmune =" << targetImmune;

    if (targetHealth <= 0
        || targetHealth >= 200
        || targetImmune)   // �����޵�״̬���
    {
        qDebug() << "[Triggerbot] skipping fire: health =" << targetHealth
            << ", immune =" << targetImmune;
        return;
    }

    // ��ǰ�߾���ʱ��
    auto now = std::chrono::steady_clock::now();

    // �����ӳ� 20ms����5ms �����
    constexpr int baseDelay = 20;
    int variableDelay = baseDelay + randInt(-5, 5);  // 15�C25ms

    // ���������һ�οɿ���ʱ��
    if (now >= m_nextFireTime)
    {
        simulateHumanClick();

        // ������һ�οɿ���ʱ�䣬��ֹ������
        m_nextFireTime = now + std::chrono::milliseconds(variableDelay);
    }
}
