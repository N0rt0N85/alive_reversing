#include "stdafx_ao.h"
#include "Game.hpp"
// SATURN: bt1125 VUpdate population census. pu (row 12 `u`) is the largest
// unexplained block in the port -- 36 ms of a 90 ms tick -- and it has never
// had a single COUNT pointed at it: bt1041 measured it as 86-98 % VUpdate and
// bt1061 then deleted the per-object argmax because it charged two FRT reads
// and a call PER OBJECT PER TICK to feed statics nothing printed. These two
// are one `++` each, no clock read at all, and they are latched on the SAME
// argmax tick as pu itself (src/sys_saturn.cxx, kPhase[4..5]) so that u/nu is
// a real per-call cost rather than two different ticks divided by each other.
extern "C" unsigned int Tethys_gVuCalls;
extern "C" unsigned int Tethys_gVuList;
#include "logger.hpp"
#include "Function.hpp"
#include "FixedPoint.hpp"
#include "BaseGameObject.hpp"
#include "SwitchStates.hpp"
#include "DDCheat.hpp"
#include "Io.hpp"
#include "Psx.hpp"
#include "Sys.hpp"
#include "DynamicArray.hpp"
#include "BaseAliveGameObject.hpp"
#include "stdlib.hpp"
#include "ResourceManager.hpp"
#include "PsxDisplay.hpp"
#include "Map.hpp"
#include "GameSpeak.hpp"
#include "CheatController.hpp"
#include "DDCheat.hpp"
#include "MusicController.hpp"
#include "VGA.hpp"
#include "Input.hpp"
#include "Midi.hpp"
#include "PauseMenu.hpp"
#include "Abe.hpp"
#include "SaveGame.hpp" // SATURN bt816: seed gSaveBuffer_505668 at boot
#include "ShadowZone.hpp"
#include "CameraSwapper.hpp"
#include "AmbientSound.hpp"
#include "PsxRender.hpp"
#include "ScreenManager.hpp"
#include "Error.hpp"
#include "Events.hpp"
#include "Sound.hpp"
#include "../AliveLibAE/Game.hpp"
#include "AddPointer.hpp"
#include "PathDataExtensions.hpp"
#include "GameAutoPlayer.hpp"

namespace AO {

#ifdef TETHYS_SATURN
// SATURN (bt1012): tick-phase accumulators, defined in src/sys_saturn.cxx and
// reset there on every screen change, so they share the scope of lt/tt and
// divide by tt for per-tick figures. See the long note at the first timer in
// the main loop below for why the tick had to be cut this finely.
extern "C" u32 Tethys_gPhUpd;
extern "C" u32 Tethys_gPhAnim;
// SATURN: bt1134 -- a RAW-TICK twin of the AnimateAll bracket. bt1133's A/B was
// UNDECIDABLE and the emulator was not the reason: `a` is whole MILLISECONDS on
// a phase worth 2-12 of them, so quantisation alone is +/-6 to 25 percent and a
// 10 percent effect cannot be seen. 208 raw FRT ticks = 1 ms, so this is 208x
// the resolution for two extra clock reads per TICK -- not per object, which is
// what bt1061 deleted. The ms bracket STAYS: `o == u+a+v+w` is the closure check
// and it is in ms.
extern "C" u32 Tethys_gPhAnimRaw;
extern "C" u32 Tethys_RawTicks(void);
// SATURN (ao242.13) THE PROBE GATE -- see the banner in src/renderer_saturn.cxx.
// START+L now stops the instrument, not just its display, so the frame the
// tester records with the overlay off is the frame the GAME costs. A macro, not
// a static inline: bt1136 measured `static inline` not being inlined at -Os.
extern "C" u8 Tethys_gProbeOn;
#define TETHYS_PT() (Tethys_gProbeOn ? Tethys_RawTicks() : 0u)
extern "C" unsigned int Tethys_gTear;
extern "C" unsigned int Tethys_TearDrop(void);
// ao262.18 TETHYS_PD -- AND THE ROOT CAUSE IS IN SRL, NOT IN THE GAUGE.
//
// The `d` probe printed T9999 -- the clamp -- on the busy hardware screens, and
// held it for twenty seconds: one AO type credited with >= 999.9 ms per tick,
// which is not a slow object, it is a poisoned accumulator. The mechanism is
// SRL::Timer::Capture (SaturnRingLib/saturnringlib/srl_timer.hpp:1084):
//
//     uint16_t frtValue = (FrchReg << 8) | FrclReg;   // <-- FRT read FIRST
//     __asm__ volatile("" : : : "memory");
//     return Tickstamp(Timer::overflowCounter, frtValue);   // counter read AFTER
//
// overflowCounter is incremented by FrtHandler, an INTERRUPT HANDLER (same file,
// :891). If the FRT wraps between those two reads, the low half is pre-wrap
// (0xFFFF-ish) and the high half is post-wrap: the timestamp lands 65,536 ticks
// -- 315 ms -- in the FUTURE. The next capture is normal, so `end - start`
// underflows u32 to ~4.29e9, and one such addition owns its accumulator for the
// rest of the screen. The FRT wraps every ~315 ms, so on a twenty-second visit
// there are ~60 windows, and this port makes hundreds of captures a tick.
//
// THIS IS NOT A `d` PROBE DEFECT. Every bracket in the port reads through
// Tethys_RawTicks and every one of them is exposed; `d` is simply where it
// landed on 2026-08-22. It is also why an unclamped gauge used to print a
// ten-digit field and blow the row width (bt1147).
//
// The fix is at the DELTA, not at the capture, and that is deliberate: reading
// the counter twice to close the race would double the cost of every bracket
// (K = 0.44 raw ticks = 56 SH-2 cycles per pair on hardware, ~470 pairs a tick
// on c8), which is the tax ao242.13 built the gate to remove. A backwards delta
// is never valid, and the tear is always exactly +/- 65,536 ticks, so ONE
// unsigned compare against half a wrap rejects both directions. 32,768 ticks is
// 157 ms; no frame-path bracket is within an order of magnitude of that.
//   A statement expression, so the operands are evaluated ONCE -- `e` is a live
// hardware read and a naive macro would call it twice and measure the wrong
// thing. Not a static inline: bt1136 measured those NOT being inlined at -Os.
//   E on row 21 counts the rejects. If T9999 ever comes back with E reading 000,
// this diagnosis is refuted and the next reading is the accumulator itself.
//   The reject path is a CALL, not an inline increment: it is cold by
// construction (a tear is a once-per-screen event at worst), and the address
// literal plus load-add-store it replaces was costing text at all fifteen sites
// against a HWRAM pre-flight floor with 0.2 KB of margin left.
#define TETHYS_PD(e, s) ({ const unsigned int tethys_pd_ = (unsigned int) ((e) - (s)); \
    (tethys_pd_ < 32768u) ? tethys_pd_ : Tethys_TearDrop(); })
// SATURN (ao242.13) `d` IS THE LAST UNSPLIT NODE IN THE FRAME, and on c8 it is
// the biggest: 11.8 ms of a 57.8 ms tick for 26 drawables, 0.45 ms each, with no
// column able to say whether that is one expensive object or twenty-six ordinary
// ones. ao242.12 gated ScreenManager::VRender off, so `v` IS `d` now -- the two
// columns row 21 used to spend on `s` and on the dropped-Sprts count are free,
// and they are worth more spent naming who is inside it.
//   Cost: ONE bracket per drawable, and bt1055 is the rule it has to answer to.
// bt1055 forbade a per-iteration counter in the VUpdate loop because that loop
// runs ~250 times a tick for a few cycles apiece; this one runs 15-26 times for
// ~450,000 cycles apiece. 26 pairs at the measured K = 0.49 raw ticks is 12.7
// ticks = 0.06 ms against 11.8 -- 0.5%, and it is switched off with the rest.
//   The table is indexed by AO type id (0..103, BaseGameObject::Types is s16) and
// reset per SCREEN with everything else on rows 16-22, bt1007/bt1008: a boot
// cumulative on a per-screen row reports the past forever.
extern "C" u16 Tethys_gDrawByType[104];
// ao262.18: the table is sized to the enum (AO Types run 0..103), so the index
// is CLAMPED rather than masked -- 103 is not a power of two, and field_4_typeId
// is an s16 that a corrupt object could carry anything in.
static inline unsigned int TethysDrawBucket(AO::Types t)
{
    const int i = static_cast<int>(t);
    return (i >= 0 && i < 104) ? static_cast<unsigned int>(i) : 0u;
}
extern "C" u32 Tethys_gPhRend;
// SATURN (ao242.6) the frame hierarchy -- definitions in src/sys_saturn.cxx
extern "C" u32 Tethys_gUStamp;   // the running stamp S0..S3 differences
extern "C" u32 Tethys_gUtRaw;    // uT tail | uA loop A | uB loop B
extern "C" u32 Tethys_gUaRaw;
extern "C" u32 Tethys_gUbRaw;
extern "C" u32 Tethys_gScrRaw;   // vs  ScreenManager::VRender
extern "C" u32 Tethys_gDrawWalk; // n   drawables walked
extern "C" u32 Tethys_gInAnimate;// the parent flag for Upload's two callers
// SATURN (bt1030): pu owns the possession spike -- 25 ms of a 47 ms worst
// tick against 4 ms quiet -- and pu is one number over the whole object list.
// These three split it WITHOUT summing anything: the single most expensive
// VUpdate of the tick, that object's typeId, and how many VUpdates ran. That
// combination is what separates "one object got expensive" from "the list got
// long", and a per-type accumulator could not: 250 objects x a u32 is .bss the
// HWRAM pre-flight has no room for, and a sum would hide a single 20 ms call
// inside a big total exactly the way the per-screen means hid this spike.
// bt1061: both declarations go with the bracket they served -- Tethys_RawTicks
// had no other caller in this TU.
#if defined(TETHYS_START_CAM) && TETHYS_START_CAM > 0
// The buffer lives in SaveGame.cpp as an ALIVE_VAR with no header declaration;
// Abe.cpp:68 declares it the same way for the same reason.
ALIVE_VAR_EXTERN(SaveData, gSaveBuffer_505668);
static u16 sStartSpawnLaps = 0; // bt1017: see the boot-spawn note in the loop
static u8 sStartSpawnDone = 0;
#endif
extern "C" u32 Tethys_gAbeX;   // bt1016: world position, for the boot-spawn
extern "C" u32 Tethys_gAbeY;   // coordinates -- read, never invented
extern "C" u32 Tethys_gCurCam; // bt1014: overlay readout, so a photo names its
                               // own screen and A/Bs stop needing a route
static u32 tPhase0 = 0;

// SATURN (bt1015): the bt1014 boot teleport is REVERTED. It replayed
// DDCheat::Teleport_409CE0 verbatim -- SetActiveCam plus flying -- on the
// argument that copying the cheat could add no failure mode the manual pad-2
// route did not already have. THE FIELD SAYS OTHERWISE: the game booted on the
// first screen and then ran away through several screens in a row.
//
// The reason is the half of the manual route that is not in the cheat's code.
// SetActiveCam moves the CAMERA and nothing else, so Abe stayed at his C01
// world position while the camera sat on C03; sControlledCharacter is then out
// of the camera's bounds, and BaseAliveGameObject's edge tests
// (SetActiveCameraDelayed_444CA0, lines ~494-621) chase it screen by screen.
// A human teleports and then FLIES somewhere valid; nobody does that at boot.
// Flying kept Abe alive, which is all it was ever going to do.
//
// If this is retried, the camera is not the thing to set: Abe has to be placed
// first, which means a valid collision line at the destination, and it cannot
// be validated from the build machine -- Ymir is the user's to launch.
#endif

DynamicArrayT<BaseGameObject>* gLoadingFiles = nullptr;

// TODO: Move these few funcs to correct location
#ifdef _WIN32
EXPORT s32 CC Add_Dirty_Area_48D910(s32, s32, s32, s32)
{
    NOT_IMPLEMENTED();
    return 0;
}

EXPORT s32 MessageBox_48E3F0(const char_type* /*pTitle*/, s32 /*lineNumber*/, const char_type* /*pMsg*/, ...)
{
    NOT_IMPLEMENTED();
    return 0;
}

EXPORT s32 CC Sys_WindowMessageHandler_4503B0(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    LRESULT ret = 0;

    switch (msg)
    {
        case WM_PAINT:
        {
            RECT rect = {};
            PAINTSTRUCT paint = {};
            BeginPaint(hWnd, &paint);
            GetClientRect(hWnd, &rect);
            PatBlt(paint.hdc, 0, 0, rect.right, rect.bottom, BLACKNESS); // use pal 0
            EndPaint(hWnd, &paint);
            Add_Dirty_Area_48D910(0, 0, 640, 240);
        }
            return 1;

        case WM_CLOSE:
            return (MessageBoxA(hWnd, "Do you really want to quit ?", "Abe's Oddysee", MB_DEFBUTTON2 | MB_ICONQUESTION | MB_YESNO) == IDNO) ? -1 : 0;

        case WM_KEYDOWN:
            if (wParam == VK_F1)
            {
                MessageBox_48E3F0(
                    "About Abe",
                    -1,
                    "Oddworld Abe's Oddysee 2.0\nPC version by Digital Dialect\n\nBuild date: %s %s\n",
                    "Oct 22 1997",
                    "14:32:52");
                Input_InitKeyStateArray_48E5F0();
            }
            Input_SetKeyState_48E610(static_cast<s32>(wParam), 1);
            return 0;

        case WM_KEYUP:
            Input_SetKeyState_48E610(static_cast<s32>(wParam), 0);
            break;

        case WM_SETCURSOR:
        {
            static auto hCursor = LoadCursor(nullptr, IDC_ARROW);
            SetCursor(hCursor);
        }
            return -1;

    #ifndef BEHAVIOUR_CHANGE_FORCE_WINDOW_MODE
        case WM_NCLBUTTONDOWN:
            // Prevent window being moved when click + dragged
            return -1;
    #endif

        case WM_ACTIVATE:
        case WM_SETFOCUS:
        case WM_KILLFOCUS:
        case WM_ENTERMENULOOP:
        case WM_EXITMENULOOP:
        case WM_ENTERSIZEMOVE:
        case WM_EXITSIZEMOVE:
            Input_InitKeyStateArray_48E5F0();
            break;

        case WM_INITMENUPOPUP:
            // TODO: Constants for wParam
            if ((u32) lParam >> 16)
            {
                return -1;
            }
            break;

        case WM_SYSKEYDOWN:
            // TODO: Constants for wParam
            if (wParam == 18 || wParam == 32)
            {
                ret = -1;
            }
            Input_SetKeyState_48E610(static_cast<s32>(wParam), 1);
            break;

        case WM_SYSKEYUP:
            // TODO: Constants for wParam
            if (wParam == 18 || wParam == 32)
            {
                ret = -1;
            }
            Input_SetKeyState_48E610(static_cast<s32>(wParam), 0);
            break;

        case WM_TIMER:
            return 1;
        default:
            return static_cast<s32>(ret);
    }
    return static_cast<s32>(ret);
}
using TFilter = AddPointer_t<s32 CC(HWND, UINT, WPARAM, LPARAM)>;

EXPORT void CC Sys_SetWindowProc_Filter_48E950(TFilter)
{
    NOT_IMPLEMENTED();
}

#endif


ALIVE_VAR(1, 0x507670, u32, gnFrameCount_507670, 0);
ALIVE_VAR(1, 0x504618, DynamicArrayT<BaseGameObject>*, gObjList_drawables_504618, nullptr);

ALIVE_VAR(1, 0x50766C, DynamicArrayT<BaseGameObject>*, ObjListPlatforms_50766C, nullptr);

void Game_ForceLink()
{
}

ALIVE_VAR(1, 0x5076CC, s16, gbKillUnsavedMudsDone_5076CC, 0);

// TODO: Move to game ender controller for AO sync
ALIVE_VAR(1, 0x5076C4, s16, gRestartRuptureFarmsKilledMuds_5076C4, 0);
ALIVE_VAR(1, 0x5076C8, s16, gRestartRuptureFarmsSavedMuds_5076C8, 0);

ALIVE_VAR(1, 0x5076D0, s16, gOldKilledMuds_5076D0, 0);
ALIVE_VAR(1, 0x5076D4, s16, gOldSavedMuds_5076D4, 0);

ALIVE_VAR(1, 0x507B78, s16, sBreakGameLoop_507B78, 0);
ALIVE_VAR(1, 0x507698, s16, gAttract_507698, 0);
ALIVE_VAR(1, 0x507B0C, s32, gTimeOut_NotUsed_507B0C, 0);
ALIVE_VAR(1, 0x507B10, s32, gFileOffset_NotUsed_507B10, 0);
ALIVE_VAR(1, 0x505564, DynamicArrayT<AnimationBase>*, gObjList_animations_505564, nullptr);

ALIVE_VAR(1, 0x508BF8, s8, gDDCheatMode_508BF8, 0);
ALIVE_VAR(1, 0x508BFC, s8, byte_508BFC, 0);

ALIVE_ARY(1, 0x4CECC8, s8, 3, gDriveLetter_4CECC8, {'D', ':', '0'});



EXPORT s32 CC Game_End_Frame_4505D0(u32 bSkip)
{
    return Game_End_Frame_4950F0(bSkip);
}

static void Main_ParseCommandLineArguments()
{
    IO_Init_48E1A0(0);

    // TODO: I guess some code got removed that picked the CD-ROM drive like in AE since this
    // doesn't really make any sense anymore.
    char_type cdDrivePath[3] = {};
    cdDrivePath[0] = gDriveLetter_4CECC8[0];
    cdDrivePath[1] = gDriveLetter_4CECC8[1];
    if (gDriveLetter_4CECC8[0] > 'Z')
    {
        cdDrivePath[0] = 'C';
    }

    PSX_EMU_Set_Cd_Emulation_Paths_49B000(".", cdDrivePath, nullptr);

#ifdef TETHYS_SATURN
    // SATURN: no window, no title -- and this was the last live std::string
    // of the core (keeps basic_string-inst out of the image, ~30-40 K).
    Sys_WindowClass_Register_48E9E0("ABE_WINCLASS", "Tethys", 32, 64, 640, 480);
#else
    std::string windowTitle = WindowTitleAO();

    if (GetGameAutoPlayer().IsRecording())
    {
        windowTitle += " [Recording]";
    }
    else if (GetGameAutoPlayer().IsPlaying())
    {
        windowTitle += " [AutoPlay]";
    }

    Sys_WindowClass_Register_48E9E0("ABE_WINCLASS", windowTitle.c_str(), 32, 64, 640, 480);
#endif

    Sys_Set_Hwnd_48E340(Sys_GetWindowHandle_48E930());

    const LPSTR pCmdLine = Sys_GetCommandLine_48E920();
    if (pCmdLine)
    {
        if (_strcmpi(pCmdLine, "-it_is_me_your_father") == 0)
        {
            Input_GetCurrentKeyStates_48E630();
            if (Input_IsVKPressed_48E5D0(VK_SHIFT))
            {
                gDDCheatMode_508BF8 = 1;
                PSX_DispEnv_Set_48D900(2);
                PSX_EMU_Set_screen_mode_499910(2);
            }
        }
        // Force DDCheat
#if FORCE_DDCHEAT
        gDDCheatMode_508BF8 = 1;
#endif
    }

    if (!pCmdLine)
    {
        PSX_DispEnv_Set_48D900(2);
        PSX_EMU_Set_screen_mode_499910(2);
    }
    else
    {
        if (_strcmpi(pCmdLine, "-interline") == 0)
        {
            PSX_DispEnv_Set_48D900(1);
            PSX_EMU_Set_screen_mode_499910(1);
            byte_508BFC = 0;
        }
        else if (_strcmpi(pCmdLine, "-vstretch") == 0)
        {
            PSX_DispEnv_Set_48D900(0);
            PSX_EMU_Set_screen_mode_499910(0);
            byte_508BFC = 0;
        }
        else if (_strcmpi(pCmdLine, "-vdouble") == 0)
        {
            PSX_DispEnv_Set_48D900(0);
            PSX_EMU_Set_screen_mode_499910(0);
            byte_508BFC = 1;
        }
        else
        {
            PSX_DispEnv_Set_48D900(2);
            PSX_EMU_Set_screen_mode_499910(2);
        }
    }

    Init_VGA_AndPsxVram();

    PSX_EMU_Init_49A1D0(false);
    PSX_EMU_VideoAlloc_49A2B0();
    PSX_EMU_SetCallBack_499920(1, Game_End_Frame_4505D0);
    //Main_Set_HWND_499900(Sys_GetWindowHandle_48E930()); // Note: Set global is never read
}

EXPORT void CC Init_GameStates_41CEC0()
{
    sKilledMudokons_5076BC = gRestartRuptureFarmsKilledMuds_5076C4;
    sRescuedMudokons_5076C0 = gRestartRuptureFarmsSavedMuds_5076C8;
    sSwitchStates_505568 = {};
    gOldKilledMuds_5076D0 = 0;
    gOldSavedMuds_5076D4 = 0;
    gbKillUnsavedMudsDone_5076CC = 0;
}


EXPORT void CC Init_Sound_DynamicArrays_And_Others_41CD20()
{
    DebugFont_Init_487EC0();

    for (OverlayRecord& rec : sOverlayTable_4C5AA8.records)
    {
        CdlFILE cdFile = {};
        CdlFILE* pFile = PSX_CdSearchFile_49B930(&cdFile, rec.field_0_fileName);
        if (pFile)
        {
            rec.field_4_pos = PSX_CdLoc_To_Pos_49B3B0(&pFile->field_0_loc);
        }
    }

    pPauseMenu_5080E0 = nullptr;
    sActiveHero_507678 = nullptr;
    sControlledCharacter_50767C = nullptr;
    sNumCamSwappers_507668 = 0;
    gnFrameCount_507670 = 0;

    gFilesPending_507714 = 0;
    bLoadingAFile_50768C = 0;

    ObjListPlatforms_50766C = ao_new<DynamicArrayT<BaseGameObject>>();
    ObjListPlatforms_50766C->ctor_4043E0(20);

    ObjList_5009E0 = ao_new<DynamicArrayT<ResourceManager::ResourceManager_FileRecord>>();
    ObjList_5009E0->ctor_4043E0(10); // not used in AE

    sShadowZone_dArray_507B08 = ao_new<DynamicArrayT<ShadowZone>>();
    sShadowZone_dArray_507B08->ctor_4043E0(4);

    gBaseAliveGameObjects_4FC8A0 = ao_new<DynamicArrayT<BaseAliveGameObject>>();
    gBaseAliveGameObjects_4FC8A0->ctor_4043E0(20);

    gLoadingFiles = ao_new<DynamicArrayT<BaseGameObject>>();
    gLoadingFiles->ctor_4043E0(20); // TODO: Leaked on purpose for now

    ResourceManager::Init_454DA0();
    SND_Init_476E40();
    SND_Init_Ambiance_4765C0();
    MusicController::Create_4436C0();

    Init_GameStates_41CEC0(); // Note: inlined

    // TODO: The switch state clearing is done in Init_GameStates in AE
    // check this is not an AO bug
    SwitchStates_ClearAll();
}

EXPORT void CC Game_Init_LoadingIcon_445E30()
{
    u8** ppRes = ResourceManager::GetLoadedResource_4554F0(ResourceManager::Resource_Animation, AOResourceID::kLoadingAOResID, 1, 0);
    if (!ppRes)
    {
        ResourceManager::LoadResourceFile_455270("LOADING.BAN", nullptr);
        ppRes = ResourceManager::GetLoadedResource_4554F0(ResourceManager::Resource_Animation, AOResourceID::kLoadingAOResID, 1, 0);
    }
    ResourceManager::Set_Header_Flags_4557D0(ppRes, ResourceManager::ResourceHeaderFlags::eNeverFree);
}

EXPORT void CC Game_Free_LoadingIcon_445E80()
{
    u8** ppRes = ResourceManager::GetLoadedResource_4554F0(ResourceManager::Resource_Animation, AOResourceID::kLoadingAOResID, 0, 0);
    if (ppRes)
    {
        ResourceManager::FreeResource_455550(ppRes);
    }
}


using TExitGameCB = AddPointer_t<void CC()>;

ALIVE_VAR(1, 0x9F664C, TExitGameCB, sGame_OnExitCallback_9F664C, nullptr);

EXPORT void CC Game_SetExitCallBack_48E040(TExitGameCB)
{
    NOT_IMPLEMENTED();
}

EXPORT void CC Game_ExitGame_450730()
{
    PSX_EMU_VideoDeAlloc_49A550();
}


EXPORT s32 CC CreateTimer_48F030(s32, void*)
{
    NOT_IMPLEMENTED();
    return 0;
}

EXPORT void CC Game_Shutdown_48E050()
{
    if (sGame_OnExitCallback_9F664C)
    {
        sGame_OnExitCallback_9F664C();
        sGame_OnExitCallback_9F664C = nullptr;
    }

    CreateTimer_48F030(0, nullptr); // Creates a timer that calls a call back which is always null, therefore seems like dead code?
    Input_DisableInput_48E690();
    //SND_MCI_Close_493C30(); // TODO: Seems like more dead code because the mci is never set?
    SND_SsQuit_4938E0();
    IO_Stop_ASync_IO_Thread_491A80();
    VGA_Shutdown_4900E0();
    Error_ShowErrorStackToUser_48DF10(true);
}


EXPORT void CC Game_Loop_437630()
{
    sBreakGameLoop_507B78 = 0;
    bool bPauseMenuObjectFound = false;
    while (!gBaseGameObject_list_9F2DF0->Empty())
    {
        GetGameAutoPlayer().SyncPoint(SyncPoints::MainLoopStart);

        Events_Reset_Active_417320();

        // Update objects
        GetGameAutoPlayer().SyncPoint(SyncPoints::ObjectsUpdateStart);
#ifdef TETHYS_SATURN
        {   // SATURN (ao242.6) S1: uT closes, uA opens
            const u32 tS1 = TETHYS_PT();
            Tethys_gUtRaw += tS1 - Tethys_gUStamp;
            Tethys_gUStamp = tS1;
        }
#endif
        for (s32 i = 0; i < gBaseGameObject_list_9F2DF0->Size(); i++)
        {
            BaseGameObject* pObjIter = gBaseGameObject_list_9F2DF0->ItemAt(i);
            if (!pObjIter)
            {
                break;
            }

            // SATURN (ao242.6): the per-iteration `Tethys_gVuList++` is GONE from
            // here. It was ~250 write-through stores a tick -- every one an
            // external bus cycle on this CPU -- INSIDE the very loop uA now
            // measures, i.e. a counter that had become a term in the number it
            // was about to be divided into. It is replaced by one add at each
            // loop's exit. The meaning shifts from "objects examined" to "list
            // length", which are identical unless the `break` on a null ItemAt
            // fires -- and DynamicArray's RemoveAt swap-with-last invariant
            // forbids a null before Size().
            if (pObjIter->field_6_flags.Get(BaseGameObject::eUpdatable_Bit2) && !pObjIter->field_6_flags.Get(BaseGameObject::eDead_Bit3) && (sNumCamSwappers_507668 == 0 || pObjIter->field_6_flags.Get(BaseGameObject::eUpdateDuringCamSwap_Bit10)))
            {
                if (pObjIter->field_8_update_delay > 0)
                {
                    pObjIter->field_8_update_delay--;
                }
                else
                {
                    if (pObjIter == pPauseMenu_5080E0)
                    {
                        bPauseMenuObjectFound = true;
                    }
                    else
                    {
                        // SATURN (bt1061): the bt1030/bt1031 per-object VUpdate
                        // bracket is REMOVED, verdict first. It answered in
                        // bt1040 -- the possession-screen spike was ONE object
                        // (OrbWhirlWind, via the vptr) and not a crowd -- and
                        // bt1041 then closed the follow-up by measuring u as
                        // 86-98% VUpdate on four captures. Its display was
                        // retired at bt1054; what stayed behind was the WORK:
                        // two FRT captures and a call for EVERY object, EVERY
                        // tick, feeding statics that nothing printed.
                        //   ~250 objects x 2 reads x 60 Hz of pure dead weight,
                        // and its .text is what pays this build's pre-flight
                        // floor. THIS IS THE HOUSE RULE APPLIED LATE: a gauge
                        // that has answered is deleted WITH its verdict, and
                        // deleting the row is not deleting the gauge.
                        Tethys_gVuCalls++; // SATURN: bt1125 population census
                        pObjIter->VUpdate();
                    }
                }
            }
        }

#ifdef TETHYS_SATURN
        {   // SATURN (ao242.6) S2: uA closes, uB opens
            const u32 tS2 = TETHYS_PT();
            Tethys_gUaRaw += tS2 - Tethys_gUStamp;
            Tethys_gUStamp = tS2;
            Tethys_gVuList += (u32) gBaseGameObject_list_9F2DF0->Size();
        }
#endif
        for (s32 i = 0; i < gLoadingFiles->Size(); i++)
        {
            BaseGameObject* pObjIter = gLoadingFiles->ItemAt(i);
            if (pObjIter->field_6_flags.Get(BaseGameObject::eUpdatable_Bit2) && !pObjIter->field_6_flags.Get(BaseGameObject::eDead_Bit3) && (sNumCamSwappers_507668 == 0 || pObjIter->field_6_flags.Get(BaseGameObject::eUpdateDuringCamSwap_Bit10)))
            {
                if (pObjIter->field_8_update_delay > 0)
                {
                    pObjIter->field_8_update_delay--;
                }
                else
                {
                    Tethys_gVuCalls++; // SATURN: bt1125 population census
                    pObjIter->VUpdate(); // bt1061: bracket removed, see above
                }
            }

            if (pObjIter->field_6_flags.Get(BaseGameObject::eDead_Bit3) && pObjIter->field_C_refCount == 0)
            {
                i = gLoadingFiles->RemoveAt(i);
                pObjIter->VDestructor(1);
            }
        }

#ifdef TETHYS_SATURN
        {   // SATURN (ao242.6) S3: uB closes. u - uT - uA - uB is the while
            // back-edge and must read <= 2 tenths of a ms.
            Tethys_gUbRaw += TETHYS_PT() - Tethys_gUStamp; // ao262.18: unguarded
            Tethys_gVuList += (u32) gLoadingFiles->Size();
        }
#endif
        GetGameAutoPlayer().SyncPoint(SyncPoints::ObjectsUpdateEnd);

#ifdef TETHYS_SATURN
        // SATURN (bt1012): PHASE TIMERS. src/sys_saturn.cxx split the tick into
        // po (outside PSX_VSync) and pi (inside), and src/renderer_saturn.cxx
        // then carved pw (the OT walk) out of po. The field answer, marginal
        // between two captures of one elevator visit: pw 8.2 ms a tick and FLAT,
        // pi 8.9 and flat, but po - pw = 54.7 ms against a 33 ms budget. So the
        // cost is in this loop body, and the six mechanisms measured before it
        // (accumulation, sr's fill, the palette hash, VDP1 fill, the submission
        // path, the SCSP key-on wait) are all out. The W row rules out volume
        // too: OT prims 354 -> 316 and upload KB 8 -> 7 while the cost rose.
        //
        // Three phases run between the VSync that just returned and the DrawOTag
        // below, and they have different owners:
        //   pu = the two VUpdate loops above -- game AI and physics, pure RELIVE
        //   pa = AnimateAll -- animation stepping, where cel UPLOADS happen
        //   pv = the VRender loop below -- prim building, our seam per drawable
        // pu + pa + pv should account for nearly all of po - pw; whatever is
        // missing is the destruction loop and ScreenChange further down.
        Tethys_gPhUpd += SYS_GetTicks() - tPhase0;
        tPhase0 = SYS_GetTicks();
        Tethys_gCurCam = static_cast<u32>(gMap_507BA8.field_4_current_camera);
        // SATURN (bt1016): Abe's WORLD position, for the overlay's ax/ay.
        // The boot-spawn hook needs real coordinates and I am not inventing
        // them again -- bt1014 died of exactly that. LoadFromMemory_459970
        // hands Motion_62_LoadedSaveSpawn a saved x/y, which raycasts +-60 for
        // a collision line and then calls MapFollowMe_401D30(TRUE); that last
        // call is the piece bt1014's SetActiveCam path did not have, and its
        // absence is why the map chased an out-of-bounds Abe screen by screen.
        if (sActiveHero_507678)
        {
            Tethys_gAbeX = static_cast<u32>(FP_GetExponent(sActiveHero_507678->field_A8_xpos));
            Tethys_gAbeY = static_cast<u32>(FP_GetExponent(sActiveHero_507678->field_AC_ypos));
        }
        // SATURN (ao261.21): bt816's death-respawn seed, RELOCATED FROM
        // Game_Run_4373D0. With the menu boot there is no Abe at Init time and
        // SaveToMemory_459490 dereferences sActiveHero_507678 unguarded
        // (SaveGame.cpp:370 onward), so seeding at boot is a null read on SH-2.
        //   The seed itself is still needed for exactly bt816's reason:
        // gSaveBuffer_505668 stays zeroed until the first ContinuePoint TLV
        // fires (Abe.cpp:3240), and a death inside that window would
        // LoadFromMemory a (eMenu_0, path 0, cam 0) buffer -- and
        // Path_Get_Bly_Record(eMenu_0, 0) is kNullPathBlyRec, so
        // field_D4_pPathData is null and Map.cpp:2196 dereferences it.
        //   So seed once, on the first tick where a real level is live AND Abe
        // exists. -fno-threadsafe-statics is on, so the function-local static is
        // a plain word with no guard variable.
        {
            static s16 sTethysSaveSeeded = 0;
            if (!sTethysSaveSeeded && sActiveHero_507678
                && gMap_507BA8.field_0_current_level != LevelIds::eMenu_0)
            {
                sTethysSaveSeeded = 1;
                SaveGame::Tethys_SeedSaveBuffer();
            }
        }
#if defined(TETHYS_START_CAM) && TETHYS_START_CAM > 0
        // SATURN (bt1017): DEBUG BOOT SPAWN, through the game's OWN respawn.
        // Not a camera jump -- bt1014 tried that and the map chased an
        // out-of-bounds Abe screen by screen, because SetActiveCam moves the
        // camera and nothing else. This seeds gSaveBuffer_505668 (already
        // populated at boot by Tethys_SeedSaveBuffer, so every other field is
        // valid) and runs the same LoadFromMemory_459970 that death runs. That
        // path sets Motion_62_LoadedSaveSpawn, which places Abe at the saved
        // x/y, RAYCASTS +-60 to bind a collision line, and calls
        // MapFollowMe_401D30(TRUE) -- the piece bt1014 lacked. If the raycast
        // misses, Abe falls and the map still follows him.
        //
        // The coordinates are MEASURED, not derived: 15/4 at (6434, 213) read
        // off Abe standing on the possession screen via the bt1016 ax/ay
        // readout. Deriving them from the camera-cell geometry is exactly what
        // broke bt1014. Set TETHYS_START_CAM=0 in the Makefile to disable.
        if (!sStartSpawnDone && ++sStartSpawnLaps > 120 && sActiveHero_507678
            && gMap_507BA8.field_0_current_level == LevelIds::eRuptureFarms_1)
        {
            sStartSpawnDone = 1;
            gSaveBuffer_505668.field_234_current_level = LevelIds::eRuptureFarms_1;
            gSaveBuffer_505668.field_236_current_path = TETHYS_START_PATH;
            gSaveBuffer_505668.field_238_current_camera = TETHYS_START_CAM;
            gSaveBuffer_505668.field_224_xpos = TETHYS_START_X;
            gSaveBuffer_505668.field_228_ypos = TETHYS_START_Y;
            SaveGame::LoadFromMemory_459970(&gSaveBuffer_505668, 1);
        }
#endif
#endif

        // Animate everything
        // SATURN (ao242.6) sTethysInAnimate -- THE PARENT FLAG, and without it the
        // frame hierarchy cannot sum. Upload (and through it the texel copy and the
        // LZSS) has TWO parents: AnimateAll here, and VUpdate via
        // Set_Animation_Data_402A40 / Init_402D20. Every raw tick either of them
        // spends has until now been added to one accumulator, so `a` was credited
        // with work that happened inside `u` and the a-subtree could never close.
        // One byte, set and cleared TWICE PER TICK -- never per call, never in a
        // loop -- routes each sample to its real parent.
        Tethys_gInAnimate = 1;
        const u32 tAnimRaw0 = TETHYS_PT(); // SATURN: bt1134 (ao242.13: gated)
        if (sNumCamSwappers_507668 <= 0)
        {
            GetGameAutoPlayer().SyncPoint(SyncPoints::AnimateAll);
            AnimationBase::AnimateAll_4034F0(gObjList_animations_505564);
        }

#ifdef TETHYS_SATURN
        Tethys_gInAnimate = 0; // SATURN (ao242.6): back under `u`'s parentage
        Tethys_gPhAnim += SYS_GetTicks() - tPhase0;
        Tethys_gPhAnimRaw += TETHYS_PT() - tAnimRaw0; // SATURN: bt1134 (unguarded)
        tPhase0 = SYS_GetTicks();
#endif

        // Render objects
        PrimHeader** ppOt = gPsxDisplay_504C78.field_C_drawEnv[gPsxDisplay_504C78.field_A_buffer_index].field_70_ot_buffer;

        GetGameAutoPlayer().SyncPoint(SyncPoints::DrawAllStart);
        // SATURN (ao242.6): `i` is hoisted so the walk can be counted ONCE at the
        // loop exit. This list is the only one in the render phase that has never
        // had a trip counter, and `v` cannot be divided without one.
        s32 iDraw = 0;
        for (iDraw = 0; iDraw < gObjList_drawables_504618->Size(); iDraw++)
        {
            const s32 i = iDraw;
            BaseGameObject* pDrawable = gObjList_drawables_504618->ItemAt(i);
            if (!pDrawable)
            {
                break;
            }

            if (pDrawable->field_6_flags.Get(BaseGameObject::eDead_Bit3))
            {
                pDrawable->field_6_flags.Clear(BaseGameObject::eCantKill_Bit11);
            }
            else if (pDrawable->field_6_flags.Get(BaseGameObject::eDrawable_Bit4))
            {
                pDrawable->field_6_flags.Set(BaseGameObject::eCantKill_Bit11);
#ifdef TETHYS_SATURN
                // SATURN (ao242.13): the `d` probe -- see the head of the file.
                // The whole bracket, table lookup included, is behind the gate,
                // so with the overlay off this is one predicted-not-taken branch.
                if (Tethys_gProbeOn)
                {
                    const u32 tD0 = Tethys_RawTicks();
                    pDrawable->VRender(ppOt);
                    // & 127 never collides: AO Types run 0..103 (eElectrocute_103
                    // is the last), so the mask is a bound, not a hash.
                    // ao262.18: under the tear guard. This is the bucket
                    // that printed T9999 on hardware for twenty seconds -- see
                    // the TETHYS_PD banner at the head of the file. The table is
                    // the ONLY accumulator here with no per-screen base, so a
                    // poisoned sample owns it until the next flip, and the
                    // poisoned bucket then wins the argmax and hides the real
                    // answer (D057 eMine_57 displaced D073 eRope_73 on c8).
                    // quarter raw ticks -- see the declaration in sys_saturn.cxx
                    Tethys_gDrawByType[TethysDrawBucket(pDrawable->field_4_typeId)]
                        += static_cast<u16>(TETHYS_PD(Tethys_RawTicks(), tD0) >> 2);
                }
                else
                {
                    pDrawable->VRender(ppOt);
                }
#else
                pDrawable->VRender(ppOt);
#endif
            }
        }
        GetGameAutoPlayer().SyncPoint(SyncPoints::DrawAllEnd);
#ifdef TETHYS_SATURN
        Tethys_gDrawWalk += (u32) iDraw; // SATURN (ao242.6): one add, at the exit
#endif

        DebugFont_Flush_487F50();
        PSX_DrawSync_496750(0);
#ifdef TETHYS_SATURN
        {   // SATURN (ao242.6) 'vs': ScreenManager::VRender loops a FIXED 300
            // times regardless of what the screen actually holds, so it is a
            // constant floor under every render phase and it has never been
            // priced. A CALL boundary, one execution a tick.
            const u32 tScr0 = TETHYS_PT();
            pScreenManager_4FF7C8->VRender(ppOt);
            Tethys_gScrRaw += TETHYS_PT() - tScr0; // ao262.18: unguarded
        }
#else
        pScreenManager_4FF7C8->VRender(ppOt);
#endif
        SYS_EventsPump_44FF90();

#ifdef TETHYS_SATURN
        Tethys_gPhRend += SYS_GetTicks() - tPhase0; // SATURN (bt1012)
#endif

        GetGameAutoPlayer().SyncPoint(SyncPoints::RenderOT);
        gPsxDisplay_504C78.PSX_Display_Render_OT_40DD20();

#ifdef TETHYS_SATURN
        // Re-armed AFTER the OT render, so the next lap's pu measures only the
        // update loops and never the walk (pw already owns that).
        tPhase0 = SYS_GetTicks(); // SATURN (bt1012)
        // SATURN (ao242.6) S0 -- `u` starts HERE, one lap early, because the
        // accumulator is armed after the OT render and closes after the update
        // loops. Three more stamps partition it into the tail (destroy loop,
        // pause, ScreenChange, Input), loop A (the VUpdate list) and loop B
        // (gLoadingFiles -- the synchronous-CD suspect). Stamps, not nested
        // brackets: four RawTicks reads a tick, and every span is a difference of
        // two of them, so no span can double-count another.
        Tethys_gUStamp = TETHYS_PT();
#endif

        GetGameAutoPlayer().SyncPoint(SyncPoints::RenderStart);

        // Destroy objects with certain flags
        for (s32 i = 0; i < gBaseGameObject_list_9F2DF0->Size(); i++)
        {
            BaseGameObject* pObj = gBaseGameObject_list_9F2DF0->ItemAt(i);
            if (!pObj)
            {
                break;
            }

            if (pObj->field_6_flags.Get(BaseGameObject::eDead_Bit3) && pObj->field_C_refCount == 0)
            {
                i = gBaseGameObject_list_9F2DF0->RemoveAt(i);
                pObj->VDestructor(1);
            }
        }

        GetGameAutoPlayer().SyncPoint(SyncPoints::RenderEnd);

        if (bPauseMenuObjectFound && pPauseMenu_5080E0)
        {
            pPauseMenu_5080E0->VUpdate();
        }

        bPauseMenuObjectFound = false;

        gMap_507BA8.ScreenChange_4444D0();
        Input().Update(GetGameAutoPlayer());

        if (sNumCamSwappers_507668 == 0)
        {
            GetGameAutoPlayer().SyncPoint(SyncPoints::IncrementFrame);
            gnFrameCount_507670++;
        }

        if (sBreakGameLoop_507B78)
        {
            GetGameAutoPlayer().SyncPoint(SyncPoints::MainLoopExit);
            break;
        }

        GetGameAutoPlayer().ValidateObjectStates();

    } // Main loop end

    const PSX_RECT rect = {0, 0, 368, 480};
    PSX_ClearImage_496020(&rect, 0, 0, 0);
    PSX_DrawSync_496750(0);
    PSX_VSync_496620(0);
    ResourceManager::WaitForPendingResources_41EA60(0);

    for (s32 i = 0; i < gBaseGameObject_list_9F2DF0->Size(); i++)
    {
        BaseGameObject* pObjToKill = gBaseGameObject_list_9F2DF0->ItemAt(i);
        if (!pObjToKill)
        {
            break;
        }

        if (pObjToKill->field_C_refCount == 0)
        {
            i = gBaseGameObject_list_9F2DF0->RemoveAt(i);
            pObjToKill->VDestructor(1);
        }
    }
}

EXPORT void CC DDCheat_Allocate_409560()
{
    auto pDDCheat = ao_new<DDCheat>();
    if (pDDCheat)
    {
        pDDCheat->ctor_4095D0();
    }
}

EXPORT void Game_Run_4373D0()
{
    SYS_EventsPump_44FF90();

    gAttract_507698 = 0;
    gTimeOut_NotUsed_507B0C = 6000;
    gFileOffset_NotUsed_507B10 = 34;

    DDCheat::DebugStr_495990("Abe's Oddysee Attract=%d Timeout=%d FileOffset=%d DA Track=NA\n", 0, 200, 34);
    SYS_EventsPump_44FF90();
    PSX_ResetCallBack_49AFB0();

    //Nop_49BAF0();
    //Nop_49BB50();

    gPsxDisplay_504C78.ctor_40DAB0(&gPsxDisplayParams_4BB830);
    Input().InitPad_4331A0(1);

    gBaseGameObject_list_9F2DF0 = ao_new<DynamicArrayT<BaseGameObject>>();
    gBaseGameObject_list_9F2DF0->ctor_4043E0(90);

    gObjList_drawables_504618 = ao_new<DynamicArrayT<BaseGameObject>>();
    gObjList_drawables_504618->ctor_4043E0(80);

    gObjList_animations_505564 = ao_new<DynamicArrayT<AnimationBase>>();
    gObjList_animations_505564->ctor_4043E0(80);

    // NOTE: We need to call Input_Init() before Init_Sound_DynamicArrays_And_Others() because of gLatencyHack
    // which can be configured from the ini
    Input_Init_44EB60();

    Init_Sound_DynamicArrays_And_Others_41CD20();

#ifdef TETHYS_SATURN
    // SATURN: no Pxtd extension chunks on the converted disc, and the retail
    // implementation opens all 16 LVLs at boot (dozens of seconds on a real
    // CD drive). The compiled-in PathData tables are the ground truth here.
    // SATURN (ao261.21): BOOT TO THE MAIN MENU, exactly as the retail non-Saturn
    // path below does. The old jump-start into R1P15C01 was design decision D10
    // -- a P3 SCOPE choice ("one path of one level", FMVs stubbed), never a
    // capability limit -- and every premise behind it has since been paid for:
    //   * S1.LVL is converted and shipped (build.ps1 $lvlNames) -- all 15 menu
    //     cameras, STARTANM.BND, ABEINTRO/DOOR/HIGHLITE/ABESPEAK, OPTSNDFX,
    //     S1SEQ.BSQ, S1PATH.BND. Verified in the delivered cd/data/S1.LVL.
    //   * S1P01C10 carries the MenuController_90 TLV, and the menu cameras carry
    //     the EMBEDDED Font id 1 the menu needs (it is not a standalone file --
    //     tools/converter/cam.py converts that chunk, and the CAM streamer
    //     fabricates a heap resource from the on-disk type/id at
    //     ResourceManager.cpp:2139, which happens inside Load_Path_Items
    //     (Map.cpp:1675) two drains BEFORE the Menu is constructed at
    //     Map.cpp:2215). So the menu font resolves itself on this path.
    //   * FMV screen changes are inert, not fatal: movie_stub.cxx holds
    //     sMovie_ref_count_9F309C at 0, so CameraSwapper's ePlay1FMV_5 case
    //     finishes on its first tick and its ppCamRes guard tolerates our null
    //     Camera::field_C_ppBits.
    //
    // ABE AND THE PAUSEMENU ARE DELIBERATELY *NOT* BUILT HERE any more.
    // Menu::NewGameStart_47B9C0 (MainMenu.cpp:2115-2140) creates both when the
    // player picks "Begin", then does SetActiveCam(eRuptureFarms_1, 15, 1).
    // Building them at boot would strand an Abe in a level the map has never
    // opened AND turn the menu's own `if (!sActiveHero_507678)` into a no-op.
    //
    // AND Tethys_SeedSaveBuffer IS NOT CALLED HERE: SaveToMemory_459490
    // dereferences sActiveHero_507678 with no guard (SaveGame.cpp:370 onward)
    // and there is no Abe at menu time -- on SH-2 that is a read through a null
    // pointer. bt816's seed moved into Game_Loop_437630; see the block there.
    gMap_507BA8.Init_443EE0(LevelIds::eMenu_0, 1, CameraIds::Menu::eCopyright_10, CameraSwapEffects::eInstantChange_0, 0, 0);
#else
    Path_Set_NewData_FromLvls();

#if DEVELOPER_MODE
    // Boot directly to the "abe hello" screen
    gMap_507BA8.Init_443EE0(LevelIds::eMenu_0, 1, 1, CameraSwapEffects::eInstantChange_0, 0, 0);
#else
    // Normal copy right screen boot
    gMap_507BA8.Init_443EE0(LevelIds::eMenu_0, 1, 10, CameraSwapEffects::eInstantChange_0, 0, 0);
#endif
#endif

    DDCheat_Allocate_409560();

    pEventSystem_4FF954 = ao_new<GameSpeak>();
    pEventSystem_4FF954->ctor_40F990();

    pCheatController_4FF958 = ao_new<CheatController>();
    pCheatController_4FF958->ctor_40FBF0();

    Game_Init_LoadingIcon_445E30();
    Game_Loop_437630();
    Game_Free_LoadingIcon_445E80();

    DDCheat::ClearProperties_4095B0();

    gMap_507BA8.Shutdown_443F90();

    if (gObjList_animations_505564)
    {
        gObjList_animations_505564->dtor_404440();
        ao_delete_free_447540(gObjList_animations_505564);
    }

    if (gObjList_drawables_504618)
    {
        gObjList_drawables_504618->dtor_404440();
        ao_delete_free_447540(gObjList_drawables_504618);
    }

    if (gBaseGameObject_list_9F2DF0)
    {
        gBaseGameObject_list_9F2DF0->dtor_404440();
        ao_delete_free_447540(gBaseGameObject_list_9F2DF0);
    }

    MusicController::Shutdown_4437E0();
    SND_Reset_Ambiance_4765E0();
    SND_Shutdown_476EC0();
    PSX_CdControlB_49BB40(8, 0, 0);
    PSX_ResetCallBack_49AFB0();
    PSX_StopCallBack_49AFC0();
    InputObject::Shutdown_433230();
    PSX_ResetGraph_4987E0(3);

    DDCheat::DebugStr_495990("Abe's Oddysee Demo Done\n");
}


EXPORT void Game_Main_450050()
{
    BaseAliveGameObject_ForceLink();

    GetGameAutoPlayer().ParseCommandLine(Sys_GetCommandLine_48E920());

    Main_ParseCommandLineArguments();
    Game_SetExitCallBack_48E040(Game_ExitGame_450730);
#ifdef _WIN32
    // Only SDL2 supported in AO
    Sys_SetWindowProc_Filter_48E950(Sys_WindowMessageHandler_4503B0);
#endif
    Game_Run_4373D0();

    // TODO: AE inlined calls here (pull AE's code into another func)
    Game_Shutdown_48E050();
}

} // namespace AO
