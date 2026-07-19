#include "stdafx_ao.h"
#include "FG1.hpp"
#include "Function.hpp"
#include "Psx.hpp"
#include "Primitives.hpp"
#include "ScreenManager.hpp"
#include "ResourceManager.hpp"
#include "Map.hpp"
#include "Game.hpp"
#include "Sys_common.hpp"
#include "VRam.hpp"
#include "stdlib.hpp"
#include "PsxDisplay.hpp"
#include "Compression.hpp"
#include "../AliveLibAE/Renderer/IRenderer.hpp"
#include "FG1Reader.hpp"

namespace AO {

struct Fg1Block
{
    Poly_FT4 field_0_polys[2];
    PSX_RECT field_58_rect;
    s32 field_60_padding;
    s16 field_64_padding;
    Layer field_66_mapped_layer;
    u32 field_68_array_of_height[16]; // Added for AE format interop
};
//ALIVE_ASSERT_SIZEOF(Fg1Block, 0x68);

class FG1Reader final : public BaseFG1Reader
{
public:
    explicit FG1Reader(FG1& fg1)
        : BaseFG1Reader(FG1Format::AO)
        , mFg1(fg1)
    {
    }

    void OnPartialChunk(const Fg1Chunk& rChunk) override
    {
        Fg1Block* pRenderBlock = &mFg1.field_20_chnk_res[mIdx++];
        mFg1.Convert_Chunk_To_Render_Block_453BA0(&rChunk, pRenderBlock);
    }

    void OnFullChunk(const Fg1Chunk& rChunk) override
    {
        // For some reason the screen manage doesn't work the same as in AE and this won't
        // result in full blocks getting drawn. Therefore we should never see this get called
        // as all blocks are partial (full blocks are "fake" partial blocks).
        pScreenManager_4FF7C8->InvalidateRect_406D80(
            rChunk.field_4_xpos_or_compressed_size,
            rChunk.field_6_ypos,
            rChunk.field_8_width + rChunk.field_4_xpos_or_compressed_size - 1,
            rChunk.field_A_height + rChunk.field_6_ypos - 1,
            rChunk.field_2_layer_or_decompressed_size);
    }

    u8** Allocate(u32 len) override
    {
        return ResourceManager::Allocate_New_Locked_Resource_454F80(
            ResourceManager::Resource_PBuf,
            0,
            len);
    }

    void Deallocate(u8** ptr) override
    {
        ResourceManager::FreeResource_455550(ptr);
    }

private:
    FG1& mFg1;
    u32 mIdx = 0;
};

// Reads the tweaked AE format FG1
class FG1ReaderAE final : public BaseFG1Reader
{
public:
    explicit FG1ReaderAE(FG1& fg1)
        : BaseFG1Reader(FG1Format::AE)
        , mFg1(fg1)
    {
    }

    void OnPartialChunk(const Fg1Chunk& rChunk) override
    {
        Fg1Block* pRenderBlock = &mFg1.field_20_chnk_res[mIdx++];
        mFg1.Convert_Chunk_To_Render_Block_AE(&rChunk, pRenderBlock);
    }

    void OnFullChunk(const Fg1Chunk& rChunk) override
    {
        pScreenManager_4FF7C8->InvalidateRect_406D80(
            rChunk.field_4_xpos_or_compressed_size,
            rChunk.field_6_ypos,
            rChunk.field_8_width + rChunk.field_4_xpos_or_compressed_size - 1,
            rChunk.field_A_height + rChunk.field_6_ypos - 1,
            rChunk.field_2_layer_or_decompressed_size);
    }

    u8** Allocate(u32 len) override
    {
        // Shouldn't be called for this format
        return ResourceManager::Allocate_New_Locked_Resource_454F80(
            ResourceManager::Resource_PBuf,
            0,
            len);
    }

    void Deallocate(u8** ptr) override
    {
        // Shouldn't be called for this format
        ResourceManager::FreeResource_455550(ptr);
    }

private:
    FG1& mFg1;
    u32 mIdx = 0;
};

#ifdef TETHYS_SATURN
// SATURN: forensics for the OT code-0 abort seen at S4 -- distinguishes
// "block never filled by the reader" (bad at ctor exit) from "block
// corrupted between ctor and first VRender" (heap-side). Wired to the
// death screen via a named fatal in VRender below.
extern "C" volatile s32 Tethys_gFg1BadAtCtor;
volatile s32 Tethys_gFg1BadAtCtor = -1;
extern "C" volatile s32 Tethys_gFg1Ctors;
volatile s32 Tethys_gFg1Ctors = 0;
extern "C" volatile s32 Tethys_gFg1LastN;
volatile s32 Tethys_gFg1LastN = -1;
// SATURN: FG1 blocks skipped in VRender because vram_alloc failed (dense wall
// overflows the shared PSX VRAM model). >0 = incomplete foreground, not a crash.
extern "C" volatile s32 Tethys_gFg1Skipped;
volatile s32 Tethys_gFg1Skipped = 0;
extern "C" [[noreturn]] void Tethys_Fatal(const char_type* msg);

[[noreturn]] static void Tethys_Fg1Fatal(const char_type* what, s32 a, s32 b, s32 c, s32 d, s32 e)
{
    static char_type msg[44];
    char_type* p = msg;
    for (const char_type* s = what; *s; s++)
    {
        *p++ = *s;
    }
    const s32 vals[5] = {a, b, c, d, e};
    for (s32 v = 0; v < 5; v++)
    {
        *p++ = ' ';
        s32 x = vals[v];
        if (x < 0)
        {
            *p++ = '-';
            x = -x;
        }
        char_type d[10];
        s32 n = 0;
        do
        {
            d[n++] = static_cast<char_type>('0' + x % 10);
            x /= 10;
        } while (x);
        while (n)
        {
            *p++ = d[--n];
        }
    }
    *p = 0;
    Tethys_Fatal(msg);
}
#endif

static const Layer sFg1_layer_to_bits_layer_4BC024[] = {Layer::eLayer_FG1_37, Layer::eLayer_FG1_Half_18};

void FG1::Convert_Chunk_To_Render_Block_453BA0(const Fg1Chunk* pChunk, Fg1Block* pBlock)
{
    const s16 width_rounded = (pChunk->field_8_width + 1) & ~1u;
    if (vram_alloc_450860(pChunk->field_8_width, pChunk->field_A_height, &pBlock->field_58_rect))
    {
        pBlock->field_66_mapped_layer = sFg1_layer_to_bits_layer_4BC024[pChunk->field_2_layer_or_decompressed_size];

        PSX_RECT rect = {};
        rect.x = pBlock->field_58_rect.x;
        rect.y = pBlock->field_58_rect.y;
        rect.w = width_rounded;
        rect.h = pChunk->field_A_height;
        IRenderer::GetRenderer()->Upload(IRenderer::BitDepth::e16Bit, rect, (u8*) &pChunk[1]);

        const s16 tPage = static_cast<s16>(PSX_getTPage_4965D0(TPageMode::e16Bit_2, TPageAbr::eBlend_0, rect.x /*& 0xFFC0*/, rect.y));

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
            Poly_Set_SemiTrans_498A40(&rPoly.mBase.header, FALSE);
            Poly_Set_Blending_498A00(&rPoly.mBase.header, TRUE);

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
        }
    }
    else
    {
        pBlock->field_58_rect.w = 0;
    }
}
static const Layer sFg1_layer_to_bits_layer[] = {Layer::eLayer_Well_Half_4, Layer::eLayer_FG1_Half_18, Layer::eLayer_Well_23, Layer::eLayer_FG1_37};

void FG1::Convert_Chunk_To_Render_Block_AE(const Fg1Chunk* pChunk, Fg1Block* pBlock)
{
    // Map the layer from FG1 internal to OT layer
    pBlock->field_66_mapped_layer = sFg1_layer_to_bits_layer[pChunk->field_2_layer_or_decompressed_size];

    // Copy in the bits that represent the see through pixels
    memcpy(pBlock->field_68_array_of_height, &pChunk[1], pChunk->field_A_height * sizeof(u32));

    for (Poly_FT4& rPoly : pBlock->field_0_polys)
    {
        rPoly = {};

        PolyFT4_Init(&rPoly);

        Poly_Set_SemiTrans_498A40(&rPoly.mBase.header, FALSE);
        Poly_Set_Blending_498A00(&rPoly.mBase.header, TRUE);

        SetTPage(&rPoly, static_cast<u16>(PSX_getTPage_4965D0(TPageMode::e16Bit_2, TPageAbr::eBlend_0, 0, 0)));

        SetXYWH(&rPoly, pChunk->field_4_xpos_or_compressed_size, pChunk->field_6_ypos, pChunk->field_8_width, pChunk->field_A_height);

        SetPrimExtraPointerHack(&rPoly, pBlock->field_68_array_of_height);
    }
}

BaseGameObject* FG1::dtor_453DF0()
{
    SetVTable(this, 0x4BC028);

    gObjList_drawables_504618->Remove_Item(this);

    for (s32 i = 0; i < field_18_render_block_count; i++)
    {
        if (field_20_chnk_res[i].field_58_rect.w > 0)
        {
            Vram_free_450CE0(
                {field_20_chnk_res[i].field_58_rect.x, field_20_chnk_res[i].field_58_rect.y},
                {field_20_chnk_res[i].field_58_rect.w, field_20_chnk_res[i].field_58_rect.h});
        }
    }

    ResourceManager::FreeResource_455550(field_1C_ptr);
    return dtor_487DF0();
}

FG1* FG1::ctor_4539C0(u8** ppRes)
{
    ctor_487E10(1);

    SetVTable(this, 0x4BC028);

    field_6_flags.Set(Options::eDrawable_Bit4);
    field_6_flags.Set(Options::eSurviveDeathReset_Bit9);
    field_6_flags.Set(Options::eUpdateDuringCamSwap_Bit10);

    field_4_typeId = Types::eFG1_67;

    field_10_cam_pos_x = FP_GetExponent(pScreenManager_4FF7C8->field_10_pCamPos->field_0_x);
    field_12_cam_pos_y = FP_GetExponent(pScreenManager_4FF7C8->field_10_pCamPos->field_4_y);

    field_16_current_path = gMap_507BA8.field_2_current_path;
    field_14_current_level = gMap_507BA8.field_0_current_level;

    gObjList_drawables_504618->Push_Back(this);

    // Cast to the actual FG1 resource block format
    FG1ResourceBlockHeader* pHeader = reinterpret_cast<FG1ResourceBlockHeader*>(*ppRes);

    // Check if its relive format FG1
    bool isReliveFG1 = false;
    if (pHeader->mCount == ResourceManager::Resource_FG1)
    {
        // adjust past the new file magic
        pHeader = reinterpret_cast<FG1ResourceBlockHeader*>(*ppRes + sizeof(u32));

        isReliveFG1 = true;
    }

    field_18_render_block_count = static_cast<s16>(pHeader->mCount);

#ifdef TETHYS_SATURN
    // SATURN ROOT FIX (bt864): `pHeader` is a RAW pointer into the FG1 SOURCE
    // block, which on the CAM-streaming path is NON-locked (Alloc_New_Resource
    // in ResourceManager.cpp). The CHNK alloc just below -- and every per-chunk
    // PBuf alloc inside loader.Iterate() -- can fail on the memory-walled barrel
    // heap and run Reclaim_Memory, which COMPACTS: it memmoves the non-locked
    // source and the decompressor then reads/writes through the now-dangling
    // pointer, stomping whatever moved into its place (observed: Mine_Flash's
    // Header -> 0x1a57120f -> heap cascade -> ALL sprites gone from the
    // possession room + "Res missing" on the return to Abe). The CHNK and PBuf
    // blocks are already eLocked; the SOURCE is the one movable block, so LOCK
    // it for the construction and restore after. (PC's 5.12 MB heap never
    // Reclaims here -- no-op there.)
    ResourceManager::Header* pSrcHdr = ResourceManager::Get_Header_455620(ppRes);
    const s16 srcFlagsSaved = pSrcHdr->field_6_flags;
    pSrcHdr->field_6_flags |= ResourceManager::ResourceHeaderFlags::eLocked;
#endif

    field_1C_ptr = ResourceManager::Allocate_New_Locked_Resource_454F80(ResourceManager::Resource_CHNK, 0, pHeader->mCount * sizeof(Fg1Block));
#ifdef TETHYS_SATURN
    // SATURN: on a full resource heap this alloc returns null and the
    // deref below writes 21 render blocks through *nullptr (wild writes;
    // cost a full S4 forensics chain). PC's 5.12 MB heap never fails here.
    if (!field_1C_ptr)
    {
        pSrcHdr->field_6_flags = srcFlagsSaved; // restore before dying
        Tethys_Fatal("FG1 CHNK alloc failed");
    }
#endif
    field_20_chnk_res = reinterpret_cast<Fg1Block*>(*field_1C_ptr);

    if (isReliveFG1)
    {
        FG1ReaderAE loader(*this);
        loader.Iterate(pHeader);
    }
    else
    {
        FG1Reader loader(*this);
        loader.Iterate(pHeader);
    }

#ifdef TETHYS_SATURN
    pSrcHdr->field_6_flags = srcFlagsSaved; // restore the source's original lock state
#endif

#ifdef TETHYS_SATURN
    // How many render blocks left the ctor WITHOUT an initialized poly
    // (code 0 = the reader never filled the slot, or vram_alloc failed).
    Tethys_gFg1Ctors++;
    Tethys_gFg1LastN = field_18_render_block_count;
    Tethys_gFg1BadAtCtor = 0;
    for (s32 i = 0; i < field_18_render_block_count; i++)
    {
        if (field_20_chnk_res[i].field_0_polys[0].mBase.header.rgb_code.code_or_pad == 0)
        {
            Tethys_gFg1BadAtCtor++;
        }
    }
#endif

    return this;
}

BaseGameObject* FG1::VDestructor(s32 flags)
{
    return Vdtor_453E90(flags);
}

void FG1::VUpdate()
{
    // Empty
}

void FG1::VScreenChanged()
{
    VScreenChanged_453DE0();
}

void FG1::VScreenChanged_453DE0()
{
    field_6_flags.Set(BaseGameObject::eDead_Bit3);
}

void FG1::VRender(PrimHeader** ppOt)
{
    VRender_453D50(ppOt);
}

void FG1::VRender_453D50(PrimHeader** ppOt)
{
    for (s32 i = 0; i < field_18_render_block_count; i++)
    {
        Fg1Block* pBlock = &field_20_chnk_res[i];
        // AE blocks don't have a vram alloc
        //if (pBlock->field_58_rect.w > 0)
        {
            Poly_FT4* pPoly = &pBlock->field_0_polys[gPsxDisplay_504C78.field_A_buffer_index];

#ifdef TETHYS_SATURN
            // SATURN: name the faulting block before the generic OT trap
            // fires -- "FG1 <blk> <count> <badAtCtor>": badAtCtor 0 means
            // the poly was VALID at ctor exit and something corrupted it
            // between ctor and this first render.
            // "FG1 <blk> <nThis> <rect.w> <ctorCalls> <nLastCtor>":
            // rect.w 0 = vram_alloc failed for this very block; >0 with a
            // zero code = corrupted after ctor. ctorCalls>1 = several FG1
            // objects were built (bad/lastN then describe the LATEST one).
            if (pPoly->mBase.header.rgb_code.code_or_pad == 0)
            {
                // SATURN: code 0 = uninitialized poly. rect.w == 0 means
                // vram_alloc FAILED for this block -- a dense FG1 wall (barrel
                // walls) overflows the shared 1024x512 PSX VRAM model, unlike
                // Animation there is no fallback. SKIP it (draw the wall minus
                // the blocks that did not fit) instead of feeding a garbage poly
                // to the OT (-> wild OT write / crash). rect.w > 0 with code 0 is
                // a genuine post-ctor corruption -> keep the forensic fatal.
                if (pBlock->field_58_rect.w == 0)
                {
                    Tethys_gFg1Skipped++;
                    continue;
                }
                Tethys_Fg1Fatal("FG1", i, field_18_render_block_count,
                                pBlock->field_58_rect.w, Tethys_gFg1Ctors, Tethys_gFg1LastN);
            }
#endif
            OrderingTable_Add_498A80(OtLayer(ppOt, pBlock->field_66_mapped_layer), &pPoly->mBase.header);

            pScreenManager_4FF7C8->InvalidateRect_406E40(
                X0(pPoly),
                Y0(pPoly),
                X3(pPoly),
                Y3(pPoly),
                pScreenManager_4FF7C8->field_2E_idx);
        }
    }
}

FG1* FG1::Vdtor_453E90(s32 flags)
{
    dtor_453DF0();
    if (flags & 1)
    {
        ao_delete_free_447540(this);
    }
    return this;
}

} // namespace AO
