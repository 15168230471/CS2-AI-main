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
	std::string weaponName = "";
	if (weaponName == "") {
		weaponName = std::string(game_info.player_weapon);
		std::replace(weaponName.begin(), weaponName.end(), '/', '_');
		std::cout << "[DEBUG]  Weapon Name:" << std::endl;
		std::cout << weaponName << std::endl;
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
		inputs[0].ki.wVk = '1';
		inputs[0].ki.dwFlags = 0;
		inputs[1].type = INPUT_KEYBOARD;
		inputs[1].ki.wVk = '1';
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
