#include "stdafx.h"
#include "Game.hpp"
#include "../relive_lib/Sys.hpp"
#include "VGA.hpp"
#include "Input.hpp"
#include "../relive_lib/Psx.hpp"
#include "../relive_lib/DynamicArray.hpp"
#include "../relive_lib/Sound/Sound.hpp" // for shut down func
#include "AmbientSound.hpp"
#include "../relive_lib/PsxDisplay.hpp"
#include "Map.hpp"
#include "../relive_lib/GameObjects/ScreenManager.hpp"
#include "stdlib.hpp"
#include "PauseMenu.hpp"
#include "GameSpeak.hpp"
#include "DDCheat.hpp"
#include "Io.hpp"
#include "../relive_lib/Sound/Midi.hpp"
#include <fstream>
#include "../relive_lib/Events.hpp"
#include "Abe.hpp"
#include "MusicController.hpp"
#include "../relive_lib/GameObjects/CheatController.hpp"
#include "Slurg.hpp"
#include "PathDataExtensions.hpp"
#include "GameAutoPlayer.hpp"
#include "../relive_lib/Function.hpp"
#include "../relive_lib/GameObjects/ShadowZone.hpp"
#include "../relive_lib/ResourceManagerWrapper.hpp"
#include "GameEnderController.hpp"
#include "ColourfulMeter.hpp"
#include "../relive_lib/GameObjects/GasCountDown.hpp"
#include "../relive_lib/SwitchStates.hpp"
#include "../relive_lib/Collisions.hpp"
#include "../relive_lib/GameObjects/PlatformBase.hpp"

u32 sGnFrame = 0;

bool gBreakGameLoop = false;
s16 gNumCamSwappers = 0;
bool gSkipGameObjectUpdates = false;

bool sCommandLine_ShowFps = false;
bool gDDCheatOn = false;

// Fps calcs
f64 sFps_55EFDC = 0.0;
s32 sFrameDiff_5CA4DC = 0;
s32 sFrameCount_5CA300 = 0;

u16 gAttract = 0;

// QuickSave load/Restart path calls this
void DestroyObjects()
{
    ResourceManagerWrapper::LoadingLoop(false);
    for (s32 iterations = 0; iterations < 2; iterations++)
    {
        for (s32 idx = 0;idx < gBaseGameObjects->Size(); idx++)
        {
            BaseGameObject* pObj = gBaseGameObjects->ItemAt(idx);
            if (!pObj)
            {
                break;
            }

            if (!pObj->GetSurviveDeathReset())
            {
                idx = gBaseGameObjects->RemoveAt(idx);

                delete pObj;
            }
        }
    }
}

static f64 Calculate_FPS_495250(s32 frameCount)
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

static void DrawFps_4952F0(f32 fps)
{
    char_type strBuffer[125] = {};
    snprintf(strBuffer, sizeof(strBuffer), "%02.1f fps ", static_cast<f64>(fps));
    gPsxDisplay.mDebugFont.DebugFont_Printf(0, strBuffer);
}


s32 Game_End_Frame(u32 flags)
{
    if (flags & 1)
    {
        gTurnOffRendering = false;
        return 0;
    }

    const f64 fps = Calculate_FPS_495250(sFrameCount_5CA300);
    if (sCommandLine_ShowFps)
    {
        DrawFps_4952F0(static_cast<f32>(fps));
    }

    ++sFrameCount_5CA300;

    if (Sys_PumpMessages())
    {
        exit(0);
    }
    return 0;
}

void SYS_EventsPump()
{
    if (Sys_PumpMessages())
    {
        exit(0);
    }
}

// SATURN: SYS_GetTicks and Alive_Show_ErrorMsg are OS-seam functions that
// happen to be DEFINED here with SDL.  We leave them undefined on Saturn on
// purpose: src/ provides them (SRL frame counter, and the SH-2 exception death
// screen), and an unresolved symbol is a seam we can see rather than a stub we
// forget.
#ifndef TETHYS_SATURN
u32 SYS_GetTicks()
{
    // Using this instead of SDL_GetTicks resolves a weird x64 issue on windows where
    // the tick returned is a lot faster on some machines.
    return static_cast<u32>(SDL_GetPerformanceCounter() / (SDL_GetPerformanceFrequency() / 1000));
}
#endif

void Alive_Show_ErrorMsg(const char_type* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    char_type buf[2048] = {};
    vsnprintf(buf, sizeof(buf) - 1, fmt, args);
    va_end(args);

#ifndef TETHYS_SATURN
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, ("R.E.L.I.V.E. " + BuildString()).c_str(), buf, nullptr);
#else
    ALIVE_FATAL("%s", buf);   // SATURN: no message box; the death screen is it.
#endif
}


void Init_GameStates()
{
    gKilledMudokons = gFeeco_Restart_KilledMudCount; // DDCheat
    gRescuedMudokons = gFeecoRestart_SavedMudCount;

    gDeathGasOn = false; // GasCountDown
    gDeathGasTimer = 0;

    gbDrawMeterCountDown = false; // ColourfulMeter
    gTotalMeterBars = 0;

    gAbeInvincible = false; // Abe

    SwitchStates_ClearRange(0, 255);
}

static void Init_Sound_DynamicArrays_And_Others()
{
    gPauseMenu = nullptr; // PauseMenu
    gAbe = nullptr;
    sControlledCharacter = nullptr;
    gNumCamSwappers = 0; // TODO: Move
    sGnFrame = 0;

    PlatformBase::MakeArray();
    ShadowZone::MakeArray();

    gBaseAliveGameObjects = relive_new DynamicArrayT<BaseAliveGameObject>(20);

    SND_Init();
    SND_Init_Ambiance();
    MusicController::Create();

    Init_GameStates(); // Init other vars + switch states
}

static void Game_Init_LoadingIcon()
{
    /*
    u8** ppRes = ResourceManager::GetLoadedResource(ResourceManager::Resource_Animation, AEResourceID::kLoadingResID, 1u, 0);
    if (!ppRes)
    {
        ResourceManager::LoadResourceFile_49C170("LOADING.BAN", nullptr);
        ppRes = ResourceManager::GetLoadedResource(ResourceManager::Resource_Animation, AEResourceID::kLoadingResID, 1u, 0);
    }
    ResourceManager::Set_Header_Flags_49C650(ppRes, ResourceManager::ResourceHeaderFlags::eNeverFree);
    */
}

static void Game_Free_LoadingIcon()
{
    //gLoadingResource.Clear();
    /*
    u8** ppRes = ResourceManager::GetLoadedResource(ResourceManager::Resource_Animation, AEResourceID::kLoadingResID, 0, 0);
    if (ppRes)
    {
        ResourceManager::FreeResource_49C330(ppRes);
    }*/
}

void Game_Shutdown()
{
    Input_DisableInputForPauseMenuAndDebug_4EDDC0();
    GetSoundAPI().mSND_SsQuit();
    IO_Stop_ASync_IO_Thread_4F26B0();
    VGA_Shutdown();
}


void Game_Loop()
{
    gBreakGameLoop = false;
    bool bPauseMenuObjectFound = false;
    while (!gBaseGameObjects->IsEmpty())
    {
        GetGameAutoPlayer().SyncPoint(SyncPoints::MainLoopStart);

        EventsResetActive();
        Slurg::Clear_Slurg_Step_Watch_Points();
        gSkipGameObjectUpdates = false;

        // Update objects
        GetGameAutoPlayer().SyncPoint(SyncPoints::ObjectsUpdateStart);
        for (s32 baseObjIdx = 0; baseObjIdx < gBaseGameObjects->Size(); baseObjIdx++)
        {
            BaseGameObject* pBaseGameObject = gBaseGameObjects->ItemAt(baseObjIdx);

            if (!pBaseGameObject || gSkipGameObjectUpdates)
            {
                break;
            }

            if (pBaseGameObject->GetUpdatable()
			    && !pBaseGameObject->GetDead() 
                && (gNumCamSwappers == 0 || pBaseGameObject->GetUpdateDuringCamSwap()))
            {
                const s32 updateDelay = pBaseGameObject->UpdateDelay();
                if (updateDelay <= 0)
                {
                    if (pBaseGameObject == gPauseMenu)
                    {
                        bPauseMenuObjectFound = true;
                    }
                    else
                    {
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

        // Animate everything
        if (gNumCamSwappers <= 0)
        {
            GetGameAutoPlayer().SyncPoint(SyncPoints::AnimateAll);
            AnimationBase::AnimateAll(AnimationBase::gAnimations);
        }

        // Render objects
        GetGameAutoPlayer().SyncPoint(SyncPoints::DrawAllStart);
        for (s32 i = 0; i < gObjListDrawables->Size(); i++)
        {
            BaseGameObject* pDrawable = gObjListDrawables->ItemAt(i);
            if (!pDrawable)
            {
                break;
            }

            if (pDrawable->GetDead())
            {
                pDrawable->SetCantKill(false);
            }
            else if (pDrawable->GetDrawable())
            {
                pDrawable->SetCantKill(true);
                pDrawable->VRender(gPsxDisplay.mDrawEnv.mOrderingTable);
            }
        }
        GetGameAutoPlayer().SyncPoint(SyncPoints::DrawAllEnd);

        gPsxDisplay.mDebugFont.DebugFont_Flush();
        gScreenManager->VRender(gPsxDisplay.mDrawEnv.mOrderingTable);
        SYS_EventsPump(); // Exit checking?

        GetGameAutoPlayer().SyncPoint(SyncPoints::RenderOT);
        gPsxDisplay.RenderOrderingTable();
        
        GetGameAutoPlayer().SyncPoint(SyncPoints::RenderStart);

        // Destroy objects with certain flags
        for (s32 idx = 0; idx < gBaseGameObjects->Size(); idx++)
        {
            BaseGameObject* pObj = gBaseGameObjects->ItemAt(idx);
            if (!pObj)
            {
                break;
            }

            if (pObj->GetDead() && !pObj->GetCantKill() && pObj->mChaseCounter == 0)
            {
                idx = gBaseGameObjects->RemoveAt(idx);
                relive_delete pObj;
            }
        }

        GetGameAutoPlayer().SyncPoint(SyncPoints::RenderEnd);

        if (bPauseMenuObjectFound && gPauseMenu)
        {
            gPauseMenu->VUpdate();
        }

        bPauseMenuObjectFound = false;

        gMap.ScreenChange();
        Input().Update(GetGameAutoPlayer());

        if (gNumCamSwappers == 0)
        {
            GetGameAutoPlayer().SyncPoint(SyncPoints::IncrementFrame);
            sGnFrame++;
        }

        if (gBreakGameLoop)
        {
            GetGameAutoPlayer().SyncPoint(SyncPoints::MainLoopExit);
            break;
        }

        GetGameAutoPlayer().ValidateObjectStates();

    } // Main loop end

    PSX_VSync(VSyncMode::UncappedFps);

    // Destroy all game objects
    for (s32 i = 0; i < gBaseGameObjects->Size(); i++)
    {
        BaseGameObject* pObjToKill = gBaseGameObjects->ItemAt(i);
        if (!pObjToKill)
        {
            break;
        }

        if (pObjToKill->GetDead())
        {
            i = gBaseGameObjects->RemoveAt(i);
            relive_delete pObjToKill;
        }
    }
}

void DDCheat_Allocate()
{
    relive_new DDCheat();
}

// SATURN: Game_Run's start-up is ten calls in a row, and a hang in any of them
// presents identically -- a frozen picture between "7 engine built" and the
// first frame.  AE_MARK names each step on the debug overlay (the same trace
// src_ae/hw/main_ae.cxx writes for the boot chain), so the LAST row on screen
// is the step that completed and the missing one is the step that hung.
#ifdef TETHYS_SATURN
extern "C" void Tethys_AE_Mark(const char* stage);
extern "C" void Tethys_AE_InstallInput(); // src_ae/hw/sys_ae.cxx
extern "C" void Tethys_AE_ClearBootTrace(); // src_ae/hw/main_ae.cxx
    #define AE_MARK(s) Tethys_AE_Mark(s)
#else
    #define AE_MARK(s)
#endif

void Game_Run()
{
    // Begin start up
    SYS_EventsPump();

    gAttract = 0;

    SYS_EventsPump();

    gPsxDisplay.Init();
    AE_MARK("8 psxdisplay");
    Input_Pads_Reset_4FA960(); // starts card/pads on psx ver
    Input_EnableInput_4EDDD0();
    AE_MARK("9 input pads");

    gBaseGameObjects = relive_new DynamicArrayT<BaseGameObject>(90);

    BaseAnimatedWithPhysicsGameObject::MakeArray(); // Makes drawables

    AnimationBase::CreateAnimationArray();
    AE_MARK("10 object arrays");

    Input_Init();
#ifdef TETHYS_SATURN
    // SATURN: Input_Init just installed the PC keyboard/DirectInput
    // converter (Input.cpp:1540).  Swap it for the SMPC pad reader --
    // AFTER, not before, or the engine overwrites us with a callback
    // that reads hardware this machine does not have.
    Tethys_AE_InstallInput();
#endif
    AE_MARK("11 input init");
    Init_Sound_DynamicArrays_And_Others();
    AE_MARK("12 sound arrays");
    
#ifdef TETHYS_SATURN
    // SATURN: boot straight into the Mines, not the menu.
    //
    // eMenu is level 2000 and cd/data_ae/TETHYS.PAK holds level 2001 only --
    // 198 Mines cameras across 12 paths, and nothing else.  Asking for a screen
    // that was never packed hung Game_Run here with no message: the pack lookup
    // returns null cleanly (cd_ae.cxx Find), and the engine has no answer for a
    // camera that does not exist.  Path 1 camera 1 IS in the pack.
    //
    // The same shape as the Oddysee port, which boots directly to R1P15C01
    // rather than through a menu it cannot yet draw.  Revisit when the menu
    // level is packed and the LCD/font seam (AE-8) exists to draw it.
    //
    // CAMERA 4, not 1, and the choice is evidence rather than taste: MIP01C04
    // is the only cell on path 1 carrying AbeStart_22 (plus ContinuePoint_0).
    // Abe is spawned BY that TLV, so on any other screen of this path a correct
    // engine draws no Abe at all -- and "no Abe" would then be indistinguishable
    // from a broken path loader.  Booting where the spawn lives makes the test
    // able to fail honestly.
    gMap.Init(EReliveLevelIds::eMines, 1, 4, CameraSwapEffects::eInstantChange_0, 0, 0);

    // SATURN: BOOT WARP -- spawn where the spawn is, then step to where the test is.
    //
    // The tester needs the screen carrying path 1's exit and has never reached
    // it; navigating there by hand every boot is not a test, it is a chore.  But
    // the camera cannot simply be changed above: MIP01C04 is the ONLY cell on
    // path 1 with AbeStart_22, Abe is created BY that TLV, and booting anywhere
    // else gives a screen with no hero at all.  So Init still lands on the
    // spawn -- and then this moves him, which is what the game's own cheat does
    // (DDCheat::Teleport, DDCheat.cpp:125).
    //
    // THE DESTINATION IS READ OUT OF THE PATH DATA, NOT EYEBALLED.  MIP01C22
    // (cell 92) holds the only Door_5 on path 1 that leaves it -- the doors on
    // C17/C18/C21 all point back at their own camera -- and it goes to path 7
    // camera 11.  Its rect is TL(850,1740) BR(875,1760), so x is its centre.
    //
    // y comes from the COLLISION, which is the part that decides whether this
    // works or kills him.  Exactly one line crosses x=862 anywhere near the
    // door: (752,1758)-(1127,1758).  Standing on it is standing in the doorway,
    // so there is no fall, and none of the flying-cheat business the Oddysee
    // warp needed to survive arriving at stale coordinates.
    //
    // Collision is per PATH, not per camera, so the single frame Abe spends at
    // these coordinates before ScreenChange runs is already standing on that
    // line.  Nothing to sequence.
    //
    // Set kBootWarpCam to 0 to boot on the spawn as before.
    {
        constexpr s16 kBootWarpPath = 1;
        constexpr s16 kBootWarpCam = 22;
        constexpr s32 kBootWarpX = 862;
        constexpr s32 kBootWarpY = 1758;
        if (kBootWarpCam != 0 && gAbe)
        {
            gAbe->mXPos = FP_FromInteger(kBootWarpX);
            gAbe->mYPos = FP_FromInteger(kBootWarpY);
            gMap.SetActiveCam(EReliveLevelIds::eMines, kBootWarpPath, kBootWarpCam,
                              CameraSwapEffects::eInstantChange_0, 0, 1);
        }
    }
#else
    gMap.Init(EReliveLevelIds::eMenu, 1, 25, CameraSwapEffects::eInstantChange_0, 0, 0);
#endif
    AE_MARK("13 map init");

    DDCheat_Allocate();
    AE_MARK("14 ddcheat");

    gEventSystem = relive_new GameSpeak();

    gCheatController = relive_new CheatController();
    AE_MARK("15 gamespeak");

    Game_Init_LoadingIcon();
    AE_MARK("16 loading icon");

    // Main loop start
    AE_MARK("17 game loop");
#ifdef TETHYS_SATURN
    // SATURN: the boot trace has done its job -- from here it is 25 rows of
    // text over the picture the tester is trying to judge.
    Tethys_AE_ClearBootTrace();
#endif
    Game_Loop();

    // Shut down start
    Game_Free_LoadingIcon();

    DDCheat::ClearProperties();

    gMap.Shutdown();

    AnimationBase::FreeAnimationArray();
    BaseAnimatedWithPhysicsGameObject::FreeArray();
    relive_delete gBaseGameObjects;
    PlatformBase::FreeArray();
    ShadowZone::FreeArray();
    relive_delete gBaseAliveGameObjects;
    relive_delete gCollisions;

    MusicController::Shutdown();

    SND_Reset_Ambiance();
    SND_Shutdown();
    Input().ShutDown_45F020();
}

void Game_Main()
{
    // Only returns once the engine is shutting down
    Game_Run();

    Game_Shutdown();
}
