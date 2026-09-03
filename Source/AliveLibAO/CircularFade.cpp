#include "stdafx_ao.h"
#include "CircularFade.hpp"
#include "Function.hpp"
#include "ScreenManager.hpp"
#include "ResourceManager.hpp"
#include "PsxDisplay.hpp"
#include "stdlib.hpp"

namespace AO {

CircularFade* CircularFade::ctor_479E20(FP xpos, FP ypos, FP scale, s16 direction, s8 destroyOnDone)
{
    ctor_417C10();
    SetVTable(this, 0x4BCE38);

    if (direction)
    {
        field_1A8_fade_colour = 0;
    }
    else
    {
        field_1A8_fade_colour = 255;
    }

    // NOTE: Inlined
    VFadeIn_479FE0(static_cast<s8>(direction), destroyOnDone);

    const u8 fade_rgb = static_cast<u8>((field_1A8_fade_colour * 60) / 100);
    field_C4_b = fade_rgb;
    field_C2_g = fade_rgb;
    field_C0_r = fade_rgb;

    const AnimRecord& rec = AO::AnimRec(AnimId::Circular_Fade);
    u8** ppRes = ResourceManager::GetLoadedResource_4554F0(ResourceManager::Resource_Animation, rec.mResourceId, 1, 0);
    Animation_Init_417FD0(rec.mFrameTableOffset, rec.mMaxW, rec.mMaxH, ppRes, 1);

    field_CC_bApplyShadows &= ~1u;

    field_10_anim.field_4_flags.Clear(AnimFlags::eBit16_bBlending);
    field_BC_sprite_scale.fpValue = scale.fpValue * 2;
    field_10_anim.field_14_scale.fpValue = scale.fpValue * 2;

    field_A8_xpos = xpos;
    field_AC_ypos = ypos;
    field_10_anim.field_B_render_mode = TPageAbr::eBlend_2;
    field_10_anim.field_C_layer = Layer::eLayer_FadeFlash_40;
    field_C0_r = field_1A8_fade_colour;
    field_C2_g = field_1A8_fade_colour;
    field_C4_b = field_1A8_fade_colour;

    Init_SetTPage_495FB0(&field_188_tPage[0], 0, 0, PSX_getTPage_4965D0(TPageMode::e16Bit_2, TPageAbr::eBlend_2, 0, 0));
    Init_SetTPage_495FB0(&field_188_tPage[1], 0, 0, PSX_getTPage_4965D0(TPageMode::e16Bit_2, TPageAbr::eBlend_2, 0, 0));
    return this;
}

BaseGameObject* CircularFade::VDestructor(s32 flags)
{
    dtor_417D10();
    if (flags & 1)
    {
        ao_delete_free_447540(this);
    }
    return this;
}

void CircularFade::VRender(PrimHeader** ppOt)
{
    VRender_47A080(ppOt);
}

void CircularFade::VRender_47A080(PrimHeader** ppOt)
{
    const u8 fade_rgb = static_cast<u8>((field_1A8_fade_colour * 60) / 100);

    field_C0_r = fade_rgb;
    field_C4_b = fade_rgb;
    field_C2_g = fade_rgb;

    field_10_anim.field_8_r = fade_rgb;
    field_10_anim.field_9_g = fade_rgb;
    field_10_anim.field_A_b = fade_rgb;

#ifdef TETHYS_SATURN
    // SATURN (347.ao.1): ONE FULL-SCREEN QUAD INSTEAD OF AN IRIS.  Ported from
    // the decision already taken and shipped on the AE side (relive-ae
    // 9fcf0ad9, "the iris is a subtraction, so it becomes one full-screen
    // fade"), because the hardware argument is identical and AO has no reason
    // to reach a different answer.
    //
    // Every piece of this effect is a SUBTRACTIVE blend the Saturn cannot do
    // where it is needed.  The PSX effect is a SpotLight cel scaled over the
    // focus point plus four eBlend_2 tiles filling the screen around that cel's
    // frame rect; eBlend_2 is B - F, so a tile at colour 0 subtracts nothing
    // and is INVISIBLE, and the picture darkens as the colour climbs to 255.
    // On this port the background is the VDP2 bitmap plane and the tiles are
    // VDP1 quads, and the two cannot blend at all -- VDP1 draws into its own
    // framebuffer.  AE's tester saw the consequence verbatim: "ecran noir,
    // carre blanc, rond noir".
    //
    // 346.ao.1 tried to keep the SHAPE, approximating the subtract with a black
    // VDP1 quad at three coverages (none / half-transparency / opaque).  That
    // was the wrong trade and the tester said so: three levels across a 22-frame
    // ramp is two hard steps, the first of them on frame ONE, where the PSX is
    // still showing the room untouched.
    //
    // The port already has the right instrument.  A FULL-SCREEN tile is folded
    // into the VDP2 COLOUR OFFSET (renderer_saturn.cxx AccumulateFade), which
    // darkens the camera plane and the sprites together, register-only, 255
    // smooth levels, and whose eBlend_2 case is exactly a negative offset.  So
    // emit ONE full-screen tile carrying the same colour and the same blend
    // mode and let that path do the work: the intensity ramp -- the part that
    // actually hides the camera swap -- is then reproduced EXACTLY, not
    // approximated.  640 x height is EffectBase's own geometry
    // (EffectBase.cpp:65-66), which is what FadeIsFullScreen is written around.
    //
    // WHAT IS DELIBERATELY LOST: the circular shape.  A colour offset is global
    // to the layer, so there is no way to leave a hole in it.  A faithful iris
    // needs a VDP2 LINE WINDOW gating NBG1 (a per-scanline x0/x1 table, 960 B,
    // which does describe a circle) plus VDP1 user clipping for the sprites --
    // a feature to prototype on hardware, not a bug fix.  A clean fade is a
    // correct transition; a stepped black frame around a bright square is not.
    //
    // The tPage prim still goes in, and AFTER the tile: OrderingTable_Add
    // PREPENDS, so the walk meets the tPage first and sModalTPage carries
    // eBlend_2 by the time the tile is drawn.  That ordering is the whole
    // reason the backend knows this is a subtract.
    {
        const u8 saturnFade = static_cast<u8>(field_1A8_fade_colour);
        Prim_Tile* pFull = &field_E8[gPsxDisplay_504C78.field_A_buffer_index];
        Init_Tile(pFull);
        SetRGB0(pFull, saturnFade, saturnFade, saturnFade);
        SetXY0(pFull, 0, 0);
        pFull->field_14_w = 640;
        pFull->field_16_h = gPsxDisplay_504C78.field_2_height;
        Poly_Set_SemiTrans_498A40(&pFull->mBase.header, 1);
        OrderingTable_Add_498A80(OtLayer(ppOt, field_10_anim.field_C_layer), &pFull->mBase.header);
        OrderingTable_Add_498A80(OtLayer(ppOt, field_10_anim.field_C_layer), &field_188_tPage[gPsxDisplay_504C78.field_A_buffer_index].mBase);
        pScreenManager_4FF7C8->InvalidateRect_406CC0(0, 0, 640, gPsxDisplay_504C78.field_2_height);
    }
#else
    field_10_anim.vRender(
        FP_GetExponent(field_A8_xpos + (FP_FromInteger(pScreenManager_4FF7C8->field_14_xpos + field_CA_xOffset)) - pScreenManager_4FF7C8->field_10_pCamPos->field_0_x),
        FP_GetExponent(field_AC_ypos + (FP_FromInteger(pScreenManager_4FF7C8->field_16_ypos + field_C8_yOffset)) - pScreenManager_4FF7C8->field_10_pCamPos->field_4_y),
        ppOt,
        0,
        0);
    PSX_RECT frameRect = {};
    field_10_anim.Get_Frame_Rect_402B50(&frameRect);
    pScreenManager_4FF7C8->InvalidateRect_406E40(
        frameRect.x,
        frameRect.y,
        frameRect.w,
        frameRect.h,
        pScreenManager_4FF7C8->field_2E_idx);

    frameRect.h--;
    frameRect.w--;

    if (frameRect.y < 0)
    {
        frameRect.y = 0;
    }

    if (frameRect.x < 0)
    {
        frameRect.x = 0;
    }

    if (frameRect.w >= 640)
    {
        frameRect.w = 639;
    }

    if (frameRect.h >= 240)
    {
        frameRect.h = 240;
    }

    const u8 fadeColour = static_cast<u8>(field_1A8_fade_colour);


    Prim_Tile* pTile = &field_E8[gPsxDisplay_504C78.field_A_buffer_index];
    Init_Tile(pTile);
    SetRGB0(pTile, fadeColour, fadeColour, fadeColour);
    SetXY0(pTile, 0, 0);
    pTile->field_14_w = gPsxDisplay_504C78.field_0_width;
    pTile->field_16_h = frameRect.y;
    Poly_Set_SemiTrans_498A40(&pTile->mBase.header, 1);
    OrderingTable_Add_498A80(OtLayer(ppOt, field_10_anim.field_C_layer), &pTile->mBase.header);

    Prim_Tile* pTile2_1 = &field_110[gPsxDisplay_504C78.field_A_buffer_index];
    Init_Tile(pTile2_1);
    SetRGB0(pTile2_1, fadeColour, fadeColour, fadeColour);

    s16 w = 0;
    if (field_10_anim.field_4_flags.Get(AnimFlags::eBit5_FlipX))
    {
        w = frameRect.x + 1;
    }
    else
    {
        w = frameRect.x;
    }
    SetXY0(pTile2_1, 0, frameRect.y);
    pTile2_1->field_14_w = w;
    pTile2_1->field_16_h = frameRect.h - frameRect.y;
    Poly_Set_SemiTrans_498A40(&pTile2_1->mBase.header, 1);
    OrderingTable_Add_498A80(OtLayer(ppOt, field_10_anim.field_C_layer), &pTile2_1->mBase.header);

    Prim_Tile* pTile2 = &field_138[gPsxDisplay_504C78.field_A_buffer_index];
    Init_Tile(pTile2);
    SetRGB0(pTile2, fadeColour, fadeColour, fadeColour);
    SetXY0(pTile2, frameRect.w + 1, frameRect.y);
    pTile2->field_14_w = gPsxDisplay_504C78.field_0_width - frameRect.w;
    pTile2->field_16_h = frameRect.h - frameRect.y;
    Poly_Set_SemiTrans_498A40(&pTile2->mBase.header, 1);
    OrderingTable_Add_498A80(OtLayer(ppOt, field_10_anim.field_C_layer), &pTile2->mBase.header);

    Prim_Tile* pTile3 = &field_160[gPsxDisplay_504C78.field_A_buffer_index];
    Init_Tile(pTile3);
    SetRGB0(pTile3, fadeColour, fadeColour, fadeColour);
    SetXY0(pTile3, 0, frameRect.h);
    pTile3->field_14_w = gPsxDisplay_504C78.field_0_width;
    pTile3->field_16_h = gPsxDisplay_504C78.field_2_height - frameRect.h;
    Poly_Set_SemiTrans_498A40(&pTile3->mBase.header, 1);
    OrderingTable_Add_498A80(OtLayer(ppOt, field_10_anim.field_C_layer), &pTile3->mBase.header);
    OrderingTable_Add_498A80(OtLayer(ppOt, field_10_anim.field_C_layer), &field_188_tPage[gPsxDisplay_504C78.field_A_buffer_index].mBase);

    if (field_1A8_fade_colour < 255)
    {
        pScreenManager_4FF7C8->InvalidateRect_406CC0(
            0,
            0,
            gPsxDisplay_504C78.field_0_width,
            gPsxDisplay_504C78.field_2_height);
    }

#endif

    // SHARED: the done-detection reads field_1A8_fade_colour only, so it is the
    // same on both sides of the branch above.
    if ((field_1A8_fade_colour == 255 && field_E4_flags.Get(CircularFade::eBit1_FadeIn)) || (field_1A8_fade_colour == 0 && !field_E4_flags.Get(CircularFade::eBit1_FadeIn)))
    {
        field_E4_flags.Set(CircularFade::eBit2_Done);

        if (field_E4_flags.Get(CircularFade::eBit3_DestroyOnDone))
        {
            field_6_flags.Set(BaseGameObject::eDead_Bit3);
        }
    }
}

void CircularFade::VUpdate()
{
    VUpdate_47A030();
}

void CircularFade::VUpdate_47A030()
{
    if ((!field_E4_flags.Get(Flags::eBit4_NeverSet) && !field_E4_flags.Get(Flags::eBit2_Done)))
    {
        field_1A8_fade_colour += field_1AA_speed;
        if (field_E4_flags.Get(Flags::eBit1_FadeIn))
        {
            if (field_1A8_fade_colour > 255)
            {
                field_1A8_fade_colour = 255;
            }
        }
        else if (field_1A8_fade_colour < 0)
        {
            field_1A8_fade_colour = 0;
        }
    }
}

s8 CircularFade::VFadeIn_479FE0(u8 direction, s8 destroyOnDone)
{
    field_E4_flags.Set(Flags::eBit1_FadeIn, direction);

    field_E4_flags.Clear(Flags::eBit2_Done);
    field_E4_flags.Clear(Flags::eBit4_NeverSet);

    field_E4_flags.Set(Flags::eBit3_DestroyOnDone, destroyOnDone);


    if (field_E4_flags.Get(Flags::eBit1_FadeIn))
    {
        field_1AA_speed = 12;
    }
    else
    {
        field_1AA_speed = -12;
    }
    return static_cast<s8>(field_E4_flags.Raw().all);
}

void CircularFade::VScreenChanged()
{
    // Empty
}

s32 CircularFade::VDone_47A4C0()
{
    return field_E4_flags.Get(Flags::eBit2_Done);
}

CircularFade* CC Make_Circular_Fade_447640(FP xpos, FP ypos, FP scale, s16 direction, s8 destroyOnDone)
{
    auto pCircularFade = ao_new<CircularFade>();
    if (pCircularFade)
    {
        pCircularFade->ctor_479E20(xpos, ypos, scale, direction, destroyOnDone);
    }
    return pCircularFade;
}

} // namespace AO
