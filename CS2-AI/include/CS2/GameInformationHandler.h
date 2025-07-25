#pragma once
#include <iostream>
#include <vector>
#include <optional>
#include "Offsets.h"
#include "Utility/Vec3D.h"
#include "Utility/Vec2D.h"
#include "MemoryManager.h"
#include "ConfigReader.h"


struct Movement
{
	bool forward = false;
	bool backward = false;
	bool left = false;
	bool right = false;
};

struct WeaponInfo {
	uint16_t weapon_id;
	int32_t  ammo_clip1;
	// 你可以扩展其他字段
};


struct ControlledPlayer 
{
	Vec2D<float> view_vec;
	Vec3D<float> position;
	Vec3D<float> head_position;
	Vec3D<float> chest_position;
	Movement movement;
	DWORD shooting;
	DWORD shots_fired;
	int team;
	int health;
	bool isImmune;
	WeaponInfo weapon_info;  // 包含武器ID和子弹数;

};

struct PlayerInformation 
{
	Vec3D<float> position;
	Vec3D<float> head_position;
	Vec3D<float> chest_position;
	int team;
	int health;
	bool isImmune;
	DWORD shots_fired;
	uintptr_t pawn_addr;
};



struct GameInformation 
{
	ControlledPlayer controlled_player;
	std::vector<PlayerInformation> other_players;
	std::optional<PlayerInformation> player_in_crosshair;
	std::optional<PlayerInformation> closest_enemy_player;
	char current_map[64] = "";
	
};

class GameInformationhandler
{
public:
	GameInformationhandler() = default;
	bool init(const Config& config);
	bool loadOffsets();
	void update_game_information();

	GameInformation get_game_information() const;
	bool is_attached_to_process()const;
	void set_player_movement(const Movement& movement);
	WeaponInfo get_weapon_info(uintptr_t local_player_pawn);

private:
	ControlledPlayer read_controlled_player_information(uintptr_t player_address);
	std::vector<PlayerInformation> read_other_players(uintptr_t player_address);
	Movement read_controlled_player_movement(uintptr_t player_address);
	Vec3D<float> get_head_bone_position(uintptr_t player_pawn);
	Vec3D<float> get_chest_bone_position(uintptr_t player_pawn);
	uintptr_t get_list_entity(uintptr_t id, uintptr_t entity_list);
	uintptr_t get_entity_controller_or_pawn(uintptr_t list_entity, uintptr_t id);
	std::optional<PlayerInformation> read_player(uintptr_t entity_list_begin, uintptr_t id, uintptr_t player_address);
	std::optional<PlayerInformation> read_player_in_crosshair(uintptr_t player_controller, uintptr_t player_pawn);
	std::optional<PlayerInformation> get_closest_enemy(const GameInformation& game_info);
	void read_in_current_map(char* buffer, size_t buffer_size);
	bool read_in_if_controlled_player_is_shooting();
	

	bool m_attached_to_process = false;
	GameInformation m_game_information;
	MemoryManager m_process_memory;
	uintptr_t m_client_dll_address = 0;
	Offsets m_offsets = {};
	Config m_config = {};
};