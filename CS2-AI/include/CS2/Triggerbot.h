// CS2/Triggerbot.h
#pragma once

#include "Utility/Utility.h"
#include "GameInformationHandler.h"
extern volatile bool g_just_fired;
class Triggerbot
{
public:
    void update(GameInformationhandler* handler);
    
private:
    long long m_delay_time = 0;
    // 用于控制下一次开火的时间点
    std::chrono::steady_clock::time_point m_nextFireTime{ std::chrono::steady_clock::now() };
};
