#include "stdafx_ao.h"
#include "Function.hpp"
#include "LightEffect.hpp"
#include "ResourceManager.hpp"
#include "Math.hpp"
#include "Game.hpp"
#include "stdlib.hpp"
#include "CameraSwapper.hpp"
#include "ScreenManager.hpp"

namespace AO {

LightEffect* LightEffect::ctor_4064C0(Path_LightEffect* pTlv, s32 tlvInfo)
{
    ctor_417C10();
    SetVTable(this, 0x4BA1E0);

    field_4_typeId = Types::eNone_0;
    field_E4_tlvInfo = tlvInfo;

    const AnimRecord& rec = AO::AnimRec(AnimId::Star);
    u8** ppRes = ResourceManager::GetLoadedResource_4554F0(ResourceManager::Resource_Animation, rec.mResourceId, 1, 0);
    Animation_Init_417FD0(rec.mFrameTableOffset, rec.mMaxW, rec.mMaxH, ppRes, 1);

    field_EC_rnd1 = gnFrameCount_507670 + Math_RandomRange_450F20(2, 8);
    field_F0_rnd2 = field_EC_rnd1 + Math_RandomRange_450F20(4, 8);
    field_F4_rnd3 = Math_RandomRange_450F20(96, 128);
    field_F8_rnd4 = Math_RandomRange_450F20(190, 255);

    field_CC_bApplyShadows &= ~1u;

    field_10_anim.field_4_flags.Set(AnimFlags::eBit20_use_xy_offset);
    field_10_anim.field_4_flags.Set(AnimFlags::eBit15_bSemiTrans);
    field_10_anim.field_4_flags.Set(AnimFlags::eBit16_bBlending);

    field_10_anim.field_C_layer = Layer::eLayer_Foreground_Half_17;
    field_10_anim.field_B_render_mode = TPageAbr::eBlend_0;
    field_10_anim.SetFrame_402AC0(1);

    // maybe width height like in door effect
    field_E8 = 0;
    field_EA = 0;

    field_BC_sprite_scale = FP_FromDouble(0.4);
    field_A8_xpos = FP_FromInteger(pTlv->field_10_top_left.field_0_x);
    field_AC_ypos = FP_FromInteger(pTlv->field_10_top_left.field_2_y);
#ifdef TETHYS_SATURN
    Tethys_StarArm(this, &field_D4, &field_D8); // SATURN 477.ao.2: the PlayStation's twinkle (VRender)
#endif
    return this;
}

BaseGameObject* LightEffect::dtor_406770()
{
    SetVTable(this, 0x4BA1E0);
    gMap_507BA8.TLV_Reset_446870(field_E4_tlvInfo, -1, 0, 0);
    return dtor_417D10();
}

BaseGameObject* LightEffect::VDestructor(s32 flags)
{
    return Vdtor_406800(flags);
}

LightEffect* LightEffect::Vdtor_406800(s32 flags)
{
    dtor_406770();
    if (flags & 1)
    {
        ao_delete_free_447540(this);
    }
    return this;
}


#ifdef TETHYS_SATURN
// SATURN 477.ao.2 -- THE STOCKYARDS' TWINKLING STARS, AS THE PLAYSTATION SHOWS THEM.
//
// The PC original ships VRender empty, so the 31 stars of E1/E2 drew neither on
// PC nor here: only the glow painted into the camera was left, still ("halos
// instead of blinking stars", 473.ao.1 field report). The PlayStation draws them.
//
// What it draws, measured frame by frame on a PS1 capture (PAL, E1P02C01, ten
// seconds, the four stars and the gold glow beside them): a sparkle at the 0.4
// scale the ctor sets, holding one of TWO steady looks, a brighter one and a
// smaller, dimmer one (area 35 against 19 video pixels, peak 254 against ~200),
// flipping at random, bright for 2 to 6 ticks, dim for 3 to 9. The 20x12 big
// sparkle never shows. Neither STAR.BAN's own loop (26 frames, one a tick, the
// big one three times a cycle) nor upstream's VUpdate ramp (its reschedule
// branch cannot run as decompiled: frame > rnd2 implies frame >= rnd1, tested
// first) produces that, so the twinkle is the PlayStation's as seen: frame 1
// (12x8) bright, frame 2 (10x6) dim, the loop stopped.
//
// ITS OWN GENERATOR, never Math_RandomRange_450F20: the attract demos replay pad
// input open-loop and PLAYBK10 and PLAYBK20 walk the Stockyards; one extra draw
// from the game's generator sends a demo off course.
static u32 sTethysStarSeed = 0x2545F491u;

static s32 Tethys_StarRand(s32 lo, s32 hi)
{
    sTethysStarSeed = sTethysStarSeed * 1103515245u + 12345u;
    const u32 r = (sTethysStarSeed >> 16) & 0x7FFFu;
    return lo + static_cast<s32>((r * static_cast<u32>(hi - lo + 1)) >> 15);
}

void Tethys_StarArm(BaseAnimatedWithPhysicsGameObject* pObj, s32* pNextFlip, s32* pDim)
{
    pObj->field_10_anim.field_10_frame_delay = 0x7FFF; // the loop stopped: Tethys_StarTwinkle picks the frame
    pObj->field_10_anim.SetFrame_402AC0(1);
    *pDim = 0;
    *pNextFlip = static_cast<s32>(gnFrameCount_507670) + Tethys_StarRand(0, 6); // out of step from the start
}

void Tethys_StarTwinkle(BaseAnimatedWithPhysicsGameObject* pObj, s32* pNextFlip, s32* pDim)
{
    if (static_cast<s32>(gnFrameCount_507670) - *pNextFlip < 0)
    {
        return;
    }
    *pDim = !*pDim;
    *pNextFlip = static_cast<s32>(gnFrameCount_507670) + (*pDim ? Tethys_StarRand(3, 9) : Tethys_StarRand(2, 6));
    pObj->field_10_anim.SetFrame_402AC0(*pDim ? 2 : 1);
}

// Upstream's reconstruction below, live, plus the one thing it left out: the 0.4
// sprite scale, which the base class applies in its own VRender
// (BaseAnimatedWithPhysicsGameObject.cpp:146) and this override never did. At
// scale 1 the sparkle is 20x12, upstream's "over bright ugly" star. The texture
// is raw (eBit16_bBlending, ctor), so no colour reaches the cel.
//
// 477.ao.3: THE BRIGHT LOOK IS DRAWN AT 0.5. At 0.4 the VDP1 shrink keeps too few
// texels of these tiny cels (it covers floor(size * scale) + 1 pixels and samples
// the nearest texel): frame 1 came out as a 3x4 cross with a 1x2 white core and
// grey arms, frame 2 as a 3x3 cross with one white pixel and brighter lavender
// arms, so the DIM look read as the bigger one (Ymir capture of E1P02C01, 21 Oct).
// The PlayStation shows the opposite: bright about twice the dim one's area, and
// whiter. Frame 1 at 0.5 covers 4x5 with a 4-pixel white cross; frame 2 stays at
// the ctor's 0.4. Render only: field_BC_sprite_scale keeps its 0.4.
void Tethys_StarRender(BaseAnimatedWithPhysicsGameObject* pObj, PrimHeader** ppOt)
{
    if (sNumCamSwappers_507668 == 0)
    {
        const FP xpos = FP_FromInteger(pScreenManager_4FF7C8->field_14_xpos) + pObj->field_A8_xpos - pScreenManager_4FF7C8->field_10_pCamPos->field_0_x;
        const FP ypos = FP_FromInteger(pScreenManager_4FF7C8->field_16_ypos) + pObj->field_AC_ypos - pScreenManager_4FF7C8->field_10_pCamPos->field_4_y;

        pObj->field_10_anim.field_14_scale = pObj->field_10_anim.field_92_current_frame == 1
                                                 ? FP_FromRaw(0x8000) // 0.5, the bright look
                                                 : pObj->field_BC_sprite_scale;
        pObj->field_10_anim.vRender(FP_GetExponent(xpos), FP_GetExponent(ypos), ppOt, 0, 0);

        PSX_RECT rect = {};
        pObj->field_10_anim.Get_Frame_Rect_402B50(&rect);
        pScreenManager_4FF7C8->InvalidateRect_406E40(
            rect.x,
            rect.y,
            rect.w,
            rect.h,
            pScreenManager_4FF7C8->field_2E_idx);
    }
}
#endif

void LightEffect::VRender_4067F0(PrimHeader** ppOt)
{
#ifdef TETHYS_SATURN
    Tethys_StarRender(this, ppOt); // SATURN 477.ao.2: see above
#else
    (void) ppOt;
#endif
    // Pretty much the same as door effect render - commented out because OG is empty which means the "stock yard stars" are missing.
    // However rendering as-is produces over bright ugly looking stars and is probably why DD turned it off
    /*
    if (sNumCamSwappers_507668 == 0)
    {
        const FP xpos = FP_FromInteger(pScreenManager_4FF7C8->field_14_xpos) + field_A8_xpos - pScreenManager_4FF7C8->field_10_pCamPos->field_0_x;
        const FP ypos = FP_FromInteger(pScreenManager_4FF7C8->field_16_ypos) + field_AC_ypos - pScreenManager_4FF7C8->field_10_pCamPos->field_4_y;

        field_10_anim.field_8_r = static_cast<u8>(field_C0_r/4);
        field_10_anim.field_9_g = static_cast<u8>(field_C2_g/4);
        field_10_anim.field_A_b = static_cast<u8>(field_C4_b/4);

        field_10_anim.vRender(
            FP_GetExponent(FP_FromInteger((FP_GetExponent(xpos)))),
            FP_GetExponent(FP_FromInteger((FP_GetExponent(ypos)))),
            ppOt,
            0,
            0);

        PSX_RECT rect = {};
        field_10_anim.Get_Frame_Rect_402B50(&rect);
        pScreenManager_4FF7C8->InvalidateRect_406E40(
            rect.x,
            rect.y,
            rect.w,
            rect.h,
            pScreenManager_4FF7C8->field_2E_idx);
    }
    */
}

void LightEffect::VRender(PrimHeader** ppOt)
{
    VRender_4067F0(ppOt);
}

void LightEffect::VScreenChanged()
{
    VScreenChanged_4067E0();
}

void LightEffect::VScreenChanged_4067E0()
{
    field_6_flags.Set(BaseGameObject::eDead_Bit3);
}

void LightEffect::VUpdate()
{
    VUpdate_406610();
}

void LightEffect::VUpdate_406610()
{
#ifdef TETHYS_SATURN
    Tethys_StarTwinkle(this, &field_D4, &field_D8); // SATURN 477.ao.2: see VRender
#endif
    if (static_cast<s32>(gnFrameCount_507670) >= field_EC_rnd1)
    {
        const s32 v6 = field_F0_rnd2 - field_EC_rnd1;
        if (static_cast<s32>(gnFrameCount_507670) == v6 / 2)
        {
            field_F4_rnd3 = Math_RandomRange_450F20(96, 128);
        }

        const FP v9 = (FP_FromInteger(128) * FP_FromInteger(gnFrameCount_507670 - field_EC_rnd1) / FP_FromInteger(v6));
        const FP v11 = -Math_Cosine_4510A0(FP_GetExponent(v9) & 0xFF);

        s32 tmp = field_F4_rnd3 + FP_GetExponent(FP_FromInteger(field_F8_rnd4) * v11);

        u8 rgb = 0;
        if (tmp <= 255)
        {
            rgb = tmp & 0xFF;
        }
        else
        {
            rgb = 255;
        }

        field_C0_r = rgb;
        field_C2_g = rgb;
        field_C4_b = rgb;
    }
    else if (static_cast<s32>(gnFrameCount_507670) > field_F0_rnd2)
    {
        field_EC_rnd1 = gnFrameCount_507670 + Math_RandomRange_450F20(2, 8);
        field_F0_rnd2 = field_EC_rnd1 + Math_RandomRange_450F20(4, 8);
        field_F8_rnd4 = Math_RandomRange_450F20(150, 180);
        u8 rgb = 96;

        field_C0_r = rgb;
        field_C2_g = rgb;
        field_C4_b = rgb;
    }
}

} // namespace AO
