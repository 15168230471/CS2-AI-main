#include "UI/CS2Runner.h"
#include <windows.h>

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

void CS2Runner::checkPlayerStatus()
{
	auto game_info = m_cs2_ai_handler->get_game_info_handler()->get_game_information();
	int team = game_info.controlled_player.team;
	std::string mapName = "";
	bool PlayisImmune = game_info.controlled_player.isImmune;
	uint16_t weaponId = game_info.controlled_player.weaponid;
	// 新增逻辑：isImmune且weaponId不为6720则依次按B-3-2-ESC
	if (PlayisImmune == 1 && weaponId != 6720) {
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

			// 随机短延迟（按住）
			Sleep(35 + rand() % 40); // 35~75ms

			// 松开
			INPUT keyUp = {};
			keyUp.type = INPUT_KEYBOARD;
			keyUp.ki.wVk = keys[i];
			keyUp.ki.dwFlags = KEYEVENTF_KEYUP;
			SendInput(1, &keyUp, sizeof(INPUT));

			// 两个键之间再停一会儿（模拟思考/操作速度）
			Sleep(75 + rand() % 50); // 75~125ms
		}
	}
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
	
void CS2Runner::update()
{
	
	auto now = std::chrono::steady_clock::now();
	if (now - m_lastStatusCheck >= m_statusCheckInterval) {
		m_lastStatusCheck = now;
		checkPlayerStatus();
	}
	
	std::scoped_lock lock(m_mutex);
	if (m_mode == ModeRunning::AI)
		m_cs2_ai_handler->update();
	else if (m_mode == ModeRunning::POINT_CREATOR)
		m_cs2_navmesh_points_handler->update();
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
