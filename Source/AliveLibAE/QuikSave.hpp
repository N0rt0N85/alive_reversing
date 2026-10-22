#pragma once

#include "../AliveLibCommon/FunctionFwd.hpp"
#include "MainMenu.hpp"
#include "SwitchStates.hpp"

struct Quicksave_PSX_Header final
{
    char_type field_0_frame_1_name[128];
    s8 field_80_frame_2_padding[128];
    s8 field_100_frame_3_padding[128];
    s8 field_180_frame_4_padding[128];
};
ALIVE_ASSERT_SIZEOF(Quicksave_PSX_Header, 0x200);

enum class LevelIds : s16;

struct Quicksave_WorldInfo final
{
    s32 field_0_gnFrame;
    LevelIds field_4_level;
    s16 field_6_path;
    s16 field_8_cam;
    s16 field_A_save_num;
    s16 field_C_controlled_x;
    s16 field_E_controlled_y;
    s16 field_10_controlled_scale;
    s16 field_12_saved_muds;
    s16 field_14_killed_muds;
    s8 field_16_muds_in_area;
    s8 field_17_last_saved_killed_muds_per_path;
    s8 field_18_saved_killed_muds_per_zulag[20];
    s8 field_2C_current_zulag_number;
    s8 field_2D_total_meter_bars;
    s16 field_2E_use_alt_save_header;
    s16 field_30_bDrawMeterCountDown;
    s16 field_32_visited_bonewerks;
    s16 field_34_visited_barracks;
    s16 field_36_visited_feeco_ender;
    s32 field_38_gas_timer;
    s16 field_3C_bBulletProof;
    s16 field_3E_padding;
};
ALIVE_ASSERT_SIZEOF(Quicksave_WorldInfo, 0x40);

struct Quicksave final
{
    Quicksave_PSX_Header field_0_header;
    s32 field_200_accumulated_obj_count;
    Quicksave_WorldInfo field_204_world_info;
    Quicksave_WorldInfo field_244_restart_path_world_info;
    u8 field_284_restart_path_abe_state[216];
    SwitchStates field_35C_restart_path_switch_states;
    SwitchStates field_45C_switch_states;
    u8 field_55C_objects_state_data[6820];
};
ALIVE_ASSERT_SIZEOF(Quicksave, 0x2000);

struct SaveFileRec final
{
    char_type field_0_fileName[32];
    u32 field_20_lastWriteTimeStamp;
};
ALIVE_ASSERT_SIZEOF(SaveFileRec, 0x24);

#ifdef TETHYS_SATURN
// SATURN (U1c T04, AO 450.ao.1 twin): the load lists show the four backup-RAM
// slots of one device (src/save_saturn.cxx), not a PC folder: 4 records, not
// 128 (-4,464 B of .bss). Every writer is bounded by ALIVE_COUNTOF.
ALIVE_ARY_EXTERN(SaveFileRec, 4, sSaveFileRecords_BB31D8);
#else
ALIVE_ARY_EXTERN(SaveFileRec, 128, sSaveFileRecords_BB31D8);
#endif
ALIVE_VAR_EXTERN(Quicksave, sActiveQuicksaveData_BAF7F8);
ALIVE_VAR_EXTERN(s32, sAccumulatedObjectCount_5C1BF4);
ALIVE_VAR_EXTERN(s32, sSavedGameToLoadIdx_BB43FC);
ALIVE_VAR_EXTERN(s32, sTotalSaveFilesCount_BB43E0);
ALIVE_VAR_EXTERN(u16, sQuickSave_saved_switchResetters_count_BB234C);

EXPORT void CC Quicksave_LoadActive_4C9170();
EXPORT void CC Quicksave_4C90D0();
EXPORT void CC Quicksave_ReadWorldInfo_4C9490(const Quicksave_WorldInfo* pInfo);
EXPORT void CC Quicksave_SaveWorldInfo_4C9310(Quicksave_WorldInfo* pInfo);
EXPORT void CC Quicksave_FindSaves_4D4150();
void QuikSave_RestoreBlyData_D481890_4C9BE0(const u8* pSaveData);
EXPORT void CC Quicksave_SaveSwitchResetterStates_4C9870();
EXPORT void CC Quicksave_RestoreSwitchResetterStates_4C9A30();

#ifdef TETHYS_SATURN
// SATURN (AE_ENGINE_DECISION.md 11.20, step 1): kObjectTypeAttributesTable's
// value for a TLV type (0 not saved, 1 saved with Created cleared, 2 saved as
// is), for the path table of Map.cpp. 0 for a type past the table.
u8 Tethys_QuicksaveTlvAttr(s32 type);

// SATURN (U1c T07/T08/T10, decision D-T2): the PC's save files are backup-RAM
// slots (src/save_saturn.cxx). Write: sActiveQuicksaveData from 0x200 (its
// zero tail trimmed) into the slot requested with Tethys_SaveSlotRequest,
// titled pTitle (the camera code). Read: slot `slot` into sActiveQuicksaveData,
// only once its checksum holds (the PSX header and the tail zeroed). Both
// return false on any failure, the read leaving the quicksave untouched.
bool Quicksave_WriteSlot_Saturn(const char_type* pTitle);
bool Quicksave_ReadSlot_Saturn(s32 slot);

// SATURN (decision D-T3): THE SHIPPED QUICKSAVE RECORDS, ATTRnnnn.SAV (attract
// demos) and NXTPnnnn.SAV (the path-skip cheat), are big-endian on the disc
// (tools/ae/ae_save_schema.py), but each TLV id inside them follows the
// TlvItemInfoUnion layout they were converted against. Stream R1 reordered the
// union for the Saturn (Factory.hpp, so .all keeps the PC's value) AFTER the
// packs were converted with the old "parts" rule: played now, those records
// would bind objects to the wrong TLVs. 0 = the demos and the path skip stay
// off (the menu never times out into a demo, the demo page and the cheat do
// nothing). Set 1 once ae_save_schema.py --generate gives the "numeric" rule
// and cd/data_ae is reconverted (requests.md, R1 -> lead and R3 -> lead).
// Done 2026-10-18: the schema gives "numeric" (--check and --selftest OK), the
// 11 packs were reconverted (9 changed: only their SAV records, the one
// converter input that moved) and check_pack_ae reads them back with 0 findings.
#define TETHYS_AE_SAV_RECORDS_CURRENT 1
#endif
