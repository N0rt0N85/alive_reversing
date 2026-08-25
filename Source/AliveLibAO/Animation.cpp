#include "stdafx_ao.h"
#include "Animation.hpp"
#include "Function.hpp"
#include "PsxDisplay.hpp"
#include "Primitives.hpp"
#include "Sys_common.hpp"
#include "ResourceManager.hpp"
#include "VRam.hpp"
#include "Game.hpp"
#include "Slig.hpp"
#include "Dove.hpp"
#include "Bullet.hpp"
#include "BulletShell.hpp"
#include "Dove.hpp"
#include "stdlib.hpp"
#include "Sfx.hpp"
#include "Events.hpp"
#include "Particle.hpp"
#include "Compression.hpp"
#include "Abe.hpp"
#include "Throwable.hpp"
#include "Collisions.hpp"
#include "Slog.hpp"
#include "Blood.hpp"
#include "Renderer/IRenderer.hpp"
#include "AnimResources.hpp"
// SATURN (bt1128): THE ANIMATE CENSUS. `a` (overlay row 12) is the largest
// steady-state phase in the port -- 10-11 ms of a ~30 ms tick -- and nothing
// has ever counted anything inside it. Defined in src/sys_saturn.cxx beside the
// kPhase[] argmax that differences them, so they describe THE VERY TICK `a`
// describes. Free-running monotone; never reset here.
extern "C" unsigned int Tethys_gAnWalk;
extern "C" unsigned int Tethys_gAnDecode;
// SATURN: bt1135 -- RAW ticks inside vDecode ALONE. bt1134 split AnimateAll
// into copy (cp) and everything-else and got 41-60 percent copy; the rest was
// assumed to be the walk plus decompression. The walk cannot be it -- this
// loop body is Size(), ItemAt, a flag test and a decrement, and forty of those
// are not 90,000 cycles. So bracket the decompressor and stop assuming:
// ar - cp - dc is then the walk, and it should read ~0. Two FRT reads per
// DECODE (d is 3-22 a tick), which is the cheap end of every bracket this port
// has ever carried.
extern "C" unsigned int Tethys_gDcRaw;
// SATURN bt1138: the decompression-destination A/B -- see the type 4/5 case.
extern "C" const unsigned int Tethys_kDbufScratchBytes;
extern "C" unsigned char* Tethys_gDbufScratch;
extern "C" unsigned int Tethys_gDbufRaw[2];
extern "C" unsigned int Tethys_gDbufBytes[2];
extern "C" unsigned int Tethys_gDbufFall;
extern "C" unsigned int Tethys_gAnimRebind; // ao242.17 'mv': FrameHeader rebinds after a heap compaction
// SATURN (ao262.10): the two silent object-killers, see Init_402D20.
extern "C" unsigned int Tethys_gVramFail;
extern "C" unsigned int Tethys_gPalFail;
extern "C" unsigned int Tethys_gInAnimate; // SATURN (ao242.6): the parent split
extern "C" unsigned int Tethys_RawTicks(void);
// SATURN (ao242.13) the probe gate -- see src/renderer_saturn.cxx.
extern "C" unsigned char Tethys_gProbeOn;
#define TETHYS_PT() (Tethys_gProbeOn ? Tethys_RawTicks() : 0u)
extern "C" unsigned int Tethys_gTear;
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

// Fix pollution from windows.h
#undef min
#undef max

namespace AO {

EXPORT s16* CC Animation_OnFrame_Slig_46F610(void* pObj, s16* pData)
{
    auto pSlig = static_cast<Slig*>(pObj);
    if (pSlig->field_8_update_delay != 0)
    {
        return pData + 2;
    }

    BulletType bulletType = BulletType::ePossessedSlig_0;
    if (pSlig->field_10A_flags.Get(Flags_10A::e10A_Bit2_bPossesed))
    {
        pSlig->field_254_prevent_depossession |= 1u;
        bulletType = BulletType::ePossessedSlig_0;
    }
    else
    {
        bulletType = BulletType::eNormalBullet_1;
    }

    const FP xOff = pSlig->field_BC_sprite_scale * FP_FromInteger(pData[0]);
    const FP yOff = pSlig->field_BC_sprite_scale * FP_FromInteger(pData[1]);
    if (pSlig->field_10_anim.field_4_flags.Get(AnimFlags::eBit5_FlipX))
    {
        auto pBullet = ao_new<Bullet>();
        if (pBullet)
        {
            pBullet->ctor_409380(
                pSlig,
                bulletType,
                pSlig->field_A8_xpos,
                yOff + pSlig->field_AC_ypos,
                FP_FromInteger(-640),
                0,
                pSlig->field_BC_sprite_scale,
                0);
        }

        New_ShootingFire_Particle_419720(
            pSlig->field_A8_xpos - xOff,
            pSlig->field_AC_ypos + yOff,
            1,
            pSlig->field_BC_sprite_scale);

        auto pBulletShell = ao_new<BulletShell>();
        if (pBulletShell)
        {
            pBulletShell->ctor_462790(
                pSlig->field_A8_xpos,
                pSlig->field_AC_ypos + yOff,
                0,
                pSlig->field_BC_sprite_scale);
        }
    }
    else
    {
        auto pBullet = ao_new<Bullet>();
        if (pBullet)
        {
            pBullet->ctor_409380(
                pSlig,
                bulletType,
                pSlig->field_A8_xpos,
                yOff + pSlig->field_AC_ypos,
                FP_FromInteger(640),
                0,
                pSlig->field_BC_sprite_scale,
                0);
        }

        New_ShootingFire_Particle_419720(
            pSlig->field_A8_xpos + xOff,
            pSlig->field_AC_ypos + yOff,
            0,
            pSlig->field_BC_sprite_scale);

        auto pBulletShell = ao_new<BulletShell>();
        if (pBulletShell)
        {
            pBulletShell->ctor_462790(
                pSlig->field_A8_xpos,
                pSlig->field_AC_ypos + yOff,
                1,
                pSlig->field_BC_sprite_scale);
        }
    }

    if (pSlig->field_BC_sprite_scale == FP_FromDouble(0.5))
    {
        SFX_Play_43AD70(SoundEffect::SligShoot_6, 85);
    }
    else
    {
        SFX_Play_43AD70(SoundEffect::SligShoot_6, 0);
    }

    Event_Broadcast_417220(kEvent_2, pSlig);
    Event_Broadcast_417220(kEvent_14, pSlig);

    Dove::All_FlyAway_40F390();

    return pData + 2;
}

EXPORT s16* CC Animation_OnFrame_ZBallSmacker_41FB00(void* pObj, s16* pData);

EXPORT s16* CC Slog_OnFrame_471FD0(void* pObj, s16* pData)
{
    auto pSlog = static_cast<Slog*>(pObj);
    if (pSlog->field_10C_pTarget)
    {
        PSX_RECT targetRect = {};
        pSlog->field_10C_pTarget->VGetBoundingRect(&targetRect, 1);

        PSX_RECT slogRect = {};
        pSlog->VGetBoundingRect(&slogRect, 1);

        if (RectsOverlap(slogRect, targetRect))
        {
            if (pSlog->field_10C_pTarget->field_BC_sprite_scale == pSlog->field_BC_sprite_scale && !pSlog->field_110)
            {
                if (pSlog->field_10C_pTarget->VTakeDamage(pSlog))
                {
                    FP blood_xpos = {};
                    if (pSlog->field_10_anim.field_4_flags.Get(AnimFlags::eBit5_FlipX))
                    {
                        blood_xpos = pSlog->field_A8_xpos - (pSlog->field_BC_sprite_scale * FP_FromInteger(pData[0]));
                    }
                    else
                    {
                        blood_xpos = pSlog->field_A8_xpos + (pSlog->field_BC_sprite_scale * FP_FromInteger(pData[0]));
                    }

                    const FP blood_ypos = (pSlog->field_BC_sprite_scale * FP_FromInteger(pData[1])) + pSlog->field_AC_ypos;

                    auto pBlood = ao_new<Blood>();
                    if (pBlood)
                    {
                        pBlood->ctor_4072B0(
                            blood_xpos,
                            blood_ypos - FP_FromInteger(8),
                            (pSlog->field_B4_velx * FP_FromInteger(2)),
                            FP_FromInteger(0),
                            pSlog->field_BC_sprite_scale,
                            50);
                    }

                    pSlog->field_110 = 1;

                    SFX_Play_43AD70(SoundEffect::SlogBite_39, 0);
                }
            }
        }
    }

    return pData + 2;
}

const FP_Point kAbeVelTable_4C6608[6] = {
    {FP_FromInteger(3), FP_FromInteger(-14)},
    {FP_FromInteger(10), FP_FromInteger(-10)},
    {FP_FromInteger(15), FP_FromInteger(-8)},
    {FP_FromInteger(10), FP_FromInteger(3)},
    {FP_FromInteger(10), FP_FromInteger(-4)},
    {FP_FromInteger(4), FP_FromInteger(-3)}};

EXPORT s16* CC Abe_OnFrame_429E30(void* pObj, s16* pData)
{
    auto pAbe = static_cast<Abe*>(pObj);

    FP xVel = kAbeVelTable_4C6608[pAbe->field_19D_throw_direction].field_0_x * pAbe->field_BC_sprite_scale;
    const FP yVel = kAbeVelTable_4C6608[pAbe->field_19D_throw_direction].field_4_y * pAbe->field_BC_sprite_scale;

    FP directed_x = {};
    if (sActiveHero_507678->field_10_anim.field_4_flags.Get(AnimFlags::eBit5_FlipX))
    {
        xVel = -xVel;
        directed_x = -(pAbe->field_BC_sprite_scale * FP_FromInteger(pData[0]));
    }
    else
    {
        directed_x = (pAbe->field_BC_sprite_scale * FP_FromInteger(pData[0]));
    }

    FP data_y = FP_FromInteger(pData[1]);

    FP hitX = {};
    FP hitY = {};
    PathLine* pLine = nullptr;
    if (sCollisions_DArray_504C6C->RayCast_40C410(
            pAbe->field_A8_xpos,
            pAbe->field_AC_ypos + data_y,
            pAbe->field_A8_xpos + directed_x,
            pAbe->field_AC_ypos + data_y,
            &pLine,
            &hitX,
            &hitY,
            pAbe->field_BC_sprite_scale != FP_FromDouble(0.5) ? 6 : 0x60))
    {
        directed_x = hitX - pAbe->field_A8_xpos;
        xVel = -xVel;
    }

    if (sActiveHero_507678->field_198_pThrowable)
    {
        sActiveHero_507678->field_198_pThrowable->field_A8_xpos = directed_x + sActiveHero_507678->field_A8_xpos;
        BaseThrowable* pThrowable = sActiveHero_507678->field_198_pThrowable;
        pThrowable->field_AC_ypos = (pAbe->field_BC_sprite_scale * data_y) + sActiveHero_507678->field_AC_ypos;
        pThrowable->VThrow(xVel, yVel);
        pThrowable->field_BC_sprite_scale = pAbe->field_BC_sprite_scale;
        sActiveHero_507678->field_198_pThrowable = nullptr;
    }
    return pData + 2;
}

TFrameCallBackType kAbe_Anim_Frame_Fns_4CEBEC[] = {Abe_OnFrame_429E30};
TFrameCallBackType kSlig_Anim_Frame_Fns_4CEBF0[] = {Animation_OnFrame_Slig_46F610};
TFrameCallBackType kSlog_Anim_Frame_Fns_4CEBF4[] = {Slog_OnFrame_471FD0};
TFrameCallBackType kZBall_Anim_Frame_Fns_4CEBF8[] = {Animation_OnFrame_ZBallSmacker_41FB00};

void Animation::vDecode()
{
    VDecode_403550();
}

static IRenderer::BitDepth AnimFlagsToBitDepth(const BitField32<AnimFlags>& flags)
{
    if (flags.Get(AnimFlags::eBit14_Is16Bit))
    {
        return IRenderer::BitDepth::e16Bit;
    }
    else if (flags.Get(AnimFlags::eBit13_Is8Bit))
    {
        return IRenderer::BitDepth::e8Bit;
    }
    return IRenderer::BitDepth::e4Bit;
}

#ifdef TETHYS_SATURN
// SATURN (bt863): both last-screen crashes trace to a corrupt/stale resource
// block pointer (*field_20_ppBlock resolved to 0x1A57120F -- not cart/HWRAM/LWRAM).
// A garbage block feeds garbage into BOTH the animation callback dispatch
// (field_1C_fn_ptr_array[*data] -> wild function pointer -> the 0x0D773xxx jump)
// AND UploadTexture (garbage compression_type 188 -> the CMP fatal). VDecode's
// existing guard only rejected null, not garbage. Validate the block lands in a
// real region; if not, SKIP the frame (draw stale, never crash) and record the
// signature so the corruption SOURCE (which anim, is the address consistent) is
// visible without a death screen. This is a firewall, not the root fix.
extern "C" volatile s32 Tethys_gAnimBadBlock = 0; // count of skipped garbage-block decodes
static inline bool Tethys_BlockPtrSane(const u8* b)
{
    const u32 a = reinterpret_cast<u32>(b);
    return (a >= 0x02400000u && a < 0x02800000u)   // cart, cached
        || (a >= 0x22400000u && a < 0x22800000u)   // cart, uncached
        || (a >= 0x06000000u && a < 0x06100000u)   // HWRAM
        || (a >= 0x00200000u && a < 0x00300000u);  // LWRAM
}

// SATURN (bt955): the SAME corruption class also reaches the UPDATE/CULLING
// path, which the bt863 firewall above never covered -- it only guards
// VDecode_403550. Field death on hardware (SH-2 CPU address error, fr91475,
// ~51 min in): Is_In_Current_Camera_417CC0 -> VGetBoundingRect_418120 ->
// Get_FrameHeader_403A00, whose result is dereferenced with no test at all
// 23 call sites. Gauges for the twin firewall installed there:
//   n   = rejects
//   why = 1 no/insane block, 2 frame index out of range, 3 misaligned or
//         out-of-region frame header
//   ptr = the last rejected value (the SAME address every time names a single
//         stale owner; drifting addresses mean a wandering write)
extern "C" volatile s32 Tethys_gAnimBadFrame = 0;
extern "C" volatile s32 Tethys_gAnimBadWhy = 0;
extern "C" volatile u32 Tethys_gAnimBadPtr = 0;
#endif

void Animation::UploadTexture(const FrameHeader* pFrameHeader, const PSX_RECT& vram_rect, s16 width_bpp_adjusted)
{
    IRenderer& renderer = *IRenderer::GetRenderer();
#ifdef TETHYS_SATURN
    // SATURN ROOT FIX (ao242.17) -- pFrameHeader IS A RAW DEREF OF A MOVABLE
    // BLOCK, AND EVERYTHING BELOW CAN MOVE IT.
    //
    // THE POINTER. vDecode builds it as
    //     &(*field_20_ppBlock)[pFrameInfoHeader->field_0_frame_header_offset]
    // (this file, in VDecode just above the call to us) -- a raw u8* into this
    // animation's own resource block, passed down here and then used both as
    // the compression-type discriminator AND as the DECOMPRESSION SOURCE
    // (&pFrameHeader->field_8_width2 in every arm of the switch).
    //
    // WHAT MOVES IT. Two allocation sites sit BETWEEN that deref and its use:
    // the bt870 buffer growth immediately below, and EnsureDecompressionBuffer
    // inside four of the five switch arms. Both call
    // Alloc_New_Resource_454F20, i.e. Alloc_New_Resource_Impl with
    // bReclaimOnFail = true, and on a first-fit miss that runs
    // Reclaim_Memory_455660(0) -- which, because sizeToReclaim == 0 is
    // rewritten to kResHeapSize (ResourceManager.cpp, top of the function),
    // COMPACTS THE WHOLE HEAP and memmoves every non-locked block. The
    // animation block is non-locked: Anim chunks are created with
    // Alloc_New_Resource_454F20 (locked = false), including the CAM-EMBEDDED
    // ones -- which is exactly what the R1P15C07/C08 background barrels are
    // (kBgAnimRecords maps them to "R1P15C07.CAM" +24764 and "R1P15C08.CAM"
    // +28752, not to any .BAN).
    //
    // So the sequence is: deref -> allocate -> heap compacts -> decompress from
    // a pointer that now names somebody else's bytes. bt828 already wrote this
    // hazard class down at the reclaim retry itself ("any live object that
    // cached a RAW deref (u8*) of a moved block is left with a DANGLING
    // non-null pointer"); this is that class, on the animation decode path.
    // The handle survives the move, so the repair is free: keep the OFFSET and
    // re-derive.
    //
    // WHY THIS IS SHAPED AS A HOIST RATHER THAN FIVE REBINDS. Doing the
    // allocating UP FRONT -- both sites, before the switch -- leaves exactly
    // ONE window to close instead of five, and the arms then find the buffer
    // already present so their own EnsureDecompressionBuffer cannot allocate
    // and cannot move anything. Behaviour is identical: every arm that calls it
    // would have called it immediately anyway, and type 0 (which needs no
    // buffer) is excluded by the same test the switch uses.
    //
    // WHEN IT FIRES, which is why the report is "it corrupts WHEN I pull the
    // lever / chant" rather than "it is always wrong": the first fit only
    // misses once the heap is fragmented, and those events are precisely what
    // fragments it -- each new object (the lift wheel and pulley starting to
    // animate, the BirdPortal's terminators and sparks) takes its own resource,
    // vram and palette allocations mid-screen. 'mv' on row 7 counts the
    // rebinds: mv00 for a whole screen means no block moved under a decode
    // there and this fix is not what changed anything.
    const u8* const fhBlock0 = field_20_ppBlock ? *field_20_ppBlock : nullptr;
    const u32 fhOff = fhBlock0
                          ? static_cast<u32>(reinterpret_cast<const u8*>(pFrameHeader) - fhBlock0)
                          : 0u;
    // SATURN ROOT FIX (bt870): Type 4/5 (LZSS) embeds its TRUE decompressed
    // length as the first u32 of the payload, and Decompress_Type_4_5 writes
    // exactly that many bytes -- IGNORING field_28_dbuf_size. For several frames
    // that dest_len EXCEEDS the buffer sized from the anim's maxW/maxH (offline
    // tools/check_anim_overrun.py: R1ROPES.BAN id=1000 the pull-cable +56 B,
    // FXFLARE +12, PORTAL +16 -- all cmp5). The write then overruns the
    // decompression buffer and stomps the adjacent heap block's Header
    // (0x0B0B0B0C) -> HI!=0 "walking as Abe" at screen 8, Abe's palette lost
    // first. PC's 5 MB heap absorbs the spill; Saturn's packed cart heap does
    // not. GROW the buffer to hold the full frame before decompressing (renders
    // correctly, no clip). Sanity-capped so a garbage dest_len can't OOM.
    {
        const CompressionType ct = pFrameHeader->field_7_compression_type;
        if (ct == CompressionType::eType_4_RLE || ct == CompressionType::eType_5_RLE)
        {
            const u32 destLen = *reinterpret_cast<const u32*>(&pFrameHeader->field_8_width2);
            if (destLen > field_28_dbuf_size && destLen <= 0x40000u) // <=256K sanity cap
            {
                if (field_24_dbuf)
                {
                    ResourceManager::FreeResource_455550(field_24_dbuf);
                }
                field_28_dbuf_size = destLen;
                // SATURN (ao262.2): NON-FATAL. The `return` below is this
                // site's whole point -- skip the frame rather than overrun --
                // and it was unreachable, because Alloc_New_Resource_454F20
                // fatals on failure. destLen is up to 256 KB; it fits the 4 MB
                // cart heap and does not fit the 1 MB no-cart one, which is
                // exactly the shape of "fatal on death, only without a cart".
                field_24_dbuf = ResourceManager::Alloc_New_Resource_ImplEx(
                    ResourceManager::Resource_DecompressionBuffer, 0, destLen,
                    false, ResourceManager::BlockAllocMethod::eFirstMatching,
                    true /*reclaim*/, false /*never fatal -- see below*/);
                if (!field_24_dbuf)
                {
                    return; // cannot fit this frame -> skip it, never overrun
                }
            }
        }
    }
    // ao242.17: the SECOND allocating site, hoisted out of the switch arms so
    // the rebind below covers it too. Type 0 uploads straight from the block
    // and must not be made to allocate a buffer it never reads.
    if (pFrameHeader->field_7_compression_type != CompressionType::eType_0_NoCompression)
    {
        EnsureDecompressionBuffer(); // result re-tested by each arm, unchanged
    }
    // ao242.17: THE ONE WINDOW, CLOSED. Nothing above this line derefs
    // pFrameHeader after an allocation; nothing below it allocates.
    if (fhBlock0 && field_20_ppBlock && *field_20_ppBlock != fhBlock0)
    {
        pFrameHeader = reinterpret_cast<const FrameHeader*>(*field_20_ppBlock + fhOff);
        Tethys_gAnimRebind++; // 'mv' -- a block really did move under a decode
    }
#endif
    switch (pFrameHeader->field_7_compression_type)
    {
        case CompressionType::eType_0_NoCompression:
            renderer.Upload(AnimFlagsToBitDepth(field_4_flags), vram_rect, reinterpret_cast<const u8*>(&pFrameHeader->field_8_width2));
            break;

        case CompressionType::eType_1_NotUsed:
            if (EnsureDecompressionBuffer())
            {
                // TODO: Refactor structure to get pixel data/remove casts
                Decompress_Type_1_403150(
                    (u8*) &pFrameHeader[1],
                    *field_24_dbuf,
                    *(u32*) &pFrameHeader->field_8_width2,
                    2 * pFrameHeader->field_5_height * width_bpp_adjusted);
                renderer.Upload(AnimFlagsToBitDepth(field_4_flags), vram_rect, *field_24_dbuf);
            }
            break;

        case CompressionType::eType_2_ThreeToFourBytes:
            if (EnsureDecompressionBuffer())
            {
                Decompress_Type_2_403390(
                    (u8*) &pFrameHeader[1],
                    *field_24_dbuf,
                    2 * pFrameHeader->field_5_height * width_bpp_adjusted);
                renderer.Upload(AnimFlagsToBitDepth(field_4_flags), vram_rect, *field_24_dbuf);
            }
            break;

        case CompressionType::eType_3_RLE_Blocks:
            if (EnsureDecompressionBuffer())
            {
                // TODO: Refactor structure to get pixel data/remove casts
                Decompress_Type_3_4031E0(
                    (u8*) &pFrameHeader[1],
                    *field_24_dbuf,
                    *(u32*) &pFrameHeader->field_8_width2,
                    2 * pFrameHeader->field_5_height * width_bpp_adjusted);
                renderer.Upload(AnimFlagsToBitDepth(field_4_flags), vram_rect, *field_24_dbuf);
            }
            break;

        case CompressionType::eType_4_RLE:
        case CompressionType::eType_5_RLE:
            if (EnsureDecompressionBuffer())
            {
#ifdef TETHYS_SATURN
                // SATURN (ao240.2) THE BUFFER LIVES IN LWRAM NOW, AND THE THREE-ARM
                // A/B THAT PUT IT THERE IS RETIRED WITH ITS ANSWER.
                //
                // WHAT THE ARMS MEASURED. Same decoder, same frame, the only thing
                // differing between arms was this destination pointer: arm A the
                // Resource_DecompressionBuffer (in cart mode, the cartridge --
                // A-bus), arm B an LWRAM scratch, arm C the SAME cart bytes through
                // the UNCACHED window (0x224xxxxx). Real hardware, ao240.1,
                // 2026-08-20, 154 samples whose three coverage counts agreed inside
                // 15 %:
                //     rB/rA = 0.881  (0.853 .. 0.910)   LWRAM ~12 % faster
                //     rC/rA = 1.800  (1.667 .. 1.851)   uncached ~80 % worse
                // The second number is the interesting one and it is now in
                // ../saturn-refs/knowledge/HW_MEMORY_AND_BUS.md: the SH-2 cache is
                // WRITE-THROUGH, so the decoder's stores leave on the bus in both
                // arms -- the entire 80 % is carried by its READS, i.e. the LZSS
                // back-references re-reading the destination it just wrote. So a
                // read-modify loop still wants a cached window even when every
                // write is external, and a "we are only writing, the cache cannot
                // help" argument is wrong. (That argument was mine, at bt1142.)
                //
                // WHY THE ARMS HAD TO EXIST AT ALL. On Ymir the same experiment
                // read rB/rA = 1.00 and rC/rA = 1.18, i.e. "the two placements are
                // equivalent". That is not a null result, it is an ABSENT
                // measurement -- Ymir models the cache partially and the A-bus not
                // at all -- and it was nearly published as a refutation.
                //
                // WHY THE COVERAGE CHECK WAS LOAD-BEARING, since the next A/B will
                // want it: decompression cost per byte varies 3.2x with content, so
                // unequal arms compare different work. Two designs died on it. A
                // per-VBLANK flip (bt1138) cancelled itself at ft020 and gave 4.6:1.
                // Frame PARITY (bt1140) still gave 2.25:1, because parity ALIASES
                // WITH ANIMATION PERIOD -- an anim whose frame lasts an even number
                // of ticks decodes on the same parity forever, which is a property
                // of the content that no per-frame counter can fix. Only a PER
                // DECODE counter splits every animation evenly.
                //
                // WHAT SHIPS. LWRAM unconditionally, whenever the frame fits the
                // scratch. The fallback is the old cart buffer and it is counted
                // (fb): the scratch is 32 KiB against a measured worst R1 frame of
                // 13,104 B, and Tethys_gDbufFall read 0 on every capture it ever
                // shipped in -- but a count that is asserted rather than measured
                // is how bt999 died, so it keeps its column.
                //   Sizing, so nobody expects the frame rate to move: on hardware
                // the LZSS is ~7 ms of a frame ((dc-cp)/kw = 0.494 against cp/kw =
                // 0.115, so it is 4.3x the copy), and 12 % of that is ~0.85 ms. It
                // is free -- LWRAM had 632 KB idle -- and it is not the fps story.
                u8* pDst = *field_24_dbuf;
                if (Tethys_gDbufScratch != nullptr
                    && field_28_dbuf_size <= Tethys_kDbufScratchBytes)
                {
                    pDst = Tethys_gDbufScratch;
                }
                else
                {
                    Tethys_gDbufFall++; // 'fb': this frame stayed in the old buffer
                }
                {
                    // ONE rate now, not three. Kept because the next hardware slot
                    // has to see that the move actually took: r should land near
                    // the old rB (~141) and nowhere near the old rA (~161).
                    const unsigned int t0 = TETHYS_PT();
                    Decompress_Type_4_5_461770(reinterpret_cast<const u8*>(&pFrameHeader->field_8_width2), pDst);
                    // SATURN (ao242.6) THE PARENT SPLIT. vDecode is reached from
                    // AnimateAll AND from VUpdate (Set_Animation_Data_402A40,
                    // Init_402D20), so every tick charged to [0] was charged to
                    // the animate phase whether it happened there or not -- which
                    // is why the a-subtree could never sum. [1] takes the
                    // out-of-phase half; the array already had three slots and
                    // only [0] was ever written. Both raw AND bytes are split, so
                    // row 7's r stays a true rate over its own population.
                    const u32 kPar = Tethys_gInAnimate ? 0u : 1u;
                    Tethys_gDbufRaw[kPar] += TETHYS_PT() - t0; // ao262.18: unguarded
                    Tethys_gDbufBytes[kPar] += *reinterpret_cast<const u32*>(&pFrameHeader->field_8_width2);
                }
                renderer.Upload(AnimFlagsToBitDepth(field_4_flags), vram_rect, pDst);
#else
                // TODO: Refactor structure to get pixel data/remove casts
                Decompress_Type_4_5_461770(reinterpret_cast<const u8*>(&pFrameHeader->field_8_width2), *field_24_dbuf);
                renderer.Upload(AnimFlagsToBitDepth(field_4_flags), vram_rect, *field_24_dbuf);
#endif
            }
            break;

        default:
#ifdef TETHYS_SATURN
            // FIREWALL backstop (bt863): a block that passed the VDecode range
            // check but still yields a garbage compression_type (an in-region but
            // clobbered/mis-offset frame). SKIP the upload -- draw stale, never
            // crash. Counted with the out-of-range skips (Tethys_gAnimBadBlock).
            Tethys_gAnimBadBlock++;
            break;
#else
            LOG_ERROR("Unknown compression type " << static_cast<s32>(pFrameHeader->field_7_compression_type));
            ALIVE_FATAL("Unknown compression type");
            break;
#endif
    }
}

void Animation::VDecode_403550()
{
    if (!field_20_ppBlock || !*field_20_ppBlock)
    {
        return;
    }
#ifdef TETHYS_SATURN
    // FIREWALL (bt863): a garbage block pointer (use-after-free / clobbered handle)
    // would otherwise reach the callback dispatch (wild jump) and UploadTexture
    // (CMP fatal). Skip the decode and record it instead of crashing.
    if (!Tethys_BlockPtrSane(*field_20_ppBlock))
    {
        Tethys_gAnimBadBlock++; // firewall skip count (an/ap detail dropped bt867 to fund the overrun fix)
        return;
    }
#endif

    AnimationHeader* pAnimationHeader = reinterpret_cast<AnimationHeader*>(&(*field_20_ppBlock)[field_18_frame_table_offset]);
    if (pAnimationHeader->field_2_num_frames == 1 && field_4_flags.Get(AnimFlags::eBit12_ForwardLoopCompleted))
    {
        return;
    }

    if (field_4_flags.Get(AnimFlags::eBit19_LoopBackwards))
    {
        // Loop backwards
        const s16 prevFrameNum = --field_92_current_frame;
        field_E_frame_change_counter = static_cast<s16>(field_10_frame_delay);

        if (prevFrameNum < pAnimationHeader->field_4_loop_start_frame)
        {
            if (field_4_flags.Get(AnimFlags::eBit8_Loop))
            {
                // Loop to last frame
                field_92_current_frame = pAnimationHeader->field_2_num_frames - 1;
            }
            else
            {
                // Stay on current frame
                field_E_frame_change_counter = 0;
                field_92_current_frame = prevFrameNum + 1;
            }
        }

        // Is first (last since running backwards) frame?
        if (field_92_current_frame == 0)
        {
            field_4_flags.Set(AnimFlags::eBit18_IsLastFrame);
        }
        else
        {
            field_4_flags.Clear(AnimFlags::eBit18_IsLastFrame);
        }
    }
    else
    {
        // Loop forwards
        const s16 nextFrameNum = ++field_92_current_frame;
        field_E_frame_change_counter = static_cast<s16>(field_10_frame_delay);

        // Animation reached end point
        if (nextFrameNum >= pAnimationHeader->field_2_num_frames)
        {
            if (field_4_flags.Get(AnimFlags::eBit8_Loop))
            {
                // Loop back to loop start frame
                field_92_current_frame = pAnimationHeader->field_4_loop_start_frame;
            }
            else
            {
                // Stay on current frame
                field_92_current_frame = nextFrameNum - 1;
                field_E_frame_change_counter = 0;
            }

            field_4_flags.Set(AnimFlags::eBit12_ForwardLoopCompleted);
        }

        // Is last frame ?
        if (field_92_current_frame == pAnimationHeader->field_2_num_frames - 1)
        {
            field_4_flags.Set(AnimFlags::eBit18_IsLastFrame);
        }
        else
        {
            field_4_flags.Clear(AnimFlags::eBit18_IsLastFrame);
        }
    }

    if (field_4_flags.Get(AnimFlags::eBit11_bToggle_Bit10))
    {
        field_4_flags.Toggle(AnimFlags::eBit10_alternating_flag);
    }

    const FrameInfoHeader* pFrameInfoHeader = Get_FrameHeader_403A00(-1); // -1 = use current frame
    if (pFrameInfoHeader->field_6_count > 0)
    {
        if (field_1C_fn_ptr_array)
        {
            FrameInfoHeader* pFrameHeaderCopy = this->Get_FrameHeader_403A00(-1);

            // This data can be an array of u32's + other data up to field_6_count
            // which appears AFTER the usual data.

            // TODO: Should be typed to s16* ??
            const u32* pCallBackData = reinterpret_cast<const u32*>(&pFrameHeaderCopy->field_8_data.points[3]);
            for (s32 i = 0; i < pFrameHeaderCopy->field_6_count; i++)
            {
                const auto pFnCallBack = field_1C_fn_ptr_array[*pCallBackData];
                if (!pFnCallBack)
                {
                    break;
                }
                pCallBackData++; // Skip the array index
                // Pass the data pointer into the call back which will then read and skip any extra data
                pCallBackData += *pFnCallBack(field_94_pGameObj, (s16*) pCallBackData);
            }
        }
    }

    const FrameHeader* pFrameHeader = reinterpret_cast<const FrameHeader*>(&(*field_20_ppBlock)[pFrameInfoHeader->field_0_frame_header_offset]);

    // No VRAM allocation
    if (field_84_vram_rect.w <= 0)
    {
        return;
    }

    s16 width_bpp_adjusted = 0;
    if (field_4_flags.Get(AnimFlags::eBit13_Is8Bit))
    {
        // 8 bit, divided by half
        width_bpp_adjusted = ((pFrameHeader->field_4_width + 3) / 2) & ~1;
    }
    else if (field_4_flags.Get(AnimFlags::eBit14_Is16Bit))
    {
        // 16 bit, only multiple of 2 rounding
        width_bpp_adjusted = (pFrameHeader->field_4_width + 1) & ~1;
    }
    else
    {
        // 4 bit divide by quarter
        width_bpp_adjusted = ((pFrameHeader->field_4_width + 7) / 4) & ~1;
    }

    PSX_RECT vram_rect = {
        field_84_vram_rect.x,
        field_84_vram_rect.y,
        width_bpp_adjusted,
        pFrameHeader->field_5_height,
    };

    // Clamp width
    if (vram_rect.w > field_84_vram_rect.w)
    {
        vram_rect.w = field_84_vram_rect.w;
    }

    // Clamp height
    if (pFrameHeader->field_5_height > field_84_vram_rect.h)
    {
        vram_rect.h = field_84_vram_rect.h;
    }

    UploadTexture(pFrameHeader, vram_rect, width_bpp_adjusted);
}

void Animation::vRender(s32 xpos, s32 ypos, PrimHeader** ppOt, s16 width, s16 height)
{
    VRender_403AE0(xpos, ypos, ppOt, width, height);
}

void Animation::VRender_403AE0(s32 xpos, s32 ypos, PrimHeader** ppOt, s16 width, s16 height)
{
    if (field_4_flags.Get(AnimFlags::eBit3_Render))
    {
        u8** ppBlock = field_20_ppBlock;
        if (ppBlock)
        {
            const u8* pBlock = *ppBlock;
            const FrameInfoHeader* pFrameInfoHeader = Get_FrameHeader_403A00(-1);
            const FrameHeader* pFrameHeader = (const FrameHeader*) &pBlock[pFrameInfoHeader->field_0_frame_header_offset];

            FP frame_width_fixed = {};
            FP frame_height_fixed = {};
            if (width)
            {
                frame_width_fixed = FP_FromInteger(width);
                frame_height_fixed = FP_FromInteger(height);
            }
            else
            {
                frame_width_fixed = FP_FromInteger(PCToPsxX(pFrameHeader->field_4_width, 20));
                frame_height_fixed = FP_FromInteger(pFrameHeader->field_5_height);
            }

            FP xOffSet_fixed = {};
            FP yOffset_fixed = {};
            if (field_4_flags.Get(AnimFlags::eBit20_use_xy_offset))
            {
                xOffSet_fixed = FP_FromInteger(0);
                yOffset_fixed = FP_FromInteger(0);
            }
            else
            {
                xOffSet_fixed = FP_FromInteger(pFrameInfoHeader->field_8_data.offsetAndRect.mOffset.x);
                yOffset_fixed = FP_FromInteger(pFrameInfoHeader->field_8_data.offsetAndRect.mOffset.y);
            }

            TPageMode textureMode = {};
            if (field_4_flags.Get(AnimFlags::eBit13_Is8Bit))
            {
                textureMode = TPageMode::e8Bit_1;
            }
            else if (field_4_flags.Get(AnimFlags::eBit14_Is16Bit))
            {
                textureMode = TPageMode::e16Bit_2;
            }
            else
            {
                textureMode = TPageMode::e4Bit_0;
            }

            s16 tPageY = 0;
            if (field_4_flags.Get(AnimFlags::eBit10_alternating_flag) || field_84_vram_rect.y >= 256)
            {
                tPageY = 256;
            }
            else
            {
                tPageY = 0;
            }

            Poly_FT4* pPoly = &field_2C_ot_data[gPsxDisplay_504C78.field_A_buffer_index];
            PolyFT4_Init(pPoly);
            Poly_Set_SemiTrans_498A40(&pPoly->mBase.header, field_4_flags.Get(AnimFlags::eBit15_bSemiTrans));
            Poly_Set_Blending_498A00(&pPoly->mBase.header, field_4_flags.Get(AnimFlags::eBit16_bBlending));

            SetRGB0(pPoly, field_8_r, field_9_g, field_A_b);
            SetTPage(pPoly, static_cast<s16>(PSX_getTPage_4965D0(textureMode, field_B_render_mode, field_84_vram_rect.x, tPageY)));
            SetClut(pPoly, static_cast<s16>(PSX_getClut_496840(field_8C_pal_vram_xy.field_0_x, field_8C_pal_vram_xy.field_2_y)));

            u8 u1 = field_84_vram_rect.x & 63;
            if (textureMode == TPageMode::e8Bit_1)
            {
                u1 *= 2;
            }
            else if (textureMode == TPageMode::e4Bit_0)
            {
                u1 *= 4;
            }
            else
            {
                // 16 bit
            }

            const u8 v0 = static_cast<u8>(field_84_vram_rect.y);
            const u8 u0 = pFrameHeader->field_4_width + u1 - 1;
            const u8 v1 = pFrameHeader->field_5_height + v0 - 1;

            if (field_14_scale != FP_FromInteger(1))
            {
                // Apply scale to x/y pos
                frame_height_fixed = (frame_height_fixed * field_14_scale);
                frame_width_fixed = (frame_width_fixed * field_14_scale);

                // Apply scale to x/y offset
                xOffSet_fixed = (xOffSet_fixed * field_14_scale);
                yOffset_fixed = (yOffset_fixed * field_14_scale) - FP_FromInteger(1);
            }

            s32 polyXPos;
            if (field_4_flags.Get(AnimFlags::eBit5_FlipX))
            {
                SetUV0(pPoly, u0, v0);
                SetUV1(pPoly, u1, v0);
                SetUV2(pPoly, u0, v1);
                SetUV3(pPoly, u1, v1);

                polyXPos = xpos - FP_GetExponent(xOffSet_fixed + frame_width_fixed + FP_FromDouble(0.499));
            }
            else
            {
                SetUV0(pPoly, u1, v0);
                SetUV1(pPoly, u0, v0);
                SetUV2(pPoly, u1, v1);
                SetUV3(pPoly, u0, v1);

                polyXPos = xpos + FP_GetExponent(xOffSet_fixed + FP_FromDouble(0.499));
            }

            const s16 polyYPos = static_cast<s16>(ypos + FP_GetExponent((yOffset_fixed + FP_FromDouble(0.499))));
            const s16 xConverted = static_cast<s16>(PsxToPCX(polyXPos));
            const s16 width_adjusted = FP_GetExponent(PsxToPCX(frame_width_fixed) - FP_FromDouble(0.501)) + xConverted;
            const s16 height_adjusted = FP_GetExponent(frame_height_fixed - FP_FromDouble(0.501)) + polyYPos;

            SetXY0(pPoly, xConverted, polyYPos);
            SetXY1(pPoly, width_adjusted, polyYPos);
            SetXY2(pPoly, xConverted, height_adjusted);
            SetXY3(pPoly, width_adjusted, height_adjusted);

            SetPrimExtraPointerHack(pPoly, nullptr);
            OrderingTable_Add_498A80(OtLayer(ppOt, field_C_layer), &pPoly->mBase.header);
        }
    }
}

void Animation::vCleanUp()
{
    VCleanUp_403F40();
}

void Animation::VCleanUp_403F40()
{
    if (field_4_flags.Get(AnimFlags::eBit17_bFreeResource))
    {
        ResourceManager::FreeResource_455550(field_20_ppBlock);
#ifdef TETHYS_SATURN
        // SATURN ROOT FIX (bt865): idempotency, mirroring the vram_rect.w=0 and
        // pal_depth=0 guards below (added when the dtor was changed to ALWAYS
        // vCleanUp). This free was left non-idempotent: on a persistent/pooled
        // Animation (eSurviveDeathReset objects like Abe, alive across screen
        // walks) a SECOND vCleanUp re-frees field_20_ppBlock. By then the heap
        // node may have been recycled to a NEW resource, so the stale handle
        // DECREMENTS THE NEW OCCUPANT -> premature free of a still-live block ->
        // the next alloc coalesces + overruns its neighbour's Header (observed:
        // heap corruption "walking as Abe", Mine_Flash stomp, sprites vanish).
        // Clear the ownership flag + null the handle so a repeat vCleanUp is a
        // no-op, exactly like the two frees that follow.
        field_4_flags.Clear(AnimFlags::eBit17_bFreeResource);
        field_20_ppBlock = nullptr;
#endif
    }

    gObjList_animations_505564->Remove_Item(this);


    // inlined Animation_Pal_Free ?
    if (field_84_vram_rect.w > 0)
    {
        Vram_free_450CE0({field_84_vram_rect.x, field_84_vram_rect.y}, {field_84_vram_rect.w, field_84_vram_rect.h});
        field_84_vram_rect.w = 0; // SATURN: idempotent -- a second vCleanUp must not double-free
    }

    if (field_90_pal_depth > 0)
    {
        IRenderer::GetRenderer()->PalFree(IRenderer::PalRecord{field_8C_pal_vram_xy.field_0_x, field_8C_pal_vram_xy.field_2_y, field_90_pal_depth});
        field_90_pal_depth = 0; // SATURN: idempotent -- the dtor may vCleanUp again
    }

    ResourceManager::FreeResource_455550(field_24_dbuf);
#ifdef TETHYS_SATURN
    // SATURN (bt958): finish bt865 on the TWIN handle. field_24_dbuf is the same
    // kind of thing as field_20_ppBlock -- a slot in the 375-node pool, not a
    // block address -- so after the free it is still non-null, still 4-aligned,
    // still in RAM, and EnsureDecompressionBuffer below tests ONLY `!field_24_dbuf`.
    // The stale slot therefore gets reused, and Decompress_Type_4_5 writes
    // field_28_dbuf_size bytes THROUGH it into whichever live resource now owns
    // that node. That stomp is invisible to Tethys_HeapCheck (it validates the
    // header stride, not payloads) and leaves exactly the field-death signature:
    // block base sane, frame table garbage.
    // NOTE: field_28_dbuf_size is deliberately NOT cleared. Keeping it lets a
    // later EnsureDecompressionBuffer allocate a correctly-sized fresh buffer
    // (including a bt870-grown size); zeroing it would ask for 0 bytes.
    field_24_dbuf = nullptr;
#endif
}

bool Animation::EnsureDecompressionBuffer()
{
    if (!field_24_dbuf)
    {
        // SATURN (ao262.2): NON-FATAL. This function's return value is a
        // bool that every one of the five switch arms tests -- and it could
        // never be false, because the allocator fataled first. Now a starved
        // heap drops the decode instead of the run.
        field_24_dbuf = ResourceManager::Alloc_New_Resource_ImplEx(
                    ResourceManager::Resource_DecompressionBuffer, 0, field_28_dbuf_size,
                    false, ResourceManager::BlockAllocMethod::eFirstMatching,
                    true /*reclaim*/, false /*never fatal -- see below*/);
    }
    return field_24_dbuf != nullptr;
}

// SATURN (bt1128): two numbers, both free.
//   gAnWalk   -- the loop's EXIT INDEX is the animation count, so it costs one
//                add PER CALL and nothing per iteration. The body is unchanged.
//                (bt1055's rule: never instrument a hot loop per iteration or
//                the instrument is what you measure.)
//   gAnDecode -- vDecode() calls THIS FUNCTION made. One meaning: the decodes
//                at Set_Animation_Data_402A40 and Init_402D20 run inside
//                VUpdate and belong to `u`; conflating them makes it unreadable.
// The one caller is Game.cpp, itself guarded by sNumCamSwappers_507668 <= 0, so
// per-call == per-tick EXCEPT during a camera swap, when neither advances and
// the tick reads n000. That is a reading, not a fault -- note it and move on.
void CC AnimationBase::AnimateAll_4034F0(DynamicArrayT<AnimationBase>* pAnimList)
{
    s32 i = 0;
    for (; i < pAnimList->Size(); i++)
    {
        auto pAnim = pAnimList->ItemAt(i);
        if (!pAnim)
        {
            break;
        }

        if (pAnim->field_4_flags.Get(AnimFlags::eBit2_Animate))
        {
            if (pAnim->field_E_frame_change_counter > 0)
            {
                pAnim->field_E_frame_change_counter--;
                if (pAnim->field_E_frame_change_counter == 0)
                {
                    Tethys_gAnDecode++; // SATURN: bt1128 census
                    {
                        const unsigned int tDc0 = TETHYS_PT(); // SATURN bt1135
                        pAnim->vDecode();
                        Tethys_gDcRaw += TETHYS_PT() - tDc0; // ao262.18: unguarded
                    }
                }
            }
        }
    }
    Tethys_gAnWalk += static_cast<unsigned int>(i); // SATURN: bt1128, exit index
}

#ifdef TETHYS_SATURN
// SATURN: bt982 -- COMPILED frame-table offset -> ACTUAL offset.
//
// The offline converter compacts Anim chunks: the Saturn frames are half-size
// after the 2x downscale, so leaving each one at its PSX offset wasted 38.7%
// of every payload (952,656 B measured over R1.LVL) -- dead weight the
// ResourceManager pays for twice, in the resident block AND in the contiguous
// staging allocation. That padding is what wedged the no-cart configuration on
// the R1P15 mine screen (hardware "LWRAM wedge" fatal, bt980). Compaction
// repacks the payload and slides the frame-table region down as one block;
// every other cross-reference is rewritten offline, but the table offsets
// baked into kAnimRecords/kBgAnimRecords are compiled into THIS binary, so
// they are translated here, at the only two doors into an animation.
//
// The payload carries its own fixup record at +8: 'TSAT', compiled table base,
// actual table base (all u32 BE = native SH-2 loads). No record -> no
// translation, so uncompacted chunks are untouched.
//
// IDEMPOTENT BY CONSTRUCTION, and it has to be: Abe.cpp:4523 and Elum.cpp:2104
// feed field_18_frame_table_offset -- an ALREADY translated value -- straight
// back into Set_Animation_Data_402A40. The converter only compacts when the
// whole new payload ends at or below the compiled table base, so a translated
// offset can never satisfy the `>= oldBase` test and passes through unchanged.
// Reference implementation + the guard: tools/converter/anim.py
// (fixup_table_offset / _compact_payload).
// SATURN (ao262.11): payload bytes of an Animation chunk (ResourceManager.cpp,
// where the Header layout lives), and the count of frame-table offsets refused
// for being outside their block. `fw` on the MN row -- cumulative since boot,
// like every other latch, because one rejection anywhere is the whole finding.
extern "C" u32 Tethys_AnimBlockBytes(u8** ppRes);
extern "C" u32 Tethys_gAnimOffRej = 0;

static s32 Tethys_FixupFrameTable(const u8* pBlock, s32 off)
{
    // A misaligned or null block is an SH-2 address error waiting to happen;
    // bail to the untranslated offset and let the bt955 firewall handle it.
    if (!pBlock || (reinterpret_cast<u32>(pBlock) & 3u) || off < 0)
    {
        return off;
    }
    const u32* pFixup = reinterpret_cast<const u32*>(pBlock + 8);
    if (pFixup[0] != 0x54534154u) // 'TSAT'
    {
        return off;
    }
    const u32 oldBase = pFixup[1];
    const u32 newBase = pFixup[2];
    if (static_cast<u32>(off) >= oldBase && oldBase >= newBase)
    {
        return off - static_cast<s32>(oldBase - newBase);
    }
    return off;
}
#endif

s16 Animation::Set_Animation_Data_402A40(s32 frameTableOffset, u8** pAnimRes)
{
    FrameTableOffsetExists(frameTableOffset, false);
    if (pAnimRes)
    {
        field_20_ppBlock = pAnimRes;
    }

    if (!field_20_ppBlock)
    {
        return 0;
    }

#ifdef TETHYS_SATURN
    frameTableOffset = Tethys_FixupFrameTable(*field_20_ppBlock, frameTableOffset);

    // SATURN (ao262.11) A FRAME TABLE THAT IS NOT IN THIS BLOCK.
    //
    // Read the two branches above together: a NULL pAnimRes does not mean "no
    // animation", it means "keep the block you already have" -- 181 callers use
    // it that way to move within one .BAN. So when a caller passes a resource
    // handle that came back null because the chunk is GONE, this function keeps
    // the PREVIOUS block and applies the NEW block's frame-table offset to it.
    // That is not a missing sprite; it is a read tens of KB past the end of a
    // heap chunk, whose result is then walked as a frame table. Silent, and it
    // lands somewhere different every run.
    //
    // THE ENGINE SHIPS THAT HAZARD ALREADY, which is why this stays after the
    // ao262.12 withdrawal of the change that first exposed it.
    // Abe::Free_Resources_422870 exists because Midi.cpp:1115 frees res[0]
    // when a VAB will not fit, and nothing reloads it -- Abe.cpp:1768/3272/3864
    // only take CONTROL back, so Abe keeps playing with res[0] null and every
    // motion in the 15-63 band asks for the block that is gone. That path is
    // rare on a roomy heap and NOT rare on this one: it fires exactly when
    // memory is short, which is when a wild read is least likely to land
    // somewhere harmless.
    //
    // The bound is the block's own size, so it cannot reject a legitimate
    // in-block offset, and Tethys_AnimBlockBytes returns 0 -- pass through
    // unchanged -- for any handle that is not a live Animation chunk. Refusing
    // costs the object one animation change: it keeps playing what it had.
    {
        const u32 blockBytes = Tethys_AnimBlockBytes(field_20_ppBlock);
        if (blockBytes != 0
            && static_cast<u32>(frameTableOffset) + sizeof(AnimationHeader) > blockBytes)
        {
            Tethys_gAnimOffRej++;
            return 0;
        }
    }
#endif
    field_18_frame_table_offset = frameTableOffset;

    AnimationHeader* pAnimationHeader = reinterpret_cast<AnimationHeader*>(&(*field_20_ppBlock)[field_18_frame_table_offset]);
    field_10_frame_delay = pAnimationHeader->field_0_fps;

    field_4_flags.Clear(AnimFlags::eBit12_ForwardLoopCompleted);
    field_4_flags.Clear(AnimFlags::eBit18_IsLastFrame);
    field_4_flags.Clear(AnimFlags::eBit19_LoopBackwards);
    field_4_flags.Clear(AnimFlags::eBit8_Loop);

    if (pAnimationHeader->field_6_flags & AnimationHeader::eLoopFlag)
    {
        field_4_flags.Set(AnimFlags::eBit8_Loop);
    }

    field_E_frame_change_counter = 1;
    field_92_current_frame = -1;

    vDecode();

    // Reset to start frame
    field_E_frame_change_counter = 1;
    field_92_current_frame = -1;

    return 1;
}

void Animation::SetFrame_402AC0(s16 newFrame)
{
    if (field_20_ppBlock)
    {
        if (newFrame == -1)
        {
            newFrame = 0;
        }

        AnimationHeader* pHead = reinterpret_cast<AnimationHeader*>(*field_20_ppBlock + field_18_frame_table_offset); // TODO: Make getting offset to animation header cleaner

        if (newFrame > pHead->field_2_num_frames)
        {
            newFrame = pHead->field_2_num_frames;
        }

        field_E_frame_change_counter = 1;
        field_92_current_frame = newFrame - 1;
    }
}

s16 Animation::Init_402D20(s32 frameTableOffset, DynamicArray* /*animList*/, BaseGameObject* pGameObj, u16 maxW, u16 maxH, u8** ppAnimData, u8 bAllocateVRam, s32 b_StartingAlternationState, s8 bEnable_flag10_alternating)
{
    FrameTableOffsetExists(frameTableOffset, false, maxW, maxH);
    field_4_flags.Raw().all = 0; // TODO extra - init to 0's first - this may be wrong if any bits are explicitly set before this is called

    field_18_frame_table_offset = frameTableOffset;
    field_20_ppBlock = ppAnimData;
    field_1C_fn_ptr_array = nullptr;
    field_24_dbuf = nullptr;

    if (!ppAnimData)
    {
        LOG_WARNING("Animation init failed because the resource wasn't loaded!");
        return 0;
    }

#ifdef TETHYS_SATURN
    // SATURN: bt982 -- see Tethys_FixupFrameTable above. Applied after the
    // null check because it needs the block, and re-stored because field_18
    // was written from the compiled value a few lines up.
    frameTableOffset = Tethys_FixupFrameTable(*ppAnimData, frameTableOffset);
    field_18_frame_table_offset = frameTableOffset;
#endif

    field_94_pGameObj = pGameObj;

    AnimationHeader* pHeader = reinterpret_cast<AnimationHeader*>(&(*ppAnimData)[frameTableOffset]);

    field_10_frame_delay = pHeader->field_0_fps;
    field_E_frame_change_counter = 1;
    field_92_current_frame = -1;
    field_B_render_mode = TPageAbr::eBlend_0;
    field_8_r = 0;
    field_9_g = 0;
    field_A_b = 0;
    field_14_scale = FP_FromInteger(1);

    FrameInfoHeader* pFrameInfoHeader = Get_FrameHeader_403A00(0);
    u8* pAnimData = *ppAnimData;

    const FrameHeader* pFrameHeader = reinterpret_cast<const FrameHeader*>(&(*field_20_ppBlock)[pFrameInfoHeader->field_0_frame_header_offset]);

    u8* pClut = &pAnimData[pFrameHeader->field_0_clut_offset];

    field_4_flags.Clear(AnimFlags::eBit1);
    field_4_flags.Clear(AnimFlags::eBit5_FlipX);
    field_4_flags.Clear(AnimFlags::eBit6_FlipY);
    field_4_flags.Clear(AnimFlags::eBit7_SwapXY);
    field_4_flags.Set(AnimFlags::eBit2_Animate);
    field_4_flags.Set(AnimFlags::eBit3_Render);

    field_4_flags.Set(AnimFlags::eBit8_Loop, pHeader->field_6_flags & AnimationHeader::eLoopFlag);

    field_4_flags.Set(AnimFlags::eBit10_alternating_flag, bEnable_flag10_alternating & 1);
    field_4_flags.Set(AnimFlags::eBit11_bToggle_Bit10, b_StartingAlternationState & 1);

    field_4_flags.Clear(AnimFlags::eBit14_Is16Bit);
    field_4_flags.Clear(AnimFlags::eBit13_Is8Bit);
    field_4_flags.Clear(AnimFlags::eBit15_bSemiTrans);

    field_4_flags.Set(AnimFlags::eBit16_bBlending);
    field_4_flags.Set(AnimFlags::eBit17_bFreeResource);

    if (bAllocateVRam)
    {
        field_84_vram_rect.w = 0;
    }

    field_90_pal_depth = 0;

    s32 vram_width = 0;
    s16 pal_depth = 0;
    if (pFrameHeader->field_6_colour_depth == 4)
    {
        vram_width = (maxW % 2) + (maxW / 2);
        pal_depth = 16;
    }
    else if (pFrameHeader->field_6_colour_depth == 8)
    {
        vram_width = maxW;

        if (*(u16*) pClut == 64) // CLUT entry count/len
        {
            pal_depth = 64;
        }
        else
        {
            pal_depth = 256;
        }

        field_4_flags.Set(AnimFlags::eBit13_Is8Bit);
    }
    else if (pFrameHeader->field_6_colour_depth == 16)
    {
        vram_width = maxW * 2;
        field_4_flags.Set(AnimFlags::eBit14_Is16Bit);
    }
    else
    {
        return 0;
    }

    s32 bVramAllocOK = 1;
    if (bAllocateVRam)
    {
        bVramAllocOK = vram_alloc_450B20(maxW, maxH, pFrameHeader->field_6_colour_depth, &field_84_vram_rect);
#ifdef TETHYS_SATURN
        // SATURN (ao262.10): THE FAILURE MODE NO GAUGE COULD SEE.
        //
        // A failed Init_402D20 does not merely lose a frame -- it returns 0, and
        // BaseAnimatedWithPhysicsGameObject.cpp:94-109 then sets eListAddFailed
        // and eDead WITHOUT ever pushing the object onto gObjList_drawables. The
        // object is reaped a tick later. That is the tester's "a Mudokon is
        // missing": no crash, no message, a character simply absent, and not one
        // of the four Mudokon factories checks the constructor's result.
        //
        // Of the ways Init can fail, the resource-heap one now bumps `rn` and
        // names itself on the SN row. THESE TWO DID NOT COUNT ANYTHING:
        // Vram_alloc_4956C0 returns 0 with no counter, and PalAlloc only counts
        // successes. So a VRAM or CLUT exhaustion removed characters completely
        // invisibly -- and it produces a symptom IDENTICAL to the heap case, so
        // reading SN alone would have convicted the wrong subsystem.
        // `vf`/`pf` on the AF row are the discriminator: SN names the heap,
        // these two name the VRAM side, and exactly one of the three should move.
        if (!bVramAllocOK)
        {
            Tethys_gVramFail++;
        }
#endif
    }

    bool bPalAllocOK = true;
    if (pal_depth > 0 && bVramAllocOK)
    {
        IRenderer::PalRecord rec;
        rec.depth = pal_depth;
        bPalAllocOK = IRenderer::GetRenderer()->PalAlloc(rec);
#ifdef TETHYS_SATURN
        if (!bPalAllocOK)
        {
            Tethys_gPalFail++; // ao262.10: see the note on the vram leg above
        }
#endif

        field_8C_pal_vram_xy.field_0_x = rec.x;
        field_8C_pal_vram_xy.field_2_y = rec.y;
        // SATURN: on a FAILED PalAlloc the renderer leaves rec at {0,0}; storing
        // a non-zero depth here would make a later vCleanUp PalFree a (0,0) rect
        // it never owned -> Pal_free_483390 does sPal_table_5C9164[0 - 240] ^=
        // ... (an out-of-bounds write that corrupts the AE palette-allocation
        // model -> wrong CRAM coords -> the on-screen colour/debug corruption).
        // Only record a palette home when the alloc actually succeeded.
        field_90_pal_depth = bPalAllocOK ? rec.depth : 0;

        if (bVramAllocOK && bPalAllocOK)
        {
            IRenderer::GetRenderer()->PalSetData(rec, pClut + 4); // +4 Skip len, load pal
        }
    }

#ifdef TETHYS_SATURN
    // SATURN (bt1044): GIVE BACK WHAT THIS CALL TOOK BEFORE BAILING OUT.
    //
    // Three of Init's failure exits below return 0 AFTER vram_alloc (and, on
    // two of them, PalAlloc) already succeeded, and they release neither. On
    // PSX that is a slow bleed nobody notices; here the VRAM model is the
    // scarce resource whose OCCUPANCY drives allocator cost superlinearly, so
    // a leaked rect is not just lost space, it makes every later placement
    // slower -- the exact quantity bt1041/bt1044 exist to cut.
    //
    // The caller cannot clean up for us. Gibs is the proven case: on a failed
    // part it does `field_5C4_parts_used_count = i` (Gibs.cpp:141/:161), so its
    // dtor frees parts [0, i) and part i -- the one that got a rect and then
    // failed -- is never touched again. Init took it, Init returns it.
    //
    // Ownership is tracked with locals, never by inspecting the fields: an
    // Animation may be re-Init'd, so a non-zero field_84_vram_rect.w can be a
    // PREVIOUS owner's rect and freeing that would hand the model a live
    // allocation. Only what THIS call allocated is released. Both clears mirror
    // vCleanUp's idempotency guards, so a later vCleanUp is a no-op.
    const bool bOwnsVram = (bAllocateVRam != 0) && (bVramAllocOK != 0);
    const bool bOwnsPal = (pal_depth > 0) && (bVramAllocOK != 0) && bPalAllocOK;
    auto tethysReleaseSpoils = [&]() {
        if (bOwnsVram && field_84_vram_rect.w > 0)
        {
            Vram_free_450CE0({field_84_vram_rect.x, field_84_vram_rect.y}, {field_84_vram_rect.w, field_84_vram_rect.h});
            field_84_vram_rect.w = 0;
        }
        if (bOwnsPal && field_90_pal_depth > 0)
        {
            IRenderer::GetRenderer()->PalFree(IRenderer::PalRecord{field_8C_pal_vram_xy.field_0_x, field_8C_pal_vram_xy.field_2_y, field_90_pal_depth});
            field_90_pal_depth = 0;
        }
    };
#endif

    const bool bOk = bVramAllocOK && bPalAllocOK;
    if (!bOk)
    {
        LOG_WARNING("Animation init failed because the vram or pal alloc failed!");
#ifdef TETHYS_SATURN
        tethysReleaseSpoils(); // SATURN (bt1044): PalAlloc failed AFTER vram_alloc took a rect
#endif
        return 0;
    }

    field_28_dbuf_size = maxH * (vram_width + 3);

    if (pFrameHeader->field_7_compression_type != CompressionType::eType_0_NoCompression)
    {
        const u32 id = ResourceManager::Get_Header_455620(field_20_ppBlock)->field_C_id;
        // SATURN (ao262.2): NON-FATAL. The recovery below -- release the vram
        // rect and palette this init already took, and report the failure to
        // the caller -- is complete and was unreachable for the same reason.
        field_24_dbuf = ResourceManager::Alloc_New_Resource_ImplEx(
                    ResourceManager::Resource_DecompressionBuffer, id, field_28_dbuf_size,
                    false, ResourceManager::BlockAllocMethod::eFirstMatching,
                    true /*reclaim*/, false /*never fatal -- see below*/);
        if (!field_24_dbuf)
        {
            LOG_WARNING("Animation init failed because it couldn't alloc a new resource!");
#ifdef TETHYS_SATURN
            tethysReleaseSpoils(); // SATURN (bt1044): rect AND palette both taken by now
#endif
            return 0;
        }
    }

    // NOTE: OG bug or odd compiler code gen? Why isn't it using the passed in list which appears to always be this anyway ??
    const auto result = gObjList_animations_505564->Push_Back(this);
    if (!result)
    {
        LOG_ERROR("gObjList_animations_505564->Push_Back(this) returned 0 but shouldn't");
#ifdef TETHYS_SATURN
        tethysReleaseSpoils(); // SATURN (bt1044): the decompression buffer is reaped by vCleanUp; the rect was not
#endif
        return 0;
    }

    // Get first frame decompressed/into VRAM
    vDecode();

    field_E_frame_change_counter = 1;
    field_92_current_frame = -1;

    return result;
}

s16 Animation::Get_Frame_Count_403540()
{
    AnimationHeader* pHead = reinterpret_cast<AnimationHeader*>(*field_20_ppBlock + field_18_frame_table_offset); // TODO: Make getting offset to animation header cleaner
    return pHead->field_2_num_frames;
}

ALIVE_VAR(1, 0x4BA090, FrameInfoHeader, sBlankFrameInfoHeader_4BA090, {});

#ifdef TETHYS_SATURN
// SATURN (bt955): ONE out-of-line reject path shared by the three firewall
// checks below. Inlining the counter triplet at each site cost ~60 B of .text
// and the HWRAM pre-flight gate has well under 1 KB of slack, so keep it
// noinline on purpose.
__attribute__((noinline)) static FrameInfoHeader* Tethys_AnimReject(s32 why, u32 detail)
{
    Tethys_gAnimBadFrame++;
    Tethys_gAnimBadWhy = why;
    Tethys_gAnimBadPtr = detail;
    return &sBlankFrameInfoHeader_4BA090;
}
#endif

FrameInfoHeader* Animation::Get_FrameHeader_403A00(s32 frame)
{
#ifdef TETHYS_SATURN
    // SATURN FIREWALL (bt955) -- the update/culling-path twin of the bt863
    // firewall in VDecode_403550. NOTE the return value: on Saturn every
    // failure yields the ZEROED sBlankFrameInfoHeader_4BA090, never nullptr.
    // None of the 23 callers test for null, and on Saturn *null does not fault
    // (it reads the boot ROM at 0x0000000C), so a null return just moves the
    // crash somewhere less diagnosable. A blank header instead degrades to a
    // zero-size bounding rect at the object's own position: the object culls
    // out for one frame and the console lives.
    if (!field_20_ppBlock || !*field_20_ppBlock || !Tethys_BlockPtrSane(*field_20_ppBlock))
    {
        return Tethys_AnimReject(1, field_20_ppBlock ? reinterpret_cast<u32>(*field_20_ppBlock) : 0u);
    }
#else
    if (!field_20_ppBlock)
    {
        return nullptr;
    }
#endif

    if (frame < -1 || frame == -1)
    {
        frame = field_92_current_frame != -1 ? field_92_current_frame : 0;
    }

    AnimationHeader* pHead = reinterpret_cast<AnimationHeader*>(*field_20_ppBlock + field_18_frame_table_offset); // TODO: Make getting offset to animation header cleaner

#ifdef TETHYS_SATURN
    // mFrameOffsets holds exactly field_2_num_frames entries; an out-of-range
    // index reads arbitrary anim bytes and yields a wild -- frequently odd --
    // frameOffset, which produces the identical crash signature to a stale
    // block. Reject it here so the two causes stay distinguishable (why=2).
    // bt958: validate pHead BEFORE reading through it. Check (1) validated the
    // block BASE, not the sum: field_18_frame_table_offset is unbounded here, so
    // a wild offset puts pHead anywhere and the num_frames read below would raise
    // the very address error this guard exists to prevent. (Bounding the offset
    // against the resource's own size needs the block Header -- that lands with
    // the gauge build, together with the ownership test that is the only thing
    // able to see a RECYCLED slot.)
    if ((reinterpret_cast<u32>(pHead) & 3)
        || !Tethys_BlockPtrSane(reinterpret_cast<const u8*>(pHead)))
    {
        return Tethys_AnimReject(5, reinterpret_cast<u32>(pHead));
    }

    if (frame < 0 || pHead->field_2_num_frames <= 0 || frame >= pHead->field_2_num_frames)
    {
        return Tethys_AnimReject(2, static_cast<u32>(frame));
    }
#endif

    u32 frameOffset = pHead->mFrameOffsets[frame];

    FrameInfoHeader* pFrame = reinterpret_cast<FrameInfoHeader*>(*field_20_ppBlock + frameOffset);

    // Never seen this get hit, perhaps some sort of PSX specific check as addresses have to be aligned there?
    // TODO: Remove it in the future when proven to be not required?
#if defined(_MSC_VER) && !defined(_WIN64)
    if (reinterpret_cast<u32>(pFrame) & 3)
    {
        FrameInfoHeader* Unknown = &sBlankFrameInfoHeader_4BA090;
        return Unknown;
    }
#endif

#ifdef TETHYS_SATURN
    // The guard above is the ORIGINAL PSX alignment check, and it is fenced off
    // to 32-bit MSVC. On SH-2 it is not cosmetic: a misaligned mov.w/mov.l is a
    // CPU address error, which IS the field death (the faulting instruction was
    // `mov.w @(8,r6)` reading points[1] off this pointer). Revive it, and reject
    // a header that lands outside every real region while we are here.
    if ((reinterpret_cast<u32>(pFrame) & 3)
        || !Tethys_BlockPtrSane(reinterpret_cast<const u8*>(pFrame)))
    {
        return Tethys_AnimReject(3, reinterpret_cast<u32>(pFrame));
    }
#endif

    return pFrame;
}

void Animation::LoadPal_403090(u8** pAnimData, s32 palOffset)
{
    if (pAnimData)
    {
        const u8* pPalDataOffset = &(*pAnimData)[palOffset];
        if (field_90_pal_depth != 16 && field_90_pal_depth != 64 && field_90_pal_depth != 256)
        {
            LOG_ERROR("Bad pal depth " << field_90_pal_depth);
            ALIVE_FATAL("Bad pal depth");
        }
        IRenderer::GetRenderer()->PalSetData(IRenderer::PalRecord{field_8C_pal_vram_xy.field_0_x, field_8C_pal_vram_xy.field_2_y, field_90_pal_depth}, pPalDataOffset + 4); // +4 skip len, load pal
    }
}


static void CC Poly_FT4_Get_Rect(PSX_RECT* pRect, const Poly_FT4* pPoly)
{
    if (PSX_Prim_Code_Without_Blending_Or_SemiTransparency(pPoly->mBase.header.rgb_code.code_or_pad) == PrimTypeCodes::ePolyFT4)
    {
        pRect->x = pPoly->mBase.vert.x;
        pRect->y = pPoly->mBase.vert.y;
        pRect->w = pPoly->mVerts[2].mVert.x;
        pRect->h = pPoly->mVerts[2].mVert.y;
    }
    else
    {
        pRect->h = 0;
        pRect->w = 0;
        pRect->y = 0;
        pRect->x = 0;
    }
}

EXPORT void Animation::Get_Frame_Rect_402B50(PSX_RECT* pRect)
{
    Poly_FT4* pPoly = &field_2C_ot_data[gPsxDisplay_504C78.field_A_buffer_index];
    if (!field_4_flags.Get(AnimFlags::eBit20_use_xy_offset))
    {
        Poly_FT4_Get_Rect(pRect, pPoly);
        return;
    }

    const auto min_x0_x1 = std::min(X0(pPoly), X1(pPoly));
    const auto min_x2_x3 = std::min(X2(pPoly), X3(pPoly));
    pRect->x = std::min(min_x0_x1, min_x2_x3);

    const auto max_x0_x1 = std::max(X0(pPoly), X1(pPoly));
    const auto max_x2_x3 = std::max(X2(pPoly), X3(pPoly));
    pRect->w = std::max(max_x0_x1, max_x2_x3);

    const auto min_y0_y1 = std::min(Y0(pPoly), Y1(pPoly));
    const auto min_y2_y3 = std::min(Y2(pPoly), Y3(pPoly));
    pRect->y = std::min(min_y0_y1, min_y2_y3);

    const auto max_y0_y1 = std::max(Y0(pPoly), Y1(pPoly));
    const auto max_y2_y3 = std::max(Y2(pPoly), Y3(pPoly));
    pRect->h = std::max(max_y0_y1, max_y2_y3);
}

EXPORT void Animation::Get_Frame_Width_Height_403E80(s16* pWidth, s16* pHeight)
{
    FrameInfoHeader* pFrameHeader = Get_FrameHeader_403A00(-1);
    if (field_4_flags.Get(AnimFlags::eBit22_DeadMode))
    {
        ALIVE_FATAL("Mode should never be used");
    }
    else
    {
        auto pHeader = reinterpret_cast<const FrameHeader*>(&(*field_20_ppBlock)[pFrameHeader->field_0_frame_header_offset]);
        *pWidth = pHeader->field_4_width;
        *pHeight = pHeader->field_5_height;
    }
}

EXPORT void Animation::Get_Frame_Offset_403EE0(s16* pBoundingX, s16* pBoundingY)
{
    FrameInfoHeader* pFrameHeader = Get_FrameHeader_403A00(-1);
    *pBoundingX = pFrameHeader->field_8_data.offsetAndRect.mOffset.x;
    *pBoundingY = pFrameHeader->field_8_data.offsetAndRect.mOffset.y;
}

void AnimationUnknown::vCleanUp()
{
    VCleanUp2_404280();
}

void AnimationUnknown::VRender2(s32 xpos, s32 ypos, PrimHeader** ppOt)
{
    VRender2_403FD0(xpos, ypos, ppOt);
}

void AnimationUnknown::vRender(s32 /*xpos*/, s32 /*ypos*/, PrimHeader** /*pOt*/, s16 /*width*/, s16 /*height*/)
{
    // Empty @ 402A20
}

void AnimationUnknown::vDecode()
{
    // Empty @ 402A10
}

void AnimationUnknown::VCleanUp2_404280()
{
    field_68_anim_ptr = nullptr;
}

void AnimationUnknown::VRender2_403FD0(s32 xpos, s32 ypos, PrimHeader** ppOt)
{
    Poly_FT4* pPoly = &field_10_polys[gPsxDisplay_504C78.field_A_buffer_index];
    if (field_4_flags.Get(AnimFlags::eBit3_Render))
    {
        // Copy from animation to local
        *pPoly = field_68_anim_ptr->field_2C_ot_data[gPsxDisplay_504C78.field_A_buffer_index];

        FrameInfoHeader* pFrameInfoHeader = field_68_anim_ptr->Get_FrameHeader_403A00(-1);

        FrameHeader* pFrameHeader = reinterpret_cast<FrameHeader*>(&(*field_68_anim_ptr->field_20_ppBlock)[pFrameInfoHeader->field_0_frame_header_offset]);

        s32 frameOffX = pFrameInfoHeader->field_8_data.offsetAndRect.mOffset.x;
        s32 frameOffY = pFrameInfoHeader->field_8_data.offsetAndRect.mOffset.y;
        s32 frameH = pFrameHeader->field_5_height;
        s32 frameW = pFrameHeader->field_4_width;

        if (field_6C_scale != FP_FromInteger(1))
        {
            frameH = FP_GetExponent(FP_FromInteger(frameH) * field_6C_scale);
            frameW = FP_GetExponent(FP_FromInteger(frameW) * field_6C_scale);
            frameOffX = FP_GetExponent(FP_FromInteger(frameOffX) * field_6C_scale);
            frameOffY = FP_GetExponent(FP_FromInteger(frameOffY) * field_6C_scale);
        }

        s32 polyX = 0;
        s32 polyY = 0;
        if (field_68_anim_ptr->field_4_flags.Get(AnimFlags::eBit7_SwapXY))
        {
            if (field_68_anim_ptr->field_4_flags.Get(AnimFlags::eBit6_FlipY))
            {
                if (field_68_anim_ptr->field_4_flags.Get(AnimFlags::eBit5_FlipX))
                {
                    polyX = xpos - frameOffY - frameH;
                }
                else
                {
                    polyX = frameOffY + xpos;
                }
                polyY = frameOffX + ypos;
            }
            else
            {
                if (field_68_anim_ptr->field_4_flags.Get(AnimFlags::eBit5_FlipX))
                {
                    polyX = xpos - frameOffY - frameH;
                }
                else
                {
                    polyX = frameOffY + xpos;
                }
                polyY = ypos - frameOffX - frameW;
            }
        }
        else if (field_68_anim_ptr->field_4_flags.Get(AnimFlags::eBit6_FlipY))
        {
            if (field_68_anim_ptr->field_4_flags.Get(AnimFlags::eBit5_FlipX))
            {
                polyX = xpos - frameOffX - frameW;
            }
            else
            {
                polyX = frameOffX + xpos;
            }
            polyY = ypos - frameOffY - frameH;
        }
        else
        {
            if (field_68_anim_ptr->field_4_flags.Get(AnimFlags::eBit5_FlipX))
            {
                polyX = xpos - frameOffX - frameW;
            }
            else
            {
                polyX = frameOffX + xpos;
            }
            polyY = frameOffY + ypos;
        }

        if (!field_4_flags.Get(AnimFlags::eBit16_bBlending))
        {
            SetRGB0(pPoly, field_8_r, field_9_g, field_A_b);
        }

        const s32 w = frameW + polyX - 1;
        const s32 h = frameH + polyY - 1;
        SetXY0(pPoly, static_cast<s16>(polyX), static_cast<s16>(polyY));
        SetXY1(pPoly, static_cast<s16>(w), static_cast<s16>(polyY));
        SetXY2(pPoly, static_cast<s16>(polyX), static_cast<s16>(h));
        SetXY3(pPoly, static_cast<s16>(w), static_cast<s16>(h));

        SetPrimExtraPointerHack(pPoly, nullptr);
        OrderingTable_Add_498A80(OtLayer(ppOt, field_C_layer), &pPoly->mBase.header);
    }
}

void AnimationUnknown::GetRenderedSize_404220(PSX_RECT* pRect)
{
    Poly_FT4_Get_Rect(pRect, &field_10_polys[gPsxDisplay_504C78.field_A_buffer_index]);
}

} // namespace AO
