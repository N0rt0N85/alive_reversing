#include "stdafx_ao.h"
#include "Function.hpp"
#include "ZapLine.hpp"
#include "stdlib.hpp"
#include "ResourceManager.hpp"
#include "Map.hpp"
#include "PsxDisplay.hpp"
#include "ScreenManager.hpp"
#include "Game.hpp"
#include "Math.hpp"

void ZapLine_ForceLink()
{ }

namespace AO {

BaseGameObject* ZapLine::VDestructor(s32 flags)
{
    return Vdtor_479B20(flags);
}

BaseGameObject* ZapLine::dtor_478E90()
{
    SetVTable(this, 0x4BCDE8);
    ResourceManager::FreeResource_455550(field_E8_ppRes);
    ao_delete_free_450770(field_128_sprite_positions);
    ao_delete_free_450770(field_12C_zap_points);
    ao_delete_free_450770(field_130_sprite_segment_positions);
    return dtor_417D10();
}

ZapLine* ZapLine::ctor_4789A0(FP x1, FP y1, FP x2, FP y2, s16 aliveTime, ZapLineType type, Layer layer)
{
    ctor_417C10();
    SetVTable(this, 0x4BCDE8);

    field_4_typeId = Types::eZapLine_94;
    field_11A_type = type;

    AnimId animId = AnimId::None;
    if (field_11A_type == ZapLineType::eThin_1)
    {
#ifdef TETHYS_SATURN
        // SATURN (383.ao.1): 12 x 20 = 240 sprites, and BOTH budgets refuse it.
        // See the eThick_0 arm below for the full reasoning -- this is the same
        // change on the Shrykull bolt (Shrykull.cpp:214, the only eThin_1 site).
        // 12 x 7 = 84 keeps the shape (the segment count, which is what carries
        // the jitter, is untouched) and only thins the dots along it.
        field_120_number_of_pieces_per_segment = 7;
#else
        field_120_number_of_pieces_per_segment = 20;
#endif
        field_11E_number_of_segments = 12;
        animId = AnimId::Zap_Line_Blue;
        field_11C_tPageAbr = TPageAbr::eBlend_3;
    }
    else if (field_11A_type == ZapLineType::eThick_0)
    {
#ifdef TETHYS_SATURN
        // SATURN (383.ao.1): THE BEAM COULD NEVER BE DRAWN, AND ASKING FOR IT
        // COULD KILL THE RUN.  28 x 10 = 280 sprites buys two things it cannot
        // have: 280 * sizeof(ZapLineSprites) = 13,440 B of LOCKED resource heap,
        // asked for at FIRE time on the memory-walled R1P15 heap, and 280
        // Prim_Sprt pushed into the ordering table EVERY frame against a
        // per-frame Sprt allowance of 96 (kBloodCapPacked, mode 0, shared with
        // Blood and HintFly) and a measured VDP1 ceiling of 128 commands.  So
        // even when the allocation succeeded the tail of the bolt was silently
        // dropped -- which is the tester's second sentence, "le robot n'arrive
        // pas a tirer".
        //   28 x 3 = 84 fits under both: 4,032 B, and 84 Sprt with twelve to
        // spare under the allowance.  The SEGMENT count is untouched, so the
        // bolt keeps exactly its authored shape -- only the number of dots laid
        // along each segment drops, and the cel is 9x9 (SPLINE.BAN, AO column of
        // AnimId::Zap_Line_Red) over a beam of ~150-250 px, i.e. still ~4x
        // overlap between neighbours.  CalculateZapPoints scales with it by
        // construction: delta = 1 / pieces_per_segment (:347).
        field_120_number_of_pieces_per_segment = 3;
#else
        field_120_number_of_pieces_per_segment = 10;
#endif
        field_11E_number_of_segments = 28;
        animId = AnimId::Zap_Line_Red;
        field_11C_tPageAbr = TPageAbr::eBlend_1;
    }

    const AnimRecord& rec = AO::AnimRec(animId);
    u8** ppRes = ResourceManager::GetLoadedResource_4554F0(ResourceManager::Resource_Animation, rec.mResourceId, 1, 0);
    Animation_Init_417FD0(rec.mFrameTableOffset, rec.mMaxW, rec.mMaxH, ppRes, 1);

    field_10_anim.field_4_flags.Clear(AnimFlags::eBit15_bSemiTrans);
    field_10_anim.field_C_layer = layer;
    field_122_number_of_sprites = field_11E_number_of_segments * field_120_number_of_pieces_per_segment;

#ifdef TETHYS_SATURN
    // SATURN (383.ao.1): A COSMETIC BEAM MUST NOT BE ABLE TO KILL THE RUN.
    //
    // This is the "RES NULL" the tester photographed in a Rupture Farms room
    // with a BoomMachine and a chant-triggered SecurityClaw.  The chain is
    // exact: Abe chants -> kEventAbeOhm_8 -> SecurityClaw::VUpdate_418DE0 ->
    // ao_new<ZapLine> + this ctor -> Allocate_New_Locked_Resource_454F80
    // (ResourceManager.cpp:2604) -> Alloc_New_Resource_Impl passes
    // bReclaimOnFail through as bFatalOnFail (:2518-2522) -> the miss lands on
    // ResourceManager.cpp:2586-2593, Tethys_ResNullReport then
    // Tethys_Fatal("RES NULL").  A LOCKED block cannot be moved by
    // Reclaim_Memory (:3084 skips locked blocks), so every locked resource pins
    // its fragment and a big contiguous ask on the walled R1P15 heap is exactly
    // the one that misses.
    //   The beam is pure decoration: the damage is dealt by the CALLER, one
    // statement later and independently (SecurityClaw.cpp:425 /
    // SecurityOrb.cpp:218, sActiveHero->VTakeDamage(this)), and both callers
    // discard the pointer as soon as the ctor returns.  So refusing softly
    // costs the bolt and nothing else.
    //   Same shape as Animation.cpp:466 and :1016 -- keep the compaction retry
    // (it is worth trying), drop the fatal.  Marking the object dead before
    // returning is what makes the null safe: Game.cpp:587 and :631 skip dead
    // objects in the update walks, :801 skips them in the drawable walk, and
    // :644 destroys them once the refcount clears -- so nothing ever reads the
    // null buffers, and the dtor is null-tolerant on all four
    // (FreeResource_455550 guards, ao_delete_free_450770 guards).
    field_E8_ppRes = ResourceManager::Alloc_New_Resource_ImplEx(
        ResourceManager::Resource_Spline, 0,
        sizeof(ZapLineSprites) * field_122_number_of_sprites,
        true /*locked*/, ResourceManager::BlockAllocMethod::eLastMatching,
        true /*reclaim*/, false /*never fatal -- see above*/);
    if (!field_E8_ppRes)
    {
        field_124_pSprts = nullptr;
        field_128_sprite_positions = nullptr;
        field_12C_zap_points = nullptr;
        field_130_sprite_segment_positions = nullptr;
        field_E4_state = ZapLineState::eInit_0;
        field_6_flags.Set(BaseGameObject::eDead_Bit3);
        return this;
    }
#else
    field_E8_ppRes = ResourceManager::Allocate_New_Locked_Resource_454F80(ResourceManager::Resource_Spline, 0, sizeof(ZapLineSprites) * field_122_number_of_sprites);
#endif
    field_124_pSprts = reinterpret_cast<ZapLineSprites*>(*field_E8_ppRes);

    field_128_sprite_positions = reinterpret_cast<PSX_Point*>(alloc_450740(sizeof(PSX_Point) * field_122_number_of_sprites));
    field_12C_zap_points = reinterpret_cast<ZapPoint*>(alloc_450740(sizeof(ZapPoint) * field_120_number_of_pieces_per_segment));
    field_130_sprite_segment_positions = reinterpret_cast<FP_Point*>(alloc_450740(sizeof(FP_Point) * field_11E_number_of_segments));

    field_118_max_alive_time = aliveTime;

    field_A8_xpos = x1;
    field_AC_ypos = y1;

    field_E4_state = ZapLineState::eInit_0;
    field_116_alive_timer = 0;

    if (field_10_anim.field_4_flags.Get(AnimFlags::eBit13_Is8Bit))
    {
        field_114_tPageMode = TPageMode::e8Bit_1;
    }
    else if (field_10_anim.field_4_flags.Get(AnimFlags::eBit14_Is16Bit))
    {
        field_114_tPageMode = TPageMode::e16Bit_2;
    }
    else
    {
        field_114_tPageMode = TPageMode::e4Bit_0;
    }

    u8 u0 = field_10_anim.field_84_vram_rect.x & 0x3F;
    if (field_114_tPageMode == TPageMode::e8Bit_1)
    {
        u0 = 2 * u0;
    }
    else if (field_114_tPageMode == TPageMode::e4Bit_0)
    {
        u0 = 4 * u0;
    }

    auto pFrameHeader = reinterpret_cast<FrameHeader*>(&(*field_10_anim.field_20_ppBlock)[field_10_anim.Get_FrameHeader_403A00(-1)->field_0_frame_header_offset]);

    const u8 frameW = pFrameHeader->field_4_width;
    const u8 frameH = pFrameHeader->field_5_height;

    for (s32 i = 0; i < 2; i++)
    {
        for (s32 j = 0; j < field_11E_number_of_segments; j++)
        {
            for (s32 k = 0; k < field_120_number_of_pieces_per_segment; k++)
            {
                Prim_Sprt* pSprt = &field_124_pSprts[(j * field_120_number_of_pieces_per_segment) + k].field_0_sprts[i];
                Sprt_Init(pSprt);

                Poly_Set_SemiTrans_498A40(&pSprt->mBase.header, 1);
                Poly_Set_Blending_498A00(&pSprt->mBase.header, 1);
                SetClut(pSprt, static_cast<s16>(PSX_getClut_496840(
                                   field_10_anim.field_8C_pal_vram_xy.field_0_x,
                                   field_10_anim.field_8C_pal_vram_xy.field_2_y)));

                SetUV0(pSprt, u0, field_10_anim.field_84_vram_rect.y & 0xFF);
                pSprt->field_14_w = frameW - 1;
                pSprt->field_16_h = frameH - 1;
            }
        }
    }

    CalculateSourceAndDestinationPositions_478CF0(x1, y1, x2, y2);

    return this;
}

ZapLine* ZapLine::Vdtor_479B20(s32 flags)
{
    dtor_478E90();
    if (flags & 1)
    {
        ao_delete_free_447540(this);
    }
    return this;
}

void ZapLine::CalculateSourceAndDestinationPositions_478CF0(FP xPosSource, FP yPosSource, FP xPosDest, FP yPosDest)
{
    field_10C_x_position_source = FP_GetExponent(xPosSource - (pScreenManager_4FF7C8->field_10_pCamPos->field_0_x - FP_FromInteger(pScreenManager_4FF7C8->field_14_xpos)));
    field_10E_y_position_source = FP_GetExponent(yPosSource - (pScreenManager_4FF7C8->field_10_pCamPos->field_4_y - FP_FromInteger(pScreenManager_4FF7C8->field_16_ypos)));
    field_110_x_position_destination = FP_GetExponent(xPosDest - (pScreenManager_4FF7C8->field_10_pCamPos->field_0_x - FP_FromInteger(pScreenManager_4FF7C8->field_14_xpos)));
    field_112_y_position_destination = FP_GetExponent(yPosDest - (pScreenManager_4FF7C8->field_10_pCamPos->field_4_y - FP_FromInteger(pScreenManager_4FF7C8->field_16_ypos)));

    field_10C_x_position_source = PsxToPCX(field_10C_x_position_source, 11);
    field_110_x_position_destination = PsxToPCX(field_110_x_position_destination, 11);

    s16 xOff = 0;
    s16 yOff = 0;
    field_10_anim.Get_Frame_Offset_403EE0(&xOff, &yOff);

    field_10C_x_position_source = FP_GetExponent(FP_FromInteger(xOff) + FP_FromInteger(field_10C_x_position_source));
    field_10E_y_position_source = FP_GetExponent(FP_FromInteger(yOff) + FP_FromInteger(field_10E_y_position_source));
    field_110_x_position_destination = FP_GetExponent(FP_FromInteger(xOff) + FP_FromInteger(field_110_x_position_destination));
    field_112_y_position_destination = FP_GetExponent(FP_FromInteger(yOff) + FP_FromInteger(field_112_y_position_destination));
}

void ZapLine::VScreenChanged()
{
    VScreenChanged_479B00();
}

void ZapLine::VScreenChanged_479B00()
{
    if (gMap_507BA8.field_0_current_level != gMap_507BA8.field_A_level || gMap_507BA8.field_2_current_path != gMap_507BA8.field_C_path)
    {
        field_6_flags.Set(BaseGameObject::eDead_Bit3);
    }
}

void ZapLine::VRender(PrimHeader** ppOt)
{
    VRender_479840(ppOt);
}

void ZapLine::VRender_479840(PrimHeader** ppOt)
{
    if (gMap_507BA8.Is_Point_In_Current_Camera_4449C0(
            field_B2_lvl_number,
            field_B0_path_number,
            field_A8_xpos,
            field_AC_ypos,
            0)
        && field_E4_state > ZapLineState::eInitSpriteVertices_2)
    {
        const auto bufferIdx = gPsxDisplay_504C78.field_A_buffer_index;

        for (s32 i = 0; i < field_11E_number_of_segments; i++)
        {
            for (s32 j = 0; j < field_120_number_of_pieces_per_segment; j++)
            {
                Prim_Sprt* pSprt = &field_124_pSprts->field_0_sprts[j + (i * field_120_number_of_pieces_per_segment)];
                OrderingTable_Add_498A80(OtLayer(ppOt, field_10_anim.field_C_layer), &pSprt[bufferIdx].mBase.header);
            }
        }

        const s32 calcTPage = PSX_getTPage_4965D0(
            field_114_tPageMode,
            field_11C_tPageAbr,
            field_10_anim.field_84_vram_rect.x,
            field_10_anim.field_84_vram_rect.y & ~63); // TODO: Required ?

        Prim_SetTPage* pTPage = &field_EC_tPage_p8[bufferIdx];
        Init_SetTPage_495FB0(pTPage, 0, 0, calcTPage);
        OrderingTable_Add_498A80(OtLayer(ppOt, field_10_anim.field_C_layer), &pTPage->mBase);

        PSX_RECT* pRect = &field_134_rects[bufferIdx];
        pRect->x = 32767;
        pRect->w = -32767;
        pRect->y = 32767;
        pRect->h = -32767;

        for (s32 i = 0; i < field_11E_number_of_segments; i++)
        {
            const PSX_Point* pPoint = &field_128_sprite_positions[i * field_120_number_of_pieces_per_segment];
            for (s32 j = 0; j < field_120_number_of_pieces_per_segment; j++)
            {
                if (pPoint->field_0_x < pRect->x)
                {
                    pRect->x = pPoint->field_0_x;
                }

                if (pPoint->field_0_x > pRect->w)
                {
                    pRect->w = pPoint->field_0_x;
                }

                if (pPoint->field_2_y < pRect->y)
                {
                    pRect->y = pPoint->field_2_y;
                }

                if (pPoint->field_2_y > pRect->h)
                {
                    pRect->h = pPoint->field_2_y;
                }
            }
        }

        pRect->x -= 25;
        pRect->w += 25;
        pRect->y -= 25;
        pRect->h += 25;
        const PSX_RECT* pRectToUse = &field_134_rects[gPsxDisplay_504C78.field_A_buffer_index];
        pScreenManager_4FF7C8->InvalidateRect_406E40(
            pRectToUse->x,
            pRectToUse->y,
            pRectToUse->w,
            pRectToUse->h,
            pScreenManager_4FF7C8->field_2E_idx);
    }
}

void ZapLine::VUpdate()
{
    VUpdate_4796B0();
}

void ZapLine::VUpdate_4796B0()
{
    field_116_alive_timer++;

    switch (field_E4_state)
    {
        case ZapLineState::eInit_0:
            CalculateZapPoints_479380();

            if (field_11A_type == ZapLineType::eThin_1)
            {
                CalculateThinSpriteSegmentPositions_4791F0();
            }
            else if (field_11A_type == ZapLineType::eThick_0)
            {
                CalculateThickSpriteSegmentPositions_478F20();
            }
            field_E4_state = ZapLineState::eInitSpritePositions_1;
            break;

        case ZapLineState::eInitSpritePositions_1:
            CalculateSpritePositionsOuter();
            field_E4_state = ZapLineState::eInitSpriteVertices_2;
            break;

        case ZapLineState::eInitSpriteVertices_2:
        case ZapLineState::eUpdateSpriteVertices_4:
            UpdateSpriteVertexPositions_4795B0();

            if (field_116_alive_timer >= field_118_max_alive_time && field_11A_type != ZapLineType::eThin_1)
            {
                field_6_flags.Set(BaseGameObject::eDead_Bit3);
                return;
            }

            if (field_11A_type == ZapLineType::eThin_1)
            {
                CalculateThinSpriteSegmentPositions_4791F0();
            }
            else if (field_11A_type == ZapLineType::eThick_0)
            {
                CalculateThickSpriteSegmentPositions_478F20();
            }
            field_E4_state = ZapLineState::eUpdateSpritePositions_3;
            break;

        case ZapLineState::eUpdateSpritePositions_3:
            CalculateSpritePositionsOuter();
            field_E4_state = ZapLineState::eUpdateSpriteVertices_4;
            break;
    }
}

void ZapLine::CalculateSpritePositionsOuter()
{
    for (s16 i = 0; i < field_11E_number_of_segments; i++)
    {
        if (i == 0)
        {
            // First item.
            CalculateSpritePositionsInner_479400(0, 0, 1, 0);
        }
        else
        {
            const s16 lastIdx = field_11E_number_of_segments - 1;
            if (i == lastIdx)
            {
                // Last item.
                CalculateSpritePositionsInner_479400(field_11E_number_of_segments - 2, lastIdx, lastIdx, field_11E_number_of_segments - 1);
            }
            else
            {
                // Other items.
                CalculateSpritePositionsInner_479400(i - 1, i, i + 1, i);
            }
        }
    }
}

void ZapLine::CalculateZapPoints_479380()
{
    FP acc = FP_FromInteger(0);
    const FP delta = FP_FromInteger(1) / FP_FromInteger(field_120_number_of_pieces_per_segment);
    for (s32 i = 0; i < field_120_number_of_pieces_per_segment; i++)
    {
        const FP accSqrd = (acc * acc);
        field_12C_zap_points[i].field_0 = accSqrd - FP_FromRaw(2 * acc.fpValue) + FP_FromInteger(1);
        field_12C_zap_points[i].field_4 = -FP_FromRaw(2 * accSqrd.fpValue) + FP_FromRaw(2 * acc.fpValue) + FP_FromInteger(1);
        field_12C_zap_points[i].field_8 = accSqrd;
        acc += delta;
    }
}

void ZapLine::CalculateThinSpriteSegmentPositions_4791F0()
{
    field_130_sprite_segment_positions[0].field_0_x = FP_FromInteger(field_10C_x_position_source);
    field_130_sprite_segment_positions[0].field_4_y = FP_FromInteger(field_10E_y_position_source);
    field_130_sprite_segment_positions[field_11E_number_of_segments - 1].field_0_x = FP_FromInteger(field_110_x_position_destination);
    field_130_sprite_segment_positions[field_11E_number_of_segments - 1].field_4_y = FP_FromInteger(field_112_y_position_destination);

    const FP x2Diff = FP_FromInteger(field_110_x_position_destination - field_10C_x_position_source) / FP_FromInteger(field_11E_number_of_segments);
    const FP y2Diff = FP_FromInteger(field_112_y_position_destination - field_10E_y_position_source) / FP_FromInteger(field_11E_number_of_segments);

    const FP y2DiffDiv = -y2Diff * FP_FromDouble(0.1);
    const FP x2DiffDiv = x2Diff * FP_FromDouble(0.1);

    for (s32 i = 1; i < field_11E_number_of_segments - 1; i++)
    {
        const FP rnd = FP_FromInteger(Math_NextRandom() % 32 - 16);
        field_130_sprite_segment_positions[i].field_0_x = (y2DiffDiv * rnd) + FP_FromInteger(field_10C_x_position_source) + (FP_FromInteger(i) * x2Diff);
        field_130_sprite_segment_positions[i].field_4_y = (x2DiffDiv * rnd) + FP_FromInteger(field_10E_y_position_source) + (FP_FromInteger(i) * y2Diff);
    }
}

void ZapLine::CalculateThickSpriteSegmentPositions_478F20()
{
    s32 v1 = 0;
    if (field_116_alive_timer >= 8)
    {
        const s32 v4 = field_118_max_alive_time - field_116_alive_timer;
        if (v4 >= 8)
        {
            v1 = 4;
        }
        else
        {
            v1 = (v4 / 4) + 3;
        }
    }
    else
    {
        v1 = (field_116_alive_timer / 4) + 3;
    }

    s32 v5 = 1 << v1;
    s32 v6 = 1 << (v1 - 1);

    field_130_sprite_segment_positions[0].field_0_x = FP_FromInteger(field_10C_x_position_source);
    field_130_sprite_segment_positions[0].field_4_y = FP_FromInteger(field_10E_y_position_source);

    field_130_sprite_segment_positions[field_11E_number_of_segments - 1].field_0_x = FP_FromInteger(field_110_x_position_destination);
    field_130_sprite_segment_positions[field_11E_number_of_segments - 1].field_4_y = FP_FromInteger(field_112_y_position_destination);

    s32 angExtra = 0;
    if ((gnFrameCount_507670 / 8) & 1)
    {
        angExtra = 0;
    }
    else
    {
        angExtra = 128;
    }

    const FP xDiff = FP_FromInteger(field_110_x_position_destination - field_10C_x_position_source) / FP_FromInteger(field_11E_number_of_segments);
    const FP xDiffDiv = (xDiff * FP_FromDouble(1.5));

    const FP yDiff = FP_FromInteger(field_112_y_position_destination - field_10E_y_position_source) / FP_FromInteger(field_11E_number_of_segments);
    const FP yDiffDiv = (-yDiff * FP_FromDouble(1.5));

    for (s32 i = 1; i < field_11E_number_of_segments - 1; i++)
    {
        const u8 ang = static_cast<u8>(angExtra + 18 * i);

        field_130_sprite_segment_positions[i].field_0_x = FP_FromInteger(Math_NextRandom() % v5) + (Math_Cosine_4510A0(ang) * yDiffDiv) + FP_FromInteger(field_10C_x_position_source) + (FP_FromInteger(i) * xDiff) - FP_FromInteger(v6);

        field_130_sprite_segment_positions[i].field_4_y = FP_FromInteger(Math_NextRandom() % v5) + (Math_Cosine_4510A0(ang) * xDiffDiv) + FP_FromInteger(field_10E_y_position_source) + (FP_FromInteger(i) * yDiff) - FP_FromInteger(v6);
    }

    field_134_rects[0].x = 0;
    field_134_rects[0].y = 0;
    field_134_rects[0].w = gPsxDisplay_504C78.field_0_width;
    field_134_rects[0].h = gPsxDisplay_504C78.field_2_height;

    field_134_rects[1].x = 0;
    field_134_rects[1].y = 0;
    field_134_rects[1].w = gPsxDisplay_504C78.field_0_width;
    ;
    field_134_rects[1].h = gPsxDisplay_504C78.field_2_height;
}

void ZapLine::UpdateSpriteVertexPositions_4795B0()
{
    for (s32 i = 0; i < field_11E_number_of_segments; i++)
    {
        for (s32 j = 0; j < field_120_number_of_pieces_per_segment; j++)
        {
            const auto pPoint = &field_128_sprite_positions[j + (i * field_120_number_of_pieces_per_segment)];
            Prim_Sprt* pSprt = &field_124_pSprts->field_0_sprts[j + (i * field_120_number_of_pieces_per_segment)];
            SetXY0(&pSprt[0], pPoint->field_0_x, pPoint->field_2_y);
            SetXY0(&pSprt[1], pPoint->field_0_x, pPoint->field_2_y);
        }
    }
}

void ZapLine::CalculateSpritePositionsInner_479400(s32 idx1, s32 idx2, s32 idx3, s16 idx4)
{
    const FP x1 = field_130_sprite_segment_positions[idx1].field_0_x;
    const FP y1 = field_130_sprite_segment_positions[idx1].field_4_y;

    const FP x2 = field_130_sprite_segment_positions[idx2].field_0_x;
    const FP y2 = field_130_sprite_segment_positions[idx2].field_4_y;

    const FP x3 = field_130_sprite_segment_positions[idx3].field_0_x;
    const FP y3 = field_130_sprite_segment_positions[idx3].field_4_y;

    for (s32 i = 0; i < field_120_number_of_pieces_per_segment; i++)
    {
        auto pItem = &field_128_sprite_positions[i + (idx4 * field_120_number_of_pieces_per_segment)];

        pItem->field_0_x = FP_GetExponent(
            FP_FromRaw((
                           (field_12C_zap_points[i].field_8 * x3) + (field_12C_zap_points[i].field_4 * x2) + (field_12C_zap_points[i].field_0 * x1))
                           .fpValue
                       >> 1));

        pItem->field_2_y = FP_GetExponent(
            FP_FromRaw((
                           (field_12C_zap_points[i].field_8 * y3) + (field_12C_zap_points[i].field_4 * y2) + (field_12C_zap_points[i].field_0 * y1))
                           .fpValue
                       >> 1));
    }
}

} // namespace AO
