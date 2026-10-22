#include "stdafx.h"
#include "Map.hpp"
#include "PathData.hpp"
#include "Function.hpp"
#include "ScreenManager.hpp"
#include "ResourceManager.hpp"
#include "LvlArchive.hpp"
#include "Sound/Midi.hpp"
#include "SwitchStates.hpp"
#include "Game.hpp"
#include "Abe.hpp"
#include "MusicController.hpp"
#include "BackgroundMusic.hpp"
#include "stdlib.hpp"
#include "Path.hpp"
#include "QuikSave.hpp"
#include "Text.hpp"
#include "Sfx.hpp"
#include "FG1.hpp"
#include "CameraSwapper.hpp"
#include "MainMenu.hpp"
#include "Events.hpp"
#include "Movie.hpp"
#include "Particle.hpp"
#include "Door.hpp"
#include "Sound/PsxSpuApi.hpp"
#include "Sys.hpp"
#include <assert.h>

void Map_ForceLink()
{ }

ALIVE_VAR(1, 0x5c311c, s16, sMap_bDoPurpleLightEffect_5C311C, 0);
ALIVE_VAR(1, 0x5c3118, Camera*, sCameraBeingLoaded_5C3118, nullptr);
ALIVE_VAR(1, 0x5c3120, u32, sSoundChannelsMask_5C3120, 0);

#ifdef TETHYS_SATURN
// SATURN (stream R1): the platform hooks this file calls, AO Map.cpp's twins.
// Flip notification and marks, src/renderer_saturn.cxx: the sprite list is
// emptied and the flip timer stamped at T0 (420.ao.1: the wipe effect travels
// with the notification), row 20's head marks (446.ao.8).
extern "C" void Tethys_OnScreenChange(s32 inMenu, s32 effect);
extern "C" void Tethys_FlipMark(s32 k);
extern "C" bool Tethys_gCamPaintHidden; // 420.ao.1: the next stream paints behind a film
extern "C" volatile s32 Tethys_gBootPhase; // src/sys_saturn.cxx: which GoTo_Camera phase was live
extern "C" void Tethys_CamCacheReset();    // src/cam_cache.cxx, level-scoped (bt1061)
extern "C" void Tethys_CamPathPreload(const void* pCamNames, s32 count); // ResourceManager.cpp (426.ao.4)
bool Tethys_MenuPinsRelease();             // ResourceManager.cpp (442.ao.1)
extern "C" void Tethys_ActorPackForLevel(const char* lvl2); // src/renderer_saturn.cxx (R2, D-V1)
#if defined(TETHYS_AE_ZONE) && TETHYS_AE_ZONE
// AE_ENGINE_DECISION.md 11.20 step 2 (src/zone_ae.cxx): the code zone.
extern "C" void Tethys_ZoneForLevel(const char* lvl2);
extern "C" void Tethys_ZoneBounds(u32* pStart, u32* pEnd);
extern "C" volatile u32 Tethys_gZoneSurvivors;
// An object whose destructor lives in the code zone must not outlive the level
// that loaded it: the next level's load overwrites that code. None should (every
// zone class dies on a level change, the screen change deletes it); one that
// does is deleted here, while its code is still in, and counted.
static void Tethys_ZoneSurvivors()
{
    u32 z0 = 0;
    u32 z1 = 0;
    Tethys_ZoneBounds(&z0, &z1);
    if (z0 >= z1)
    {
        return;
    }
    for (s32 i = 0; i < gBaseGameObject_list_BB47C4->Size(); i++)
    {
        BaseGameObject* pObj = gBaseGameObject_list_BB47C4->ItemAt(i);
        if (!pObj)
        {
            break;
        }
        // The first slot of the vtable is VDestructor (BaseGameObject.hpp):
        // overridden by every class, so it is the class's own code.
        const u32 dtor = **reinterpret_cast<u32**>(pObj);
        if (dtor >= z0 && dtor < z1)
        {
            i = gBaseGameObject_list_BB47C4->RemoveAt(i);
            pObj->VDestructor(1);
            Tethys_gZoneSurvivors++;
        }
    }
}
#endif
extern "C" u8 Tethys_gChangeFilmPass;      // src/movie_ae.cxx: the eUnknown_11 film pass is running
// 425.ao.10/426.ao.2/bt1044-1047/bt1069: the flip partition, latched per screen
// change (renderer and CD seam counters, zeroed at T0; raw ticks ~208 a ms).
extern "C" u32 Tethys_RawTicks();
extern "C" u32 Tethys_gFlipPostRaw0;
extern "C" volatile u32 Tethys_gFlipPostMs;
extern "C" volatile u32 Tethys_gFlipQ[4];
extern "C" u32 Tethys_gCdRawAccum;
extern "C" volatile u32 Tethys_gFlipCdMs;
extern "C" u32 Tethys_gCdBytes;
extern "C" volatile u32 Tethys_gFlipKb;
extern "C" u32 Tethys_gCdReads;
extern "C" volatile u32 Tethys_gFlipReads;
// 446.ao.11: the camera GoTo_Camera already asked for (the block above its old
// cameras' teardown); Load_Path_Items must not ask a second time.
static Camera* sTethysAskedEarly = nullptr;
#endif


// Map Path_ChangeTLV::field_18_wipe to CameraSwapEffects
const CameraSwapEffects kPathChangeEffectToInternalScreenChangeEffect_55D55C[10] = {
    CameraSwapEffects::ePlay1FMV_5,
    CameraSwapEffects::eRightToLeft_2,
    CameraSwapEffects::eLeftToRight_1,
    CameraSwapEffects::eBottomToTop_4,
    CameraSwapEffects::eTopToBottom_3,
    CameraSwapEffects::eBoxOut_8,
    CameraSwapEffects::eVerticalSplit_6,
    CameraSwapEffects::eHorizontalSplit_7,
    CameraSwapEffects::eUnknown_11,
    CameraSwapEffects::eInstantChange_0};

EXPORT void CC static_map_construct_4802F0()
{
    gMap_5C3030.Reset_4805D0();
}

EXPORT void CC static_map_destruct_480330()
{
    gMap_5C3030.Shutdown_4804E0();
}

EXPORT void CC static_map_init_4802D0()
{
    static_map_construct_4802F0();
    atexit(static_map_destruct_480330);
}

void Map::ScreenChange_Common()
{
#ifdef TETHYS_SATURN
    Tethys_FlipMark(0); // 446.ao.8: row 20 `a` = T0 to here (the teardown); one caller, after it
#endif
    if (field_6_state == CamChangeStates::eSliceCam_1)
    {
        ResourceManager::Reclaim_Memory_49C470(0);
        Handle_PathTransition_481610();
    }
    else if (field_6_state == CamChangeStates::eInstantChange_2)
    {
        ResourceManager::Reclaim_Memory_49C470(0);
        GoTo_Camera_481890();
    }

    field_6_state = CamChangeStates::eInactive_0;

    SND_Stop_Channels_Mask_4CA810(sSoundChannelsMask_5C3120);
    sSoundChannelsMask_5C3120 = 0;
}

void Map::ScreenChange_480B80()
{
    if (field_6_state == CamChangeStates::eInactive_0)
    {
        return;
    }

#ifdef TETHYS_SATURN
    // SATURN (AO 420.ao.1/310.ao.1 twin, U1a A13, U10 R13): a screen change is
    // committed past this point. The renderer empties the sprite list (the dtor
    // storm below frees their textures while the synchronous stream holds the
    // display), stamps the flip timer and carries the effect; the level is
    // passed in, not sampled later, because field_0_current_level is the next
    // one by the time the flip ramp reads it.
    Tethys_OnScreenChange(field_0_current_level == LevelIds::eMenu_0 ? 1 : 0,
                          static_cast<s32>(field_10_screen_change_effect));
    // SATURN (AO 446.ao.8/446.ao.9 twin, U1a A18): start the new screen's .CAM
    // read NOW, under the teardown and the heap compaction below. Inside the
    // loaded level only (a level change swaps the .LVL), never for a film, which
    // owns the drive. A walk off the screen edge (eSliceCam_1) is predicted the
    // way Handle_PathTransition_481610 will pick it: the PathTransition TLV
    // under the hero, else the neighbouring cell in the direction of travel. A
    // wrong guess costs one stopped read (the reader restarts any read that is
    // not its record).
    if (field_0_current_level != LevelIds::eMenu_0)
    {
        char_type camName[20] = {};
        LevelIds lvl = field_A_level;
        CameraSwapEffects eff = field_10_screen_change_effect;
        if (field_6_state == CamChangeStates::eInstantChange_2)
        {
            Path_Format_CameraName_460FB0(camName, field_A_level, field_C_path, field_E_camera); // with ".CAM"
        }
        else if (field_6_state == CamChangeStates::eSliceCam_1)
        {
            Path_Change* pTlv = nullptr;
            if (field_18_pAliveObj)
            {
                const s16 x = FP_GetExponent(field_18_pAliveObj->field_B8_xpos);
                const s16 y = FP_GetExponent(field_18_pAliveObj->field_BC_ypos);
                pTlv = static_cast<Path_Change*>(sPath_dword_BB47C0->TLV_Get_At_4DB4B0(x, y, x, y, TlvTypes::PathTransition_1));
            }
            if (pTlv)
            {
                lvl = pTlv->field_10_level;
                eff = kPathChangeEffectToInternalScreenChangeEffect_55D55C[pTlv->field_18_wipe];
                Path_Format_CameraName_460FB0(camName, pTlv->field_10_level, pTlv->field_12_path, pTlv->field_14_camera);
            }
            else
            {
                eff = CameraSwapEffects::eLeftToRight_1; // a slide, as Handle_PathTransition sets
                s32 cx = field_D0_cam_x_idx;
                s32 cy = field_D2_cam_y_idx;
                switch (field_14_direction)
                {
                    case MapDirections::eMapLeft_0:
                        cx--;
                        break;
                    case MapDirections::eMapRight_1:
                        cx++;
                        break;
                    case MapDirections::eMapTop_2:
                        cy--;
                        break;
                    case MapDirections::eMapBottom_3:
                        cy++;
                        break;
                    default:
                        break;
                }
                if (cx >= 0 && cy >= 0 && cx < sPath_dword_BB47C0->field_6_cams_on_x && cy < sPath_dword_BB47C0->field_8_cams_on_y)
                {
                    const u8* pPathRes = *GetPathResourceBlockPtr(field_2_current_path);
                    memcpy(camName, pPathRes + sizeof(CameraName) * (cx + cy * sPath_dword_BB47C0->field_6_cams_on_x), sizeof(CameraName));
                    if (camName[0])
                    {
                        strcat(camName, ".CAM");
                    }
                }
            }
        }
        if (camName[0] && lvl == field_0_current_level
            && eff != CameraSwapEffects::ePlay1FMV_5 && eff != CameraSwapEffects::ePlay2FMVs_9
            && eff != CameraSwapEffects::ePlay3FMVs_10 && eff != CameraSwapEffects::eUnknown_11)
        {
            ResourceManager::Tethys_PrefetchCamFile(camName);
        }
    }
#endif

    if (sMap_bDoPurpleLightEffect_5C311C)
    {
        RemoveObjectsWithPurpleLight_480740(1);
    }

    PSX_DrawSync_4F6280(0);

    for (s32 i = 0; i < 2; i++) // Not sure why this is done twice?
    {
        for (s32 j = 0; j < gBaseGameObject_list_BB47C4->Size(); j++)
        {
            BaseGameObject* pItem = gBaseGameObject_list_BB47C4->ItemAt(j);
            if (!pItem)
            {
                break;
            }

            pItem->VScreenChanged();

            // Did the screen change kill the object?
            if (pItem->field_6_flags.Get(BaseGameObject::eDead_Bit3))
            {
                j = gBaseGameObject_list_BB47C4->RemoveAt(j);
                pItem->VDestructor(1);
            }
        }
    }

    ResourceManager::NoEffect_49C700();

    //dword_5CA4A8 = 0; // TODO: Never used?

    // TODO: Refactor this logic
    if (!sMap_bDoPurpleLightEffect_5C311C && field_A_level == field_0_current_level)
    {
        ScreenChange_Common();
        return;
    }

    if (field_A_level != field_0_current_level)
    {
        SsUtAllKeyOff_4FDFE0(0);
    }

    if (field_A_level != LevelIds::eNone)
    {
        if (field_A_level == LevelIds::eCredits_16)
        {
            sSoundChannelsMask_5C3120 = 0;
            ScreenChange_Common();
            return;
        }
    }
    else if (field_0_current_level == LevelIds::eMenu_0)
    {
        sSoundChannelsMask_5C3120 = 0;
        ScreenChange_Common();
        return;
    }
    sSoundChannelsMask_5C3120 = SND_4CA5D0(0, 0, 36, 70, 0, 0);
    ScreenChange_Common();
}

// OG only allows for 30 paths, given the editor allows for the full 99 we have to use a bigger array in a non ABI breaking way
// Note that currently there is only ever 1 Map object instance thus this global workaround is OK for now.
struct Map_PathsArrayExtended final
{
    u8** field_0_pPathRecs[99];
};
static Map_PathsArrayExtended sPathsArrayExtended = {};

#ifdef TETHYS_SATURN
void Tethys_PathPagingForget(bool bFree); // AE_ENGINE_DECISION.md 11.20 step 1, below ClearPathResourceBlocks
#endif

void Map::FreePathResourceBlocks()
{
#ifdef TETHYS_SATURN
    Tethys_PathPagingForget(true); // the path table goes with the level's paths
#endif
    for (s32 i = 0; i < Path_Get_Num_Paths(field_0_current_level); ++i)
    {
        if (sPathsArrayExtended.field_0_pPathRecs[i])
        {
            ResourceManager::FreeResource_49C330(sPathsArrayExtended.field_0_pPathRecs[i]);
            sPathsArrayExtended.field_0_pPathRecs[i] = nullptr;
        }

        if (i < ALIVE_COUNTOF(field_54_path_res_array.field_0_pPathRecs))
        {
            if (field_54_path_res_array.field_0_pPathRecs[i])
            {
                field_54_path_res_array.field_0_pPathRecs[i] = nullptr;
            }
        }
    }
}

void Map::GetPathResourceBlockPtrs()
{
    // Get pointer to each PATH
    for (s32 i = 1; i < Path_Get_Num_Paths(field_A_level); ++i)
    {
#ifdef TETHYS_SATURN
        // SATURN (AO 454.ao.1 twin, U1a A10): THE PATHS ARE LOCKED. Path::Loader_4DB800
        // keeps a raw Path_TLV* across every factory call, and a factory whose
        // allocation compacts the heap (Reclaim_Memory_49C470 moves every
        // unlocked block) would leave it on the old address. A locked block is
        // never moved and still goes with its last ref (FreeResource_Impl clears
        // the flag), so the level change frees it as before. `pp` counts the
        // compactions it refused.
        sPathsArrayExtended.field_0_pPathRecs[i] = ResourceManager::GetLoadedResource_49C2A0(ResourceManager::Resource_Path, i, TRUE, TRUE);
#else
        sPathsArrayExtended.field_0_pPathRecs[i] = ResourceManager::GetLoadedResource_49C2A0(ResourceManager::Resource_Path, i, TRUE, FALSE);
#endif

        if (i < ALIVE_COUNTOF(field_54_path_res_array.field_0_pPathRecs))
        {
            field_54_path_res_array.field_0_pPathRecs[i] = sPathsArrayExtended.field_0_pPathRecs[i];
        }
    }
}

u8** Map::GetPathResourceBlockPtr(u32 pathId)
{
    if (pathId < ALIVE_COUNTOF(field_54_path_res_array.field_0_pPathRecs))
    {
        return field_54_path_res_array.field_0_pPathRecs[pathId];
    }
    return sPathsArrayExtended.field_0_pPathRecs[pathId];
}

void Map::ClearPathResourceBlocks()
{
    for (s32 i = 0; i < ALIVE_COUNTOF(field_54_path_res_array.field_0_pPathRecs); i++)
    {
        field_54_path_res_array.field_0_pPathRecs[i] = nullptr;
    }

    for (s32 i = 0; i < ALIVE_COUNTOF(sPathsArrayExtended.field_0_pPathRecs); i++)
    {
        sPathsArrayExtended.field_0_pPathRecs[i] = nullptr;
    }
#ifdef TETHYS_SATURN
    Tethys_PathPagingForget(false); // a reset: the heap that held the table is gone with the rest
#endif
}

#ifdef TETHYS_SATURN
// ---- SATURN (AE_ENGINE_DECISION.md 11.20, step 1): THE CURRENT PATH ONLY.
// The PC keeps every path of the level in the heap, locked, for the whole level:
// MIPATH.BND is 79,432 B and BRPATH.BND 118,932 B, for ONE path in use at a time
// (12,352 B in MIP01, 2,012 B in BRP28). While Abe is elsewhere, a path is
// needed for one thing only: the state of its objects (a Slig dead, a Mudokon
// saved, a lever pulled), the flags and tlv_state bytes of its TLVs, exactly
// what Quicksave_SaveBlyData_4C9660 writes. So the level change still reads the
// whole BND once (it holds every TLV's starting state), copies that state into
// one table, and keeps only the destination path. A path change files the old
// path's state in the table, frees its block, and brings the new path back from
// its own record, <LV>PATHnn.BND (tools/ae/convert_ae.py path_piece_records):
// one CD command of 2 to 9 sectors. The three readers of a path that is not the
// current one go through the table: Path::TLV_Reset_4DB8E0 (an object that
// outlives its path), Quicksave_SaveBlyData_4C9660 and its restore.
//   Objects killed by the screen change are deleted in ScreenChange_480B80,
// before GoTo_Camera, so their last TLV_Reset lands in the old block before it
// is filed. A survivor (Abe, a follower) may still hold field_FC_pPathTLV into
// it: cleared when the block goes (null is a state the code already handles).
//   The path kept is moved to the heap top beside the other locked blocks: a
// locked island in the middle is what cut MIP01C07's free space in two.
//   Kill switch: TETHYS_AE_PATH_PAGING=0 keeps every path resident, as before.
#ifndef TETHYS_AE_PATH_PAGING
#define TETHYS_AE_PATH_PAGING 1
#endif
extern "C" [[noreturn]] void Tethys_Fatal(const char_type* msg);
extern "C" {
// Test gauges, read in RAM: path records brought back from the disc, TLV
// resets filed in the table, the table's bytes, levels left whole (why: 1 no
// room for the table, 2 an offset past 14 bits).
volatile u32 Tethys_gPathPageLoads = 0;
volatile u32 Tethys_gPathPageLate = 0;
volatile u32 Tethys_gPathPageTable = 0;
volatile u32 Tethys_gPathPageOff = 0;
}
static const s32 kTethysPathSlots = 32;                   // PathData.cpp kMaxPaths
static const u32 kTethysPathTableType = 0x74745350;       // 'PStt', for the heap census
static u8** sTethysPathTable = nullptr;
static LevelIds sTethysPathLevel = LevelIds::eNone;
static s16 sTethysPathFirst[kTethysPathSlots] = {};
static s16 sTethysPathCount[kTethysPathSlots] = {};

// ONE walk of a resident path block, in Quicksave_SaveBlyData_4C9660's order
// (cells of the index table, then each chain), for its four uses: count the TLVs
// (and check each offset fits 14 bits), fill a slice from the starting state,
// file the block's state into its slice, give the slice's state back to the
// block. One function, not a template per use: the HWRAM pool pays for code.
// Returns the TLVs walked; *pOk goes false on an offset that does not fit or
// does not meet the slice.
enum class TethysPathWalk : s32
{
    eCount,
    eFill,
    eToTable,
    eToBlock,
};
static s32 Tethys_PathWalk(LevelIds level, s32 pathId, u8* pPathRes, TethysPathWalk mode, TethysPathEntry* e, s32 count, bool* pOk)
{
    const PathBlyRec* pPathRec = Path_Get_Bly_Record_460F30(level, static_cast<s16>(pathId));
    if (!pPathRec->field_0_blyName)
    {
        return 0;
    }
    const PathData* pPathData = pPathRec->field_4_pPathData;
    const s32 widthCount = (pPathData->field_4_bTop - pPathData->field_0_bLeft) / pPathData->field_A_grid_width;
    const s32 heightCount = (pPathData->field_6_bBottom - pPathData->field_2_bRight) / pPathData->field_C_grid_height;
    const s32* indexTable = reinterpret_cast<const s32*>(pPathRes + pPathData->field_16_object_indextable_offset);
    u8* const pObjects = pPathRes + pPathData->field_12_object_offset;
    s32 n = 0;
    for (s32 j = 0; j < widthCount * heightCount; j++)
    {
        if (indexTable[j] == -1)
        {
            continue;
        }
        Path_TLV* pTlv = reinterpret_cast<Path_TLV*>(pObjects + indexTable[j]);
        while (pTlv)
        {
            const u32 off = static_cast<u32>(reinterpret_cast<u8*>(pTlv) - pObjects);
            if (mode == TethysPathWalk::eCount)
            {
                if (off > 0x3FFFu)
                {
                    *pOk = false;
                }
            }
            else if (mode == TethysPathWalk::eFill)
            {
                const u32 attr = Tethys_QuicksaveTlvAttr(static_cast<s32>(pTlv->field_4_type.mType)) & 3u;
                e[n].field_0_off_attr = static_cast<u16>(off | (attr << 14));
                e[n].field_2_flags = pTlv->field_0_flags.Raw().all;
                e[n].field_3_state = pTlv->field_1_tlv_state;
            }
            else if (n >= count || (e[n].field_0_off_attr & 0x3FFFu) != off)
            {
                *pOk = false;
            }
            else if (mode == TethysPathWalk::eToTable)
            {
                e[n].field_2_flags = pTlv->field_0_flags.Raw().all;
                e[n].field_3_state = pTlv->field_1_tlv_state;
            }
            else
            {
                pTlv->field_0_flags.Raw().all = e[n].field_2_flags;
                pTlv->field_1_tlv_state = e[n].field_3_state;
            }
            n++;
            pTlv = Path::Next_TLV_4DB6A0(pTlv);
        }
    }
    return n;
}

static TethysPathEntry* Tethys_PathEntries()
{
    return sTethysPathTable ? reinterpret_cast<TethysPathEntry*>(*sTethysPathTable) : nullptr;
}

[[noreturn]] static void Tethys_PathFatal(const char_type* pWhat, s32 pathId)
{
    static char_type msg[32];
    sprintf(msg, "%s %sPATH%02d", pWhat, Path_Get_Lvl_Name(sTethysPathLevel), static_cast<int>(pathId));
    Tethys_Fatal(msg);
}

TethysPathEntry* Tethys_PathTableSlice(s32 pathId, s32* pCount)
{
    TethysPathEntry* pAll = Tethys_PathEntries();
    if (!pAll || pathId <= 0 || pathId >= kTethysPathSlots || sTethysPathLevel != gMap_5C3030.field_0_current_level
        || sPathsArrayExtended.field_0_pPathRecs[pathId])
    {
        *pCount = 0;
        return nullptr;
    }
    *pCount = sTethysPathCount[pathId];
    return pAll + sTethysPathFirst[pathId];
}

void Tethys_PathTableReset(s32 pathId, u32 tlvOffset, s16 hiFlags, s8 bSetCreated, s8 bSetDestroyed)
{
    s32 n = 0;
    TethysPathEntry* e = Tethys_PathTableSlice(pathId, &n);
    // Every match, not the first: two cells may name one chain, and the walk
    // then lists its TLVs twice, as the quicksave does.
    for (s32 i = 0; i < n; i++)
    {
        if ((e[i].field_0_off_attr & 0x3FFFu) != tlvOffset)
        {
            continue;
        }
        u8 flags = e[i].field_2_flags;
        flags = (bSetDestroyed & 1) ? static_cast<u8>(flags | TLV_Flags::eBit2_Destroyed) : static_cast<u8>(flags & ~TLV_Flags::eBit2_Destroyed);
        flags = (bSetCreated & 1) ? static_cast<u8>(flags | TLV_Flags::eBit1_Created) : static_cast<u8>(flags & ~TLV_Flags::eBit1_Created);
        e[i].field_2_flags = flags;
        if (hiFlags != -1)
        {
            e[i].field_3_state = static_cast<u8>(hiFlags);
        }
        Tethys_gPathPageLate++;
    }
}

// Copy between a resident block and its slice, the offsets checked on the way.
static bool Tethys_PathSliceCopy(s32 pathId, u8* pPathRes, bool bToTable)
{
    bool ok = true;
    const s32 count = sTethysPathCount[pathId];
    const s32 n = Tethys_PathWalk(sTethysPathLevel, pathId, pPathRes, bToTable ? TethysPathWalk::eToTable : TethysPathWalk::eToBlock,
                                  Tethys_PathEntries() + sTethysPathFirst[pathId], count, &ok);
    return ok && n == count;
}

static void Tethys_PathSetSlot(s32 pathId, u8** ppRes)
{
    sPathsArrayExtended.field_0_pPathRecs[pathId] = ppRes;
    if (pathId < ALIVE_COUNTOF(gMap_5C3030.field_54_path_res_array.field_0_pPathRecs))
    {
        gMap_5C3030.field_54_path_res_array.field_0_pPathRecs[pathId] = ppRes;
    }
}

// File a resident path's state in the table and let its block go.
static void Tethys_PathRelease(s32 pathId)
{
    u8** ppRes = sPathsArrayExtended.field_0_pPathRecs[pathId];
    if (!ppRes)
    {
        return;
    }
    if (!Tethys_PathSliceCopy(pathId, *ppRes, true))
    {
        Tethys_PathFatal("PG file", pathId);
    }
    const u8* pBlock = *ppRes;
    const u32 bytes = ResourceManager::Get_Header_49C410(ppRes)->field_0_size;
    for (s32 i = 0; i < gBaseGameObject_list_BB47C4->Size(); i++)
    {
        BaseGameObject* pObj = gBaseGameObject_list_BB47C4->ItemAt(i);
        if (!pObj)
        {
            break;
        }
        if (pObj->field_6_flags.Get(BaseGameObject::eIsBaseAliveGameObject_Bit6))
        {
            auto pAlive = static_cast<BaseAliveGameObject*>(pObj);
            const u8* p = reinterpret_cast<const u8*>(pAlive->field_FC_pPathTLV);
            if (p >= pBlock && p < pBlock + bytes)
            {
                pAlive->field_FC_pPathTLV = nullptr;
            }
        }
    }
    ResourceManager::FreeResource_49C330(ppRes);
    Tethys_PathSetSlot(pathId, nullptr);
}

// Move a resident path block to the heap top (last fit), beside the other locked
// blocks. Kept in place when nothing higher fits.
static void Tethys_PathToTop(s32 pathId)
{
    u8** ppRes = sPathsArrayExtended.field_0_pPathRecs[pathId];
    if (!ppRes || ResourceManager::Get_Header_49C410(ppRes)->field_4_ref_count != 1)
    {
        return;
    }
    const u32 bytes = ResourceManager::Get_Header_49C410(ppRes)->field_0_size - sizeof(ResourceManager::Header);
    u8** ppTop = ResourceManager::Alloc_New_Resource_ImplEx(ResourceManager::Resource_Path, static_cast<u32>(pathId), bytes, true,
                                                             ResourceManager::eLastMatching, false, false);
    if (!ppTop)
    {
        return;
    }
    if (*ppTop < *ppRes)
    {
        ResourceManager::FreeResource_49C330(ppTop);
        return;
    }
    memcpy(*ppTop, *ppRes, bytes);
    ResourceManager::FreeResource_49C330(ppRes);
    Tethys_PathSetSlot(pathId, ppTop);
}

void Tethys_PathPagingForget(bool bFree)
{
    if (bFree && sTethysPathTable)
    {
        ResourceManager::FreeResource_49C330(sTethysPathTable);
    }
    sTethysPathTable = nullptr;
    sTethysPathLevel = LevelIds::eNone;
}

// The level change, right after GetPathResourceBlockPtrs: build the table from
// the whole BND, keep the destination path, let the others go.
void Map::Tethys_PathPagingLevelLoaded()
{
    Tethys_PathPagingForget(true);
#if TETHYS_AE_PATH_PAGING
    const s16 keep = field_C_path ? field_C_path : 1;
    const s32 nPaths = Path_Get_Num_Paths(field_A_level);
    if (nPaths > kTethysPathSlots)
    {
        return;
    }
    s32 total = 0;
    s32 resident = 0;
    bool fits = true;
    for (s32 i = 1; i < nPaths; i++)
    {
        sTethysPathFirst[i] = static_cast<s16>(total);
        sTethysPathCount[i] = 0;
        u8** ppRes = sPathsArrayExtended.field_0_pPathRecs[i];
        if (!ppRes)
        {
            continue;
        }
        resident++;
        const s32 c = Tethys_PathWalk(field_A_level, i, *ppRes, TethysPathWalk::eCount, nullptr, 0, &fits);
        sTethysPathCount[i] = static_cast<s16>(c);
        total += c;
    }
    if (!fits)
    {
        Tethys_gPathPageOff = 2;
        return;
    }
    if (resident < 2 || total == 0)
    {
        return; // one path: nothing to page
    }
    sTethysPathTable = ResourceManager::Alloc_New_Resource_ImplEx(kTethysPathTableType, 0, static_cast<u32>(total) * sizeof(TethysPathEntry), true,
                                                                  ResourceManager::eLastMatching, false, false);
    if (!sTethysPathTable)
    {
        Tethys_gPathPageOff = 1; // every path stays resident, the PC's way
        return;
    }
    sTethysPathLevel = field_A_level;
    Tethys_gPathPageTable = static_cast<u32>(total) * sizeof(TethysPathEntry);
    TethysPathEntry* pAll = Tethys_PathEntries();
    for (s32 i = 1; i < nPaths; i++)
    {
        u8** ppRes = sPathsArrayExtended.field_0_pPathRecs[i];
        if (!ppRes)
        {
            continue;
        }
        bool ok = true;
        Tethys_PathWalk(field_A_level, i, *ppRes, TethysPathWalk::eFill, pAll + sTethysPathFirst[i], sTethysPathCount[i], &ok);
    }
    for (s32 i = 1; i < nPaths; i++)
    {
        if (i != keep)
        {
            Tethys_PathRelease(i);
        }
    }
    Tethys_PathToTop(keep);
#endif
}

// A path change inside the level, before Path::Init_4DB200 reads the new block.
void Map::Tethys_PathPagingSwitch(s16 fromPath, s16 toPath)
{
#if TETHYS_AE_PATH_PAGING
    if (!sTethysPathTable || sTethysPathLevel != field_A_level || fromPath == toPath || toPath <= 0 || toPath >= kTethysPathSlots)
    {
        return;
    }
    if (fromPath > 0 && fromPath < kTethysPathSlots)
    {
        Tethys_PathRelease(fromPath);
    }
    if (sPathsArrayExtended.field_0_pPathRecs[toPath])
    {
        return;
    }
    char_type name[16];
    sprintf(name, "%sPATH%02d.BND", Path_Get_Lvl_Name(field_A_level), static_cast<int>(toPath));
    ResourceManager::LoadResourceFile_49C170(name, nullptr);
    u8** ppRes = ResourceManager::GetLoadedResource_49C2A0(ResourceManager::Resource_Path, toPath, TRUE, TRUE);
    if (!ppRes)
    {
        Tethys_PathFatal("PG load", toPath);
    }
    Tethys_PathSetSlot(toPath, ppRes);
    if (!Tethys_PathSliceCopy(toPath, *ppRes, false))
    {
        Tethys_PathFatal("PG slice", toPath);
    }
    Tethys_gPathPageLoads++;
    Tethys_PathToTop(toPath);
#else
    (void) fromPath;
    (void) toPath;
#endif
}
#endif

void Map::RemoveObjectsWithPurpleLight_480740(s16 bMakeInvisible)
{
    auto pObjectsWithLightsArray = ae_new<DynamicArrayT<BaseAnimatedWithPhysicsGameObject>>();
    pObjectsWithLightsArray->ctor_40CA60(16);

    auto pPurpleLightArray = ae_new<DynamicArrayT<Particle>>();
    pPurpleLightArray->ctor_40CA60(16);

    bool bAddedALight = false;
    for (s32 i = 0; i < gBaseGameObject_list_BB47C4->Size(); i++)
    {
        BaseGameObject* pObj = gBaseGameObject_list_BB47C4->ItemAt(i);
        if (!pObj)
        {
            break;
        }

        if (pObj->field_6_flags.Get(BaseGameObject::eIsBaseAnimatedWithPhysicsObj_Bit5))
        {
            if (pObj->field_6_flags.Get(BaseGameObject::eDrawable_Bit4))
            {
                auto pBaseObj = static_cast<BaseAnimatedWithPhysicsGameObject*>(pObj);

                PSX_RECT objRect = {};
                pBaseObj->vGetBoundingRect_424FD0(&objRect, 1);

                if (pBaseObj->field_DC_bApplyShadows & 2)
                {
                    if (pBaseObj->field_20_animation.field_4_flags.Get(AnimFlags::eBit3_Render))
                    {
                        if (!pBaseObj->field_6_flags.Get(BaseGameObject::eDead_Bit3) && pBaseObj != sControlledCharacter_5C1B8C && gMap_5C3030.Rect_Location_Relative_To_Active_Camera_480FE0(&objRect) == CameraPos::eCamCurrent_0)
                        {
                            pObjectsWithLightsArray->Push_Back(pBaseObj);
                            const FP k60Scaled = (pBaseObj->field_CC_sprite_scale * FP_FromInteger(60));
                            Particle* pPurpleLight = New_DestroyOrCreateObject_Particle_426F40(
                                FP_FromInteger((objRect.x + objRect.w) / 2),
                                FP_FromInteger(((objRect.y + objRect.h) / 2)) + k60Scaled,
                                pBaseObj->field_CC_sprite_scale);

                            if (pPurpleLight)
                            {
                                pPurpleLightArray->Push_Back(pPurpleLight);
                                bAddedALight = true;
                            }
                        }
                    }
                }
            }
        }
    }

    if (bAddedALight)
    {
        SFX_Play_46FBA0(SoundEffect::PossessEffect_17, 40, 2400);

        for (s32 counter = 0; counter < 12; counter++)
        {
            if (bMakeInvisible && counter == 4)
            {
                // Make all the objects that have lights invisible now that the lights have been rendered for a few frames
                for (s32 i = 0; i < pObjectsWithLightsArray->Size(); i++)
                {
                    BaseAnimatedWithPhysicsGameObject* pObj = pObjectsWithLightsArray->ItemAt(i);
                    if (!pObj)
                    {
                        break;
                    }
                    pObj->field_20_animation.field_4_flags.Clear(AnimFlags::eBit3_Render);
                }
            }

            for (s32 i = 0; i < pPurpleLightArray->Size(); i++)
            {
                Particle* pLight = pPurpleLightArray->ItemAt(i);
                if (!pLight)
                {
                    break;
                }

                if (!pLight->field_6_flags.Get(BaseGameObject::eDead_Bit3))
                {
                    pLight->VUpdate();
                }
            }

            // TODO/HACK what is the point of the f64 loop? Why not do both in 1 iteration ??
            for (s32 i = 0; i < pPurpleLightArray->Size(); i++)
            {
                Particle* pLight = pPurpleLightArray->ItemAt(i);
                if (!pLight)
                {
                    break;
                }

                if (!pLight->field_6_flags.Get(BaseGameObject::eDead_Bit3))
                {
                    pLight->field_20_animation.vDecode_40AC90();
                }
            }

            for (s32 i = 0; i < gObjList_drawables_5C1124->Size(); i++)
            {
                BaseGameObject* pDrawable = gObjList_drawables_5C1124->ItemAt(i);
                if (!pDrawable)
                {
                    break;
                }

                if (!pDrawable->field_6_flags.Get(BaseGameObject::eDead_Bit3))
                {
                    // TODO: Seems strange to check this flag, how did it get in the drawable list if its not a drawable ??
                    if (pDrawable->field_6_flags.Get(BaseGameObject::eDrawable_Bit4))
                    {
                        pDrawable->VRender(gPsxDisplay_5C1130.field_10_drawEnv[gPsxDisplay_5C1130.field_C_buffer_index].field_70_ot_buffer);
                    }
                }
            }

            PSX_DrawSync_4F6280(0);
            pScreenManager_5BB5F4->VRender(gPsxDisplay_5C1130.field_10_drawEnv[gPsxDisplay_5C1130.field_C_buffer_index].field_70_ot_buffer);
            SYS_EventsPump_494580();
            gPsxDisplay_5C1130.PSX_Display_Render_OT_41DDF0();
        }

        if (bMakeInvisible)
        {
            // Make all the objects that had lights visible again
            for (s32 i = 0; i < pObjectsWithLightsArray->Size(); i++)
            {
                BaseAnimatedWithPhysicsGameObject* pObj = pObjectsWithLightsArray->ItemAt(i);
                if (!pObj)
                {
                    break;
                }
                pObj->field_20_animation.field_4_flags.Set(AnimFlags::eBit3_Render);
            }
        }
    }

    pObjectsWithLightsArray->field_4_used_size = 0;
    pPurpleLightArray->field_4_used_size = 0;

    if (pObjectsWithLightsArray)
    {
        pObjectsWithLightsArray->dtor_40CAD0();
        ae_delete_free_495540(pObjectsWithLightsArray);
    }

    if (pPurpleLightArray)
    {
        pPurpleLightArray->dtor_40CAD0();
        ae_delete_free_495540(pPurpleLightArray);
    }
}

void Map::Handle_PathTransition_481610()
{
    Path_Change* pPathChangeTLV = nullptr;
    if (field_18_pAliveObj)
    {
        pPathChangeTLV = static_cast<Path_Change*>(sPath_dword_BB47C0->TLV_Get_At_4DB4B0(
            FP_GetExponent(field_18_pAliveObj->field_B8_xpos),
            FP_GetExponent(field_18_pAliveObj->field_BC_ypos),
            FP_GetExponent(field_18_pAliveObj->field_B8_xpos),
            FP_GetExponent(field_18_pAliveObj->field_BC_ypos),
            TlvTypes::PathTransition_1));
    }

    if (field_18_pAliveObj && pPathChangeTLV)
    {
        field_A_level = pPathChangeTLV->field_10_level;
        field_C_path = pPathChangeTLV->field_12_path;
        field_E_camera = pPathChangeTLV->field_14_camera;
        field_12_fmv_base_id = pPathChangeTLV->field_16_movie;

        field_10_screen_change_effect = kPathChangeEffectToInternalScreenChangeEffect_55D55C[pPathChangeTLV->field_18_wipe];

        field_18_pAliveObj->field_C2_lvl_number = field_A_level;
        field_18_pAliveObj->field_C0_path_number = field_C_path;
        GoTo_Camera_481890();

        switch (pPathChangeTLV->field_1A_scale)
        {
            case Scale_short::eFull_0:
                sActiveHero_5C1B68->field_CC_sprite_scale = FP_FromDouble(1.0);
                sActiveHero_5C1B68->field_20_animation.field_C_render_layer = Layer::eLayer_AbeMenu_32;
                break;

            case Scale_short::eHalf_1:
                sActiveHero_5C1B68->field_CC_sprite_scale = FP_FromDouble(0.5);
                sActiveHero_5C1B68->field_20_animation.field_C_render_layer = Layer::eLayer_AbeMenu_Half_13;
                break;

            default:
                LOG_ERROR("Invalid scale " << (s32) pPathChangeTLV->field_1A_scale);
                break;
        }

        CameraPos remapped = CameraPos::eCamInvalid_m1;
        switch (field_14_direction)
        {
            case MapDirections::eMapLeft_0:
                remapped = CameraPos::eCamLeft_3;
                break;
            case MapDirections::eMapRight_1:
                remapped = CameraPos::eCamRight_4;
                break;
            case MapDirections::eMapTop_2:
                remapped = CameraPos::eCamTop_1;
                break;
            case MapDirections::eMapBottom_3:
                remapped = CameraPos::eCamBottom_2;
                break;
        }

        field_18_pAliveObj->VOnPathTransition_408320(
            field_D0_cam_x_idx * field_D4_ptr->field_A_grid_width,
            field_D2_cam_y_idx * field_D4_ptr->field_C_grid_height,
            remapped);
    }
    else
    {
        switch (field_14_direction)
        {
            case MapDirections::eMapLeft_0:
                field_D0_cam_x_idx--;
                field_10_screen_change_effect = CameraSwapEffects::eRightToLeft_2;
                break;
            case MapDirections::eMapRight_1:
                field_D0_cam_x_idx++;
                field_10_screen_change_effect = CameraSwapEffects::eLeftToRight_1;
                break;
            case MapDirections::eMapTop_2:
                field_D2_cam_y_idx--;
                field_10_screen_change_effect = CameraSwapEffects::eBottomToTop_4;
                break;
            case MapDirections::eMapBottom_3:
                field_D2_cam_y_idx++;
                field_10_screen_change_effect = CameraSwapEffects::eTopToBottom_3;
                break;
            default:
                break;
        }

        const u32 pCamNameOffset = sizeof(CameraName) * (field_D0_cam_x_idx + (field_D2_cam_y_idx * sPath_dword_BB47C0->field_6_cams_on_x));
        const u8* pPathRes = *GetPathResourceBlockPtr(field_2_current_path);
        auto pCameraName = reinterpret_cast<const CameraName*>(pPathRes + pCamNameOffset);

        // Convert the 2 digit camera number string to an integer
        field_E_camera = 1 * (pCameraName->name[7] - '0') + 10 * (pCameraName->name[6] - '0');

        GoTo_Camera_481890();
    }
}

CameraPos Map::GetDirection_4811A0(s32 level, s32 path, FP xpos, FP ypos)
{
    if (level != static_cast<s32>(field_0_current_level))
    {
        return CameraPos::eCamInvalid_m1;
    }

    if (path != field_2_current_path)
    {
        return CameraPos::eCamInvalid_m1;
    }

    PSX_RECT rect = {};
    rect.x = FP_GetExponent(xpos);
    rect.w = FP_GetExponent(xpos);
    rect.y = FP_GetExponent(ypos);
    rect.h = FP_GetExponent(ypos);

    CameraPos ret = Rect_Location_Relative_To_Active_Camera_480FE0(&rect);

    PSX_RECT camWorldRect = {};
    if (!Get_Camera_World_Rect_481410(ret, &camWorldRect))
    {
        return CameraPos::eCamInvalid_m1;
    }

    const FP x = FP_FromInteger(camWorldRect.x);
    const FP y = FP_FromInteger(camWorldRect.y);
    const FP w = FP_FromInteger(camWorldRect.w);
    const FP h = FP_FromInteger(camWorldRect.h);

    switch (ret)
    {
        case CameraPos::eCamCurrent_0:
            return ret;

        case CameraPos::eCamTop_1:
            if (ypos < y || xpos < x || xpos > w)
            {
                return CameraPos::eCamInvalid_m1;
            }
            return ypos > h ? CameraPos::eCamCurrent_0 : ret;

        case CameraPos::eCamBottom_2:
            if (ypos > h || xpos < x || xpos > w)
            {
                return CameraPos::eCamInvalid_m1;
            }
            return ypos < y ? CameraPos::eCamCurrent_0 : ret;

        case CameraPos::eCamLeft_3:
            if (xpos < x || ypos < y || ypos > h)
            {
                return CameraPos::eCamInvalid_m1;
            }
            return xpos > w ? CameraPos::eCamCurrent_0 : ret;

        case CameraPos::eCamRight_4:
            if (xpos > w || ypos < y || ypos > h)
            {
                return CameraPos::eCamInvalid_m1;
            }
            return xpos < x ? CameraPos::eCamCurrent_0 : ret;

        default:
            return CameraPos::eCamInvalid_m1;
    }
}

void Map::Init_4803F0(LevelIds level, s16 path, s16 camera, CameraSwapEffects screenChangeEffect, s16 fmvBaseId, s16 forceChange)
{
    sPath_dword_BB47C0 = ae_new<Path>();
    sPath_dword_BB47C0->ctor_4DB170();

    field_2C_camera_array[0] = nullptr;
    field_2C_camera_array[1] = nullptr;
    field_2C_camera_array[2] = nullptr;
    field_2C_camera_array[3] = nullptr;
    field_2C_camera_array[4] = nullptr;

    field_22_overlayID = -1;

    field_4_current_camera = static_cast<s16>(-1);
    field_2_current_path = static_cast<s16>(-1);
    field_0_current_level = LevelIds::eNone;

    field_8_force_load = 0;

    SetActiveCam_480D30(level, path, camera, screenChangeEffect, fmvBaseId, forceChange);
    GoTo_Camera_481890();

    field_6_state = CamChangeStates::eInactive_0;
}

void Map::Shutdown_4804E0()
{
    sLvlArchive_5BC520.Free_433130();
    stru_5C3110.Free_433130();

    // Free Path resources
    FreePathResourceBlocks();

    // Free cameras
    for (s32 i = 0; i < ALIVE_COUNTOF(field_2C_camera_array); i++)
    {
        if (field_2C_camera_array[i])
        {
            field_2C_camera_array[i]->dtor_480E00();
            ae_delete_free_495540(field_2C_camera_array[i]);
            field_2C_camera_array[i] = nullptr;
        }
    }

    pScreenManager_5BB5F4 = nullptr;

    // Free path
    if (sPath_dword_BB47C0)
    {
        sPath_dword_BB47C0->dtor_4DB1A0();
        ae_delete_free_495540(sPath_dword_BB47C0);
    }
    sPath_dword_BB47C0 = nullptr;

    ResourceManager::Reclaim_Memory_49C470(0);
    Reset_4805D0();
}

void Map::Reset_4805D0()
{
    for (s32 i = 0; i < ALIVE_COUNTOF(field_2C_camera_array); i++)
    {
        field_2C_camera_array[i] = nullptr;
    }

    ClearPathResourceBlocks();

    field_CC_unused = 1;
    field_CE_free_all_anim_and_palts = 0;
    field_D8_restore_quick_save = 0;
}

void Map::GoTo_Camera_481890()
{
#ifdef TETHYS_SATURN
    Tethys_gBootPhase = 1;
    Tethys_FlipMark(1);          // 446.ao.8: row 20 `b` ends here (the heap compaction)
    sTethysAskedEarly = nullptr; // 446.ao.11: never carried across a change
#endif
    s16 bShowLoadingIcon = FALSE;
    if (field_0_current_level != LevelIds::eMenu_0 && field_0_current_level != LevelIds::eCredits_16 && field_0_current_level != LevelIds::eNone)
    {
        bShowLoadingIcon = TRUE;
    }

    if (field_10_screen_change_effect == CameraSwapEffects::eUnknown_11)
    {
#ifdef TETHYS_SATURN
        // SATURN (review RV1): the films of this pass play before the destination
        // is painted; src/movie_ae.cxx RestoreBackground leaves only those to it.
        Tethys_gChangeFilmPass = 1;
#endif
        BaseGameObject* pFmvRet = FMV_Camera_Change_482650(nullptr, this, field_0_current_level);
        for (s32 i = 0; i < gBaseGameObject_list_BB47C4->Size(); i++)
        {
            SYS_EventsPump_494580();

            BaseGameObject* pBaseGameObj = gBaseGameObject_list_BB47C4->ItemAt(i);
            if (!pBaseGameObj)
            {
                break;
            }

            if (pBaseGameObj->field_6_flags.Get(BaseGameObject::eDead_Bit3) && pBaseGameObj->field_6_flags.Get(BaseGameObject::eCantKill_Bit11) == false)
            {
                i = gBaseGameObject_list_BB47C4->RemoveAt(i);
                pBaseGameObj->VDestructor(1);
                if (pBaseGameObj == pFmvRet)
                {
                    // FMV trans done
                    break;
                }
            }
            else if (pBaseGameObj->field_6_flags.Get(BaseGameObject::eUpdatable_Bit2))
            {
                if (!(pBaseGameObj->field_6_flags.Get(BaseGameObject::eDead_Bit3)) && (!sNum_CamSwappers_5C1B66 || pBaseGameObj->field_6_flags.Get(BaseGameObject::eUpdateDuringCamSwap_Bit10)))
                {
                    const s32 updateDelay = pBaseGameObj->UpdateDelay();
                    if (updateDelay > 0)
                    {
                        pBaseGameObj->SetUpdateDelay(updateDelay - 1);
                    }
                    else
                    {
                        pBaseGameObj->VUpdate();
                    }
                }
            }
        }
#ifdef TETHYS_SATURN
        Tethys_gChangeFilmPass = 0;
#endif

        if (sSoundChannelsMask_5C3120)
        {
            SND_Stop_Channels_Mask_4CA810(sSoundChannelsMask_5C3120);
        }
        sSoundChannelsMask_5C3120 = SND_4CA5D0(0, 0, 36, 70, 0, 0);
    }

#ifdef TETHYS_SATURN
    // SATURN (decision D-R5, AO 420.ao.1 + 448.ao.7 policy, U1a A16): test the
    // DESTINATION level. field_0_current_level is still the level being left
    // here, so the PC's test never armed the icon on "Begin" (the menu is
    // current), the one load the player watches. Never at the boot's arrival at
    // the menu (from eNone, the only origin that arrives there from nowhere),
    // never when an attract demo leaves the menu (the icon is an object, and one
    // more object moves the order a recording's actors update in), never for
    // the credits.
    if (field_A_level != LevelIds::eCredits_16
        && !(field_A_level == LevelIds::eMenu_0 && field_0_current_level == LevelIds::eNone)
        && !(gAttract_5C1BA0 && field_0_current_level == LevelIds::eMenu_0))
#else
    if (field_0_current_level != LevelIds::eMenu_0 && field_0_current_level != LevelIds::eNone)
#endif
    {
        if (field_A_level != field_0_current_level
            || field_8_force_load
            || (field_C_path != field_2_current_path && field_10_screen_change_effect == CameraSwapEffects::ePlay1FMV_5))
        {
            Game_ShowLoadingIcon_482D80();
        }
    }

    if (field_A_level != field_0_current_level
        || field_C_path != field_2_current_path
        || field_8_force_load)
    {
        field_22_overlayID = GetOverlayId_480710();
    }

    if (field_A_level != field_0_current_level || field_8_force_load)
    {
        pResourceManager_5C1BB0->LoadingLoop_465590(bShowLoadingIcon);

#ifdef TETHYS_SATURN
        if (field_A_level != field_0_current_level)
        {
            // SATURN (AO bt1061 + 442.ao.1 twins, U1a A13): the .CAM cache is
            // level-scoped (record names are unique only within an archive, and
            // the archive's base sector moves: a stale hit would stream the wrong
            // screen), and the menu pins mean nothing once the menu is left.
            Tethys_CamCacheReset();
            Tethys_MenuPinsRelease();
        }
#endif
        // Free all cameras
        for (s32 i = 0; i < ALIVE_COUNTOF(field_2C_camera_array); i++)
        {
            if (field_2C_camera_array[i])
            {
                field_2C_camera_array[i]->dtor_480E00();
                ae_delete_free_495540(field_2C_camera_array[i]);
                field_2C_camera_array[i] = nullptr;
            }
        }

        if (field_0_current_level != LevelIds::eNone)
        {
            // Close LVL archives
            sLvlArchive_5BC520.Free_433130();
            stru_5C3110.Free_433130();

            // Free all but the first ?
            FreePathResourceBlocks();

            sPath_dword_BB47C0->Free_4DB1C0();

            if (field_A_level != field_0_current_level)
            {
                SND_Reset_4C9FB0();
            }

            ResourceManager::Reclaim_Memory_49C470(0);
        }

        pResourceManager_5C1BB0->LoadingLoop_465590(bShowLoadingIcon);

#ifdef TETHYS_SATURN
        if (field_A_level != field_0_current_level)
        {
            // SATURN (R2's request, U10 R08 runtime half, D-V1): the DESTINATION
            // level's actor palette pack (ACTORPAL.<two letters>;1). Here no CD
            // file is mid-read (LoadingLoop just drained the queue), no camera
            // streams (every camera was freed above; ScreenChange never
            // prefetches across levels) and the new archive is not open yet (the
            // pack read may close the current GFS handle). The boot level comes
            // through here too: the first GoTo_Camera arrives from eNone.
            Tethys_ActorPackForLevel(Path_Get_Lvl_Name(field_A_level));
#if defined(TETHYS_AE_ZONE) && TETHYS_AE_ZONE
            // AE_ENGINE_DECISION.md 11.20 step 2: the destination level's
            // classes into the code zone, at the same quiet point. Every object
            // of the old level died in ScreenChange_480B80 (each zone class dies
            // on a level change); one that did not would run code the load is
            // about to overwrite, so it goes first, here, while its code is in.
            Tethys_ZoneSurvivors();
            Tethys_ZoneForLevel(Path_Get_Lvl_Name(field_A_level));
#endif
        }
#endif

        // Open LVL
        while (!sLvlArchive_5BC520.Open_Archive_432E80(CdLvlName(field_A_level)))
        {
            if (gAttract_5C1BA0)
            {
                // NOTE: Dead branch? Given no attract directory exists
                char_type fileName[256] = {};
                strcpy(fileName, "ATTRACT");
                strcat(fileName, CdLvlName(field_A_level));
                if (sLvlArchive_5BC520.Open_Archive_432E80(fileName))
                {
                    break;
                }
            }
            Display_Full_Screen_Message_Blocking_465820(Path_Get_Unknown(field_A_level), MessageType::eLongTitle_0);
        }

        // Open Path BND
        ResourceManager::LoadResourceFile_49C170(Path_Get_BndName(field_A_level), nullptr);

        // Get pointer to each PATH
        GetPathResourceBlockPtrs();

#ifdef TETHYS_SATURN
        // AE_ENGINE_DECISION.md 11.20 step 1: the table of every path's TLV
        // state, then only the destination path stays.
        Tethys_PathPagingLevelLoaded();
        Tethys_gBootPhase = 2;
#endif
        if (field_A_level == field_0_current_level)
        {
            MusicController::PlayMusic_47FD60(MusicController::MusicTypes::eNone_0, sActiveHero_5C1B68, 0, 0);
        }
        else
        {
            SND_Load_VABS_4CA350(Path_Get_MusicInfo(field_A_level), Path_Get_Reverb(field_A_level));
            SND_Load_Seqs_4CAED0(sSeqData_558D50.mSeqs, Path_Get_BsqFileName(field_A_level));

            auto pBackgroundMusic = ae_new<BackgroundMusic>();
            if (pBackgroundMusic)
            {
                pBackgroundMusic->ctor_4CB110(Path_Get_BackGroundMusicId(field_A_level));
            }
        }

        if (!field_8_force_load)
        {
            SwitchStates_SetRange_465FA0(2, 255);
        }

        if (field_CE_free_all_anim_and_palts)
        {
            ResourceManager::Free_Resource_Of_Type_49C6B0(ResourceManager::Resource_Animation);
            ResourceManager::Free_Resource_Of_Type_49C6B0(ResourceManager::Resource_Palt);
            field_CE_free_all_anim_and_palts = FALSE;
        }
    }

    if (!field_C_path)
    {
        field_C_path = 1;
    }

    const s16 prevPathId = field_2_current_path;
    const LevelIds prevLevelId = field_0_current_level;

    field_2_current_path = field_C_path;
    field_0_current_level = field_A_level;
    field_4_current_camera = field_E_camera;

    const PathBlyRec* pPathRec_1 = Path_Get_Bly_Record_460F30(field_A_level, field_C_path);
    field_D4_ptr = pPathRec_1->field_4_pPathData;

#ifdef TETHYS_SATURN
    // AE_ENGINE_DECISION.md 11.20 step 1: a path change inside the level files
    // the old path's TLV state and brings the new path back from the disc.
    if (prevLevelId == field_A_level)
    {
        Tethys_PathPagingSwitch(prevPathId, field_C_path);
    }
#endif
    sPath_dword_BB47C0->Init_4DB200(
        field_D4_ptr,
        field_A_level,
        field_C_path,
        field_E_camera,
        GetPathResourceBlockPtr(field_C_path));

    if (sQuickSave_saved_switchResetters_count_BB234C > 0)
    {
        Quicksave_RestoreSwitchResetterStates_4C9A30();
    }

    char_type pStrBuffer[13] = {};
    Path_Format_CameraName_460FB0(pStrBuffer, field_A_level, field_C_path, field_E_camera);

    u32 pCamNameOffset = 0;
    if (sizeof(CameraName) * sPath_dword_BB47C0->field_6_cams_on_x * sPath_dword_BB47C0->field_8_cams_on_y > 0)
    {
        for (;;)
        {
            u8* pPathRes = *GetPathResourceBlockPtr(field_C_path);
            CameraName* pCameraNameIter = reinterpret_cast<CameraName*>(pPathRes + pCamNameOffset);

            if (strncmp(pCameraNameIter->name, pStrBuffer, sizeof(CameraName)) == 0)
            {
                // Matched
                break;
            }

            pCamNameOffset += sizeof(CameraName);
            if (pCamNameOffset >= sizeof(CameraName) * sPath_dword_BB47C0->field_6_cams_on_x * sPath_dword_BB47C0->field_8_cams_on_y)
            {
                // Out of bounds
                break;
            }
        }
    }

    field_D0_cam_x_idx = static_cast<s16>((pCamNameOffset / sizeof(CameraName)) % sPath_dword_BB47C0->field_6_cams_on_x);
    field_D2_cam_y_idx = static_cast<s16>((pCamNameOffset / sizeof(CameraName)) / sPath_dword_BB47C0->field_6_cams_on_x);
    field_24_camera_offset.field_0_x = FP_FromInteger(field_D0_cam_x_idx * field_D4_ptr->field_A_grid_width);
    field_24_camera_offset.field_4_y = FP_FromInteger(field_D2_cam_y_idx * field_D4_ptr->field_C_grid_height);

    // If map has changed then load new collision info
    if (prevPathId != field_2_current_path || prevLevelId != field_0_current_level)
    {
        if (sCollisions_DArray_5C1128)
        {
            sCollisions_DArray_5C1128->dtor_4189F0();
            ae_delete_free_495540(sCollisions_DArray_5C1128);
        }

        sCollisions_DArray_5C1128 = ae_new<Collisions>();
        if (sCollisions_DArray_5C1128)
        {
            sCollisions_DArray_5C1128->ctor_418930(pPathRec_1->field_8_pCollisionData, *GetPathResourceBlockPtr(field_2_current_path));
        }
    }

    if (field_D8_restore_quick_save)
    {
        QuikSave_RestoreBlyData_D481890_4C9BE0(field_D8_restore_quick_save);
        field_D8_restore_quick_save = nullptr;
    }

#ifdef TETHYS_SATURN
    // SATURN (AO 426.ao.4 twin): with a cartridge, the whole path's backgrounds
    // in ONE CD command at a path or level change, before camera[0] is built so
    // the screen we arrive on is served from the span too. Best-effort in every
    // direction (no cartridge, an oversized path, a failed read: the per-screen
    // reader runs as before).
    if (prevPathId != field_2_current_path || prevLevelId != field_0_current_level)
    {
        Tethys_CamPathPreload(*GetPathResourceBlockPtr(field_2_current_path),
                              sPath_dword_BB47C0->field_6_cams_on_x * sPath_dword_BB47C0->field_8_cams_on_y);
    }
    Tethys_gBootPhase = 4;
#endif

    // Copy camera array and blank out the source
    for (s32 i = 0; i < ALIVE_COUNTOF(field_40_stru_5); i++)
    {
        field_40_stru_5[i] = field_2C_camera_array[i];
        field_2C_camera_array[i] = nullptr;
    }

    field_2C_camera_array[0] = Create_Camera_4829E0(field_D0_cam_x_idx, field_D2_cam_y_idx, 1);
#ifdef TETHYS_SATURN
    // SATURN (AO S4 twin, decision D-R3, U1a A9): THE NEIGHBOUR CAMERAS ARE NOT
    // BUILT. The PC keeps the four screens around this one resident (their
    // FG1, Anim and Font chunks and every resource their TLVs ask for); the
    // archived beta measured 284,248 of 373,308 B of MIP01C04's resident set
    // never shown. Null neighbours are an already-supported state: Create_Camera
    // returns null for any cell without a camera name and every consumer
    // tolerates it. What they answered is answered from the path's CameraName
    // grid instead (Tethys_GridHasCamera: the edge flips and the neighbour
    // world rectangles); a flip loads the new cell inside GoTo_Camera.
    field_2C_camera_array[3] = nullptr;
    field_2C_camera_array[4] = nullptr;
    field_2C_camera_array[1] = nullptr;
    field_2C_camera_array[2] = nullptr;
#else
    field_2C_camera_array[3] = Create_Camera_4829E0(field_D0_cam_x_idx - 1, field_D2_cam_y_idx, 0);
    field_2C_camera_array[4] = Create_Camera_4829E0(field_D0_cam_x_idx + 1, field_D2_cam_y_idx, 0);
    field_2C_camera_array[1] = Create_Camera_4829E0(field_D0_cam_x_idx, field_D2_cam_y_idx - 1, 0);
    field_2C_camera_array[2] = Create_Camera_4829E0(field_D0_cam_x_idx, field_D2_cam_y_idx + 1, 0);
#endif

    // Free resources for each camera
    for (s32 i = 0; i < ALIVE_COUNTOF(field_40_stru_5); i++)
    {
        if (field_40_stru_5[i])
        {
            pResourceManager_5C1BB0->Free_Resources_For_Camera_4656F0(field_40_stru_5[i]);
        }
    }

    pResourceManager_5C1BB0->LoadingLoop_465590(bShowLoadingIcon);

#ifdef TETHYS_SATURN
    // SATURN (AO 446.ao.11 twin, U1a A18): ASK FOR WHAT THE NEW SCREEN NEEDS
    // BEFORE THE OLD ONE LETS GO. With the neighbours not built, the camera Abe
    // walks off is destroyed just below, releasing everything it held BEFORE
    // Load_Path_Items asks for the new screen's resources, which then came back
    // from the CD. Asking first reads nothing (the request pass walks the path's
    // TLVs, in memory, and only QUEUES what is missing): a resource both screens
    // use gains the new camera's reference before the old camera drops its own.
    // The queued files still load after the stream, i.e. after the frees, so the
    // heap peak does not grow. A reused camera (flag 1 set) asks nothing.
    {
        Camera* pNew = field_2C_camera_array[0];
        if (pNew && !(pNew->field_30_flags & 1))
        {
            sCameraBeingLoaded_5C3118 = pNew;
            sPath_dword_BB47C0->Loader_4DB800(pNew->field_14_xpos, pNew->field_16_ypos, LoadMode::LoadResourceFromList_1, TlvTypes::None_m1);
            sCameraBeingLoaded_5C3118 = nullptr;
            sTethysAskedEarly = pNew;
        }
    }
#endif
    // Free each camera itself
    for (s32 i = 0; i < ALIVE_COUNTOF(field_40_stru_5); i++)
    {
        if (field_40_stru_5[i])
        {
            field_40_stru_5[i]->dtor_480E00();
            ae_delete_free_495540(field_40_stru_5[i]);
            field_40_stru_5[i] = nullptr;
        }
    }

#ifdef TETHYS_SATURN
    Tethys_gBootPhase = 5;
#endif
    Map::Load_Path_Items_482C10(field_2C_camera_array[0], LoadMode::ConstructObject_0);
#ifdef TETHYS_SATURN
    // 426.ao.2: q0 from lo's own T0 (stamped in FlipEnd, inside the stream above).
    Tethys_gFlipQ[0] = (Tethys_RawTicks() - Tethys_gFlipPostRaw0) / 208u;
    u32 qT = Tethys_RawTicks();
#endif
    pResourceManager_5C1BB0->LoadingLoop_465590(bShowLoadingIcon);
#ifdef TETHYS_SATURN
    Tethys_gFlipQ[1] = (Tethys_RawTicks() - qT) / 208u;
    qT = Tethys_RawTicks();
    Tethys_gBootPhase = 6;
#endif
    Map::Load_Path_Items_482C10(field_2C_camera_array[3], LoadMode::ConstructObject_0);
    Map::Load_Path_Items_482C10(field_2C_camera_array[4], LoadMode::ConstructObject_0);
    Map::Load_Path_Items_482C10(field_2C_camera_array[1], LoadMode::ConstructObject_0);
    Map::Load_Path_Items_482C10(field_2C_camera_array[2], LoadMode::ConstructObject_0);

    // Create the screen manager if it hasn't already been done (probably should have always been done by this point though?)
    if (!pScreenManager_5BB5F4)
    {
        pScreenManager_5BB5F4 = ae_new<ScreenManager>();
        if (pScreenManager_5BB5F4)
        {
            pScreenManager_5BB5F4->ctor_40E3E0(field_2C_camera_array[0]->field_C_pCamRes, &field_24_camera_offset);
        }
    }

#ifdef TETHYS_SATURN
    Tethys_gBootPhase = 7;
#endif
    sPath_dword_BB47C0->Loader_4DB800(field_D0_cam_x_idx, field_D2_cam_y_idx, LoadMode::ConstructObject_0, TlvTypes::None_m1); // none = load all
#ifdef TETHYS_SATURN
    Tethys_gFlipQ[2] = (Tethys_RawTicks() - qT) / 208u; // with the ScreenManager construction (boot only)
    qT = Tethys_RawTicks();
    Tethys_gBootPhase = 8;
#endif
    if (prevPathId != field_2_current_path || prevLevelId != field_0_current_level)
    {
        if (sActiveHero_5C1B68)
        {
            if (field_2_current_path == sActiveHero_5C1B68->field_C0_path_number)
            {
                sActiveHero_5C1B68->VCheckCollisionLineStillValid_408A40(10);
            }
        }
    }

    Create_FG1s_480F10();

#ifdef TETHYS_SATURN
    // 425.ao.10/bt1044-1047/bt1069: q3, then lo (the whole tail since FlipEnd),
    // lc (the flip's CD time), lk and nr, all over the same window, closed
    // before the rare film branches below.
    Tethys_gFlipQ[3] = (Tethys_RawTicks() - qT) / 208u;
    Tethys_gFlipPostMs = (Tethys_RawTicks() - Tethys_gFlipPostRaw0) / 208u;
    Tethys_gFlipCdMs = Tethys_gCdRawAccum / 208u;
    Tethys_gFlipKb = Tethys_gCdBytes >> 10;
    Tethys_gFlipReads = Tethys_gCdReads;
#endif

    if (field_10_screen_change_effect == CameraSwapEffects::ePlay1FMV_5)
    {
        Map::FMV_Camera_Change_482650(field_2C_camera_array[0]->field_C_pCamRes, this, field_A_level);
    }

    if (field_10_screen_change_effect == CameraSwapEffects::eUnknown_11)
    {
        pScreenManager_5BB5F4->DecompressCameraToVRam_40EF60(reinterpret_cast<u16**>(field_2C_camera_array[0]->field_C_pCamRes));
        pScreenManager_5BB5F4->InvalidateRect_40EC10(0, 0, 640, 240);
        pScreenManager_5BB5F4->MoveImage_40EB70();
        pScreenManager_5BB5F4->field_40_flags |= 0x10000;
    }

    if (prevLevelId != field_0_current_level)
    {
        pResourceManager_5C1BB0->LoadingLoop_465590(FALSE);
    }

    if (field_10_screen_change_effect != CameraSwapEffects::ePlay1FMV_5 && field_10_screen_change_effect != CameraSwapEffects::eUnknown_11)
    {
        if (field_1E_door)
        {
            // TODO: Add template helpers

            // Door transition
            Path_Door* pDoorTlv = static_cast<Path_Door*>(sPath_dword_BB47C0->TLV_First_Of_Type_In_Camera_4DB6D0(TlvTypes::Door_5, 0));
            while (pDoorTlv->field_18_door_number != sActiveHero_5C1B68->field_1A0_door_id)
            {
                pDoorTlv = static_cast<Path_Door*>(Path::TLV_Next_Of_Type_4DB720(pDoorTlv, TlvTypes::Door_5));
            }

            CreateScreenTransistionForTLV(pDoorTlv);
        }
        else
        {
            if (!field_20)
            {
                auto obj = ae_new<CameraSwapper>();
                if (obj)
                {
                    obj->ctor_4E5000(field_2C_camera_array[0]->field_C_pCamRes, field_10_screen_change_effect, 368 / 2, 240 / 2);
                }
            }
            else
            {
                // TODO: Add template helpers

                // Teleporter transition
                Path_Teleporter* pTeleporterTlv = static_cast<Path_Teleporter*>(sPath_dword_BB47C0->TLV_First_Of_Type_In_Camera_4DB6D0(TlvTypes::Teleporter_88, 0));
                Path_Teleporter_Data teleporterData = pTeleporterTlv->field_10_data;
                while (teleporterData.field_10_teleporter_switch_id != sActiveHero_5C1B68->field_1A0_door_id)
                {
                    pTeleporterTlv = static_cast<Path_Teleporter*>(Path::TLV_Next_Of_Type_4DB720(pTeleporterTlv, TlvTypes::Teleporter_88));
                    teleporterData = pTeleporterTlv->field_10_data;
                }

                CreateScreenTransistionForTLV(pTeleporterTlv);
            }
        }
    }

    bHideLoadingIcon_5C1BAA = 0;
    loading_ticks_5C1BAC = 0;

    field_8_force_load = 0;

    if (sSoundChannelsMask_5C3120)
    {
        SND_Stop_Channels_Mask_4CA810(sSoundChannelsMask_5C3120);
        sSoundChannelsMask_5C3120 = 0;
    }
}

Camera* Map::GetCamera(CameraPos pos)
{
    return field_2C_camera_array[static_cast<s32>(pos)];
}

#ifdef TETHYS_SATURN
// SATURN (AO twin, U1a A9): the neighbour Camera objects are never built here
// (GoTo_Camera_481890), so the edge flips and the neighbour rectangles ask the
// path's CameraName grid with Create_Camera_4829E0's own border semantics
// (bounds check, then an empty name = no camera there). No heap.
static bool Tethys_GridHasCamera(Map* pMap, s32 dx, s32 dy)
{
    const s32 xpos = pMap->field_D0_cam_x_idx + dx;
    const s32 ypos = pMap->field_D2_cam_y_idx + dy;
    if (!sPath_dword_BB47C0 || xpos < 0 || ypos < 0
        || xpos >= sPath_dword_BB47C0->field_6_cams_on_x || ypos >= sPath_dword_BB47C0->field_8_cams_on_y)
    {
        return false;
    }
    u8** ppPathRes = pMap->GetPathResourceBlockPtr(pMap->field_2_current_path);
    if (!ppPathRes || !*ppPathRes)
    {
        return false;
    }
    const CameraName* pCamName = reinterpret_cast<const CameraName*>(
        &(*ppPathRes)[sizeof(CameraName) * (xpos + sPath_dword_BB47C0->field_6_cams_on_x * ypos)]);
    return pCamName->name[0] != 0;
}
#endif

void Map::CreateScreenTransistionForTLV(Path_TLV* pTlv)
{
    auto obj = ae_new<CameraSwapper>();
    if (obj)
    {
        // TODO: Refactor
        const FP_Point* pCamPos2 = pScreenManager_5BB5F4->field_20_pCamPos;
        const s16 doorYDiff = static_cast<s16>(pTlv->field_8_top_left.field_2_y - FP_GetExponent(pCamPos2->field_4_y));
        FP camX = pCamPos2->field_0_x;
        const s16 midX = (pTlv->field_8_top_left.field_0_x + pTlv->field_C_bottom_right.field_0_x) / 2;

        const s16 rightPos = static_cast<s16>(midX - FP_GetExponent(camX));
        const s16 xpos2 = rightPos;
        obj->ctor_4E5000(field_2C_camera_array[0]->field_C_pCamRes, field_10_screen_change_effect, xpos2, doorYDiff);
    }
}

void Map::Get_map_size_480640(PSX_Point* pPoint)
{
    pPoint->field_0_x = field_D4_ptr->field_4_bTop;
    pPoint->field_2_y = field_D4_ptr->field_6_bBottom;
}

void Map::GetCurrentCamCoords_480680(PSX_Point* pPoint)
{
    pPoint->field_0_x = field_D0_cam_x_idx * field_D4_ptr->field_A_grid_width;
    pPoint->field_2_y = field_D2_cam_y_idx * field_D4_ptr->field_C_grid_height;
}

void Map::Get_Abe_Spawn_Pos_4806D0(PSX_Point* pPoint)
{
    pPoint->field_0_x = field_D4_ptr->field_1A_abe_start_xpos;
    pPoint->field_2_y = field_D4_ptr->field_1C_abe_start_ypos;
}

s16 Map::GetOverlayId_480710()
{
    // TODO: Probably need to redo field_C data as 1 bytes instead of a word
    return Path_Get_Bly_Record_460F30(field_A_level, field_C_path)->field_C_overlay_id & 0xFF;
}

void Map::Create_FG1s_480F10()
{
    pScreenManager_5BB5F4->UnsetDirtyBits_FG1_40ED70();

    Camera* pCamera = field_2C_camera_array[0];
    for (s32 i = 0; i < pCamera->field_0.Size(); i++)
    {
        u8** ppRes = pCamera->field_0.ItemAt(i);
        if (!ppRes)
        {
            break;
        }

        if (*ppRes)
        {
            ResourceManager::Header* pHeader = ResourceManager::Get_Header_49C410(ppRes);
            if (pHeader->field_8_type == ResourceManager::Resource_FG1)
            {
                FG1* pFG1 = ae_new<FG1>();
                pFG1->ctor_499FC0(ppRes);
            }
        }
    }
}

s16 Map::Get_Camera_World_Rect_481410(CameraPos camIdx, PSX_RECT* pRect)
{
    if (camIdx < CameraPos::eCamCurrent_0 || camIdx > CameraPos::eCamRight_4)
    {
        return 0;
    }

    Camera* pCamera = field_2C_camera_array[static_cast<s32>(camIdx)];
#ifdef TETHYS_SATURN
    // SATURN (AO 448.ao.3 twin, U1a A9): THE NEIGHBOUR SCREENS ARE HEARD. With
    // the neighbour Camera objects not built, everything that asks where an
    // object stands relative to the screen got "nowhere" for anything off
    // screen: the sounds of what walked off (GetDirection_4811A0, Sfx.cpp:243),
    // the chase and attack logic of a creature next door (Fleech, Glukkon,
    // Paramite, Scrab, Slog). The cell is tested on the grid, as Create_Camera
    // would have, and the rectangle is built from the cell indices, which are
    // what a neighbour Camera's field_14/16 held.
    s32 camX = 0;
    s32 camY = 0;
    if (pCamera)
    {
        camX = pCamera->field_14_xpos;
        camY = pCamera->field_16_ypos;
    }
    else if (camIdx != CameraPos::eCamCurrent_0)
    {
        static const s8 kDx[5] = {0, 0, 0, -1, 1}; // CameraPos: current, top, bottom, left, right
        static const s8 kDy[5] = {0, -1, 1, 0, 0};
        const s32 i = static_cast<s32>(camIdx);
        if (!Tethys_GridHasCamera(this, kDx[i], kDy[i]))
        {
            return 0;
        }
        camX = field_D0_cam_x_idx + kDx[i];
        camY = field_D2_cam_y_idx + kDy[i];
    }
    else
    {
        return 0;
    }

    if (!pRect)
    {
        return 1;
    }

    const s16 xpos = static_cast<s16>(camX * field_D4_ptr->field_A_grid_width);
    const s16 ypos = static_cast<s16>(camY * field_D4_ptr->field_C_grid_height);
#else
    if (!pCamera)
    {
        return 0;
    }

    if (!pRect)
    {
        return 1;
    }

    const s16 xpos = pCamera->field_14_xpos * field_D4_ptr->field_A_grid_width;
    const s16 ypos = pCamera->field_16_ypos * field_D4_ptr->field_C_grid_height;
#endif

    pRect->x = xpos;
    pRect->y = ypos;
    pRect->w = xpos + 368;
    pRect->h = ypos + 240;
    return 1;
}

s16 Map::Is_Point_In_Current_Camera_4810D0(s32 level, s32 path, FP xpos, FP ypos, s16 width)
{
    const FP calculated_width = (width != 0) ? FP_FromInteger(6) : FP_FromInteger(0);
    if (static_cast<LevelIds>(level) != field_0_current_level || path != field_2_current_path) // TODO: Remove when 100%
    {
        return FALSE;
    }

    PSX_RECT rect = {};
    rect.x = FP_GetExponent(xpos - calculated_width);
    rect.w = FP_GetExponent(calculated_width + xpos);
    rect.y = FP_GetExponent(ypos);
    rect.h = FP_GetExponent(ypos);
    return Rect_Location_Relative_To_Active_Camera_480FE0(&rect) == CameraPos::eCamCurrent_0;
}


EXPORT CameraPos Map::Rect_Location_Relative_To_Active_Camera_480FE0(PSX_RECT* pRect)
{
    if (Event_Get_422C00(kEventDeathReset))
    {
        return CameraPos::eCamNone_5;
    }

    const s32 camX = FP_GetExponent(field_24_camera_offset.field_0_x);
    const s32 camY = FP_GetExponent(field_24_camera_offset.field_4_y);

    if (pRect->x > (camX + 368))
    {
        return CameraPos::eCamRight_4;
    }

    if (pRect->y > (camY + 240))
    {
        return CameraPos::eCamBottom_2;
    }

    if (pRect->w >= camX)
    {
        if (pRect->h < camY)
        {
            return CameraPos::eCamTop_1;
        }
        else
        {
            return CameraPos::eCamCurrent_0;
        }
    }

    return CameraPos::eCamLeft_3;
}

s16 Map::SetActiveCam_480D30(LevelIds level, s16 path, s16 cam, CameraSwapEffects screenChangeEffect, s16 fmvBaseId, s16 forceChange)
{
    if (!forceChange && cam == field_4_current_camera && level == field_0_current_level && path == field_2_current_path)
    {
        return 0;
    }

    field_E_camera = cam;
    field_12_fmv_base_id = fmvBaseId;
    field_C_path = path;
    field_A_level = level;
    field_10_screen_change_effect = screenChangeEffect;

    field_6_state = CamChangeStates::eInstantChange_2;

    if (screenChangeEffect == CameraSwapEffects::ePlay1FMV_5 || screenChangeEffect == CameraSwapEffects::eUnknown_11)
    {
        sMap_bDoPurpleLightEffect_5C311C = 1;
    }
    else
    {
        sMap_bDoPurpleLightEffect_5C311C = 0;
    }

    return 1;
}

BaseGameObject* CC Map::FMV_Camera_Change_482650(u8** ppBits, Map* pMap, LevelIds lvlId)
{
    if (pMap->field_12_fmv_base_id > 10000u)
    {
        // Trippe FMV
        FmvInfo* pFmvRec1 = Path_Get_FMV_Record_460F70(lvlId, pMap->field_12_fmv_base_id / 10000);
        FmvInfo* pFmvRec2 = Path_Get_FMV_Record_460F70(lvlId, pMap->field_12_fmv_base_id / 100 % 100);
        FmvInfo* pFmvRec3 = Path_Get_FMV_Record_460F70(lvlId, pMap->field_12_fmv_base_id % 100);
        sLevelId_dword_5CA408 = static_cast<s32>(lvlId); // TODO: Strongly type this, but it hasn't got the same underlaying type as the enum grr..
        u32 pos1 = 0;
        u32 pos2 = 0;
        u32 pos3 = 0;
        Get_fmvs_sectors_494460(
            pFmvRec1->field_0_pName,
            pFmvRec2->field_0_pName,
            pFmvRec3->field_0_pName,
            &pos1,
            &pos2,
            &pos3);

        auto pSwapper = ae_new<CameraSwapper>();
        return pSwapper->ctor_4E4ED0(
            ppBits,
            pos1,
            pFmvRec1->field_4_id,
            pos2,
            pFmvRec2->field_4_id,
            pos3,
            pFmvRec3->field_4_id,

            pFmvRec1->field_6_flags & 1,
            pFmvRec1->field_8_flags,
            pFmvRec1->field_A_volume,

            pFmvRec2->field_6_flags & 1,
            pFmvRec2->field_8_flags,
            pFmvRec2->field_A_volume,

            pFmvRec3->field_6_flags & 1,
            pFmvRec3->field_8_flags,
            pFmvRec3->field_A_volume);
    }
    else if (pMap->field_12_fmv_base_id >= 100u)
    {
        // Double FMV
        FmvInfo* pFmvRec1 = Path_Get_FMV_Record_460F70(lvlId, pMap->field_12_fmv_base_id / 100);
        FmvInfo* pFmvRec2 = Path_Get_FMV_Record_460F70(lvlId, pMap->field_12_fmv_base_id % 100);
        u32 cdPos1 = 0;
        u32 cdPos2 = 0;
        Get_fmvs_sectors_494460(pFmvRec1->field_0_pName, pFmvRec2->field_0_pName, 0, &cdPos1, &cdPos2, 0);
        sLevelId_dword_5CA408 = static_cast<s32>(lvlId); // HACK
        auto pSwapper = ae_new<CameraSwapper>();
        return pSwapper->ctor_4E4DC0(
            ppBits,
            cdPos1,
            pFmvRec1->field_4_id,
            cdPos2,
            pFmvRec2->field_4_id,

            pFmvRec1->field_6_flags & 1,
            pFmvRec1->field_8_flags,
            pFmvRec1->field_A_volume,

            pFmvRec2->field_6_flags & 1,
            pFmvRec2->field_8_flags,
            pFmvRec2->field_A_volume);
    }
    else // < 100
    {
        // Single FMV
        FmvInfo* pFmvRec1 = Path_Get_FMV_Record_460F70(lvlId, pMap->field_12_fmv_base_id);
        u32 cdPos = 0;
        Get_fmvs_sectors_494460(pFmvRec1->field_0_pName, 0, 0, &cdPos, 0, 0);
        sLevelId_dword_5CA408 = static_cast<s32>(lvlId); // HACK
        auto pSwapper = ae_new<CameraSwapper>();
        return pSwapper->ctor_4E4CA0(
            ppBits,
            cdPos,
            pFmvRec1->field_4_id,
            pFmvRec1->field_6_flags & 1,
            pFmvRec1->field_8_flags,
            pFmvRec1->field_A_volume);
    }
}

Camera* Map::Create_Camera_4829E0(s16 xpos, s16 ypos, s32 /*a4*/)
{
    // Check min bound
    if (xpos < 0 || ypos < 0)
    {
        return nullptr;
    }

    // Check max bounds
    if (xpos >= sPath_dword_BB47C0->field_6_cams_on_x || ypos >= sPath_dword_BB47C0->field_8_cams_on_y)
    {
        return nullptr;
    }

    // Return existing camera if we already have one
    for (s32 i = 0; i < ALIVE_COUNTOF(field_40_stru_5); i++)
    {
        if (field_40_stru_5[i]
            && field_40_stru_5[i]->field_1A_level == field_0_current_level
            && field_40_stru_5[i]->field_18_path == field_2_current_path
            && field_40_stru_5[i]->field_14_xpos == xpos
            && field_40_stru_5[i]->field_16_ypos == ypos)
        {
            Camera* pTemp = field_40_stru_5[i];
            field_40_stru_5[i] = nullptr;
            return pTemp;
        }
    }

    // Get a pointer to the camera name from the Path resource
    const u8* pPathData = *GetPathResourceBlockPtr(field_2_current_path);
    auto pCamName = reinterpret_cast<const CameraName*>(&pPathData[(xpos + (ypos * sPath_dword_BB47C0->field_6_cams_on_x)) * sizeof(CameraName)]);

    // Empty/blank camera in the map array
    if (!pCamName->name[0])
    {
        return nullptr;
    }

    Camera* newCamera = ae_new<Camera>();
    newCamera->ctor_480DD0();

    // Copy in the camera name from the Path resource and append .CAM
    memset(newCamera->field_1E_cam_name, 0, sizeof(newCamera->field_1E_cam_name));
    strncpy(newCamera->field_1E_cam_name, pCamName->name, ALIVE_COUNTOF(CameraName::name));
    strcat(newCamera->field_1E_cam_name, ".CAM");

    newCamera->field_14_xpos = xpos;
    newCamera->field_16_ypos = ypos;

    newCamera->field_30_flags &= -1;

    newCamera->field_1A_level = field_0_current_level;
    newCamera->field_18_path = field_2_current_path;

    // Calculate hash/resource ID of the camera
    newCamera->field_10_camera_resource_id = 1 * (pCamName->name[7] - '0') + 10 * (pCamName->name[6] - '0') + 100 * (pCamName->name[4] - '0') + 1000 * (pCamName->name[3] - '0');

    // Convert the 2 digit camera number string to an integer
    newCamera->field_1C_camera_number = 1 * (pCamName->name[7] - '0') + 10 * (pCamName->name[6] - '0');

    return newCamera;
}

void CCSTD Map::Load_Path_Items_482C10(Camera* pCamera, LoadMode loadMode)
{
    if (!pCamera)
    {
        return;
    }

    // Is camera resource loaded check
    if (!(pCamera->field_30_flags & 1))
    {
        if (loadMode == LoadMode::ConstructObject_0)
        {
#ifdef TETHYS_SATURN
            // SATURN (decision D-R1, U1a A4, AO round 5 twin): the .CAM streams
            // instead of being staged whole: Bits to VDP2 NBG1 (never heaped),
            // FG1/Anim/Font/Demo fabricated as heap chunks in the camera's array.
            // Synchronous and complete on return (field_30_flags bit 0 set,
            // field_C_pCamRes left null: the guarded DecompressCameraToVRam has
            // nothing to do). The order is the PC's: the camera was queued first
            // and read first.
            //   AO 420.ao.1 (U7-T8): a screen change that plays films first paints
            // its destination HIDDEN, into the background container while NBG1 is
            // parked, and the end of the films reveals it with register writes
            // (ScreenManager::DecompressCameraToVRam_40EF60 from CameraSwapper, or
            // the film layer): no flash of the camera the film will cover, no
            // second CD read of the same .CAM after it.
            {
                const CameraSwapEffects eff = gMap_5C3030.field_10_screen_change_effect;
                Tethys_gCamPaintHidden = eff == CameraSwapEffects::ePlay1FMV_5
                                      || eff == CameraSwapEffects::ePlay2FMVs_9
                                      || eff == CameraSwapEffects::ePlay3FMVs_10;
            }
            ResourceManager::Tethys_StreamCamFile(pCamera);
            Tethys_gCamPaintHidden = false;
#else
            // Async camera load
            ResourceManager::LoadResourceFile_49C130(pCamera->field_1E_cam_name, Camera::On_Loaded_480ED0, pCamera, pCamera);
#endif

            sCameraBeingLoaded_5C3118 = pCamera;
#ifdef TETHYS_SATURN
            if (sTethysAskedEarly == pCamera)
            {
                sTethysAskedEarly = nullptr; // 446.ao.11: asked before the old camera let go
            }
            else
#endif
            {
                sPath_dword_BB47C0->Loader_4DB800(pCamera->field_14_xpos, pCamera->field_16_ypos, LoadMode::LoadResourceFromList_1, TlvTypes::None_m1); // none = load all
            }
        }
        else
        {
            // Blocking camera load
            ResourceManager::LoadResourceFile_49C170(pCamera->field_1E_cam_name, pCamera);
            pCamera->field_30_flags |= 1;
            pCamera->field_C_pCamRes = ResourceManager::GetLoadedResource_49C2A0(ResourceManager::Resource_Bits, pCamera->field_10_camera_resource_id, 1, 0);

            sCameraBeingLoaded_5C3118 = pCamera;
            sPath_dword_BB47C0->Loader_4DB800(pCamera->field_14_xpos, pCamera->field_16_ypos, LoadMode::LoadResource_2, TlvTypes::None_m1); // none = load all
        }
        sCameraBeingLoaded_5C3118 = nullptr;
    }
}

void CC Map::LoadResource_4DBE00(const char_type* pFileName, s32 type, s32 resourceId, LoadMode loadMode, s16 bDontLoad)
{
    if (!bDontLoad)
    {
        pResourceManager_5C1BB0->LoadResource_464EE0(pFileName, type, resourceId, sCameraBeingLoaded_5C3118, sCameraBeingLoaded_5C3118, 0, 1);
        if (loadMode == LoadMode::LoadResource_2)
        {
            pResourceManager_5C1BB0->LoadingLoop_465590(0);
        }
    }
}

void CC Map::LoadResourcesFromList_4DBE70(const char_type* pFileName, ResourceManager::ResourcesToLoadList* pList, LoadMode loadMode, s16 bDontLoad)
{
    if (!bDontLoad)
    {
        pResourceManager_5C1BB0->LoadResourcesFromList_465150(pFileName, pList, sCameraBeingLoaded_5C3118, sCameraBeingLoaded_5C3118, 0, 1);
        if (loadMode == LoadMode::LoadResource_2)
        {
            pResourceManager_5C1BB0->LoadingLoop_465590(0);
        }
    }
}

s16 Map::SetActiveCameraDelayed_4814A0(MapDirections direction, BaseAliveGameObject* pObj, s16 swapEffect)
{
    Path_Change* pPathChangeTLV = nullptr;
    CameraSwapEffects convertedSwapEffect = CameraSwapEffects::eInstantChange_0;
    if (pObj)
    {
        pPathChangeTLV = reinterpret_cast<Path_Change*>(sPath_dword_BB47C0->TLV_Get_At_4DB4B0(
            FP_GetExponent(pObj->field_B8_xpos),
            FP_GetExponent(pObj->field_BC_ypos),
            FP_GetExponent(pObj->field_B8_xpos),
            FP_GetExponent(pObj->field_BC_ypos),
            TlvTypes::PathTransition_1));
    }

    if (pObj && pPathChangeTLV)
    {
        field_A_level = pPathChangeTLV->field_10_level;
        field_C_path = pPathChangeTLV->field_12_path;
        field_E_camera = pPathChangeTLV->field_14_camera;
        if (swapEffect < 0)
        {
            // Map the TLV/editor value of screen change to the internal screen change
            convertedSwapEffect = kPathChangeEffectToInternalScreenChangeEffect_55D55C[pPathChangeTLV->field_18_wipe];
        }
        else
        {
            // If not negative then its an actual swap effect
            convertedSwapEffect = static_cast<CameraSwapEffects>(swapEffect);
        }
    }
    else
    {
        switch (direction)
        {
#ifdef TETHYS_SATURN
            // SATURN (AO twin, U1a A9): the neighbour cameras are not built:
            // the grid answers (Tethys_GridHasCamera), else no edge flip at all.
            case MapDirections::eMapLeft_0:
                if (!Tethys_GridHasCamera(this, -1, 0))
                {
                    return 0;
                }
                break;
            case MapDirections::eMapRight_1:
                if (!Tethys_GridHasCamera(this, 1, 0))
                {
                    return 0;
                }
                break;
            case MapDirections::eMapBottom_3:
                if (!Tethys_GridHasCamera(this, 0, 1))
                {
                    return 0;
                }
                break;
            case MapDirections::eMapTop_2:
                if (!Tethys_GridHasCamera(this, 0, -1))
                {
                    return 0;
                }
                break;
#else
            case MapDirections::eMapLeft_0:
                if (!GetCamera(CameraPos::eCamLeft_3))
                {
                    return 0;
                }
                break;
            case MapDirections::eMapRight_1:
                if (!GetCamera(CameraPos::eCamRight_4))
                {
                    return 0;
                }
                break;
            case MapDirections::eMapBottom_3:
                if (!GetCamera(CameraPos::eCamBottom_2))
                {
                    return 0;
                }
                break;
            case MapDirections::eMapTop_2:
                if (!GetCamera(CameraPos::eCamTop_1))
                {
                    return 0;
                }
                break;
#endif
        }

        field_A_level = field_0_current_level;
        field_C_path = field_2_current_path;
        convertedSwapEffect = static_cast<CameraSwapEffects>(swapEffect); // TODO: Correct ??
    }

    field_14_direction = direction;
    field_18_pAliveObj = pObj;
    field_1C = convertedSwapEffect;
    field_6_state = CamChangeStates::eSliceCam_1;
    sMap_bDoPurpleLightEffect_5C311C = 0;

    if (convertedSwapEffect == CameraSwapEffects::ePlay1FMV_5 || convertedSwapEffect == CameraSwapEffects::eUnknown_11)
    {
        sMap_bDoPurpleLightEffect_5C311C = 1;
    }

    return 1;
}

ALIVE_VAR(1, 0x5C3030, Map, gMap_5C3030, {});
