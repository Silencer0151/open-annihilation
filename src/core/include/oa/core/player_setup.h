// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

/* Per-player game-setup record (the block Player.info refers to). */
#ifndef OA_CORE_PLAYER_SETUP_H
#define OA_CORE_PLAYER_SETUP_H

#include "oa/core/types.h"

#define OA_PLAYER_SETUP_INFO_BYTES 0xb9

/* Bits of PlayerSetupInfo.options; the host's copy is the game setting. */
#define OA_SETUP_OPTION_STARTED 0x0010u
#define OA_SETUP_OPTION_READY 0x0020u
#define OA_SETUP_OPTION_WATCHER 0x0040u /* spectating slot; also blocks resource sharing */
#define OA_SETUP_OPTION_WATCHING_ALLOWED 0x0080u
#define OA_SETUP_OPTION_UNMAPPED 0x0100u
#define OA_SETUP_OPTION_LOS_LIMITED 0x0200u
#define OA_SETUP_OPTION_LOS_TRUE 0x0400u
#define OA_SETUP_OPTION_COMMANDER_MASK 0x1800u
#define OA_SETUP_OPTION_COMMANDER_SHIFT 11
#define OA_SETUP_OPTION_CHEATS_ALLOWED 0x2000u
#define OA_SETUP_OPTION_FIXED_LOCATIONS 0x4000u
#define OA_SETUP_OPTION_GAME_CLOSED 0x8000u

/* Bits of PlayerSetupInfo.status. */
#define OA_SETUP_STATUS_PASSWORD 0x0001u
#define OA_SETUP_STATUS_ALLIED_VICTORY 0x0002u
#define OA_SETUP_STATUS_HAS_DISC 0x0004u
#define OA_SETUP_STATUS_SERVICE 0x0008u

/* Bits of PlayerSetupInfo.chat_flags. */
#define OA_SETUP_CHAT_UTF8 0x01u /* the sender sends and reads chat as UTF-8 */

OA_CORE_BEGIN

#pragma pack(push, 1)

/* One player's game-setup settings. */
typedef struct PlayerSetupInfo {
    char map_name[0x80];
    char password[0xb];
    uint16_t screen_width;
    uint16_t screen_height;
    uint8_t reserved_after_screen_height; /* copied with the block; the engine never reads it */
    uint32_t player_id;
    uint8_t state; /* mirrors Player.status except for players on other machines */
    uint8_t side;
    uint8_t color;
    uint8_t role;
    uint8_t reserved_after_role; /* copied with the block; the engine never reads it */
    uint16_t memory_mb;
    uint16_t options;         /* SETUP_OPTION_*; unaligned */
    uint16_t status;          /* SETUP_STATUS_*; unaligned */
    uint16_t lowest_latency;  /* ? milliseconds */
    uint16_t energy_hundreds; /* starting energy / 100 */
    uint16_t metal_hundreds;  /* starting metal / 100 */
    uint16_t max_units;
    uint8_t version_major;
    uint8_t version_minor;
    uint32_t map_hash;
    uint8_t engine_signature[2]; /* 'O', 'A' when Open Annihilation sent it; 3.1c never reads it */
    uint8_t reserved_after_signature[0x5]; /* copied with the block; the engine never reads it */
    uint8_t recorder_protocol; /* the sender's recorder version, 0 for none; 3.1c never reads it */
    uint8_t chat_signature[2]; /* 'U', '8' when chat_flags holds the sender's chat */
    uint8_t chat_flags;        /* OA_SETUP_CHAT_*, read only after chat_signature */
    uint8_t reserved_after_chat_flags; /* copied with the block; the engine never reads it */
} PlayerSetupInfo;

#pragma pack(pop)

OA_ASSERT_SIZE(PlayerSetupInfo, OA_PLAYER_SETUP_INFO_BYTES);
OA_ASSERT_OFFSET(PlayerSetupInfo, map_name, 0x0);
OA_ASSERT_OFFSET(PlayerSetupInfo, password, 0x80);
OA_ASSERT_OFFSET(PlayerSetupInfo, screen_width, 0x8b);
OA_ASSERT_OFFSET(PlayerSetupInfo, screen_height, 0x8d);
OA_ASSERT_OFFSET(PlayerSetupInfo, reserved_after_screen_height, 0x8f);
OA_ASSERT_OFFSET(PlayerSetupInfo, player_id, 0x90);
OA_ASSERT_OFFSET(PlayerSetupInfo, state, 0x94);
OA_ASSERT_OFFSET(PlayerSetupInfo, side, 0x95);
OA_ASSERT_OFFSET(PlayerSetupInfo, color, 0x96);
OA_ASSERT_OFFSET(PlayerSetupInfo, role, 0x97);
OA_ASSERT_OFFSET(PlayerSetupInfo, reserved_after_role, 0x98);
OA_ASSERT_OFFSET(PlayerSetupInfo, memory_mb, 0x99);
OA_ASSERT_OFFSET(PlayerSetupInfo, options, 0x9b);
OA_ASSERT_OFFSET(PlayerSetupInfo, status, 0x9d);
OA_ASSERT_OFFSET(PlayerSetupInfo, lowest_latency, 0x9f);
OA_ASSERT_OFFSET(PlayerSetupInfo, energy_hundreds, 0xa1);
OA_ASSERT_OFFSET(PlayerSetupInfo, metal_hundreds, 0xa3);
OA_ASSERT_OFFSET(PlayerSetupInfo, max_units, 0xa5);
OA_ASSERT_OFFSET(PlayerSetupInfo, version_major, 0xa7);
OA_ASSERT_OFFSET(PlayerSetupInfo, version_minor, 0xa8);
OA_ASSERT_OFFSET(PlayerSetupInfo, map_hash, 0xa9);
OA_ASSERT_OFFSET(PlayerSetupInfo, engine_signature, 0xad);
OA_ASSERT_OFFSET(PlayerSetupInfo, reserved_after_signature, 0xaf);
OA_ASSERT_OFFSET(PlayerSetupInfo, recorder_protocol, 0xb4);
OA_ASSERT_OFFSET(PlayerSetupInfo, chat_signature, 0xb5);
OA_ASSERT_OFFSET(PlayerSetupInfo, chat_flags, 0xb7);
OA_ASSERT_OFFSET(PlayerSetupInfo, reserved_after_chat_flags, 0xb8);

OA_CORE_END

#endif
