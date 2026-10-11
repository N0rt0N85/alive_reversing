#include "stdafx_ao.h"
#include "CheatController.hpp"
#include "Function.hpp"
#include "stdlib.hpp"
#include "Map.hpp"
#include "Input.hpp"

#ifdef TETHYS_SATURN
// SATURN (446.ao.3): src/sys_saturn.cxx, 1 while R is down on pad 1. No
// arguments on either side: an extern "C" arity mismatch links silently.
extern "C" s32 Tethys_CheatButtonHeld();
#endif

namespace AO {

ALIVE_VAR(1, 0x4FF958, CheatController*, pCheatController_4FF958, nullptr);

ALIVE_VAR(1, 0x507704, s16, sEnableFartGasCheat_507704, 0);
ALIVE_VAR(1, 0x507708, s16, sVoiceCheat_507708, 0);
ALIVE_VAR(1, 0x50770C, s16, sEnableCheatFMV_50770C, 0);
ALIVE_VAR(1, 0x507710, s16, sEnableCheatLevelSelect_507710, 0);

EXPORT void CC CheatController_Cheat_FartGas_40FB70()
{
    sEnableFartGasCheat_507704 = sEnableFartGasCheat_507704 == 0;
}

EXPORT void CC CheatController_Cheat_VoiceLocks_40FB90()
{
    sVoiceCheat_507708 = sVoiceCheat_507708 == 0;
}

EXPORT void CC CheatController_Cheat_LevelSelect_40FBD0()
{
    if (gMap_507BA8.field_4_current_camera == 1)
    {
        sEnableCheatLevelSelect_507710 = sEnableCheatLevelSelect_507710 == 0;
    }
}

EXPORT void CC CheatController_Cheat_FMV_40FBB0()
{
    if (gMap_507BA8.field_4_current_camera == 1)
    {
        sEnableCheatFMV_50770C = sEnableCheatFMV_50770C == 0;
    }
}

struct CheatEntry final
{
    s32 field_0_level_mask;
    s32 field_4_cheat_code_length;
    const InputCommands* field_8_cheat_code_ary;
    s32 field_C_success_idx;
    void (*field_10_callback)(void);
};
ALIVE_ASSERT_SIZEOF(CheatEntry, 0x14);


#ifdef TETHYS_SATURN
// SATURN (446.ao.2): THE PSX CODES, NOT THE PC ONES. RELIVE carries the PC
// keyboard's tables (hold Shift; six Throws for the fart gas, arrows only on the
// menu); these are the PlayStation's, entered holding R1. The game reads that R1
// as Run, but the player's thumb knows it as the right shoulder, so we ask for R
// (446.ao.3; 446.ao.2 took Run, i.e. X). The PSX face buttons are the Saturn
// diamond (sys_saturn.cxx): Square = Action (A), Cross = Crouch/Roll (B),
// Circle = Throw (C), Triangle = Hop (Y).
//   Green farts, in play:  Up, Left, Right, Square, Circle, Cross
//   Voice locks, in play:  Triangle, Up, Circle, Left, Cross, Down, Square, Right
//   FMV select, title:     Up, Left, Right, Square, Circle, Triangle, Square,
//                          Right, Left, Up, Right
//   Level select, title:   Down, Right, Left, Right, Square, Circle, Square,
//                          Triangle, Circle, Square, Right, Left
// These replace the PC tables outright: they were the only other way in.
const InputCommands sCheatKeyArray_FartGas_4C50B8[] = {
    eUp, eLeft, eRight, eDoAction, eThrowItem, eCrouchOrRoll,
};

const InputCommands sCheatKeyArray_VoiceLocks_4C50C8[] = {
    eHop, eUp, eThrowItem, eLeft, eCrouchOrRoll, eDown, eDoAction, eRight,
};

const InputCommands sCheatKeyArray_FMV_4C50D8[] = {
    eUp, eLeft, eRight, eDoAction, eThrowItem, eHop, eDoAction, eRight, eLeft, eUp, eRight,
};

const InputCommands sCheatKeyArray_LevelSelect_4C50E8[] = {
    eDown, eRight, eLeft, eRight, eDoAction, eThrowItem, eDoAction, eHop, eThrowItem, eDoAction, eRight, eLeft,
};
#else
const InputCommands sCheatKeyArray_FartGas_4C50B8[] = {
    eThrowItem,
    eThrowItem,
    eThrowItem,
    eThrowItem,
    eThrowItem,
    eThrowItem,
};

const InputCommands sCheatKeyArray_VoiceLocks_4C50C8[] = {
    eCrouchOrRoll,
    eDown,
    eThrowItem,
    eDoAction,
    eDown,
    eCrouchOrRoll,
};

const InputCommands sCheatKeyArray_FMV_4C50D8[] = {
    eUp,
    eLeft,
    eRight,
    eLeft,
    eRight,
    eLeft,
    eRight,
    eDown,
};

const InputCommands sCheatKeyArray_LevelSelect_4C50E8[] = {
    eDown,
    eRight,
    eLeft,
    eRight,
    eLeft,
    eRight,
    eLeft,
    eUp,
};
#endif

CheatEntry stru_4C50F8[4] = {
    {~0, ALIVE_COUNTOF(sCheatKeyArray_FartGas_4C50B8), sCheatKeyArray_FartGas_4C50B8, 0, &CheatController_Cheat_FartGas_40FB70},
    {~0, ALIVE_COUNTOF(sCheatKeyArray_VoiceLocks_4C50C8), sCheatKeyArray_VoiceLocks_4C50C8, 0, &CheatController_Cheat_VoiceLocks_40FB90},
    {1, ALIVE_COUNTOF(sCheatKeyArray_FMV_4C50D8), sCheatKeyArray_FMV_4C50D8, 0, &CheatController_Cheat_FMV_40FBB0},
    {1, ALIVE_COUNTOF(sCheatKeyArray_LevelSelect_4C50E8), sCheatKeyArray_LevelSelect_4C50E8, 0, &CheatController_Cheat_LevelSelect_40FBD0}};

CheatController* CheatController::ctor_40FBF0()
{
    ctor_487E10(1);
    field_6_flags.Set(BaseGameObject::eSurviveDeathReset_Bit9);
    SetVTable(this, 0x4BA8A8);
    field_4_typeId = Types::eNone_0;
    field_10 = 0;
    return this;
}

BaseGameObject* CheatController::VDestructor(s32 flags)
{
    return Vdtor_40FCD0(flags);
}


void CheatController::VScreenChanged()
{
    // Empty
}

BaseGameObject* CheatController::dtor_40FC20()
{
    SetVTable(this, 0x4BA8A8);
    pCheatController_4FF958 = nullptr;
    return dtor_487DF0();
}

CheatController* CheatController::Vdtor_40FCD0(s32 flags)
{
    dtor_40FC20();
    if (flags & 1)
    {
        ao_delete_free_447540(this);
    }
    return this;
}

void CheatController::VUpdate()
{
    VUpdate_40FC40();
}

void CheatController::VUpdate_40FC40()
{
#ifdef TETHYS_SATURN
    // SATURN (446.ao.2-3): compare the game verbs only. Held R makes every face
    // button arrive as GameSpeak bank 2, i.e. its enabler beside the PSX face bit
    // (AO/Input.cpp:57-107); and if a remap moves that bank off R, our A carries
    // Confirm and our B carries Back instead (sys_saturn.cxx). Either way an exact
    // match would never see Square or Cross.
    const u16 held = static_cast<u16>(Input().Held() & ~(InputCommands::eLeftGamespeak | InputCommands::eRightGameSpeak | InputCommands::eUnPause_OrConfirm | InputCommands::ePause | InputCommands::eBack | InputCommands::eCheatMode));
#else
    const u16 held = Input().Held();
#endif
    if (held)
    {
        // Only do cheat code check if shift is held
#ifdef TETHYS_SATURN
        // SATURN (446.ao.3): R, which AO cannot see alone (sys_saturn.cxx).
        if (Tethys_CheatButtonHeld())
#else
        if (Input().IsAnyPressed(InputCommands::eRun))
#endif
        {
            for (auto& cheatEntry : stru_4C50F8)
            {
                // Bit shift current level for level mask.
                if ((1 << static_cast<s32>(gMap_507BA8.field_0_current_level)) & cheatEntry.field_0_level_mask)
                {
                    if (held == cheatEntry.field_8_cheat_code_ary[cheatEntry.field_C_success_idx])
                    {
                        cheatEntry.field_C_success_idx++;

                        // Check if we've successfully entered all cheat code keys.
                        if (cheatEntry.field_C_success_idx >= cheatEntry.field_4_cheat_code_length)
                        {
                            cheatEntry.field_C_success_idx = 0;
                            cheatEntry.field_10_callback();
                        }
                    }
                    else
                    {
                        cheatEntry.field_C_success_idx = 0;
                    }
                }
            }
        }
    }
}

} // namespace AO
