#include "stdafx_ao.h"
#include "Font.hpp"
#include "Function.hpp"
#include "ResourceManager.hpp"
#include "VRam.hpp"
#include "FixedPoint.hpp"
#include "Sys_common.hpp"
#include "Primitives.hpp"
#include "PsxDisplay.hpp"
#include "../AliveLibCommon/FunctionFwd.hpp"
#include "ScreenManager.hpp"
#include "Math.hpp"
#include "../AliveLibAE/Renderer/IRenderer.hpp"

namespace AO {

ALIVE_VAR(1, 0x4FFD68, FontContext, sFontContext_4FFD68, {});

ALIVE_VAR(1, 0x5080E4, s16, sDisableFontFlicker_5080E4, 0);
ALIVE_VAR(1, 0x508BF4, u8, sFontDrawScreenSpace_508BF4, 0);

const Font_AtlasEntry sFont1Atlas_4C56E8[116] = {
    {0, 0, 2, 0},
    {0, 0, 9, 0},
    {43, 80, 6, 23},
    {182, 0, 11, 10},
    {182, 0, 11, 10},
    {182, 0, 11, 10},
    {182, 0, 11, 10},
    {182, 0, 11, 10},
    {85, 96, 7, 8},
    {52, 79, 11, 23},
    {64, 79, 11, 23},
    {186, 0, 11, 10},
    {0, 79, 23, 17},
    {86, 79, 7, 23},
    {75, 79, 10, 11},
    {94, 79, 6, 23},
    {149, 79, 17, 23},
    {193, 51, 17, 23},
    {32, 51, 17, 23},
    {50, 51, 17, 23},
    {69, 51, 17, 23},
    {87, 51, 17, 23},
    {106, 51, 17, 23},
    {124, 51, 17, 23},
    {142, 51, 17, 23},
    {158, 51, 17, 23},
    {176, 51, 17, 23},
    {110, 79, 7, 22},
    {102, 79, 7, 22},
    {61, 79, 10, 23},
    {25, 79, 15, 16},
    {73, 79, 10, 23},
    {118, 79, 14, 23},
    {182, 0, 11, 10},
    {9, 0, 17, 23},
    {26, 0, 15, 23},
    {41, 0, 15, 23},
    {56, 0, 16, 23},
    {73, 0, 12, 23},
    {86, 0, 12, 23},
    {98, 0, 15, 23},
    {114, 0, 16, 23},
    {132, 0, 7, 23},
    {139, 0, 14, 23},
    {154, 0, 15, 23},
    {170, 0, 12, 23},
    {0, 25, 21, 23},
    {21, 25, 16, 23},
    {37, 25, 18, 23},
    {56, 25, 14, 23},
    {71, 25, 18, 23},
    {90, 25, 14, 23},
    {106, 25, 14, 23},
    {121, 25, 15, 23},
    {137, 25, 16, 23},
    {153, 25, 17, 23},
    {170, 25, 20, 23},
    {191, 25, 15, 23},
    {0, 50, 17, 23},
    {17, 50, 13, 23},
    {61, 79, 10, 23},
    {131, 79, 17, 23},
    {73, 79, 10, 23},
    {186, 0, 11, 10},
    {86, 79, 27, 10},
    {186, 0, 7, 11},
    {0, 110, 16, 22},
    {17, 110, 13, 22},
    {31, 110, 13, 22},
    {44, 110, 15, 22},
    {59, 110, 11, 22},
    {71, 110, 11, 22},
    {83, 110, 13, 22},
    {97, 110, 14, 22},
    {113, 110, 7, 22},
    {120, 110, 11, 22},
    {134, 110, 13, 22},
    {149, 110, 11, 22},
    {0, 137, 17, 22},
    {19, 137, 15, 22},
    {34, 137, 17, 22},
    {52, 137, 13, 22},
    {66, 137, 17, 22},
    {84, 137, 13, 22},
    {97, 137, 12, 22},
    {111, 137, 14, 22},
    {125, 137, 14, 22},
    {141, 137, 15, 22},
    {157, 137, 17, 22},
    {1, 164, 13, 22},
    {16, 164, 15, 22},
    {32, 164, 12, 22},
    {0, 186, 36, 22},
    {39, 186, 36, 22},
    {78, 186, 36, 22},
    {117, 186, 36, 22},
    {156, 186, 36, 22},
    {0, 210, 36, 22},
    {39, 210, 36, 22},
    {78, 210, 36, 22},
    {189, 124, 30, 19},
    {221, 124, 30, 20},
    {224, 143, 25, 21},
    {193, 143, 25, 21},
    {162, 110, 19, 22},
    {211, 0, 44, 26},
    {196, 164, 16, 22},
    {48, 164, 16, 22},
    {64, 164, 16, 22},
    {81, 164, 16, 22},
    {98, 164, 16, 22},
    {115, 164, 16, 22},
    {131, 164, 16, 22},
    {148, 164, 16, 22},
    {163, 164, 16, 22},
    {180, 164, 16, 22}};

const Font_AtlasEntry sFont2Atlas_4C58B8[104] = {
    {0, 0, 2, 0},
    {0, 0, 14, 0},
    {105, 42, 14, 13},
    {105, 70, 14, 4},
    {0, 0, 14, 0},
    {75, 70, 14, 13},
    {0, 0, 14, 0},
    {0, 0, 14, 0},
    {105, 70, 8, 5},
    {45, 70, 14, 13},
    {60, 70, 14, 13},
    {105, 84, 14, 0},
    {75, 84, 14, 13},
    {45, 42, 14, 13},
    {60, 84, 14, 13},
    {30, 42, 14, 13},
    {0, 0, 14, 0},
    {0, 56, 14, 13},
    {15, 56, 14, 13},
    {30, 56, 14, 13},
    {45, 56, 14, 13},
    {60, 56, 14, 13},
    {75, 56, 14, 13},
    {90, 56, 14, 13},
    {105, 56, 14, 13},
    {0, 70, 14, 13},
    {15, 70, 14, 13},
    {60, 42, 14, 13},
    {75, 42, 14, 13},
    {45, 70, 14, 13},
    {30, 70, 14, 13},
    {60, 70, 14, 13},
    {90, 42, 14, 13},
    {0, 0, 14, 0},
    {0, 0, 14, 13},
    {15, 0, 14, 13},
    {30, 0, 14, 13},
    {45, 0, 14, 13},
    {60, 0, 14, 13},
    {75, 0, 14, 13},
    {90, 0, 14, 13},
    {105, 0, 14, 13},
    {0, 14, 14, 13},
    {15, 14, 14, 13},
    {30, 14, 14, 13},
    {45, 14, 14, 13},
    {60, 14, 14, 13},
    {75, 14, 14, 13},
    {90, 14, 14, 13},
    {105, 14, 14, 13},
    {0, 28, 14, 13},
    {15, 28, 14, 13},
    {30, 28, 14, 13},
    {45, 28, 14, 13},
    {60, 28, 14, 13},
    {75, 28, 14, 13},
    {90, 28, 14, 13},
    {105, 28, 14, 13},
    {0, 42, 14, 13},
    {15, 42, 14, 13},
    {45, 70, 14, 13},
    {90, 70, 14, 13},
    {60, 70, 14, 13},
    {0, 0, 14, 0},
    {0, 0, 14, 0},
    {105, 70, 7, 4},
    {0, 0, 14, 13},
    {15, 0, 14, 13},
    {30, 0, 14, 13},
    {45, 0, 14, 13},
    {60, 0, 14, 13},
    {75, 0, 14, 13},
    {90, 0, 14, 13},
    {105, 0, 14, 13},
    {0, 14, 14, 13},
    {15, 14, 14, 13},
    {30, 14, 14, 13},
    {45, 14, 14, 13},
    {60, 14, 14, 13},
    {75, 14, 14, 13},
    {90, 14, 14, 13},
    {105, 14, 14, 13},
    {0, 28, 14, 13},
    {15, 28, 14, 13},
    {30, 28, 14, 13},
    {45, 28, 14, 13},
    {60, 28, 14, 13},
    {75, 28, 14, 13},
    {90, 28, 14, 13},
    {105, 28, 14, 13},
    {0, 42, 14, 13},
    {15, 42, 14, 13},
    {0, 98, 20, 15},
    {20, 98, 20, 15},
    {40, 98, 20, 15},
    {60, 98, 20, 15},
    {80, 98, 20, 15},
    {0, 115, 20, 15},
    {20, 115, 20, 15},
    {40, 115, 20, 15},
    {0, 132, 20, 15},
    {20, 132, 20, 15},
    {40, 132, 20, 15},
    {60, 132, 20, 15}};


#ifdef TETHYS_SATURN
// SATURN: fonts whose FntP poly block failed to allocate (dead fonts) --
// shown on the death screen / overlay (src/sys_saturn.cxx row 12 "F").
extern "C" volatile u32 Tethys_gFontNullPolys = 0;

// SATURN (S8): the localized (French / Spanish) atlas tables, extracted from
// the shipped AbeWin.exe.  The Saturn build ships localized game data ONLY
// (build.ps1 emits Tethys.bin from the FR install and Tethys_ES.bin from the
// ES one), and the localized FONT SHEETS are laid out differently from the US
// ones, so the US sFont*Atlas_* tables above are simply wrong for our data.
#include "tethys_euro_atlas.inc"

// SATURN: atlas index rule, ONE definition for the four call sites below.
// The Euro tables push the control block from index 92 (US) to 145, so the
// control bias goes 84 -> 137 and the printable ceiling 122 -> 175:
//
//   uc = (u8) c                          <- MUST be unsigned, see below
//   uc <= 32 || uc > 175  ->  (uc < 8 || uc > 31) ? 1 (blank) : uc + 137
//   otherwise             ->  uc - 31
//
// The (u8) cast is load-bearing: char_type is plain `char`
// (AliveLibCommon/Types.hpp:7) and sh2eb-elf GCC makes plain char SIGNED, so
// every CP437 accent (0x80..0xAF) is NEGATIVE here and would take the `<= 32`
// control branch and render as a space -- a correct atlas rendered blank.
enum : s32
{
    kTethysAtlasBlankIdx = 1, // entry 1 = the blank / fallback advance
};

static s32 Tethys_AtlasIndex(u8 uc)
{
    if (uc <= 32 || uc > kTethysEuroFontPrintableHi)
    {
        if (uc < 8 || uc > 31)
        {
            return kTethysAtlasBlankIdx;
        }
        return static_cast<s32>(uc) + kTethysEuroFontCtrlBias;
    }
    return static_cast<s32>(uc) - 31;
}

// SATURN: out-of-range atlas indices caught by Tethys_AtlasIndexChecked
// (nonzero = a control code the loaded sheet has no glyph for -- font2 only
// carries chars 8..19, so e.g. kAO_Or ("\x14") on an LCD screen lands here).
extern "C" volatile u32 Tethys_gFontAtlasClamps = 0;

// The two tables have DIFFERENT lengths (169 vs 157) and FontContext cannot
// grow a count field (LCDScreen/LCDStatusBoard/GasCountDown/MainMenu all have
// ALIVE_ASSERT_SIZEOF over it), so the length is re-derived from the resource
// id that selected the table in LoadFontType_41C040.  An unchecked index into
// a const array on SH-2 reads arbitrary .rodata -> arbitrary VRAM rects.
static s32 Tethys_AtlasIndexChecked(const FontContext* pCtx, s32 idx)
{
    const s32 count = (pCtx->field_C_resource_id == 1)
                          ? static_cast<s32>(ALIVE_COUNTOF(kTethysFont1AtlasEuro))
                          : static_cast<s32>(ALIVE_COUNTOF(kTethysFont2AtlasEuro));
    if (idx < 0 || idx >= count)
    {
        Tethys_gFontAtlasClamps = Tethys_gFontAtlasClamps + 1;
        return kTethysAtlasBlankIdx;
    }
    return idx;
}

// SATURN: compiled font palettes (AO/LCDScreen.cpp:21-87,
// AO/LCDStatusBoard.cpp sStatsSignFontPalette_4CD570, AO/GasCountDown.cpp
// byte_4C5080, AO/PauseMenu.cpp byte_4C5EE8, AO/MainMenu.cpp sFontPal_4D0090)
// are PSX u16s frozen as u8[32] LITTLE-endian byte pairs.  The Saturn
// PalSetData (src/renderer_saturn.cxx:3784) assembles each entry BIG-endian
// ((src[0] << 8) | src[1]) because every OTHER palette it sees comes from the
// offline converter, which already emits CRAM-ready big-endian entries.  So
// these compiled ones must be swapped -- and put through the project CRAM
// value rule (tools/converter/common.py sat_cram): 0x0000 stays transparent,
// anything else gets the Saturn opaque bit.  Entry 0 is {0,0} in every one of
// these palettes, so transparency is preserved.
void Tethys_PalSetCompiled(s16 palX, s16 palY, s16 depth, const u8* pPalette)
{
    u8 swapped[32];
    s16 entries = depth;
    if (entries > 16)
    {
        entries = 16; // compiled palettes are u8[32] = 16 entries, never more
    }
    for (s16 i = 0; i < entries; i++)
    {
        const u16 psx = static_cast<u16>(static_cast<u16>(pPalette[2 * i]) | (static_cast<u16>(pPalette[(2 * i) + 1]) << 8));
        const u16 sat = (psx == 0) ? static_cast<u16>(0) : static_cast<u16>(0x8000u | (psx & 0x7FFFu));
        swapped[2 * i] = static_cast<u8>(sat >> 8);
        swapped[(2 * i) + 1] = static_cast<u8>(sat & 0xFF);
    }
    IRenderer::GetRenderer()->PalSetData(IRenderer::PalRecord{palX, palY, entries}, swapped);
}
#endif

void CC FontContext::static_ctor_41C010()
{
    atexit(static_dtor_41C020);
}

void CC FontContext::static_dtor_41C020()
{
    sFontContext_4FFD68.dtor_41C110();
}

void FontContext::LoadFontType_41C040(s16 resourceID)
{
    field_C_resource_id = resourceID;
    auto loadedResource = ResourceManager::GetLoadedResource_4554F0(ResourceManager::Resource_Font, resourceID, 1, 0);
    auto fontFile = reinterpret_cast<File_Font*>(*loadedResource);

    vram_alloc_450B20(fontFile->field_0_width, fontFile->field_2_height, fontFile->field_4_color_depth, &field_0_rect);

    const PSX_RECT rect = {field_0_rect.x, field_0_rect.y, static_cast<s16>(fontFile->field_0_width / 4), fontFile->field_2_height};

    IRenderer::GetRenderer()->Upload(fontFile->field_4_color_depth == 16 ? IRenderer::BitDepth::e16Bit : IRenderer::BitDepth::e4Bit, rect, fontFile->field_28_pixel_buffer);

    // Free our loaded font resource as its now in vram
    ResourceManager::FreeResource_455550(loadedResource);

    switch (resourceID)
    {
        case 1:
#ifdef TETHYS_SATURN
            // SATURN (S8): localized sheet -> localized table (see the
            // tethys_euro_atlas.inc banner). field_C_resource_id, set above,
            // is what Tethys_AtlasIndexChecked re-derives the length from.
            field_8_atlas_array = kTethysFont1AtlasEuro;
#else
            field_8_atlas_array = sFont1Atlas_4C56E8;
#endif
            break;
        case 2:
#ifdef TETHYS_SATURN
            field_8_atlas_array = kTethysFont2AtlasEuro;
#else
            field_8_atlas_array = sFont2Atlas_4C58B8;
#endif
            break;
        default:
            ALIVE_FATAL("Unknown font resource ID !!!");
            break;
    }
}

void FontContext::dtor_41C110()
{
    if (field_0_rect.x > 0)
    {
        Vram_free_450CE0(
            {field_0_rect.x, field_0_rect.y},
            {field_0_rect.w, field_0_rect.h});
    }
}

AliveFont* AliveFont::ctor_41C170(s32 maxCharLength, const u8* palette, FontContext* fontContext)
{
    field_34_font_context = fontContext;

    IRenderer::PalRecord rec;
    rec.depth = 16;
    if (!IRenderer::GetRenderer()->PalAlloc(rec))
    {
        LOG_ERROR("PalAlloc failed");
    }

#ifdef TETHYS_SATURN
    // SATURN: `palette` is always a compiled PSX little-endian u8[32] (every
    // caller passes one: LCDScreen, LCDStatusBoard, GasCountDown, PauseMenu,
    // MainMenu) -- swap it before it reaches the big-endian PalSetData.
    Tethys_PalSetCompiled(rec.x, rec.y, rec.depth, palette);
#else
    IRenderer::GetRenderer()->PalSetData(rec, palette);
#endif

    field_28_palette_rect.x = rec.x;
    field_28_palette_rect.y = rec.y;
    field_28_palette_rect.w = rec.depth;
    field_28_palette_rect.h = 1;

    field_30_poly_count = maxCharLength;
    // SATURN (ao262.4): NON-FATAL, so the block just below can finally run.
    // The tester's fatal named this exact allocation -- FntP id 2, 5,760 B,
    // against a no-cart heap at us933948 / cap942420 = 99.10% -- and the
    // degrade path underneath has existed since S4 without ever being
    // reachable, because Allocate_New_Locked_Resource_454F80 fatals first.
    // That is also why `df` has read 00 on every capture ever taken: the gauge
    // was watching a path that could not execute. Same locked/eLastMatching
    // allocation as before, reclaim retry kept, null returned instead of death.
    field_20_fnt_poly_block_ptr = ResourceManager::Alloc_New_Resource_ImplEx(
        ResourceManager::Resource_FntP,
        fontContext->field_C_resource_id,
        sizeof(Poly_FT4) * 2 * maxCharLength,
        true, ResourceManager::BlockAllocMethod::eLastMatching,
        true /*reclaim*/, false /*never fatal*/);
#ifdef TETHYS_SATURN
    // SATURN: third instance of the unchecked-null-handle class (after FG1's
    // CHNK block): on a heap at peak this alloc returns null and the *null
    // below reads the BIOS reset vector -- DrawString then queued "polys"
    // at 0x20000200 into the OT (the S4 "OTa 20000200" fatal, LCDScreen).
    // Degrade instead of dying: a dead font draws nothing (PauseMenu's
    // 175-char block may legitimately fail until fonts land in S8).
    if (!field_20_fnt_poly_block_ptr)
    {
        Tethys_gFontNullPolys = Tethys_gFontNullPolys + 1;
        field_24_fnt_poly_array = nullptr;
        field_30_poly_count = 0;
        return this;
    }
#endif
    field_24_fnt_poly_array = reinterpret_cast<Poly_FT4*>(*field_20_fnt_poly_block_ptr);
    return this;
}

EXPORT u32 AliveFont::MeasureWidth_41C2B0(const char_type* text)
{
    s32 result = 0;

    // SATURN: same quadratic strlen-as-loop-condition as DrawString_41C360 --
    // see the block there. MeasureWidth runs per string on the layout path.
    const u32 textLen = static_cast<u32>(strlen(text));
    for (u32 i = 0; i < textLen; i++)
    {
        const char_type c = text[i];
        s32 charIndex = 0;

#ifdef TETHYS_SATURN
        // SATURN (S8): one shared Euro rule + a bounds check (this site was
        // already decompiled from a Euro build -- `c + 137` and `c < 7` --
        // and so read up to 53 entries PAST the 104-entry US table).
        charIndex = Tethys_AtlasIndex(static_cast<u8>(c));
        if (charIndex == kTethysAtlasBlankIdx)
        {
            result += field_34_font_context->field_8_atlas_array[1].field_2_width;
            continue;
        }
        charIndex = Tethys_AtlasIndexChecked(field_34_font_context, charIndex);
#else
        if (c <= 32 || static_cast<u8>(c) > 175)
        {
            if (c < 7 || c > 31)
            {
                result += field_34_font_context->field_8_atlas_array[1].field_2_width;
                continue;
            }
            else
            {
                charIndex = c + 137;
            }
        }
        else
        {
            charIndex = c - 31;
        }
#endif

        result += field_34_font_context->field_8_atlas_array[0].field_2_width;
        result += field_34_font_context->field_8_atlas_array[charIndex].field_2_width;
    }

    if (!sFontDrawScreenSpace_508BF4)
    {
        result -= field_34_font_context->field_8_atlas_array[0].field_2_width;
        result = PCToPsxX(result, 20);
    }

    return result;
}

EXPORT s32 AliveFont::MeasureWidth_41C200(char_type character)
{
    s32 result = 0;
    s32 charIndex = 0;

#ifdef TETHYS_SATURN
    // SATURN (S8): shared Euro rule (+ 84 -> + 137) + bounds check.
    charIndex = Tethys_AtlasIndex(static_cast<u8>(character));
    if (charIndex == kTethysAtlasBlankIdx)
    {
        return field_34_font_context->field_8_atlas_array[1].field_2_width;
    }
    charIndex = Tethys_AtlasIndexChecked(field_34_font_context, charIndex);
#else
    if (character <= 32 || character > 175)
    {
        if (character < 8 || character > 31)
        {
            return field_34_font_context->field_8_atlas_array[1].field_2_width;
        }
        charIndex = character + 84;
    }
    else
    {
        charIndex = character - 31;
    }
#endif
    result = field_34_font_context->field_8_atlas_array[charIndex].field_2_width;

    if (!sFontDrawScreenSpace_508BF4)
    {
#ifdef TETHYS_SATURN
        // SATURN (P10): no FPU on SH-2.  0.575 IS 23/40 -- the very PSX->PC x
        // scale PCToPsxX applies (PsxDisplay.hpp:28), which the decompiler
        // spelled as a double.  `result` is a u8 atlas width (<= 69 in either
        // Euro table), and for 0 <= result <= 199 the integer form (r*23)/40
        // is BIT-IDENTICAL to (s32)(r * 0.575) -- checked exhaustively.
        result = PCToPsxX(result);
#else
        result = static_cast<s32>(result * 0.575);
#endif
    }

    return result;
}


s32 AliveFont::MeasureWidth_41C280(const char_type* text, FP scale)
{
    const FP width = FP_FromInteger(MeasureWidth_41C2B0(text));
#ifdef TETHYS_SATURN
    // SATURN (P10): FP_FromDouble(0.5) == FP_FromRaw(0x8000) exactly
    // (0.5 * 0x10000 = 32768) -- same bits, no FPU.
    return FP_GetExponent((width * scale) + FP_FromRaw(0x8000));
#else
    return FP_GetExponent((width * scale) + FP_FromDouble(0.5));
#endif
}

EXPORT s32 AliveFont::DrawString_41C360(PrimHeader** ppOt, const char_type* text, s16 x, s16 y, TPageAbr abr, s32 bSemiTrans, s32 blendMode, Layer layer, u8 r, u8 g, u8 b, s32 polyOffset, FP scale, s32 maxRenderWidth, s32 colorRandomRange)
{
#ifdef TETHYS_SATURN
    // SATURN: dead font (FntP alloc failed at ctor) -- draw nothing.
    if (!field_24_fnt_poly_array)
    {
        return polyOffset;
    }
#endif
    if (!sFontDrawScreenSpace_508BF4)
    {
        x = PsxToPCX(x, 11);
    }

    s32 characterRenderCount = 0;
    const s32 maxRenderX = PsxToPCX(maxRenderWidth, 11);
    s16 offsetX = x;
    s32 charInfoIndex = 0;
    auto poly = &field_24_fnt_poly_array[gPsxDisplay_504C78.field_A_buffer_index + (2 * polyOffset)];

    const s32 tpage = PSX_getTPage_4965D0(TPageMode::e4Bit_0, abr, field_34_font_context->field_0_rect.x & ~63, field_34_font_context->field_0_rect.y);
    const s32 clut = PSX_getClut_496840(field_28_palette_rect.x, field_28_palette_rect.y);

    // SATURN: strlen was the LOOP CONDITION, re-evaluated on every character.
    // Read in the GENERATED code, not guessed (bt1136): Font.o disassembles at
    // the loop head to `mov.l <strlen>,r0 / jsr @r0 / cmp/hs r0,r1 / bf body`
    // with the back-edge branching straight to it, so GCC did NOT hoist it --
    // it cannot prove the Prim writes in the body leave `text` alone. The cost
    // is quadratic in the message length: N calls each scanning N bytes, plus
    // N jsr/rts pairs, on a core with no cache to spare. c7 only draws ~15
    // glyphs a frame so it is small HERE -- but DrawString is also the marquee
    // and the GameSpeak subtitle path, where N is 60+ and this is ~0.6 ms a
    // tick. `text` is const and nothing in either loop writes through it.
    // BOTH sites are fixed: MeasureWidth_41C2B0 has the identical loop, and
    // the rule this project paid for is to find the CLASS before patching the
    // second instance.
    const u32 textLen = static_cast<u32>(strlen(text));
    for (u32 i = 0; i < textLen; i++)
    {
        if (offsetX >= maxRenderX)
        {
            break;
        }

        const u8 c = text[i];

#ifdef TETHYS_SATURN
        // SATURN (S8): shared Euro rule (+ 84 -> + 137) + bounds check.
        charInfoIndex = Tethys_AtlasIndex(c);
        if (charInfoIndex == kTethysAtlasBlankIdx)
        {
            offsetX += field_34_font_context->field_8_atlas_array[0].field_2_width + field_34_font_context->field_8_atlas_array[1].field_2_width;
            continue;
        }
        charInfoIndex = Tethys_AtlasIndexChecked(field_34_font_context, charInfoIndex);
#else
        if (c <= 32 || c > 175)
        {
            if (c < 8 || c > 31)
            {
                offsetX += field_34_font_context->field_8_atlas_array[0].field_2_width + field_34_font_context->field_8_atlas_array[1].field_2_width;
                continue;
            }
            charInfoIndex = c + 84;
        }
        else
        {
            charInfoIndex = c - 31;
        }
#endif

        const auto fContext = field_34_font_context;
        const auto atlasEntry = &fContext->field_8_atlas_array[charInfoIndex];

        const s8 charWidth = atlasEntry->field_2_width;
        const auto charHeight = atlasEntry->field_3_height;
        const s8 texture_u = static_cast<s8>(atlasEntry->field_0_x + (4 * (fContext->field_0_rect.x & 0x3F)));
        const s8 texture_v = static_cast<s8>(atlasEntry->field_1_y + LOBYTE(fContext->field_0_rect.y));

#ifdef TETHYS_SATURN
        // SATURN (P10): SH-2 has no FPU -- 16.16 multiply instead of the
        // decompiler's double.  For scale == FP_FromInteger(1) this is
        // bit-exact: Math_FixedPoint_Multiply(w << 16, 0x10000) == w << 16,
        // and FP_GetExponent divides that back to w, exactly like w * 1.0.
        const s16 widthScaled = FP_GetExponent(FP_FromInteger(charWidth) * scale);
        const s16 heightScaled = FP_GetExponent(FP_FromInteger(charHeight) * scale);
#else
        const s16 widthScaled = static_cast<s16>(charWidth * FP_GetDouble(scale));
        const s16 heightScaled = static_cast<s16>(charHeight * FP_GetDouble(scale));
#endif

        PolyFT4_Init(poly);

        SetPrimExtraPointerHack(poly, nullptr);

        Poly_Set_SemiTrans_498A40(&poly->mBase.header, bSemiTrans);
        Poly_Set_Blending_498A00(&poly->mBase.header, blendMode);

        SetRGB0(
            poly,
            static_cast<u8>(r + Math_RandomRange_450F20(static_cast<s16>(-colorRandomRange), static_cast<s16>(colorRandomRange))),
            static_cast<u8>(g + Math_RandomRange_450F20(static_cast<s16>(-colorRandomRange), static_cast<s16>(colorRandomRange))),
            static_cast<u8>(b + Math_RandomRange_450F20(static_cast<s16>(-colorRandomRange), static_cast<s16>(colorRandomRange))));

        SetTPage(poly, static_cast<s16>(tpage));
        SetClut(poly, static_cast<s16>(clut));

        // Padding
        poly->mVerts[1].mUv.tpage_clut_pad = 0;
        poly->mVerts[2].mUv.tpage_clut_pad = 0;

        // P0
        SetXY0(poly, offsetX, y);
        SetUV0(poly, texture_u, texture_v);

        // P1
        SetXY1(poly, offsetX + widthScaled, y);
        SetUV1(poly, texture_u + charWidth, texture_v);

        // P2
        SetXY2(poly, offsetX, y + heightScaled);
        SetUV2(poly, texture_u, texture_v + charHeight);

        // P3
        SetXY3(poly, offsetX + widthScaled, y + heightScaled);
        SetUV3(poly, texture_u + charWidth, texture_v + charHeight);

        OrderingTable_Add_498A80(OtLayer(ppOt, layer), &poly->mBase.header);

        ++characterRenderCount;

        offsetX += widthScaled + FP_GetExponent(FP_FromInteger(field_34_font_context->field_8_atlas_array[0].field_2_width) * scale);

        poly += 2;
    }

    pScreenManager_4FF7C8->InvalidateRect_406E40(x, y - 1, offsetX, y + 24, pScreenManager_4FF7C8->field_2E_idx);

    return polyOffset + characterRenderCount;
}

void AliveFont::dtor_41C130()
{
    IRenderer::GetRenderer()->PalFree(IRenderer::PalRecord{field_28_palette_rect.x, field_28_palette_rect.y, field_28_palette_rect.w});
    field_28_palette_rect.x = 0;
#ifdef TETHYS_SATURN
    // SATURN: dead font (see ctor) -- nothing to free.
    if (!field_20_fnt_poly_block_ptr)
    {
        return;
    }
#endif
    ResourceManager::FreeResource_455550(field_20_fnt_poly_block_ptr);
}

const char_type* AliveFont::SliceText_41C6C0(const char_type* text, s32 left, FP scale, s32 right)
{
    s32 xOff = 0;
    s32 rightWorldSpace = PsxToPCX(right, 11);

    if (sFontDrawScreenSpace_508BF4)
    {
        xOff = left;
    }
    else
    {
        xOff = PsxToPCX(left, 11);
    }

    for (const char_type* strPtr = text; *strPtr; strPtr++)
    {
        s32 atlasIdx = 0;
        char_type character = *strPtr;
        if (xOff >= rightWorldSpace)
        {
            break;
        }

#ifdef TETHYS_SATURN
        // SATURN (S8): shared Euro rule + bounds check.  This site kept the
        // US ceiling `> 122` while the three others already said `> 175`, so
        // it treated EVERY accented byte as blank and mis-measured every
        // French / Spanish line the word-wrapper looked at.
        atlasIdx = Tethys_AtlasIndexChecked(field_34_font_context, Tethys_AtlasIndex(static_cast<u8>(character)));
#else
        if (character <= 32 || character > 122)
        {
            atlasIdx = character < 8 || character > 31 ? 1 : character + 84;
        }
        else
        {
            atlasIdx = character - 31;
        }
#endif

#ifdef TETHYS_SATURN
        // SATURN (P10): 16.16 multiply, no FPU.  Bit-exact at scale == 1
        // (the only scale LCDScreen::VUpdate_4341B0 ever passes).
        xOff += FP_GetExponent(FP_FromInteger(field_34_font_context->field_8_atlas_array[atlasIdx].field_2_width) * scale) + field_34_font_context->field_8_atlas_array->field_2_width;
#else
        xOff += static_cast<s32>(field_34_font_context->field_8_atlas_array[atlasIdx].field_2_width * FP_GetDouble(scale)) + field_34_font_context->field_8_atlas_array->field_2_width;
#endif
        text = strPtr;
    }

    return text;
}

} // namespace AO
