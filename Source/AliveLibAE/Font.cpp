#include "stdafx.h"
#include "Font.hpp"
#include "Function.hpp"
#include "DebugHelpers.hpp"
#include "FixedPoint.hpp"
#include "VRam.hpp"
#include "Resources.hpp"
#include "Sys_common.hpp"
#include "Renderer/IRenderer.hpp"

// SATURN (D-T1): the French ABE2.FNT / LCDFONT.FNT sheets need their own rects
// (tools/ae/ae_text.py reads them out of Exoddus.exe; the index rule is
// unchanged). Without them the French accents draw wrong (font1) or blank
// (font2); TETHYS_LANG_EN keeps RELIVE's US rects.
#if defined(TETHYS_SATURN) && !defined(TETHYS_LANG_EN) && __has_include("ae/tethys_ae_font_atlas_fr.inc")
#include "ae/tethys_ae_font_atlas_fr.inc"
#define TETHYS_AE_FONT_FR 1
#else
#define TETHYS_AE_FONT_FR 0
#endif

#ifdef TETHYS_SATURN
// SATURN (U10 R25, AO 310.ao.1 twin): a font sheet is the one 4bpp upload the
// converter does NOT pre-decimate 2:1 (tools/converter/font.py ships it at its
// native width). The renderer reads a slot at the sheet's own stride only when
// the slot is registered as a font sheet (src/renderer_saturn.cxx,
// Tethys_NoteFontRect); unregistered, every row after the first shears.
extern "C" void Tethys_NoteFontRect(s16 x, s16 y);
extern "C" void Tethys_ForgetFontRect(s16 x, s16 y);
extern "C" [[noreturn]] void Tethys_Fatal(const char_type* msg);

// SATURN: THE INDEX RULE, AS Exoddus.exe COMPILES IT (read in the decrypted
// image, Font::DrawString 0x4337D0, MeasureWidth 0x433630/0x433700): UNSIGNED
// byte compares (`cmp al, 0x20 / jbe`, `cmp al, 0xaf / ja`, `cmp al, 7 / jb`).
// RELIVE reads the byte as a signed char in MeasureWidth_433700 and compares a
// plain char in MeasureWidth_433630; sh2eb-elf GCC makes plain char SIGNED, so
// every CP437 accent (0x80..0xAF) took the blank branch there: every accented
// letter measured as a space (U1c finding 8). -1 = the blank advance
// (atlas[1]). Both AE tables have 169 entries and the largest index this rule
// can produce is 0x1F + 137 = 168, so no bounds check is needed (asserted
// below, after the tables).
static inline s32 Tethys_AeFontIndex(u8 c, u8 printableHi)
{
    if (c > 0x20u && c <= printableHi)
    {
        return static_cast<s32>(c) - 31;
    }
    if (c >= 7u && c <= 0x1Fu)
    {
        return static_cast<s32>(c) + 137;
    }
    return -1;
}

// SATURN: every palette a Font is built with is a COMPILED PSX u16[16] stored
// as little-endian byte pairs (pal_554474, pal_560F80, sLCDScreen_Palette(2),
// sStatsSignFontPalette_55CF8C, GasCountDown, ColourfulMeter, BrewMachine). The
// Saturn PalSetData assembles entries BIG-endian because every other palette
// it sees comes from the offline converter, already CRAM-ready. So these are
// swapped here and put through the project CRAM rule (tools/converter/common.py
// sat_cram): 0x0000 stays transparent, anything else gets the opaque bit 15.
// AO's twin is AO::Tethys_PalSetCompiled (AliveLibAO/Font.cpp).
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

void Font_ForceLink()
{
}

/*
 Notes for when coming across crazy width conversions:
 If it looks like this:

 (40 * x + 11) / 23

 Then its probably x *= 0.575;
 If it looks like:

 v17 = (s32)(40 * x + 11 + ((s64)(-1307163959i64 * (40 * x + 11)) >> 32)) >> 4;
 x = (v17 >> 31) + v17;

 then do x /= 0.575;
*/

EXPORT void CC static_font1context_dtor_433360()
{
    sFont1Context_5BC5C8.dtor_433510();
}

EXPORT void CC static_font1context_init_433300()
{
    //static_font1context_ctor_nullsub_47();
    atexit(static_font1context_dtor_433360);
}

EXPORT void CC static_font2context_dtor_4333E0()
{
    sFont2Context_5BC5D8.dtor_433510();
}

EXPORT void CC static_font2context_init_433380()
{
    //static_font2context_ctor_nullsub_48();
    atexit(static_font2context_dtor_4333E0);
}

ALIVE_VAR(1, 0x5bc5c8, Font_Context, sFont1Context_5BC5C8, {});
ALIVE_VAR(1, 0x5BC5D8, Font_Context, sFont2Context_5BC5D8, {});

ALIVE_VAR(1, 0x5c9304, s8, sDisableFontFlicker_5C9304, 0);

ALIVE_VAR(1, 0x5ca4b4, u8, sFontDrawScreenSpace_5CA4B4, 0);

ALIVE_VAR(1, 0x5BC5E8, s16, sFontType2LoadCount_5BC5E8, 0);

#ifndef TETHYS_SATURN // SATURN: the custom-font loaders (DebugHelpers.cpp only) are std::vector, never linked
static std::vector<std::vector<u8>> sLoadedAtlas;
#endif

namespace Alive {
Font::Font()
{
}

Font::Font(s32 maxCharLength, const u8* palette, Font_Context* fontContext)
{
    ctor_433590(maxCharLength, palette, fontContext);
}

void Font::ctor_433590(s32 maxCharLength, const u8* palette, Font_Context* fontContext)
{
    field_34_font_context = fontContext;

    IRenderer::PalRecord rec = {0, 0, 16};
    if (!IRenderer::GetRenderer()->PalAlloc(rec))
    {
        LOG_ERROR("PalAlloc failure");
    }

#ifdef TETHYS_SATURN
    // SATURN: `palette` is always a compiled PSX little-endian u8[32] (see
    // Tethys_PalSetCompiled above): swap it before the big-endian PalSetData.
    Tethys_PalSetCompiled(rec.x, rec.y, rec.depth, palette);
#else
    IRenderer::GetRenderer()->PalSetData(rec, palette);
#endif

    field_28_palette_rect.x = rec.x;
    field_28_palette_rect.y = rec.y;
    field_28_palette_rect.w = rec.depth;
    field_28_palette_rect.h = 1;

    field_30_poly_count = maxCharLength;
#if DEVELOPER_MODE // Use normal memory allocating for fonts, so we don't overload the resource heap
    auto db = new void*[1];
    db[0] = ae_new_malloc_4954D0(sizeof(Poly_FT4) * 2 * maxCharLength);
    field_20_fnt_poly_block_ptr = reinterpret_cast<u8**>(db);
#elif defined(TETHYS_GAME_AE)
    // SATURN (AO Font.cpp ao262.4 twin, review RV1): the same locked last-fit
    // block with its reclaim retry, but NON-FATAL. On Saturn
    // Allocate_New_Locked_Resource_49BF40 ends in the "RES NULL" fatal
    // (ResourceManager.cpp Alloc_New_Resource_Impl), so the dead-font branch
    // below could never run; AO's tester died on this very block (FntP,
    // 5,760 B, heap at 99.10 %).
    field_20_fnt_poly_block_ptr = ResourceManager::Alloc_New_Resource_ImplEx(
        ResourceManager::Resource_FntP, fontContext->field_C_resource_id, sizeof(Poly_FT4) * 2 * maxCharLength,
        true, ResourceManager::BlockAllocMethod::eLastMatching, true /*reclaim*/, false /*never fatal*/);
#else
    field_20_fnt_poly_block_ptr = ResourceManager::Allocate_New_Locked_Resource_49BF40(ResourceManager::Resource_FntP, fontContext->field_C_resource_id, sizeof(Poly_FT4) * 2 * maxCharLength);
#endif
#ifdef TETHYS_SATURN
    // SATURN (AO ao262.4 twin): the FntP block comes from the resource heap and
    // Alloc_New_Resource_Impl returns null when it is full (after one reclaim,
    // ResourceManager.cpp). The PC dereferenced it; here the font is DEAD
    // instead: DrawString draws nothing, the dtor frees nothing. A text that
    // vanishes on a full heap is visible and survivable; *nullptr is the BIOS
    // reset vector queued into the ordering table.
    if (!field_20_fnt_poly_block_ptr)
    {
        field_24_fnt_poly_array = nullptr;
        field_30_poly_count = 0;
        return;
    }
#endif
    field_24_fnt_poly_array = reinterpret_cast<Poly_FT4*>(*field_20_fnt_poly_block_ptr);
}

void Font::dtor_433540()
{
    IRenderer::GetRenderer()->PalFree(IRenderer::PalRecord{field_28_palette_rect.x, field_28_palette_rect.y, field_28_palette_rect.w});
    field_28_palette_rect.x = 0;

#ifdef TETHYS_SATURN
    // SATURN: a dead font (see the ctor) holds no FntP block.
    if (!field_20_fnt_poly_block_ptr)
    {
        return;
    }
#endif
#if DEVELOPER_MODE
    auto db = reinterpret_cast<void**>(field_20_fnt_poly_block_ptr);
    ae_delete_free_495540(*db);
    delete[] db;
#else
    ResourceManager::FreeResource_49C330(field_20_fnt_poly_block_ptr);
#endif
}

#ifdef TETHYS_SATURN
// SATURN (U1c T01, U3 T10): the Saturn body is Exoddus.exe's own (read in the
// decrypted image at 0x4337D0), all integer: no FPU on SH-2, and the PC's
// doubles were RELIVE's reading of integer code, not the game's.
//   - x and the right edge: (40 * x + 11) / 23, i.e. PsxToPCX(x, 11), a ROUNDED
//     368 -> 640 conversion (RELIVE wrote x / 0.575, which truncates).
//   - glyph size: Math_FixedPoint_Multiply(w << 16, scale) then the signed
//     divide by 65536, i.e. FP_GetExponent(FP_FromInteger(w) * scale).
//   - spacing: FP_GetExponent(FP_FromInteger(atlas[0].w) * scale). RELIVE wrote
//     atlas[0].w * FP_GetExponent(scale), which is 0 for every menu label drawn
//     at scale 0.75 or 0.84: letters touching where the game spaces them.
//   - the byte is UNSIGNED (Tethys_AeFontIndex) and the loop stops at the NUL
//     (no strlen re-evaluated per glyph, AO bt1136's quadratic cost).
// Plus two Saturn guards AO paid for: a dead font draws nothing (the ctor), and
// the cursor is bounded by the pool (AO 390.ao.1): `poly` indexes the FntP
// block at 2 * (polyOffset + n), and nothing upstream checks it. A string that
// would run past the pool stops drawing there.
s32 Font::DrawString_4337D0(PrimHeader** ppOt, const char_type* text, s32 x, s16 y, TPageAbr abr, s32 bSemiTrans, s32 blendMode, Layer layer, u8 r, u8 g, u8 b, s32 polyOffset, FP scale, s32 maxRenderWidth, s16 colorRandomRange)
{
    if (!field_24_fnt_poly_array || polyOffset < 0 || polyOffset >= field_30_poly_count)
    {
        return polyOffset;
    }
    if (!sFontDrawScreenSpace_5CA4B4)
    {
        x = PsxToPCX(x, 11); // 368 to 640. Convert world space to screen space coords.
    }

    s32 characterRenderCount = 0;
    const s32 maxRenderX = PsxToPCX(maxRenderWidth, 11);
    s32 offsetX = x;
    auto poly = &field_24_fnt_poly_array[gPsxDisplay_5C1130.field_C_buffer_index + (2 * polyOffset)];

    const Font_Context* fContext = field_34_font_context;
    const Font_AtlasEntry* pAtlas = fContext->field_8_atlas_array;
    const s32 tpage = PSX_getTPage_4F60E0(TPageMode::e4Bit_0, abr, fContext->field_0_rect.x & 0xFFC0, fContext->field_0_rect.y & 0xFF00);
    const s32 clut = PSX_getClut_4F6350(field_28_palette_rect.x, field_28_palette_rect.y);
    const s16 spacing = FP_GetExponent(FP_FromInteger(static_cast<s32>(pAtlas[0].field_2_width)) * scale);

    for (const u8* pText = reinterpret_cast<const u8*>(text); *pText; pText++)
    {
        if (offsetX >= maxRenderX)
        {
            break;
        }
        if (polyOffset + characterRenderCount >= field_30_poly_count)
        {
            break; // the pool is full: see the block above
        }

        const s32 charInfoIndex = Tethys_AeFontIndex(*pText, 0xAFu);
        if (charInfoIndex < 0)
        {
            offsetX += pAtlas[1].field_2_width;
            continue;
        }

        const Font_AtlasEntry* atlasEntry = &pAtlas[charInfoIndex];
        const u8 charWidth = atlasEntry->field_2_width;
        const u8 charHeight = atlasEntry->field_3_height;
        const u8 texture_u = static_cast<u8>(atlasEntry->field_0_x + (4 * (fContext->field_0_rect.x & 0x3F)));
        const u8 texture_v = static_cast<u8>(atlasEntry->field_1_y + LOBYTE(fContext->field_0_rect.y));

        const s16 widthScaled = FP_GetExponent(FP_FromInteger(static_cast<s32>(charWidth)) * scale);
        const s16 heightScaled = FP_GetExponent(FP_FromInteger(static_cast<s32>(charHeight)) * scale);

        PolyFT4_Init(poly);
        Poly_Set_SemiTrans_4F8A60(&poly->mBase.header, bSemiTrans);
        Poly_Set_Blending_4F8A20(&poly->mBase.header, blendMode);

        SetRGB0(
            poly,
            static_cast<u8>(r + Math_RandomRange_496AB0(-colorRandomRange, colorRandomRange)),
            static_cast<u8>(g + Math_RandomRange_496AB0(-colorRandomRange, colorRandomRange)),
            static_cast<u8>(b + Math_RandomRange_496AB0(-colorRandomRange, colorRandomRange)));

        SetTPage(poly, static_cast<s16>(tpage));
        SetClut(poly, static_cast<s16>(clut));

        // Padding
        poly->mVerts[1].mUv.tpage_clut_pad = 0;
        poly->mVerts[2].mUv.tpage_clut_pad = 0;

        const s16 x0 = static_cast<s16>(offsetX);
        const s16 x1 = static_cast<s16>(offsetX + widthScaled);
        SetXY0(poly, x0, y);
        SetUV0(poly, texture_u, texture_v);
        SetXY1(poly, x1, y);
        SetUV1(poly, static_cast<u8>(texture_u + charWidth), texture_v);
        SetXY2(poly, x0, static_cast<s16>(y + heightScaled));
        SetUV2(poly, texture_u, static_cast<u8>(texture_v + charHeight));
        SetXY3(poly, x1, static_cast<s16>(y + heightScaled));
        SetUV3(poly, static_cast<u8>(texture_u + charWidth), static_cast<u8>(texture_v + charHeight));

        OrderingTable_Add_4F8AA0(OtLayer(ppOt, layer), &poly->mBase.header);

        ++characterRenderCount;
        offsetX += widthScaled + spacing;
        poly += 2;
    }

    pScreenManager_5BB5F4->InvalidateRect_40EC90(x, y - 1, offsetX, y + 24, pScreenManager_5BB5F4->field_3A_idx);

    return polyOffset + characterRenderCount;
}
#else
s32 Font::DrawString_4337D0(PrimHeader** ppOt, const char_type* text, s32 x, s16 y, TPageAbr abr, s32 bSemiTrans, s32 blendMode, Layer layer, u8 r, u8 g, u8 b, s32 polyOffset, FP scale, s32 maxRenderWidth, s16 colorRandomRange)
{
    if (!sFontDrawScreenSpace_5CA4B4)
    {
        x = static_cast<s32>(x / 0.575); // 368 to 640. Convert world space to screen space coords.
    }

    s32 characterRenderCount = 0;
    const s32 maxRenderX = static_cast<s32>(maxRenderWidth / 0.575);
    s16 offsetX = static_cast<s16>(x);
    s32 charInfoIndex = 0;
    auto poly = &field_24_fnt_poly_array[gPsxDisplay_5C1130.field_C_buffer_index + (2 * polyOffset)];

    s32 tpage = PSX_getTPage_4F60E0(TPageMode::e4Bit_0, abr, field_34_font_context->field_0_rect.x & 0xFFC0, field_34_font_context->field_0_rect.y & 0xFF00);
    s32 clut = PSX_getClut_4F6350(field_28_palette_rect.x, field_28_palette_rect.y);

    for (u32 i = 0; i < strlen(text); i++)
    {
        if (offsetX >= maxRenderX)
        {
            break;
        }

        const u8 c = text[i];
        if (c <= 0x20u || c > 0xAFu)
        {
            if (c < 7u || c > 0x1Fu)
            {
                offsetX += field_34_font_context->field_8_atlas_array[1].field_2_width;
                continue;
            }
            charInfoIndex = c + 137;
        }
        else
        {
            charInfoIndex = c - 31;
        }

        const auto fContext = field_34_font_context;
        const auto atlasEntry = &fContext->field_8_atlas_array[charInfoIndex];

        const s8 charWidth = atlasEntry->field_2_width;
        const auto charHeight = atlasEntry->field_3_height;
        const s8 texture_u = static_cast<s8>(atlasEntry->field_0_x + (4 * (fContext->field_0_rect.x & 0x3F)));
        const s8 texture_v = static_cast<s8>(atlasEntry->field_1_y + LOBYTE(fContext->field_0_rect.y));

        const s16 widthScaled = static_cast<s16>(charWidth * FP_GetDouble(scale));
        const s16 heightScaled = static_cast<s16>(charHeight * FP_GetDouble(scale));

        PolyFT4_Init(poly);
        Poly_Set_SemiTrans_4F8A60(&poly->mBase.header, bSemiTrans);
        Poly_Set_Blending_4F8A20(&poly->mBase.header, blendMode);

        SetRGB0(
            poly,
            static_cast<u8>(r + Math_RandomRange_496AB0(-colorRandomRange, colorRandomRange)),
            static_cast<u8>(g + Math_RandomRange_496AB0(-colorRandomRange, colorRandomRange)),
            static_cast<u8>(b + Math_RandomRange_496AB0(-colorRandomRange, colorRandomRange)));

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

        OrderingTable_Add_4F8AA0(OtLayer(ppOt, layer), &poly->mBase.header);

        ++characterRenderCount;

        offsetX += widthScaled + static_cast<s16>(field_34_font_context->field_8_atlas_array[0].field_2_width * FP_GetExponent(scale));

        poly += 2;
    }

    pScreenManager_5BB5F4->InvalidateRect_40EC90(x, y - 1, offsetX, y + 24, pScreenManager_5BB5F4->field_3A_idx);

    return polyOffset + characterRenderCount;
}
#endif // TETHYS_SATURN: DrawString_4337D0

#ifdef TETHYS_SATURN
// SATURN (U1c T01): the four measures, as Exoddus.exe computes them
// (0x433700, 0x4336C0, 0x433630, 0x433BD0), all integer.
//   - The 640 -> 368 conversion is (23 * w + 20) / 40 = PCToPsxX(w, 20), a
//     ROUNDED one; RELIVE's `w * 0.575` truncates (and costs a soft double).
//   - The byte is unsigned everywhere (see Tethys_AeFontIndex).
s32 Font::MeasureWidth_433700(const char_type* text)
{
    s32 result = 0;
    const Font_AtlasEntry* pAtlas = field_34_font_context->field_8_atlas_array;
    for (const u8* pText = reinterpret_cast<const u8*>(text); *pText; pText++)
    {
        const s32 charIndex = Tethys_AeFontIndex(*pText, 0xAFu);
        if (charIndex < 0)
        {
            result += pAtlas[1].field_2_width;
            continue;
        }
        result += pAtlas[0].field_2_width;
        result += pAtlas[charIndex].field_2_width;
    }

    if (!sFontDrawScreenSpace_5CA4B4)
    {
        result = PCToPsxX(result, 20); // Convert screen space to world space.
    }
    return result;
}

// Measures the width of a string with scale applied: FP_FromDouble(0.5) is
// exactly FP_FromRaw(0x8000), the image's `add eax, 0x8000`.
s32 Font::MeasureWidth_4336C0(const char_type* text, FP scale)
{
    const FP ret = (FP_FromInteger(MeasureWidth_433700(text)) * scale) + FP_FromRaw(0x8000);
    return FP_GetExponent(ret);
}

// Measures the width of a single character (the blank is returned unscaled,
// as in the image).
s32 Font::MeasureWidth_433630(char_type character)
{
    const Font_AtlasEntry* pAtlas = field_34_font_context->field_8_atlas_array;
    const s32 charIndex = Tethys_AeFontIndex(static_cast<u8>(character), 0xAFu);
    if (charIndex < 0)
    {
        return pAtlas[1].field_2_width;
    }
    s32 result = pAtlas[charIndex].field_2_width;
    if (!sFontDrawScreenSpace_5CA4B4)
    {
        result = PCToPsxX(result, 20); // Convert screen space to world space.
    }
    return result;
}

// The LCD marquee's cut points, as the image computes them (0x433BD0):
//   - both edges go 368 -> 640 with the ROUNDED (40 * x + 11) / 23: RELIVE wrote
//     `right * 0.575` for the right edge, the conversion the other way round;
//   - each glyph advances FP_GetExponent(FP_FromInteger(w) * scale) + spacing:
//     RELIVE divided that product by 0x10000 a second time (the `<< 16` of its
//     own comment got lost), so the marquee measured 2 px per letter and its
//     cut-off and tick sound ran ahead of the text (U1c finding 8);
//   - the printable ceiling IS 0x7A here, in the shipped French image too
//     (`cmp al, 0x7a`): accents slice at the blank advance. Kept: it is the
//     game's own behaviour, and on LCDFONT (every printable glyph 14 or 9 px,
//     the blank 14) it moves a cut point by at most one glyph;
//   - it returns where the walk stopped, the NUL when the whole string fits
//     (RELIVE returned the start of the string there).
const char_type* Font::SliceText_433BD0(const char_type* text, s32 left, FP scale, s32 right)
{
    const Font_AtlasEntry* pAtlas = field_34_font_context->field_8_atlas_array;
    const s32 rightWorldSpace = PsxToPCX(right, 11);
    s32 xOff = sFontDrawScreenSpace_5CA4B4 ? left : PsxToPCX(left, 11);

    const u8* pText = reinterpret_cast<const u8*>(text);
    for (; *pText; pText++)
    {
        if (xOff >= rightWorldSpace)
        {
            break;
        }
        const s32 atlasIdx = Tethys_AeFontIndex(*pText, 0x7Au);
        if (atlasIdx < 0)
        {
            xOff += pAtlas[1].field_2_width;
            continue;
        }
        xOff += FP_GetExponent(FP_FromInteger(static_cast<s32>(pAtlas[atlasIdx].field_2_width)) * scale) + pAtlas[0].field_2_width;
    }
    return reinterpret_cast<const char_type*>(pText);
}
#else
s32 Font::MeasureWidth_433700(const char_type* text)
{
    s32 result = 0;

    for (u32 i = 0; i < strlen(text); i++)
    {
        const s8 c = text[i];
        s32 charIndex = 0;

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

        result += field_34_font_context->field_8_atlas_array[0].field_2_width;
        result += field_34_font_context->field_8_atlas_array[charIndex].field_2_width;
    }

    if (!sFontDrawScreenSpace_5CA4B4)
    {
        result = static_cast<s32>(result * 0.575); // Convert screen space to world space.
    }

    return result;
}

// Measures the width of a string with scale applied.
s32 Font::MeasureWidth_4336C0(const char_type* text, FP scale)
{
    FP ret = (FP_FromInteger(MeasureWidth_433700(text)) * scale) + FP_FromDouble(0.5);
    return FP_GetExponent(ret);
}

// Measures the width of a single character.
s32 Font::MeasureWidth_433630(char_type character)
{
    s32 result = 0;
    s32 charIndex = 0;

    if (character <= 32u || character > 175u)
    {
        if (character < 7u || character > 31u)
        {
            return field_34_font_context->field_8_atlas_array[1].field_2_width;
        }
        charIndex = character + 137;
    }
    else
    {
        charIndex = character - 31;
    }
    result = field_34_font_context->field_8_atlas_array[charIndex].field_2_width;

    if (!sFontDrawScreenSpace_5CA4B4)
    {
        result = static_cast<s32>(result * 0.575); // Convert screen space to world space.
    }

    return result;
}

// Wasn't too sure what to call this. Returns the s8 offset of where the text is cut off. (left and right region)
const char_type* Font::SliceText_433BD0(const char_type* text, s32 left, FP scale, s32 right)
{
    s32 xOff = 0;
    s32 rightWorldSpace = static_cast<s32>(right * 0.575);

    if (sFontDrawScreenSpace_5CA4B4)
    {
        xOff = left;
    }
    else
    {
        xOff = static_cast<s32>(left / 0.575);
    }


    for (const char_type* strPtr = text; *strPtr; strPtr++)
    {
        s32 atlasIdx = 0;
        char_type character = *strPtr;
        if (xOff >= rightWorldSpace)
        {
            return strPtr;
        }

        if (character <= 0x20u || character > 0x7Au)
        {
            if (character < 7u || character > 0x1Fu)
            {
                xOff += field_34_font_context->field_8_atlas_array[1].field_2_width;
                continue;
            }
            atlasIdx = character + 137;
        }
        else
        {
            atlasIdx = character - 31;
        }

        // v12 = field_34_font_context->field_8_atlas_array[atlasIdx].field_3_height;
        // Math_FixedPoint_Multiply_496C50(v12 << 16, scale);
        xOff += static_cast<s32>(field_34_font_context->field_8_atlas_array[atlasIdx].field_2_width * FP_GetDouble(scale)) / 0x10000 + field_34_font_context->field_8_atlas_array->field_2_width;
    }

    return text;
}
#endif // TETHYS_SATURN: the measures
} // namespace Alive

void Font_Context::LoadFontType_433400(s16 resourceID)
{
    // Override game fonts with our XInput friendly ones.
#if XINPUT_SUPPORT
    if (resourceID == 2)
    {
        LoadFontTypeFromOddFontMem(sOddFontXboxLCD, nullptr);
        return;
    }
    if (resourceID == 1)
    {
        LoadFontTypeFromOddFontMem(sOddFontXboxMenu, nullptr);
        return;
    }
#endif

    auto loadedResource = ResourceManager::GetLoadedResource_49C2A0(ResourceManager::Resource_Font, resourceID, 1u, 0);
#ifdef TETHYS_SATURN
    // SATURN: the PC dereferenced a missing sheet (*nullptr). Every caller loads
    // the sheet's file first (the menu cameras and ABE2.FNT for id 1, LCDFONT.FNT
    // through the factories for id 2), so an absent one is a loader fault:
    // named here rather than an address error further on.
    if (!loadedResource)
    {
        Tethys_Fatal(resourceID == 1 ? "AE font sheet 1 (ABE2.FNT) not resident" : "AE font sheet 2 (LCDFONT.FNT) not resident");
    }
#endif
    auto fontFile = reinterpret_cast<File_Font*>(*loadedResource);

    field_C_resource_id = resourceID;

    Vram_alloc_4956C0(fontFile->field_0_width, fontFile->field_2_height, fontFile->field_4_color_depth, &field_0_rect);
#ifdef TETHYS_SATURN
    // SATURN (310.ao.1 twin): register the slot as a native-width font sheet
    // BEFORE the upload, which is where the renderer derives the slot's stride.
    Tethys_NoteFontRect(field_0_rect.x, field_0_rect.y);
#endif
    const PSX_RECT vramAllocatedRect = {field_0_rect.x, field_0_rect.y, static_cast<s16>(fontFile->field_0_width / 4), fontFile->field_2_height};

    IRenderer::GetRenderer()->Upload(fontFile->field_4_color_depth == 16 ? IRenderer::BitDepth::e16Bit : IRenderer::BitDepth::e4Bit, vramAllocatedRect, fontFile->field_28_pixel_buffer);

    // Free our loaded font resource as its now in vram
    ResourceManager::FreeResource_49C330(loadedResource);

    switch (resourceID)
    {
        case 1:
            field_8_atlas_array = sFont1Atlas_551D34;
            break;
        case 2:
            field_8_atlas_array = sFont2Atlas_551FD8;
            break;
        default:
            ALIVE_FATAL("Unknown font resource ID !!!");
            break;
    }
}

void Font_Context::dtor_433510()
{
    if (field_0_rect.x)
    {
#ifdef TETHYS_SATURN
        // SATURN (310.ao.1 twin): a later cel landing on the freed origin must
        // not be read as a font sheet.
        Tethys_ForgetFontRect(field_0_rect.x, field_0_rect.y);
#endif
        Vram_free_495A60({field_0_rect.x, field_0_rect.y}, {field_0_rect.w, field_0_rect.h});
    }
}

#ifndef TETHYS_SATURN // SATURN: the custom-font loaders (DebugHelpers.cpp only) read host files into std::vector
bool Font_Context::LoadFontTypeFromFile(const char_type* fontPath, const char_type* atlasPath, s8* pPaletteOut)
{
    auto debugFont = FS::ReadFile(fontPath);
    auto debugFontAtlas = FS::ReadFile(atlasPath);

    if (!debugFont.size() || !debugFontAtlas.size())
    {
        LOG_ERROR("Could not load custom font!");
        return false;
    }

    sLoadedAtlas.push_back(debugFontAtlas);

    LoadFontTypeCustom(reinterpret_cast<File_Font*>(debugFont.data()), reinterpret_cast<Font_AtlasEntry*>(sLoadedAtlas.back().data()), pPaletteOut);
    return true;
}

void Font_Context::LoadFontTypeCustom(File_Font* fontFile, Font_AtlasEntry* fontAtlas, s8* pPaletteOut)
{
    // Give custom fonts a constant resource id for now.
    field_C_resource_id = 0xff;

    Vram_alloc_4956C0(fontFile->field_0_width, fontFile->field_2_height, fontFile->field_4_color_depth, &field_0_rect);
    const PSX_RECT vramAlloctedRect = {field_0_rect.x, field_0_rect.y, static_cast<s16>(fontFile->field_0_width / 4), fontFile->field_2_height};

    if (pPaletteOut)
    {
        memcpy(pPaletteOut, fontFile->field_8_palette, fontFile->field_6_palette_size * 2);
    }


    IRenderer::GetRenderer()->Upload(fontFile->field_4_color_depth == 16 ? IRenderer::BitDepth::e16Bit : IRenderer::BitDepth::e4Bit, vramAlloctedRect, fontFile->field_28_pixel_buffer);

    field_8_atlas_array = fontAtlas;
}

bool Font_Context::LoadFontTypeFromOddFont(const char_type* fontPath, s8* pPaletteOut)
{
    auto debugFont = FS::ReadFile(fontPath);
    if (!debugFont.size())
    {
        LOG_ERROR("Could not load custom font!");
        return false;
    }

    return LoadFontTypeFromOddFontMem(debugFont.data(), pPaletteOut);
}

bool Font_Context::LoadFontTypeFromOddFontMem(u8* data, s8* pPaletteOut)
{
    auto fontFile = reinterpret_cast<File_Font*>(data);
    s32* atlasCount = reinterpret_cast<s32*>(fontFile->field_28_pixel_buffer + ((fontFile->field_0_width * fontFile->field_2_height) / 2));
    Font_AtlasEntry* atlasData = reinterpret_cast<Font_AtlasEntry*>(atlasCount + 1);

    auto debugFontAtlas = std::vector<u8>((u8*) atlasData, (u8*) atlasData + (sizeof(Font_AtlasEntry) * *atlasCount));

    sLoadedAtlas.push_back(debugFontAtlas);
    LoadFontTypeCustom(reinterpret_cast<File_Font*>(data), reinterpret_cast<Font_AtlasEntry*>(sLoadedAtlas.back().data()), pPaletteOut);
    return true;
}
#endif // !TETHYS_SATURN: the custom-font loaders

#if TETHYS_AE_FONT_FR
ALIVE_ARY(1, 0x551D34, Font_AtlasEntry, 169, sFont1Atlas_551D34, {TETHYS_AE_FONT1_ATLAS_ROWS});
#else
ALIVE_ARY(1, 0x551D34, Font_AtlasEntry, 169, sFont1Atlas_551D34,
          {{0u, 0u, 2u, 0u},
           {0u, 0u, 9u, 0u},
           {43u, 80u, 6u, 23u},
           {182u, 0u, 11u, 10u},
           {182u, 0u, 11u, 10u},
           {182u, 0u, 11u, 10u},
           {182u, 0u, 11u, 10u},
           {182u, 0u, 11u, 10u},
           {85u, 96u, 7u, 8u},
           {52u, 79u, 11u, 23u},
           {64u, 79u, 11u, 23u},
           {186u, 0u, 11u, 10u},
           {0u, 79u, 23u, 17u},
           {86u, 79u, 7u, 23u},
           {75u, 79u, 10u, 11u},
           {94u, 79u, 6u, 23u},
           {0u, 0u, 9u, 0u},
           {193u, 51u, 17u, 23u},
           {32u, 51u, 17u, 23u},
           {50u, 51u, 17u, 23u},
           {69u, 51u, 17u, 23u},
           {87u, 51u, 17u, 23u},
           {106u, 51u, 17u, 23u},
           {124u, 51u, 17u, 23u},
           {142u, 51u, 17u, 23u},
           {158u, 51u, 17u, 23u},
           {176u, 51u, 17u, 23u},
           {110u, 79u, 7u, 22u},
           {102u, 79u, 7u, 22u},
           {61u, 79u, 10u, 23u},
           {25u, 79u, 15u, 16u},
           {73u, 79u, 10u, 23u},
           {118u, 79u, 14u, 23u},
           {182u, 0u, 11u, 10u},
           {9u, 0u, 17u, 23u},
           {26u, 0u, 15u, 23u},
           {41u, 0u, 15u, 23u},
           {56u, 0u, 16u, 23u},
           {73u, 0u, 12u, 23u},
           {86u, 0u, 12u, 23u},
           {98u, 0u, 15u, 23u},
           {114u, 0u, 16u, 23u},
           {132u, 0u, 7u, 23u},
           {139u, 0u, 14u, 23u},
           {154u, 0u, 15u, 23u},
           {170u, 0u, 12u, 23u},
           {0u, 25u, 21u, 23u},
           {21u, 25u, 16u, 23u},
           {37u, 25u, 18u, 23u},
           {56u, 25u, 14u, 23u},
           {71u, 25u, 18u, 23u},
           {90u, 25u, 14u, 23u},
           {106u, 25u, 14u, 23u},
           {121u, 25u, 15u, 23u},
           {137u, 25u, 16u, 23u},
           {153u, 25u, 17u, 23u},
           {170u, 25u, 20u, 23u},
           {191u, 25u, 15u, 23u},
           {0u, 50u, 17u, 23u},
           {17u, 50u, 13u, 23u},
           {61u, 79u, 10u, 23u},
           {0u, 0u, 9u, 0u},
           {73u, 79u, 10u, 23u},
           {186u, 0u, 11u, 10u},
           {86u, 79u, 27u, 10u},
           {186u, 0u, 7u, 11u},
           {0u, 108u, 16u, 24u},
           {17u, 108u, 13u, 24u},
           {31u, 108u, 13u, 24u},
           {44u, 108u, 15u, 24u},
           {59u, 108u, 11u, 24u},
           {71u, 108u, 11u, 24u},
           {83u, 108u, 13u, 24u},
           {97u, 108u, 14u, 24u},
           {113u, 108u, 7u, 24u},
           {120u, 108u, 11u, 24u},
           {134u, 108u, 13u, 24u},
           {149u, 108u, 11u, 24u},
           {0u, 135u, 17u, 24u},
           {19u, 135u, 15u, 24u},
           {34u, 135u, 17u, 24u},
           {52u, 135u, 13u, 24u},
           {66u, 135u, 17u, 24u},
           {84u, 135u, 13u, 24u},
           {97u, 135u, 12u, 24u},
           {111u, 135u, 14u, 24u},
           {125u, 135u, 14u, 24u},
           {141u, 135u, 15u, 24u},
           {157u, 135u, 17u, 24u},
           {1u, 163u, 13u, 24u},
           {16u, 163u, 15u, 24u},
           {32u, 163u, 12u, 24u},
           {0u, 0u, 9u, 0u},
           {0u, 0u, 9u, 0u},
           {0u, 0u, 9u, 0u},
           {134u, 79u, 16u, 9u},
           {0u, 0u, 9u, 0u},
           {227u, 211u, 12u, 24u},
           {239u, 187u, 14u, 24u},
           {233u, 79u, 11u, 24u},
           {201u, 79u, 14u, 24u},
           {217u, 79u, 14u, 24u},
           {187u, 79u, 14u, 24u},
           {0u, 110u, 16u, 22u},
           {227u, 211u, 12u, 24u},
           {211u, 52u, 11u, 24u},
           {222u, 52u, 11u, 24u},
           {244u, 79u, 11u, 24u},
           {213u, 28u, 10u, 24u},
           {175u, 136u, 8u, 24u},
           {240u, 51u, 7u, 24u},
           {218u, 79u, 17u, 24u},
           {0u, 110u, 16u, 22u},
           {233u, 79u, 11u, 24u},
           {223u, 234u, 19u, 20u},
           {223u, 234u, 19u, 20u},
           {214u, 162u, 17u, 24u},
           {230u, 162u, 17u, 24u},
           {239u, 27u, 16u, 24u},
           {225u, 187u, 14u, 24u},
           {210u, 187u, 14u, 24u},
           {16u, 164u, 15u, 22u},
           {230u, 162u, 17u, 24u},
           {239u, 187u, 14u, 24u},
           {0u, 0u, 9u, 0u},
           {0u, 0u, 9u, 0u},
           {0u, 0u, 9u, 0u},
           {0u, 0u, 9u, 0u},
           {0u, 0u, 9u, 0u},
           {170u, 79u, 16u, 24u},
           {234u, 51u, 7u, 24u},
           {223u, 27u, 16u, 24u},
           {197u, 187u, 14u, 24u},
           {238u, 211u, 15u, 24u},
           {238u, 211u, 16u, 24u},
           {0u, 0u, 9u, 0u},
           {0u, 0u, 9u, 0u},
           {197u, 212u, 14u, 24u},
           {0u, 0u, 9u, 0u},
           {0u, 0u, 9u, 0u},
           {0u, 0u, 9u, 0u},
           {0u, 0u, 9u, 0u},
           {197u, 0u, 7u, 22u},
           {0u, 0u, 9u, 0u},
           {0u, 0u, 9u, 0u},
           {0u, 186u, 36u, 22u},
           {39u, 186u, 36u, 22u},
           {78u, 186u, 36u, 22u},
           {117u, 186u, 36u, 22u},
           {156u, 186u, 36u, 22u},
           {0u, 210u, 36u, 22u},
           {39u, 210u, 36u, 22u},
           {78u, 210u, 36u, 22u},
           {189u, 124u, 30u, 19u},
           {221u, 124u, 30u, 20u},
           {224u, 143u, 25u, 21u},
           {193u, 143u, 25u, 21u},
           {162u, 110u, 19u, 18u},
           {211u, 0u, 44u, 26u},
           {196u, 162u, 16u, 24u},
           {48u, 162u, 16u, 24u},
           {64u, 162u, 16u, 24u},
           {81u, 162u, 16u, 24u},
           {98u, 162u, 16u, 24u},
           {115u, 162u, 16u, 24u},
           {131u, 162u, 16u, 24u},
           {148u, 162u, 16u, 24u},
           {163u, 162u, 16u, 24u},
           {180u, 162u, 16u, 24u}});
#endif

#if TETHYS_AE_FONT_FR
ALIVE_ARY(1, 0x551FD8, Font_AtlasEntry, 169, sFont2Atlas_551FD8, {TETHYS_AE_FONT2_ATLAS_ROWS});
#else
ALIVE_ARY(1, 0x551FD8, Font_AtlasEntry, 169, sFont2Atlas_551FD8,
          {{0u, 0u, 2u, 0u},
           {0u, 0u, 14u, 0u},
           {105u, 42u, 14u, 13u},
           {105u, 70u, 14u, 4u},
           {0u, 0u, 14u, 0u},
           {75u, 70u, 14u, 13u},
           {0u, 0u, 14u, 0u},
           {0u, 0u, 14u, 0u},
           {105u, 70u, 8u, 5u},
           {45u, 70u, 14u, 13u},
           {60u, 70u, 14u, 13u},
           {105u, 84u, 14u, 0u},
           {75u, 84u, 14u, 13u},
           {45u, 42u, 14u, 13u},
           {60u, 84u, 14u, 13u},
           {30u, 42u, 14u, 13u},
           {0u, 0u, 14u, 0u},
           {0u, 56u, 14u, 13u},
           {15u, 56u, 14u, 13u},
           {30u, 56u, 14u, 13u},
           {45u, 56u, 14u, 13u},
           {60u, 56u, 14u, 13u},
           {75u, 56u, 14u, 13u},
           {90u, 56u, 14u, 13u},
           {105u, 56u, 14u, 13u},
           {0u, 70u, 14u, 13u},
           {15u, 70u, 14u, 13u},
           {60u, 42u, 14u, 13u},
           {75u, 42u, 14u, 13u},
           {45u, 70u, 14u, 13u},
           {30u, 70u, 14u, 13u},
           {60u, 70u, 14u, 13u},
           {90u, 42u, 14u, 13u},
           {0u, 0u, 14u, 0u},
           {0u, 0u, 14u, 13u},
           {15u, 0u, 14u, 13u},
           {30u, 0u, 14u, 13u},
           {45u, 0u, 14u, 13u},
           {60u, 0u, 14u, 13u},
           {75u, 0u, 14u, 13u},
           {90u, 0u, 14u, 13u},
           {105u, 0u, 14u, 13u},
           {0u, 14u, 14u, 13u},
           {15u, 14u, 14u, 13u},
           {30u, 14u, 14u, 13u},
           {45u, 14u, 14u, 13u},
           {60u, 14u, 14u, 13u},
           {75u, 14u, 14u, 13u},
           {90u, 14u, 14u, 13u},
           {105u, 14u, 14u, 13u},
           {0u, 28u, 14u, 13u},
           {15u, 28u, 14u, 13u},
           {30u, 28u, 14u, 13u},
           {45u, 28u, 14u, 13u},
           {60u, 28u, 14u, 13u},
           {75u, 28u, 14u, 13u},
           {90u, 28u, 14u, 13u},
           {105u, 28u, 14u, 13u},
           {0u, 42u, 14u, 13u},
           {15u, 42u, 14u, 13u},
           {45u, 70u, 14u, 13u},
           {90u, 70u, 14u, 13u},
           {60u, 70u, 14u, 13u},
           {0u, 0u, 14u, 0u},
           {0u, 0u, 14u, 0u},
           {105u, 70u, 7u, 4u},
           {0u, 0u, 14u, 13u},
           {15u, 0u, 14u, 13u},
           {30u, 0u, 14u, 13u},
           {45u, 0u, 14u, 13u},
           {60u, 0u, 14u, 13u},
           {75u, 0u, 14u, 13u},
           {90u, 0u, 14u, 13u},
           {105u, 0u, 14u, 13u},
           {0u, 14u, 14u, 13u},
           {15u, 14u, 14u, 13u},
           {30u, 14u, 14u, 13u},
           {45u, 14u, 14u, 13u},
           {60u, 14u, 14u, 13u},
           {75u, 14u, 14u, 13u},
           {90u, 14u, 14u, 13u},
           {105u, 14u, 14u, 13u},
           {0u, 28u, 14u, 13u},
           {15u, 28u, 14u, 13u},
           {30u, 28u, 14u, 13u},
           {45u, 28u, 14u, 13u},
           {60u, 28u, 14u, 13u},
           {75u, 28u, 14u, 13u},
           {90u, 28u, 14u, 13u},
           {105u, 28u, 14u, 13u},
           {0u, 42u, 14u, 13u},
           {15u, 42u, 14u, 13u},
           {0u, 0u, 9u, 0u},
           {0u, 0u, 9u, 0u},
           {0u, 0u, 9u, 0u},
           {0u, 0u, 9u, 0u},
           {0u, 0u, 9u, 0u},
           {0u, 0u, 9u, 0u},
           {0u, 0u, 9u, 0u},
           {0u, 0u, 9u, 0u},
           {0u, 0u, 9u, 0u},
           {0u, 0u, 9u, 0u},
           {0u, 0u, 9u, 0u},
           {0u, 0u, 9u, 0u},
           {0u, 0u, 9u, 0u},
           {0u, 0u, 9u, 0u},
           {0u, 0u, 9u, 0u},
           {0u, 0u, 9u, 0u},
           {0u, 0u, 9u, 0u},
           {0u, 0u, 9u, 0u},
           {0u, 0u, 9u, 0u},
           {0u, 0u, 9u, 0u},
           {0u, 0u, 9u, 0u},
           {0u, 0u, 9u, 0u},
           {0u, 0u, 9u, 0u},
           {0u, 0u, 9u, 0u},
           {0u, 0u, 9u, 0u},
           {0u, 0u, 9u, 0u},
           {0u, 0u, 9u, 0u},
           {0u, 0u, 9u, 0u},
           {0u, 0u, 9u, 0u},
           {0u, 0u, 9u, 0u},
           {0u, 0u, 9u, 0u},
           {0u, 0u, 9u, 0u},
           {0u, 0u, 9u, 0u},
           {0u, 0u, 9u, 0u},
           {0u, 0u, 9u, 0u},
           {0u, 0u, 9u, 0u},
           {0u, 0u, 9u, 0u},
           {0u, 0u, 9u, 0u},
           {0u, 0u, 9u, 0u},
           {0u, 0u, 9u, 0u},
           {0u, 0u, 9u, 0u},
           {0u, 0u, 9u, 0u},
           {0u, 0u, 9u, 0u},
           {0u, 0u, 9u, 0u},
           {0u, 0u, 9u, 0u},
           {0u, 0u, 9u, 0u},
           {0u, 0u, 9u, 0u},
           {0u, 0u, 9u, 0u},
           {0u, 0u, 9u, 0u},
           {0u, 0u, 9u, 0u},
           {0u, 0u, 9u, 0u},
           {0u, 0u, 9u, 0u},
           {0u, 0u, 9u, 0u},
           {0u, 98u, 20u, 15u},
           {20u, 98u, 20u, 15u},
           {40u, 98u, 20u, 15u},
           {60u, 98u, 20u, 15u},
           {80u, 98u, 20u, 15u},
           {0u, 115u, 20u, 15u},
           {20u, 115u, 20u, 15u},
           {40u, 115u, 20u, 15u},
           {0u, 132u, 20u, 15u},
           {20u, 132u, 20u, 15u},
           {40u, 132u, 20u, 15u},
           {60u, 132u, 20u, 15u},
           {0u, 0u, 0u, 0u},
           {0u, 0u, 0u, 0u},
           {0u, 0u, 0u, 0u},
           {0u, 0u, 0u, 0u},
           {0u, 0u, 0u, 0u},
           {0u, 0u, 0u, 0u},
           {0u, 0u, 0u, 0u},
           {0u, 0u, 0u, 0u},
           {0u, 0u, 0u, 0u},
           {0u, 0u, 0u, 0u},
           {0u, 0u, 0u, 0u},
           {0u, 0u, 0u, 0u}});
#endif

#ifdef TETHYS_SATURN
// SATURN: the largest index Tethys_AeFontIndex can return is 0x1F + 137 = 168
// (see its block), so both tables must keep their 169 entries.
static_assert(ALIVE_ARY_SIZEOF(sFont1Atlas_551D34) == 169 * sizeof(Font_AtlasEntry)
                  && ALIVE_ARY_SIZEOF(sFont2Atlas_551FD8) == 169 * sizeof(Font_AtlasEntry),
              "Tethys_AeFontIndex reads up to entry 168");
#endif
