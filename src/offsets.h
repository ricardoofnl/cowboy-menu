#pragma once
#include <stdint.h>

// rdr2 ps4 CUSA08519 game 1.32. handler offsets are resolved by scanning the eboot for each
// native hash (mov rcx,<hash> ; lea rdx,[rip+handler]); vaddr = file offset - 0x4000.
// the player/weapon/horse offsets get filled from the anchor scan once research lands.

#define GAME_TITLEID "CUSA08519"
#define GAME_VERSION "1.32"

// metaped satchel set, already verified in this eboot
#define HASH_SATCHELS                     0x94504D26u
#define OFF_GET_PLAYER_PED                0x21b9140ULL
#define OFF_IS_META_PED_USING_COMPONENT   0x21455e0ULL
#define OFF_REMOVE_TAG_FROM_META_PED      0x2145a60ULL
#define OFF_UPDATE_PED_VARIATION          0x2146070ULL
#define OFF_SET_ACTIVE_META_COMPS_UPDATED 0x214e040ULL

// TODO: player / weapon / horse handler offsets from the anchor scan