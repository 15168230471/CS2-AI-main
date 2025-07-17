#include "UI/CS2Runner.h"
#include <windows.h>


extern volatile bool g_isPaused;

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
		// 1 ms sleep because else activating the behaviors would take too long since the lock is taken all the time
		// Could be solved by e.g. making the "ActivatedFeatures" atomic but for now the sleep should be enough
		Sleep(1);
	}

	deleteLater();
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

	// 静态变量记录上次不无敌时刻（单位ms）
	static DWORD last_not_immune_tick = GetTickCount();


	DWORD now = GetTickCount();

	/*std::cout << "PlayisImmune" << PlayisImmune << std::endl;
	
	std::cout << "health" << health << std::endl;
	std::cout << now << std::endl;

	std::cout << last_not_immune_tick << std::endl;*/
	/*std::cout << weaponId << std::endl;*/
	
	// 满足条件时且没触发过
	if ( (now - last_not_immune_tick >10000)
		&& PlayisImmune == 1
		&& weaponId != plan_weapon_id
		&& health != 0)
	{
		
		// 按键码
		const WORD keys[] = { 'B', '3', '2', VK_ESCAPE };
		const int keyCount = 4;

		for (int i = 0; i < keyCount; ++i) {
			// 按下
			INPUT keyDown = {};
			keyDown.type = INPUT_KEYBOARD;
			keyDown.ki.wVk = keys[i];
			keyDown.ki.dwFlags = 0;
			SendInput(1, &keyDown, sizeof(INPUT));

			// 短延迟（模拟真人按键）
			Sleep(35 + rand() % 40); // 35~75ms

			// 松开
			INPUT keyUp = {};
			keyUp.type = INPUT_KEYBOARD;
			keyUp.ki.wVk = keys[i];
			keyUp.ki.dwFlags = KEYEVENTF_KEYUP;
			SendInput(1, &keyUp, sizeof(INPUT));

			Sleep(75 + rand() % 50); // 75~125ms
		}
		last_not_immune_tick = now;
		
		std::cout << "[DEBUG] buy weapon once!" << std::endl;
	}
	// 如果当前处于非无敌状态，更新时间戳
	if ( weaponId == plan_weapon_id) {
		last_not_immune_tick = now;   // 只要你变成无敌 或 拿了plan_weapon_id（） 或死亡，tick刷新
		
	}
}
	
void CS2Runner::update()
{
	if (g_isPaused)
		return;
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
		m_cs2_navmesh_points_handler->update();
}
void CS2Runner::focusAndClipCS2Window() {
	auto opt_cfg = Config::read_in_config_data();
	std::string winname = "Counter-Strike 2"; // 默认
	if (opt_cfg.has_value()) {
		winname = opt_cfg->windowname;
	}
	// 转wstring
	std::wstring wname(winname.begin(), winname.end());
	HWND hwnd = FindWindowW(NULL, wname.c_str());
	if (hwnd) {
		SetForegroundWindow(hwnd);
		// ====== 改为锁定客户区 ======
		RECT clientRect;
		if (GetClientRect(hwnd, &clientRect)) {
			POINT tl = { clientRect.left, clientRect.top };
			POINT br = { clientRect.right, clientRect.bottom };
			ClientToScreen(hwnd, &tl);
			ClientToScreen(hwnd, &br);
			RECT clipRect = { tl.x, tl.y, br.x, br.y };
			ClipCursor(&clipRect);
		}
		// ====== 结束 ======
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
	bool success = m_cs2_navmesh_points_handler->save_to_file();

	return success;
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
