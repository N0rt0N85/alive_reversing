#include "stdafx.h"
#include "AddPointer.hpp"
#include "Game.hpp"
#include "Sys.hpp"
#include "VGA.hpp"
#include "bmp.hpp"
#include "Input.hpp"
#include "Error.hpp"
#include "Psx.hpp"
#include "DynamicArray.hpp"
#include "Sound/Sound.hpp" // for shut down func
#include "AmbientSound.hpp"
#include "Function.hpp"
#include "ResourceManager.hpp"
#include "PsxDisplay.hpp"
#include "Map.hpp"
#include "ScreenManager.hpp"
#include "Animation.hpp"
#include "stdlib.hpp"
#include "PauseMenu.hpp"
#include "GameSpeak.hpp"
#include "PathData.hpp"
#include "DDCheat.hpp"
#include "QuikSave.hpp"
#include "Io.hpp"
#include "LvlArchive.hpp"
#include "DDraw.hpp"
#include "Sound/Midi.hpp"
#include <atomic>
#include <fstream>
#include "DebugHelpers.hpp"
#include "Events.hpp"
#include "Abe.hpp"
#include "PathData.hpp"
#include "MusicController.hpp"
#include <gmock/gmock.h>
#include "CheatController.hpp"
#include "FG1.hpp"
#include "PsxRender.hpp"
#include "Slurg.hpp"
#include "Movie.hpp"
#include "PathDataExtensions.hpp"
#include "GameAutoPlayer.hpp"
#include <string>

void Game_ForceLink()
{ }

#ifdef TETHYS_SATURN
// SATURN (U4 T13, twin of AO Game.cpp:3-176): the overlay's frame-phase feeds.
// All are defined, game-neutral, in src/sys_saturn.cxx, which resets them on
// every screen change and prints them (rows 3, 12): pu = the VUpdate walk, pa
// = AnimateAll, pv = the drawables, the FG1 list and ScreenManager::VRender;
// what PSX_VSync and the OT walk cost is measured there. nu/nl count VUpdate
// calls and list length, n the drawables walked, vs the ScreenManager walk in
// raw FRT ticks. The raw-tick reads go through the probe gate (START+L stops
// the instrument, not only its display: AO ao242.13).
extern "C" u32 Tethys_gPhUpd;
extern "C" u32 Tethys_gPhAnim;
extern "C" u32 Tethys_gPhAnimRaw;
extern "C" u32 Tethys_gPhRend;
extern "C" u32 Tethys_gScrRaw;
extern "C" u32 Tethys_gDrawWalk;
extern "C" u32 Tethys_gVuCalls;
extern "C" u32 Tethys_gVuList;
extern "C" u32 Tethys_gCurCam; // so a photo names its own screen
extern "C" u32 Tethys_gAbeX;   // Abe's world position (the debug spawn's coordinates
extern "C" u32 Tethys_gAbeY;   // are read off these, never derived)
extern "C" u8 Tethys_gProbeOn;
extern "C" u32 Tethys_RawTicks(void);
#define TETHYS_PT() (Tethys_gProbeOn ? Tethys_RawTicks() : 0u)
// SATURN (R2's request, AO Game.cpp:790-814 twin): the parent flag that routes
// each cel upload's cost to AnimateAll or to VUpdate (the dc gauges of AE
// Animation.cpp), and the slave SH-2's LZSS prefetch of the next tick's cels
// (AliveLibCommon/TethysCelCache.hpp, compiled into AE Animation.cpp; fenced
// before the sprite flush by src/sys_saturn.cxx).
extern "C" u32 Tethys_gInAnimate;
extern "C" void Tethys_CelPrefetch();
extern "C" void Tethys_GlowTickAE(); // src/glow_ae.cxx (declared with its twins in TethysObjectHooks.hpp)
static u32 sTethysPhase0 = 0;
extern "C" void Tethys_ConfigLoad(void); // src/sys_saturn.cxx: the options record
#endif

using TExitGameCallBack = AddPointer_t<void CC()>;

ALIVE_VAR(1, 0xBBFB00, TExitGameCallBack, sGame_OnExitCallback_BBFB00, nullptr);

ALIVE_VAR(1, 0x5C1B84, u32, sGnFrame_5C1B84, 0);

// Timer
ALIVE_VAR(1, 0xBBB9D4, u32, sTimer_period_BBB9D4, 0);

// Arrays of things
ALIVE_VAR(1, 0x5C1B78, DynamicArrayT<BaseGameObject>*, ObjList_5C1B78, nullptr);
ALIVE_VAR(1, 0x5BD4D8, DynamicArray*, ObjList_5BD4D8, nullptr);
ALIVE_VAR(1, 0x5C1B80, DynamicArrayT<ShadowZone>*, sShadowZone_dArray_5C1B80, nullptr);

ALIVE_VAR(1, 0x5C2FE0, s16, sBreakGameLoop_5C2FE0, 0);
ALIVE_VAR(1, 0x5C1B66, s16, sNum_CamSwappers_5C1B66, 0);
ALIVE_VAR(1, 0x5C2F78, s32, dword_5C2F78, 0);
ALIVE_VAR(1, 0x5C2FA0, s16, bSkipGameObjectUpdates_5C2FA0, 0);

ALIVE_ARY(1, 0x5CA488, s8, 30, sCdRomDrives_5CA488, {});
ALIVE_VAR(1, 0x5CA4D4, s32, dword_5CA4D4, 0);
ALIVE_VAR(1, 0x55EF90, s32, k1_dword_55EF90, 1);
ALIVE_VAR(1, 0x55EF88, bool, byte_55EF88, true);
ALIVE_VAR(1, 0x5CA4D0, bool, sCommandLine_ShowFps_5CA4D0, false);
ALIVE_VAR(1, 0x5CA4B5, bool, sCommandLine_DDCheatEnabled_5CA4B5, false);
ALIVE_VAR(1, 0x5CA4D2, bool, byte_5CA4D2, false);
ALIVE_VAR(1, 0x5CA4E0, s32, dword_5CA4E0, 0);

// Fps calcs
ALIVE_VAR(1, 0xBD0F08, s8, bQuitting_BD0F08, 0);
#ifndef TETHYS_SATURN // SATURN: see Calculate_FPS_495250
ALIVE_VAR(1, 0x55EFDC, f64, sFps_55EFDC, 0.0);
#endif
ALIVE_VAR(1, 0x5CA4DC, s32, sFrameDiff_5CA4DC, 0);
ALIVE_VAR(1, 0xC2D03C, s32, sNumRenderedPrims_C2D03C, 0);
ALIVE_VAR(1, 0x5CA300, s32, sFrameCount_5CA300, 0);

ALIVE_VAR(1, 0x5C1B94, s16, word_5C1B94, 0);
ALIVE_VAR(1, 0x5C2A4C, Abe, gAbeObj_5C2A4C, {}); // TODO: Refactor to remove global object, in fact it seems pointless since an abe can get new'ed up, just check the BaseGameObject type instead ??
ALIVE_VAR(1, 0x554D5C, Abe*, spAbe_554D5C, &gAbeObj_5C2A4C);


ALIVE_VAR(1, 0x5C2F6C, u32, dword_5C2F6C, 0);
ALIVE_VAR(1, 0x5C1BA0, u16, gAttract_5C1BA0, 0);
ALIVE_VAR(1, 0x5C2F70, u32, dword_5C2F70, 0);



ALIVE_VAR(1, 0x5c1b68, Abe*, sActiveHero_5C1B68, 0);

bool gDebugHelpersEnabled = false;

#include "GameEnderController.hpp"
#include "ColourfulMeter.hpp"
#include "GasCountDown.hpp"

EXPORT void CC Init_GameStates_43BF40()
{
    sKilledMudokons_5C1BC0 = sFeeco_Restart_KilledMudCount_5C1BC6;
    sRescuedMudokons_5C1BC2 = sFeecoRestart_SavedMudCount_5C1BC8;

    // TODO: Check that the unused vars here really are unused
    //word_5C1BCA = 0;
    //word_5C1BCC = 0;
    //word_5C1BF0 = 0;

    gGasOn_5C1C00 = 0;
    sGasTimer_5C1BE8 = 0;

    gbDrawMeterCountDown_5C1BF8 = FALSE;
    gTotalMeterBars_5C1BFA = 0;

    gAbeBulletProof_5C1BDA = 0;

    SwitchStates_SetRange_465FA0(2u, 255u);
}

EXPORT bool CC Is_Cd_Rom_Drive_495470(CHAR driveLetter)
{
#if _WIN32
    CHAR RootPathName[4] = {};
    strcpy(RootPathName, "C:\\");
    RootPathName[0] = driveLetter;
    return ::GetDriveTypeA(RootPathName) == DRIVE_CDROM;
#else
    return false;
#endif
}

void DestroyObjects_4A1F20()
{
    pResourceManager_5C1BB0->LoadingLoop_465590(FALSE);
    for (s32 iterations = 0; iterations < 2; iterations++)
    {
        for (s32 idx = 0; idx < gBaseGameObject_list_BB47C4->Size(); idx++)
        {
            BaseGameObject* pObj = gBaseGameObject_list_BB47C4->ItemAt(idx);
            if (!pObj)
            {
                break;
            }

            if (!(pObj->field_6_flags.Get(BaseGameObject::eSurviveDeathReset_Bit9)))
            {
                idx = gBaseGameObject_list_BB47C4->RemoveAt(idx);
                pObj->VDestructor(1);
            }
        }
    }
}

#ifndef TETHYS_SATURN // SATURN (U3 T10): the PC's FPS text (doubles), only Game_End_Frame_4950F0 (src's on the Saturn) called it
EXPORT f64 CC Calculate_FPS_495250(s32 frameCount)
{
    static u32 sLastTime_5CA338 = SYS_GetTicks() - 500;
    const u32 curTime = SYS_GetTicks();
    const s32 timeDiff = curTime - sLastTime_5CA338;

    if (static_cast<s32>((curTime - sLastTime_5CA338)) < 500)
    {
        return sFps_55EFDC;
    }

    const s32 diffFrames = frameCount - sFrameDiff_5CA4DC;
    sFps_55EFDC = static_cast<f64>(diffFrames) * 1000.0 / static_cast<f64>(timeDiff);

    sLastTime_5CA338 = curTime;
    sFrameDiff_5CA4DC = frameCount;
    return sFps_55EFDC;
}

EXPORT void CC DrawFps_4952F0(Bitmap* pBmp, s32 x, s32 y, f32 fps)
{
    char_type strBuffer[125] = {};
    sprintf(strBuffer, "%02.1f fps ", fps);
    sNumRenderedPrims_C2D03C = 0;
    BMP_Draw_String_4F2230(pBmp, x, y, 0xFF80FFu, 1, strBuffer);
    Add_Dirty_Area_4ED970(0, 0, 97, 32);
}
#endif // !TETHYS_SATURN: Calculate_FPS_495250, DrawFps_4952F0

EXPORT void CC sub_4FBA20()
{
    NOT_IMPLEMENTED();
}

EXPORT void CC CheckShiftCapslock_4953B0()
{
    NOT_IMPLEMENTED();
}

EXPORT void CC Draw_Debug_Strings_4F2800()
{
    NOT_IMPLEMENTED();
}


#ifndef TETHYS_SATURN // SATURN: src/sys_saturn.cxx defines it (the PC body is FPS text, the message pump, exit())
EXPORT s32 CC Game_End_Frame_4950F0(u32 flags)
{
    if (flags & 1)
    {
        turn_off_rendering_BD0F20 = 0;
        return 0;
    }

    /*
    const u8 oldShowVRam = sPsxEMU_show_vram_BD1465;
    if (sCommandLine_DDCheatEnabled_5CA4B5)
    {
        if (Input_IsVKPressed_4EDD40(VK_SCROLL))
        {
            sPsxEMU_show_vram_BD1465 = !sPsxEMU_show_vram_BD1465;
            while (Input_IsVKPressed_4EDD40(VK_SCROLL))
            {
                //nullsub_3();
                SYS_EventsPump_494580();
            }
        }
    }

    if (oldShowVRam && !sPsxEMU_show_vram_BD1465)
    {
        Add_Dirty_Area_4ED970(0, 0, 640, 240);
    }
    */

    const f64 fps = Calculate_FPS_495250(sFrameCount_5CA300);
    CheckShiftCapslock_4953B0();
    if (sCommandLine_ShowFps_5CA4D0)
    {
        Bitmap* pVram = spBitmap_C2D038;
        if (!spBitmap_C2D038)
        {
            pVram = &sPsxVram_C1D160;
        }

        DrawFps_4952F0(
            pVram,
            sPSX_EMU_DrawEnvState_C3D080.field_0_clip.x + 4,
            sPSX_EMU_DrawEnvState_C3D080.field_0_clip.y + 4,
            static_cast<f32>(fps));
    }

    Draw_Debug_Strings_4F2800();
    ++sFrameCount_5CA300;

    if (Sys_PumpMessages_4EE4F4())
    {
        bQuitting_BD0F08 = 1;
        exit(0);
    }
    sub_4FBA20();
    return 0;
}
#endif

EXPORT void CC VLC_Tables_Init_496720()
{
    NOT_IMPLEMENTED();
}

EXPORT void CC Main_ParseCommandLineArguments_494EA0(const char_type* /*pCmdLineNotUsed*/, const char_type* pCommandLine)
{
    //nullsub_2(); // Note: Pruned
    IO_Init_494230();

    // Default the CD drive to C:
    char_type strDrive[3] = {};
    strcpy(strDrive, "C:");

    // Collect up all CD rom drives
    s32 pos = 0;
    for (s8 drive = 'D'; drive < 'Z'; drive++)
    {
        if (Is_Cd_Rom_Drive_495470(drive))
        {
            sCdRomDrives_5CA488[pos++] = drive;
        }
    }

    // Use the first found CD ROM drive as the game location
    if (sCdRomDrives_5CA488[0])
    {
        strDrive[0] = sCdRomDrives_5CA488[0];
    }

    PSX_EMU_Set_Cd_Emulation_Paths_4FAA70(".", strDrive, strDrive);

#ifdef TETHYS_SATURN
    // SATURN (L0, decision D-B3): there is no window; the title builders of
    // Sys_common.hpp:41-75 are std::string, which would pull libstdc++'s
    // string and exception chain into the image. A literal, as AO's twin.
    Sys_WindowClass_Register_4EE22F("ABE_WINCLASS", "Tethys Exoddus", 32, 64, 640, 480);
#else
    std::string windowTitle = WindowTitleAE();
    
    if (GetGameAutoPlayer().IsRecording())
    {
        windowTitle += " [Recording]";
    }
    else if (GetGameAutoPlayer().IsPlaying())
    {
        windowTitle += " [AutoPlay]";
    }

    Sys_WindowClass_Register_4EE22F("ABE_WINCLASS", windowTitle.c_str(), 32, 64, 640, 480);
#endif
    Sys_Set_Hwnd_4F2C50(Sys_GetWindowHandle_4EE180());

    dword_5CA4D4 = 0;
    k1_dword_55EF90 = 1; // Global way to turn off semi trans rendering?
    byte_55EF88 = true;

    if (pCommandLine)
    {
        if (strstr(pCommandLine, "-ddfps"))
        {
            sCommandLine_ShowFps_5CA4D0 = true;
        }

        if (strstr(pCommandLine, "-ddnoskip"))
        {
            sCommandLine_NoFrameSkip_5CA4D1 = true;
        }

        if (strstr(pCommandLine, "-ddfast"))
        {
            byte_5CA4D2 = true;
            dword_5CA4D4 = 1;
            byte_55EF88 = false;
            dword_5CA4E0 = 2;
        }

        if (strstr(pCommandLine, "-ddfastest"))
        {
            dword_5CA4E0 = 1;
        }

        if (strstr(pCommandLine, "-ddcheat"))
        {
            sCommandLine_DDCheatEnabled_5CA4B5 = true;
        }

#if DEVELOPER_MODE
        if (strstr(pCommandLine, "-debug"))
        {
            gDebugHelpersEnabled = true;
        }
#endif
    }

    if (dword_5CA4E0 == 1)
    {
        PSX_DispEnv_Set_4ED960(1);
        PSX_EMU_Set_screen_mode_4F9420(1);
    }
    else if (dword_5CA4E0 == 2)
    {
        PSX_DispEnv_Set_4ED960(0);
        PSX_EMU_Set_screen_mode_4F9420(0);
    }
    else
    {
        PSX_DispEnv_Set_4ED960(2);
        PSX_EMU_Set_screen_mode_4F9420(2);
    }

    Init_VGA_AndPsxVram_494690();
    PSX_EMU_Init_4F9CD0(false);
    PSX_EMU_VideoAlloc_4F9D70();
    PSX_EMU_SetCallBack_4F9430(1, Game_End_Frame_4950F0);
    //Main_Set_HWND_4F9410(Sys_GetWindowHandle_4EE180()); // Note: Set but never read
    VLC_Tables_Init_496720();
}

EXPORT s32 CC CreateTimer_4EDEC0(UINT /*uDelay*/, void* /*callBack*/)
{
    NOT_IMPLEMENTED();
    return 0;
}


ALIVE_VAR(1, 0x5C1A24, DynamicArrayT<AnimationBase>*, gObjList_animations_5C1A24, nullptr);
ALIVE_VAR(1, 0x5C1124, DynamicArrayT<BaseGameObject>*, gObjList_drawables_5C1124, nullptr);


EXPORT void CC static_init_set_default_hero_43EC10()
{
    sActiveHero_5C1B68 = spAbe_554D5C;
}

EXPORT void CC Init_Sound_DynamicArrays_And_Others_43BDB0()
{
    DebugFont_Init_4DCF40();
    word_5C1B94 = 1; // Used in dead overlay stuff, CD number ??
    //Overlays_Init_43BFC0(); // Note: Pointless because never used in PC
    pPauseMenu_5C9300 = nullptr;
    sActiveHero_5C1B68 = spAbe_554D5C;
    sControlledCharacter_5C1B8C = 0;
    sNum_CamSwappers_5C1B66 = 0;
    sGnFrame_5C1B84 = 0;
    sbLoadingInProgress_5C1B96 = 0;

    ObjList_5C1B78 = ae_new<DynamicArrayT<BaseGameObject>>(); // For trap doors/dynamic platforms?
    ObjList_5C1B78->ctor_40CA60(20);

    ObjList_5BD4D8 = ae_new<DynamicArray>();
    ObjList_5BD4D8->ctor_40CA60(10); // Never seems to be used?

    sShadowZone_dArray_5C1B80 = ae_new<DynamicArrayT<ShadowZone>>();
    sShadowZone_dArray_5C1B80->ctor_40CA60(4);

    gBaseAliveGameObjects_5C1B7C = ae_new<DynamicArrayT<BaseAliveGameObject>>();
    gBaseAliveGameObjects_5C1B7C->ctor_40CA60(20);

    ResourceManager::Init_49BCE0();
    SND_Init_4CA1F0();
    SND_Init_Ambiance_4CB480();
    MusicController::Create_47FC40();
    Init_GameStates_43BF40(); // Init other vars + switch states
}

#ifndef TETHYS_SATURN // SATURN: src/sys_saturn.cxx defines it (no message pump, no exit)
EXPORT void CC SYS_EventsPump_494580()
{
    if (Sys_PumpMessages_4EE4F4())
    {
        bQuitting_BD0F08 = 1;
        exit(0);
    }
    sub_4FBA20();
}
#endif

EXPORT void CC DDCheat_Allocate_415320()
{
    auto pDDCheat = ae_new<DDCheat>();
    pDDCheat->ctor_4153C0();
}

EXPORT void CC Game_Loop_467230();

#if defined(TETHYS_AE_TOUR) && TETHYS_AE_TOUR
// SATURN (AE_ENGINE_DECISION.md 11.20, step 3): THE HEAP TOUR, a TEST IMAGE
// ONLY (tethys_start_ae.mk TETHYS_AE_TOUR=1). Once a real game runs (not the
// menu, not a demo), Abe flies (DDCheat's flight) through every camera of the
// nine game levels, the game's own SetActiveCam with an instant change, three
// seconds each. Per camera, three words: the resource heap in use (KB, 10 bits),
// its largest free block (KB, 10 bits) and the lowest HWRAM pool free seen
// during the three seconds (64-byte units, 12 bits); then the most bytes of
// game objects the heap held during them (11.22, the overflow past the HWRAM
// reserve); then the textures at the end of them (11.22): live VDP1 slots (7
// bits), KB left in the texture heap (9 bits), uploads the renderer refused
// (12 bits) and animations refused a PSX VRAM rect (4 bits) during them. The log
// lives in the resource heap (one locked block, last fit), not in .bss: the
// HWRAM pool is one of the things measured. tools/ae/ae_tour.py read joins it to
// the camera list. A wedge or an OOM on the way is the usual named fatal: the
// log says how far the tour got.
#include "ae/tour_cams.inc"
u32 Tethys_HeapLargestFree(); // ResourceManager.cpp
extern "C" u32 Tethys_PoolFree(); // src/game_ae.cxx
extern "C" volatile u32 Tethys_gHeapObjBytes; // ResourceManager.cpp, 11.22
extern "C" u32 Tethys_TexCensus();            // src/renderer_saturn.cxx, 11.22
extern "C" u32 Tethys_gS5[24];                // [7]: uploads refused
extern "C" u32 Tethys_gVramFail;              // src/sys_saturn.cxx
extern "C" {
volatile u32 Tethys_gTourDone = 0;
volatile u32 Tethys_gTourWords = 0; // words per camera, for the reader
u32* volatile Tethys_gTourLogPtr = nullptr;
}
static s32 sTethysTourIdx = -1;
static s32 sTethysTourWait = 0;
static s32 sTethysTourPlace = 0;
static u32 sTethysTourPoolMin = 0xFFFFFFFFu;
static u32 sTethysTourObjPeak = 0;
static u32 sTethysTourTexFail0 = 0;
static u32 sTethysTourVramFail0 = 0;

static void Tethys_TourTick()
{
    if (gAttract_5C1BA0 || !sActiveHero_5C1B68 || gMap_5C3030.field_6_state != Map::CamChangeStates::eInactive_0
        || gMap_5C3030.field_0_current_level == LevelIds::eMenu_0 || gMap_5C3030.field_0_current_level == LevelIds::eNone)
    {
        return;
    }
    const u32 count = ALIVE_COUNTOF(kTethysTourCams);
    if (sTethysTourIdx < 0)
    {
        u8** ppLog = ResourceManager::Alloc_New_Resource_ImplEx(0x52554F54 /* 'TOUR' */, 0, count * 3 * sizeof(u32), true,
                                                                ResourceManager::eLastMatching, false, false);
        if (!ppLog)
        {
            return; // no room yet: try again next frame
        }
        Tethys_gTourLogPtr = reinterpret_cast<u32*>(*ppLog);
        Tethys_gTourWords = 3;
        sTethysTourIdx = 0;
        sTethysTourWait = 150;
        return;
    }
    const u32 pool = Tethys_PoolFree();
    if (pool < sTethysTourPoolMin)
    {
        sTethysTourPoolMin = pool;
    }
    if (Tethys_gHeapObjBytes > sTethysTourObjPeak)
    {
        sTethysTourObjPeak = Tethys_gHeapObjBytes;
    }
    if (sTethysTourPlace)
    {
        // The change is done: Abe to the middle of the camera, flying, as
        // DDCheat's teleport places him, so no fall and no death on arrival.
        sTethysTourPlace = 0;
        PSX_Point pos = {};
        gMap_5C3030.GetCurrentCamCoords_480680(&pos);
        sActiveHero_5C1B68->field_B8_xpos = FP_FromInteger(pos.field_0_x + 184);
        sActiveHero_5C1B68->field_BC_ypos = FP_FromInteger(pos.field_2_y + 60);
        sActiveHero_5C1B68->field_C2_lvl_number = gMap_5C3030.field_0_current_level;
        sActiveHero_5C1B68->field_C0_path_number = gMap_5C3030.field_2_current_path;
        sActiveHero_5C1B68->field_100_pCollisionLine = nullptr;
        sActiveHero_5C1B68->field_F8_LastLineYPos = sActiveHero_5C1B68->field_BC_ypos;
        sDDCheat_FlyingEnabled_5C2C08 = true;
    }
    if (--sTethysTourWait > 0)
    {
        return;
    }
    if (sTethysTourIdx > 0 && static_cast<u32>(sTethysTourIdx) <= count)
    {
        const u32 used = sManagedMemoryUsedSize_AB4A04 >> 10;
        const u32 mx = Tethys_HeapLargestFree() >> 10;
        const u32 pm = sTethysTourPoolMin >> 6;
        u32* const pRow = &Tethys_gTourLogPtr[3 * (sTethysTourIdx - 1)];
        pRow[0] = ((used > 1023u ? 1023u : used) << 22) | ((mx > 1023u ? 1023u : mx) << 12) | (pm > 4095u ? 4095u : pm);
        pRow[1] = sTethysTourObjPeak;
        const u32 tex = Tethys_TexCensus();
        const u32 live = tex >> 16;
        const u32 room = tex & 0xFFFFu;
        const u32 texFail = Tethys_gS5[7] - sTethysTourTexFail0;
        const u32 vramFail = Tethys_gVramFail - sTethysTourVramFail0;
        pRow[2] = ((live > 127u ? 127u : live) << 25) | ((room > 511u ? 511u : room) << 16)
                | ((texFail > 4095u ? 4095u : texFail) << 4) | (vramFail > 15u ? 15u : vramFail);
        Tethys_gTourDone = static_cast<u32>(sTethysTourIdx);
    }
    if (static_cast<u32>(sTethysTourIdx) >= count)
    {
        sTethysTourWait = 0x7FFFFFFF; // done: Abe stays where the last camera put him
        return;
    }
    const u16 w = kTethysTourCams[sTethysTourIdx];
    sDDCheat_FlyingEnabled_5C2C08 = true;
    gMap_5C3030.SetActiveCam_480D30(static_cast<LevelIds>(w >> 12), static_cast<s16>((w >> 7) & 31), static_cast<s16>(w & 127),
                                    CameraSwapEffects::eInstantChange_0, 0, 0);
    sTethysTourPlace = 1;
    sTethysTourPoolMin = 0xFFFFFFFFu;
    sTethysTourObjPeak = Tethys_gHeapObjBytes;
    sTethysTourTexFail0 = Tethys_gS5[7];
    sTethysTourVramFail0 = Tethys_gVramFail;
    sTethysTourIdx++;
    sTethysTourWait = 90; // three seconds of updates for the screen's loads
}
#endif

EXPORT void CC Game_Init_LoadingIcon_482CD0()
{
    u8** ppRes = ResourceManager::GetLoadedResource_49C2A0(ResourceManager::Resource_Animation, AEResourceID::kLoadingResID, 1u, 0);
    if (!ppRes)
    {
        ResourceManager::LoadResourceFile_49C170("LOADING.BAN", nullptr);
        ppRes = ResourceManager::GetLoadedResource_49C2A0(ResourceManager::Resource_Animation, AEResourceID::kLoadingResID, 1u, 0);
    }
    ResourceManager::Set_Header_Flags_49C650(ppRes, ResourceManager::ResourceHeaderFlags::eNeverFree);
}

EXPORT void CC Game_Free_LoadingIcon_482D40()
{
    u8** ppRes = ResourceManager::GetLoadedResource_49C2A0(ResourceManager::Resource_Animation, AEResourceID::kLoadingResID, 0, 0);
    if (ppRes)
    {
        ResourceManager::FreeResource_49C330(ppRes);
    }
}

#if DEVELOPER_MODE
extern bool gBootToLoadScreen;
#endif

EXPORT void CC Game_Run_466D40()
{
    // Begin start up
    SYS_EventsPump_494580();
    dword_5C2F6C = 6000;
    gAttract_5C1BA0 = 0;
    dword_5C2F70 = 34;
    SYS_EventsPump_494580();

    PSX_ResetCallBack_4FAA20();
    gPsxDisplay_5C1130.ctor_41DC30();
    PSX_CdInit_4FB2C0();
    PSX_CdSetDebug_4FB330(0);
    Input_Pads_Reset_4FA960(); // starts card/pads on psx ver

    gBaseGameObject_list_BB47C4 = ae_new<DynamicArrayT<BaseGameObject>>();
    gBaseGameObject_list_BB47C4->ctor_40CA60(50);

    gObjList_drawables_5C1124 = ae_new<DynamicArrayT<BaseGameObject>>();
    gObjList_drawables_5C1124->ctor_40CA60(30);

    gFG1List_5D1E28 = ae_new<DynamicArrayT<FG1>>();
    gFG1List_5D1E28->ctor_40CA60(4);


    gObjList_animations_5C1A24 = ae_new<DynamicArrayT<AnimationBase>>();
    gObjList_animations_5C1A24->ctor_40CA60(30);

    pResourceManager_5C1BB0 = ae_new<ResourceManager>();
    pResourceManager_5C1BB0->ctor_464910();

    // NOTE: We need to call Input_Init() before Init_Sound_DynamicArrays_And_Others() because of gLatencyHack
    // which can be configured from the ini
    Input_Init_491BC0();

    Init_Sound_DynamicArrays_And_Others_43BDB0();

#ifdef TETHYS_SATURN
    // SATURN (U1c T13, AO 448.ao.7 twin): the player's options record (pad
    // remap, mono), ABE_EXOD_99 on the backup device. Here rather than in
    // Input_Init above, where the PC reads its ini: without a cartridge the
    // backup library borrows its 24 KB from the resource heap, which the call
    // just above has only now bound; with one, main.cxx already fetched it
    // before the cartridge heap existed and this only applies it.
    Tethys_ConfigLoad();
#endif

#ifdef TETHYS_SATURN
    // SATURN: Camera is #pragma pack(2) (Camera.hpp, the PC's 0x32 bytes), so the
    // compiler may put this local at a 2-aligned stack slot (it did: sp+30), and
    // DynamicArray::ctor's 32-bit stores into it raise a CPU address error on the
    // console and on Mednafen at boot (Ymir lets them through). 4-aligned here.
    alignas(4) Camera camera;
#else
    Camera camera;
#endif
    camera.ctor_480DD0();

#ifndef TETHYS_SATURN // SATURN (L5): no Pxtd chunk in any AE level; AO skips its twin too
    Path_Set_NewData_FromLvls();
#endif
    
    sLvlArchive_5BC520.Open_Archive_432E80(CdLvlName(LevelIds::eMenu_0));

    ResourceManager::LoadResourceFile_49C170("STP01C25.CAM", &camera);

    camera.field_C_pCamRes = ResourceManager::GetLoadedResource_49C2A0(ResourceManager::Resource_Bits, AEResourceID::kUnknownResID_125, 1u, 0);
    gMap_5C3030.field_24_camera_offset.field_4_y = FP_FromInteger(0);
    gMap_5C3030.field_24_camera_offset.field_0_x = FP_FromInteger(0);

    pScreenManager_5BB5F4 = ae_new<ScreenManager>();
    pScreenManager_5BB5F4->ctor_40E3E0(camera.field_C_pCamRes, &gMap_5C3030.field_24_camera_offset);

    pScreenManager_5BB5F4->DecompressCameraToVRam_40EF60((u16**) camera.field_C_pCamRes);
    pScreenManager_5BB5F4->MoveImage_40EB70();

    sLvlArchive_5BC520.Free_433130();

    camera.dtor_480E00();

    s16 cameraId = 25;
#if DEVELOPER_MODE
    #if _WIN32
    if (GetKeyState(VK_LSHIFT) >= 0)
    {
        gBootToLoadScreen = true;
        cameraId = 1;
    }
    #endif
#endif

    gMap_5C3030.Init_4803F0(LevelIds::eMenu_0, 1, cameraId, CameraSwapEffects::eInstantChange_0, 0, 0);

    DDCheat_Allocate_415320();
    pEventSystem_5BC11C = ae_new<GameSpeak>();
    pEventSystem_5BC11C->ctor_421820();

    pCheatController_5BC120 = ae_new<CheatController>();
    pCheatController_5BC120->ctor_421BD0();

#ifndef TETHYS_SATURN // SATURN: the PC developer console (DebugHelpers.cpp) is not in the image
    if (gDebugHelpersEnabled)
    {
        DebugHelpers_Init(); // Custom helper code
    }
#endif

    Game_Init_LoadingIcon_482CD0();

    // Main loop start
    Game_Loop_467230();

    // Shut down start
    Game_Free_LoadingIcon_482D40();
    DDCheat::ClearProperties_415390();
    gMap_5C3030.Shutdown_4804E0();

    if (gObjList_animations_5C1A24)
    {
        gObjList_animations_5C1A24->dtor_40CAD0();
        ae_delete_free_495540(gObjList_animations_5C1A24);
    }

    if (gObjList_drawables_5C1124)
    {
        gObjList_drawables_5C1124->dtor_40CAD0();
        ae_delete_free_495540(gObjList_drawables_5C1124);
    }

    if (gFG1List_5D1E28)
    {
        gFG1List_5D1E28->dtor_40CAD0();
        ae_delete_free_495540(gFG1List_5D1E28);
    }

    if (gBaseGameObject_list_BB47C4)
    {
        gBaseGameObject_list_BB47C4->dtor_40CAD0();
        ae_delete_free_495540(gBaseGameObject_list_BB47C4);
    }

    if (ObjList_5C1B78)
    {
        ObjList_5C1B78->dtor_40CAD0();
        ae_delete_free_495540(ObjList_5C1B78);
    }

    if (ObjList_5BD4D8)
    {
        ObjList_5BD4D8->dtor_40CAD0();
        ae_delete_free_495540(ObjList_5BD4D8);
    }

    if (sShadowZone_dArray_5C1B80)
    {
        sShadowZone_dArray_5C1B80->dtor_40CAD0();
        ae_delete_free_495540(sShadowZone_dArray_5C1B80);
    }

    if (gBaseAliveGameObjects_5C1B7C)
    {
        gBaseAliveGameObjects_5C1B7C->dtor_40CAD0();
        ae_delete_free_495540(gBaseAliveGameObjects_5C1B7C);
    }

    if (sCollisions_DArray_5C1128)
    {
        sCollisions_DArray_5C1128->dtor_4189F0();
        ae_delete_free_495540(sCollisions_DArray_5C1128);
    }

    pMusicController_5C3020 = nullptr; // Note: OG bug - should have been set to nullptr after shutdown call?
    MusicController::Shutdown_47FD20();

    SND_Reset_Ambiance_4CB4B0();
    SND_Shutdown_4CA280();
    PSX_CdControlB_4FB320(8, 0, 0);
    PSX_ResetCallBack_4FAA20();
    PSX_StopCallBack_4FAA30();
    sInputObject_5BD4E0.ShutDown_45F020();
    PSX_ResetGraph_4F8800(0);
}

EXPORT void CC Game_SetExitCallBack_4F2BA0(TExitGameCallBack callBack)
{
    sGame_OnExitCallback_BBFB00 = callBack;
}

EXPORT void CC Game_ExitGame_4954B0()
{
    PSX_EMU_VideoDeAlloc_4FA010();
}

EXPORT void CC Game_Shutdown_4F2C30()
{
    if (sGame_OnExitCallback_BBFB00)
    {
        sGame_OnExitCallback_BBFB00();
        sGame_OnExitCallback_BBFB00 = nullptr;
    }

    CreateTimer_4EDEC0(0, nullptr); // Creates a timer that calls a call back which is always null, therefore seems like dead code?
    Input_DisableInputForPauseMenuAndDebug_4EDDC0();
    //SND_MCI_Close_4F0060(nullptr); // TODO: Seems like more dead code because the mci is never set?
    GetSoundAPI().SND_SsQuit();
    IO_Stop_ASync_IO_Thread_4F26B0();
    VGA_Shutdown_4F3170();
}

EXPORT s32 TMR_Init_4EDE20()
{
#if USE_SDL2 || defined(TETHYS_SATURN) // SATURN: no Win32 timer; SYS_GetTicks is the FRT (sys_saturn.cxx)
    return 0;
#else
    struct timecaps_tag ptc = {};
    if (::timeGetDevCaps(&ptc, sizeof(timecaps_tag)))
    {
        Error_PushErrorRecord_4F2920("C:\\abe2\\code\\POS\\TMR.C", 25, 0, "TMR_Init: timeGetDevCaps() failed !");
        return -1;
    }

    sTimer_period_BBB9D4 = ptc.wPeriodMin;
    // This makes timers as accurate as possible increasing cpu/power usage as a trade off
    ::timeBeginPeriod(ptc.wPeriodMin);
    return 0;
#endif
}

EXPORT s32 CC Init_Input_Timer_And_IO_4F2BF0(bool forceSystemMemorySurfaces)
{
    static bool sbGameShutdownSet_BBC560 = false;
    if (!sbGameShutdownSet_BBC560)
    {
        // OG: Change - this gets called normally anyway, using atexit results in a f64 call that
        // will f64 free and use freed objects
        //atexit(Game_Shutdown_4F2C30);
        sbGameShutdownSet_BBC560 = 1;
        gVGA_force_sys_memory_surfaces_BC0BB4 = forceSystemMemorySurfaces;
    }

    Input_EnableInput_4EDDD0();
    Input_InitKeyStateArray_4EDD60();
    TMR_Init_4EDE20();

    if (!IO_CreateThread())
    {
        return -1;
    }

    if (strstr(Sys_GetCommandLine_4EE176(), "-syncread"))
    {
        sIOSyncReads_BD2A5C = TRUE;
    }
    else
    {
        sIOSyncReads_BD2A5C = FALSE;
    }
    return 0;
}

EXPORT void CC Game_Main_4949F0()
{
    // Inits
    Init_Input_Timer_And_IO_4F2BF0(false);

    GetGameAutoPlayer().ParseCommandLine(Sys_GetCommandLine_4EE176());

    Main_ParseCommandLineArguments_494EA0(Sys_GetCommandLine_4EE176(), Sys_GetCommandLine_4EE176());

    Game_SetExitCallBack_4F2BA0(Game_ExitGame_4954B0);
#if _WIN32
    #if !USE_SDL2
    Sys_SetWindowProc_Filter_4EE197(Sys_WindowMessageHandler_494A40);
    #endif
#endif
    // Only returns once the engine is shutting down
    Game_Run_466D40();

    if (sGame_OnExitCallback_BBFB00)
    {
        sGame_OnExitCallback_BBFB00();
        sGame_OnExitCallback_BBFB00 = nullptr;
    }

    Game_Shutdown_4F2C30();
}

EXPORT void CC Game_Loop_467230()
{
    dword_5C2F78 = 0;
    sBreakGameLoop_5C2FE0 = 0;
    bool bPauseMenuObjectFound = false;
    while (!gBaseGameObject_list_BB47C4->IsEmpty())
    {
        GetGameAutoPlayer().SyncPoint(SyncPoints::MainLoopStart);

        Events_Reset_Active_422DA0();
        Slurg::Clear_Slurg_Step_Watch_Points_449A90();
        bSkipGameObjectUpdates_5C2FA0 = 0;

        // Update objects
        GetGameAutoPlayer().SyncPoint(SyncPoints::ObjectsUpdateStart);
        for (s32 baseObjIdx = 0; baseObjIdx < gBaseGameObject_list_BB47C4->Size(); baseObjIdx++)
        {
            BaseGameObject* pBaseGameObject = gBaseGameObject_list_BB47C4->ItemAt(baseObjIdx);

            if (!pBaseGameObject || bSkipGameObjectUpdates_5C2FA0)
            {
                break;
            }

            if (pBaseGameObject->field_6_flags.Get(BaseGameObject::eUpdatable_Bit2)
                && pBaseGameObject->field_6_flags.Get(BaseGameObject::eDead_Bit3) == false
                && (sNum_CamSwappers_5C1B66 == 0 || pBaseGameObject->field_6_flags.Get(BaseGameObject::eUpdateDuringCamSwap_Bit10)))
            {
                const s32 updateDelay = pBaseGameObject->UpdateDelay();
                if (updateDelay <= 0)
                {
                    if (pBaseGameObject == pPauseMenu_5C9300)
                    {
                        bPauseMenuObjectFound = true;
                    }
                    else
                    {
#ifdef TETHYS_SATURN
                        Tethys_gVuCalls++; // SATURN (U4 T13): one add, no clock read
#endif
                        pBaseGameObject->VUpdate();
                    }
                }
                else
                {
                    pBaseGameObject->SetUpdateDelay(updateDelay - 1);
                }
            }
        }
        GetGameAutoPlayer().SyncPoint(SyncPoints::ObjectsUpdateEnd);

#ifdef TETHYS_SATURN
        // SATURN (U4 T13): close pu (armed after the previous OT render), feed
        // the screen and Abe readouts, then bracket AnimateAll.
        Tethys_gVuList += static_cast<u32>(gBaseGameObject_list_BB47C4->Size());
        {
            const u32 tNow = SYS_GetTicks(); // one read per stamp: each span is a difference
            Tethys_gPhUpd += tNow - sTethysPhase0;
            sTethysPhase0 = tNow;
        }
        Tethys_gCurCam = static_cast<u32>(gMap_5C3030.field_4_current_camera);
        if (sActiveHero_5C1B68)
        {
            Tethys_gAbeX = static_cast<u32>(FP_GetExponent(sActiveHero_5C1B68->field_B8_xpos));
            Tethys_gAbeY = static_cast<u32>(FP_GetExponent(sActiveHero_5C1B68->field_BC_ypos));
        }
        Tethys_gInAnimate = 1; // SATURN (R2's request): set and cleared once per tick
        const u32 tTethysAnimRaw0 = TETHYS_PT();
#endif
        // Animate everything
        if (sNum_CamSwappers_5C1B66 <= 0)
        {
            GetGameAutoPlayer().SyncPoint(SyncPoints::AnimateAll);
            AnimationBase::AnimateAll_40AC20(gObjList_animations_5C1A24);
#ifdef TETHYS_SATURN
            // SATURN (Exoddus, src/glow_ae.cxx): a still Abe under dust that moved
            // is uploaded again with its light, and once more without it when the
            // dust has gone. Before the prefetch: no slave batch is in flight.
            Tethys_GlowTickAE();
            // SATURN (R2's request, AO 445.ao.1 twin): hand the NEXT tick's cels
            // to the slave SH-2, which decodes them while VRender and the OT walk
            // run below.
            Tethys_CelPrefetch();
#endif
        }
#ifdef TETHYS_SATURN
        Tethys_gInAnimate = 0; // SATURN: back under VUpdate's parentage
        Tethys_gPhAnimRaw += TETHYS_PT() - tTethysAnimRaw0;
        {
            const u32 tNow = SYS_GetTicks();
            Tethys_gPhAnim += tNow - sTethysPhase0;
            sTethysPhase0 = tNow;
        }
#endif

        PrimHeader** ppOtBuffer = gPsxDisplay_5C1130.field_10_drawEnv[gPsxDisplay_5C1130.field_C_buffer_index].field_70_ot_buffer;

        // Render objects
        GetGameAutoPlayer().SyncPoint(SyncPoints::DrawAllStart);
        for (s32 i = 0; i < gObjList_drawables_5C1124->Size(); i++)
        {
            BaseGameObject* pObj = gObjList_drawables_5C1124->ItemAt(i);
            if (!pObj)
            {
                break;
            }

            if (pObj->field_6_flags.Get(BaseGameObject::eDead_Bit3))
            {
                pObj->field_6_flags.Clear(BaseGameObject::eCantKill_Bit11);
            }
            else if (pObj->field_6_flags.Get(BaseGameObject::eDrawable_Bit4))
            {
                pObj->field_6_flags.Set(BaseGameObject::eCantKill_Bit11);
                pObj->VRender(ppOtBuffer);
            }
        }
        GetGameAutoPlayer().SyncPoint(SyncPoints::DrawAllEnd);
#ifdef TETHYS_SATURN
        // SATURN (U4 T13): the list length, one add at the exit (the walk stops
        // only on a null, which DynamicArray's swap-with-last forbids).
        Tethys_gDrawWalk += static_cast<u32>(gObjList_drawables_5C1124->Size());
#endif

        // Render FG1's
        for (s32 i = 0; i < gFG1List_5D1E28->Size(); i++)
        {
            FG1* pFG1 = gFG1List_5D1E28->ItemAt(i);
            if (!pFG1)
            {
                break;
            }

            if (pFG1->field_6_flags.Get(BaseGameObject::eDead_Bit3))
            {
                pFG1->field_6_flags.Clear(BaseGameObject::eCantKill_Bit11);
            }
            else if (pFG1->field_6_flags.Get(BaseGameObject::eDrawable_Bit4))
            {
                pFG1->field_6_flags.Set(BaseGameObject::eCantKill_Bit11);
                pFG1->VRender(ppOtBuffer);
            }
        }

        DebugFont_Flush_4DD050();
        PSX_DrawSync_4F6280(0);
#ifdef TETHYS_SATURN
        {
            // SATURN (U4 T13) 'vs': ScreenManager::VRender, one call a tick.
            const u32 tScr0 = TETHYS_PT();
            pScreenManager_5BB5F4->VRender(ppOtBuffer);
            Tethys_gScrRaw += TETHYS_PT() - tScr0;
        }
#else
        pScreenManager_5BB5F4->VRender(ppOtBuffer);
#endif
        SYS_EventsPump_494580(); // Exit checking?
#ifdef TETHYS_SATURN
        Tethys_gPhRend += SYS_GetTicks() - sTethysPhase0; // SATURN (U4 T13): pv
#endif

        GetGameAutoPlayer().SyncPoint(SyncPoints::RenderOT);
        gPsxDisplay_5C1130.PSX_Display_Render_OT_41DDF0();
#ifdef TETHYS_SATURN
        // SATURN (U4 T13): pu restarts here, after the OT render (whose walk and
        // present src/sys_saturn.cxx measures itself), as AO's loop does.
        sTethysPhase0 = SYS_GetTicks();
#endif

        GetGameAutoPlayer().SyncPoint(SyncPoints::RenderStart);

        // Destroy objects with certain flags
        for (s32 idx = 0; idx < gBaseGameObject_list_BB47C4->Size(); idx++)
        {
            BaseGameObject* pObj = gBaseGameObject_list_BB47C4->ItemAt(idx);
            if (!pObj)
            {
                break;
            }

            if (pObj->field_6_flags.Get(BaseGameObject::eDead_Bit3) && pObj->field_6_flags.Get(BaseGameObject::eCantKill_Bit11) == false)
            {
                idx = gBaseGameObject_list_BB47C4->RemoveAt(idx);
                pObj->VDestructor(1);
            }
        }

        GetGameAutoPlayer().SyncPoint(SyncPoints::RenderEnd);

        if (bPauseMenuObjectFound && pPauseMenu_5C9300)
        {
            pPauseMenu_5C9300->VUpdate();
        }

        bPauseMenuObjectFound = false;

#if defined(TETHYS_AE_TOUR) && TETHYS_AE_TOUR
        Tethys_TourTick(); // AE_ENGINE_DECISION.md 11.20 step 3, test image only
#endif
        gMap_5C3030.ScreenChange_480B80();
        sInputObject_5BD4E0.Update(GetGameAutoPlayer());

        if (sNum_CamSwappers_5C1B66 == 0)
        {
            GetGameAutoPlayer().SyncPoint(SyncPoints::IncrementFrame);
            sGnFrame_5C1B84++;
        }

        if (sBreakGameLoop_5C2FE0)
        {
            GetGameAutoPlayer().SyncPoint(SyncPoints::MainLoopExit);
            break;
        }

        // Enabled only for ddfast option
        if (byte_5CA4D2)
        {
            pResourceManager_5C1BB0->LoadingLoop_465590(0);
        }

        GetGameAutoPlayer().ValidateObjectStates();

    } // Main loop end

    // Clear the screen to black
    PSX_RECT rect = {};
    rect.x = 0;
    rect.y = 0;
    rect.w = 640;
    rect.h = 240;
    PSX_ClearImage_4F5BD0(&rect, 0, 0, 0);
    PSX_DrawSync_4F6280(0);
    PSX_VSync_4F6170(0);

    pResourceManager_5C1BB0->Shutdown_465610();

    // Destroy all game objects
    while (!gBaseGameObject_list_BB47C4->IsEmpty())
    {
        for (s32 idx = 0; idx < gBaseGameObject_list_BB47C4->Size(); idx++)
        {
            BaseGameObject* pObj = gBaseGameObject_list_BB47C4->ItemAt(idx);
            if (!pObj)
            {
                break;
            }

            idx = gBaseGameObject_list_BB47C4->RemoveAt(idx);
            pObj->VDestructor(1);
        }
    }
}


namespace AETest::TestsGame {
static void Test_PSX_getTPage_4F60E0()
{
    ASSERT_EQ(0, PSX_getTPage_4F60E0(TPageMode::e4Bit_0, TPageAbr::eBlend_0, 0, 0));

    ASSERT_EQ(32, PSX_getTPage_4F60E0(TPageMode::e4Bit_0, TPageAbr::eBlend_1, 0, 0));
    ASSERT_EQ(64, PSX_getTPage_4F60E0(TPageMode::e4Bit_0, TPageAbr::eBlend_2, 0, 0));
    ASSERT_EQ(96, PSX_getTPage_4F60E0(TPageMode::e4Bit_0, TPageAbr::eBlend_3, 0, 0));

    ASSERT_EQ(128, PSX_getTPage_4F60E0(TPageMode::e8Bit_1, TPageAbr::eBlend_0, 0, 0));
    ASSERT_EQ(256, PSX_getTPage_4F60E0(TPageMode::e16Bit_2, TPageAbr::eBlend_0, 0, 0));
    // ASSERT_EQ(384, PSX_getTPage_4F60E0(TPageMode::e4Bit_0, TPageAbr::eBlend_0, 0, 0));

    ASSERT_EQ(1, PSX_getTPage_4F60E0(TPageMode::e4Bit_0, TPageAbr::eBlend_0, 64, 0));
    ASSERT_EQ(2, PSX_getTPage_4F60E0(TPageMode::e4Bit_0, TPageAbr::eBlend_0, 64 * 2, 64));
    ASSERT_EQ(18, PSX_getTPage_4F60E0(TPageMode::e4Bit_0, TPageAbr::eBlend_0, 64 * 2, 64 * 4));
}

void GameTests()
{
    Test_PSX_getTPage_4F60E0();
}
} // namespace AETest::TestsGame
