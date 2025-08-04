#pragma once

#include <windows.h>
#include <tlhelp32.h>

#include <qobject.h>
#include <string>
#include <mutex>
#include <memory>
#include <chrono>
#include <atomic>

#include "CS2/CS2AI.h"
#include "CS2/NavmeshPoints.h"
#include "CS2/ConfigReader.h"

// 运行模式
enum class ModeRunning
{
    NONE = 0,
    AI,
    POINT_CREATOR
};

class CS2Runner : public QObject
{
    Q_OBJECT

public:
    CS2Runner();

    // 核心循环，每帧调用
    void update();

    // 置前并限制鼠标在 CS2 客户区
    void focusAndClipCS2Window();

    // 各种设置接口
    void set_mode(ModeRunning mode);
    void set_add_point_key(int key_code);
    bool save_navmesh_points();
    void add_point();
    void load_config();
    void load_offsets();
    void load_navmesh();
    void attach_to_process(); // 尝试附加 CS2 进程（会做节流）

    void set_activated_behavior(const ActivatedFeatures& behavior);
    std::pair<bool, Vec3D<float>> get_current_position();

    // 核心背景图/排队入口（内部封装背景图周期逻辑）
    void check_matchmaking_status();

signals:
    void finished();

public slots:
    void run();

private:
    // ================== 时间/周期控制（都在这里集中定义、注释） ==================

    // 附着 CS2 进程的重试周期（每 5 秒尝试一次，如果尚未附加）
    const std::chrono::seconds kAttachRetryInterval{ 5 };
    std::chrono::steady_clock::time_point m_lastAttachAttempt{ std::chrono::steady_clock::now() - kAttachRetryInterval };
    std::atomic<bool> m_attached{ false }; // 标记是否已成功附加到 CS2 进程

    // 普通状态检查（例如队伍/选人）周期（目前用作 map/team 检查节奏）
    const std::chrono::seconds m_statusCheckInterval{ 10 };
    std::chrono::steady_clock::time_point m_lastStatusCheck{ std::chrono::steady_clock::now() };

    // 背景图检测和点击序列逻辑的节奏（比如 3 分钟检测一次 BackgroundMap）
    const std::chrono::minutes kBackgroundCheckInterval{ 3 };
    std::chrono::steady_clock::time_point m_lastBackgroundCheck{ std::chrono::steady_clock::now() - kBackgroundCheckInterval };
    bool m_sequenceAttempted = false; // 记录是否已经执行过点击序列（第一次 vs 第二次行为不同）

    // 段位/等级输出周期（避免每帧刷）
    const std::chrono::minutes kRankLogInterval{ 3 };
    std::chrono::steady_clock::time_point m_last_rank_log{ std::chrono::steady_clock::now() };
    int m_last_recorded_rank = -1; // 上一次有效的 rank / profile level，用于升级检测

    // ================== 功能模块 ==================

    // 检查武器/免疫状态
    void check_weapon_Status();

    // 检查地图/队伍状态（比如自动选队等）
    void check_map_team_Status();

    // 判定当前是否是背景图（封装底层 game info handler）
    bool is_background_map();

    // 封装的段位/等级打印 & 升级检测逻辑
    void log_rank_if_due();

    // 点击、窗口激活、以及进程控制工具
    void perform_click_sequence(HWND hwnd);
    void click_in_window(HWND hwnd, POINT clientPt);
    void terminate_process_by_name(const std::wstring& name);

    // ================== 内部状态 ==================

    std::mutex m_mutex;
    ModeRunning m_mode = ModeRunning::AI;
    bool m_is_running = true;

    std::unique_ptr<CS2Ai> m_cs2_ai_handler = nullptr;
    std::unique_ptr<NavmeshPoints> m_cs2_navmesh_points_handler = nullptr;

    // 计划用的武器 id 用于 buy weapon once 逻辑
    int plan_weapon_id = 13776; // 默认连喷

    // 可扩展：如果有其它行为设置
};
