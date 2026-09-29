#include "stdafx_ao.h"
#include "Function.hpp"
#include "Input.hpp"
#include "Game.hpp"
#include "Midi.hpp"
#include "PauseMenu.hpp"
#include "Primitives.hpp"
#include "Psx.hpp"
#include "PsxDisplay.hpp"
#include "ResourceManager.hpp"
#include "SaveGame.hpp"
#include "StringFormatters.hpp"
#include "ScreenManager.hpp"
#include "Sound.hpp"
#include "stdlib.hpp"
#include "Sfx.hpp"
#include "Sys.hpp"
#include "Map.hpp"
#include "GameAutoPlayer.hpp"
#include "Abe.hpp" // SATURN (417.ao.1): the death gate reads sActiveHero_507678

#if ORIGINAL_PS1_BEHAVIOR
    #include "../AliveLibAE/Sys.hpp"
#endif

namespace AO {

// SATURN: 425.ao.2 -- THE MENU SPEAKS ITS DISC'S LANGUAGE.
//
// The tester: "the menu is not localized".  The main menu's own labels never
// were English -- they are ART, baked into S1.LVL's cameras, and build.ps1
// already converts one S1 per language.  What stayed English is everything
// DRAWN OVER that art, and all of it is C++ string literals: this file's six
// tables, the sixteen level names (which are also the SAVE TITLE written to the
// console's memory, so localizing them localizes the load screen too) and the
// remap page in MainMenu.cpp.
//
// The translations are not ours to write: Oddworld shipped them, and they are
// in the localized AbeWin.exe.  tools/menutext.py lifts them out offline and
// writes these .inc files -- the same shape as tools/lcdmsg.py and the LCD
// marquee (bt997), for the same reason: a pack read at boot cost 8,552 B of
// LWRAM and starved SND_CreateDS without a cartridge, while a table baked per
// language costs only the DELTA in .rodata and not one byte of LWRAM, not one
// CD read.  +165 B on the French image, +130 B on the Spanish.  The English
// literals below lose their last referent when an arm is taken, so the linker
// drops them -- which is why exactly ONE arm may define each table.
//
// x AND y COME FROM THE EXECUTABLE TOO, and that is not incidental: the
// localizers MOVED the columns to fit longer words (French controls sit at
// x=50/170 where English sits at 80/200).  A translated string at the English
// x runs off the screen.
//
// WHAT THE .inc CANNOT CARRY is the pad glyphs.  The localized executables
// spell the PC key out as a word ("echap", "intro") or drop it, so the kAO_*
// control bytes are gone from those lines; they are rebuilt here, around the
// macros, so the label follows a remapped pad instead of lying about a
// keyboard.  Accented bytes are written as octal escapes, like the .inc does,
// because this file is UTF-8 and a raw 'é' would reach the atlas as two bytes
// and draw two blanks.
#if defined(TETHYS_SATURN) && !defined(TETHYS_LANG_EN) && defined(TETHYS_LANG_ES) && __has_include("tethys_menu_es.inc")
    #include "tethys_menu_es.inc"
    #define TETHYS_MENU_L10N 1
    #define TETHYS_MENU_QUIT_YESNO   kAO_ConfirmContinue " s\241   " kAO_Esc " no"
    #define TETHYS_MENU_SAVE_OK      kAO_ConfirmContinue "   guardar"
    #define TETHYS_MENU_SAVE_CANCEL  kAO_Esc "   cancelar"
    #define TETHYS_MENU_MORE_EXIT    kAO_ConfirmContinue " m\240s  " kAO_Esc " salir"
    #define TETHYS_MENU_MOUNT        "montar " kAO_Or " entrar"
    #define TETHYS_MENU_EXIT         kAO_Esc " salir"
    #define TETHYS_MENU_SAVED        "GUARDADO"
    #define TETHYS_MENU_SAVE_FAILED  "FALLO"
    #define TETHYS_MENU_SLOT_FREE    "-- LIBRE --"
#elif defined(TETHYS_SATURN) && !defined(TETHYS_LANG_EN) && !defined(TETHYS_LANG_ES) && __has_include("tethys_menu_fr.inc")
    #include "tethys_menu_fr.inc"
    #define TETHYS_MENU_L10N 1
    #define TETHYS_MENU_QUIT_YESNO   kAO_ConfirmContinue " oui   " kAO_Esc " non"
    #define TETHYS_MENU_SAVE_OK      kAO_ConfirmContinue "   sauvegarder"
    #define TETHYS_MENU_SAVE_CANCEL  kAO_Esc "   annuler"
    #define TETHYS_MENU_MORE_EXIT    kAO_ConfirmContinue " plus  " kAO_Esc " quitter"
    // The French executable lost the second verb here where the Spanish keeps
    // it ("montar <or> entrar"); "entrer" is the only word in this file that
    // Oddworld did not write.
    #define TETHYS_MENU_MOUNT        "monter " kAO_Or " entrer"
    #define TETHYS_MENU_EXIT         kAO_Esc " quitter"
    #define TETHYS_MENU_SAVED        "SAUVEGARDE OK"
    #define TETHYS_MENU_SAVE_FAILED  "ECHEC"
    #define TETHYS_MENU_SLOT_FREE    "-- LIBRE --"
#else
    #define TETHYS_MENU_SAVED        "SAVED"
    #define TETHYS_MENU_SAVE_FAILED  "SAVE FAILED"
    #define TETHYS_MENU_SLOT_FREE    "-- EMPTY --"
#endif

ALIVE_VAR(1, 0x5080E0, PauseMenu*, pPauseMenu_5080E0, nullptr);

const u8 byte_4C5EE8[32] = {
    0u,
    0u,
    33u,
    132u,
    66u,
    136u,
    99u,
    140u,
    132u,
    144u,
    165u,
    20u,
    231u,
    28u,
    8u,
    33u,
    41u,
    37u,
    74u,
    41u,
    107u,
    45u,
    140u,
    49u,
    173u,
    53u,
    239u,
    61u,
    16u,
    66u,
    115u,
    78u};


PauseMenu* PauseMenu::ctor_44DEA0()
{
    ctor_417C10();
    SetVTable(this, 0x4BBD68);

    field_4_typeId = Types::ePauseMenu_61;
    field_8_update_delay = 25;

    field_6_flags.Clear(BaseGameObject::eDrawable_Bit4);
    field_6_flags.Set(BaseGameObject::eSurviveDeathReset_Bit9);

    gObjList_drawables_504618->Push_Back(this);
    field_E4_font.ctor_41C170(175, byte_4C5EE8, &sFontContext_4FFD68);
    field_130 = 0;
    field_11C = 0;
    sDisableFontFlicker_5080E4 = FALSE;
    return this;
}

BaseGameObject* PauseMenu::dtor_44DF40()
{
    SetVTable(this, 0x4BBD68);

    field_6_flags.Clear(Options::eDrawable_Bit4);
    gObjList_drawables_504618->Remove_Item(this);
    field_E4_font.dtor_41C130();

    return dtor_417D10();
}

BaseGameObject* PauseMenu::VDestructor(s32 flags)
{
    return Vdtor_44EAA0(flags);
}

PauseMenu* PauseMenu::Vdtor_44EAA0(s32 flags)
{
    dtor_44DF40();
    if (flags & 1)
    {
        ao_delete_free_447540(this);
    }
    return this;
}

void PauseMenu::VScreenChanged()
{
    VScreenChange_44EA90();
}

void PauseMenu::VScreenChange_44EA90()
{
    if (gMap_507BA8.field_A_level == LevelIds::eCredits_10)
    {
        field_6_flags.Set(BaseGameObject::eDead_Bit3);
    }
}

void PauseMenu::VUpdate()
{
    VUpdate_44DFB0();
}

ALIVE_VAR(1, 0x9F1188, s16, word_9F1188, 0);
ALIVE_VAR(1, 0x504620, s16, word_504620, 0);
ALIVE_VAR(1, 0x504622, s16, word_504622, 0);
ALIVE_VAR(1, 0x9F0E60, u16, word_9F0E60, 0);
ALIVE_VAR(1, 0x504624, u16, word_504624, 0);
ALIVE_VAR(1, 0x504626, u16, word_504626, 0);

EXPORT s16 Reset_Unknown_45A5B0()
{
    word_9F1188 = -1;
    word_504620 = -1;
    word_504622 = -1;
    word_9F0E60 = 0;
    word_504624 = 0;
    word_504626 = 0;
    return 1;
}

struct saveName final
{
    char_type characters[26];
};
ALIVE_VAR(1, 0x5080C6, saveName, saveNameBuffer_5080C6, {});

#ifdef TETHYS_MENU_L10N
// SATURN: 425.ao.2 -- these sixteen names are ALSO the save title (:471 copies
// one into saveNameBuffer), so this array alone localizes the load screen.
// The eighteen-byte ceiling is not a style rule: the buffer is characters[26],
// the copy starts at [2] and the path id, its separator and the NUL take the
// rest, so nineteen bytes stops terminating the string and the strcat that
// follows walks out of the struct.  Spanish sits EXACTLY on it -- "Templo
// paramoniano", "Nidos scrabanianos" and two more are 18 bytes to the letter --
// so tools/menutext.py refuses anything longer rather than trusting a reader to
// notice.
const char_type* gLevelNames_4CE1D4[20] = {
    kTethysMenuLevelNames[0], kTethysMenuLevelNames[1], kTethysMenuLevelNames[2],
    kTethysMenuLevelNames[3], kTethysMenuLevelNames[4], kTethysMenuLevelNames[5],
    kTethysMenuLevelNames[6], kTethysMenuLevelNames[7], kTethysMenuLevelNames[8],
    kTethysMenuLevelNames[9], kTethysMenuLevelNames[10], kTethysMenuLevelNames[11],
    kTethysMenuLevelNames[12], kTethysMenuLevelNames[13], kTethysMenuLevelNames[14],
    kTethysMenuLevelNames[15], kTethysMenuLevelNames[16], kTethysMenuLevelNames[17],
    kTethysMenuLevelNames[18], kTethysMenuLevelNames[19]};
#else
const char_type* gLevelNames_4CE1D4[20] = {
    "¸",
    "RuptureFarms",
    "Monsaic Lines",
    "Paramonia",
    "Paramonian Temple",
    "Stockyard Escape",
    "Stockyards",
    "",
    "Scrabania",
    "Scrabanian Temple",
    "",
    "",
    "The Boardroom",
    "RuptureFarms II",
    "Paramonian Nest",
    "Scrabanian Nest",
    "Rescue Zulag 1",
    "Rescue Zulag 2",
    "Rescue Zulag 3",
    "Rescue Zulag 4"};
#endif

enum PauseMenuPages
{
    ePause_0 = 0,
    eSave_1 = 1,
    eControls_2 = 2,
    eQuit_3 = 3
};

#ifdef TETHYS_SATURN
// SATURN (ao261.22): the save-result banner.  0 = nothing to show, 1 = saved,
// 2 = failed.  File-scope rather than a PauseMenu field because the struct has
// ALIVE_ASSERT_SIZEOF over it and there is exactly one pause menu.
static s16 sTethysSaveMsg = 0;
static s16 sTethysSaveHold = 0;

// SATURN (389.ao.1): THE SLOT PICKER.
//
// src/save_saturn.cxx always had four slots and always chose between them
// itself -- "first FREE, else the oldest" -- with its own header saying that
// was only "since there is no picker UI". This is that UI, and it is small
// because everything under it already existed: the device sweep
// (Tethys_SaveDevice*), the occupancy mask and the stored titles
// (Tethys_SaveSlot*), and the main menu's list/navigate/confirm shape to copy.
//
// The slot travels through Tethys_SaveSlotRequest, NOT through the save name.
// The name looked like the obvious channel -- IO_EnumerateDirectory already
// round-trips the slot as a leading digit for LOAD -- but that digit is
// PREFIXED onto the stored title for display, so putting one in the title
// would show it twice and eventually be parsed as part of the name.
//
// Leaving the page without confirming must not leave a request armed for some
// later save, so the request is set at the moment of confirmation only, and
// save_saturn consumes it once and clears it.
extern "C" s32 Tethys_SaveSlotRequest(s32 slot);
extern "C" s32 Tethys_SaveSlotMask(void);
extern "C" s32 Tethys_SaveSlotCount(void);
extern "C" s32 Tethys_SaveSlotTitle(s32 slot, char* out, s32 outMax);
extern "C" const char* Tethys_SaveDeviceName(s32 device);
extern "C" s32 Tethys_SaveDeviceGet(void);
static s16 sTethysSlot = 0;

// SNAPSHOT, not a live read. Tethys_SaveSlotMask/Title each go to the device
// through the BUP BIOS, so calling them from VRender would be EIGHT backup
// reads per frame for a list that cannot change while the page is open. Taken
// once when the page opens, and again after a write so the player watches the
// save land in the slot they chose.
static s32 sTethysSlotMask = 0;
static s32 sTethysSlotN = 0;
static char_type sTethysSlotTitle[4][24] = {};

static void Tethys_SnapshotSlots()
{
    sTethysSlotN = Tethys_SaveSlotCount();
    if (sTethysSlotN > 4)
    {
        sTethysSlotN = 4; // the render array is the bound, not the device
    }
    sTethysSlotMask = Tethys_SaveSlotMask();
    for (s32 i = 0; i < sTethysSlotN; i++)
    {
        sTethysSlotTitle[i][0] = 0;
        if (sTethysSlotMask & (1 << i))
        {
            Tethys_SaveSlotTitle(i, sTethysSlotTitle[i],
                                 static_cast<s32>(sizeof(sTethysSlotTitle[i])));
        }
    }
}
#endif

void PauseMenu::VUpdate_44DFB0()
{
#ifdef TETHYS_SATURN
    // SATURN (bt1071): sFontContext_4FFD68's atlas is loaded only by the Menu
    // (MainMenu.cpp:730), which the menu-less boot never runs. Load it lazily
    // on the FIRST pause: by now the R1 archive is open, LoadResourceFile_455270
    // registers the font globally (a null camera is tolerated), and LoadFontType
    // uploads it then frees it. One blocking CD read on that first pause; the
    // atlas then stays resident in VRAM for the session. Try once: if the font
    // is genuinely absent the menu stays inert rather than re-reading every
    // frame.
    //
    // FONT CHOICE -- id 1 (MENU.FNT), the real menu font: the pause menu must
    // match the PSX original, not the LCD marquee face. The earlier "id 1
    // overflows DrawString_41C360's s8 UV" reasoning was WRONG on two counts:
    // (1) the menu font is exactly 256x256 = one full PSX texture page, so the
    // allocator keeps it page-aligned (rect.x&0x3F == 0 and rect.y&0xFF == 0)
    // and texture_u/v never exceed 255; (2) even where a wrap could occur, the
    // s8 store + u8 read-back in the renderer round-trips losslessly. The menu
    // font is the SAFEST case, not the hardest -- the LCDFONT switch (ao261)
    // was a wrong fix for a non-bug and is reverted here.
    if (!sFontContext_4FFD68.field_8_atlas_array)
    {
        static s16 sTethysMenuFontTried = 0;
        if (!sTethysMenuFontTried)
        {
            sTethysMenuFontTried = 1;
            if (ResourceManager::LoadResourceFile_455270("MENU.FNT", nullptr))
            {
                sFontContext_4FFD68.LoadFontType_41C040(1);
                sFontLoaded_507688 = 1;
            }
        }
        if (!sFontContext_4FFD68.field_8_atlas_array)
        {
            return;
        }
    }
#endif
#ifdef TETHYS_SATURN
    // SATURN (417.ao.1): NO PAUSE BETWEEN DEATH AND RESPAWN. AO's pause has no
    // gate at all -- AE's has one (AliveLibAE/PauseMenu.cpp:1612: health > 0,
    // not electrocuted, not in a well/Shrykull) and AO never got it. Pausing
    // during Motion_60_Dead runs the menu's modal loop (SND_StopAll, Reclaim_
    // Memory_455660) in the middle of the death sequence, whose case 4 fetches
    // the respawn resources and whose Motion_61_Respawn reloads the checkpoint
    // (SaveGame::LoadFromMemory_459970) -- the tester's "bug au respawn".
    // Health alone is NOT enough: Motion_61 case 0 sets it back to 1 on its first
    // frame (Abe.cpp:7260) while the respawn still has ~70 frames to run, and
    // Motion_59_DeathDropFall only zeroes it when it hands over to Motion_60
    // (Abe.cpp:7023). So the gate is health OR electrocuted OR a death/respawn
    // motion; the respawn ends by leaving Motion_61, which re-opens the pause.
    if (sActiveHero_507678)
    {
        const s16 heroMotion = sActiveHero_507678->field_FC_current_motion;
        if (sActiveHero_507678->field_100_health <= FP_FromInteger(0)
            || sActiveHero_507678->field_10A_flags.Get(Flags_10A::e10A_Bit5_Electrocuted)
            || heroMotion == eAbeMotions::Motion_59_DeathDropFall_42CBE0
            || heroMotion == eAbeMotions::Motion_60_Dead_42C4C0
            || heroMotion == eAbeMotions::Motion_61_Respawn_42CD20
            || heroMotion == eAbeMotions::Motion_86_FallLandDie_42EDD0
            || heroMotion == eAbeMotions::Motion_164_PoisonGasDeath_42A120)
        {
            return;
        }
    }
#endif
    if (Input().IsAnyHeld(InputCommands::ePause))
    {
        SND_StopAll_4762D0();
        SFX_Play_43AE60(SoundEffect::PossessEffect_21, 40, 2400, 0);
        field_6_flags.Set(Options::eDrawable_Bit4);
        field_11C = 1;
        field_124 = 0;
        field_126_page = PauseMenuPages::ePause_0;
        Reset_Unknown_45A5B0();
        field_132_always_0 = 0;
        field_11E_selected_glow = 52;
        field_120_selected_glow_counter = 8;

        // This is bad, let's nuke it later :)
        while (1)
        {
            sDisableFontFlicker_5080E4 = 1;
            SYS_EventsPump_44FF90();

            for (s32 idx = 0; idx < gObjList_drawables_504618->Size(); idx++)
            {
                auto pObjIter = gObjList_drawables_504618->ItemAt(idx);
                if (!pObjIter)
                {
                    break;
                }
                if (!pObjIter->field_6_flags.Get(Options::eDead_Bit3))
                {
                    if (pObjIter->field_6_flags.Get(Options::eDrawable_Bit4))
                    {
                        pObjIter->VRender(gPsxDisplay_504C78.field_C_drawEnv[gPsxDisplay_504C78.field_A_buffer_index].field_70_ot_buffer);
                    }
                }
            }
            pScreenManager_4FF7C8->VRender(
                gPsxDisplay_504C78.field_C_drawEnv[gPsxDisplay_504C78.field_A_buffer_index].field_70_ot_buffer);
            PSX_DrawSync_496750(0);
            ResourceManager::Reclaim_Memory_455660(500000);
            gPsxDisplay_504C78.PSX_Display_Render_OT_40DD20();
            Input().Update(GetGameAutoPlayer());

            if (field_120_selected_glow_counter > 0)
            {
                field_11E_selected_glow += 8;
            }

            if (field_11E_selected_glow <= 100 || field_120_selected_glow_counter <= 0)
            {
                if (field_120_selected_glow_counter <= 0)
                {
                    field_11E_selected_glow -= 8;
                    if (field_11E_selected_glow < 52)
                    {
                        field_120_selected_glow_counter = -field_120_selected_glow_counter;
                        field_11E_selected_glow += field_120_selected_glow_counter;
                    }
                }
            }
            else
            {
                field_120_selected_glow_counter = -field_120_selected_glow_counter;
                field_11E_selected_glow += field_120_selected_glow_counter;
            }

            enum Page1Selectables
            {
                eContinue_0 = 0,
                eSave_1 = 1,
                eControls_2 = 2,
                eQuit_3 = 3
            };

            switch (field_126_page)
            {
                case PauseMenuPages::ePause_0:
                {
                    if (Input().IsAnyHeld(InputCommands::eCheatMode | InputCommands::eDown))
                    {
                        field_124++;
                        if (field_124 > 3)
                        {
                            field_124 = 0;
                        }
                        SFX_Play_43AE60(SoundEffect::MenuNavigation_61, 45, 400, 0);
                    }

                    if (Input().IsAnyHeld(InputCommands::eUp))
                    {
                        field_124--;
                        if (field_124 < 0)
                        {
                            field_124 = 3;
                        }
                        SFX_Play_43AE60(SoundEffect::MenuNavigation_61, 45, 400, 0);
                    }

#if ORIGINAL_PS1_BEHAVIOR // OG Change - Pause Menu controls like PS1
                    if (Input().IsAnyHeld(InputCommands::ePause))
                    {
                        field_11C = 0;
                        SFX_Play_43AE60(SoundEffect::PossessEffect_21, 40, 2400, 0);
                        SND_Restart_476340();
                        break;
                    }

                    const bool optionClicked = Input().IsAnyHeld(InputCommands::eUnPause_OrConfirm);
#else
                    const bool optionClicked = Input().IsAnyHeld(
                        InputCommands::eHop | InputCommands::eThrowItem | InputCommands::eUnPause_OrConfirm | InputCommands::eDoAction | InputCommands::eBack);
#endif
                    if (optionClicked)
                    {
                        switch (field_124)
                        {
                            case Page1Selectables::eContinue_0:
                            {
                                field_11C = 0;
                                SFX_Play_43AE60(SoundEffect::PossessEffect_21, 40, 2400, 0);
                                SND_Restart_476340();
                                break;
                            }
                            case Page1Selectables::eSave_1:
                            {
                                field_126_page = PauseMenuPages::eSave_1;
                                field_12C = 0;
                                field_12E = 0;
                                field_134 = 1;
#ifdef TETHYS_SATURN
                                // Open on the slot the backend would have taken
                                // on its own, so confirming without touching
                                // anything behaves exactly as it did before.
                                Tethys_SnapshotSlots();
                                {
                                    sTethysSlot = 0;
                                    for (s32 i = 0; i < sTethysSlotN; i++)
                                    {
                                        if (!(sTethysSlotMask & (1 << i)))
                                        {
                                            sTethysSlot = static_cast<s16>(i);
                                            break;
                                        }
                                    }
                                }
#endif
                                SFX_Play_43AD70(SoundEffect::IngameTransition_107, 90, 0);
                                s32 tmp = static_cast<s32>(gMap_507BA8.field_0_current_level);
                                if (gMap_507BA8.field_0_current_level == LevelIds::eRuptureFarmsReturn_13)
                                {
                                    s16 row = 0;
                                    auto pathId = SaveGame::GetPathId(gMap_507BA8.field_2_current_path, &row);

                                    if (pathId != -1)
                                    {
                                        tmp += row + 3;
                                    }
                                }

                                auto curPathId = gMap_507BA8.field_2_current_path;
                                char_type curPathIdNumBuf[12] = {};

                                strncpy(&saveNameBuffer_5080C6.characters[2], gLevelNames_4CE1D4[tmp], 19);
                                if (tmp != 12 && tmp != 14 && tmp != 15)
                                {
                                    strcat(&saveNameBuffer_5080C6.characters[2], " ");
                                    if (strlen(&saveNameBuffer_5080C6.characters[2]) < 18)
                                    {
                                        strcat(&saveNameBuffer_5080C6.characters[2], "p");
                                    }
                                    sprintf(curPathIdNumBuf, "%d", curPathId);
                                    strncat(&saveNameBuffer_5080C6.characters[2], curPathIdNumBuf, 19u);
                                }

                                const char_type aux[2] = {18, 0};
                                strncat(&saveNameBuffer_5080C6.characters[2], aux, 19u);
#if ORIGINAL_PS1_BEHAVIOR // OG Change - Allow for exiting save menu using controller
                                setSaveMenuOpen(true); // Sets saveMenuOpen bool to true, instead of disabling input
#else
                                Input_DisableInput_48E690();
#endif
                                break;
                            }
                            case Page1Selectables::eControls_2:
                            {
                                field_126_page = PauseMenuPages::eControls_2;
                                field_128_controller_id = 0;
                                SFX_Play_43AD70(SoundEffect::IngameTransition_107, 90, 0);
                                break;
                            }
                            case Page1Selectables::eQuit_3:
                            {
                                field_126_page = PauseMenuPages::eQuit_3;
                                field_124 = 0;
                                SFX_Play_43AD70(SoundEffect::IngameTransition_107, 90, 0);
                                break;
                            }
                            default:
                            {
                                break;
                            }
                        }
                        break;
                    }
                    break;
                }
                case PauseMenuPages::eSave_1:
                {
                    if (field_12C)
                    {
                        if (field_12C == 4)
                        {
                            if (field_134)
                            {
                                field_134 = 0;
                            }
                            else
                            {
#ifdef TETHYS_SATURN
                                // SATURN (ao261.22): TELL THE PLAYER WHETHER IT
                                // WORKED.  The original discards this return
                                // because on PC a failed fopen means a broken
                                // installation; on a console it is the ORDINARY
                                // case -- no backup RAM fitted, an unformatted
                                // device, or 32 KB already full of other games'
                                // saves.  Without feedback the menu simply
                                // closed and a silent failure looked exactly
                                // like a success.
                                const Bool32 saved =
                                    SaveGame::SaveToFile_45A110(&saveNameBuffer_5080C6.characters[2]);
                                sTethysSaveMsg = saved ? 1 : 2;
                                sTethysSaveHold = 90; // ~3 s at 30 fps
                                Tethys_SnapshotSlots(); // the list the player now sees
                                if (!saved)
                                {
                                    SFX_Play_43AD70(SoundEffect::ElectricZap_46, 0, 0);
                                }
#else
                                SaveGame::SaveToFile_45A110(&saveNameBuffer_5080C6.characters[2]);
#endif
                                field_12C = 5;
                                field_12A = 13;
                                field_122 = 120;
                            }
                        }
                        else if (field_12C == 5)
                        {
#ifdef TETHYS_SATURN
                            // Hold the result on screen before the menu closes.
                            // Any confirm/cancel button cuts it short, so a
                            // player who has read it never waits out the timer.
                            if (sTethysSaveHold > 0
                                && !Input().IsAnyHeld(InputCommands::eBack | InputCommands::eUnPause_OrConfirm | InputCommands::eThrowItem | InputCommands::eDoAction))
                            {
                                sTethysSaveHold--;
                                break;
                            }
                            sTethysSaveMsg = 0;
                            sTethysSaveHold = 0;
#endif
                            field_11C = 0;
                            SFX_Play_43AE60(SoundEffect::PossessEffect_21, 40, 2400, 0);
                            SND_Restart_476340();
                        }
                        break;
                    }

#ifdef TETHYS_SATURN
                    // The slot moves only while the page is IDLE: once the
                    // player has confirmed, field_12C walks 4 -> 5 through the
                    // write and the result hold, and a stray d-pad press there
                    // must not move a highlight the write has already used.
                    if (field_12C == 0)
                    {
                        const s32 n = sTethysSlotN;
                        if (n > 1 && Input().IsAnyHeld(InputCommands::eCheatMode | InputCommands::eDown))
                        {
                            sTethysSlot = static_cast<s16>((sTethysSlot + 1) % n);
                            SFX_Play_43AE60(SoundEffect::MenuNavigation_61, 45, 400, 0);
                        }
                        if (n > 1 && Input().IsAnyHeld(InputCommands::eUp))
                        {
                            sTethysSlot = static_cast<s16>((sTethysSlot + n - 1) % n);
                            SFX_Play_43AE60(SoundEffect::MenuNavigation_61, 45, 400, 0);
                        }
                    }
#endif
                    auto last_pressed = static_cast<char_type>(Input_GetLastPressedKey_44F2C0());
                    char_type lastPressedKeyNT[2] = {last_pressed, 0};

#if ORIGINAL_PS1_BEHAVIOR // OG Change - Exit save menu using controller
                    if (last_pressed == VK_ESCAPE || last_pressed == VK_RETURN) // Keyboard ESC or ENTER
                    {
                        setSaveMenuOpen(false);
                    }
                    else if (Input().IsAnyHeld(InputCommands::eBack)) // Triangle
                    {
                        last_pressed = VK_ESCAPE;
                        setSaveMenuOpen(false);
                    }
                    else if (Input().IsAnyHeld(InputCommands::eUnPause_OrConfirm)) // Cross or Start
                    {
                        last_pressed = VK_RETURN;
                        setSaveMenuOpen(false);
                    }
#endif

                    if (!last_pressed)
                    {
                        break;
                    }
                    auto string_len_no_nullterminator = strlen(&saveNameBuffer_5080C6.characters[2]);
                    switch (last_pressed)
                    {
                        case VK_ESCAPE:
                        {
                            SFX_Play_43AE60(SoundEffect::PossessEffect_21, 40, 2400, 0);
                            field_126_page = 0;
                            Input_Reset_44F2F0();
                            break;
                        }
                        case VK_RETURN:
                        {
                            if (string_len_no_nullterminator <= 1)
                            {
                                SFX_Play_43AD70(SoundEffect::ElectricZap_46, 0, 0);
                                break;
                            }
                            SFX_Play_43AD70(SoundEffect::IngameTransition_107, 90, 0);
                            saveNameBuffer_5080C6.characters[string_len_no_nullterminator + 1] = 0;
#ifdef TETHYS_SATURN
                            // Armed HERE and nowhere else, so cancelling the
                            // page cannot leave a stale request behind.
                            Tethys_SaveSlotRequest(sTethysSlot);
#endif
                            field_12C = 4;
                            field_12A = 11;
                            field_134 = 1;
                            Input_Reset_44F2F0();
                            break;
                        }
                        case VK_BACK:
                        {
                            if (string_len_no_nullterminator <= 1)
                            {
                                SFX_Play_43AD70(SoundEffect::ElectricZap_46, 0, 0);
                                break;
                            }
                            saveNameBuffer_5080C6.characters[string_len_no_nullterminator] = 18;
                            saveNameBuffer_5080C6.characters[string_len_no_nullterminator + 1] = 0;
                            SFX_Play_43AD70(SoundEffect::PickupItem_33, 0, 0);
                            break;
                        }
                        default:
                        {
                            if (strspn(lastPressedKeyNT, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 !-"))
                            {
                                if (lastPressedKeyNT[0] != 32 || (string_len_no_nullterminator != 1 && saveNameBuffer_5080C6.characters[string_len_no_nullterminator] != lastPressedKeyNT[0]))
                                {
                                    if (string_len_no_nullterminator > 19)
                                    {
                                        SFX_Play_43AD70(SoundEffect::SackWobble_34, 0, 0);
                                    }
                                    else
                                    {
                                        saveNameBuffer_5080C6.characters[string_len_no_nullterminator + 1] = lastPressedKeyNT[0];
                                        saveNameBuffer_5080C6.characters[string_len_no_nullterminator + 2] = 18;
                                        saveNameBuffer_5080C6.characters[string_len_no_nullterminator + 3] = 0;
                                        SFX_Play_43AD70(SoundEffect::RockBounce_31, 0, 0);
                                    }
                                }
                                else
                                {
                                    SFX_Play_43AE60(SoundEffect::PossessEffect_21, 30, 2600, 0);
                                }
                            }
                            else
                            {
                                SFX_Play_43AE60(SoundEffect::PossessEffect_21, 70, 2200, 0);
                            }
                            break;
                        }
                    }
                    break;
                }
                case PauseMenuPages::eControls_2:
                {
                    if (Input().IsAnyHeld(InputCommands::eBack | InputCommands::eHop))
                    {
                        field_126_page = 0;
                        SFX_Play_43AE60(SoundEffect::PossessEffect_21, 40, 2400, 0);
                    }

                    if (Input().IsAnyHeld(
                            InputCommands::eThrowItem | InputCommands::eUnPause_OrConfirm | InputCommands::eDoAction | InputCommands::eCheatMode | InputCommands::eUp | InputCommands::eRight | InputCommands::eDown | InputCommands::eLeft))
                    {
                        field_128_controller_id++;
                        if (field_128_controller_id < 2)
                        {
                            SFX_Play_43AD70(SoundEffect::IngameTransition_107, 90, 0);
                        }
                        else
                        {
                            field_128_controller_id = 0;
                            field_126_page = PauseMenuPages::ePause_0;
                            SFX_Play_43AE60(SoundEffect::PossessEffect_21, 40, 2400, 0);
                        }
                    }
                    break;
                }
                case PauseMenuPages::eQuit_3:
                {
                    if (Input().IsAnyHeld(InputCommands::eBack | InputCommands::eHop))
                    {
                        field_126_page = 0;
                        SFX_Play_43AE60(SoundEffect::PossessEffect_21, 40, 2400, 0);
                    }

                    if (Input().IsAnyHeld(InputCommands::eThrowItem | InputCommands::eUnPause_OrConfirm | InputCommands::eDoAction))
                    {
                        field_11C = 0;
                        SFX_Play_43AE60(SoundEffect::PossessEffect_21, 40, 2400, 0);
                        if (pPauseMenu_5080E0 && pPauseMenu_5080E0 == this)
                        {
                            pPauseMenu_5080E0->field_6_flags.Set(Options::eDead_Bit3);
                        }
                        else
                        {
                            field_6_flags.Set(BaseGameObject::eDead_Bit3);
                        }
                        pPauseMenu_5080E0 = 0;
                        gMap_507BA8.SetActiveCam_444660(LevelIds::eMenu_0, 1, CameraIds::Menu::eMainMenu_1, CameraSwapEffects::eInstantChange_0, 0, 0);
                        gMap_507BA8.field_DC_free_all_anim_and_palts = 1;
                        Input().SetCurrentController(InputObject::PadIndex::First);
                    }
                    break;
                }
                default:
                {
                    break;
                }
            }

            if (!field_11C)
            {
                Input().Update(GetGameAutoPlayer());
                field_6_flags.Clear(Options::eDrawable_Bit4);
                break;
            }
        }

        sDisableFontFlicker_5080E4 = 0;
    }
}

void PauseMenu::VRender(PrimHeader** ppOt)
{
    VRender_44E6F0(ppOt);
}

#ifdef TETHYS_SATURN
// SATURN 390.ao.1 -- THE 15-ARGUMENT TAIL, WRITTEN ONCE.
//
// 389.ao.1 shipped the exact fatal the ao261.24 note in eSave_1 describes, in
// the hunk that quotes it.  DrawString_41C360's tail is
//     (..., u8 r, u8 g, u8 b, s32 polyOffset, FP scale, s32 maxRenderWidth,
//      s32 colorRandomRange)
// and the five new calls passed `..., b, 0, FP_FromInteger(1), 640, cursor` --
// so polyOffset was the literal 0 and the cursor went into the colour jitter.
// Every line after the first therefore re-issued polys the previous line had
// already linked into the ordering table, and adding an already-linked
// PrimHeader closes a LOOP in the tag chain: the walk spins to its 100,000-step
// cap (the freeze) and then dies on "OT walk runaway" (the fatal) -- precisely
// what the tester photographed on opening the save page.
//
// A comment did not prevent that, because the defect is POSITIONAL and a
// comment is prose.  This wrapper is the structural fix: the cursor is the LAST
// parameter, no other s32 sits beside it, and the positional tail now exists
// once in this file instead of five times.  Every line this menu adds is
// centred on 184, so the centring folds in as well.
static s32 Tethys_DrawLine(AliveFont& font, PrimHeader** ppOt, const char_type* text,
                           s16 y, u8 r, u8 g, u8 b, s32 polyOffset)
{
    // Centre on 184 and keep the line on screen.  MEASURED: MeasureWidth and
    // the entry x are in the SAME space, the 368-wide one whose centre IS 184
    // (DrawString converts with PsxToPCX(x, 11) on the way to the polygon), so
    // the screen's right edge here is 368 and a line wider than that cannot be
    // shown at all.  DrawEntries' own guard tests `>= 608`, which in this space
    // is nearly twice the screen and therefore never fires -- copying it would
    // have looked like protection while providing none.  The real protection is
    // the width trim at the call site; this is the backstop.
    const s16 wide = static_cast<s16>(font.MeasureWidth_41C2B0(text));
    s16 x = static_cast<s16>(184 - wide / 2);
    if (x < 8)
    {
        x = 8;
    }
    return font.DrawString_41C360(
        ppOt,
        text,
        x,
        y,
        TPageAbr::eBlend_0,
        1,
        0,
        Layer::eLayer_Menu_41,
        r, g, b,
        polyOffset,             // <- the running cursor, in ITS slot
        FP_FromInteger(1),
        640,
        0);                     // colorRandomRange
}
#endif

ALIVE_VAR(1, 0xA88B90, s8, byte_A88B90, 0);

#ifdef TETHYS_MENU_L10N
// SATURN: 425.ao.2.  Strings AND columns from the localized executable; the
// colour and flag bytes are RELIVE's, byte-identical in all three images
// (menutext.py fails hard if one ever differs, rather than emitting a guess).
PauseMenu::PauseEntry pauseEntries_4CDE50[6] = {
    {kTethysMenuPause[0].x, kTethysMenuPause[0].y, kTethysMenuPause[0].s, 128u, 16u, 255u, '\x01'},
    {kTethysMenuPause[1].x, kTethysMenuPause[1].y, kTethysMenuPause[1].s, 128u, 16u, 255u, '\x01'},
    {kTethysMenuPause[2].x, kTethysMenuPause[2].y, kTethysMenuPause[2].s, 128u, 16u, 255u, '\x01'},
    {kTethysMenuPause[3].x, kTethysMenuPause[3].y, kTethysMenuPause[3].s, 128u, 16u, 255u, '\x01'},
    {kTethysMenuPause[4].x, kTethysMenuPause[4].y, kTethysMenuPause[4].s, 228u, 116u, 99u, '\x01'},
    {0, 0, nullptr, 0u, 0u, 0u, '\0'}};
#else
PauseMenu::PauseEntry pauseEntries_4CDE50[6] = {
    {184, 85, "CONTINUE", 128u, 16u, 255u, '\x01'},
    {184, 110, "SAVE", 128u, 16u, 255u, '\x01'},
    {184, 135, "CONTROLS", 128u, 16u, 255u, '\x01'},
    {184, 160, "QUIT", 128u, 16u, 255u, '\x01'},
    {184, 42, "- paused -", 228u, 116u, 99u, '\x01'},
    {0, 0, nullptr, 0u, 0u, 0u, '\0'}};
#endif

PauseMenu::PauseEntry PauseEntry2_4CDE98[2] = {
    {184, 205, "r1p01c01", 128u, 16u, 255u, '\x01'},
    {0, 0, nullptr, 0u, 0u, 0u, '\0'}};

#ifdef TETHYS_MENU_L10N
// [1] is rebuilt: the localized executables spell "intro"/"echap" out as PC key
// names, and the French one even carries kAO_Crouch where English has
// kAO_ConfirmContinue -- so the .inc's bytes would label the wrong pad button.
PauseMenu::PauseEntry quitEntries_4CDEA8[3] = {
    {kTethysMenuQuit[0].x, kTethysMenuQuit[0].y, kTethysMenuQuit[0].s, 128u, 16u, 255u, '\x01'},
    {kTethysMenuQuit[1].x, kTethysMenuQuit[1].y, TETHYS_MENU_QUIT_YESNO, 160u, 160u, 160u, '\x01'},
    {0, 0, nullptr, 0u, 0u, 0u, '\0'}};
#else
PauseMenu::PauseEntry quitEntries_4CDEA8[3] = {
    {184, 110, "REALLY QUIT?", 128u, 16u, 255u, '\x01'},
    {184, 135, kAO_ConfirmContinue " yes   " kAO_Esc " no", 160u, 160u, 160u, '\x01'},
    {0, 0, nullptr, 0u, 0u, 0u, '\0'}};
#endif

PauseMenu::PauseEntry saveEntries_4CDED0[4] = {
#ifdef TETHYS_SATURN
    // SATURN 390.ao.1: the typed save name moves 120 -> 150, to open a band for
    // the slot list above it.  MEASURED, not chosen: the menu atlas
    // (sFont1Atlas_4C56E8) carries letter glyphs 22-23 px tall with a 26 px
    // worst case, so a line at y occupies y..y+23 typically.  389.ao.1 put four
    // slots at 40/60/80/100 under a device name at 22 and that collides TWICE --
    // the device line (22..45) into slot 1 at 40, and slot 4 (100..123) into
    // this entry at 120.  Neither was ever seen, because the page fataled
    // before it finished drawing; both would have shipped behind the fix.
    //   150..173 still clears the "B save" hint at 180.
    {184, 150, "DUMMY_TEXT", 128u, 16u, 255u, '\x01'},
#else
    {184, 120, "DUMMY_TEXT", 128u, 16u, 255u, '\x01'},
#endif
#ifdef TETHYS_SATURN
    // SATURN: "enter"/"esc" are PC keyboard labels that do not exist on the pad.
    // Use the ENGINE'S OWN control bytes rather than hard-coded letters: every
    // entry string is run through String_FormatString_450DC0 (DrawEntries:728),
    // which substitutes kAO_ConfirmContinue / kAO_Esc via Input_GetButtonString
    // -> our Saturn table (src/sys_saturn.cxx:3652-3666). So these render "B" and
    // "Y" and they FOLLOW THE PAD MAPPING -- remap the pad and the label moves
    // with it, instead of drifting into a lie the way the hard-coded "B save /
    // C cancel" pair did the moment eBack left C.
    //   (The earlier note here claimed these glyphs "draw Cross/Triangle". That
    // was wrong twice over: the Euro atlas holds lettered oval keycaps A..H, not
    // PSX button art -- and the formatter substitutes them before the font ever
    // sees them, so no glyph is drawn at all.)
    #ifdef TETHYS_MENU_L10N
    // 425.ao.2: the same two lines with the executable's words.  The .inc's x/y
    // for these two already match what 390.ao.1 chose; entry [0] above keeps
    // 150, which is OURS and not in any executable.
    {kTethysMenuSave[1].x, kTethysMenuSave[1].y, TETHYS_MENU_SAVE_OK, 160u, 160u, 160u, '\x01'},
    {kTethysMenuSave[2].x, kTethysMenuSave[2].y, TETHYS_MENU_SAVE_CANCEL, 160u, 160u, 160u, '\x01'},
    #else
    {184, 180, kAO_ConfirmContinue "   save", 160u, 160u, 160u, '\x01'},
    {184, 205, kAO_Esc "   cancel", 160u, 160u, 160u, '\x01'},
    #endif
#else
    {184, 180, "enter   save", 160u, 160u, 160u, '\x01'},
    {184, 205, "esc   cancel", 160u, 160u, 160u, '\x01'},
#endif
    {0, 0, nullptr, 0u, 0u, 0u, '\0'}};

#ifdef TETHYS_MENU_L10N
// SATURN: 425.ao.2.  The seven GLYPH rows keep RELIVE's kAO_* macros and take
// only the .inc's COLUMN -- the executable's own bytes there are raw control
// codes for a PC keyboard, while the macros are substituted through our Saturn
// button table and follow a remapped pad.  The word rows take both.  French
// moves the two columns from 80/200 to 50/170 to fit "marcher discretement";
// at the English x it would run off the screen.
PauseMenu::PauseEntry controlsPageOne_4CDF00[17] = {
    {kTethysMenuControls[0].x, kTethysMenuControls[0].y, TETHYS_MENU_MORE_EXIT, 128u, 16u, 255u, '\x01'},
    {kTethysMenuControls[1].x, kTethysMenuControls[1].y, kTethysMenuControls[1].s, 127u, 127u, 127u, '\x01'},
    {kTethysMenuControls[2].x, kTethysMenuControls[2].y, kAO_Run " + " kAO_Left " " kAO_Right, 160u, 160u, 160u, '\0'},
    {kTethysMenuControls[3].x, kTethysMenuControls[3].y, kAO_Sneak " + " kAO_Left " " kAO_Right, 160u, 160u, 160u, '\0'},
    {kTethysMenuControls[4].x, kTethysMenuControls[4].y, kAO_Jump_Or_Hello " " kAO_Or " " kAO_Up, 160u, 160u, 160u, '\0'},
    {kTethysMenuControls[5].x, kTethysMenuControls[5].y, kAO_Crouch " " kAO_Or " " kAO_Down, 160u, 160u, 160u, '\0'},
    {kTethysMenuControls[6].x, kTethysMenuControls[6].y, kAO_Throw " + " kAO_DirectionalButtons, 160u, 160u, 160u, '\0'},
    {kTethysMenuControls[7].x, kTethysMenuControls[7].y, kAO_Action, 160u, 160u, 160u, '\0'},
    {kTethysMenuControls[8].x, kTethysMenuControls[8].y, kAO_Up, 160u, 160u, 160u, '\0'},
    {kTethysMenuControls[9].x, kTethysMenuControls[9].y, kTethysMenuControls[9].s, 128u, 16u, 255u, '\0'},
    {kTethysMenuControls[10].x, kTethysMenuControls[10].y, kTethysMenuControls[10].s, 128u, 16u, 255u, '\0'},
    {kTethysMenuControls[11].x, kTethysMenuControls[11].y, kTethysMenuControls[11].s, 128u, 16u, 255u, '\0'},
    {kTethysMenuControls[12].x, kTethysMenuControls[12].y, kTethysMenuControls[12].s, 128u, 16u, 255u, '\0'},
    {kTethysMenuControls[13].x, kTethysMenuControls[13].y, kTethysMenuControls[13].s, 128u, 16u, 255u, '\0'},
    {kTethysMenuControls[14].x, kTethysMenuControls[14].y, kTethysMenuControls[14].s, 128u, 16u, 255u, '\0'},
    {kTethysMenuControls[15].x, kTethysMenuControls[15].y, TETHYS_MENU_MOUNT, 128u, 16u, 255u, '\0'},
    {0, 0, nullptr, 0u, 0u, 0u, '\0'}};
#else
PauseMenu::PauseEntry controlsPageOne_4CDF00[17] = {
    {184, 205, kAO_ConfirmContinue " more  " kAO_Esc " exit", 128u, 16u, 255u, '\x01'},
    {184, 20, "Actions", 127u, 127u, 127u, '\x01'},
    {80, 50, kAO_Run " + " kAO_Left " " kAO_Right, 160u, 160u, 160u, '\0'},
    {80, 70, kAO_Sneak " + " kAO_Left " " kAO_Right, 160u, 160u, 160u, '\0'},
    {80, 90, kAO_Jump_Or_Hello " " kAO_Or " " kAO_Up, 160u, 160u, 160u, '\0'},
    {80, 110, kAO_Crouch " " kAO_Or " " kAO_Down, 160u, 160u, 160u, '\0'},
    {80, 130, kAO_Throw " + " kAO_DirectionalButtons, 160u, 160u, 160u, '\0'},
    {80, 150, kAO_Action, 160u, 160u, 160u, '\0'},
    {80, 170, kAO_Up, 160u, 160u, 160u, '\0'},
    {200, 50, "run", 128u, 16u, 255u, '\0'},
    {200, 70, "sneak", 128u, 16u, 255u, '\0'},
    {200, 90, "jump", 128u, 16u, 255u, '\0'},
    {200, 110, "crouch", 128u, 16u, 255u, '\0'},
    {200, 130, "throw", 128u, 16u, 255u, '\0'},
    {200, 150, "action", 128u, 16u, 255u, '\0'},
    {200, 170, "mount " kAO_Or " zturn", 128u, 16u, 255u, '\0'},
    {0, 0, nullptr, 0u, 0u, 0u, '\0'}};
#endif

#ifdef TETHYS_MENU_L10N
// SATURN: 425.ao.2.  Same rule as the controls page -- the eight chord rows and
// the header chord keep their kAO_* macros, the nine word rows take the
// executable's text.  Bonus that only shows here: the whistle lines carry the
// atlas markers 0x17/0x18 that RELIVE's compiled English had lost (it reads
// "whistle " with a trailing space and nothing after it); the menu sheet is
// font type 1, which does have those two rects.
PauseMenu::PauseEntry gamepadGameSpeak_4CDFD0[21] = {
    {kTethysMenuGameSpeak[0].x, kTethysMenuGameSpeak[0].y, TETHYS_MENU_EXIT, 128u, 16u, 255u, '\x01'},
    {kTethysMenuGameSpeak[1].x, kTethysMenuGameSpeak[1].y, kTethysMenuGameSpeak[1].s, 127u, 127u, 127u, '\x01'},
    {kTethysMenuGameSpeak[2].x, kTethysMenuGameSpeak[2].y, kAO_Speak1 " + " kAO_Speak2, 160u, 160u, 160u, '\x01'},
    {kTethysMenuGameSpeak[3].x, kTethysMenuGameSpeak[3].y, kTethysMenuGameSpeak[3].s, 128u, 16u, 255u, '\x01'},
    {kTethysMenuGameSpeak[4].x, kTethysMenuGameSpeak[4].y, kTethysMenuGameSpeak[4].s, 128u, 16u, 255u, '\0'},
    {kTethysMenuGameSpeak[5].x, kTethysMenuGameSpeak[5].y, kTethysMenuGameSpeak[5].s, 128u, 16u, 255u, '\0'},
    {kTethysMenuGameSpeak[6].x, kTethysMenuGameSpeak[6].y, kTethysMenuGameSpeak[6].s, 128u, 16u, 255u, '\0'},
    {kTethysMenuGameSpeak[7].x, kTethysMenuGameSpeak[7].y, kTethysMenuGameSpeak[7].s, 128u, 16u, 255u, '\0'},
    {kTethysMenuGameSpeak[8].x, kTethysMenuGameSpeak[8].y, kTethysMenuGameSpeak[8].s, 128u, 16u, 255u, '\0'},
    {kTethysMenuGameSpeak[9].x, kTethysMenuGameSpeak[9].y, kTethysMenuGameSpeak[9].s, 128u, 16u, 255u, '\0'},
    {kTethysMenuGameSpeak[10].x, kTethysMenuGameSpeak[10].y, kTethysMenuGameSpeak[10].s, 128u, 16u, 255u, '\0'},
    {kTethysMenuGameSpeak[11].x, kTethysMenuGameSpeak[11].y, kTethysMenuGameSpeak[11].s, 128u, 16u, 255u, '\0'},
    {kTethysMenuGameSpeak[12].x, kTethysMenuGameSpeak[12].y, kAO_Speak1 "+" kAO_Jump_Or_Hello, 160u, 160u, 160u, '\0'},
    {kTethysMenuGameSpeak[13].x, kTethysMenuGameSpeak[13].y, kAO_Speak1 "+" kAO_Throw, 160u, 160u, 160u, '\0'},
    {kTethysMenuGameSpeak[14].x, kTethysMenuGameSpeak[14].y, kAO_Speak1 "+" kAO_Crouch, 160u, 160u, 160u, '\0'},
    {kTethysMenuGameSpeak[15].x, kTethysMenuGameSpeak[15].y, kAO_Speak1 "+" kAO_Action, 160u, 160u, 160u, '\0'},
    {kTethysMenuGameSpeak[16].x, kTethysMenuGameSpeak[16].y, kAO_Speak2 "+" kAO_Jump_Or_Hello, 160u, 160u, 160u, '\0'},
    {kTethysMenuGameSpeak[17].x, kTethysMenuGameSpeak[17].y, kAO_Speak2 "+" kAO_Throw, 160u, 160u, 160u, '\0'},
    {kTethysMenuGameSpeak[18].x, kTethysMenuGameSpeak[18].y, kAO_Speak2 "+" kAO_Crouch, 160u, 160u, 160u, '\0'},
    {kTethysMenuGameSpeak[19].x, kTethysMenuGameSpeak[19].y, kAO_Speak2 "+" kAO_Action, 160u, 160u, 160u, '\0'},
    {0, 0, nullptr, 0u, 0u, 0u, '\0'}};
#else
PauseMenu::PauseEntry gamepadGameSpeak_4CDFD0[21] = {
    {184, 205, kAO_Esc " exit", 128u, 16u, 255u, '\x01'},
    {184, 20, "GameSpeak", 127u, 127u, 127u, '\x01'},
    {184, 55, kAO_Speak1 " + " kAO_Speak2, 160u, 160u, 160u, '\x01'},
    {184, 75, "chant", 128u, 16u, 255u, '\x01'},
    {100, 104, "hello", 128u, 16u, 255u, '\0'},
    {100, 126, "angry", 128u, 16u, 255u, '\0'},
    {100, 148, "wait", 128u, 16u, 255u, '\0'},
    {100, 170, "follow me", 128u, 16u, 255u, '\0'},
    {290, 104, "whistle ", 128u, 16u, 255u, '\0'},
    {290, 126, "fart", 128u, 16u, 255u, '\0'},
    {290, 148, "whistle ", 128u, 16u, 255u, '\0'},
    {290, 170, "laugh", 128u, 16u, 255u, '\0'},
    {2, 104, kAO_Speak1 "+" kAO_Jump_Or_Hello, 160u, 160u, 160u, '\0'},
    {2, 126, kAO_Speak1 "+" kAO_Throw, 160u, 160u, 160u, '\0'},
    {2, 148, kAO_Speak1 "+" kAO_Crouch, 160u, 160u, 160u, '\0'},
    {2, 170, kAO_Speak1 "+" kAO_Action, 160u, 160u, 160u, '\0'},
    {192, 104, kAO_Speak2 "+" kAO_Jump_Or_Hello, 160u, 160u, 160u, '\0'},
    {192, 126, kAO_Speak2 "+" kAO_Throw, 160u, 160u, 160u, '\0'},
    {192, 148, kAO_Speak2 "+" kAO_Crouch, 160u, 160u, 160u, '\0'},
    {192, 170, kAO_Speak2 "+" kAO_Action, 160u, 160u, 160u, '\0'},
    {0, 0, nullptr, 0u, 0u, 0u, '\0'}};
#endif

// Not localized, and deliberately: Input_JoyStickEnabled returns true in hard
// code on Saturn (src/sys_saturn.cxx), so VRender never takes the keyboard arm
// and these twenty-one entries are unreachable.  menutext.py skips them too.
PauseMenu::PauseEntry keyboardGameSpeak_4CE0D0[21] = {
    {184, 205, kAO_Esc " exit", 128u, 16u, 255u, '\x01'},
    {184, 20, "GameSpeak", 127u, 127u, 127u, '\x01'},
    {184, 55, "0", 160u, 160u, 160u, '\x01'},
    {184, 75, "chant", 128u, 16u, 255u, '\x01'},
    {90, 104, "hello", 128u, 16u, 255u, '\0'},
    {90, 126, "follow me", 128u, 16u, 255u, '\0'},
    {90, 148, "wait", 128u, 16u, 255u, '\0'},
    {90, 170, "angry", 128u, 16u, 255u, '\0'},
    {240, 104, "laugh", 128u, 16u, 255u, '\0'},
    {240, 126, "whistle ", 128u, 16u, 255u, '\0'},
    {240, 148, "fart", 128u, 16u, 255u, '\0'},
    {240, 170, "whistle ", 128u, 16u, 255u, '\0'},
    {52, 104, "1", 160u, 160u, 160u, '\0'},
    {52, 126, "2", 160u, 160u, 160u, '\0'},
    {52, 148, "3", 160u, 160u, 160u, '\0'},
    {52, 170, "4", 160u, 160u, 160u, '\0'},
    {202, 104, "5", 160u, 160u, 160u, '\0'},
    {202, 126, "6", 160u, 160u, 160u, '\0'},
    {202, 148, "7", 160u, 160u, 160u, '\0'},
    {202, 170, "8", 160u, 160u, 160u, '\0'},
    {0, 0, nullptr, 0u, 0u, 0u, '\0'}};

void PauseMenu::DrawEntries(PrimHeader** ppOt, PauseEntry* entry, s16 selectedEntryId, s32 polyOffset = 0)
{
    for (s16 entryId = 0; entry[entryId].field_4_strBuf; ++entryId)
    {
        s16 colourOffset;
        if (entryId == selectedEntryId && (field_126_page != 1 || field_132_always_0))
        {
            colourOffset = field_11E_selected_glow;
        }
        else
        {
            colourOffset = 0;
        }
        const char_type* stringBuffer;
        if (&entry[entryId] == &saveEntries_4CDED0[0])
        {
            stringBuffer = &saveNameBuffer_5080C6.characters[2];
        }
        else
        {
            stringBuffer = entry[entryId].field_4_strBuf;
        }
        if (!stringBuffer)
        {
            break;
        }
        char_type formattedString[128] = {};
        String_FormatString_450DC0(stringBuffer, formattedString);
        s16 clampedFontWidth;
        if (entry[entryId].field_B == 1)
        {
            s16 font_width_2 = static_cast<s16>(field_E4_font.MeasureWidth_41C2B0(formattedString));
            clampedFontWidth = font_width_2 >= 608 ? 16 : (entry[entryId].field_0_x - font_width_2 / 2);
        }
        else
        {
            clampedFontWidth = entry[entryId].field_0_x;
        }
        polyOffset = field_E4_font.DrawString_41C360(
            ppOt,
            formattedString,
            clampedFontWidth,
            entry[entryId].field_2_y,
            TPageAbr::eBlend_0,
            1,
            0,
            Layer::eLayer_Menu_41,
            static_cast<u8>(colourOffset + entry[entryId].field_8_r),
            static_cast<u8>(colourOffset + entry[entryId].field_9_g),
            static_cast<u8>(colourOffset + entry[entryId].field_A_b),
            polyOffset,
            FP_FromInteger(1),
            640,
            0);
    }
    Poly_F4* pPrim = &field_158[gPsxDisplay_504C78.field_A_buffer_index];
    PolyF4_Init(pPrim);
    Poly_Set_SemiTrans_498A40(&pPrim->mBase.header, 1);
    Poly_Set_Blending_498A00(&pPrim->mBase.header, 0);
    u8 color = 0x64;
    if (field_126_page != PauseMenuPages::ePause_0)
    {
        color = 160;
    }
    SetRGB0(pPrim, color, color, color);
    SetXY0(pPrim, 0, 0);
    SetXY1(pPrim, 640, 0);
    SetXY2(pPrim, 0, 240);
    SetXY3(pPrim, 640, 240);
    Prim_SetTPage* prim_tpage = &field_138_tPage[gPsxDisplay_504C78.field_A_buffer_index];
    Init_SetTPage_495FB0(prim_tpage, 0, 0, PSX_getTPage_4965D0(TPageMode::e4Bit_0, TPageAbr::eBlend_2, 0, 0));
    OrderingTable_Add_498A80(OtLayer(ppOt, Layer::eLayer_Menu_41), &pPrim->mBase.header);
    OrderingTable_Add_498A80(OtLayer(ppOt, Layer::eLayer_Menu_41), &prim_tpage->mBase);
    pScreenManager_4FF7C8->InvalidateRect_406E40(
        0,
        0,
        640,
        240,
        pScreenManager_4FF7C8->field_2E_idx);
}

void PauseMenu::VRender_44E6F0(PrimHeader** ppOt)
{
#ifdef TETHYS_SATURN
    // SATURN: belt-and-braces with the VUpdate_44DFB0 guard -- every branch
    // below measures/draws text through the shared atlas (null until S8).
    // VRender can also fire from the modal loop's drawables walk if anything
    // external sets eDrawable_Bit4.
    if (!sFontContext_4FFD68.field_8_atlas_array)
    {
        return;
    }
#endif
    switch (field_126_page)
    {
        case PauseMenuPages::ePause_0:
        case PauseMenuPages::eQuit_3:
        {
            PauseEntry* entries = &quitEntries_4CDEA8[0];
            if (field_126_page != PauseMenuPages::eQuit_3)
            {
                entries = &pauseEntries_4CDE50[0];
            }
            char_type cameraNameBuffer[48] = {};
            Path_Format_CameraName_4346B0(
                cameraNameBuffer,
                gMap_507BA8.field_0_current_level,
                gMap_507BA8.field_2_current_path,
                gMap_507BA8.field_4_current_camera);
            cameraNameBuffer[8] = 0;
            if (strlen(cameraNameBuffer) != 0)
            {
                for (unsigned idx = 0; idx < strlen(cameraNameBuffer); idx++)
                {
                    s8 letter = cameraNameBuffer[idx];
                    s8 letterCandidate = 0;
                    if (letter < 'A' || letter > 'Z')
                    {
                        if (letter < '0' || letter > '9')
                        {
                            continue;
                        }
                        letterCandidate = letter - 26;
                    }
                    else
                    {
                        letterCandidate = letter | ' ';
                    }
                    cameraNameBuffer[idx] = letterCandidate;
                }
            }
            auto polyOffset = field_E4_font.DrawString_41C360(
                ppOt,
                cameraNameBuffer,
                static_cast<s16>(PauseEntry2_4CDE98[0].field_0_x - field_E4_font.MeasureWidth_41C2B0(cameraNameBuffer) / 2),
                PauseEntry2_4CDE98[0].field_2_y,
                TPageAbr::eBlend_0,
                1,
                0,
                Layer::eLayer_Menu_41,
                128,
                16,
                255,
                0,
                FP_FromInteger(1),
                640,
                0);
            DrawEntries(ppOt, entries, field_124, polyOffset);
            break;
        }
        case PauseMenuPages::eSave_1:
        {
#ifdef TETHYS_SATURN
            // SATURN (ao261.22): the save-result banner, drawn over the page
            // for the hold set in VUpdate.  Green-ish for success, red for
            // failure -- the tint is the SetRGB0 modulation of the font's grey
            // ramp, so these are the actual on-screen colours, not names.
            //
            // ao261.24 -- IT MUST BE DRAWN *FIRST*, AND ITS polyOffset FED TO
            // DrawEntries.  ao261.22/.23 drew it AFTER DrawEntries and passed a
            // literal polyOffset of 0, which is not a cosmetic slip: the font
            // owns ONE prim pool of 175 entries (ctor_41C170 at line 76) and
            // polyOffset is the bump cursor into it.  Restarting at 0 re-issued
            // prims that DrawEntries had already linked into the ordering table
            // this frame, and adding an already-linked PrimHeader to an OT
            // bucket closes a LOOP in the tag chain.  The renderer's walk then
            // ran until its 100,000-step cap and died on "OT walk runaway
            // (cyclic tag chain)" -- the fatal the tester photographed the
            // moment they pressed save.  The cap did its job; the caller was
            // wrong.  DrawEntries returns void, so the only ordering that can
            // thread the cursor correctly is banner-then-entries.
            s32 savePolyOffset = 0;
            if (sTethysSaveMsg)
            {
                const char_type* msg = (sTethysSaveMsg == 1) ? TETHYS_MENU_SAVED : TETHYS_MENU_SAVE_FAILED; // 425.ao.2
                const u8 r = (sTethysSaveMsg == 1) ? 96u : 255u;
                const u8 g = (sTethysSaveMsg == 1) ? 255u : 64u;
                const u8 b = (sTethysSaveMsg == 1) ? 96u : 64u;
                // 390.ao.1: 150 -> 16.  The name moved into 150 (see
                // saveEntries_4CDED0), and 16 is the device line's row -- the
                // banner TAKES that row rather than sharing one, because the
                // device is already chosen by then and which slot now holds the
                // save is the thing worth still seeing.  The slot list stays up
                // underneath, so "SAVED" and the filled slot read together.
                savePolyOffset = Tethys_DrawLine(field_E4_font, ppOt, msg, 16,
                                                 r, g, b, savePolyOffset);
            }
            // 389.ao.1 THE SLOT LIST, drawn BETWEEN the banner and DrawEntries.
            //
            // That position is forced, not chosen: DrawEntries returns void, so
            // it can only ever be last in the polyOffset chain.
            //
            // 390.ao.1 -- every line here goes through Tethys_DrawLine, which
            // takes the running cursor as its LAST argument and returns the new
            // one.  389.ao.1 wrote these calls out longhand and mis-slotted that
            // cursor into colorRandomRange, which is the fatal documented above;
            // see the wrapper's own note.  Nothing in this block may call
            // DrawString_41C360 directly again.
            //
            // One line per slot, "N title" or "N -- LIBRE --", with the device
            // named above them so the player can see WHERE this is going -- the
            // device sweep already exists and picking a slot without knowing
            // the device would only be half the choice.
            {
                const s32 mask = sTethysSlotMask;
                const s32 n = sTethysSlotN;
                const char_type* dev = Tethys_SaveDeviceName(Tethys_SaveDeviceGet());
                if (dev && !sTethysSaveMsg)
                {
                    savePolyOffset = Tethys_DrawLine(field_E4_font, ppOt, dev, 16,
                                                     128u, 128u, 128u, savePolyOffset);
                }
                for (s32 i = 0; i < n; i++)
                {
                    char_type line[32];
                    line[0] = static_cast<char_type>('1' + i);
                    line[1] = ' ';
                    s32 w = 2;
                    if (mask & (1 << i))
                    {
                        const char_type* title = sTethysSlotTitle[i];
                        for (s32 k = 0; title[k] && w < 30; k++)
                        {
                            line[w++] = title[k];
                        }
                    }
                    if (w == 2)
                    {
                        // 425.ao.2: was French on the Spanish and English discs too.
                        const char_type* empty = TETHYS_MENU_SLOT_FREE;
                        for (s32 k = 0; empty[k] && w < 30; k++)
                        {
                            line[w++] = empty[k];
                        }
                    }
                    line[w] = 0;
                    // TRIM TO WHAT THE SCREEN HOLDS, by width and not by count.
                    // AO builds the default save name from the level, path and
                    // camera -- "RUPTUREFARMS 1 EMBALLAGE" and the like -- and
                    // measured in the font's own units that is 425 wide against
                    // a 368-wide screen, so centring it puts x at -28 and the
                    // first characters fall off the left edge.  A character cap
                    // cannot fix that: 'W' is 17 wide and 'l' is 7, so the same
                    // count is 2.4x the pixels depending on the name.  Drop
                    // characters until it fits, keeping the "N " prefix.
                    while (w > 2 && field_E4_font.MeasureWidth_41C2B0(line) > 352)
                    {
                        line[--w] = 0;
                    }
                    // The highlight is the font's own grey ramp modulated by
                    // SetRGB0, the same mechanism the result banner uses, so
                    // these are the real on-screen colours and not names.
                    const bool sel = (i == sTethysSlot);
                    // 390.ao.1 -- 23 px steps from 44, MEASURED against the
                    // atlas rather than copied from a neighbouring page.
                    // sFont1Atlas_4C56E8's letters are 22-23 px tall, so the
                    // 20 px steps 389.ao.1 borrowed from controlsPageOne_4CDF00
                    // overlapped every line with the next by 3 px -- that page
                    // gets away with it because its rows are short labels with
                    // few descenders, and this one would not have.
                    //   The box is EXACTLY 23 for every character a slot line
                    // can hold: the title charset is restricted to
                    // [A-Za-z0-9 !-] (the VK_ default case below), and in
                    // sFont1Atlas_4C56E8 every digit and capital is 23 tall
                    // while '-' is 11 and lowercase is shorter.  The atlas's
                    // 26 px entries are lettered keycaps and control codes,
                    // which cannot appear here.  So 25 = 23 + a 2 px gap.
                    //   Band: device/banner 16..39, slots 44/69/94/119 with the
                    // last ending at 142, save name 150..173, "B save" at 180.
                    savePolyOffset = Tethys_DrawLine(
                        field_E4_font, ppOt, line, static_cast<s16>(44 + i * 25),
                        sel ? 255u : 112u, sel ? 255u : 112u, sel ? 160u : 112u,
                        savePolyOffset);
                }
            }
            DrawEntries(ppOt, &saveEntries_4CDED0[0], -1, savePolyOffset);
#else
            DrawEntries(ppOt, &saveEntries_4CDED0[0], -1);
#endif
            break;
        }
        case PauseMenuPages::eControls_2:
        {
            PauseEntry* entries = nullptr;
            if (field_128_controller_id == 1)
            {
                if (Input().JoyStickEnabled())
                {
                    entries = &gamepadGameSpeak_4CDFD0[0];
                }
                else
                {
                    entries = &keyboardGameSpeak_4CE0D0[0];
                }
            }
            else
            {
                entries = &controlsPageOne_4CDF00[0] + field_128_controller_id;
            }
            DrawEntries(ppOt, entries, 0);
            break;
        }
        default:
        {
            ALIVE_FATAL("Unknown menu page!");
        }
    }
}

} // namespace AO
