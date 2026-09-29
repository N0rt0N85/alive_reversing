#include "stdafx_ao.h"
#include "Function.hpp"
#include "MotionDetector.hpp"
#include "stdlib.hpp"
#include "ResourceManager.hpp"
#include "SwitchStates.hpp"
#include "Sfx.hpp"
#include "Events.hpp"
#include "CameraSwapper.hpp"
#include "Abe.hpp"
#include "DDCheat.hpp"
#include "Game.hpp"
#include "Alarm.hpp"
#include "ScreenManager.hpp"
#include "PsxDisplay.hpp"
#include "Sys_common.hpp"

namespace AO {

#undef min
#undef max

// SATURN: 423.ao.2 -- THE DETECTOR, ITS BEAM AND ITS LASER BAR GO BEHIND THE
// ACTORS.  AO files all three on eLayer_Foreground_36, over Abe (32) and every
// other actor (33-35), and relies on the PSX's additive blend to tint whoever
// stands in the beam.  VDP1 has one framebuffer and can only ADD against the
// VDP2 background, so a beam or a 37x60 laser bar drawn over Abe replaces his
// texels and cuts him in two.  Layer 31 is the last one before the actors:
// they now paint over the effect, which keeps its place above everything else
// (FG1 at 37 still covers it, as in AO).  The beam's blend itself is the
// renderer's (Draw(Poly_F3&), src/renderer_saturn.cxx).
#ifdef TETHYS_SATURN
static constexpr Layer kDetectorLayer = Layer::eLayer_DoorFlameRollingBallPortalClip_Half_31;
#else
static constexpr Layer kDetectorLayer = Layer::eLayer_Foreground_36;
#endif

// SATURN: 424.ao.2 -- THE LASER'S RED, PUT BACK ON ABE. Layer 31 costs the red
// the beam and the bar ADD to Abe on the PSX, and in E1/E2 -- the only levels
// with detectors -- he is a silhouette (sAbeTints_4C6438: 25/25/25, a 3/16
// shade), so that red is most of what the player sees of the laser on him.
// VRender asks the renderer to add it to his palette while he stands in the bar
// (Tethys_HeroLaserTint, src/renderer_saturn.cxx). The PSX adds it as a bright
// ~13-column line plus the beam; a palette can only add it to ALL of him, so
// the amounts (5-bit CRAM units) match the PSX's mean red over his pixels when
// the bar is centred on him -- its most visible moment, and the line is exactly
// what a uniform tint loses. Composited from the shipped E1P06C04: at rest (Abe
// still, beam B + F/4) the PSX mean is 45 and 5 gives 45; in detection (Abe
// walking, beam B + F) it is 72 and 8 gives 71; about 4 without either.
// Averaged over the whole crossing instead, the PSX adds only 24 and 42.
#ifdef TETHYS_SATURN
extern "C" void Tethys_HeroLaserTint(s32 clut, s32 red5);
static constexpr s32 kLaserRedIdle = 5;   // the beam at B + F/4
static constexpr s32 kLaserRedDetect = 8; // the beam at B + F
#endif

// SATURN: 425.ao.1 -- THE BEAM WAS DRAWN IN FRONT OF ITS OWN BAR, AND THE WHOLE
// GROUP IN FRONT OF A BACKGROUND-PLANE ABE.  Two field reports, one shape: the
// group had ONE layer, so its three parts had one order and it was the wrong
// one.  It now spans a BAND, and the band mirrors when the hero changes plane.
// kDetectorLayer above stays the ctor's initial value; VRender re-files all
// three parts every frame, because both facts it reads change every frame.
//
//  (a) THE BAR ATE ITSELF.  OrderingTable_Add inserts at the HEAD (bucket walk
//      is LIFO) and the drawables list holds the detector before its own laser,
//      so the triangle was emitted AFTER the bar and landed in front of it.  The
//      triangle's base is the vertical at the bar's x, while the bar's cel is
//      CENTRED on that x (mMin.x == -mMax.x on all 12 frames of MOTION.BAN), so
//      the flat red replaced the half of the bar on the emitter's side over most
//      of its height, and swapped sides each time the bar crossed the emitter.
//      That is the tester's "we never see the end of the laser whole", and it
//      needs only ONE detector.  Bar and flare now sit a band above every beam,
//      so no bar can be eaten -- not by its own beam, not by a neighbour's.  It
//      is also the first time the order is DETERMINISTIC: DynamicArray::
//      Remove_Item swaps the last element into the freed slot, so the drawables
//      order this used to depend on is not stable across a session.
//  (b) BEAMS OF ONE FAN are then ranked by span, SHORTEST IN FRONT (the tester's
//      "centre in front, outside behind").  At height y beam i covers
//      [x0 + (x1_i - x0) * (y - y0) / H, x1_i], so a short beam in front can
//      never cover a long beam's tip while the reverse does.  Not cosmetic:
//      field_160_bObjectInLaser is PER DETECTOR, so when the hero cuts one beam
//      of a fan only that one goes bright, and a bright beam hidden behind a
//      long idle one removes the only cue that says which beam he is in.
//      Fans are 3 at most (E1P06: 3 on one emitter; E2P04: two groups of 3).
//  (c) 28 AND NOT 30.  BirdPortal drops a ScreenClipper at 29 that narrows the
//      clip to half the screen and another at 31 that restores it, and E1 path 6
//      -- the fan screen -- carries four BirdPortals.  A beam at 28 is emitted
//      before the clip narrows; the bar at 31 keeps sharing the restoring
//      clipper's bucket exactly as it does today.  Ranks 1 and 2 fall on 27 and
//      26 (26 is the full-scale shadow bucket: a third beam and a shadow then
//      share it, which is an order between two things that never meet on screen
//      -- beams hang from the ceiling, shadows lie on the floor).
#ifdef TETHYS_SATURN
static constexpr s16 kLaserBeamLayer = 28; // eLayer_ZapLinesElum_28
static constexpr s16 kLaserBarLayer = 31;  // eLayer_DoorFlameRollingBallPortalClip_Half_31
static constexpr s16 kLaserHalfBand = 19;  // full layer - half layer, Layer.hpp (32-13, 31-12)
static constexpr s16 kLaserFanDepth = 3;   // most beams on one emitter, measured on E1/E2

// Emitter-to-bar distance in PSX units: the fan's rank key.
static s32 Tethys_LaserSpan(FP emitterX, FP barX)
{
    const s32 d = FP_GetExponent(barX) - FP_GetExponent(emitterX);
    return d < 0 ? -d : d;
}
#endif

MotionDetector* MotionDetector::ctor_437A50(Path_MotionDetector* pTlv, s32 tlvInfo)
{
    ctor_417C10();
    SetVTable(this, 0x4BB878);
    field_4_typeId = Types::eMotionDetector_59;
    const AnimRecord& rec = AO::AnimRec(AnimId::MotionDetector_Flare);
    u8** ppRes = ResourceManager::GetLoadedResource_4554F0(ResourceManager::Resource_Animation, rec.mResourceId, 1, 0);
    Animation_Init_417FD0(rec.mFrameTableOffset, rec.mMaxW, rec.mMaxH, ppRes, 1);
    field_10_anim.field_4_flags.Set(AnimFlags::eBit7_SwapXY);
    field_10_anim.field_B_render_mode = TPageAbr::eBlend_1;
    field_10_anim.field_C_layer = kDetectorLayer; // SATURN: was eLayer_Foreground_36
    field_C8_yOffset = 0;
    field_C0_r = 64;
    field_C4_b = 0;
    field_C2_g = 0;
    field_160_bObjectInLaser = 0;
    field_F6_bDontComeBack = 1;
    field_E4_tlvInfo = tlvInfo;

    if (pTlv->field_18_scale == Scale_short::eHalf_1)
    {
        field_BC_sprite_scale = FP_FromDouble(0.5);
    }
    else
    {
        field_BC_sprite_scale = FP_FromInteger(1);
    }

    field_F8_top_left_x = FP_FromInteger(pTlv->field_10_top_left.field_0_x);
    field_100_bottom_right_x = FP_FromInteger(pTlv->field_14_bottom_right.field_0_x);

    field_FC_top_left_y = FP_FromInteger(pTlv->field_10_top_left.field_2_y);
    field_104_bottom_right_y = FP_FromInteger(pTlv->field_14_bottom_right.field_2_y);

    field_A8_xpos = FP_FromInteger(pTlv->field_1A_device_x);
    field_AC_ypos = FP_FromInteger(pTlv->field_1C_device_y);

    field_15C_speed = FP_FromRaw(pTlv->field_1E_speed_x256 << 8);

    const AnimRecord& laserRec = AO::AnimRec(AnimId::MotionDetector_Laser);
    u8** ppResLaser = ResourceManager::GetLoadedResource_4554F0(ResourceManager::Resource_Animation, laserRec.mResourceId, 1, 0);
    if (pTlv->field_20_initial_move_direction == Path_MotionDetector::InitialMoveDirection::eRight_0)
    {
        
        field_E8_state = States::eMoveRight_0;
        auto pMotionDetectors = ao_new<MotionDetectorLaser>();
        if (pMotionDetectors)
        {
            pMotionDetectors->ctor_417C10();
            SetVTable(pMotionDetectors, 0x4BB840);
            pMotionDetectors->field_4_typeId = Types::eRedLaser_76;
            
            pMotionDetectors->Animation_Init_417FD0(laserRec.mFrameTableOffset, laserRec.mMaxW, laserRec.mMaxH, ppResLaser, 1);
            
            pMotionDetectors->field_10_anim.field_B_render_mode = TPageAbr::eBlend_1;
            pMotionDetectors->field_10_anim.field_C_layer = kDetectorLayer; // SATURN: was eLayer_Foreground_36

            pMotionDetectors->field_A8_xpos = field_F8_top_left_x;
            pMotionDetectors->field_AC_ypos = field_104_bottom_right_y;

            pMotionDetectors->field_BC_sprite_scale = field_BC_sprite_scale;
            pMotionDetectors->field_C8_yOffset = 0;
            field_108_pLaser = pMotionDetectors;
        }
    }
    else if (pTlv->field_20_initial_move_direction == Path_MotionDetector::InitialMoveDirection::eLeft_1)
    {
        field_E8_state = States::eMoveLeft_2;
        auto pMotionDetectors = ao_new<MotionDetectorLaser>();
        if (pMotionDetectors)
        {
            pMotionDetectors->ctor_417C10();
            SetVTable(pMotionDetectors, 0x4BB840);
            pMotionDetectors->field_4_typeId = Types::eRedLaser_76;
            
            pMotionDetectors->Animation_Init_417FD0(laserRec.mFrameTableOffset, laserRec.mMaxW, laserRec.mMaxH, ppResLaser, 1);
            
            pMotionDetectors->field_10_anim.field_B_render_mode = TPageAbr::eBlend_1;
            pMotionDetectors->field_10_anim.field_C_layer = kDetectorLayer; // SATURN: was eLayer_Foreground_36
            pMotionDetectors->field_A8_xpos = field_100_bottom_right_x;
            pMotionDetectors->field_AC_ypos = field_104_bottom_right_y;
            pMotionDetectors->field_BC_sprite_scale = field_BC_sprite_scale;
            pMotionDetectors->field_C8_yOffset = 0;
            field_108_pLaser = pMotionDetectors;
        }
    }
    else
    {
        ALIVE_FATAL("couldn't find start move direction for motion detector");
    }

    field_108_pLaser->field_C_refCount++;

    field_F0_disable_switch_id = pTlv->field_24_disable_switch_id;

    field_108_pLaser->field_10_anim.field_4_flags.Set(AnimFlags::eBit3_Render, SwitchStates_Get(field_F0_disable_switch_id) == 0);

    field_10_anim.field_4_flags.Set(AnimFlags::eBit3_Render, pTlv->field_22_draw_flare == Choice_short::eYes_1);

    field_F4_alarm_duration = pTlv->field_28_alarm_duration;

    field_F2_alarm_switch_id = pTlv->field_26_alarm_switch_id;

    return this;
}

void MotionDetector::SetDontComeBack_437E00(s16 bDontComeBack)
{
    field_F6_bDontComeBack = bDontComeBack;
}

BaseGameObject* MotionDetector::dtor_437D70()
{
    SetVTable(this, 0x4BB878);

    if (field_F6_bDontComeBack)
    {
        gMap_507BA8.TLV_Reset_446870(field_E4_tlvInfo, -1, 0, 0);
    }
    else
    {
        gMap_507BA8.TLV_Reset_446870(field_E4_tlvInfo, -1, 0, 1);
    }

    if (field_108_pLaser)
    {
        field_108_pLaser->field_C_refCount--;
        field_108_pLaser->field_6_flags.Set(Options::eDead_Bit3);
    }

    return dtor_417D10();
}

void MotionDetector::VScreenChanged()
{
    VScreenChanged_438520();
}

void MotionDetector::VScreenChanged_438520()
{
    field_6_flags.Set(BaseGameObject::eDead_Bit3);
}

BaseGameObject* MotionDetector::VDestructor(s32 flags)
{
    return Vdtor_438530(flags);
}

MotionDetector* MotionDetector::Vdtor_438530(s32 flags)
{
    dtor_437D70();
    if (flags & 1)
    {
        ao_delete_free_447540(this);
    }

    return this;
}


void MotionDetector::VUpdate()
{
    VUpdate_437E90();
}

void MotionDetector::VUpdate_437E90()
{
    if (Event_Get_417250(kEventDeathReset_4))
    {
        field_6_flags.Set(Options::eDead_Bit3);
    }

    if (!sNumCamSwappers_507668)
    {
        if (SwitchStates_Get(field_F0_disable_switch_id))
        {
            field_108_pLaser->field_10_anim.field_4_flags.Clear(AnimFlags::eBit3_Render);
        }
        else
        {
            field_108_pLaser->field_10_anim.field_4_flags.Set(AnimFlags::eBit3_Render);

            PSX_RECT laserRect = {};
            field_108_pLaser->VGetBoundingRect(&laserRect, 1);

            field_160_bObjectInLaser = FALSE;

            for (s32 i = 0; i < gBaseAliveGameObjects_4FC8A0->Size(); i++)
            {
                BaseAliveGameObject* pObj = gBaseAliveGameObjects_4FC8A0->ItemAt(i);
                if (!pObj)
                {
                    break;
                }

                if (pObj->field_4_typeId != Types::eTimedMine_8)
                {
                    PSX_RECT objRect = {};
                    pObj->VGetBoundingRect(&objRect, 1);

                    if (laserRect.x <= (objRect.w - 8) && laserRect.w >= (objRect.x + 8) && laserRect.h >= objRect.y && laserRect.y <= objRect.h && pObj->field_BC_sprite_scale == field_BC_sprite_scale)
                    {
                        if (pObj == sActiveHero_507678)
                        {
                            if (gnFrameCount_507670 % 2)
                            {
                                SFX_Play_43AD70(SoundEffect::Zap2_58, 45, 0);
                            }
                        }

                        bool alarm = false;
                        if (pObj->field_4_typeId == Types::eAbe_43)
                        {
                            if (pObj->field_FC_current_motion != eAbeMotions::Motion_0_Idle_423520 && pObj->field_FC_current_motion != eAbeMotions::Motion_19_CrouchIdle_4284C0 && pObj->field_FC_current_motion != eAbeMotions::Motion_66_LedgeHang_428D90 && pObj->field_FC_current_motion != eAbeMotions::Motion_62_LoadedSaveSpawn_45ADD0 && pObj->field_FC_current_motion != eAbeMotions::Motion_60_Dead_42C4C0 && !sDDCheat_FlyingEnabled_50771C)
                            {
                                alarm = true;
                            }
                        }
                        else if (FP_GetExponent(pObj->field_B4_velx) || FP_GetExponent(pObj->field_B8_vely))
                        {
                            alarm = true;
                        }

                        if (alarm)
                        {
                            field_160_bObjectInLaser = TRUE;

                            if (alarmInstanceCount_5076A8 == 0)
                            {
                                auto pAlarm = ao_new<Alarm>();
                                if (pAlarm)
                                {
                                    pAlarm->ctor_402570(
                                        field_F4_alarm_duration,
                                        field_F2_alarm_switch_id,
                                        0,
                                        Layer::eLayer_Above_FG1_39);
                                }

                                if (pObj == sActiveHero_507678)
                                {
                                    Mudokon_SFX_42A4D0(MudSounds::eOops_16, 0, 0, nullptr);
                                }
                            }
                        }
                    }
                }
            }


            switch (field_E8_state)
            {
                case States::eMoveRight_0:
                    if (field_108_pLaser->field_A8_xpos >= field_100_bottom_right_x)
                    {
                        field_E8_state = States::eWaitThenMoveLeft_1;
                        field_EC_timer = gnFrameCount_507670 + 15;
                        SFX_Play_43AD70(SoundEffect::MenuNavigation_61, 0);
                    }
                    else
                    {
                        field_108_pLaser->field_A8_xpos += field_15C_speed;
                    }
                    break;

                case States::eWaitThenMoveLeft_1:
                    if (static_cast<s32>(gnFrameCount_507670) > field_EC_timer)
                    {
                        field_E8_state = States::eMoveLeft_2;
                    }
                    break;

                case States::eMoveLeft_2:
                    if (field_108_pLaser->field_A8_xpos <= field_F8_top_left_x)
                    {
                        field_E8_state = States::eWaitThenMoveRight_3;
                        field_EC_timer = gnFrameCount_507670 + 15;
                        SFX_Play_43AD70(SoundEffect::MenuNavigation_61, 0);
                    }
                    else
                    {
                        field_108_pLaser->field_A8_xpos -= field_15C_speed;
                    }
                    break;

                case States::eWaitThenMoveRight_3:
                    if (static_cast<s32>(gnFrameCount_507670) > field_EC_timer)
                    {
                        field_E8_state = States::eMoveRight_0;
                    }
                    break;

                default:
                    return;
            }
        }
    }
}


void MotionDetector::VRender(PrimHeader** ppOt)
{
    VRender_438250(ppOt);
}

void MotionDetector::VRender_438250(PrimHeader** ppOt)
{
#ifdef TETHYS_SATURN
    // SATURN: 425.ao.1 -- file the three parts BEFORE the flare goes in, since
    // BaseAnimatedWithPhysicsGameObject::VRender just below reads field_C_layer.
    // The bar is a separate drawable pushed after this one, so it reads what is
    // written here in the same pass (and one frame late in the worst case, if
    // Remove_Item has since swapped it ahead of us -- invisible).
    //
    // THE HERO ON THE OTHER PLANE.  Every MotionDetector TLV in the game is full
    // scale (measured on E1PATH/E2PATH), so when Abe walks a half-scale path he
    // draws at eLayer_AbeMenu_Half_13 and a group pinned at 31 buries him -- the
    // 37x60 bar above all.  Dropping the group to the half MIRROR of its own
    // band puts him back in front at 13, which is the relation he already has at
    // full scale (32 over 31).  This INVERTS the depth AO wants -- there the
    // foreground laser is in front of him and ADDS to him -- so the 424.ao.2
    // tint below now fires across planes too: he is in front, and he is red.
    // Trading the depth for the colour was Romain's call, 2026-09-25.
    const bool heroOtherPlane = sActiveHero_507678
        && sActiveHero_507678->field_BC_sprite_scale != field_BC_sprite_scale;
    const s16 band = heroOtherPlane ? kLaserHalfBand : static_cast<s16>(0);
    s16 rank = 0;
    if (field_108_pLaser && gObjList_drawables_504618)
    {
        const s32 mySpan = Tethys_LaserSpan(field_A8_xpos, field_108_pLaser->field_A8_xpos);
        for (s32 i = 0; i < gObjList_drawables_504618->Size(); i++)
        {
            BaseGameObject* pObj = gObjList_drawables_504618->ItemAt(i);
            if (!pObj || pObj == this || pObj->field_4_typeId != Types::eMotionDetector_59)
            {
                continue;
            }
            MotionDetector* pOther = static_cast<MotionDetector*>(pObj);
            if (!pOther->field_108_pLaser)
            {
                continue;
            }
            const s32 span = Tethys_LaserSpan(pOther->field_A8_xpos, pOther->field_108_pLaser->field_A8_xpos);
            // strict, then a stable tie-break, so two equal spans never swap
            if (span < mySpan || (span == mySpan && pOther < this))
            {
                rank++;
            }
        }
        if (rank >= kLaserFanDepth)
        {
            rank = static_cast<s16>(kLaserFanDepth - 1);
        }
    }
    field_10_anim.field_C_layer = static_cast<Layer>(kLaserBarLayer - band);
    if (field_108_pLaser)
    {
        field_108_pLaser->field_10_anim.field_C_layer = static_cast<Layer>(kLaserBarLayer - band);
    }
    const Layer beamLayer = static_cast<Layer>(kLaserBeamLayer - band - rank);
#else
    const Layer beamLayer = field_10_anim.field_C_layer;
#endif
    field_A8_xpos += FP_FromInteger(11);
    BaseAnimatedWithPhysicsGameObject::VRender(ppOt);
    field_A8_xpos -= FP_FromInteger(11);

    if (!SwitchStates_Get(field_F0_disable_switch_id))
    {
        const s16 screen_top = FP_GetExponent(pScreenManager_4FF7C8->field_10_pCamPos->field_4_y - FP_FromInteger(pScreenManager_4FF7C8->field_16_ypos));

        const s16 screen_left = FP_GetExponent(pScreenManager_4FF7C8->field_10_pCamPos->field_0_x - FP_FromInteger(pScreenManager_4FF7C8->field_14_xpos));

        PSX_RECT bLaserRect = {};
        field_108_pLaser->VGetBoundingRect(&bLaserRect, 1);

        const s16 x0 = static_cast<s16>(PsxToPCX(FP_GetExponent(field_A8_xpos) - screen_left, 11));
        const s16 y0 = FP_GetExponent(field_AC_ypos) - screen_top;
        const s16 y1 = FP_GetExponent(field_108_pLaser->field_AC_ypos) - screen_top;
        const s16 y2 = y1 + bLaserRect.y - bLaserRect.h;
        const s16 x1 = static_cast<s16>(PsxToPCX(FP_GetExponent(field_108_pLaser->field_A8_xpos) - screen_left, 11));

        Poly_F3* pPrim = &field_10C_prims[gPsxDisplay_504C78.field_A_buffer_index];
        PolyF3_Init(pPrim);

        SetXY0(pPrim, x0, y0);
        SetXY1(pPrim, x1, y1);
        SetXY2(pPrim, x1, y2);

        SetRGB0(pPrim, 64, 0, 0);

        // Add triangle
        // SATURN: 425.ao.1 -- beamLayer, one band under the bar.  The tpage that
        // follows MUST stay in the same bucket right behind it: the bucket walk
        // is LIFO, so [tpage, triangle] adjacent is what guarantees sModalTPage
        // is this beam's when the triangle is drawn, fan or no fan.
        Poly_Set_SemiTrans_498A40(&pPrim->mBase.header, TRUE);
        OrderingTable_Add_498A80(OtLayer(ppOt, beamLayer), &pPrim->mBase.header);

        // Add tpage
        Init_SetTPage_495FB0(&field_13C_tPage[gPsxDisplay_504C78.field_A_buffer_index], 0, 0, PSX_getTPage_4965D0(TPageMode::e16Bit_2, field_160_bObjectInLaser != 0 ? TPageAbr::eBlend_1 : TPageAbr::eBlend_3, 0, 0)); // When detected transparency is off, gives the "solid red" triangle
        OrderingTable_Add_498A80(OtLayer(ppOt, beamLayer), &field_13C_tPage[gPsxDisplay_504C78.field_A_buffer_index].mBase);

#ifdef TETHYS_SATURN
        // SATURN: 424.ao.2 -- Abe in the bar is VUpdate_437E90's own detection
        // test on the same rect, and the beam's mode picks the amount.
        // 425.ao.1: the sprite_scale equality is GONE.  It kept the tint for the
        // detector's own plane, which was right while the group sat over Abe; it
        // is wrong now that a background-plane Abe is pulled in FRONT of the
        // group -- there the red IS the laser, and the rect test is in screen
        // space for both, so it already means "they overlap on screen".
        if (sActiveHero_507678)
        {
            PSX_RECT heroRect = {};
            sActiveHero_507678->VGetBoundingRect(&heroRect, 1);
            if (bLaserRect.x <= (heroRect.w - 8) && bLaserRect.w >= (heroRect.x + 8) && bLaserRect.h >= heroRect.y && bLaserRect.y <= heroRect.h)
            {
                const PSX_Point& pal = sActiveHero_507678->field_10_anim.field_8C_pal_vram_xy;
                Tethys_HeroLaserTint(static_cast<u16>(PSX_getClut_496840(pal.field_0_x, pal.field_2_y)),
                                     field_160_bObjectInLaser ? kLaserRedDetect : kLaserRedIdle);
            }
        }
#endif

        pScreenManager_4FF7C8->InvalidateRect_406E40(
            std::min(x0, std::min(x1, x1)),
            std::min(y0, std::min(y1, y2)),
            std::max(x0, std::max(x1, x1)),
            std::max(y0, std::max(y1, y2)),
            pScreenManager_4FF7C8->field_2E_idx);
    }
}

BaseGameObject* MotionDetectorLaser::VDestructor(s32 flags)
{
    dtor_417D10();
    if (flags & 1)
    {
        ao_delete_free_447540(this);
    }
    return this;
}

} // namespace AO
