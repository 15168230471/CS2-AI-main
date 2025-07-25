#pragma once
#include <qobject.h>
#include <string>
#include <math.h>
#include <mutex>
#include <qthread.h>
#include <iostream>
#include <memory>
#include "CS2/CS2AI.h"
#include "CS2/NavmeshPoints.h"
#include "CS2/ConfigReader.h"
enum class ModeRunning
{
    NONE = 0, AI, POINT_CREATOR
};

class CS2Runner : public QObject
{
    Q_OBJECT

public:
    CS2Runner();
    void update();
    void focusAndClipCS2Window();
    void set_mode(ModeRunning mode);
    void set_add_point_key(int key_code);
    bool save_navmesh_points();
    void add_point();
    void load_config();
    void load_offsets();
    void load_navmesh();
    void attach_to_process();
    void set_activated_behavior(const ActivatedFeatures& behavior);
    std::pair<bool, Vec3D<float>> get_current_position();
  
signals:
    void finished();
public slots:
    void run();

private:
    const std::chrono::milliseconds m_statusCheckInterval{ 5000 };
    std::chrono::steady_clock::time_point m_lastStatusCheck{ std::chrono::steady_clock::now() };
    void check_weapon_Status();
    void check_map_team_Status();
    std::mutex m_mutex;
    ModeRunning m_mode = ModeRunning::AI;
    bool m_is_running = true;
    std::unique_ptr<CS2Ai> m_cs2_ai_handler = nullptr;
    std::unique_ptr<NavmeshPoints> m_cs2_navmesh_points_handler = nullptr;
	int plan_weapon_id = 10768; // Default weapon ID for planning, ����
    //int plan_weapon_id = 525968; // Default weapon ID for planning, AK47

};