#include "stdafx.h"
#include "FG1.hpp"
#include "Function.hpp"
#include "stdlib.hpp"
#include "ResourceManager.hpp"
#include "Map.hpp"
#include "ScreenManager.hpp"
#include "PsxDisplay.hpp"
#include "Sys_common.hpp"
#include "FG1Reader.hpp"
#ifdef TETHYS_SATURN
#include "VRam.hpp"
#include "Font.hpp" // Tethys_PauseHidesText (stream R3's pause predicate)
#include "Renderer/IRenderer.hpp"

// SATURN (decision D-R2, U1a A6, C2's request): FG1 pieces skipped in VRender
// because their PSX VRAM-model slot was refused (a dense foreground overflows
// the model shared with every sprite): an incomplete foreground, not a crash.
extern "C" volatile s32 Tethys_gFg1Skipped;
volatile s32 Tethys_gFg1Skipped = 0;
#endif

ALIVE_VAR(1, 0x5D1E28, DynamicArrayT<FG1>*, gFG1List_5D1E28, nullptr);

const static Layer sFg1_layer_to_bits_layer_5469BC[4] = {Layer::eLayer_Well_Half_4, Layer::eLayer_FG1_Half_18, Layer::eLayer_Well_23, Layer::eLayer_FG1_37};

class FG1Reader final : public BaseFG1Reader
{
public:
#ifdef TETHYS_SATURN
    // SATURN (D-R2): the converter writes AO's FG1 format for Exoddus
    // (tools/ae/ae_cam_convert.py, impl/C2.md): 16bpp content-only pieces,
    // never a full block nor a compressed span (FG1Reader.hpp's TETHYS arm
    // walks 12 + ((w + 1) / 2) * h * 2 bytes a piece).
    explicit FG1Reader(FG1& fg1)
        : BaseFG1Reader(FG1Format::AO)
        , mFg1(fg1)
    {
    }
#else
    explicit FG1Reader(FG1& fg1)
        : BaseFG1Reader(FG1Format::AE)
        , mFg1(fg1)
    {
    }
#endif

    void OnPartialChunk(const Fg1Chunk& rChunk) override
    {
#ifdef TETHYS_SATURN
        // A walk that meets more pieces than the header counted would write
        // past the CHNK block: the extra pieces are not built.
        if (mIdx >= static_cast<u32>(mFg1.field_28_render_block_count))
        {
            return;
        }
#endif
        Fg1Block* pRenderBlock = &mFg1.field_30_chnk_res[mIdx++];
        mFg1.Convert_Chunk_To_Render_Block_49A210(&rChunk, pRenderBlock);
    }

    void OnFullChunk(const Fg1Chunk& rChunk) override
    {
        pScreenManager_5BB5F4->InvalidateRect_40EC50(
            rChunk.field_4_xpos_or_compressed_size,
            rChunk.field_6_ypos,
            rChunk.field_8_width + rChunk.field_4_xpos_or_compressed_size - 1,
            rChunk.field_A_height + rChunk.field_6_ypos - 1,
            rChunk.field_2_layer_or_decompressed_size);
    }

    u8** Allocate(u32 len) override
    {
        // Shouldn't be called for AE
        return ResourceManager::Allocate_New_Locked_Resource_49BF40(
            ResourceManager::Resource_PBuf,
            0,
            len);
    }

    void Deallocate(u8** ptr) override
    {
        // Shouldn't be called for AE
        ResourceManager::FreeResource_49C330(ptr);
    }

private:
    FG1& mFg1;
    u32 mIdx = 0;
};

FG1* FG1::ctor_499FC0(u8** pFG1Res)
{
    BaseGameObject_ctor_4DBFA0(1, 0);

    SetVTable(this, 0x5469C8); // vTbl_FG1_5469C8

    field_6_flags.Set(BaseGameObject::eUpdateDuringCamSwap_Bit10);
    field_6_flags.Set(BaseGameObject::eSurviveDeathReset_Bit9);
    field_6_flags.Set(BaseGameObject::eDrawable_Bit4);

    SetType(AETypes::eFG1_101);

    field_24_level_id = gMap_5C3030.field_0_current_level;
    field_26_path_id = gMap_5C3030.field_2_current_path;

    gFG1List_5D1E28->Push_Back(this);

    // Cast to the actual FG1 resource block format
    FG1ResourceBlockHeader* pHeader = reinterpret_cast<FG1ResourceBlockHeader*>(*pFG1Res);
    
    // Check if its relive format FG1
    if (pHeader->mCount == ResourceManager::Resource_FG1)
    {
        // adjust past the new file magic
        pHeader = reinterpret_cast<FG1ResourceBlockHeader*>(*pFG1Res + sizeof(u32));
    }

    // So we can extract out the count of chunks and allocate a resource for it
    field_20_unused = 0;
    field_28_render_block_count = static_cast<s16>(pHeader->mCount);
#ifdef TETHYS_SATURN
    // SATURN (AO bt864 twin): pHeader is a RAW pointer into the FG1 SOURCE
    // chunk, which the camera stream allocated unlocked; the CHNK allocation
    // below can compact the heap (Reclaim_Memory_49C470 moves every unlocked
    // block) and leave the reader walking the old address. Lock the source for
    // the construction, restore its flags after.
    ResourceManager::Header* pSrcHdr = ResourceManager::Get_Header_49C410(pFG1Res);
    const s16 srcFlagsSaved = pSrcHdr->field_6_flags;
    pSrcHdr->field_6_flags |= ResourceManager::ResourceHeaderFlags::eLocked;
    // SATURN (AO ao262.9 twin): the CHNK array is asked SOFTLY. Refused, the
    // screen loses its foreground layer, not the run: restore the source lock
    // (a lock left set would pin the block forever), null the array AND zero
    // the count (the dtor and VRender walk the count).
    field_2C_ptr = ResourceManager::Alloc_New_Resource_ImplEx(
        ResourceManager::Resource_CHNK, 0, pHeader->mCount * sizeof(Fg1Block),
        true, ResourceManager::BlockAllocMethod::eLastMatching,
        true /*reclaim*/, false /*never fatal*/);
    if (!field_2C_ptr)
    {
        pSrcHdr->field_6_flags = srcFlagsSaved;
        field_30_chnk_res = nullptr;
        field_28_render_block_count = 0;
        return this;
    }
    field_30_chnk_res = reinterpret_cast<Fg1Block*>(*field_2C_ptr);
    // A piece the reader never reaches stays a refused slot (w 0): VRender
    // skips it and the dtor frees nothing for it.
    for (s32 i = 0; i < field_28_render_block_count; i++)
    {
        field_30_chnk_res[i].field_58_rect.w = 0;
    }

    FG1Reader loader(*this);
    loader.Iterate(pHeader);

    pSrcHdr->field_6_flags = srcFlagsSaved;
#else
    field_2C_ptr = ResourceManager::Allocate_New_Locked_Resource_49BF40(ResourceManager::Resource_CHNK, 0, pHeader->mCount * sizeof(Fg1Block));
    field_30_chnk_res = reinterpret_cast<Fg1Block*>(*field_2C_ptr);

    FG1Reader loader(*this);
    loader.Iterate(pHeader);
#endif

    return this;
}

BaseGameObject* FG1::VDestructor(s32 flags)
{
    return vdtor_49A1E0(flags);
}

BaseGameObject* FG1::vdtor_49A1E0(s32 flags)
{
    dtor_49A540();
    if (flags & 1)
    {
        ae_delete_free_495540(this);
    }
    return this;
}

void FG1::dtor_49A540()
{
    SetVTable(this, 0x5469C8); // vTbl_FG1_5469C8
    gFG1List_5D1E28->Remove_Item(this);
#ifdef TETHYS_SATURN
    // SATURN (AO FG1.cpp:285-297 twin): every built piece holds a PSX
    // VRAM-model slot; give each back (a refused or unbuilt piece holds w 0).
    for (s32 i = 0; field_30_chnk_res && i < field_28_render_block_count; i++)
    {
        const PSX_RECT& r = field_30_chnk_res[i].field_58_rect;
        if (r.w > 0)
        {
            Vram_free_495A60({r.x, r.y}, {r.w, r.h});
        }
    }
#endif
    ResourceManager::FreeResource_49C330(field_2C_ptr);
    BaseGameObject_dtor_4DBEC0();
}

s16 FG1::Convert_Chunk_To_Render_Block_49A210(const Fg1Chunk* pChunk, Fg1Block* pBlock)
{
#ifdef TETHYS_SATURN
    // SATURN (decision D-R2, U1a A6; AO Convert_Chunk_To_Render_Block_453BA0,
    // AliveLibAO/FG1.cpp:180-251): the piece's 16bpp pixels go to a PSX
    // VRAM-model slot and are drawn by two textured quads, AO's pixel path, in
    // place of the PC's transparency mask over the camera. The converter's
    // contract (impl/C2.md): x and w even, w <= 32, h <= 64, pixels 0x0000
    // transparent or 0x8000 | the CRAM word NBG1 shows there, content-only
    // (((w + 1) / 2) x h, the renderer's ContractDims), and bit 15 of the layer
    // field set on every piece: the certificate that it IS the background under
    // it, so the renderer may skip it when nothing was drawn beneath
    // (kFg1MarkClut, 442.ao.6). The layer index is AE's own 4-entry table.
    const s16 width_rounded = static_cast<s16>((pChunk->field_8_width + 1) & ~1u);
    if (!Vram_alloc_4956C0(pChunk->field_8_width, pChunk->field_A_height, 16, &pBlock->field_58_rect))
    {
        pBlock->field_58_rect.w = 0; // refused: skipped by VRender, counted there
        return 0;
    }
    const u16 rawLayer = pChunk->field_2_layer_or_decompressed_size;
    const bool bSkippable = (rawLayer & 0x8000u) != 0;
    pBlock->field_66_mapped_layer = sFg1_layer_to_bits_layer_5469BC[rawLayer & 3u];

    PSX_RECT rect = {};
    rect.x = pBlock->field_58_rect.x;
    rect.y = pBlock->field_58_rect.y;
    rect.w = width_rounded;
    rect.h = pChunk->field_A_height;
    IRenderer::GetRenderer()->Upload(IRenderer::BitDepth::e16Bit, rect, reinterpret_cast<const u8*>(&pChunk[1]));

    const s16 tPage = static_cast<s16>(PSX_getTPage_4F60E0(TPageMode::e16Bit_2, TPageAbr::eBlend_0, rect.x, rect.y));
    const u8 u0 = rect.x & 63;
    const u8 v0 = static_cast<u8>(rect.y);
    const u8 u1 = static_cast<u8>(u0 + pChunk->field_8_width - 1);
    const u8 v1 = static_cast<u8>(v0 + pChunk->field_A_height - 1);
    const s16 x1 = pChunk->field_4_xpos_or_compressed_size + pChunk->field_8_width;
    const s16 y2 = pChunk->field_6_ypos + pChunk->field_A_height;

    for (Poly_FT4& rPoly : pBlock->field_0_polys)
    {
        rPoly = {};
        PolyFT4_Init(&rPoly);
        Poly_Set_SemiTrans_4F8A60(&rPoly.mBase.header, FALSE);
        Poly_Set_Blending_4F8A20(&rPoly.mBase.header, TRUE);
        SetTPage(&rPoly, tPage);
        SetXY0(&rPoly, pChunk->field_4_xpos_or_compressed_size, pChunk->field_6_ypos);
        SetXY1(&rPoly, x1, pChunk->field_6_ypos);
        SetXY2(&rPoly, pChunk->field_4_xpos_or_compressed_size, y2);
        SetXY3(&rPoly, x1, y2);
        SetUV0(&rPoly, u0, v0);
        SetUV1(&rPoly, u1, v0);
        SetUV2(&rPoly, u0, v1);
        SetUV3(&rPoly, u1, v1);
        SetRGB0(&rPoly, 128, 128, 128);
        if (bSkippable)
        {
            SetClut(&rPoly, static_cast<s16>(kFg1MarkClut));
        }
    }
    return 1;
#else
    // Map the layer from FG1 internal to OT layer
    pBlock->field_66_mapped_layer = sFg1_layer_to_bits_layer_5469BC[pChunk->field_2_layer_or_decompressed_size];

    // Copy in the bits that represent the see through pixels
    memcpy(pBlock->field_68_array_of_height, &pChunk[1], pChunk->field_A_height * sizeof(u32));

    for (Poly_FT4& rPoly : pBlock->field_0_polys)
    {
        rPoly = {};

        PolyFT4_Init(&rPoly);

        Poly_Set_SemiTrans_4F8A60(&rPoly.mBase.header, FALSE);
        Poly_Set_Blending_4F8A20(&rPoly.mBase.header, TRUE);

        SetTPage(&rPoly, static_cast<u16>(PSX_getTPage_4F60E0(TPageMode::e16Bit_2, TPageAbr::eBlend_0, 0, 0)));

        SetXYWH(&rPoly, pChunk->field_4_xpos_or_compressed_size, pChunk->field_6_ypos, pChunk->field_8_width, pChunk->field_A_height);

        SetPrimExtraPointerHack(&rPoly, pBlock->field_68_array_of_height);
    }
    return 1;
#endif
}

void FG1::VRender(PrimHeader** ppOt)
{
    vRender_49A3C0(ppOt);
}

void FG1::vRender_49A3C0(PrimHeader** ppOt)
{
#ifdef TETHYS_SATURN
    // SATURN (decision D-R2, U1a A6; AO VRender_453D50): every built piece is
    // drawn every frame. The PC's per-tile IsDirty gate goes with the dirty
    // tiles (InvalidateRect_40EC90 is a no-op here); the renderer itself skips a
    // certified piece that covers nothing. AO 442.ao.7: not drawn while the
    // pause menu is up (the veil dims NBG1 and the palettes, these direct-colour
    // pieces would stay bright over it). A refused CHNK leaves no array.
    if (!field_30_chnk_res || Tethys_PauseHidesText())
    {
        return;
    }
    for (s32 i = 0; i < field_28_render_block_count; i++)
    {
        Fg1Block* pBlock = &field_30_chnk_res[i];
        if (pBlock->field_58_rect.w == 0)
        {
            Tethys_gFg1Skipped++; // its VRAM-model slot was refused
            continue;
        }
        Poly_FT4* pPoly = &pBlock->field_0_polys[gPsxDisplay_5C1130.field_C_buffer_index];
        OrderingTable_Add_4F8AA0(OtLayer(ppOt, pBlock->field_66_mapped_layer), &pPoly->mBase.header);
    }
#else
    for (s32 i = 0; i < field_28_render_block_count; i++)
    {
        Poly_FT4* pPoly = &field_30_chnk_res[i].field_0_polys[gPsxDisplay_5C1130.field_C_buffer_index];
        const s32 xpos = X0(pPoly);
        const s32 ypos = Y0(pPoly);
        if (pScreenManager_5BB5F4->IsDirty_40EBC0(pScreenManager_5BB5F4->field_3A_idx, xpos, ypos) || pScreenManager_5BB5F4->IsDirty_40EBC0(3, xpos, ypos))
        {
            OrderingTable_Add_4F8AA0(OtLayer(ppOt, field_30_chnk_res[i].field_66_mapped_layer), &pPoly->mBase.header);
            // NOTE: Polygon has a pointer to the bit fields for which pixels should be skipped
            pScreenManager_5BB5F4->InvalidateRect_40EC90(xpos, ypos, X3(pPoly), Y3(pPoly), pScreenManager_5BB5F4->field_3A_idx);
        }
    }
#endif
}

void FG1::VScreenChanged()
{
    vScreenChanged_49A520();
}

void FG1::vScreenChanged_49A520()
{
    field_6_flags.Set(BaseGameObject::eDead_Bit3);
}
