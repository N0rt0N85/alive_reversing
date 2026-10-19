#include "stdafx.h"
#include "ZapLine.hpp"
#include "Function.hpp"
#include "ResourceManager.hpp"
#include "stdlib.hpp"
#include "ScreenManager.hpp"
#include "PsxDisplay.hpp"
#include "Game.hpp"
#ifdef TETHYS_SATURN
#include "TethysObjectHooks.hpp"
#endif

EXPORT ZapLine* ZapLine::ctor_4CC690(FP xPosSource, FP yPosSource, FP xPosDest, FP yPosDest, s16 aliveTime, ZapLineType type, Layer layer)
{
    BaseAnimatedWithPhysicsGameObject_ctor_424930(0);
    field_12A_type = type;
    SetVTable(this, 0x5478A0);
    SetType(AETypes::eZapLine_135);

    if (type == ZapLineType::eThin_1)
    {
        // Creates thin blue zap lines.
#ifdef TETHYS_SATURN
        // SATURN (U1d T3, AO 383.ao.1 twin): 12 x 20 = 240 dots against a Sprt
        // allowance of 96 a frame (src/renderer_saturn.cxx kBloodCapPacked) and
        // 128 VDP1 commands: the tail was never drawn. The Shrykull's bolt keeps
        // its 12 segments (the jitter, the authored shape) and lays 7 dots on
        // each: 84, the same thinning AO ships on the same object.
        field_130_number_of_pieces_per_segment = 7;
#else
        field_130_number_of_pieces_per_segment = 20;
#endif
        field_12E_number_of_segments = 12;
        const AnimRecord& rec = AnimRec(AnimId::Zap_Line_Blue);
        u8** ppRes = Add_Resource_4DC130(ResourceManager::Resource_Animation, rec.mResourceId);
        Animation_Init_424E10(rec.mFrameTableOffset, rec.mMaxW, rec.mMaxH, ppRes, 1, 1);
        field_12C_tPageAbr = TPageAbr::eBlend_3;
    }
    else if (type == ZapLineType::eThick_0)
    {
        // Creates thick red zap lines.
#ifdef TETHYS_SATURN
        // SATURN (U1d T3, decision D3 (b)): 28 x 10 = 280 dots a bolt, and a
        // Greeter fires THREE at once (Greeter.cpp:515-551): even AO's 3 dots a
        // segment would be 252 against the 96 allowance, and the bolts were cut.
        // One dot a segment keeps the 28-segment shape at 28 dots: 84 for a
        // Greeter's zap, 28 for a security orb's. CalculateZapPoints scales
        // with it (delta = 1 / pieces): the dot sits between two segment points.
        field_130_number_of_pieces_per_segment = 1;
#else
        field_130_number_of_pieces_per_segment = 10;
#endif
        field_12E_number_of_segments = 28;
        const AnimRecord& rec = AnimRec(AnimId::Zap_Line_Red);
        u8** ppRes = Add_Resource_4DC130(ResourceManager::Resource_Animation, rec.mResourceId);
        Animation_Init_424E10(rec.mFrameTableOffset, rec.mMaxW, rec.mMaxH, ppRes, 1, 1);
        field_12C_tPageAbr = TPageAbr::eBlend_1;
    }

    field_132_number_of_sprites = field_12E_number_of_segments * field_130_number_of_pieces_per_segment;
    field_20_animation.field_4_flags.Set(AnimFlags::eBit25_bDecompressDone); // HIBYTE |= 1
    field_20_animation.field_4_flags.Clear(AnimFlags::eBit15_bSemiTrans);
    field_20_animation.field_C_render_layer = layer;

#ifdef TETHYS_SATURN
    // SATURN (U1d T2/T3): ONE locked block for the four arrays (the sprites, the
    // dot positions, the zap points, the segment positions), taken from the
    // resource heap instead of three HWRAM pool mallocs (the pool is the scarce
    // memory: ~350 B a bolt moved out of it), and refused SOFTLY: the beam is
    // decoration, its caller deals the damage on its own (Greeter, SecurityOrb,
    // Shrykull). Every array size is a multiple of 4 and the block is 4-aligned,
    // so each carved array is aligned for its FP members. Locked: the block
    // never moves, which is what makes the raw pointers below legal (upstream
    // relies on the same for its sprites).
    {
        const u32 sprBytes = sizeof(ZapLineSprites) * field_132_number_of_sprites;
        const u32 posBytes = sizeof(PSX_Point) * field_132_number_of_sprites;
        const u32 zapBytes = sizeof(ZapPoint) * field_130_number_of_pieces_per_segment;
        const u32 segBytes = sizeof(FP_Point) * field_12E_number_of_segments;
        field_F8_ppRes = Tethys_ResAllocSoft(ResourceManager::ResourceType::Resource_Spline, 0, sprBytes + posBytes + zapBytes + segBytes,
                                             true /*locked*/, true /*reclaim: nothing here holds a raw pointer yet*/);
        if (!field_F8_ppRes)
        {
            // Dead on arrival, with null buffers (the dtor and the two virtuals
            // below tolerate them). A thick bolt is fired and forgotten: it dies
            // now. The Shrykull's thin bolt is KEPT by id and moved every tick
            // (Shrykull.cpp:179-200, :339, the last without a null test), so it
            // stays alive and simply never draws, as long as its owner wants it.
            Tethys_gZapAllocFail++;
            field_134_pSprites = nullptr;
            field_138_sprite_positions = nullptr;
            field_13C_zap_points = nullptr;
            field_140_sprite_segment_positions = nullptr;
            field_128_max_alive_time = aliveTime;
            field_F4_state = ZapLineState::eInit_0;
            field_126_alive_timer = 0;
            if (type != ZapLineType::eThin_1)
            {
                field_6_flags.Set(BaseGameObject::eDead_Bit3);
            }
            CalculateSourceAndDestinationPositions_4CCAD0(xPosSource, yPosSource, xPosDest, yPosDest);
            return this;
        }
        u8* const pBlock = *field_F8_ppRes;
        field_134_pSprites = reinterpret_cast<ZapLineSprites*>(pBlock);
        field_138_sprite_positions = reinterpret_cast<PSX_Point*>(pBlock + sprBytes);
        field_13C_zap_points = reinterpret_cast<ZapPoint*>(pBlock + sprBytes + posBytes);
        field_140_sprite_segment_positions = reinterpret_cast<FP_Point*>(pBlock + sprBytes + posBytes + zapBytes);
    }
#else
    field_F8_ppRes = ResourceManager::Allocate_New_Locked_Resource_49BF40(ResourceManager::ResourceType::Resource_Spline, 0, sizeof(ZapLineSprites) * field_132_number_of_sprites); // Spln (spline?).

    field_134_pSprites = reinterpret_cast<ZapLineSprites*>(*field_F8_ppRes);
    field_138_sprite_positions = reinterpret_cast<PSX_Point*>(ae_malloc_non_zero_4954F0(sizeof(PSX_Point) * field_132_number_of_sprites));
    field_13C_zap_points = reinterpret_cast<ZapPoint*>(ae_malloc_non_zero_4954F0(sizeof(ZapPoint) * field_130_number_of_pieces_per_segment));
    field_140_sprite_segment_positions = reinterpret_cast<FP_Point*>(ae_malloc_non_zero_4954F0(sizeof(FP_Point) * field_12E_number_of_segments));
#endif

    field_128_max_alive_time = aliveTime;

    field_B8_xpos = xPosSource;
    field_BC_ypos = yPosSource;

    field_F4_state = ZapLineState::eInit_0;
    field_126_alive_timer = 0;

    if (field_20_animation.field_4_flags.Get(AnimFlags::eBit13_Is8Bit))
    {
        field_124_tPageMode = TPageMode::e8Bit_1;
    }
    else if (field_20_animation.field_4_flags.Get(AnimFlags::eBit14_Is16Bit))
    {
        field_124_tPageMode = TPageMode::e16Bit_2;
    }
    else
    {
        field_124_tPageMode = TPageMode::e4Bit_0;
    }

    u8 u0 = field_20_animation.field_84_vram_rect.x & 0x3F;
    if (field_124_tPageMode == TPageMode::e8Bit_1)
    {
        u0 = 2 * u0;
    }
    else if (field_124_tPageMode == TPageMode::e4Bit_0)
    {
        u0 = 4 * u0;
    }

    auto pFrameHeader = reinterpret_cast<const FrameHeader*>(&(*field_20_animation.field_20_ppBlock)[field_20_animation.Get_FrameHeader_40B730(-1)->field_0_frame_header_offset]);

    const u8 frameW = pFrameHeader->field_4_width;
    const u8 frameH = pFrameHeader->field_5_height;

    for (s32 i = 0; i < 2; i++)
    {
        for (s32 j = 0; j < field_12E_number_of_segments; j++)
        {
            for (s32 k = 0; k < field_130_number_of_pieces_per_segment; k++)
            {
                Prim_Sprt* pSprt = &field_134_pSprites[(j * field_130_number_of_pieces_per_segment) + k].field_0_sprts[i];
                Sprt_Init_4F8910(pSprt);

                Poly_Set_SemiTrans_4F8A60(&pSprt->mBase.header, 1);
                Poly_Set_Blending_4F8A20(&pSprt->mBase.header, 1);

                SetClut(pSprt, static_cast<s16>(PSX_getClut_4F6350(field_20_animation.field_8C_pal_vram_xy.field_0_x, field_20_animation.field_8C_pal_vram_xy.field_2_y)));

                SetUV0(pSprt, u0, field_20_animation.field_84_vram_rect.y & 0xFF);
                pSprt->field_14_w = frameW - 1;
                pSprt->field_16_h = frameH - 1;
            }
        }
    }

    CalculateSourceAndDestinationPositions_4CCAD0(xPosSource, yPosSource, xPosDest, yPosDest);

    return this;
}

BaseGameObject* ZapLine::VDestructor(s32 flags)
{
    return vdtor_4CCAA0(flags);
}

void ZapLine::VUpdate()
{
    vUpdate_4CD790();
}

void ZapLine::VRender(PrimHeader** ppOt)
{
    vRender_4CD8C0(ppOt);
}


void ZapLine::VScreenChanged()
{
    vScreenChanged_4CDBE0();
}

ZapLine* ZapLine::vdtor_4CCAA0(s32 flags)
{
    dtor_4CCCB0();
    if (flags & 1)
    {
        ae_delete_free_495540(this);
    }
    return this;
}

void ZapLine::CalculateSourceAndDestinationPositions_4CCAD0(FP xPosSource, FP yPosSource, FP xPosDest, FP yPosDest)
{
    field_11C_x_position_source = FP_GetExponent(xPosSource - pScreenManager_5BB5F4->field_20_pCamPos->field_0_x);
    field_11E_y_position_source = FP_GetExponent(yPosSource - pScreenManager_5BB5F4->field_20_pCamPos->field_4_y);
    field_120_x_position_destination = FP_GetExponent(xPosDest - pScreenManager_5BB5F4->field_20_pCamPos->field_0_x);
    field_122_y_position_destination = FP_GetExponent(yPosDest - pScreenManager_5BB5F4->field_20_pCamPos->field_4_y);

    field_11C_x_position_source = PsxToPCX(field_11C_x_position_source, 11);
    field_120_x_position_destination = PsxToPCX(field_120_x_position_destination, 11);

    s16 xOff = 0;
    s16 yOff = 0;
    field_20_animation.Get_Frame_Offset_40C480(&xOff, &yOff);

    field_11C_x_position_source = FP_GetExponent(FP_FromInteger(xOff) + FP_FromInteger(field_11C_x_position_source));
    field_11E_y_position_source = FP_GetExponent(FP_FromInteger(yOff) + FP_FromInteger(field_11E_y_position_source));
    field_120_x_position_destination = FP_GetExponent(FP_FromInteger(xOff) + FP_FromInteger(field_120_x_position_destination));
    field_122_y_position_destination = FP_GetExponent(FP_FromInteger(yOff) + FP_FromInteger(field_122_y_position_destination));
}

void ZapLine::dtor_4CCCB0()
{
    SetVTable(this, 0x5478A0);
    ResourceManager::FreeResource_49C330(field_F8_ppRes);
#ifndef TETHYS_SATURN
    // SATURN: the three arrays live inside field_F8_ppRes (see the ctor).
    ae_non_zero_free_495560(field_138_sprite_positions);
    ae_non_zero_free_495560(field_13C_zap_points);
    ae_non_zero_free_495560(field_140_sprite_segment_positions);
#endif
    BaseAnimatedWithPhysicsGameObject_dtor_424AD0();
}

void ZapLine::vScreenChanged_4CDBE0()
{
    if (gMap_5C3030.field_0_current_level != gMap_5C3030.field_A_level || gMap_5C3030.field_2_current_path != gMap_5C3030.field_C_path)
    {
        field_6_flags.Set(BaseGameObject::eDead_Bit3);
    }
}

void ZapLine::CalculateThickSpriteSegmentPositions_4CCD50()
{
    // TODO: Convert bit operations to something more readable.
    s32 v1 = 0;
    if (field_126_alive_timer >= 8)
    {
        const s32 remainingAliveTime = field_128_max_alive_time - field_126_alive_timer;
        if (remainingAliveTime >= 8)
        {
            v1 = 4;
        }
        else
        {
            v1 = (remainingAliveTime / 4) + 3;
        }
    }
    else
    {
        v1 = field_126_alive_timer / 4 + 3;
    }

    s32 v5 = 1 << v1;
    s32 v6 = 1 << (v1 - 1);

    field_140_sprite_segment_positions[0].field_0_x = FP_FromInteger(field_11C_x_position_source);
    field_140_sprite_segment_positions[0].field_4_y = FP_FromInteger(field_11E_y_position_source);

    field_140_sprite_segment_positions[field_12E_number_of_segments - 1].field_0_x = FP_FromInteger(field_120_x_position_destination);
    field_140_sprite_segment_positions[field_12E_number_of_segments - 1].field_4_y = FP_FromInteger(field_122_y_position_destination);

    s32 angExtra = 0;
    if ((sGnFrame_5C1B84 / 8) & 1)
    {
        angExtra = 0;
    }
    else
    {
        angExtra = 128;
    }

    const FP xDiff = FP_FromInteger(field_120_x_position_destination - field_11C_x_position_source) / FP_FromInteger(field_12E_number_of_segments);
    const FP xDiffDiv = -xDiff * FP_FromDouble(1.5);

    const FP yDiff = FP_FromInteger(field_122_y_position_destination - field_11E_y_position_source) / FP_FromInteger(field_12E_number_of_segments);
    const FP yDiffDiv = yDiff * FP_FromDouble(1.5);


    // First and last done above.
    for (s32 i = 1; i < field_12E_number_of_segments - 1; i++)
    {
        const u8 ang = static_cast<u8>(angExtra + 18 * i);
        field_140_sprite_segment_positions[i].field_0_x = FP_FromInteger(Math_NextRandom() % v5) + (Math_Cosine_496CD0(ang) * xDiffDiv) + FP_FromInteger(field_11C_x_position_source) + (FP_FromInteger(i) * xDiff) - FP_FromInteger(v6);

        field_140_sprite_segment_positions[i].field_4_y = FP_FromInteger(Math_NextRandom() % v5) + (Math_Cosine_496CD0(ang) * yDiffDiv) + FP_FromInteger(field_11E_y_position_source) + (FP_FromInteger(i) * yDiff) - FP_FromInteger(v6);
    }

    field_144_rects[0].x = 0;
    field_144_rects[0].y = 0;
    field_144_rects[0].w = gPsxDisplay_5C1130.field_0_width;
    field_144_rects[0].h = gPsxDisplay_5C1130.field_2_height;

    field_144_rects[1].x = 0;
    field_144_rects[1].y = 0;
    field_144_rects[1].w = gPsxDisplay_5C1130.field_0_width;
    field_144_rects[1].h = gPsxDisplay_5C1130.field_2_height;
}

void ZapLine::CalculateThinSpriteSegmentPositions_4CD110()
{
    field_140_sprite_segment_positions[0].field_0_x = FP_FromInteger(field_11C_x_position_source);
    field_140_sprite_segment_positions[0].field_4_y = FP_FromInteger(field_11E_y_position_source);
    field_140_sprite_segment_positions[field_12E_number_of_segments - 1].field_0_x = FP_FromInteger(field_120_x_position_destination);
    field_140_sprite_segment_positions[field_12E_number_of_segments - 1].field_4_y = FP_FromInteger(field_122_y_position_destination);

    const FP x2Diff = FP_FromInteger(field_120_x_position_destination - field_11C_x_position_source) / FP_FromInteger(field_12E_number_of_segments);
    const FP y2Diff = FP_FromInteger(field_122_y_position_destination - field_11E_y_position_source) / FP_FromInteger(field_12E_number_of_segments);

    const FP y2DiffDiv = -y2Diff * FP_FromDouble(0.1);
    const FP x2DiffDiv = x2Diff * FP_FromDouble(0.1);

    for (s32 i = 1; i < field_12E_number_of_segments - 1; i++)
    {
        const FP rnd = FP_FromInteger(Math_NextRandom() % 32 - 16);
        field_140_sprite_segment_positions[i].field_0_x = (y2DiffDiv * rnd) + FP_FromInteger(field_11C_x_position_source) + (FP_FromInteger(i) * x2Diff);
        field_140_sprite_segment_positions[i].field_4_y = (x2DiffDiv * rnd) + FP_FromInteger(field_11E_y_position_source) + (FP_FromInteger(i) * y2Diff);
    }
}

void ZapLine::CalculateZapPoints_4CD340()
{
    FP acc = FP_FromInteger(0);
    const FP delta = FP_FromInteger(1) / FP_FromInteger(field_130_number_of_pieces_per_segment);
    for (s32 i = 0; i < field_130_number_of_pieces_per_segment; i++)
    {
        const FP accSqrd = (acc * acc);
        field_13C_zap_points[i].field_0_part_1 = accSqrd - FP_FromRaw(2 * acc.fpValue) + FP_FromInteger(1);
        field_13C_zap_points[i].field_4_part_2 = -FP_FromRaw(2 * accSqrd.fpValue) + FP_FromRaw(2 * acc.fpValue) + FP_FromInteger(1);
        field_13C_zap_points[i].field_8_part_3 = accSqrd;
        acc += delta;
    }
}

void ZapLine::CalculateSpritePositionsInner_4CD400(s32 idx1, s32 idx2, s32 idx3, s16 idx4)
{
    const FP x1 = field_140_sprite_segment_positions[idx1].field_0_x;
    const FP y1 = field_140_sprite_segment_positions[idx1].field_4_y;

    const FP x2 = field_140_sprite_segment_positions[idx2].field_0_x;
    const FP y2 = field_140_sprite_segment_positions[idx2].field_4_y;

    const FP x3 = field_140_sprite_segment_positions[idx3].field_0_x;
    const FP y3 = field_140_sprite_segment_positions[idx3].field_4_y;

    for (s32 i = 0; i < field_130_number_of_pieces_per_segment; i++)
    {
        auto pItem = &field_138_sprite_positions[i + (idx4 * field_130_number_of_pieces_per_segment)];

        pItem->field_0_x = FP_GetExponent(
            FP_FromRaw((
                           (field_13C_zap_points[i].field_8_part_3 * x3) + (field_13C_zap_points[i].field_4_part_2 * x2) + (field_13C_zap_points[i].field_0_part_1 * x1))
                           .fpValue
                       >> 1));

        pItem->field_2_y = FP_GetExponent(
            FP_FromRaw((
                           (field_13C_zap_points[i].field_8_part_3 * y3) + (field_13C_zap_points[i].field_4_part_2 * y2) + (field_13C_zap_points[i].field_0_part_1 * y1))
                           .fpValue
                       >> 1));
    }
}

void ZapLine::UpdateSpriteVertexPositions_4CD650()
{
    for (s32 i = 0; i < field_12E_number_of_segments; i++)
    {
        for (s32 j = 0; j < field_130_number_of_pieces_per_segment; j++)
        {
            const auto pPoint = &field_138_sprite_positions[j + (i * field_130_number_of_pieces_per_segment)];
            Prim_Sprt* pSprt = &field_134_pSprites->field_0_sprts[j + (i * field_130_number_of_pieces_per_segment)];
            SetXY0(&pSprt[0], pPoint->field_0_x, pPoint->field_2_y);
            SetXY0(&pSprt[1], pPoint->field_0_x, pPoint->field_2_y);
        }
    }
}

void ZapLine::CalculateSpritePositionsOuter_4CD5D0()
{
    for (s16 i = 0; i < field_12E_number_of_segments; i++)
    {
        if (i == 0)
        {
            // First item.
            CalculateSpritePositionsInner_4CD400(0, 0, 1, 0);
        }
        else
        {
            const s16 lastIdx = field_12E_number_of_segments - 1;
            if (i == lastIdx)
            {
                // Last item.
                CalculateSpritePositionsInner_4CD400(field_12E_number_of_segments - 2, lastIdx, lastIdx, field_12E_number_of_segments - 1);
            }
            else
            {
                // Other items.
                CalculateSpritePositionsInner_4CD400(i - 1, i, i + 1, i);
            }
        }
    }
}

void ZapLine::vUpdate_4CD790()
{
#ifdef TETHYS_SATURN
    if (!field_134_pSprites)
    {
        return; // SATURN: refused its buffers (ctor): an inert thin bolt, nothing to move
    }
#endif
    field_126_alive_timer++;

    // TODO: States 3 and 4 might not actually be needed, since states 1 and 2 do the same thing; though the class only seems to render in states 3 and 4.
    switch (field_F4_state)
    {
        case ZapLineState::eInit_0:
            CalculateZapPoints_4CD340();

            if (field_12A_type == ZapLineType::eThin_1)
            {
                CalculateThinSpriteSegmentPositions_4CD110();
            }
            else if (field_12A_type == ZapLineType::eThick_0)
            {
                CalculateThickSpriteSegmentPositions_4CCD50();
            }

            field_F4_state = ZapLineState::eInitSpritePositions_1;
            break;

        case ZapLineState::eInitSpritePositions_1:
            CalculateSpritePositionsOuter_4CD5D0();
            field_F4_state = ZapLineState::eInitSpriteVertices_2;
            break;

        case ZapLineState::eInitSpriteVertices_2:
        case ZapLineState::eUpdateSpriteVertices_4:
            UpdateSpriteVertexPositions_4CD650();

            if (field_126_alive_timer >= field_128_max_alive_time && field_12A_type != ZapLineType::eThin_1)
            {
                field_6_flags.Set(BaseGameObject::eDead_Bit3);
                return;
            }

            if (field_12A_type == ZapLineType::eThin_1)
            {
                CalculateThinSpriteSegmentPositions_4CD110();
            }
            else if (field_12A_type == ZapLineType::eThick_0)
            {
                CalculateThickSpriteSegmentPositions_4CCD50();
            }
            field_F4_state = ZapLineState::eUpdateSpritePositions_3;
            break;

        case ZapLineState::eUpdateSpritePositions_3:
            CalculateSpritePositionsOuter_4CD5D0();
            field_F4_state = ZapLineState::eUpdateSpriteVertices_4;
            break;
    }
}

void ZapLine::vRender_4CD8C0(PrimHeader** ppOt)
{
#ifdef TETHYS_SATURN
    // SATURN (AO 390.ao.1 twin): `h` counts render calls and dots queued; an
    // inert bolt (refused its buffers) draws nothing.
    Tethys_gZapLink++;
    if (!field_134_pSprites)
    {
        return;
    }
#endif
    if (gMap_5C3030.Is_Point_In_Current_Camera_4810D0(
            field_C2_lvl_number,
            field_C0_path_number,
            field_B8_xpos,
            field_BC_ypos,
            0)
        && field_F4_state > ZapLineState::eInitSpriteVertices_2)
    {
        const auto bufferIdx = gPsxDisplay_5C1130.field_C_buffer_index;

        for (s32 i = 0; i < field_12E_number_of_segments; i++)
        {
            for (s32 j = 0; j < field_130_number_of_pieces_per_segment; j++)
            {
                Prim_Sprt* pSprt = &field_134_pSprites->field_0_sprts[j + (i * field_130_number_of_pieces_per_segment)];
#ifdef TETHYS_SATURN
                pSprt[bufferIdx].mBase.header.hackPtr = nullptr; // queued once, below
                Tethys_gZapLink++;
#else
                OrderingTable_Add_4F8AA0(OtLayer(ppOt, field_20_animation.field_C_render_layer), &pSprt[bufferIdx].mBase.header);
#endif
            }
        }

#ifdef TETHYS_SATURN
        // SATURN (U1d T3, AO 448.ao.8 twin): ONE RENDERER CALL FOR THE BEAM. The
        // dots share one cel, palette, size and colour and only their places
        // move; the first dot is the only one queued and stands for all of them,
        // in the order the table would have walked them (src/renderer_saturn.cxx,
        // DrawSprtBurst), before the tpage as they were. The loop above reads the
        // Sprts as one flat array, a Sprt apart (RELIVE's own indexing, kept: on
        // the single-buffered AE display bufferIdx is always 0).
        const s32 tethysDots = field_12E_number_of_segments * field_130_number_of_pieces_per_segment;
        if (tethysDots > 0)
        {
            Prim_Sprt* pLead = &field_134_pSprites->field_0_sprts[bufferIdx];
            field_154_tethys_spray.mMagic = kTethysSprayMagic;
            field_154_tethys_spray.mpFirst = reinterpret_cast<const u8*>(pLead);
            field_154_tethys_spray.mCount = static_cast<u16>(tethysDots);
            field_154_tethys_spray.mStride = static_cast<u16>(sizeof(Prim_Sprt));
            pLead->mBase.header.hackPtr = &field_154_tethys_spray;
            OrderingTable_Add_4F8AA0(OtLayer(ppOt, field_20_animation.field_C_render_layer), &pLead->mBase.header);
        }
#endif

        const s32 calcTPage = PSX_getTPage_4F60E0(
            field_124_tPageMode,
            field_12C_tPageAbr,
            field_20_animation.field_84_vram_rect.x,
            field_20_animation.field_84_vram_rect.y);

        Prim_SetTPage* pTPage = &field_FC_tPage_p8[bufferIdx];
        Init_SetTPage_4F5B60(pTPage, 0, 0, calcTPage);
        OrderingTable_Add_4F8AA0(OtLayer(ppOt, field_20_animation.field_C_render_layer), &pTPage->mBase);

        PSX_RECT* pRect = &field_144_rects[bufferIdx];
        pRect->x = 32767;
        pRect->w = -32767;
        pRect->y = 32767;
        pRect->h = -32767;

        for (s32 i = 0; i < field_12E_number_of_segments; i++)
        {
            const PSX_Point* pPoint = &field_138_sprite_positions[i * field_130_number_of_pieces_per_segment];
            for (s32 j = 0; j < field_130_number_of_pieces_per_segment; j++)
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
        const PSX_RECT* pRectToUse = &field_144_rects[gPsxDisplay_5C1130.field_C_buffer_index];
        pScreenManager_5BB5F4->InvalidateRect_40EC90(
            pRectToUse->x,
            pRectToUse->y,
            pRectToUse->w,
            pRectToUse->h,
            pScreenManager_5BB5F4->field_3A_idx);
    }
}
