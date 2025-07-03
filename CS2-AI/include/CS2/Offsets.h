#pragma once
#include <wtypes.h>
#include <fstream>
#include <optional>
#include "Utility/json.hpp"
#include "Utility/Logging.h"

struct Offsets
{
public:
	//Will be read from the offsets files
	uintptr_t local_player_controller_offset;
	uintptr_t local_player_pawn;
	uintptr_t crosshair_offset;
	uintptr_t shots_fired_offset;
	uintptr_t entity_list_start_offset;
	uintptr_t player_health_offset;
	uintptr_t team_offset;
	uintptr_t entity_listelement_size;
	uintptr_t position;
	uintptr_t force_attack;
	uintptr_t force_forward;
	uintptr_t force_backward;
	uintptr_t force_left;
	uintptr_t force_right;
	uintptr_t model_state;
	uintptr_t sceneNode;
	uintptr_t player_pawn_handle;
	uintptr_t client_state_view_angle;
	uintptr_t current_map;
	uintptr_t global_vars;
	uintptr_t network_game_client;
	uintptr_t gun_game_immunity;  // 死斗时是否无敌状态
	uintptr_t m_pWeaponServices;    // Pawn结构体里 WeaponServices指针的偏移
	uintptr_t m_hActiveWeapon;      // WeaponServices结构体里的 m_hActiveWeapon 偏移
	uintptr_t weapon_id_offset;     // 武器实体结构体里的 m_iItemDefinitionIndex 偏移


};

std::optional <Offsets> load_offsets_from_files();
