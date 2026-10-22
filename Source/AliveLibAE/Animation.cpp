#include "stdafx.h"
#include "Animation.hpp"
#include "Function.hpp"
#include "Map.hpp"
#include "Compression.hpp"
#include "VRam.hpp"
#include "Game.hpp"
#include "PsxDisplay.hpp"
#include "Abe.hpp"    // For anim call back table
#include "Slig.hpp"   // For slig frame call back
#include "Fleech.hpp" // For fleech frame call back
#include "stdlib.hpp"
#include "Particle.hpp"
#include "Events.hpp"
#include "Slurg.hpp"
#include "Slog.hpp"
#include "Sfx.hpp"
#include "Blood.hpp"
#include "ObjectIds.hpp"
#include "Sys_common.hpp"
#include "Renderer/IRenderer.hpp"
#include <gmock/gmock.h>

#ifdef TETHYS_SATURN
// ============================================================================
// SATURN (Exoddus port, D-A1/D-A2): THE ANIMATION RUNTIME ON THE SATURN PACK.
//
// What the AE converter ships (tools/ae/convert_ae.py, gauge check_pack_ae.py):
// every frame is compression type 0 or 5, pre-scaled 2:1 and VDP1-packed
// (src/pack_contract.hpp); the CLUT count is a big-endian u32 (Init reads it
// unchanged); 191 of 199 chunks are compacted and carry the 'TSAT' record at
// payload+8; frame callbacks are {u32 index, s16, s16} x count, big-endian.
// What this file does with it, each piece the twin of AO's Animation.cpp:
//   * TSAT: compiled frame-table offsets are translated at the two doors
//     (Init_40A030, Set_Animation_Data_409C80) and exported for the three
//     object sites that address a table by its compiled offset (RockSack,
//     MineCar, Slurg): Tethys_TranslateFrameTable;
//   * every frame lives in VRAM: eBit25_bDecompressDone is forced at Init (the
//     PC draws 16- and 64-colour cels from compressed data and frees their VRAM
//     at the first render: 3,981 of AE's 4,907 frames);
//   * type 5 decodes into the shared LWRAM scratch through the cel cache and
//     the slave prefetch (AliveLibCommon/TethysCelCache.hpp); there is no
//     per-Animation decode buffer at all (the largest AE frame decodes to
//     13,000 B, the scratch holds 16,384), so no allocation happens inside a
//     decode and no block can move under it;
//   * firewalls: an insane block skips the decode, Get_FrameHeader never
//     returns null or a misaligned header, a garbage compression type skips;
//   * idempotent release: the palette and the VRAM rect are freed once, an
//     Init that fails gives back what it took.
// ============================================================================
extern "C" unsigned int Tethys_gDbufFall;   // 'fb': type-5 frames the scratch could not take (skipped)
extern "C" unsigned int Tethys_gDbufRaw[3]; // decode raw ticks, [0] in AnimateAll, [1] elsewhere
extern "C" unsigned int Tethys_gDbufBytes[3];
extern "C" unsigned int Tethys_gInAnimate;
extern "C" unsigned int Tethys_RawTicks(void);
extern "C" unsigned char Tethys_gProbeOn;
#define TETHYS_PT() (Tethys_gProbeOn ? Tethys_RawTicks() : 0u)
// ao262.10: the two silent object-killers (Init returns 0, the object dies).
extern "C" unsigned int Tethys_gVramFail;
extern "C" unsigned int Tethys_gPalFail;
// SATURN (AE_ENGINE_DECISION.md 11.21): palette changes asked of an animation
// that has no palette (its Init was refused), skipped (Load_Pal_40A530).
extern "C" {
unsigned int Tethys_gPalSkip = 0;
}
// AE_ENGINE_DECISION.md 11.22, textures for what is drawn (src/renderer_saturn.cxx).
extern "C" int Tethys_TexSlotLive(s16 x, s16 y);
extern "C" void Tethys_TexSlotEvictable(s16 x, s16 y);
extern "C" u32 Tethys_gUploadDrawn;
extern "C" u32 Tethys_gTexRoom;
// The renderer's room count at the last refused draw-time upload, and the
// refusals, cumulative (read in a dump).
static u32 sTethysUpFailRoom = 0;
extern "C" {
unsigned int Tethys_gUploadDrawnFail = 0;
}

// 11.22: an animation drawn through vRender and not drawn since its last
// prefetch is off screen: its frames wait for a draw, so its next cel is not
// posted to the slave, and the scratch keeps the cels the screen needs.
static inline bool Tethys_CelSkipAE(AnimationBase* p)
{
    const bool bSeen = p->field_4_flags.Get(AnimFlags::eTethysSeen);
    p->field_4_flags.Clear(AnimFlags::eTethysSeen);
    return p->field_4_flags.Get(AnimFlags::eTethysDrawn) && !bSeen;
}

// AE has no clustered blood cels (the AE converter's CLUSTER_CELS is empty), so
// nothing is refused by the cache or the prefetch. The light laid on Abe
// (src/glow_ae.cxx) needs no refusal either, unlike AO's chant glow: it never
// writes a cache entry (a cached cel is copied before it is lit) and only
// reads them, through Tethys_CelPeekAE below, when no slave batch is in flight.
#define TETHYS_CEL_ANIM_LIST gObjList_animations_5C1A24
#define TETHYS_CEL_REFUSE_BLOCK(pBlock) false
#define TETHYS_CEL_PREFETCH_OFF() 0
#define TETHYS_CEL_SKIP_ANIM(pBase) Tethys_CelSkipAE(pBase)
#include "TethysCelCache.hpp"
#include "TethysObjectHooks.hpp" // the exported translation hooks, checked against their callers

// SATURN (src/glow_ae.cxx): the cache's decoded copy of a particle's cel, read
// without settling anything. Its callers run in AnimateAll and right after it,
// before Tethys_CelPrefetch posts a batch; a batch in flight answers nullptr
// (the caller decodes into its own buffer instead).
extern "C" const u8* Tethys_CelPeekAE(const u8* pSrc, u32 len)
{
    const long i = Tethys_gSlzActive ? -1 : TethysCelLookup(pSrc, len);
    return i < 0 ? nullptr : Tethys_gDbufScratch + sTethysCel[i].off;
}

// bt863/bt955: firewall gauges (declared by src/sys_saturn.cxx).
extern "C" volatile s32 Tethys_gAnimBadBlock = 0; // garbage blocks and compression types skipped
extern "C" volatile s32 Tethys_gAnimBadFrame = 0; // Get_FrameHeader rejects
extern "C" volatile s32 Tethys_gAnimBadWhy = 0;   // 1 block, 2 frame index, 3 header, 5 table
extern "C" volatile u32 Tethys_gAnimBadPtr = 0;   // the last rejected value
// ao262.11: frame-table offsets refused for lying outside their own block.
extern "C" u32 Tethys_gAnimOffRej = 0;
// Defined below (ALIVE_VAR): the zeroed header the firewall hands out.
extern FrameInfoHeader& sBlankFrameInfoHeader_5440AC;

// bt982: COMPILED frame-table offset -> ACTUAL offset, through the block's own
// 'TSAT' record at +8 (u32 BE magic, compiled table base, actual table base).
// No record -> no translation (the 8 uncompacted AE chunks). Idempotent: the
// converter compacts only when the new payload ends at or below the compiled
// base (asserted per chunk by tools/ae/check_pack_ae.py), so a translated
// offset can never satisfy `>= oldBase` and passes through unchanged.
static s32 Tethys_FixupFrameTable(const u8* pBlock, s32 off)
{
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

// 388.ao.1: the same translation for code that recognises or addresses an
// animation by its COMPILED offset (RockSack.cpp, MineCar.cpp, Slurg.cpp).
// One copy of the arithmetic, the block's own record.
extern "C" s32 Tethys_TranslateFrameTable(u8** ppBlock, s32 compiledOff)
{
    if (!ppBlock || !*ppBlock)
    {
        return compiledOff;
    }
    return Tethys_FixupFrameTable(*ppBlock, compiledOff);
}

// The inverse, for quicksaves that store a runtime offset (Slurg.cpp): a value
// at or above the actual base was moved down by (oldBase - newBase); anything
// below it was never moved. Exact because the translation is a single shift of
// the table region and the converter keeps every table at or above newBase.
extern "C" s32 Tethys_UntranslateFrameTable(u8** ppBlock, s32 runtimeOff)
{
    if (!ppBlock || !*ppBlock || (reinterpret_cast<u32>(*ppBlock) & 3u) || runtimeOff < 0)
    {
        return runtimeOff;
    }
    const u32* pFixup = reinterpret_cast<const u32*>(*ppBlock + 8);
    if (pFixup[0] != 0x54534154u || pFixup[1] < pFixup[2])
    {
        return runtimeOff;
    }
    if (static_cast<u32>(runtimeOff) >= pFixup[2])
    {
        return runtimeOff + static_cast<s32>(pFixup[1] - pFixup[2]);
    }
    return runtimeOff;
}
#endif // TETHYS_SATURN

// Frame call backs ??
EXPORT s32 CC Animation_OnFrame_Common_Null_455F40(void*, s16*)
{
    return 1;
}

EXPORT s32 CC Animation_OnFrame_Null_455F60(void*, s16*)
{
    return 1;
}

EXPORT s32 CC Animation_OnFrame_Common_4561B0(void* pObjPtr, s16* pData)
{
    auto pObj = static_cast<BaseAliveGameObject*>(pObjPtr);
    const AnimRecord& dustRec = AnimRec(AnimId::Dust_Particle);
    u8** ppAnimData = ResourceManager::GetLoadedResource_49C2A0(ResourceManager::Resource_Animation, dustRec.mResourceId, FALSE, FALSE);

    FP xOff = {};
    if (pObj->field_20_animation.field_4_flags.Get(AnimFlags::eBit5_FlipX))
    {
        xOff = -(pObj->field_CC_sprite_scale * FP_FromInteger(pData[0]));
    }
    else
    {
        xOff = (pObj->field_CC_sprite_scale * FP_FromInteger(pData[0]));
    }

    FP xpos = xOff + pObj->field_B8_xpos;
    FP ypos = (pObj->field_CC_sprite_scale * FP_FromInteger(pData[1])) + pObj->field_BC_ypos + FP_FromInteger(25);
    if (!pObj->field_100_pCollisionLine)
    {
        return 1;
    }

    const s8 count = sSlurg_Step_Watch_Points_Count_5BD4DC[sSlurg_Step_Watch_Points_Idx_5C1C08];
    if (count < 5)
    {
        Slurg_Step_Watch_Points* pPoints = &sSlurg_Step_Watch_Points_5C1B28[sSlurg_Step_Watch_Points_Idx_5C1C08];
        pPoints->field_0_points[count].field_0_xPos = FP_GetExponent(xpos);
        pPoints->field_0_points[count].field_2_yPos = pObj->field_100_pCollisionLine->field_0_rect.y - 5;
        sSlurg_Step_Watch_Points_Count_5BD4DC[sSlurg_Step_Watch_Points_Idx_5C1C08] = count + 1;
    }

    if (pObj != sActiveHero_5C1B68)
    {
        return 1;
    }

    if (pObj->field_CC_sprite_scale == FP_FromDouble(0.5))
    {
        ypos -= FP_FromInteger(14);
    }

    if (pObj->field_106_current_motion == eAbeMotions::Motion_71_Knockback_455090 && pObj->field_CC_sprite_scale == FP_FromDouble(0.5))
    {
        ypos += FP_FromInteger(5);
    }

    if (pObj->field_106_current_motion == eAbeMotions::Motion_1_WalkLoop_44FBA0)
    {
        ypos -= FP_FromInteger(5);
    }

#ifdef TETHYS_SATURN
    // SATURN: the sneak's dust is made at scale 0 (below), which the PC draws as
    // nothing; here the one-pixel quad would be decimated to a half-size puff
    // under the floor line. Not made at all.
    if (pObj->field_106_current_motion == eAbeMotions::Motion_40_SneakLoop_450550)
    {
        return 1;
    }
#endif

    auto pPartical = ae_new<Particle>();
    if (pPartical)
    {
        pPartical->ctor_4CC4C0(xpos, ypos, dustRec.mFrameTableOffset, dustRec.mMaxW, dustRec.mMaxH, ppAnimData);
        pPartical->field_20_animation.field_B_render_mode = TPageAbr::eBlend_1;

        if (pObj->field_D6_scale == 1)
        {
            pPartical->field_20_animation.field_C_render_layer = Layer::eLayer_Foreground_36;
        }
        else
        {
            pPartical->field_20_animation.field_C_render_layer = Layer::eLayer_Foreground_Half_17;
        }
#ifdef TETHYS_SATURN
        // SATURN: one layer under Abe. The PC adds the dust over him; here it is
        // a VDP2 addition with the background (DUST.BAN promoted to 8 bpp,
        // tools/ae/ae_rules.py), and a dust texel drawn after Abe would REPLACE
        // his pixel (one VDP1 word per pixel), so the cloud of a run-stop would
        // hollow him out. Drawn just before him, his pixels win and the dust
        // adds to the background around him (Romain, 2026-10-19: the cloud must
        // never make Abe disappear).
        pPartical->field_20_animation.field_C_render_layer =
            static_cast<Layer>(static_cast<s16>(pObj->field_20_animation.field_C_render_layer) - 1);
#endif

        pPartical->field_D0_r = 45;
        pPartical->field_D2_g = 35;
        pPartical->field_D4_b = 5;

        switch (pObj->field_106_current_motion)
        {
            case eAbeMotions::Motion_1_WalkLoop_44FBA0:
                pPartical->field_CC_sprite_scale = FP_FromDouble(0.3) * pObj->field_CC_sprite_scale;
                break;

            case eAbeMotions::Motion_40_SneakLoop_450550:
                pPartical->field_CC_sprite_scale = FP_FromInteger(0);
                break;

            case eAbeMotions::Motion_71_Knockback_455090:
                pPartical->field_CC_sprite_scale = FP_FromInteger(1) * pObj->field_CC_sprite_scale;
                break;

            default:
                pPartical->field_CC_sprite_scale = FP_FromDouble(0.6) * pObj->field_CC_sprite_scale;
                break;
        }
#ifdef TETHYS_SATURN
        // SATURN (Exoddus; Romain, 2026-10-20: "Elle devrait etre devant comme sur
        // pc, et s'ajouter a Abe"): the puff stays one layer under Abe (above), and
        // the share of its light that falls on him is laid into his cel by
        // src/glow_ae.cxx, at the PC's own tint: this reports the puff.
        Tethys_GlowDustAE(ppAnimData);
#endif
    }
    return 1;
}

EXPORT s32 CC Animation_OnFrame_Common_434130(void* pObjPtr, s16* pData)
{
    auto pObj = static_cast<BaseAliveGameObject*>(pObjPtr);

    if (pObj->Type() == AETypes::eAbe_69 || pObj->Type() == AETypes::eMudokon_110 || pObj->Type() == AETypes::eMudokon2_81)
    {
        LOG_ERROR("never expected pObj type id to be abe or mudokon in Animation_OnFrame_Common_434130");
        ALIVE_FATAL("got wrong type id");
    }

    if (pObj->field_10C_health <= FP_FromInteger(0))
    {
        return 1;
    }

    // flying slig: kVaporResID
    u8** ppAnimRes = pObj->field_10_resources_array.ItemAt(7);
    FP xOff = {};
    if (pObj->field_20_animation.field_4_flags.Get(AnimFlags::eBit5_FlipX))
    {
        xOff = -(pObj->field_CC_sprite_scale * FP_FromInteger(pData[0]));
    }
    else
    {
        xOff = (pObj->field_CC_sprite_scale * FP_FromInteger(pData[0]));
    }

    FP xpos = xOff + pObj->field_B8_xpos;
    FP ypos = (pObj->field_CC_sprite_scale * (FP_FromInteger(pData[1]) + FP_FromInteger(25))) + pObj->field_BC_ypos;

    if (Event_Get_422C00(kEventDeathReset))
    {
        pObj->field_6_flags.Set(BaseGameObject::eDead_Bit3);
    }

    auto pParticle = ae_new<Particle>();
    if (pParticle)
    {
        const AnimRecord& vaporizeRec = AnimRec(AnimId::Vaporize_Particle);
        pParticle->ctor_4CC4C0(xpos, ypos, vaporizeRec.mFrameTableOffset, vaporizeRec.mMaxW, vaporizeRec.mMaxH, ppAnimRes);
        pParticle->field_20_animation.field_B_render_mode = TPageAbr::eBlend_1;
        pParticle->field_20_animation.field_C_render_layer = Layer::eLayer_Foreground_36;
        pParticle->field_D0_r = 64;
        pParticle->field_D2_g = 64;
        pParticle->field_D4_b = 64;
        pParticle->field_CC_sprite_scale = FP_FromDouble(0.6) * pObj->field_CC_sprite_scale;
    }
    return 1;
}

s32 CC Animation_OnFrame_Slog_4C3030(void* pObjPtr, s16* pPoints)
{
    auto pSlog = static_cast<Slog*>(pObjPtr);
    auto pTarget = static_cast<BaseAliveGameObject*>(sObjectIds_5C1B70.Find_449CF0(pSlog->field_118_target_id));
    if (!pTarget)
    {
        return 1;
    }

    if ((pTarget->Type() == AETypes::eAbe_69 && pTarget->field_106_current_motion == eAbeMotions::Motion_68_ToOffScreenHoist_454B80) || pSlog->field_114_flags.Get(Flags_114::e114_Bit7_Electrocuted))
    {
        return 1;
    }

    PSX_RECT bTargetRect = {};
    pTarget->vGetBoundingRect_424FD0(&bTargetRect, 1);

    PSX_RECT bSlogRect = {};
    pSlog->vGetBoundingRect_424FD0(&bSlogRect, 1);

    if (bSlogRect.x > bTargetRect.w || bSlogRect.w < bTargetRect.x || bSlogRect.h < bTargetRect.y || bSlogRect.y > bTargetRect.h || pTarget->field_CC_sprite_scale != FP_FromInteger(1) || pSlog->field_11C_biting_target)
    {
        return 1;
    }

    if (!pTarget->VTakeDamage_408730(pSlog))
    {
        return 1;
    }

    FP bloodX = {};
    if (pSlog->field_20_animation.field_4_flags.Get(AnimFlags::eBit5_FlipX))
    {
        bloodX = pSlog->field_B8_xpos - (pSlog->field_CC_sprite_scale * FP_FromInteger(pPoints[0]));
    }
    else
    {
        bloodX = (pSlog->field_CC_sprite_scale * FP_FromInteger(pPoints[0])) + pSlog->field_B8_xpos;
    }

    const FP bloodY = (pSlog->field_CC_sprite_scale * FP_FromInteger(pPoints[1])) + pSlog->field_BC_ypos;

    auto pBlood = ae_new<Blood>();
    if (pBlood)
    {
        pBlood->ctor_40F0B0(
            bloodX,
            bloodY - FP_FromInteger(8),
            pSlog->field_C4_velx * FP_FromInteger(2),
            FP_FromInteger(0),
            pSlog->field_CC_sprite_scale,
            50);
    }

    pSlog->field_11C_biting_target = 1;
    SFX_Play_46FA90(SoundEffect::SlogBite_34, 0);

    return 1;
}

// TODO: Array is possibly bigger, called by AnimationEx::Invoke_CallBacks_40B7A0
TFrameCallBackType kAbe_Anim_Frame_Fns_55EF98[5] = {
    Animation_OnFrame_Common_Null_455F40,
    Animation_OnFrame_Null_455F60,
    Animation_OnFrame_Abe_455F80,
    Animation_OnFrame_Common_4561B0,
    Animation_OnFrame_Common_434130};

TFrameCallBackType kSlig_Anim_Frame_Fns_55EFAC[4] = {
    Animation_OnFrame_Common_Null_455F40,
    Animation_OnFrame_Null_455F60,
    Animation_OnFrame_Slig_4C0600,
    Animation_OnFrame_Common_4561B0};

TFrameCallBackType kSlog_Anim_Frame_Fns_55EFBC[2] = {
    Animation_OnFrame_Slog_4C3030,
    Animation_OnFrame_Null_455F60};

TFrameCallBackType kFlyingSlig_Anim_Frames_Fns_55EFC4[3] = {
    Animation_OnFrame_Common_Null_455F40,
    Animation_OnFrame_Null_455F60,
    Animation_OnFrame_Common_434130};

TFrameCallBackType kFleech_Anim_Frame_Fns_55EFD0[3] = {
    Animation_OnFrame_Common_Null_455F40,
    Animation_OnFrame_Null_455F60,
    Animation_OnFrame_Fleech_449A60};

// ================================================================
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
// SATURN: the pack ships compression types 0 and 5 only. Type 0 uploads straight
// from the block; type 5 decodes into the shared scratch through the cel cache.
// Nothing here allocates, so the frame header cannot move under the decode
// (AO ao242.17's hazard has no window to open). Anything else is a garbage
// block or a pack that broke its contract: skip the upload and count it, the
// previous cel stays on screen.
//   A frame WIDER than the VRAM rect Init allocated (DRIPIK.BND 804 ships one,
// 162 px in a 145 px slot; the PC draws such cels from compressed data and
// never meets the limit) reaches here with vram_rect clamped by DecompressFrame.
// Its rows are SatTexW(frame width) texels apart while Upload reads
// SatTexW(slot width): when the two differ the cel is compacted to the slot's
// stride (cropped on the right, as the clamp means) instead of sheared.
void Animation::UploadTexture(const FrameHeader* pFrameHeader, const PSX_RECT& vram_rect, s16 width_bpp_adjusted)
{
    IRenderer& renderer = *IRenderer::GetRenderer();
    const CompressionType ct = pFrameHeader->field_7_compression_type;
    const u8* pPixels = reinterpret_cast<const u8*>(&pFrameHeader->field_8_width2);
    u32 fromRow = 0; // bytes per decoded row, then per uploaded row (pack contract)
    u32 toRow = 0;
    if (width_bpp_adjusted > vram_rect.w)
    {
        const s32 shift = field_4_flags.Get(AnimFlags::eBit13_Is8Bit) ? 1 : 2; // halfwords -> PSX pixels
        const s32 bppShift = field_4_flags.Get(AnimFlags::eBit13_Is8Bit) ? 0 : 1; // texels -> bytes
        fromRow = static_cast<u32>(((((width_bpp_adjusted << shift) + 1) / 2 + 7) & ~7) >> bppShift);
        toRow = static_cast<u32>(((((vram_rect.w << shift) + 1) / 2 + 7) & ~7) >> bppShift);
    }
    const bool bWider = fromRow != toRow;
    if (ct == CompressionType::eType_4_RLE || ct == CompressionType::eType_5_RLE)
    {
        const u32 destLen = *reinterpret_cast<const u32*>(pPixels);
        if (Tethys_gDbufScratch == nullptr || destLen > Tethys_gDbufScratchBytes)
        {
            Tethys_gDbufFall++; // 'fb': no AE frame is this big (13,000 B max), measured
            return;
        }
        u8* pDst = Tethys_gDbufScratch;
        long celOff = bWider ? -1 : TethysCelFind(pPixels, destLen);
        if (celOff >= 0)
        {
            Tethys_gDbufMemoHits++;
        }
        else
        {
            // A re-strided cel is private to its slot width: never cached.
            celOff = bWider ? -1 : TethysCelAlloc(pPixels, destLen);
            if (celOff < 0)
            {
                TethysCelEvictLow(destLen); // decodes at offset 0, drops what it overwrites
                celOff = 0;
            }
            const u32 t0 = TETHYS_PT();
            CompressionType_4Or5_Decompress_4ABAB0(pPixels, pDst + celOff);
            const u32 kPar = Tethys_gInAnimate ? 0u : 1u;
            Tethys_gDbufRaw[kPar] += TETHYS_PT() - t0;
            Tethys_gDbufBytes[kPar] += destLen;
        }
        pPixels = pDst + celOff;
    }
    else if (ct != CompressionType::eType_0_NoCompression)
    {
        Tethys_gAnimBadBlock++;
        return;
    }
    // SATURN (Exoddus, AO 387.ao.1's route 3): the additive light that falls on
    // Abe (the chant's orbs, his dust), laid into the cel before the upload, the
    // one point where it exists as writable indices (src/glow_ae.cxx). Everywhere
    // else this costs one load and a branch. The composite answers what to
    // upload: this cel untouched, or its lit copy (a cache entry is never
    // written, so nothing is forgotten).
    if (Tethys_gGlowOn && !bWider && field_4_flags.Get(AnimFlags::eBit13_Is8Bit))
    {
        pPixels = Tethys_GlowCompositeAE(this, pPixels, pFrameHeader->field_4_width, pFrameHeader->field_5_height);
    }
    if (bWider)
    {
        // Compact the rows into the scratch at offset 0 (in place for a decoded
        // cel: every row moves down, never over a row still to be read).
        if (Tethys_gDbufScratch == nullptr || fromRow * static_cast<u32>(vram_rect.h) > Tethys_gDbufScratchBytes
            || field_4_flags.Get(AnimFlags::eBit14_Is16Bit))
        {
            Tethys_gDbufFall++;
            return;
        }
        if (pPixels != Tethys_gDbufScratch)
        {
            TethysCelEvictLow(fromRow * static_cast<u32>(vram_rect.h));
        }
        u8* const pOut = Tethys_gDbufScratch;
        for (s32 row = 0; row < vram_rect.h; row++)
        {
            memmove(pOut + static_cast<u32>(row) * toRow, pPixels + static_cast<u32>(row) * fromRow, toRow);
        }
        pPixels = pOut;
    }
    renderer.Upload(AnimFlagsToBitDepth(field_4_flags), vram_rect, pPixels);
}
#else
void Animation::UploadTexture(const FrameHeader* pFrameHeader, const PSX_RECT& vram_rect, s16 width_bpp_adjusted)
{
    IRenderer& renderer = *IRenderer::GetRenderer();
    switch (pFrameHeader->field_7_compression_type)
    {
        case CompressionType::eType_0_NoCompression:
            // No compression, load the data directly into frame buffer
            field_4_flags.Set(AnimFlags::eBit25_bDecompressDone);
            renderer.Upload(AnimFlagsToBitDepth(field_4_flags), vram_rect, reinterpret_cast<const u8*>(&pFrameHeader->field_8_width2)); // TODO: Refactor structure to get pixel data
            break;

        case CompressionType::eType_1_NotUsed:
            // This isn't in any of the animation data files on disk, therefore can't ever be used.
            ALIVE_FATAL("Compression type 1 never expected to be used.");
            break;

        case CompressionType::eType_2_ThreeToFourBytes:
            field_4_flags.Set(AnimFlags::eBit25_bDecompressDone);
            if (EnsureDecompressionBuffer())
            {
                // TODO: Refactor structure to get pixel data.
                CompressionType2_Decompress_40AA50(
                    reinterpret_cast<const u8*>(&pFrameHeader[1]),
                    *field_24_dbuf,
                    width_bpp_adjusted * pFrameHeader->field_5_height * 2);

                renderer.Upload(AnimFlagsToBitDepth(field_4_flags), vram_rect, *field_24_dbuf);
            }
            break;

        case CompressionType::eType_3_RLE_Blocks:
            if (field_4_flags.Get(AnimFlags::eBit25_bDecompressDone))
            {
                if (EnsureDecompressionBuffer())
                {
                    // TODO: Refactor structure to get pixel data.
                    CompressionType_3Ae_Decompress_40A6A0(reinterpret_cast<const u8*>(&pFrameHeader->field_8_width2), *field_24_dbuf);

                    renderer.Upload(AnimFlagsToBitDepth(field_4_flags), vram_rect, *field_24_dbuf);
                }
            }
            break;

        case CompressionType::eType_4_RLE:
        case CompressionType::eType_5_RLE:
            if (EnsureDecompressionBuffer())
            {
                // TODO: Refactor structure to get pixel data.
                CompressionType_4Or5_Decompress_4ABAB0(reinterpret_cast<const u8*>(&pFrameHeader->field_8_width2), *field_24_dbuf);
                renderer.Upload(AnimFlagsToBitDepth(field_4_flags), vram_rect, *field_24_dbuf);
            }
            break;

        case CompressionType::eType_6_RLE:
            if (field_4_flags.Get(AnimFlags::eBit25_bDecompressDone))
            {
                if (EnsureDecompressionBuffer())
                {
                    // TODO: Refactor structure to get pixel data.
                    CompressionType6Ae_Decompress_40A8A0(reinterpret_cast<const u8*>(&pFrameHeader->field_8_width2), *field_24_dbuf);
                    renderer.Upload(AnimFlagsToBitDepth(field_4_flags), vram_rect, *field_24_dbuf);
                }
            }
            break;

        case CompressionType::eType_7_NotUsed:
        case CompressionType::eType_8_NotUsed:
            ALIVE_FATAL("Decompression 7 and 8 never expected to be used");
            break;
    }
}
#endif // TETHYS_SATURN

bool Animation::EnsureDecompressionBuffer()
{
    if (!field_24_dbuf)
    {
        field_24_dbuf = ResourceManager::Alloc_New_Resource_49BED0(ResourceManager::Resource_DecompressionBuffer, 0, field_28_dbuf_size);
    }
    return field_24_dbuf != nullptr;
}

void Animation::DecompressFrame()
{
    if (field_4_flags.Get(AnimFlags::eBit11_bToggle_Bit10))
    {
        field_4_flags.Toggle(AnimFlags::eBit10_alternating_flag);
    }

    const FrameInfoHeader* pFrameInfo = Get_FrameHeader_40B730(-1); // -1 = use current frame
#ifdef TETHYS_SATURN
    if (pFrameInfo == &sBlankFrameInfoHeader_5440AC)
    {
        return; // bt955: the firewall refused this frame, there is nothing to decode
    }
#endif
    if (pFrameInfo->field_6_count > 0)
    {
        Invoke_CallBacks_40B7A0();
    }

#ifdef TETHYS_SATURN
    // SATURN (AE_ENGINE_DECISION.md 11.22): TEXTURES FOR WHAT IS DRAWN. An
    // animation already drawn through vRender uploads its frame when it is
    // drawn (vRender_40B820), not here: the frame's events above ran, so the
    // game does not change, only the upload waits. Off screen it then costs
    // neither a decode nor texture memory, and the renderer may take its slot
    // back (src/renderer_saturn.cxx AeEvictStale). An animation never drawn by
    // vRender uploads here, as before: Blood, Water and ZapLine draw from the
    // rect themselves, an AnimationUnknown copies its parent's polygon.
    if (field_4_flags.Get(AnimFlags::eTethysDrawn))
    {
        field_4_flags.Set(AnimFlags::eTethysPending);
        field_4_flags.Clear(AnimFlags::eTethysUpFail);
        return;
    }
    Tethys_UploadFrame(pFrameInfo);
}

// DecompressFrame's upload half: the current frame into the slot keyed by the
// rect, without the frame's events.
void Animation::Tethys_UploadFrame(const FrameInfoHeader* pFrameInfo)
{
#endif
    const FrameHeader* pFrameHeader = reinterpret_cast<const FrameHeader*>(&(*field_20_ppBlock)[pFrameInfo->field_0_frame_header_offset]);

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
        pFrameHeader->field_5_height};

    // Clamp width
    if (width_bpp_adjusted > field_84_vram_rect.w)
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

inline s16 FP_AdjustedToInteger(FP fp, FP adjustment)
{
    return FP_GetExponent(fp + adjustment);
}

void Animation::vRender_40B820(s32 xpos, s32 ypos, PrimHeader** ppOt, s16 width, s32 height)
{
    if ((field_84_vram_rect.x || field_84_vram_rect.y) && !(field_4_flags.Get(AnimFlags::eBit25_bDecompressDone)))
    {
        Vram_free_495A60({field_84_vram_rect.x, field_84_vram_rect.y}, {field_84_vram_rect.w, field_84_vram_rect.h});
        field_84_vram_rect.x = 0;
        field_84_vram_rect.y = 0;
    }

    const s16 xpos_unknown = static_cast<s16>(PsxToPCX(xpos));
    const s16 width_unknown = static_cast<s16>(PsxToPCX(width));

    if (field_4_flags.Get(AnimFlags::eBit22_DeadMode))
    {
        ALIVE_FATAL("Unexpected flag 0x200000 / eBit22_DeadMode"); // Animation_vRender2_40BEE0()
    }

    if (!field_4_flags.Get(AnimFlags::eBit3_Render))
    {
        return;
    }

    if (!field_20_ppBlock)
    {
        return;
    }

#ifdef TETHYS_SATURN
    // SATURN (AE_ENGINE_DECISION.md 11.22): the frame goes up here, when it is
    // drawn (DecompressFrame). The first draw marks the animation and its slot
    // (the one Init filled becomes evictable); then a frame change waiting, or a
    // slot the renderer took back while the animation was off screen, uploads
    // the current frame before the polygon is built. A refused upload (no room
    // even after taking back a stale slot) leaves the animation undrawn, as any
    // refused upload does, and is tried again at its next frame change or once
    // the renderer reports room that may have appeared (Tethys_gTexRoom), not
    // at every draw: each try is a decode.
    if (field_84_vram_rect.w > 0)
    {
        if (!field_4_flags.Get(AnimFlags::eTethysDrawn))
        {
            field_4_flags.Set(AnimFlags::eTethysDrawn);
            Tethys_TexSlotEvictable(field_84_vram_rect.x, field_84_vram_rect.y);
        }
        field_4_flags.Set(AnimFlags::eTethysSeen);
        if (field_4_flags.Get(AnimFlags::eTethysPending)
            || ((!field_4_flags.Get(AnimFlags::eTethysUpFail) || Tethys_gTexRoom != sTethysUpFailRoom)
                && !Tethys_TexSlotLive(field_84_vram_rect.x, field_84_vram_rect.y)))
        {
            field_4_flags.Clear(AnimFlags::eTethysPending);
            const FrameInfoHeader* pInfo = Get_FrameHeader_40B730(-1);
            if (pInfo != &sBlankFrameInfoHeader_5440AC)
            {
                Tethys_gUploadDrawn = 1;
                Tethys_UploadFrame(pInfo);
                Tethys_gUploadDrawn = 0;
                const bool bFail = !Tethys_TexSlotLive(field_84_vram_rect.x, field_84_vram_rect.y);
                field_4_flags.Set(AnimFlags::eTethysUpFail, bFail);
                if (bFail)
                {
                    sTethysUpFailRoom = Tethys_gTexRoom;
                    Tethys_gUploadDrawnFail++;
                }
            }
        }
    }
#endif

    FrameInfoHeader* pFrameInfoHeader = Get_FrameHeader_40B730(-1);
    FrameHeader* pFrameHeader = (FrameHeader*) &(*field_20_ppBlock)[pFrameInfoHeader->field_0_frame_header_offset];

    FP frame_width_fixed;
    FP frame_height_fixed;
    if (width_unknown)
    {
        frame_width_fixed = FP_FromInteger(width_unknown);
        frame_height_fixed = FP_FromInteger(height);
    }
    else
    {
        frame_width_fixed = FP_FromInteger(pFrameHeader->field_4_width);
        frame_height_fixed = FP_FromInteger(pFrameHeader->field_5_height);
    }

    FP xOffSet_fixed;
    FP yOffset_fixed;
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

    Poly_FT4* pPoly = &field_2C_ot_data[gPsxDisplay_5C1130.field_C_buffer_index];
    PolyFT4_Init(pPoly);
    Poly_Set_SemiTrans_4F8A60(&pPoly->mBase.header, field_4_flags.Get(AnimFlags::eBit15_bSemiTrans));
    Poly_Set_Blending_4F8A20(&pPoly->mBase.header, field_4_flags.Get(AnimFlags::eBit16_bBlending));
    SetRGB0(pPoly, field_8_r, field_9_g, field_A_b);
    SetTPage(pPoly, static_cast<u16>(PSX_getTPage_4F60E0(textureMode, field_B_render_mode, field_84_vram_rect.x, field_84_vram_rect.y)));
    SetClut(pPoly, static_cast<u16>(PSX_getClut_4F6350(field_8C_pal_vram_xy.field_0_x, field_8C_pal_vram_xy.field_2_y)));

    u8 u1 = field_84_vram_rect.x & 63;
    if (textureMode == TPageMode::e8Bit_1)
    {
        // 8 bit
        u1 *= 2;
    }
    else if (textureMode == TPageMode::e4Bit_0)
    {
        // 4 bit
        u1 *= 4;
    }
    else
    {
        // 16 bit
    }

    const u8 v0 = static_cast<u8>(field_84_vram_rect.y);
    const u8 u0 = pFrameHeader->field_4_width + u1 - 1;
    const u8 v1 = pFrameHeader->field_5_height + v0 - 1;

    if (field_14_scale != FP_FromDouble(1.0))
    {
        // Apply scale to x/y pos
        frame_height_fixed *= field_14_scale;
        frame_width_fixed *= field_14_scale;

        if (field_14_scale == FP_FromDouble(0.5))
        {
            // Add 1 if half scale
            frame_height_fixed += FP_FromDouble(1.0);
            frame_width_fixed += FP_FromDouble(1.0);
        }

        // Apply scale to x/y offset
        xOffSet_fixed *= field_14_scale;
        yOffset_fixed = (yOffset_fixed * field_14_scale) - FP_FromDouble(1.0);
    }

    s16 polyXPos = 0;
    s16 polyYPos = 0;
    if (field_4_flags.Get(AnimFlags::eBit6_FlipY))
    {
        if (field_4_flags.Get(AnimFlags::eBit5_FlipX))
        {
            SetUV0(pPoly, u0, v1);
            SetUV1(pPoly, u1, v1);
            SetUV2(pPoly, u0, v0);
            SetUV3(pPoly, u1, v0);
            polyXPos = xpos_unknown - FP_AdjustedToInteger(xOffSet_fixed, FP_FromDouble(0.499)) - FP_AdjustedToInteger(frame_width_fixed, FP_FromDouble(0.499));
        }
        else
        {
            SetUV0(pPoly, u1, v1);
            SetUV1(pPoly, u0, v1);
            SetUV2(pPoly, u1, v0);
            SetUV3(pPoly, u0, v0);
            polyXPos = xpos_unknown + FP_AdjustedToInteger(xOffSet_fixed, FP_FromDouble(0.499));
        }
        // TODO: Might be wrong because this was doing something with the sign bit abs() ??
        polyYPos = static_cast<s16>(ypos) - FP_AdjustedToInteger(yOffset_fixed, FP_FromDouble(0.499)) - FP_AdjustedToInteger(frame_height_fixed, FP_FromDouble(0.499));
    }
    else
    {
        FP yPosFixed;
        if (field_4_flags.Get(AnimFlags::eBit5_FlipX))
        {
            SetUV0(pPoly, u0, v0);
            SetUV1(pPoly, u1, v0);
            SetUV2(pPoly, u0, v1);
            SetUV3(pPoly, u1, v1);

            polyXPos = xpos_unknown - FP_AdjustedToInteger(xOffSet_fixed, FP_FromDouble(0.499)) - FP_AdjustedToInteger(frame_width_fixed, FP_FromDouble(0.499));
            polyYPos = static_cast<s16>(ypos) + FP_AdjustedToInteger(yOffset_fixed, FP_FromDouble(0.499));
        }
        else
        {
            SetUV0(pPoly, u1, v0);
            SetUV1(pPoly, u0, v0);
            SetUV2(pPoly, u1, v1);
            SetUV3(pPoly, u0, v1);

            polyXPos = xpos_unknown + FP_AdjustedToInteger(xOffSet_fixed, FP_FromDouble(0.499));

            yPosFixed = yOffset_fixed + FP_FromDouble(0.499);
            polyYPos = static_cast<s16>(ypos) + FP_AdjustedToInteger(yOffset_fixed, FP_FromDouble(0.499));
        }
    }

    SetXY0(pPoly, polyXPos, polyYPos);
    SetXY1(pPoly, polyXPos + FP_GetExponent(frame_width_fixed - FP_FromDouble(0.501)), polyYPos);
    SetXY2(pPoly, polyXPos, polyYPos + FP_GetExponent(frame_height_fixed - FP_FromDouble(0.501)));
    SetXY3(pPoly, polyXPos + FP_GetExponent(frame_width_fixed - FP_FromDouble(0.501)), polyYPos + FP_GetExponent(frame_height_fixed - FP_FromDouble(0.501)));

    if (pFrameHeader->field_7_compression_type == CompressionType::eType_3_RLE_Blocks || pFrameHeader->field_7_compression_type == CompressionType::eType_6_RLE)
    {
        SetPrimExtraPointerHack(pPoly, &pFrameHeader->field_8_width2);
    }
    else
    {
        SetPrimExtraPointerHack(pPoly, nullptr);
    }

    OrderingTable_Add_4F8AA0(OtLayer(ppOt, field_C_render_layer), &pPoly->mBase.header);
}

void Animation::vCleanUp_40C630()
{
    gObjList_animations_5C1A24->Remove_Item(this);
    Animation_Pal_Free_40C4C0();
    ResourceManager::FreeResource_49C330(field_24_dbuf);
#ifdef TETHYS_SATURN
    field_24_dbuf = nullptr; // bt958: a freed handle is never reused (the dtor may vCleanUp again)
#endif
}

void Animation::vDecode2_40B200()
{
    ALIVE_FATAL("Impossible - this kind of anim data don't exist");
}

void Animation::vDecode_40AC90()
{
    if (field_4_flags.Get(AnimFlags::eBit22_DeadMode))
    {
        // Never hit ?
        vDecode2_40B200();
        return;
    }

    if (DecodeCommon())
    {
        DecompressFrame();
    }
}

bool Animation::DecodeCommon()
{
    if (!field_20_ppBlock || !*field_20_ppBlock)
    {
        return false;
    }
#ifdef TETHYS_SATURN
    // bt863: a garbage block pointer would reach the callback dispatch (a wild
    // jump) and the upload: skip the decode and count it.
    if (!Tethys_BlockPtrSane(*field_20_ppBlock))
    {
        Tethys_gAnimBadBlock++;
        return false;
    }
    TethysCelLatchVptr(this); // 445.ao.1: the prefetch walk's type test
#endif

    AnimationHeader* pAnimHeader = reinterpret_cast<AnimationHeader*>(&(*field_20_ppBlock)[field_18_frame_table_offset]);
    if (pAnimHeader->field_2_num_frames == 1 && field_4_flags.Get(AnimFlags::eBit12_ForwardLoopCompleted))
    {
        return false;
    }

    bool isLastFrame = false;
    if (field_4_flags.Get(AnimFlags::eBit19_LoopBackwards))
    {
        // Loop backwards
        const s16 prevFrameNum = --field_92_current_frame;
        field_E_frame_change_counter = field_10_frame_delay;

        if (prevFrameNum < pAnimHeader->field_4_loop_start_frame)
        {
            if (field_4_flags.Get(AnimFlags::eBit8_Loop))
            {
                // Loop to last frame
                field_92_current_frame = pAnimHeader->field_2_num_frames - 1;
            }
            else
            {
                // Stay on current frame
                field_92_current_frame = prevFrameNum + 1;
                field_E_frame_change_counter = 0;
            }

            // For some reason eBit12_ForwardLoopCompleted isn't set when going backwards
        }

        // Is first (last since running backwards) frame?
        if (field_92_current_frame == 0)
        {
            isLastFrame = true;
        }
    }
    else
    {
        // Loop forwards
        const s16 nextFrameNum = ++field_92_current_frame;
        field_E_frame_change_counter = field_10_frame_delay;

        // Animation reached end point
        if (nextFrameNum >= pAnimHeader->field_2_num_frames)
        {
            if (field_4_flags.Get(AnimFlags::eBit8_Loop))
            {
                // Loop back to loop start frame
                field_92_current_frame = pAnimHeader->field_4_loop_start_frame;
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
        if (field_92_current_frame == pAnimHeader->field_2_num_frames - 1)
        {
            isLastFrame = true;
        }
    }

    if (isLastFrame)
    {
        field_4_flags.Set(AnimFlags::eBit18_IsLastFrame);
    }
    else
    {
        field_4_flags.Clear(AnimFlags::eBit18_IsLastFrame);
    }

    return true;
}

void Animation::Invoke_CallBacks_40B7A0()
{
    if (!field_20_ppBlock || !field_1C_fn_ptr_array)
    {
        return;
    }

    FrameInfoHeader* pFrameHeaderCopy = Get_FrameHeader_40B730(-1);
    // This data can be an array of u32's + other data up to field_6_count
    // which appears AFTER the usual data.
    u32* pCallBackData = reinterpret_cast<u32*>(&pFrameHeaderCopy->field_8_data.points[3]);
    for (s32 i = 0; i < pFrameHeaderCopy->field_6_count; i++)
    {
        auto pFnCallBack = field_1C_fn_ptr_array[*pCallBackData];
        if (!pFnCallBack)
        {
            break;
        }
        pCallBackData++; // Skip the array index
        // Pass the data pointer into the call back which will then read and skip any extra data
        pCallBackData += pFnCallBack(field_94_pGameObj, (s16*) pCallBackData);
    }
}

s16 Animation::Set_Animation_Data_409C80(s32 frameTableOffset, u8** pAnimRes)
{
    FrameTableOffsetExists(frameTableOffset, true);
    if (pAnimRes)
    {
        // Animation block must match what was previously set
        if (field_4_flags.Get(AnimFlags::eBit22_DeadMode) && field_20_ppBlock != pAnimRes)
        {
            return 0;
        }
        field_20_ppBlock = pAnimRes;
    }

    if (!field_20_ppBlock)
    {
        return 0;
    }

#ifdef TETHYS_SATURN
    // bt982: the compiled offset becomes the block's actual one (idempotent, so
    // callers that feed field_18 back in are unchanged).
    frameTableOffset = Tethys_FixupFrameTable(*field_20_ppBlock, frameTableOffset);
    // ao262.11: a null pAnimRes keeps the PREVIOUS block, so a caller whose
    // resource is gone applies a new table offset to the old block: refuse an
    // offset that lies outside the block (0 = not a live Animation block, pass).
    {
        const u32 blockBytes = Tethys_AnimBlockBytes(field_20_ppBlock);
        if (blockBytes != 0 && static_cast<u32>(frameTableOffset) + sizeof(AnimationHeader) > blockBytes)
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

    vDecode_40AC90();

    // Reset to start frame
    field_E_frame_change_counter = 1;
    field_92_current_frame = -1;

    return 1;
}

void Animation::Animation_Pal_Free_40C4C0()
{
    if (field_4_flags.Get(AnimFlags::eBit22_DeadMode))
    {
        ALIVE_FATAL("Impossible case - data of this type dosen't exist");
    }
    else
    {
        if (field_84_vram_rect.w > 0)
        {
            if (field_84_vram_rect.x || field_84_vram_rect.y || field_4_flags.Get(AnimFlags::eBit9))
            {
                Vram_free_495A60({field_84_vram_rect.x, field_84_vram_rect.y}, {field_84_vram_rect.w, field_84_vram_rect.h});
#ifdef TETHYS_SATURN
                field_84_vram_rect.w = 0; // idempotent: a failed Init and the dtor both come here
#endif
            }
        }

        if (field_90_pal_depth > 0 && field_4_flags.Get(AnimFlags::eBit17_bOwnPal))
        {
            IRenderer::GetRenderer()->PalFree(IRenderer::PalRecord{field_8C_pal_vram_xy.field_0_x, field_8C_pal_vram_xy.field_2_y, field_90_pal_depth});
#ifdef TETHYS_SATURN
            field_90_pal_depth = 0; // idempotent: a second PalFree would release a home another anim owns
#endif
        }
    }
}

void Animation::SetFrame_409D50(s16 newFrame)
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

ALIVE_VAR(1, 0x5440AC, FrameInfoHeader, sBlankFrameInfoHeader_5440AC, {});

#ifdef TETHYS_SATURN
// bt955: ONE out-of-line reject path for the firewall checks below.
__attribute__((noinline)) static FrameInfoHeader* Tethys_AnimReject(s32 why, u32 detail)
{
    Tethys_gAnimBadFrame++;
    Tethys_gAnimBadWhy = why;
    Tethys_gAnimBadPtr = detail;
    return &sBlankFrameInfoHeader_5440AC;
}
#endif

FrameInfoHeader* Animation::Get_FrameHeader_40B730(s16 frame)
{
#ifdef TETHYS_SATURN
    // bt955: the update/culling-path twin of the decode firewall. On Saturn every
    // failure yields the ZEROED blank header, never nullptr: most callers do not
    // test for null, and a null read here returns boot-ROM bytes instead of a
    // fault. A blank header degrades to a zero-size rect at the object's own
    // position for one frame.
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
    // A wild table offset or frame index reads arbitrary bytes as offsets: SH-2
    // raises an address error on a misaligned load, so check before reading.
    if ((reinterpret_cast<u32>(pHead) & 3) || !Tethys_BlockPtrSane(reinterpret_cast<const u8*>(pHead)))
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
#ifdef TETHYS_SATURN
    if ((reinterpret_cast<u32>(pFrame) & 3) || !Tethys_BlockPtrSane(reinterpret_cast<const u8*>(pFrame)))
    {
        return Tethys_AnimReject(3, reinterpret_cast<u32>(pFrame));
    }
#endif

    // Never seen this get hit, perhaps some sort of PSX specific check as addresses have to be aligned there?
    // TODO: Remove it in the future when proven to be not required?
#if defined(_MSC_VER) && !defined(_WIN64)
    if (reinterpret_cast<u32>(pFrame) & 3)
    {
        FrameInfoHeader* Unknown = &sBlankFrameInfoHeader_5440AC;
        return Unknown;
    }
#endif

    return pFrame;
}

void Animation::Get_Frame_Rect_409E10(PSX_RECT* pRect)
{
    Poly_FT4* pPoly = &field_2C_ot_data[gPsxDisplay_5C1130.field_C_buffer_index];
    if (!field_4_flags.Get(AnimFlags::eBit20_use_xy_offset))
    {
        Poly_FT4_Get_Rect_409DA0(pRect, pPoly);
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

u16 Animation::Get_Frame_Count_40AC70()
{
    AnimationHeader* pHead = reinterpret_cast<AnimationHeader*>(*field_20_ppBlock + field_18_frame_table_offset); // TODO: Make getting offset to animation header cleaner
    return pHead->field_2_num_frames;
}

s16 Animation::Init_40A030(s32 frameTableOffset, DynamicArray* /*animList*/, BaseGameObject* pGameObj, u16 maxW, u16 maxH, u8** ppAnimData, u8 bOwnsPalData, s32 b_StartingAlternationState, s8 bEnable_flag10_alternating)
{
    FrameTableOffsetExists(frameTableOffset, true, maxW, maxH);
    field_4_flags.Raw().all = 0; // TODO extra - init to 0's first - this may be wrong if any bits are explicitly set before this is called
    field_4_flags.Set(AnimFlags::eBit21);

    field_18_frame_table_offset = frameTableOffset;
    field_20_ppBlock = ppAnimData;
    field_1C_fn_ptr_array = nullptr;
    field_24_dbuf = nullptr;

    if (!ppAnimData)
    {
        return 0;
    }

#ifdef TETHYS_SATURN
    // bt982: translate after the null check (it needs the block) and store again,
    // field_18 was written from the compiled value above. A block that is gone or
    // insane is refused here rather than walked.
    if (!*ppAnimData || !Tethys_BlockPtrSane(*ppAnimData))
    {
        Tethys_gAnimBadBlock++;
        return 0;
    }
    frameTableOffset = Tethys_FixupFrameTable(*ppAnimData, frameTableOffset);
    field_18_frame_table_offset = frameTableOffset;
#endif

    field_94_pGameObj = pGameObj;
    AnimationHeader* pHeader = reinterpret_cast<AnimationHeader*>(&(*ppAnimData)[frameTableOffset]);

    field_4_flags.Clear(AnimFlags::eBit1);
    field_4_flags.Clear(AnimFlags::eBit5_FlipX);
    field_4_flags.Clear(AnimFlags::eBit6_FlipY);
    field_4_flags.Clear(AnimFlags::eBit7_SwapXY);
    field_4_flags.Set(AnimFlags::eBit2_Animate);
    field_4_flags.Set(AnimFlags::eBit3_Render);

    field_4_flags.Set(AnimFlags::eBit8_Loop, pHeader->field_6_flags & AnimationHeader::eLoopFlag);

    field_4_flags.Set(AnimFlags::eBit10_alternating_flag, bEnable_flag10_alternating);

    field_4_flags.Set(AnimFlags::eBit11_bToggle_Bit10, b_StartingAlternationState);

    field_4_flags.Clear(AnimFlags::eBit14_Is16Bit);
    field_4_flags.Clear(AnimFlags::eBit13_Is8Bit);

    // Clear vram/pal inits to not allocated
    field_84_vram_rect.w = 0;
    field_90_pal_depth = 0;

    if (bOwnsPalData)
    {
        field_4_flags.Set(AnimFlags::eBit17_bOwnPal);
    }

    field_4_flags.Clear(AnimFlags::eBit24);
    field_4_flags.Clear(AnimFlags::eBit23);
    field_4_flags.Clear(AnimFlags::eBit22_DeadMode);

    // TODO: Refactor
#ifdef TETHYS_SATURN
    // The Saturn pack's 'TSAT' compaction record lives exactly here (D-A1):
    // only a non-zero word that is not the record is the dead-mode data.
    if (*((u32*) *ppAnimData + 2) != 0 && *((u32*) *ppAnimData + 2) != 0x54534154u)
#else
    if (*((u32*) *ppAnimData + 2) != 0)
#endif
    {
        // Never in any source data ?
        field_4_flags.Set(AnimFlags::eBit22_DeadMode);
        ALIVE_FATAL("This can't happen");
    }

    // NOTE: All branches related to bit 22 removed
    if (field_4_flags.Get(AnimFlags::eBit22_DeadMode))
    {
        ALIVE_FATAL("Unknown data");
    }

    field_4_flags.Clear(AnimFlags::eBit16_bBlending);
    field_4_flags.Clear(AnimFlags::eBit15_bSemiTrans);
    field_4_flags.Set(AnimFlags::eBit16_bBlending);

    field_10_frame_delay = pHeader->field_0_fps;
    field_E_frame_change_counter = 1;
    field_92_current_frame = -1;
    field_B_render_mode = TPageAbr::eBlend_0;
    field_A_b = 0;
    field_9_g = 0;
    field_8_r = 0;
    field_14_scale = FP_FromInteger(1);

    FrameInfoHeader* pFrameInfoHeader = Get_FrameHeader_40B730(0);
    u8* pAnimData = *field_20_ppBlock;

    const FrameHeader* pFrameHeader_1 = reinterpret_cast<const FrameHeader*>(&(*field_20_ppBlock)[pFrameInfoHeader->field_0_frame_header_offset]);

    u8* pClut = &pAnimData[pFrameHeader_1->field_0_clut_offset];

#ifdef TETHYS_SATURN
    if (pFrameInfoHeader == &sBlankFrameInfoHeader_5440AC)
    {
        return 0; // bt955: frame 0 refused by the firewall, nothing to size a rect on
    }
#endif
    if (!Vram_alloc_4956C0(maxW, maxH, pFrameHeader_1->field_6_colour_depth, &field_84_vram_rect))
    {
#ifdef TETHYS_SATURN
        Tethys_gVramFail++; // ao262.10: the object dies silently; `vf` says why
#endif
        return 0;
    }

    s16 pal_depth = 0;
    s8 b256Pal = 0;
    s32 vram_width = 0;
    if (pFrameHeader_1->field_6_colour_depth == 4)
    {
        const s32 halfW = maxW / 2;
        const s32 wMod2 = maxW % 2;

        vram_width = wMod2 + halfW;
        pal_depth = 16;
        b256Pal = 0; // is 16 pal
    }
    else if (pFrameHeader_1->field_6_colour_depth == 8)
    {
        vram_width = maxW;
        field_4_flags.Set(AnimFlags::eBit13_Is8Bit);
        if (*(u32*) pClut != 64) // CLUT entry count
        {
            pal_depth = 256;
            b256Pal = 1; // is 256 pal
        }
        else
        {
            pal_depth = 64;
        }
    }
    else
    {
        if (pFrameHeader_1->field_6_colour_depth == 16)
        {
            vram_width = maxW * 2;
            field_4_flags.Set(AnimFlags::eBit14_Is16Bit);
        }
        else
        {
            vram_width = pal_depth;
        }

        if (pal_depth != 16 && pal_depth != 64)
        {
            b256Pal = 1; // not 16 or 64 so must be 256 ??
        }
    }

    field_4_flags.Set(AnimFlags::eBit25_bDecompressDone, b256Pal);
#ifdef TETHYS_SATURN
    // D-A1: every Saturn frame is uploaded to VRAM (types 0 and 5 only); without
    // the bit vRender_40B820 frees the rect at the first render, as the PC does
    // for the 16- and 64-colour cels it draws from compressed data.
    field_4_flags.Set(AnimFlags::eBit25_bDecompressDone);
#endif

    if (field_4_flags.Get(AnimFlags::eBit17_bOwnPal) && !field_4_flags.Get(AnimFlags::eBit24))
    {
        IRenderer::PalRecord palRec{0, 0, pal_depth};
        if (!IRenderer::GetRenderer()->PalAlloc(palRec))
        {
#ifdef TETHYS_SATURN
            Tethys_gPalFail++; // ao262.10: `pf`
#endif
            Animation_Pal_Free_40C4C0();
            return 0;
        }

        field_8C_pal_vram_xy.field_0_x = palRec.x;
        field_8C_pal_vram_xy.field_2_y = palRec.y;
        field_90_pal_depth = pal_depth;

        IRenderer::GetRenderer()->PalSetData(palRec, pClut + 4); // Skips CLUT len
    }

    field_28_dbuf_size = maxH * (vram_width + 3);
    field_28_dbuf_size += 8; // Add 8 for some reason
    field_24_dbuf = nullptr;

    // NOTE: OG bug or odd compiler code gen? Why isn't it using the passed in list which appears to always be this anyway ??
    if (!gObjList_animations_5C1A24->Push_Back(this))
    {
#ifdef TETHYS_SATURN
        Animation_Pal_Free_40C4C0(); // bt1044: give back the rect and the palette this call took
#endif
        return 0;
    }

    // Get first frame decompressed/into VRAM
    vDecode_40AC90();

    field_E_frame_change_counter = 1;
    field_92_current_frame = -1;

    return 1;
}

void Animation::Load_Pal_40A530(u8** pAnimData, s32 palOffset)
{
    if (pAnimData)
    {
        // +4 = skip CLUT len
        const u8* pPal = &(*pAnimData)[palOffset];
#ifdef TETHYS_SATURN
        // SATURN (11.21): an Init refused for want of a VRAM rect (`vf`) leaves the
        // animation without a palette, and its owner lives on, invisible, as
        // Oddysee's do. A palette change on it (a Mudokon's mood, Mudokon.cpp
        // 1290-1321) is then a no-op instead of RELIVE's fatal: the PC original
        // has no such check. The heap tour of 477.ae.95 died here at MIP04C28.
        if (field_90_pal_depth == 0)
        {
            Tethys_gPalSkip++;
            return;
        }
#endif
        if (field_90_pal_depth != 16 && field_90_pal_depth != 64 && field_90_pal_depth != 256)
        {
            LOG_ERROR("Bad pal depth " << field_90_pal_depth);
            ALIVE_FATAL("Bad pal depth");
        }
        IRenderer::GetRenderer()->PalSetData(IRenderer::PalRecord{field_8C_pal_vram_xy.field_0_x, field_8C_pal_vram_xy.field_2_y, field_90_pal_depth}, pPal + 4);
    }
}

void Animation::Get_Frame_Offset_40C480(s16* pBoundingX, s16* pBoundingY)
{
    FrameInfoHeader* pFrameHeader = Get_FrameHeader_40B730(-1);
    *pBoundingX = pFrameHeader->field_8_data.offsetAndRect.mOffset.x;
    *pBoundingY = pFrameHeader->field_8_data.offsetAndRect.mOffset.y;
}


void Animation::Get_Frame_Width_Height_40C400(s16* pWidth, s16* pHeight)
{
    FrameInfoHeader* pFrameHeader = Get_FrameHeader_40B730(-1);
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

// ================================================================

namespace AETest::TestsAnimation {
struct TestAnimData final
{
    AnimationHeader mHeader;
    FrameInfoHeader mFrameInfoHeader;
    FrameHeader mFrameHeader;
};

static void RenderTest()
{
    Animation anim;
    anim.field_84_vram_rect.x = 0;
    anim.field_84_vram_rect.y = 0;
    anim.field_4_flags.Raw().all = 0;
    anim.field_4_flags.Set(AnimFlags::eBit3_Render);
    anim.field_92_current_frame = 0;
    anim.field_C_render_layer = Layer::eLayer_0;
    anim.field_14_scale = FP_FromInteger(1);
    anim.field_B_render_mode = TPageAbr::eBlend_0;

    TestAnimData testData = {};
    testData.mHeader.mFrameOffsets[0] = sizeof(AnimationHeader);
    testData.mHeader.field_0_fps = 2;

    testData.mFrameInfoHeader.field_0_frame_header_offset = sizeof(AnimationHeader) + sizeof(FrameInfoHeader);
    testData.mFrameInfoHeader.field_6_count = 1;
    testData.mFrameInfoHeader.field_8_data.offsetAndRect.mOffset.x = 3;
    testData.mFrameInfoHeader.field_8_data.offsetAndRect.mOffset.y = 7;

    testData.mFrameHeader.field_4_width = 20;
    testData.mFrameHeader.field_5_height = 30;
    testData.mFrameHeader.field_6_colour_depth = 8;

    TestAnimData* pTestData = &testData;

    anim.field_20_ppBlock = (u8**) &pTestData;
    anim.field_18_frame_table_offset = 0;

    gPsxDisplay_5C1130.field_C_buffer_index = 0;

    PrimHeader* ot[256] = {};
    Poly_FT4* pPoly = &anim.field_2C_ot_data[gPsxDisplay_5C1130.field_C_buffer_index];

    {
        *pPoly = {};

        anim.field_4_flags.Set(AnimFlags::eBit16_bBlending);
        anim.vRender_40B820(20, 30, ot, 0, 0);

        ASSERT_EQ(X0(pPoly), 37);
        ASSERT_EQ(Y0(pPoly), 37);

        ASSERT_EQ(X1(pPoly), 56);
        ASSERT_EQ(Y1(pPoly), 37);

        ASSERT_EQ(X2(pPoly), 37);
        ASSERT_EQ(Y2(pPoly), 66);

        ASSERT_EQ(X3(pPoly), 56);
        ASSERT_EQ(Y3(pPoly), 66);

        ASSERT_EQ(U0(pPoly), 0);
        ASSERT_EQ(V0(pPoly), 0);

        ASSERT_EQ(U1(pPoly), 19);
        ASSERT_EQ(V1(pPoly), 0);

        ASSERT_EQ(U2(pPoly), 0);
        ASSERT_EQ(V2(pPoly), 29);

        ASSERT_EQ(U3(pPoly), 19);
        ASSERT_EQ(V3(pPoly), 29);
    }

    {
        *pPoly = {};
        anim.field_4_flags.Set(eBit5_FlipX);
        anim.vRender_40B820(20, 30, ot, 0, 0);

        ASSERT_EQ(X0(pPoly), 11);
        ASSERT_EQ(Y0(pPoly), 37);

        ASSERT_EQ(X1(pPoly), 30);
        ASSERT_EQ(Y1(pPoly), 37);

        ASSERT_EQ(X2(pPoly), 11);
        ASSERT_EQ(Y2(pPoly), 66);

        ASSERT_EQ(X3(pPoly), 30);
        ASSERT_EQ(Y3(pPoly), 66);


        ASSERT_EQ(U0(pPoly), 19);
        ASSERT_EQ(V0(pPoly), 0);

        ASSERT_EQ(U1(pPoly), 0);
        ASSERT_EQ(V1(pPoly), 0);

        ASSERT_EQ(U2(pPoly), 19);
        ASSERT_EQ(V2(pPoly), 29);

        ASSERT_EQ(U3(pPoly), 0);
        ASSERT_EQ(V3(pPoly), 29);
    }

    {
        *pPoly = {};
        anim.field_4_flags.Set(eBit5_FlipX);
        anim.field_4_flags.Set(eBit13_Is8Bit);

        anim.vRender_40B820(20, 30, ot, 0, 0);

        ASSERT_EQ(X0(pPoly), 11);
        ASSERT_EQ(Y0(pPoly), 37);

        ASSERT_EQ(X1(pPoly), 30);
        ASSERT_EQ(Y1(pPoly), 37);

        ASSERT_EQ(X2(pPoly), 11);
        ASSERT_EQ(Y2(pPoly), 66);

        ASSERT_EQ(X3(pPoly), 30);
        ASSERT_EQ(Y3(pPoly), 66);


        ASSERT_EQ(U0(pPoly), 19);
        ASSERT_EQ(V0(pPoly), 0);

        ASSERT_EQ(U1(pPoly), 0);
        ASSERT_EQ(V1(pPoly), 0);

        ASSERT_EQ(U2(pPoly), 19);
        ASSERT_EQ(V2(pPoly), 29);

        ASSERT_EQ(U3(pPoly), 0);
        ASSERT_EQ(V3(pPoly), 29);
    }
}

void AnimationTests()
{
    RenderTest();
}
} // namespace AETest::TestsAnimation
