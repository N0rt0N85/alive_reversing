#include "stdafx_ao.h"
#include "Function.hpp"
#include "StringFormatters.hpp"
#include "Input.hpp"

namespace AO {

const s32 dword_4CEE78[30] = {
    // NOTE: diversion from OG!
    // the sneak - speak1 and run - speak2 pairs are now decoupled
    // so that they can each be remapped to separate buttons
    InputCommands::eBack,
    InputCommands::eCheatMode,
    InputCommands::eLeftGamespeak,
    InputCommands::eRightGameSpeak,
    InputCommands::eRun,
    InputCommands::eSneak,
    InputCommands::eHop,
    InputCommands::eDoAction,
    InputCommands::eThrowItem,
    InputCommands::eCrouchOrRoll,
    InputCommands::eUp,
    InputCommands::eDown,
    InputCommands::eLeft,
    InputCommands::eRight,
    0,
    0,
    InputCommands::eUnPause_OrConfirm,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0};

const char_type* kButtonNamesOrAtlasNums_4CEDA8[] = {
    "esc",
    "tab",
    "alt",
    "shift",
    "shift",
    "alt",
    "space",
    "ctrl",
    "Z",
    "X",
    kAO_Up,
    kAO_Down,
    kAO_Left,
    kAO_Right,
    kAO_Or,
    kAO_DirectionalButtons,
    "\x16",
    "\x17",
    "\x18",
    "\x19",
    "\x1A",
    "\x1B",
    "\x1C",
    "\x1D",
    "\x1E",
    "\x1F",
    "esc",
    "tab",
    kAO_Action,
    kAO_Jump_Or_Hello,
    kAO_Jump_Or_Hello,
    kAO_Action,
    kAO_Speak1,
    kAO_Speak2,
    kAO_Run,
    kAO_Sneak,
    kAO_Up,
    kAO_Down,
    kAO_Left,
    kAO_Right,
    kAO_Or,
    kAO_DirectionalButtons,
    "\x16",
    "\x17",
    "\x18",
    "\x19",
    "\x1A", // corruption
    "\x1B", // blank
    "\x1C", // blank
    "\x1D", // blank
    "\x1E", // blank
    "\x1F", // blank
    "\x2D", // todo: might not be part of this
};


EXPORT void CC String_FormatString_450DC0(const char_type* pInput, char_type* pOutput)
{
    char_type* pOutIter = pOutput;
    const char_type* pInIter = pInput;
    while (1)
    {
#ifdef TETHYS_SATURN
        // SATURN (bt998): READ THE BYTE UNSIGNED. This one line is the whole
        // "French black screen".
        // char_type is plain `char` (AliveLibCommon/Types.hpp:7) and this port
        // forces -fsigned-char (Makefile:99), so every CP437 accent in the
        // localized message table -- 0x82 0x85 0x87 0x88 0x8A 0x90 0x93 0x96,
        // e-acute, a-grave, c-cedilla, e-circumflex, e-grave, E-acute,
        // o-circumflex, u-circumflex -- arrives here NEGATIVE. It therefore
        // fails `in_char >= ' '`, falls into the control-code branch meant for
        // button glyphs, and ends up indexing kButtonNamesOrAtlasNums_4CEDA8
        // (53 entries) at 124: the garbage pointer that comes back is strcpy'd
        // into LCDScreen's 512-byte field_AC_message_buffer until it happens to
        // meet a zero byte. Arbitrary source, unbounded length, straight over
        // the object and whatever follows it -- no fatal, no exception, just a
        // black screen the moment the first accented marquee message is
        // formatted. Upstream is not wrong for the PC: the compiled English
        // table is pure ASCII, so this path was never reachable there.
        const u8 in_char = static_cast<u8>(*pInIter);
#else
        const char_type in_char = *pInIter;
#endif
        if (!*pInIter)
        {
            break;
        }

        if (in_char >= ' ')
        {
            *pOutIter++ = static_cast<char_type>(in_char);
            pInIter++;
        }
        else
        {
            const s32 in_char_m6 = static_cast<s32>(in_char) - 6;
            const char_type* pConverted = nullptr;

#ifdef TETHYS_SATURN
            // SATURN (bt998): and do not index either table out of range. With
            // the byte read unsigned this can only be a control code below 6
            // (nothing legal: the button glyphs are 0x08..0x13), but the OG
            // reached kButtonNamesOrAtlasNums_4CEDA8 with a NEGATIVE index by
            // construction on that branch, and a wild pointer here costs the
            // whole session. Drop the byte instead.
            if (in_char_m6 < 0 || in_char_m6 >= static_cast<s32>(ALIVE_COUNTOF(kButtonNamesOrAtlasNums_4CEDA8)))
            {
                pInIter++;
                continue;
            }
#endif

            // NOTE: diversion from OG!
            if (in_char == kAO_ConfirmContinue[0])
            {
                pConverted = Input_GetButtonString(static_cast<InputCommands>(dword_4CEE78[in_char_m6]));
            }
            else if (in_char_m6 < 0 || in_char_m6 >= 14)
            {
                pConverted = kButtonNamesOrAtlasNums_4CEDA8[in_char_m6];
            }
            else
            {
                pConverted = Input_GetButtonString(static_cast<InputCommands>(dword_4CEE78[in_char_m6]));
            }

            strcpy(pOutIter, pConverted);

            if (*pOutIter)
            {
                char_type next_char = 0;
                do
                {
                    next_char = (pOutIter++)[1];
                }
                while (next_char);
                pInIter++;
            }
            else
            {
                pInIter++;
            }
        }
    }

    // Null terminate destination string
    *pOutIter = 0;
}

} // namespace AO
