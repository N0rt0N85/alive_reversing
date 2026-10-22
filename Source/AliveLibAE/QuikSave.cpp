#include "stdafx.h"
#include "QuikSave.hpp"
#include "Function.hpp"
#include "ResourceManager.hpp"
#include "Path.hpp"
#include "PathData.hpp"
#include "Map.hpp"
#include "Abe.hpp"
#include "Glukkon.hpp"
#include "UXB.hpp"
#include "ThrowableArray.hpp"
#include "LCDStatusBoard.hpp"
#include "DDCheat.hpp"
#include "Events.hpp"
#include "WorkWheel.hpp"
#include "Drill.hpp"
#include "ScreenManager.hpp"
#include "Io.hpp"
#include "LiftPoint.hpp"
#include "LiftMover.hpp"
#include "TrapDoor.hpp"
#include "TimerTrigger.hpp"
#include "SlamDoor.hpp"
#include "FlyingSlig.hpp"
#include "SlapLock.hpp"
#include "SlapLockWhirlWind.hpp"
#include "AbilityRing.hpp"
#include "Shadow.hpp"
#include "Slurg.hpp"
#include "GasCountDown.hpp"
#include "Rock.hpp"
#include "Meat.hpp"
#include "Bone.hpp"
#include "MineCar.hpp"
#include "Slig.hpp"
#include "SligSpawner.hpp"
#include "ScrabSpawner.hpp"
#include "GameEnderController.hpp"
#include "Paramite.hpp"
#include "BirdPortal.hpp"
#include "ColourfulMeter.hpp"
#include "MinesAlarm.hpp"
#include "EvilFart.hpp"
#include "CrawlingSlig.hpp"
#include "Fleech.hpp"
#include "Greeter.hpp"
#include "Slog.hpp"
#include "Grenade.hpp"
#include "Mudokon.hpp"

struct QuickSaveRestoreTable final
{
    s32(CC* mFns[180])(const u8*);
};

QuickSaveRestoreTable sQuicksaveLoadFunctionTable = {
    nullptr,
    nullptr,
    &SligSpawner::CreateFromSaveState_409B10,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    &LiftMover::CreateFromSaveState_40D180,
    nullptr,
    &Bone::CreateFromSaveState_412C10,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    &MinesAlarm::CreateFromSaveState_417740,
    &CrawlingSlig::CreateFromSaveState_41AE80,
    nullptr,
    nullptr,
    nullptr,
    &Drill::CreateFromSaveState_421600,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    &EvilFart::CreateFromSaveState_4281C0,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    &Fleech::CreateFromSaveState_42DD50,
    nullptr,
    nullptr,
    nullptr,
    &FlyingSlig::CreateFromSaveState_437E40,
    &FlyingSligSpawner::CreateFromSaveState_43B690,
    nullptr,
    &GameEnderController::CreateFromSaveState_43BD10,
    nullptr,
    nullptr,
    &SlapLockWhirlWind::CreateFromSaveState_43DC20,
    &SlapLock::CreateFromSaveState_43EA00,
    nullptr,
    nullptr,
    &Greeter::CreateFromSaveState_446040,
    &Grenade::CreateFromSaveState_449410,
    nullptr,
    &Glukkon::CreateFromSaveState_442830,
    nullptr,
    &Abe::CreateFromSaveState_44D4F0,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    &LiftPoint::CreateFromSaveState_4630F0,
    nullptr,
    nullptr,
    &Mudokon::CreateFromSaveState_4717C0,
    nullptr,
    nullptr,
    &Meat::CreateFromSaveState_46A9E0,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    &MineCar::CreateFromSaveState_467740,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    &Paramite::CreateFromSaveState_4855A0,
    nullptr,
    nullptr,
    &BirdPortal::CreateFromSaveState_499C90,
    nullptr,
    nullptr,
    &ThrowableArray::CreateFromSaveState_49B200,
    nullptr,
    &AbilityRing::CreateFromSaveState_49DF90,
    &Rock::CreateFromSaveState_49F720,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    &Scrab::CreateFromSaveState_4A70A0,
    &ScrabSpawner::CreateFromSaveState_4ABEB0,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    &SlamDoor::CreateFromSaveState_4C08B0,
    nullptr,
    nullptr,
    &Slig::CreateFromSaveState_4B3B50,
    &Slog::CreateFromSaveState_4C54F0,
    nullptr,
    nullptr,
    &Slurg::CreateFromSaveState_4C8DF0,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    &TimerTrigger::CreateFromSaveState_4CDF70,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    &TrapDoor::CreateFromSaveState_4DDED0,
    &UXB::CreateFromSaveState_4DFAE0,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    &WorkWheel::CreateFromSaveState_4E3B10,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr};

ALIVE_VAR(1, 0x560c34, QuickSaveRestoreTable, sQuicksaveLoadFunctionTable_560C34, sQuicksaveLoadFunctionTable);

struct QuickSaveFlagTypeTable final
{
    u8 mTypes[136];
};

const QuickSaveFlagTypeTable kQuickSaveFlagsTable = {
    {0x02, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01,
     0x00, 0x01, 0x00, 0x00, 0x01, 0x02, 0x02, 0x00, 0x00, 0x01,
     0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x02, 0x00, 0x02, 0x00,
     0x02, 0x01, 0x00, 0x00, 0x01, 0x01, 0x00, 0x02, 0x01, 0x02,
     0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02,
     0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x02, 0x01, 0x00,
     0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x01,
     0x00, 0x01, 0x01, 0x02, 0x01, 0x00, 0x02, 0x01, 0x01, 0x02,
     0x01, 0x01, 0x02, 0x02, 0x02, 0x02, 0x01, 0x01, 0x01, 0x01,
     0x02, 0x01, 0x02, 0x02, 0x01, 0x01, 0x01, 0x01, 0x02, 0x01,
     0x01, 0x01, 0x02, 0x02, 0x01, 0x01, 0x02, 0x00, 0x00, 0x00,
     0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
     0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};

ALIVE_VAR(1, 0x547794, QuickSaveFlagTypeTable, kObjectTypeAttributesTable_byte_547794, kQuickSaveFlagsTable);

#if defined(TETHYS_AE_ZONE) && TETHYS_AE_ZONE
extern "C" const char_type* Tethys_ZoneAddrMissing(const void* p);            // src/zone_ae.cxx (11.20 step 2)
extern "C" [[noreturn]] void Tethys_ZoneFatal(const char_type* what, const char_type* name);
#endif

#ifdef TETHYS_SATURN
u8 Tethys_QuicksaveTlvAttr(s32 type)
{
    return (type >= 0 && type < static_cast<s32>(ALIVE_COUNTOF(kObjectTypeAttributesTable_byte_547794.mTypes)))
               ? kObjectTypeAttributesTable_byte_547794.mTypes[type]
               : 0;
}
#endif

ALIVE_VAR(1, 0x5c1bbc, u16, bUseAltSaveHeader_5C1BBC, 0);

ALIVE_VAR(1, 0xbb234c, u16, sQuickSave_saved_switchResetters_count_BB234C, 0);

void QuikSave_RestoreBlyData_D481890_4C9BE0(const u8* pSaveData)
{
    const u16* pSaveData2 = reinterpret_cast<const u16*>(pSaveData);

    while (*reinterpret_cast<const u32*>(pSaveData2) != 0)
    {
#if defined(TETHYS_AE_ZONE) && TETHYS_AE_ZONE
        // AE_ENGINE_DECISION.md 11.20 step 2: a CreateFromSaveState of a class
        // of the code zone only runs while that class is in the zone.
        {
            const char_type* pMissing = Tethys_ZoneAddrMissing(reinterpret_cast<const void*>(sQuicksaveLoadFunctionTable_560C34.mFns[*pSaveData2]));
            if (pMissing)
            {
                Tethys_ZoneFatal("ZN save", pMissing);
            }
        }
#endif
        pSaveData2 += sQuicksaveLoadFunctionTable_560C34.mFns[*pSaveData2](reinterpret_cast<const u8*>(pSaveData2)) / sizeof(u16);
    }

    // Skip the 2 zero entries, the saved flag words come after the object save state data
    const u8* pSrcFlags = reinterpret_cast<const u8*>(pSaveData2 + 2);
    for (s16 i = 1; i < Path_Get_Num_Paths(gMap_5C3030.field_0_current_level); i++)
    {
        const PathBlyRec* pPathRec = Path_Get_Bly_Record_460F30(gMap_5C3030.field_0_current_level, i);
        if (pPathRec->field_0_blyName)
        {
            const PathData* pPathData = pPathRec->field_4_pPathData;
            const s32 widthCount = (pPathData->field_4_bTop - pPathData->field_0_bLeft) / pPathData->field_A_grid_width;
            const s32 heightCount = (pPathData->field_6_bBottom - pPathData->field_2_bRight) / pPathData->field_C_grid_height;
            u8** ppPathRes = ResourceManager::GetLoadedResource_49C2A0(ResourceManager::Resource_Path, i, TRUE, FALSE);
            if (ppPathRes)
            {
                const s32 totalCameraCount = widthCount * heightCount;
                const s32* indexTable = reinterpret_cast<const s32*>(*ppPathRes + pPathData->field_16_object_indextable_offset);
                for (s32 j = 0; j < totalCameraCount; j++)
                {
                    const s32 tlvOffset = indexTable[j];
                    if (tlvOffset != -1)
                    {
                        u8* ptr = &(*ppPathRes)[pPathData->field_12_object_offset + tlvOffset];
                        Path_TLV* pTlv = reinterpret_cast<Path_TLV*>(ptr);
                        while (pTlv)
                        {
                            // TODO: Convert table to strongly typed flags
                            const u8 tableValue = kObjectTypeAttributesTable_byte_547794.mTypes[static_cast<s16>(pTlv->field_4_type.mType)];
                            if (tableValue == 1 || tableValue == 2) // Type 0 ignored - actually it should never be written here anyway
                            {
                                pTlv->field_0_flags.Raw().all = *pSrcFlags;
                                pSrcFlags++;

                                pTlv->field_1_tlv_state = *pSrcFlags;
                                pSrcFlags++;
                            }
                            pTlv = Path::Next_TLV_4DB6A0(pTlv);
                        }
                    }
                }
                ResourceManager::FreeResource_49C330(ppPathRes);
            }
#ifdef TETHYS_SATURN
            else
            {
                // SATURN (AE_ENGINE_DECISION.md 11.20 step 1): a path that is not
                // in the heap takes its state from the save into Map.cpp's table,
                // in the same walk order, so the stream stays aligned.
                s32 count = 0;
                TethysPathEntry* pEntries = Tethys_PathTableSlice(i, &count);
                for (s32 k = 0; k < count; k++)
                {
                    const u32 attr = pEntries[k].field_0_off_attr >> 14;
                    if (attr == 1 || attr == 2)
                    {
                        pEntries[k].field_2_flags = *pSrcFlags++;
                        pEntries[k].field_3_state = *pSrcFlags++;
                    }
                }
            }
#endif
        }
    }
    pResourceManager_5C1BB0->LoadingLoop_465590(FALSE);
}


ALIVE_VAR(1, 0xBAF7F8, Quicksave, sActiveQuicksaveData_BAF7F8, {});
ALIVE_VAR(1, 0x5C1BF4, s32, sAccumulatedObjectCount_5C1BF4, 0);
#ifdef TETHYS_SATURN
ALIVE_ARY(1, 0xBB31D8, SaveFileRec, 4, sSaveFileRecords_BB31D8, {}); // SATURN: four slots, QuikSave.hpp
#else
ALIVE_ARY(1, 0xBB31D8, SaveFileRec, 128, sSaveFileRecords_BB31D8, {});
#endif
ALIVE_VAR(1, 0xBB43FC, s32, sSavedGameToLoadIdx_BB43FC, 0);
ALIVE_VAR(1, 0xBB43E0, s32, sTotalSaveFilesCount_BB43E0, 0);

EXPORT void CC Quicksave_LoadFromMemory_4C95A0(Quicksave* quicksaveData)
{
    sAccumulatedObjectCount_5C1BF4 = quicksaveData->field_200_accumulated_obj_count;
    DestroyObjects_4A1F20();
    Events_Reset_422D70();
    bSkipGameObjectUpdates_5C2FA0 = 1;
    Quicksave_ReadWorldInfo_4C9490(&quicksaveData->field_204_world_info);
    sSwitchStates_5C1A28 = quicksaveData->field_45C_switch_states;
    gMap_5C3030.field_D8_restore_quick_save = reinterpret_cast<u8*>(quicksaveData->field_55C_objects_state_data);
    gMap_5C3030.SetActiveCam_480D30(
        quicksaveData->field_204_world_info.field_4_level,
        quicksaveData->field_204_world_info.field_6_path,
        quicksaveData->field_204_world_info.field_8_cam,
        CameraSwapEffects::eInstantChange_0,
        0,
        1);
    gMap_5C3030.field_8_force_load = 1;
}

EXPORT void CC Quicksave_LoadActive_4C9170()
{
    Game_ShowLoadingIcon_482D80();
    Quicksave_LoadFromMemory_4C95A0(&sActiveQuicksaveData_BAF7F8);
}

static void WriteChars(char_type*& pDst, u8 v1, u8 v2)
{
    *pDst = v1;
    pDst++;

    *pDst = v2;
    pDst++;
}

static void WriteFlags(u8*& pSaveBuffer, const Path_TLV* pTlv, const BitField8<TLV_Flags>& flags)
{
    *pSaveBuffer = flags.Raw().all;
    pSaveBuffer++;

    *pSaveBuffer = pTlv->field_1_tlv_state;
    pSaveBuffer++;
}

#ifdef TETHYS_SATURN
// SATURN (stream R1, U1a A22, AO bt1071 twin): what this function really wrote,
// published for the save side (stream R3) so the backup image can be cut to
// the bytes in use instead of all 8,192 B of the Quicksave (D-T2): the flag
// block's length, and the first byte past it (the used length of the Quicksave
// is Tethys_gSaveBlyEnd - (u8*) pSave). This site only measures.
extern "C" volatile u32 Tethys_gSaveBlobLen = 0;
extern "C" u8* volatile Tethys_gSaveBlyEnd = nullptr;
#endif

#ifdef TETHYS_SATURN
// SATURN (stream R3, U1c T04/SG4): the end of the Quicksave being written, set
// by Quicksave_SaveToMemory_4C91A0 around its call: the flag block is written
// only while it fits (the PC wrote unbounded). Null = unbounded.
static const u8* sTethysQsEnd = nullptr;
#endif

EXPORT void CCSTD Quicksave_SaveBlyData_4C9660(u8* pSaveBuffer)
{
#ifdef TETHYS_SATURN
    u8* const pTethysBlyStart = pSaveBuffer;
#endif
    for (s16 i = 1; i < Path_Get_Num_Paths(gMap_5C3030.field_0_current_level); i++)
    {
        const PathBlyRec* pPathRec = Path_Get_Bly_Record_460F30(gMap_5C3030.field_0_current_level, i);
        if (pPathRec->field_0_blyName)
        {
            const PathData* pPathData = pPathRec->field_4_pPathData;
            const s32 widthCount = (pPathData->field_4_bTop - pPathData->field_0_bLeft) / pPathData->field_A_grid_width;
            const s32 heightCount = (pPathData->field_6_bBottom - pPathData->field_2_bRight) / pPathData->field_C_grid_height;
            u8** ppPathRes = ResourceManager::GetLoadedResource_49C2A0(ResourceManager::Resource_Path, i, TRUE, FALSE);
            if (ppPathRes)
            {
                const s32 totalCameraCount = widthCount * heightCount;
                const s32* indexTable = reinterpret_cast<const s32*>(*ppPathRes + pPathData->field_16_object_indextable_offset);
                for (s32 j = 0; j < totalCameraCount; j++)
                {
                    const s32 tlvOffset = indexTable[j];
                    if (tlvOffset != -1)
                    {
                        u8* ptr = &(*ppPathRes)[pPathData->field_12_object_offset + tlvOffset];
                        Path_TLV* pTlv = reinterpret_cast<Path_TLV*>(ptr);
                        while (pTlv)
                        {
#ifdef TETHYS_SATURN
                            // SATURN (R3): never past the Quicksave (sTethysQsEnd above).
                            if (sTethysQsEnd && pSaveBuffer + 2 > sTethysQsEnd)
                            {
                                pTlv = Path::Next_TLV_4DB6A0(pTlv);
                                continue;
                            }
#endif
                            if (kObjectTypeAttributesTable_byte_547794.mTypes[static_cast<s16>(pTlv->field_4_type.mType)] == 1)
                            {
                                BitField8<TLV_Flags> flags = pTlv->field_0_flags;
                                if (flags.Get(TLV_Flags::eBit1_Created))
                                {
                                    flags.Clear(TLV_Flags::eBit1_Created);
                                    flags.Clear(TLV_Flags::eBit2_Destroyed);
                                }
                                WriteFlags(pSaveBuffer, pTlv, flags);
                            }
                            else if (kObjectTypeAttributesTable_byte_547794.mTypes[static_cast<s16>(pTlv->field_4_type.mType)] == 2)
                            {
                                WriteFlags(pSaveBuffer, pTlv, pTlv->field_0_flags);
                            }
                            else
                            {
                                // Type 0 ignored
                            }
                            pTlv = Path::Next_TLV_4DB6A0(pTlv);
                        }
                    }
                }
                ResourceManager::FreeResource_49C330(ppPathRes);
            }
#ifdef TETHYS_SATURN
            else
            {
                // SATURN (AE_ENGINE_DECISION.md 11.20 step 1): a path that is not in
                // the heap saves from Map.cpp's table, the same bytes in the same
                // order as its block would give.
                s32 count = 0;
                const TethysPathEntry* pEntries = Tethys_PathTableSlice(i, &count);
                for (s32 k = 0; k < count; k++)
                {
                    if (sTethysQsEnd && pSaveBuffer + 2 > sTethysQsEnd)
                    {
                        continue; // R3: never past the Quicksave
                    }
                    const u32 attr = pEntries[k].field_0_off_attr >> 14;
                    u8 flags = pEntries[k].field_2_flags;
                    if (attr == 1)
                    {
                        if (flags & TLV_Flags::eBit1_Created)
                        {
                            flags = static_cast<u8>(flags & ~(TLV_Flags::eBit1_Created | TLV_Flags::eBit2_Destroyed));
                        }
                    }
                    else if (attr != 2)
                    {
                        continue; // type 0 ignored
                    }
                    *pSaveBuffer++ = flags;
                    *pSaveBuffer++ = pEntries[k].field_3_state;
                }
            }
#endif
        }
    }

    // NOTE: Some values with things like total save size written here, but they are never used
#ifdef TETHYS_SATURN
    Tethys_gSaveBlobLen = static_cast<u32>(pSaveBuffer - pTethysBlyStart);
    Tethys_gSaveBlyEnd = pSaveBuffer;
#endif
}

struct SaveFlagsAndData final
{
    BitField8<TLV_Flags> flags;
    u8 data;
};
ALIVE_ARY(1, 0xBB233C, SaveFlagsAndData, 8, sSwitchReset_Saved_States_BB233C, {});

EXPORT void CC Quicksave_SaveSwitchResetterStates_4C9870()
{
    sQuickSave_saved_switchResetters_count_BB234C = 0;
    for (s16 i = 1; i < Path_Get_Num_Paths(gMap_5C3030.field_0_current_level); i++)
    {
        const PathBlyRec* pPathRec = Path_Get_Bly_Record_460F30(gMap_5C3030.field_0_current_level, i);
        if (pPathRec->field_0_blyName)
        {
            const PathData* pPathData = pPathRec->field_4_pPathData;
            const s32 widthCount = (pPathData->field_4_bTop - pPathData->field_0_bLeft) / pPathData->field_A_grid_width;
            const s32 heightCount = (pPathData->field_6_bBottom - pPathData->field_2_bRight) / pPathData->field_C_grid_height;
            u8** ppPathRes = ResourceManager::GetLoadedResource_49C2A0(ResourceManager::Resource_Path, i, TRUE, FALSE);
            if (ppPathRes)
            {
                const s32 totalCameraCount = widthCount * heightCount;
                const s32* indexTable = reinterpret_cast<const s32*>(*ppPathRes + pPathData->field_16_object_indextable_offset);
                for (s32 j = 0; j < totalCameraCount; j++)
                {
                    s32 tlvOffset = indexTable[j];
                    if (tlvOffset != -1)
                    {
                        u8* ptr = &(*ppPathRes)[pPathData->field_12_object_offset + tlvOffset];
                        Path_TLV* pTlv = reinterpret_cast<Path_TLV*>(ptr);
                        while (pTlv)
                        {
                            if (pTlv->field_4_type == TlvTypes::ResetSwitchRange_76)
                            {
                                if (sQuickSave_saved_switchResetters_count_BB234C < 8)
                                {
                                    sSwitchReset_Saved_States_BB233C[sQuickSave_saved_switchResetters_count_BB234C].flags = pTlv->field_0_flags;
                                    sSwitchReset_Saved_States_BB233C[sQuickSave_saved_switchResetters_count_BB234C].data = pTlv->field_1_tlv_state;

                                    sQuickSave_saved_switchResetters_count_BB234C++;
                                }
                                else
                                {
                                    LOG_WARNING("Out of write space !!");
                                }
                            }
                            pTlv = Path::Next_TLV_4DB6A0(pTlv);
                        }
                    }
                }
                ResourceManager::FreeResource_49C330(ppPathRes);
            }
        }
    }
}

EXPORT void CC Quicksave_RestoreSwitchResetterStates_4C9A30()
{
    s32 idx = 0;
    for (s16 i = 1; i < Path_Get_Num_Paths(gMap_5C3030.field_0_current_level); i++)
    {
        const PathBlyRec* pPathRec = Path_Get_Bly_Record_460F30(gMap_5C3030.field_0_current_level, i);
        if (pPathRec->field_0_blyName)
        {
            const PathData* pPathData = pPathRec->field_4_pPathData;
            const s32 widthCount = (pPathData->field_4_bTop - pPathData->field_0_bLeft) / pPathData->field_A_grid_width;
            const s32 heightCount = (pPathData->field_6_bBottom - pPathData->field_2_bRight) / pPathData->field_C_grid_height;
            u8** ppPathRes = ResourceManager::GetLoadedResource_49C2A0(ResourceManager::Resource_Path, i, TRUE, FALSE);
            if (ppPathRes)
            {
                const s32 totalCameraCount = widthCount * heightCount;
                const s32* indexTable = reinterpret_cast<const s32*>(*ppPathRes + pPathData->field_16_object_indextable_offset);
                for (s32 j = 0; j < totalCameraCount; j++)
                {
                    s32 tlvOffset = indexTable[j];
                    if (tlvOffset != -1)
                    {
                        u8* ptr = &(*ppPathRes)[pPathData->field_12_object_offset + tlvOffset];
                        Path_TLV* pTlv = reinterpret_cast<Path_TLV*>(ptr);
                        while (pTlv)
                        {
                            if (pTlv->field_4_type == TlvTypes::ResetSwitchRange_76)
                            {
                                if (idx < 8)
                                {
                                    pTlv->field_0_flags = sSwitchReset_Saved_States_BB233C[idx].flags;
                                    pTlv->field_1_tlv_state = sSwitchReset_Saved_States_BB233C[idx].data;

                                    idx++;
                                }
                                else
                                {
                                    LOG_WARNING("Out of read space !!");
                                }
                            }
                            pTlv = Path::Next_TLV_4DB6A0(pTlv);
                        }
                    }
                }
                ResourceManager::FreeResource_49C330(ppPathRes);
            }
        }
    }
    sQuickSave_saved_switchResetters_count_BB234C = 0;
}

void CC MEMCARD_Write_SJISC_String_4A2770(char_type* src, char_type* dst, s32 srcLength)
{
    while (srcLength > 0)
    {
        const char_type srcChar = *src;
        if (*src >= 'A' && srcChar <= 'Z')
        {
            WriteChars(dst, 0x82, *src + 0x1F);
        }
        else if (srcChar >= 'a' && srcChar <= 'z')
        {
            WriteChars(dst, 0x82, *src + 0x20);
        }
        else if (srcChar < '0' || srcChar > '9')
        {
            if (srcChar == ':')
            {
                WriteChars(dst, 0x81, 0x46);
            }
            else
            {
                if (srcChar == '"')
                {
                    WriteChars(dst, 0x81, 0x67);
                }
                else
                {
                    WriteChars(dst, 0x81, 0x40);
                }
            }
        }
        else
        {
            WriteChars(dst, 0x82, *src + 0x1F);
        }
        src++;
        srcLength--;
    }
}

EXPORT void CC MEMCARD_Generate_Timestamp_String_4A21F0(char_type* pStr)
{
    sprintf(pStr, "%03d:%02d",
            (s32)(sGnFrame_5C1B84 - sGameStartedFrame_5C1B88) / 30 / 60 / 60 % 1000,
            (s32)(sGnFrame_5C1B84 - sGameStartedFrame_5C1B88) / 30 / 60 % 60);
}

EXPORT void CC MEMCARD_Write_Timestamp_SJISC_String_4A2290(char_type* dst)
{
    char_type gameTimeStr[12] = {};
    MEMCARD_Generate_Timestamp_String_4A21F0(gameTimeStr);

    const size_t len = strlen(gameTimeStr);
    for (size_t idx = 0; idx < len; idx++)
    {
        // More SJISC conversion ?
        const s8 strChar = gameTimeStr[idx];
        if (strChar == ' ')
        {
            dst[2 * idx] = 0x81u;
            dst[2 * idx + 1] = 0x40;
        }
        else if (strChar == ':')
        {
            dst[2 * idx] = 0x81u;
            dst[2 * idx + 1] = 0x46;
        }
        else
        {
            dst[2 * idx] = 0x82u;
            dst[2 * idx + 1] = strChar + 0x1F;
        }
    }
}
#ifndef TETHYS_SATURN // SATURN (U1c T04): the PSX card headers are never written here (-1,024 B)
ALIVE_VAR(1, 0xBB19F8, Quicksave_PSX_Header, sSaveHeader2_BB19F8, {});
ALIVE_VAR(1, 0xBB17F8, Quicksave_PSX_Header, sSaveHeader1_BB17F8, {});
#endif

EXPORT void CC Quicksave_SaveToMemory_4C91A0(Quicksave* pSave)
{
    if (sActiveHero_5C1B68->field_10C_health > FP_FromInteger(0))
    {
        pSave->field_200_accumulated_obj_count = sAccumulatedObjectCount_5C1BF4;

#ifndef TETHYS_SATURN // SATURN (U1c T04/A13): the PSX memory-card header (SJIS title, play time) is never stored or read here
        // Don't really know what the point of doing this is? Might as well just memset the pSave header?
        Quicksave_PSX_Header* pHeaderToUse = nullptr;
        if (bUseAltSaveHeader_5C1BBC == 0)
        {
            pHeaderToUse = &sSaveHeader1_BB17F8;
        }
        else
        {
            pHeaderToUse = &sSaveHeader2_BB19F8;
        }
        pSave->field_0_header = *pHeaderToUse;

        MEMCARD_Write_Timestamp_SJISC_String_4A2290(&pSave->field_0_header.field_0_frame_1_name[50]);

        char_type src[12] = {};
        sprintf(src, "%2sP%02dC%02d",
                Path_Get_Lvl_Name(gMap_5C3030.field_0_current_level),
                gMap_5C3030.field_2_current_path,
                gMap_5C3030.field_4_current_camera);
        MEMCARD_Write_SJISC_String_4A2770(src, &pSave->field_0_header.field_0_frame_1_name[32], 8);
#endif // !TETHYS_SATURN: the PSX card header
        Quicksave_SaveWorldInfo_4C9310(&pSave->field_204_world_info);
        pSave->field_45C_switch_states = sSwitchStates_5C1A28;

        u8* pDataIter = pSave->field_55C_objects_state_data;
#ifdef TETHYS_SATURN
        // SATURN (U1c T04/SG4): nothing is written past the Quicksave. Each object
        // record is at most kTethysMaxObjState bytes (Abe_SaveState, the largest
        // of the 36 save types, measured by tools/ae/ae_save_schema.py; the
        // asserts keep the bound honest), and a u32 0 must follow the last one.
        // An object that would not fit is not saved: a crowded room loses a
        // record on reload instead of the Quicksave overrunning its 8 KB into
        // whatever .bss follows it. The 134 shipped saves end their objects at
        // 0x0EA8 and their flag blocks at 4,716 of 8,192 bytes.
        constexpr s32 kTethysMaxObjState = 216;
        static_assert(sizeof(Abe_SaveState) <= kTethysMaxObjState && sizeof(Fleech_State) <= kTethysMaxObjState
                          && sizeof(FlyingSlig_State) <= kTethysMaxObjState && sizeof(Slig_State) <= kTethysMaxObjState,
                      "a save state outgrew the quicksave writer's bound");
        u8* const pTethysEnd = reinterpret_cast<u8*>(pSave) + sizeof(Quicksave);
#endif
        for (s32 idx = 0; idx < gBaseGameObject_list_BB47C4->Size(); idx++)
        {
            BaseGameObject* pObj = gBaseGameObject_list_BB47C4->ItemAt(idx);
            if (!pObj)
            {
                break;
            }

            if (!(pObj->field_6_flags.Get(BaseGameObject::eDead_Bit3)))
            {
#ifdef TETHYS_SATURN
                if (pDataIter + kTethysMaxObjState + 4 > pTethysEnd)
                {
                    break; // see the bound above
                }
#endif
                pDataIter += pObj->VGetSaveState(pDataIter);
            }
        }

        // Write a u32 of 0
        pDataIter[0] = 0;
        pDataIter[1] = 0;
        pDataIter[2] = 0;
        pDataIter[3] = 0;

#ifdef TETHYS_SATURN
        sTethysQsEnd = pTethysEnd;
        Quicksave_SaveBlyData_4C9660(pDataIter + 4);
        sTethysQsEnd = nullptr;
        // SATURN (U1c T04/SG4): zero the stale tail behind the flag block (R1's
        // Tethys_gSaveBlyEnd marks where it ended), so what follows is zeros the
        // backup-RAM codec stores as one run per 128 bytes, and a slot never
        // carries bytes of an older game.
        for (u8* p = Tethys_gSaveBlyEnd; p && p < pTethysEnd; p++)
        {
            *p = 0;
        }
#else
        Quicksave_SaveBlyData_4C9660(pDataIter + 4);
#endif
    }
}

void CC Quicksave_4C90D0()
{
    Game_ShowLoadingIcon_482D80();
    pScreenManager_5BB5F4->InvalidateRect_40EC90(0, 0, 640, 240, 0);
    pScreenManager_5BB5F4->InvalidateRect_40EC90(0, 0, 640, 240, 1);
    pScreenManager_5BB5F4->InvalidateRect_40EC90(0, 0, 640, 240, 2);
    Path_Get_Bly_Record_460F30(gMap_5C3030.field_0_current_level, gMap_5C3030.field_2_current_path);
    Quicksave_SaveToMemory_4C91A0(&sActiveQuicksaveData_BAF7F8);
}

void CC Quicksave_ReadWorldInfo_4C9490(const Quicksave_WorldInfo* pInfo)
{
    bUseAltSaveHeader_5C1BBC = pInfo->field_2E_use_alt_save_header;

    // Read all fields bar the last
    for (s32 i = 0; i < ALIVE_COUNTOF(pInfo->field_18_saved_killed_muds_per_zulag); i++)
    {
        sSavedKilledMudsPerZulag_5C1B50.mData[i] = pInfo->field_18_saved_killed_muds_per_zulag[i];
    }

    // Last is read from another field
    sSavedKilledMudsPerZulag_5C1B50.mData[ALIVE_COUNTOF(sSavedKilledMudsPerZulag_5C1B50.mData) - 1] = pInfo->field_17_last_saved_killed_muds_per_path;

    sActiveHero_5C1B68->field_114_flags.Set(Flags_114::e114_Bit9_RestoredFromQuickSave);
    sZulagNumber_5C1A20 = pInfo->field_2C_current_zulag_number;
    sKilledMudokons_5C1BC0 = pInfo->field_14_killed_muds;
    sRescuedMudokons_5C1BC2 = pInfo->field_12_saved_muds;
    sMudokonsInArea_5C1BC4 = pInfo->field_16_muds_in_area; // TODO: Check types
    gTotalMeterBars_5C1BFA = pInfo->field_2D_total_meter_bars;
    gbDrawMeterCountDown_5C1BF8 = pInfo->field_30_bDrawMeterCountDown;
    sGasTimer_5C1BE8 = pInfo->field_38_gas_timer;
    gAbeBulletProof_5C1BDA = pInfo->field_3C_bBulletProof;
    sVisitedBonewerks_5C1C02 = pInfo->field_32_visited_bonewerks;
    sVisitedBarracks_5C1C04 = pInfo->field_34_visited_barracks;
    sVisitedFeecoEnder_5C1C06 = pInfo->field_36_visited_feeco_ender;
    sGnFrame_5C1B84 = pInfo->field_0_gnFrame;
}

void CC Quicksave_SaveWorldInfo_4C9310(Quicksave_WorldInfo* pInfo)
{
    PSX_RECT rect = {};
    sControlledCharacter_5C1B8C->vGetBoundingRect_424FD0(&rect, 1);

    pInfo->field_0_gnFrame = sGnFrame_5C1B84;
    pInfo->field_4_level = gMap_5C3030.field_0_current_level;
    pInfo->field_6_path = gMap_5C3030.field_2_current_path;
    pInfo->field_8_cam = gMap_5C3030.field_4_current_camera;
    pInfo->field_2E_use_alt_save_header = bUseAltSaveHeader_5C1BBC;

    for (s32 i = 0; i < ALIVE_COUNTOF(pInfo->field_18_saved_killed_muds_per_zulag); i++)
    {
        pInfo->field_18_saved_killed_muds_per_zulag[i] = sSavedKilledMudsPerZulag_5C1B50.mData[i];
    }

    pInfo->field_17_last_saved_killed_muds_per_path = sSavedKilledMudsPerZulag_5C1B50.mData[ALIVE_COUNTOF(sSavedKilledMudsPerZulag_5C1B50.mData) - 1];

    pInfo->field_2C_current_zulag_number = sZulagNumber_5C1A20;
    pInfo->field_12_saved_muds = sRescuedMudokons_5C1BC2;
    pInfo->field_14_killed_muds = sKilledMudokons_5C1BC0;
    pInfo->field_16_muds_in_area = static_cast<s8>(sMudokonsInArea_5C1BC4); // TODO: Check types
    pInfo->field_2D_total_meter_bars = gTotalMeterBars_5C1BFA;
    pInfo->field_30_bDrawMeterCountDown = gbDrawMeterCountDown_5C1BF8;
    pInfo->field_3C_bBulletProof = gAbeBulletProof_5C1BDA;
    pInfo->field_32_visited_bonewerks = sVisitedBonewerks_5C1C02;
    pInfo->field_34_visited_barracks = sVisitedBarracks_5C1C04;
    pInfo->field_36_visited_feeco_ender = sVisitedFeecoEnder_5C1C06;
    pInfo->field_38_gas_timer = sGasTimer_5C1BE8;
    pInfo->field_C_controlled_x = FP_GetExponent(sControlledCharacter_5C1B8C->field_B8_xpos);
    pInfo->field_E_controlled_y = rect.h;
    pInfo->field_10_controlled_scale = sControlledCharacter_5C1B8C->field_CC_sprite_scale == FP_FromDouble(1.0);
}

#ifdef TETHYS_SATURN
// SATURN (stream R3): see QuikSave.hpp. src/save_saturn.cxx codes, checksums
// and stores; this side only frames the bytes.
extern "C" s32 Tethys_AeSaveWrite(const u8* raw, s32 rawLen, const char* title, u32 playFrames);
extern "C" s32 Tethys_AeSaveRead(s32 slot, u8* out, s32 outMax);

bool Quicksave_WriteSlot_Saturn(const char_type* pTitle)
{
    const u8* pRaw = reinterpret_cast<const u8*>(&sActiveQuicksaveData_BAF7F8) + sizeof(Quicksave_PSX_Header);
    s32 n = static_cast<s32>(sizeof(Quicksave) - sizeof(Quicksave_PSX_Header));
    while (n > 0 && pRaw[n - 1] == 0)
    {
        n--; // Quicksave_SaveToMemory_4C91A0 zeroed everything past the flag block
    }
    if (n <= 0)
    {
        return false;
    }
    // The play time the PSX card header carried (MEMCARD_Generate_Timestamp_String_4A21F0).
    const u32 playFrames = sGnFrame_5C1B84 - static_cast<u32>(sGameStartedFrame_5C1B88);
    return Tethys_AeSaveWrite(pRaw, n, pTitle, playFrames) != 0;
}

bool Quicksave_ReadSlot_Saturn(s32 slot)
{
    u8* pRaw = reinterpret_cast<u8*>(&sActiveQuicksaveData_BAF7F8) + sizeof(Quicksave_PSX_Header);
    const s32 room = static_cast<s32>(sizeof(Quicksave) - sizeof(Quicksave_PSX_Header));
    const s32 n = Tethys_AeSaveRead(slot, pRaw, room);
    if (n <= 0)
    {
        return false;
    }
    memset(&sActiveQuicksaveData_BAF7F8.field_0_header, 0, sizeof(Quicksave_PSX_Header));
    memset(pRaw + n, 0, static_cast<size_t>(room - n));
    return true;
}
#endif

EXPORT s32 CC Sort_comparitor_4D42C0(const void* pSaveRecLeft, const void* pSaveRecRight)
{
    const s32 leftTime = reinterpret_cast<const SaveFileRec*>(pSaveRecLeft)->field_20_lastWriteTimeStamp;
    const s32 rightTime = reinterpret_cast<const SaveFileRec*>(pSaveRecRight)->field_20_lastWriteTimeStamp;

    if (leftTime <= rightTime)
    {
        return leftTime < rightTime;
    }
    else
    {
        return -1;
    }
}

void CC Quicksave_FindSaves_4D4150()
{
    sTotalSaveFilesCount_BB43E0 = 0;

    IO_EnumerateDirectory("*.sav", [](const char_type* fileName, u32 lastWriteTime)
                          {
#ifdef TETHYS_SATURN
                              // SATURN: the array's own length (4 slots here)
                              if (sTotalSaveFilesCount_BB43E0 < static_cast<s32>(ALIVE_COUNTOF(sSaveFileRecords_BB31D8)))
#else
                              if (sTotalSaveFilesCount_BB43E0 < 128)
#endif
                              {
                                  size_t fileNameLen = strlen(fileName) - 4;
                                  if (fileNameLen > 0)
                                  {
                                      // Limit length to prevent buffer overflow
                                      if (fileNameLen > 20)
                                      {
                                          fileNameLen = 20;
                                      }

                                      SaveFileRec* pRec = &sSaveFileRecords_BB31D8[sTotalSaveFilesCount_BB43E0];
                                      memcpy(pRec->field_0_fileName, fileName, fileNameLen);
                                      pRec->field_0_fileName[fileNameLen] = 0;

                                      pRec->field_20_lastWriteTimeStamp = lastWriteTime;
                                      sTotalSaveFilesCount_BB43E0++;
                                  }
                              }
                          });

    // Sort all we've found by time stamp, users probably want to load their last save first
    qsort(sSaveFileRecords_BB31D8, sTotalSaveFilesCount_BB43E0, sizeof(SaveFileRec), Sort_comparitor_4D42C0);

    // Underflow
    if (sSavedGameToLoadIdx_BB43FC < 0)
    {
        sSavedGameToLoadIdx_BB43FC = 0;
    }

    // Overflow
    if (sSavedGameToLoadIdx_BB43FC >= sTotalSaveFilesCount_BB43E0)
    {
        sSavedGameToLoadIdx_BB43FC = sTotalSaveFilesCount_BB43E0 - 1;
    }
}
