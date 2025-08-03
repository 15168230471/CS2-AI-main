#include "UI/CS2Runner.h"
#include <windows.h>
#include <tlhelp32.h>
#include <thread>
#include <chrono>
#include <algorithm>
#include <random>
#include <iostream>

extern volatile bool g_isPaused;

// ---------- 拟人化随机工具 ----------
static std::mt19937& rng() {
    static thread_local std::random_device rd;
    static thread_local std::mt19937 gen(rd());
    return gen;
}
static int rand_int(int a, int b) {
    std::uniform_int_distribution<int> dist(a, b);
    return dist(rng());
}
static double rand_double() {
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    return dist(rng());
}

// 拟人化移动（ease-in-out + 轻微抖动）
void human_move_mouse(POINT start, POINT target, int duration_ms) {
    const int steps = std::max(5, duration_ms / 15);
    for (int i = 1; i <= steps; ++i) {
        double t = (double)i / steps;
        double ease = t * t * (3 - 2 * t); // smoothstep
        double jitter_scale = (1.0 - ease) * 3.0;
        double dx = (target.x - start.x) * ease + (rand_double() - 0.5) * jitter_scale;
        double dy = (target.y - start.y) * ease + (rand_double() - 0.5) * jitter_scale;
        POINT stepPt = { static_cast<LONG>(start.x + dx), static_cast<LONG>(start.y + dy) };
        SetCursorPos(stepPt.x, stepPt.y);
        int sleep_ms = 10 + rand_int(-3, 3);
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
    }
    SetCursorPos(target.x, target.y); // 最终对齐
}

void human_click_at(POINT screenPt) {
    std::this_thread::sleep_for(std::chrono::milliseconds(rand_int(30, 120))); // 微犹豫

    INPUT down{ 0 };
    down.type = INPUT_MOUSE;
    down.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    SendInput(1, &down, sizeof(down));

    std::this_thread::sleep_for(std::chrono::milliseconds(rand_int(50, 120))); // 按住时间

    INPUT up{ 0 };
    up.type = INPUT_MOUSE;
    up.mi.dwFlags = MOUSEEVENTF_LEFTUP;
    SendInput(1, &up, sizeof(up));
}

// --------------------------------------------------
// CS2Runner 实现
// --------------------------------------------------

CS2Runner::CS2Runner() : QObject(nullptr)
{
    m_cs2_ai_handler = std::make_unique<CS2Ai>();
    m_cs2_navmesh_points_handler = std::make_unique<NavmeshPoints>(m_cs2_ai_handler->get_game_info_handler());
    load_config();
    load_offsets();
    attach_to_process();
}

void CS2Runner::run()
{
    while (m_is_running)
    {
        update();
        Sleep(1);
    }
    deleteLater();
}

// 工具：从配置获取 CS2 窗口句柄
static HWND get_cs2_hwnd_from_config()
{
    auto opt_cfg = Config::read_in_config_data();
    std::string winname = "Counter-Strike 2";
    if (opt_cfg.has_value()) {
        winname = opt_cfg->windowname;
    }
    std::wstring wname(winname.begin(), winname.end());
    return FindWindowW(NULL, wname.c_str());
}

bool CS2Runner::is_background_map()
{
    auto handler = m_cs2_ai_handler->get_game_info_handler();
    if (!handler)
        return false;
    return handler->is_background_map();
}

// 拟人化客户区点击（替代原来的 click_in_window）
void CS2Runner::click_in_window(HWND hwnd, POINT clientPt)
{
    if (!hwnd) return;

    // 先激活窗口（不过不要太机械）
    SetForegroundWindow(hwnd);
    std::this_thread::sleep_for(std::chrono::milliseconds(rand_int(30, 80)));

    // 客户区转屏幕坐标
    POINT screenPt = clientPt;
    if (!ClientToScreen(hwnd, &screenPt)) {
        std::cerr << "[WARN] ClientToScreen failed\n";
        return;
    }

    // 当前鼠标位置
    POINT current;
    GetCursorPos(&current);

    // 平滑移动到目标
    int moveDuration = rand_int(180, 380); // 拟人速度
    human_move_mouse(current, screenPt, moveDuration);

    // 20% 概率轻微偏移再修正（模拟人手抖）
    if (rand_double() < 0.2) {
        POINT miss = { screenPt.x + rand_int(-5, 5), screenPt.y + rand_int(-5, 5) };
        SetCursorPos(miss.x, miss.y);
        std::this_thread::sleep_for(std::chrono::milliseconds(rand_int(50, 100)));
        human_move_mouse(miss, screenPt, rand_int(80, 140));
    }

    // 点击
    human_click_at(screenPt);
}
bool activate_window_strong(HWND target)
{
    if (!target) return false;

    // 获取前台窗口线程/目标窗口线程
    DWORD foregroundThread = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
    DWORD targetThread = GetWindowThreadProcessId(target, nullptr);
    DWORD currentThread = GetCurrentThreadId();

    // 绑定输入线程（防止 SetForegroundWindow 失败）
    AttachThreadInput(currentThread, targetThread, TRUE);
    AttachThreadInput(foregroundThread, targetThread, TRUE);

    // 激活和置顶
    SetForegroundWindow(target);
    SetActiveWindow(target);
    SetWindowPos(target, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    
    // 确认前台（最多等 500ms）
    auto start = std::chrono::steady_clock::now();
    bool success = false;
    while (std::chrono::steady_clock::now() - start < std::chrono::milliseconds(500)) {
        if (GetForegroundWindow() == target) {
            success = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // 解绑
    AttachThreadInput(currentThread, targetThread, FALSE);
    AttachThreadInput(foregroundThread, targetThread, FALSE);

    if (!success) {
        std::cout << "[WARN] activate_window_strong: failed to make target foreground\n";
    }
    return success;
}
void CS2Runner::perform_click_sequence(HWND hwnd)
{
    if (!hwnd) return;

    // 如果你持有 MainWindow 指针，这里最小化一下（需要传入或全局访问）
    // e.g., if (mainWindowPtr) mainWindowPtr->showMinimized();

    // 强激活 CS2：AttachThreadInput + 置前
    bool activated = activate_window_strong(hwnd);
    if (!activated) {
        // 失败可以再试一次短暂延迟后重试
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        activate_window_strong(hwnd);
    }

    // 额外小缓冲（拟人）
    std::this_thread::sleep_for(std::chrono::milliseconds(rand_int(200, 400)));

    // 3. 再额外缓冲一段拟人化间隔，防止 overlay/主窗口干扰
    std::this_thread::sleep_for(std::chrono::milliseconds(rand_int(200, 400)));
    const POINT sequence[] = {
    {803, 400},  // "确定" (443-80)
    {785, 569},  // "关闭" (612-80)
    {661, 18},  // "开始" 顶部菜单 (61-80)  // 注意：变成负值的话可能不合法，需要夹在客户区内
    {552, 56},   // "匹配" (94-80)
    {710, 82},   // "死亡竞赛" (125-80)
    {341, 376},  // "炸弹拆除地图组1号" (419-80)
    {1121, 682}  // "开始" 底部菜单 (725-80)
    };


    for (const auto& pt : sequence) {
        click_in_window(hwnd, pt);
        // 拟人间隔：1.2~1.8 秒
        std::this_thread::sleep_for(std::chrono::milliseconds(rand_int(1000, 1500)));
    }
}

void CS2Runner::terminate_process_by_name(const std::wstring& name)
{
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return;

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry))
    {
        do
        {
            if (_wcsicmp(entry.szExeFile, name.c_str()) == 0)
            {
                HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, entry.th32ProcessID);
                if (hProc)
                {
                    TerminateProcess(hProc, 0);
                    CloseHandle(hProc);
                    std::wcout << L"[INFO] Terminated process: " << name
                        << L" (PID=" << entry.th32ProcessID << L")\n";
                }
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
}

// 每 5 分钟基于 BackgroundMap 做一次判断/操作
void CS2Runner::check_matchmaking_status()
{
    auto now = std::chrono::steady_clock::now();
    if (now - m_lastBackgroundCheck < std::chrono::minutes(1))
        return;
    m_lastBackgroundCheck = now;

    bool background = is_background_map();
    std::cout << "[INFO] BackgroundMap=" << (background ? "true" : "false") << std::endl;

    HWND hwnd = get_cs2_hwnd_from_config();

    if (background)
    {
        if (!m_sequenceAttempted)
        {
            std::cout << "[INFO] First background detected, performing click sequence\n";
            perform_click_sequence(hwnd);
            m_sequenceAttempted = true;
        }
        else
        {
            std::cout << "[INFO] Background still present after prior sequence, terminating CS2/Steam and exiting\n";
            terminate_process_by_name(L"cs2.exe");
            terminate_process_by_name(L"steam.exe");
            std::exit(0);
        }
    }
    else
    {
        if (m_sequenceAttempted)
            std::cout << "[INFO] Background cleared, resetting sequenceAttempted flag\n";
        m_sequenceAttempted = false;
    }
}

void CS2Runner::check_map_team_Status()
{
    auto game_info = m_cs2_ai_handler->get_game_info_handler()->get_game_information();
    int team = game_info.controlled_player.team;
    std::string mapName = "";
    if (mapName == "") {
        mapName = std::string(game_info.current_map);
        std::replace(mapName.begin(), mapName.end(), '/', '_');
    }

    std::cout << team << mapName << std::endl;
    if ((team == 0) && (mapName != "<empty>")) {
        std::cout << "[DEBUG] You have NOT selected a team yet!" << std::endl;
        INPUT inputs[2] = {};
        inputs[0].type = INPUT_KEYBOARD;
        inputs[0].ki.wVk = '2';
        inputs[0].ki.dwFlags = 0;
        inputs[1].type = INPUT_KEYBOARD;
        inputs[1].ki.wVk = '2';
        inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(2, inputs, sizeof(INPUT));
    }
}

void CS2Runner::check_weapon_Status()
{
    auto game_info = m_cs2_ai_handler->get_game_info_handler()->get_game_information();
    bool PlayisImmune = game_info.controlled_player.isImmune;
    uint16_t weaponId = game_info.controlled_player.weapon_info.weapon_id;
    int health = game_info.controlled_player.health;

    static DWORD last_not_immune_tick = GetTickCount();
    DWORD now = GetTickCount();

    if ((now - last_not_immune_tick > 10000)
        && PlayisImmune == 1
        && weaponId != plan_weapon_id
        && health != 0)
    {
        const WORD keys[] = { 'B', '3', '2', VK_ESCAPE };
        const int keyCount = 4;

        for (int i = 0; i < keyCount; ++i) {
            INPUT keyDown = {};
            keyDown.type = INPUT_KEYBOARD;
            keyDown.ki.wVk = keys[i];
            keyDown.ki.dwFlags = 0;
            SendInput(1, &keyDown, sizeof(INPUT));

            Sleep(35 + rand() % 40);

            INPUT keyUp = {};
            keyUp.type = INPUT_KEYBOARD;
            keyUp.ki.wVk = keys[i];
            keyUp.ki.dwFlags = KEYEVENTF_KEYUP;
            SendInput(1, &keyUp, sizeof(INPUT));

            Sleep(75 + rand() % 50);
        }
        last_not_immune_tick = now;

        std::cout << "[DEBUG] buy weapon once!" << std::endl;
    }

    if (weaponId == plan_weapon_id) {
        last_not_immune_tick = now;
    }
}

void CS2Runner::update()
{
    if (g_isPaused)
        return;

    // 先做 background map 检查/控制流（在地图判断之前）
    check_matchmaking_status();
    if (m_sequenceAttempted && is_background_map()) {
        return; // short-circuit：刚做过 background 点击，跳过后续
    }

    auto now = std::chrono::steady_clock::now();
    if (now - m_lastStatusCheck >= m_statusCheckInterval) {
        m_lastStatusCheck = now;
        check_map_team_Status();
    }

    check_weapon_Status();

    std::scoped_lock lock(m_mutex);
    if (m_mode == ModeRunning::AI)
    {
        m_cs2_ai_handler->update();
        focusAndClipCS2Window();
    }
    else if (m_mode == ModeRunning::POINT_CREATOR)
    {
        m_cs2_navmesh_points_handler->update();
    }
}

void CS2Runner::focusAndClipCS2Window() {
    auto opt_cfg = Config::read_in_config_data();
    std::string winname = "Counter-Strike 2";
    if (opt_cfg.has_value()) {
        winname = opt_cfg->windowname;
    }
    std::wstring wname(winname.begin(), winname.end());
    HWND hwnd = FindWindowW(NULL, wname.c_str());
    if (hwnd) {
        SetForegroundWindow(hwnd);
        RECT clientRect;
        if (GetClientRect(hwnd, &clientRect)) {
            POINT tl = { clientRect.left, clientRect.top };
            POINT br = { clientRect.right, clientRect.bottom };
            ClientToScreen(hwnd, &tl);
            ClientToScreen(hwnd, &br);
            RECT clipRect = { tl.x, tl.y, br.x, br.y };
            ClipCursor(&clipRect);
        }
    }
}

void CS2Runner::set_mode(ModeRunning mode)
{
    std::scoped_lock lock(m_mutex);
    m_mode = mode;
}

void CS2Runner::set_add_point_key(int key_code)
{
    std::scoped_lock lock(m_mutex);
    m_cs2_navmesh_points_handler->set_add_point_button(key_code);
}

bool CS2Runner::save_navmesh_points()
{
    std::scoped_lock lock(m_mutex);
    return m_cs2_navmesh_points_handler->save_to_file();
}

void CS2Runner::add_point()
{
    std::scoped_lock lock(m_mutex);
    m_cs2_navmesh_points_handler->add_point();
}

void CS2Runner::load_config()
{
    std::scoped_lock lock(m_mutex);
    m_cs2_ai_handler->load_config();
}

void CS2Runner::load_offsets()
{
    std::scoped_lock lock(m_mutex);
    m_cs2_ai_handler->load_offsets();
}

void CS2Runner::load_navmesh()
{
    std::scoped_lock lock(m_mutex);
    m_cs2_ai_handler->load_navmesh();
}

void CS2Runner::attach_to_process()
{
    std::scoped_lock lock(m_mutex);
    if (!m_cs2_ai_handler->attach_to_cs2_process())
        Logging::log_error("Error getting dll address / Error attaching to CS2 process");
    else
        Logging::log_success("Attached to the CS2 process");
}

void CS2Runner::set_activated_behavior(const ActivatedFeatures& behavior)
{
    std::scoped_lock lock(m_mutex);
    m_cs2_ai_handler->set_activated_behavior(behavior);
}

std::pair<bool, Vec3D<float>> CS2Runner::get_current_position()
{
    std::scoped_lock lock(m_mutex);
    return m_cs2_navmesh_points_handler->get_current_position();
}
