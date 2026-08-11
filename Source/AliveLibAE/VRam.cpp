#include "stdafx.h"
#include "VRam.hpp"
#include "Function.hpp"
#include "PsxDisplay.hpp"
#include <gmock/gmock.h>
#include "Renderer/IRenderer.hpp"
#include "BaseGameAutoPlayer.hpp"

const s32 kMaxAllocs = 512;

ALIVE_ARY(1, 0x5cb888, PSX_RECT, kMaxAllocs, sVramAllocations_5CB888, {});
ALIVE_VAR(1, 0x5cc888, s32, sVramNumberOfAllocations_5CC888, 0);
ALIVE_VAR(1, 0x5CC88C, u16, unused_5CC88C, 0);

EXPORT s8 CC Vram_calc_width_4955A0(s32 width, s32 depth)
{
    switch (depth)
    {
        case 0:
            return ((width + 7) >> 2) & 0xFE;
        case 1:
            return ((width + 3) >> 1) & 0xFE;
        case 2:
            return (width + 1) & 0xFE;
    }
    return 0;
}

EXPORT s32 CC Vram_Is_Area_Free_4958F0(PSX_RECT* pRect, s32 depth)
{
    pRect->x = 1024 - pRect->w;
    if (pRect->x < 0)
    {
        return 0;
    }

    s16 newX = 0;
    const s32 depthShift = 2 - depth;
    while (true)
    {
        if ((pRect->w << depthShift) + ((pRect->x & 63) << depthShift) > 256)
        {
            newX = pRect->x + 64 - 1;
            newX = (pRect->x + (64 - 1)) & ~(64 - 1);
            if (newX < pRect->x)
            {
                pRect->x = newX;
            }
        }
        else
        {
            if (sVramNumberOfAllocations_5CC888 <= 0)
            {
                return 1;
            }

#if 0 // SATURN bt1003: REVERTED -- this made hardware load times ~80% WORSE.
            // Measured on real hardware across FIVE screens matched on both the
            // flip index and the occupancy vn: n13 3267->5741 ms, n14
            // 4028->7284, n15 3998->7293, n16 3568->6146, n17 3962->7289.
            // Five controlled comparisons, all +72% to +84%. Not noise.
            //
            // WHY IT BACKFIRED, and the lesson is the useful part. The proof
            // below is correct: the ANSWER is identical and the ITERATION COUNT
            // does drop. What it never established is the cost of one iteration.
            // The stock loop EARLY-EXITS at the first blocker -- typically index
            // 0 or 1 -- so its scan is a handful of tests. This version scans all
            // N every single iteration, and N reaches 55 in the field. Trading a
            // ~2-deep scan for a 55-deep one costs more than the saved
            // iterations return. I proved equivalence of RESULT and then claimed
            // a gain in SPEED; they are different claims and only one was
            // checked. A complexity bound is not a measurement.
            //
            // If this is ever revisited: the real win is not a better scan, it
            // is not scanning -- an occupancy structure (row interval list, or a
            // per-64-column skyline) that answers "what blocks x" without
            // touching every rect. Measure with gauge `l` on HARDWARE; Ymir does
            // not model CD timing and cannot judge a load-time change at all.
            //
            // WHY IT MATTERS HERE AND NOT ON PSX. This function is called once
            // per candidate y by Vram_alloc_block, and once per FG1 chunk plus
            // once per Animation::Init -- and FG1 dies and is rebuilt on EVERY
            // camera flip, so a screen change pays a batch of ~20 allocations.
            // The stock scan takes the FIRST overlapping rect by index and steps
            // x left to just past it, so a position blocked by k rects costs k
            // separate scans of the table. Cost is therefore superlinear in
            // OCCUPANCY, not in the allocation count. Measured in the field
            // (bt998 captures, gauge `l` against gauge `vn`): flip time tracked
            // occupancy from 3802 ms at vn13 to 6728 ms at vn49 -- +2.9 s of
            // load time bought by nothing. An offline re-implementation seeded
            // with the real boot reservations and the real AnimResources
            // dimensions put one FG1 batch at 1.4 ms on a fresh screen and
            // 5.7 ms nine screens in, and had single allocations reaching
            // 20,000-30,000 overlap tests near 50% occupancy.
            //
            // WHY THE ANSWER IS IDENTICAL, not merely close. Let S be the set of
            // rects overlapping the candidate at x, and let m = min over S of
            // (rect.x - w). For any x'' with m < x'' <= x, the argmin rect j*
            // still overlaps at x'': x'' <= x < j*.x + j*.w keeps the right
            // edge condition, and x'' > m = j*.x - w keeps the left. So every
            // position the stock loop would visit between m and x is blocked
            // anyway -- taking the minimum skips only positions that were going
            // to fail. The trade is a full table scan per iteration instead of
            // an early-exit one, against a bound of one iteration per distinct
            // blocking set: worst case N*N rather than 1024*N.
            //
            // Guarded because AliveLibAE/VRam.cpp carries gmock tests that pin
            // the stock walk; the PC build keeps byte-for-byte stock behaviour.
            s32 bestX = 0x7FFFFFFF;
            for (s32 i = 0; i < sVramNumberOfAllocations_5CC888; i++)
            {
                if (Vram_rects_overlap_4959E0(pRect, &sVramAllocations_5CB888[i]))
                {
                    const s32 nx = sVramAllocations_5CB888[i].x - pRect->w + 1;
                    if (nx < bestX)
                    {
                        bestX = nx;
                    }
                }
            }
            if (bestX == 0x7FFFFFFF)
            {
                return 1; // nothing overlaps here -- the stock scan's exit
            }
            if (bestX < pRect->x)
            {
                pRect->x = static_cast<s16>(bestX);
            }
#else
            s32 i = 0;
            while (!Vram_rects_overlap_4959E0(pRect, &sVramAllocations_5CB888[i]))
            {
                i++;
                if (i >= sVramNumberOfAllocations_5CC888)
                {
                    return 1;
                }
            }

            newX = sVramAllocations_5CB888[i].x - pRect->w + 1;

            if (newX < pRect->x)
            {
                pRect->x = newX;
            }
#endif
        }

        if (--pRect->x < 0)
        {
            return 0;
        }
    }
}

#ifdef TETHYS_SATURN
// SATURN (bt1020): price the ALLOCATOR half of a screen change, so the 6-11.5 s
// stall stops being attributed by argument.
//
// Two mechanisms can each explain seconds: the CD reader's drive round-trips
// (Tethys_gCdMsAccum, cd_saturn.cxx) and this scan, whose cost is superlinear in
// vram_alloc OCCUPANCY -- bt998 field captures had flip time tracking vn from
// 3802 ms at vn13 to 6728 ms at vn49. bt999 then "fixed" the scan without ever
// pricing one iteration and made hardware 80% WORSE. So this build measures and
// changes NOTHING: the allocator below is byte-for-byte stock, and one photo
// will now split `l` into l = lc + la + the rest instead of a fourth guess.
//
// Placed on Vram_alloc_block rather than on Vram_Is_Area_Free: the block search
// is the caller that iterates y, so it captures the whole descent in one bracket
// with one pair of timer reads instead of one per candidate row. Direct
// Is_Area_Free callers are therefore NOT counted, which is the right scope --
// FG1 and Animation::Init both come through here.
extern "C" u32 Tethys_gVaRawAccum;
extern "C" u32 Tethys_RawTicks(); // raw ~208/ms ticks: bt1020's ms clock
                                  // rounded every sub-ms call to zero (bt1021)
// SATURN (bt1041/bt1044): next row worth probing after a failed one, one per
// walk direction -- src/vram_placer.cxx carries both proofs.
extern "C" s16 Tethys_VphNextRow(s16 yFailed);
extern "C" s16 Tethys_VphNextRowDown(s16 yFailed, s16 h);

namespace {
struct VaTimer
{
    u32 t0;
    VaTimer() : t0(Tethys_RawTicks()) {}
    ~VaTimer() { Tethys_gVaRawAccum += Tethys_RawTicks() - t0; }
};
} // namespace
#endif

EXPORT s32 CC Vram_alloc_block_4957B0(PSX_RECT* pRect, s32 depth)
{
#ifdef TETHYS_SATURN
    // Scoped so every one of this function's five returns is covered.
    VaTimer vaTimer;
#endif
    if (pRect->w > 1024 || pRect->h > 512)
    {
        return 0;
    }

    if (pRect->h * pRect->w >= 1024)
    {
        pRect->y = 512 - pRect->h;
        while (pRect->y >= 0)
        {
            // Old Code: if (static_cast<u8>(pRect->y) + pRect->h <= 256)
            // Instead of casting to u8 to wrap around the integer, we're
            // going to do it manually in case other platforms don't auto wrap integers on
            // cast.
            if ((pRect->y % 256) + pRect->h <= 256)
            {
                if (Vram_Is_Area_Free_4958F0(pRect, depth))
                {
                    return 1;
                }
#ifdef TETHYS_SATURN
                // SATURN (bt1044): the bt1041 skip, mirrored onto the branch
                // that carries the BIG rects -- FG1 blocks, actor sheets, and
                // the Slog/Elum/Meat/Rock gibs -- i.e. the bulk of a screen
                // change. Same theorem (proof in src/vram_placer.cxx): going
                // down, a row can only improve on the one above it by a blocker
                // falling out of range, and a blocker falls out exactly at
                // r.y - h, so every row between here and the largest such edge
                // below us is provably not the answer.
                //   The `continue` is load-bearing: the helper returns the next
                // CANDIDATE, and the loop's own --pRect->y at the bottom would
                // step one row past it. Everything else stays stock -- x, the
                // overlap test, the legality guard, and the `y >= 0` terminator
                // that this continue re-tests and that ends an exhausted search.
                pRect->y = Tethys_VphNextRowDown(pRect->y, pRect->h);
                continue;
#endif
            }
            else
            {
                // v7 &= 0xFFFFFF00; // Todo: check this. was LOBYTE(v7) = 0; Doesn't seem needed
                // to pass tests.
                const s16 ypos = (pRect->y + 255) - pRect->h + 1;
                if (ypos < pRect->y)
                {
                    pRect->y = ypos;
                }
            }
            --pRect->y;
        }
        return 0;
    }

    pRect->y = 0;
    if (512 - pRect->h <= 0)
    {
        return 0;
    }

    // Search Loop
    while (true)
    {
        const s16 yPos = pRect->y;
        if (pRect->h + yPos <= 255 || yPos >= 256)
        {
            if (!Vram_Is_Area_Free_4958F0(pRect, depth))
            {
#ifdef TETHYS_SATURN
                // SATURN (bt1041): SKIP THE ROWS THAT CANNOT BE THE ANSWER.
                // This ++ is the entire defect: a late ascending search runs it
                // 2*(256-h) = 470 times for a 21-tall chant orb, and ONE such
                // call measured 3948 raw ticks = 19.0 ms (bt1040 gauge av, with
                // uv resolving to AO::OrbWhirlWind). Theorem: if row y failed
                // and row y+k succeeds, some rect's BOTTOM EDGE lies at y+k --
                // a row can only improve on the one below it by losing a
                // blocker, and a blocker leaves exactly at its bottom edge. So
                // every row between here and the next bottom edge is provably
                // not the answer. Everything else stays stock: x, the overlap
                // test, both legality guards and all three exits, including the
                // terminator right below. Deliberately NOT bt999, which edited
                // Is_Area_Free's inner scan and cost hardware 80%.
                pRect->y = Tethys_VphNextRow(pRect->y);
#else
                pRect->y++;
#endif
                if (pRect->y >= 512 - pRect->h)
                {
                    return 0;
                }

                continue;
            }
            else
            {
                return 1;
            }
        }
        pRect->y = 255;

        ++pRect->y;
        if (pRect->y >= 512 - pRect->h)
        {
            return 0;
        }
    }

    return 1;
}

EXPORT s16 CC Vram_alloc_4956C0(u16 width, s16 height, u16 colourDepth, PSX_RECT* pRect)
{
    PSX_RECT rect = {};

    const s32 depth = colourDepth / 8;

    rect.w = Vram_calc_width_4955A0(width, depth);
    rect.h = height;

    if (sVramNumberOfAllocations_5CC888 >= kMaxAllocs || !Vram_alloc_block_4957B0(&rect, depth))
    {
        if (GetGameAutoPlayer().IsRecording() || GetGameAutoPlayer().IsPlaying())
        {
            LOG_WARNING("Fat vram alloc hax");
            pRect->w = 1;
            pRect->h = 1;
            pRect->x = 1024 - 1;
            pRect->y = 512 - 1;
            return 1;
        }
        return 0;
    }

    sVramAllocations_5CB888[sVramNumberOfAllocations_5CC888++] = rect;
    *pRect = rect;

    return 1;
}

EXPORT void CC Vram_init_495660()
{
    for (s32 i = 0; i < kMaxAllocs; i++)
    {
        sVramAllocations_5CB888[i] = {};
    }
    sbDebugFontLoaded_BB4A24 = 0;
    sVramNumberOfAllocations_5CC888 = 0;
}

EXPORT void CC Vram_alloc_explicit_4955F0(s16 x, s16 y, s16 w, s16 h)
{
    if (sVramNumberOfAllocations_5CC888 < kMaxAllocs)
    {
        sVramAllocations_5CB888[sVramNumberOfAllocations_5CC888].x = x;
        sVramAllocations_5CB888[sVramNumberOfAllocations_5CC888].y = y;
        sVramAllocations_5CB888[sVramNumberOfAllocations_5CC888].w = w - x + 1;
        sVramAllocations_5CB888[sVramNumberOfAllocations_5CC888].h = h - y + 1;
        sVramNumberOfAllocations_5CC888++;
    }
}

#ifdef TETHYS_SATURN
// SATURN (P8): Vram_free_495A60 is the ONLY point where the game releases a
// VRAM rect (Animation::VCleanUp, FG1 dtor, Paramite/Scrab far-away frees).
// The backend keeps one VDP1 texture slot per live rect and must retire it
// there, BEFORE the model recycles the coordinates for the next camera's
// allocs -- a stale slot would alias the recycled rect (same origin,
// possibly different depth). Defined in src/renderer_saturn.cxx; pure
// function of the rect, silent when no slot matches (never-uploaded rects),
// safe at any time (mid-gameplay frees included).
extern "C" void Tethys_VramFree(s16 x, s16 y, s16 w, s16 h);
#endif

EXPORT void CC Vram_free_495A60(PSX_Point xy, PSX_Point wh)
{
#ifdef TETHYS_SATURN
    Tethys_VramFree(xy.field_0_x, xy.field_2_y, wh.field_0_x, wh.field_2_y); // SATURN: P8 hook, see above
#endif
    // Find the allocation
    for (s32 i = 0; i < sVramNumberOfAllocations_5CC888; i++)
    {
        if (sVramAllocations_5CB888[i].x == xy.field_0_x && sVramAllocations_5CB888[i].y == xy.field_2_y && sVramAllocations_5CB888[i].w == wh.field_0_x && sVramAllocations_5CB888[i].h == wh.field_2_y)
        {
            // Copy the last element to this one
            sVramAllocations_5CB888[i] = sVramAllocations_5CB888[sVramNumberOfAllocations_5CC888 - 1];

            // Decrement the used count
            sVramNumberOfAllocations_5CC888--;
            return;
        }
    }
}

EXPORT Bool32 CC Vram_rects_overlap_4959E0(const PSX_RECT* pRect1, const PSX_RECT* pRect2)
{
    const s32 x1 = pRect1->x;
    const s32 x2 = pRect2->x;
    if (x1 >= x2 + pRect2->w)
    {
        return 0;
    }

    const s32 y2 = pRect2->y;
    const s32 y1 = pRect1->y;
    if (y1 >= y2 + pRect2->h)
    {
        return 0;
    }

    if (x2 < x1 + pRect1->w)
    {
        return y2 < y1 + pRect1->h;
    }

    return 0;
}


ALIVE_VAR(1, 0x5c9162, s16, pal_xpos_5C9162, 0);
ALIVE_VAR(1, 0x5c9160, s16, pal_ypos_5C9160, 0);

ALIVE_VAR(1, 0x5c915c, s16, pal_width_5C915C, 0);
ALIVE_VAR(1, 0x5c915e, s16, pal_free_count_5C915E, 0);

ALIVE_ARY(1, 0x5c9164, s32, 77, sPal_table_5C9164, {}); // TODO: Actually 32 in size ?

static bool Pal_Allocate_Helper(s32& i, s32& palX_idx, s32 maskValue, s32 numBits)
{
    for (i = 0; i < pal_free_count_5C915E; i++)
    {
        if (sPal_table_5C9164[i] != (1 << (pal_width_5C915C + 1)) - 1)
        {
            if (pal_width_5C915C != numBits)
            {
                palX_idx = 0;
                bool foundMatch = true;
                while ((maskValue << palX_idx) & sPal_table_5C9164[i])
                {
                    if (++palX_idx >= pal_width_5C915C - numBits)
                    {
                        foundMatch = false;
                        break;
                    }
                }

                if (foundMatch)
                {
                    return true;
                }
            }
        }
    }

    // Failed, out of pals
    return false;
}

static s16 Pal_Allocate_Impl(PSX_RECT* pRect, u32 paletteColorCount)
{
    if (!pal_free_count_5C915E)
    {
        return 0;
    }

    if (paletteColorCount != 256 && paletteColorCount != 64 && paletteColorCount != 16)
    {
        return 0;
    }

    s32 pal_rect_y = 0;
    s32 palX_idx = 0;
    s32 palBitMask = 0;

    if (paletteColorCount == 16)
    {
        palBitMask = 1;
        if (!Pal_Allocate_Helper(pal_rect_y, palX_idx, palBitMask, 0))
        {
            return 0;
        }
    }
    else if (paletteColorCount == 64)
    {
        palBitMask = 0xF;
        if (!Pal_Allocate_Helper(pal_rect_y, palX_idx, palBitMask, paletteColorCount / 16)) // 64/16 = 4
        {
            return 0;
        }
    }
    else if (paletteColorCount == 256)
    {
        palBitMask = 0xFFFF;
        if (!Pal_Allocate_Helper(pal_rect_y, palX_idx, palBitMask, paletteColorCount / 16)) // 256/16 = 16
        {
            return 0;
        }
    }

    pRect->w = static_cast<s16>(paletteColorCount);

    palBitMask = palBitMask << palX_idx;
    sPal_table_5C9164[pal_rect_y] |= palBitMask;
    pRect->x = static_cast<s16>(pal_xpos_5C9162 + (16 * palX_idx));
    pRect->y = static_cast<s16>(pal_rect_y + pal_ypos_5C9160);
    return 1;
}

EXPORT s16 CC Pal_Allocate_483110(PSX_RECT* pRect, u32 paletteColorCount)
{
    const s16 ret = Pal_Allocate_Impl(pRect, paletteColorCount);
    if (ret == 0 && (GetGameAutoPlayer().IsRecording() || GetGameAutoPlayer().IsPlaying()))
    {
        // pal alloc failure (panto voices: oh no he didn't!)
        LOG_WARNING("Fat pal alloc hax");
        pRect->w = static_cast<s16>(paletteColorCount);
        pRect->h = 1;
        pRect->x = 0;
        pRect->y = 0;
        return 1;
    }
    return ret;
}

EXPORT void CC Pal_free_483390(PSX_Point xy, s16 palDepth)
{
    const s32 palIdx = xy.field_2_y - pal_ypos_5C9160;
    const s32 palWidthBits = xy.field_0_x - pal_xpos_5C9162;

    switch (palDepth)
    {
        case 16:                                                     // 1 bit
            sPal_table_5C9164[palIdx] ^= 1 << ((palWidthBits) / 16); // div 16 to get num bits
            break;
        case 64: // 4 bits
            sPal_table_5C9164[palIdx] ^= 0xF << ((palWidthBits) / 16);
            break;
        case 256: // 16 bits
            sPal_table_5C9164[palIdx] ^= 0xFFFF << ((palWidthBits) / 16);
            break;
    }
}

EXPORT void CC Pal_Area_Init_483080(s16 xpos, s16 ypos, u16 width, u16 height)
{
    pal_xpos_5C9162 = xpos;
    pal_ypos_5C9160 = ypos;

    pal_width_5C915C = width / 4;
    pal_free_count_5C915E = height;

    Vram_alloc_explicit_4955F0(xpos, ypos, xpos + width - 1, ypos + height - 1);

#ifdef TETHYS_SATURN
    // SATURN FIX (bt915): the Saturn CPU CLUT mirror + PalSetData accept only x<512
    // -- the per-row allocation bitmask sPal_table_5C9164[i] is a single s32 = 32
    // bits x 16 colours = 512. The OG width/4 (=160 for the 640-wide PSX CLUT) lets
    // Pal_Allocate_Helper place a palette at palX_idx up to pal_width-numBits (=144
    // for a 256-pal), i.e. x0=16*palX_idx far past 512 (e.g. x0=384 -> 384+256=640).
    // PalSetData then SILENTLY DROPS that write (x0+n>512) -> the anim's mirror row
    // stays 0/stale -> the CAM-embedded bg-anims (R1 elevator barrels/chain/platform)
    // read half-zero/half-stale -> black/white flicker (measured: mr8 rx384). Clamp
    // the packing width to the real 32-unit row so no palette ever straddles 512
    // (256->x0<=240, 64->x0<=432, 16->x0<=496; all fit). This is NOT a capacity cut:
    // colours 512-640 were never tracked by the 32-bit bitmask, so allocating there
    // only ever produced corruption. The Vram reservation above keeps the full 640
    // (moot on Saturn: CLUTs live in the mirror/CRAM, not the PSX VRAM model).
    if (pal_width_5C915C > 32)
    {
        pal_width_5C915C = 32;
    }
#endif

    for (s32 i = 0; i < height; i++)
    {
        sPal_table_5C9164[i] = 0;
    }
}

EXPORT void CC Pal_Copy_483560(PSX_Point pPoint, s16 w, u16* pPalData, PSX_RECT* rect)
{
    rect->x = pPoint.field_0_x;
    rect->y = pPoint.field_2_y;
    rect->w = w;
    rect->h = 1;
    PSX_StoreImage_4F5E90(rect, pPalData);
}

EXPORT u32 CC Pal_Make_Colour_4834C0(u8 r, u8 g, u8 b, s16 bOpaque)
{
    return (bOpaque != 0 ? 0x8000 : 0) + ((u32) r >> 3) + 4 * ((g & 0xF8) + 32 * (b & 0xF8));
}

EXPORT void CC Pal_Set_483510(PSX_Point xy, s16 w, const u8* palData, PSX_RECT* rect)
{
    rect->x = xy.field_0_x;
    rect->y = xy.field_2_y;
    rect->w = w;
    rect->h = 1;
    IRenderer::GetRenderer()->PalSetData(IRenderer::PalRecord{xy.field_0_x, xy.field_2_y, w}, palData);
}

using namespace ::testing;

namespace AETest::TestsVRam {
void Test_VRamAllocate()
{
    /*
        PSX_RECT rect;
        Vram_alloc_4956C0(64, 128, 8, &rect);
        ASSERT_EQ(rect.x, 992);
        ASSERT_EQ(rect.y, 384);
        ASSERT_EQ(rect.w, 32);
        ASSERT_EQ(rect.h, 128);

        PSX_RECT rect2;
        Vram_alloc_4956C0(32, 45, 16, &rect2);
        ASSERT_EQ(rect2.x, 960);
        ASSERT_EQ(rect2.y, 467);
        ASSERT_EQ(rect2.w, 32);
        ASSERT_EQ(rect2.h, 45);

        PSX_RECT rect3;
        Vram_alloc_4956C0(32, 16, 8, &rect3);


        ASSERT_EQ(sVramNumberOfAllocations_5CC888, 3);
        Vram_free_495A60({ rect2.x, rect2.y }, { rect2.w, rect2.h });

        ASSERT_TRUE(memcmp(&sVramAllocations_5CB888[1], &sVramAllocations_5CB888[2], sizeof(PSX_RECT)) == 0);
        ASSERT_EQ(sVramNumberOfAllocations_5CC888, 2);

        Vram_free_495A60({ rect.x, rect.y }, { rect.w, rect.h });
        Vram_free_495A60({ rect3.x, rect2.y }, { rect3.w, rect3.h });
        */
}

void VRamTests()
{
    Test_VRamAllocate();
}
} // namespace AETest::TestsVRam
