// CS2/Triggerbot.h
#pragma once

#include "Utility/Utility.h"
#include "GameInformationHandler.h"

class Triggerbot
{
public:
    void update(GameInformationhandler* handler);

private:
    long long m_delay_time = 0;
    // ���ڿ�����һ�ο����ʱ���
    std::chrono::steady_clock::time_point m_nextFireTime{ std::chrono::steady_clock::now() };
};
