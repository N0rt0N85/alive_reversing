#include "stdafx_ao.h"
#include "Function.hpp"
#include "ZBall.hpp"
#include "ResourceManager.hpp"
#include "stdlib.hpp"
#include "Events.hpp"
#include "Sfx.hpp"
#include "BaseAliveGameObject.hpp"

namespace AO {

ALIVE_VAR(1, 0x9F1DCC, ZBall*, gCenter_ZBall_9F1DCC, nullptr);
ALIVE_VAR(1, 0x9F1DD0, ZBall*, gOutZBall_9F1DD0, nullptr);

EXPORT s16* CC Animation_OnFrame_ZBallSmacker_41FB00(void* pObj, s16* pData)
{
    auto pZBall = static_cast<ZBall*>(pObj);
    for (s32 i = 0; i < gBaseGameObject_list_9F2DF0->Size(); i++)
    {
        BaseGameObject* pBase = gBaseGameObject_list_9F2DF0->ItemAt(i);
        if (!pBase)
        {
            break;
        }

        if (pBase->field_6_flags.Get(BaseGameObject::eIsBaseAliveGameObject_Bit6))
        {
            // If the object is within the ZBall rect then smack it
            auto pAliveObj = static_cast<BaseAliveGameObject*>(pBase);

            PSX_RECT bRect = {};
            pAliveObj->VGetBoundingRect(&bRect, 1);

            if (bRect.x <= (FP_GetExponent(pZBall->field_A8_xpos) + pData[2]) && bRect.w >= (FP_GetExponent(pZBall->field_A8_xpos) + pData[0]) && bRect.h >= (FP_GetExponent(pZBall->field_AC_ypos) + pData[1]) && bRect.y <= (FP_GetExponent(pZBall->field_AC_ypos) + pData[3]))
            {
                pAliveObj->VTakeDamage(pZBall);
            }
        }
    }

    return pData + 4; // A rect I guess ?
}

ZBall* ZBall::ctor_478590(Path_ZBall* pTlv, s32 tlvInfo)
{
    ctor_417C10();
    SetVTable(this, 0x4BCD78);

    field_4_typeId = Types::eZBall_92;

    field_C4_b = 128;
    field_C2_g = 128;
    field_C0_r = 128;

    switch (pTlv->field_1C_speed)
    {
        case Path_ZBall::Speed::eNormal_0:
        {
            const AnimRecord& rec1 = AO::AnimRec(AnimId::Swinging_Ball_Normal);
            u8** ppRes1 = ResourceManager::GetLoadedResource_4554F0(ResourceManager::Resource_Animation, rec1.mResourceId, 1, 0);
            Animation_Init_417FD0(rec1.mFrameTableOffset, rec1.mMaxW, rec1.mMaxH, ppRes1, 1);
            break;
        }

        case Path_ZBall::Speed::eFast_1:
        {
            const AnimRecord& rec2 = AO::AnimRec(AnimId::Swinging_Ball_Fast);
            u8** ppRes2 = ResourceManager::GetLoadedResource_4554F0(ResourceManager::Resource_Animation, rec2.mResourceId, 1, 0);
            Animation_Init_417FD0(rec2.mFrameTableOffset, rec2.mMaxW, rec2.mMaxH, ppRes2, 1);
            break;
        }

        case Path_ZBall::Speed::eSlow_2:
        {
            const AnimRecord& rec3 = AO::AnimRec(AnimId::Swinging_Ball_Slow);
            u8** ppRes3 = ResourceManager::GetLoadedResource_4554F0(ResourceManager::Resource_Animation, rec3.mResourceId, 1, 0);
            Animation_Init_417FD0(rec3.mFrameTableOffset, rec3.mMaxW, rec3.mMaxH, ppRes3, 1);
            break;
        }

    }

    field_A8_xpos = FP_FromInteger(pTlv->field_10_top_left.field_0_x);
    field_AC_ypos = FP_FromInteger(pTlv->field_10_top_left.field_2_y);

    if (gMap_507BA8.field_0_current_level == LevelIds::eForestTemple_4)
    {
        switch (pTlv->field_18_start_pos)
        {
            case Path_ZBall::StartPos::eCenter_0:
                field_10_anim.SetFrame_402AC0(6u);
                gCenter_ZBall_9F1DCC = this;
                field_EA_sound_pitch = -800;
                break;

            case Path_ZBall::StartPos::eOut_1:
                field_10_anim.SetFrame_402AC0(0);
                gOutZBall_9F1DD0 = this;
                field_EA_sound_pitch = -400;
                break;

            case Path_ZBall::StartPos::eIn_2:
                field_10_anim.SetFrame_402AC0(13u);
                field_EA_sound_pitch = 0;
                break;
        }

        field_10_anim.vDecode();

#ifdef TETHYS_SATURN
        // SATURN 427.ao.4 -- THE THREE BALLS STOP DECODING ON THE SAME TICK.
        //
        // WHAT THIS COSTS TODAY, measured on the delivered F2.LVL rather than
        // estimated. Swinging_Ball_Normal (F2ZBALL.BAN chunk 2001, table 72172)
        // is fps=2 -- a DELAY, so one new cel every two ticks -- 27 frames, and
        // its cels run 7,248 to 13,104 B. Both screens that carry ZBalls carry
        // THREE of them (F2 path 3 cell 4 and F2 path 7 cell 1, all speed
        // eNormal_0, start_pos Out/In/Center), and the three land on frames 0,
        // 13 and 6 through the switch above.
        //   Different FRAMES, same PARITY: SetFrame_402AC0 arms the counter at 1
        // and the vDecode above re-arms all three to frame_delay = 2, so
        // AnimateAll decrements them in lockstep and all three decompress on the
        // SAME tick, then nothing on the next. The worst such tick sums
        // f[25]+f[19]+f[6] = 34,384 B = 21.83 ms of decompression alone, against
        // a 33.4 ms budget, for as long as the player stands on the screen. This
        // is not a burst like an explosion: it is the steady state of two shipped
        // screens, and it is the largest single decode tick found anywhere in the
        // game, worse than a mine.
        //
        // WHY ONE BALL AND WHY THE CENTRE ONE. At fps=2 there are exactly two
        // parities, so three balls cannot spread further than 2+1 and the choice
        // is which one moves. Enumerated over all 27 frame sizes and both phases:
        // moving the CENTRE ball gives 21,496 B = 13.64 ms, moving Out or In
        // gives 25,992 or 26,208 B. So -8.19 ms, and the arithmetic is exhaustive
        // rather than sampled.
        //
        // WHAT IT DOES NOT CHANGE. Every ball still shows the same frames in the
        // same order at the same rate, so the image is pixel-identical; the MEAN
        // is untouched (9.78 ms/tick either way) because the same bytes are
        // decoded, just not all on one tick. The centre ball's cycle shifts by
        // ONE tick, which moves its ZBall/SackWobble SFX and its smacker rect
        // (kZBall_Anim_Frame_Fns_4CEBF8) by 16.7 ms. Nothing in AO compares two
        // balls' frame numbers to each other, so no behaviour keys on their being
        // in phase -- and being in phase is not something AO arranged, it is a
        // side effect of all three constructing on the same tick.
        if (pTlv->field_18_start_pos == Path_ZBall::StartPos::eCenter_0)
        {
            field_10_anim.field_E_frame_change_counter = 1;
        }
#endif
    }

    if (pTlv->field_1A_scale != Scale_short::eFull_0)
    {
        field_BC_sprite_scale = FP_FromDouble(0.5);
        field_C6_scale = 0;
    }

    field_E4_tlvInfo = tlvInfo;
    field_10_anim.field_1C_fn_ptr_array = kZBall_Anim_Frame_Fns_4CEBF8;

    return this;
}

BaseGameObject* ZBall::VDestructor(s32 flags)
{
    return Vdtor_478860(flags);
}

void ZBall::VUpdate()
{
    VUpdate_478720();
}

ZBall* ZBall::Vdtor_478860(s32 flags)
{
    dtor_478710();
    if (flags & 1)
    {
        ao_delete_free_447540(this);
    }
    return this;
}

BaseGameObject* ZBall::dtor_478710()
{
    SetVTable(this, 0x4BCD78);
    return dtor_417D10();
}

void ZBall::VUpdate_478720()
{
    if (Event_Get_417250(kEventDeathReset_4))
    {
        field_6_flags.Set(Options::eDead_Bit3);
    }

    if (gCenter_ZBall_9F1DCC == this || gOutZBall_9F1DD0 == this)
    {
        if (field_10_anim.field_92_current_frame == 0 || field_10_anim.field_92_current_frame == 13)
        {
            SFX_Play_43AE60(SoundEffect::ZBall_62, 50, field_EA_sound_pitch, nullptr);
        }
    }

    if (gCenter_ZBall_9F1DCC == this)
    {
        if (field_10_anim.field_92_current_frame == 3 || field_10_anim.field_92_current_frame == 16)
        {
            SFX_Play_43AE60(SoundEffect::SackWobble_34, 40, field_EA_sound_pitch - 2400, nullptr);
        }
    }

    if (field_10_anim.field_92_current_frame <= 6 || field_10_anim.field_92_current_frame >= 19)
    {
        if (field_BC_sprite_scale == FP_FromInteger(1))
        {
            field_10_anim.field_C_layer = Layer::eLayer_Foreground_36;
        }
        else
        {
            field_10_anim.field_C_layer = Layer::eLayer_Foreground_Half_17;
        }
    }
    else if (field_BC_sprite_scale == FP_FromInteger(1))
    {
        field_10_anim.field_C_layer = Layer::eLayer_BeforeWell_22;
    }
    else
    {
        field_10_anim.field_C_layer = Layer::eLayer_BeforeWell_Half_3;
    }

    // Pointless because never seems to be read
    field_E8_bFrameAbove12 = field_10_anim.field_92_current_frame >= 13;

    if (!gMap_507BA8.Is_Point_In_Current_Camera_4449C0(
            field_B2_lvl_number,
            field_B0_path_number,
            field_A8_xpos,
            field_AC_ypos,
            0))
    {
        field_6_flags.Set(Options::eDead_Bit3);
        if (field_E4_tlvInfo != -1)
        {
            gMap_507BA8.TLV_Reset_446870(field_E4_tlvInfo, -1, 0, 0);
        }
    }
}

} // namespace AO
