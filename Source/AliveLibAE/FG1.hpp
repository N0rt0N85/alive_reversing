#pragma once

#include "../AliveLibCommon/FunctionFwd.hpp"
#include "BaseGameObject.hpp"
#include "DynamicArray.hpp"
#include "Primitives.hpp"

#ifdef TETHYS_SATURN
// SATURN (decision D-R2, U1a A6): AO's pixel-path block (AliveLibAO/FG1.cpp:23).
// A piece is uploaded to a PSX VRAM-model slot (field_58_rect, w 0 = refused)
// and drawn by its two textured quads; the PC's 64-byte transparency mask
// (field_68_array_of_height) has no reader on this path and goes: 0x68 B a
// piece instead of 0xA8 (BRP19C05: 94 pieces, 3,760 B less CHNK).
struct Fg1Block final
{
    Poly_FT4 field_0_polys[2];
    PSX_RECT field_58_rect;
    s32 field_60_padding;
    s16 field_64_padding;
    Layer field_66_mapped_layer;
};
ALIVE_ASSERT_SIZEOF(Fg1Block, 0x68);
#else
struct Fg1Block final
{
    Poly_FT4 field_0_polys[2];
    s32 field_58_padding;
    s32 field_5C_padding;
    s32 field_60_padding;
    s16 field_64_padding;
    Layer field_66_mapped_layer;
    u32 field_68_array_of_height[16];
};
ALIVE_ASSERT_SIZEOF(Fg1Block, 0xA8);
#endif

struct Fg1Chunk;
enum class LevelIds : s16;

class FG1 final : public BaseGameObject
{
public:
    EXPORT FG1* ctor_499FC0(u8** pFg1Res);

    virtual BaseGameObject* VDestructor(s32 flags) override;
    virtual void VRender(PrimHeader** ppOt) override;
    virtual void VUpdate() override
    { }
    virtual void VScreenChanged() override;

    EXPORT void vScreenChanged_49A520();
    EXPORT void vRender_49A3C0(PrimHeader** ppOt);
    EXPORT BaseGameObject* vdtor_49A1E0(s32 flags);
    EXPORT void dtor_49A540();
    EXPORT s16 Convert_Chunk_To_Render_Block_49A210(const Fg1Chunk* pChunk, Fg1Block* pBlock);

    s32 field_20_unused;
    LevelIds field_24_level_id;
    s16 field_26_path_id;
    s16 field_28_render_block_count;
    //s16 field_2A;
    u8** field_2C_ptr;
    Fg1Block* field_30_chnk_res;
};
ALIVE_ASSERT_SIZEOF(FG1, 0x34);

ALIVE_VAR_EXTERN(DynamicArrayT<FG1>*, gFG1List_5D1E28);
