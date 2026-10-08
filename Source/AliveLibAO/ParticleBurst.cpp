#include "stdafx_ao.h"
#include "Function.hpp"
#include "ParticleBurst.hpp"
#include "Math.hpp"
#include "stdlib.hpp"
#include "ResourceManager.hpp"
#include "Map.hpp"
#include "Game.hpp"
#include "ScreenManager.hpp"
#include "PsxDisplay.hpp"
#include "CameraSwapper.hpp"
#include "Events.hpp"
#include "Sfx.hpp"
#include "BaseAliveGameObject.hpp"
#include "Grid.hpp"

namespace AO {

struct ParticleBurst_Item final
{
    FP field_0_x;
    FP field_4_y;
    FP field_8_z;
    FP field_C_x_speed;
    FP field_10_y_speed;
    FP field_14_z_speed;
    AnimationUnknown field_18_anim;
};
ALIVE_ASSERT_SIZEOF(ParticleBurst_Item, 0x88);

static inline FP Random_Speed(FP scale)
{
    return FP_FromRaw((Math_NextRandom() - 128) << 13) * scale;
}

#ifdef TETHYS_SATURN
// SATURN 445.ao.2 -- A BURST IS SIZED AT BIRTH BY THE ROOM IN THE FRAME.
// 347.ao.1 capped every burst at 12 because the bound it must respect is the
// FRAME's 128 VDP1 commands, not the burst's. So a burst now asks the frame:
// the commands the last flush offered, minus the debris it drew (the rest of
// the scene), plus every item the live bursts already hold (as if all of them
// were on screen), plus a margin for what an explosion's own tick adds besides
// debris (its flash, the gibs, the blood). What is left is its size, never less
// than 347.ao.1's 12 -- so no screen gets fewer debris than before -- and never
// more than 40. A mine in a quiet corridor gets its 35 rocks and its sparks
// whole; a crowded screen gets 12 a burst, exactly as before. The size is fixed
// for the burst's life, so nothing pops in for a frame and out the next.
extern "C" u32 Tethys_gSubmit; // renderer_saturn.cxx: sprites offered to the last flush
static const s32 kTethysFrameCmds = 128;   // kMaxFrameSprites, SglAcceptCap
static const s32 kTethysDebrisMargin = 24; // [est] one explosion tick's other sprites
static const s32 kTethysDebrisFloor = 12;  // 347.ao.1's cap, now the minimum
static s32 sTethysLiveDebris = 0;          // items held by the live bursts
static u32 sTethysDrawnTick = 0xFFFFFFFFu;
static s32 sTethysDrawnCur = 0;            // debris laid down this tick
static s32 sTethysDrawnLast = 0;           // ... and on the tick before
static void TethysDebrisRoll()
{
    const u32 now = static_cast<u32>(gnFrameCount_507670);
    if (sTethysDrawnTick != now)
    {
        sTethysDrawnLast = (sTethysDrawnTick + 1u == now) ? sTethysDrawnCur : 0;
        sTethysDrawnCur = 0;
        sTethysDrawnTick = now;
    }
}
#endif

ParticleBurst* ParticleBurst::ctor_40D0F0(FP xpos, FP ypos, s16 particleCount, FP scale, BurstType type)
{
    ctor_417C10();
#ifdef TETHYS_SATURN
    // SATURN (bt829): CONFIRMED ROOT of the death-on-mine crash. The mine's
    // falling-rocks burst emits ~35 sprites; on top of Abe + the scene they
    // overflow SGL's sort/work area (the P2-proven Saturn hazard: a sprite past
    // the sort-table capacity trashes SGL's work area -> the vblank callback
    // stomp -> wild jsr -> silent SH-2 reset, "green/black, no fatal"). NOT the
    // heap (bt828's no-compaction best-effort did not stop it); it is the RENDER
    // (bt826 skip-render did). Empirically 3 is safe and 35 resets; cap every
    // burst to a small budget. Cosmetic only -- a few debris sprites instead of a
    // shower. Raise TETHYS_MAX_BURST_PARTICLES once a higher value is HW/Ymir-
    // verified stable (the true SGL sort-table headroom depends on scene sprites).
    //
    // 347.ao.1 -- 3 -> 12, AND THE CAP STOPS BEING A GUESS.  The note above
    // asked for exactly this: a higher value, once stable.  Two things changed
    // since bt829.  The overflow is no longer a silent reset -- SglAcceptCap
    // bounds the frame at the MEASURED 128-command ceiling and drops the
    // excess (bt1089), so the failure mode is now a missing debris sprite, not
    // a dead console.  And the tester has run explosions on hardware with the
    // stone build and reports them working.
    //
    // 12 is not a round number: it is the largest value the worst REAL case
    // survives without reaching that ceiling.  The bound is not one burst, it
    // is FallingItem.cpp:262/275/287, which can create THREE bursts of 25 in
    // the same block -- 3 x 12 = 36 debris on top of a scene measured at 49-82
    // commands = 118 at worst, still under 128.  At 16 the same case reaches
    // 130 and SglAcceptCap starts dropping, and what it drops might be Abe.
    // Heap: 136 B per particle (ParticleBurst_Item), so 1,632 B per burst and
    // 4,896 B for that triple, still on the best-effort allocation below that
    // never forces compaction.
    //
    // The cap STAYS.  RollingBall.cpp:263 asks for 150 = 20,400 B of resource
    // heap for one boulder.  The OG counts are 20 (bombs, Shrykull), 25
    // (falling items), 35 (the mine) and that 150, so at 12 every site except
    // the boulder gets most of its shower back.
    // 445.ao.2: no longer a constant -- see the block above Random_Speed. 40 is
    // the most any burst can get (every site's whole shower except the boulder:
    // 5,440 B of heap instead of 20,400) and 12 the least.
    #define TETHYS_MAX_BURST_PARTICLES 40
    {
        TethysDebrisRoll();
        const s32 scene = static_cast<s32>(Tethys_gSubmit) - sTethysDrawnLast;
        s32 room = kTethysFrameCmds - kTethysDebrisMargin - (scene > 0 ? scene : 0) - sTethysLiveDebris;
        room = (room > TETHYS_MAX_BURST_PARTICLES) ? TETHYS_MAX_BURST_PARTICLES : room;
        room = (room < kTethysDebrisFloor) ? kTethysDebrisFloor : room;
        if (particleCount > room) { particleCount = static_cast<s16>(room); }
    }
#endif
    SetVTable(this, 0x4BA480);
    field_4_typeId = Types::eParticleBurst_19;
    field_BC_sprite_scale = scale;

#ifdef TETHYS_SATURN
    // SATURN (bt828): best-effort (bReclaimOnFail=false) so this cosmetic gib array
    // NEVER forces Reclaim_Memory heap compaction on the walled R1P15 heap --
    // compaction moves non-locked blocks and dangles other objects' cached raw
    // derefs -> a later write stomps the SGL sync region -> silent SH-2 reset (the
    // death-on-mine crash: bt825/826 traced it here, workflow pinned the
    // compaction). If it doesn't fit in free space, ppRes is null and the else
    // branch marks the burst dead -- the rocks just don't spawn, no compaction.
    field_E4_ppRes = ResourceManager::Alloc_New_Resource_Impl(ResourceManager::ResourceType::Resource_3DGibs, 0, sizeof(ParticleBurst_Item) * particleCount, true, ResourceManager::BlockAllocMethod::eLastMatching, false);
    // 445.ao.2: a burst too big for the free space falls back to the old 12
    // (1,632 B) rather than to nothing, so a full heap is never worse than before.
    if (!field_E4_ppRes && particleCount > kTethysDebrisFloor)
    {
        particleCount = kTethysDebrisFloor;
        field_E4_ppRes = ResourceManager::Alloc_New_Resource_Impl(ResourceManager::ResourceType::Resource_3DGibs, 0, sizeof(ParticleBurst_Item) * particleCount, true, ResourceManager::BlockAllocMethod::eLastMatching, false);
    }
#else
    field_E4_ppRes = ResourceManager::Allocate_New_Locked_Resource_454F80(ResourceManager::ResourceType::Resource_3DGibs, 0, sizeof(ParticleBurst_Item) * particleCount);
#endif
    if (field_E4_ppRes)
    {
        field_E8_pRes = reinterpret_cast<ParticleBurst_Item*>(*field_E4_ppRes);
        for (s32 i = 0; i < particleCount; i++)
        {
            // Placement new each element
            new (&field_E8_pRes[i]) ParticleBurst_Item();
            SetVTable(&field_E8_pRes[i].field_18_anim, 0x4BA470);
        }

        field_F4_type = type;
        switch (type)
        {
            case BurstType::eFallingRocks_0:
            {
                const AnimRecord& rockRec = AO::AnimRec(AnimId::Rock_Gib);
                u8** ppRes = ResourceManager::GetLoadedResource_4554F0(ResourceManager::Resource_Animation, rockRec.mResourceId, 1, 0);
                Animation_Init_417FD0(rockRec.mFrameTableOffset, rockRec.mMaxW, rockRec.mMaxH, ppRes, 1);
                field_10_anim.field_4_flags.Clear(AnimFlags::eBit15_bSemiTrans);
                field_10_anim.field_4_flags.Set(AnimFlags::eBit16_bBlending);
                break;
            }

            case BurstType::eSticks_1:
            {
                const AnimRecord& sticksRec = AO::AnimRec(AnimId::Stick_Gib);
                u8** ppRes = ResourceManager::GetLoadedResource_4554F0(ResourceManager::Resource_Animation, sticksRec.mResourceId, 1, 0);
                Animation_Init_417FD0(sticksRec.mFrameTableOffset, sticksRec.mMaxW, sticksRec.mMaxH, ppRes, 1);
                scale = FP_FromDouble(0.4) * scale;
                field_10_anim.field_4_flags.Clear(AnimFlags::eBit15_bSemiTrans);
                field_10_anim.field_4_flags.Set(AnimFlags::eBit16_bBlending);
                break;
            }

            case BurstType::eBigPurpleSparks_2:
            {
                const AnimRecord& flareRec = AO::AnimRec(AnimId::DeathFlare_2);
                u8** ppRes = ResourceManager::GetLoadedResource_4554F0(ResourceManager::Resource_Animation, flareRec.mResourceId, 1, 0);
                Animation_Init_417FD0(flareRec.mFrameTableOffset, flareRec.mMaxW, flareRec.mMaxH, ppRes, 1);
                field_10_anim.field_4_flags.Set(AnimFlags::eBit15_bSemiTrans);
                field_10_anim.field_4_flags.Set(AnimFlags::eBit16_bBlending);
                field_10_anim.field_B_render_mode = TPageAbr::eBlend_1;
                break;
            }

            case BurstType::eBigRedSparks_3:
            {
                const AnimRecord& flareRec = AO::AnimRec(AnimId::DeathFlare_2);
                u8** ppRes = ResourceManager::GetLoadedResource_4554F0(ResourceManager::Resource_Animation, flareRec.mResourceId, 1, 0);
                Animation_Init_417FD0(flareRec.mFrameTableOffset, flareRec.mMaxW, flareRec.mMaxH, ppRes, 1);

                field_10_anim.field_B_render_mode = TPageAbr::eBlend_1;
                field_10_anim.field_4_flags.Set(AnimFlags::eBit15_bSemiTrans);
                field_10_anim.field_4_flags.Clear(AnimFlags::eBit16_bBlending);

                field_10_anim.field_8_r = 254;
                field_10_anim.field_9_g = 148;
                field_10_anim.field_A_b = 18;
                break;
            }

            case BurstType::eMeat_4:
            {
                const AnimRecord& meatRec = AO::AnimRec(AnimId::Meat_Gib);
                u8** ppRes = ResourceManager::GetLoadedResource_4554F0(ResourceManager::Resource_Animation, meatRec.mResourceId, 1, 0);
                Animation_Init_417FD0(meatRec.mFrameTableOffset, meatRec.mMaxW, meatRec.mMaxH, ppRes, 1);
                field_10_anim.field_4_flags.Clear(AnimFlags::eBit15_bSemiTrans);
                field_10_anim.field_4_flags.Set(AnimFlags::eBit16_bBlending);
                break;
            }

            default:
                break;
        }

        if (field_6_flags.Get(BaseGameObject::eListAddFailed_Bit1))
        {
            field_6_flags.Set(BaseGameObject::eDead_Bit3);
        }
        else
        {
            if (field_BC_sprite_scale == FP_FromInteger(1))
            {
                field_C6_scale = 1;
                field_10_anim.field_C_layer = Layer::eLayer_Above_FG1_39;
            }
            else
            {
                field_C6_scale = 0;
                field_10_anim.field_C_layer = Layer::eLayer_Above_FG1_Half_20;
            }

            field_EC_count = particleCount;
#ifdef TETHYS_SATURN
            sTethysLiveDebris += particleCount; // 445.ao.2: handed back by dtor_40D5A0
#endif
            field_F0_timer = gnFrameCount_507670 + 91;
            field_A8_xpos = xpos;
            field_AC_ypos = ypos;

            for (s32 i = 0; i < particleCount; i++)
            {
                field_E8_pRes[i].field_18_anim.field_68_anim_ptr = &field_10_anim;
                field_E8_pRes[i].field_18_anim.field_C_layer = field_10_anim.field_C_layer;
                field_E8_pRes[i].field_18_anim.field_6C_scale = FP_FromDouble(0.95) * field_BC_sprite_scale;

                field_E8_pRes[i].field_18_anim.field_4_flags.Set(AnimFlags::eBit3_Render);

                field_E8_pRes[i].field_18_anim.field_4_flags.Set(AnimFlags::eBit15_bSemiTrans, field_10_anim.field_4_flags.Get(AnimFlags::eBit15_bSemiTrans));

                field_E8_pRes[i].field_18_anim.field_4_flags.Set(AnimFlags::eBit16_bBlending, field_10_anim.field_4_flags.Get(AnimFlags::eBit16_bBlending));

                if (type == BurstType::eBigPurpleSparks_2)
                {
                    if (i % 2)
                    {
                        field_E8_pRes[i].field_18_anim.field_4_flags.Set(AnimFlags::eBit16_bBlending);
                    }
                }

                field_E8_pRes[i].field_18_anim.field_8_r = field_10_anim.field_8_r;
                field_E8_pRes[i].field_18_anim.field_9_g = field_10_anim.field_9_g;
                field_E8_pRes[i].field_18_anim.field_A_b = field_10_anim.field_A_b;

                field_E8_pRes[i].field_0_x = xpos;
                field_E8_pRes[i].field_4_y = ypos;
                field_E8_pRes[i].field_8_z = FP_FromInteger(0);

                field_E8_pRes[i].field_C_x_speed = Random_Speed(scale);
                field_E8_pRes[i].field_10_y_speed = -Random_Speed(scale);
                field_E8_pRes[i].field_14_z_speed = -FP_Abs(Random_Speed(scale));
            }

            if (gMap_507BA8.field_0_current_level == LevelIds::eStockYards_5 || gMap_507BA8.field_0_current_level == LevelIds::eStockYardsReturn_6)
            {
                field_C4_b = 60;
                field_C2_g = 60;
                field_C0_r = 60;
            }
        }
    }
    else
    {
        field_6_flags.Set(BaseGameObject::eDead_Bit3);
    }
    return this;
}

BaseGameObject* ParticleBurst::dtor_40D5A0()
{
    SetVTable(this, 0x4BA480);
    if (field_E4_ppRes)
    {
        ResourceManager::FreeResource_455550(field_E4_ppRes);
#ifdef TETHYS_SATURN
        // 445.ao.2: counted where field_EC_count was set, i.e. only by a burst
        // that both got its array and joined the drawable list.
        if (!field_6_flags.Get(BaseGameObject::eListAddFailed_Bit1))
        {
            sTethysLiveDebris -= field_EC_count;
        }
#endif
    }
    return dtor_417D10();
}

BaseGameObject* ParticleBurst::VDestructor(s32 flags)
{
    return Vdtor_40DA40(flags);
}

ParticleBurst* ParticleBurst::Vdtor_40DA40(s32 flags)
{
    dtor_40D5A0();
    if (flags & 1)
    {
        ao_delete_free_447540(this);
    }
    return this;
}

void ParticleBurst::VUpdate()
{
    VUpdate_40D600();
}

void ParticleBurst::VUpdate_40D600()
{
    for (s32 i = 0; i < field_EC_count; i++)
    {
        ParticleBurst_Item* pItem = &field_E8_pRes[i];

        pItem->field_0_x += pItem->field_C_x_speed;
        pItem->field_4_y += pItem->field_10_y_speed;
        pItem->field_8_z += pItem->field_14_z_speed;

        pItem->field_10_y_speed += FP_FromDouble(0.25);

        u16 result = 0;
        pItem->field_0_x = CamX_VoidSkipper_418590(pItem->field_0_x, pItem->field_C_x_speed, 16, &result);
        pItem->field_4_y = CamY_VoidSkipper_418690(pItem->field_4_y, pItem->field_10_y_speed, 16, &result);

        if (pItem->field_8_z + FP_FromInteger(300) < FP_FromInteger(15))
        {
            pItem->field_14_z_speed = -pItem->field_14_z_speed;
            pItem->field_8_z += pItem->field_14_z_speed;

            if (field_F4_type == BurstType::eMeat_4)
            {
                if (gMap_507BA8.Is_Point_In_Current_Camera_4449C0(
                        gMap_507BA8.field_0_current_level,
                        gMap_507BA8.field_2_current_path,
                        pItem->field_0_x,
                        pItem->field_4_y,
                        0))
                {
                    SFX_Play_43AE60(SoundEffect::KillEffect_78, 50, Math_RandomRange_450F20(-900, -300));
                }
            }
            else
            {
                // TODO: Never used by OG ??
                // Math_RandomRange_450F20(-64, 46);

                const s16 volume = static_cast<s16>(Math_RandomRange_450F20(-10, 10) + ((field_F0_timer - gnFrameCount_507670) / 91) + 25);

                const u8 next_rand = Math_NextRandom();
                if (next_rand < 43)
                {
                    SFX_Play_43AED0(SoundEffect::ParticleBurst_32, volume, CameraPos::eCamLeft_3);
                }
                else if (next_rand >= 85)
                {
                    SFX_Play_43AED0(SoundEffect::ParticleBurst_32, volume, CameraPos::eCamRight_4);
                }
                else
                {
                    SFX_Play_43AED0(SoundEffect::ParticleBurst_32, volume, CameraPos::eCamCurrent_0);
                }
            }
        }
    }

    if (static_cast<s32>(gnFrameCount_507670) > field_F0_timer)
    {
        field_6_flags.Set(BaseGameObject::eDead_Bit3);
    }

    if (Event_Get_417250(kEventDeathReset_4))
    {
        field_6_flags.Set(BaseGameObject::eDead_Bit3);
    }
}

void ParticleBurst::VRender(PrimHeader** ppOt)
{
    VRender_40D7F0(ppOt);
}

void ParticleBurst::VRender_40D7F0(PrimHeader** ppOt)
{
    if (sNumCamSwappers_507668 != 0)
    {
        return;
    }

    field_10_anim.field_14_scale = field_BC_sprite_scale;

    const FP_Point* pCamPos = pScreenManager_4FF7C8->field_10_pCamPos;
    const FP screen_left = pCamPos->field_0_x - FP_FromInteger(pScreenManager_4FF7C8->field_14_xpos);
    const FP screen_right = pCamPos->field_0_x + FP_FromInteger(pScreenManager_4FF7C8->field_14_xpos);

    const FP screen_top = pCamPos->field_4_y + FP_FromInteger(pScreenManager_4FF7C8->field_16_ypos);
    const FP screen_bottom = pCamPos->field_4_y - FP_FromInteger(pScreenManager_4FF7C8->field_16_ypos);

    bool bFirst = true;
    for (s32 i = 0; i < field_EC_count; i++)
    {
        ParticleBurst_Item* pItem = &field_E8_pRes[i];
        if (pItem->field_0_x >= screen_left && pItem->field_0_x <= screen_right)
        {
            if (pItem->field_4_y >= screen_bottom && pItem->field_4_y <= screen_top)
            {
#ifdef TETHYS_SATURN
                TethysDebrisRoll(); // 445.ao.2: the next burst's measure of the scene
                sTethysDrawnCur++;
#endif
                PSX_RECT rect = {};
                if (bFirst)
                {
                    field_10_anim.field_14_scale = FP_FromInteger(100) / (pItem->field_8_z + FP_FromInteger(300));
                    field_10_anim.VRender_403AE0(
                        FP_GetExponent(PsxToPCX(pItem->field_0_x - screen_left, FP_FromInteger(11))),
                        FP_GetExponent(pItem->field_4_y - screen_bottom),
                        ppOt,
                        0,
                        0);
                    field_10_anim.Get_Frame_Rect_402B50(&rect);
                    bFirst = false;
                }
                else
                {
                    pItem->field_18_anim.field_6C_scale = FP_FromInteger(100) / (pItem->field_8_z + FP_FromInteger(300));
                    pItem->field_18_anim.VRender2(
                        FP_GetExponent(PsxToPCX(pItem->field_0_x - screen_left, FP_FromInteger(11))),
                        FP_GetExponent(pItem->field_4_y - screen_bottom),
                        ppOt);
                    pItem->field_18_anim.GetRenderedSize_404220(&rect);
                }

                pScreenManager_4FF7C8->InvalidateRect_406E40(
                    rect.x,
                    rect.y,
                    rect.w,
                    rect.h,
                    pScreenManager_4FF7C8->field_2E_idx);
            }
        }
    }
}

} // namespace AO
