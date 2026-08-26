#include "stdafx_ao.h"
#include "ScreenManager.hpp"
#include "Function.hpp"
#include "ResourceManager.hpp"
#include "VRam.hpp"
#include "stdlib.hpp"
#include "../AliveLibAE/Renderer/IRenderer.hpp"

#undef min
#undef max

#ifdef TETHYS_SATURN
// SATURN: VDP2 CAM upload, defined in src/renderer_saturn.cxx (global
// namespace). The converted Bits payload is one Saturn-native blob
// (tools/converter/cam.py): u16 BE w=320, u16 BE h=224, 256 x u16 BE Saturn
// CRAM entries (0x0000 transparent, else |0x8000), then 320*240 8bpp indices.
// Must consume the blob synchronously: it lives in the resource heap and
// Reclaim_Memory_455660 memmoves heap chunks.
void Tethys_UploadCamBlob(const u8* pBlob);
#endif

#ifdef TETHYS_SATURN
#include "Map.hpp" // SATURN: gMap_507BA8 for the post-upload CAM blob free
#endif

namespace AO {

ALIVE_VAR(1, 0x4FF7C8, ScreenManager*, pScreenManager_4FF7C8, nullptr);
ALIVE_ARY(1, 0x4FC8A8, SprtTPage, 300, sSpriteTPageBuffer_4FC8A8, {});

#ifdef TETHYS_SATURN
ALIVE_VAR_EXTERN(Map, gMap_507BA8); // defined Map.cpp:330
#endif

Camera* Camera::ctor_4446E0()
{
    field_0_array.ctor_4043E0(10);
    field_30_flags &= ~1u;
    field_C_ppBits = nullptr;
    return this;
}

void Camera::dtor_444700()
{
    ResourceManager::FreeResource_455550(field_C_ppBits);

    for (s32 i = 0; i < field_0_array.Size(); i++)
    {
        u8** ppRes = field_0_array.ItemAt(i);
        if (!ppRes)
        {
            break;
        }

        ResourceManager::FreeResource_455550(ppRes);
        i = field_0_array.RemoveAt(i);
    }

    field_0_array.dtor_404440();
}


#ifdef TETHYS_SATURN
// SATURN: upload the camera's CAM blob to VDP2 VRAM and free the heap copy
// (72,212 B). The PC keeps the blob for dirty-rect Sprt recomposition, which
// the Saturn renderer drops, so once the pixels live in VDP2 the heap copy
// is dead weight. Frees BOTH refs (the LoadingFile state-4
// Move_Resources_To_DArray push into field_0_array + On_Loaded's
// GetLoadedResource(+1) into field_C_ppBits) and nulls the cached handle so
// Camera::dtor_444700's own FreeResource(field_C_ppBits) no-ops
// (FreeResource_455550 is null-safe). Idempotent: a second call finds
// field_C_ppBits null and returns.
static void Tethys_ConsumeCamBlob(Camera* pCam)
{
    if (!pCam || !pCam->field_C_ppBits || !*pCam->field_C_ppBits)
    {
        return;
    }

    Tethys_UploadCamBlob(*pCam->field_C_ppBits);

    // Clear the same dirty sets the PC path cleared after a full redraw
    // (boot: the ScreenManager does not exist yet at the first consume).
    if (pScreenManager_4FF7C8)
    {
        pScreenManager_4FF7C8->field_58_20x16_dirty_bits[0] = {};
        pScreenManager_4FF7C8->field_58_20x16_dirty_bits[1] = {};
        pScreenManager_4FF7C8->field_58_20x16_dirty_bits[2] = {};
        pScreenManager_4FF7C8->field_58_20x16_dirty_bits[3] = {};
    }

    for (s32 i = 0; i < pCam->field_0_array.Size(); i++)
    {
        u8** ppRes = pCam->field_0_array.ItemAt(i);
        if (!ppRes)
        {
            break;
        }
        if (ppRes == pCam->field_C_ppBits)
        {
            ResourceManager::FreeResource_455550(ppRes);
            pCam->field_0_array.RemoveAt(i);
            break;
        }
    }
    ResourceManager::FreeResource_455550(pCam->field_C_ppBits);
    pCam->field_C_ppBits = nullptr;
}
#endif

void CC Camera::On_Loaded_4447A0(Camera* pThis)
{
    pThis->field_30_flags |= 1u;
    pThis->field_C_ppBits = ResourceManager::GetLoadedResource_4554F0(ResourceManager::Resource_Bits, pThis->field_10_resId, 1, 0);
#ifdef TETHYS_SATURN
    // SATURN: consume the blob THE MOMENT it arrives, not at flip end. Field
    // round 2 wedged at bp5 with the fresh CAM (B-type, 72,212 B) resident
    // while the rest of the flip's queue staged behind it (us 1,002,396 /
    // 1,024,000, an 18-sector file stuck at state 0): waiting for the
    // CameraSwapper's DecompressCameraToVRam at the END of GoTo_Camera
    // withholds the +72 K exactly when the load needs it. On_Loaded fires
    // from LoadingFile state 5, BEFORE any later file allocates, so freeing
    // here hands the hole to the very next staging. On Saturn this callback
    // only ever runs for the active camera (the S4 patch nulls all neighbor
    // slots, Map.cpp GoTo_Camera), but keep the guard for shape-safety; the
    // DecompressCameraToVRam path stays as backstop for the blocking-load
    // route (LoadResourceFile_455270) which sets field_C without On_Loaded.
    if (pThis == gMap_507BA8.field_34_camera_array[0])
    {
        Tethys_ConsumeCamBlob(pThis);
    }
#endif
}

void ScreenManager::MoveImage_406C40()
{
    PSX_RECT rect = {};
    rect.x = field_20_upos;
    rect.y = field_22_vpos;
    rect.h = 240;
    rect.w = 640;
    PSX_MoveImage_4961A0(&rect, 0, 0);
}

void ScreenManager::DecompressCameraToVRam_407110(u16** ppBits)
{
#ifdef TETHYS_SATURN
    // SATURN: the converter replaced the PC Bits payload (u16 strip lengths +
    // 40 x 16x240 RGB565 strips) with the Saturn blob above -- the strip walk
    // below would misparse it. Hand the whole payload to the VDP2 NBG1
    // backend (P3_DESIGN D5) and clear the same dirty sets the PC path
    // cleared after a full redraw. The ~300 recomposition Sprts this class
    // keeps emitting from VRender_406A60 are dropped by the renderer
    // (background-tpage decode), so the dirty-bit machinery stays verbatim.
    // SATURN: normally the blob was already consumed at arrival (see
    // Tethys_ConsumeCamBlob + the On_Loaded_4447A0 hook above), so every
    // caller of this function (boot runs it twice: Init_4068A0 then
    // CameraSwapper Init_48C830 re-reading the nulled field_C_ppBits; Abe's
    // movie-done restore Abe.cpp eHandstoneMovieDone_2 is a third) arrives
    // with a null or stale handle -- skip, the VDP2 bitmap still holds the
    // image. Without the guard those calls dereference nullptr (address 0 =
    // BIOS vector table on SH-2, no MMU) and die on the blob header check.
    // The consume below only fires on the blocking-load route
    // (LoadResourceFile_455270 sets field_C_ppBits without On_Loaded).
    Camera* pCam = gMap_507BA8.field_34_camera_array[0];
    if (!ppBits || !pCam || pCam->field_C_ppBits != reinterpret_cast<u8**>(ppBits))
    {
        return;
    }
    Tethys_ConsumeCamBlob(pCam);
    return;
#else
    PSX_RECT rect = {0, 0, 16, 240};
    u8** pRes = ResourceManager::Alloc_New_Resource_454F20(ResourceManager::Resource_VLC, 0, 0x7E00); // 4 KB
    if (pRes)
    {
        // Doesn't do anything since the images are not MDEC compressed in PC
        // PSX_MDEC_rest_498C30(0);

        u16* pIter = *ppBits;
        for (s16 xpos = 0; xpos < 640; xpos += 16)
        {
            const u16 slice_len = *pIter;
            pIter++; // Skip len

            // already in correct format - no need to convert
            // rgb_conv_44FFE0(pIter, tmpBuffer, sizeof(tmpBuffer));

            rect.x = field_20_upos + xpos;
            rect.y = field_22_vpos;

            // TODO: Actually 16bit but must be uploaded as 8bit ??
            IRenderer::GetRenderer()->Upload(IRenderer::BitDepth::e8Bit, rect, reinterpret_cast<u8*>(pIter));

            // To next slice
            pIter += (slice_len / sizeof(s16));
        }

        ResourceManager::FreeResource_455550(pRes);

        field_58_20x16_dirty_bits[0] = {};
        field_58_20x16_dirty_bits[1] = {};
        field_58_20x16_dirty_bits[2] = {};
        field_58_20x16_dirty_bits[3] = {};
    }
#endif
}

void ScreenManager::InvalidateRect_406CC0(s32 x, s32 y, s32 width, s32 height)
{
    InvalidateRect_406E40(x, y, width, height, field_2E_idx);
}

ScreenManager* ScreenManager::ctor_406830(u8** ppBits, FP_Point* pCameraOffset)
{
    ctor_487E10(1);
    SetVTable(this, 0x4BA230);

    field_10_pCamPos = pCameraOffset;

    field_6_flags.Set(Options::eSurviveDeathReset_Bit9);
    field_6_flags.Set(Options::eUpdateDuringCamSwap_Bit10);

    Init_4068A0(ppBits);
    return this;
}

void ScreenManager::Init_4068A0(u8** ppBits)
{
    field_36_flags |= 1;

    field_4_typeId = Types::eScreenManager_4;

    field_14_xpos = 184;
    field_16_ypos = 120;
    field_20_upos = 0;
    field_22_vpos = 272;
    field_24_cam_width = 640;
    field_26_cam_height = 240;

    Vram_alloc_explicit_4507F0(0, 272, 640, 512);
    DecompressCameraToVRam_407110(reinterpret_cast<u16**>(ppBits));

    field_18_screen_sprites = &sSpriteTPageBuffer_4FC8A8[0];

    s16 xpos = 0;
    s16 ypos = 0;
    for (s32 i = 0; i < 300; i++)
    {
        SprtTPage* pItem = &field_18_screen_sprites[i];
        Sprt_Init(&pItem->mSprt);
        SetRGB0(&pItem->mSprt, 128, 128, 128);
        SetXY0(&pItem->mSprt, xpos, ypos);

        pItem->mSprt.field_14_w = 32;
        pItem->mSprt.field_16_h = 16;

        s32 u0 = field_20_upos + 32 * (i % 20);
        s32 v0 = field_22_vpos + 16 * (i / 20);
        s32 tpage = ScreenManager::GetTPage(TPageMode::e16Bit_2, TPageAbr::eBlend_0, &u0, &v0);

        tpage |= 0x8000;

        Init_SetTPage_495FB0(&pItem->mTPage, 0, 0, tpage);

        SetUV0(&pItem->mSprt, static_cast<u8>(u0), static_cast<u8>(v0));

        xpos += 32;
        if (xpos == 640)
        {
            xpos = 0;
            ypos += 16;
        }
    }

    for (s32 i = 0; i < 6; i++)
    {
        memset(&field_58_20x16_dirty_bits[i], 0, sizeof(field_58_20x16_dirty_bits[0]));
    }

    field_2E_idx = 2;
    field_30_y_idx = 1;
    field_32_x_idx = 0;
}


BaseGameObject* ScreenManager::VDestructor(s32 flags)
{
    return vdtor_407290(flags);
}
void ScreenManager::UnsetDirtyBits_FG1_406EF0()
{
    memset(&field_58_20x16_dirty_bits[4], 0, sizeof(this->field_58_20x16_dirty_bits[4]));
    memset(&field_58_20x16_dirty_bits[5], 0, sizeof(this->field_58_20x16_dirty_bits[5]));
}

void ScreenManager::InvalidateRect_406E40(s32 x, s32 y, s32 width, s32 height, s32 idx)
{
#ifdef TETHYS_SATURN
    // SATURN (ao262.18) -- THE WRITER OUTLIVED ITS READER, and that is the whole
    // justification. ao242.12 gated the dirty-rectangle recomposition in
    // VRender_406A60 behind `if (false)`; every GetTile in this file lives
    // between lines 398 and 416, i.e. INSIDE that gated loop. Enumerated, not
    // sampled: a grep of field_58_20x16_dirty_bits across AliveLibAO returns
    // ScreenManager.cpp only, and every read of it is in the dead loop. So these
    // bits are computed every frame for nobody.
    //
    // MEASURED (ao262.19), not estimated -- and the first draft of this comment
    // guessed the generated code wrong, so both corrections live here.
    //
    // THE WIN: -0.3 ms on `v` per tick, c7, A/B matched on tn052, ao262.18
    // against ao262.99 (this function restored, one #ifdef, nothing else). Seven
    // control columns identical TO THE DIGIT -- w0074, p0064, q0017, r0010,
    // b0004, N043, f0019 -- while v went 0036 -> 0033 and y went 0176 -> 0179.
    // The time did not vanish, it moved from work into the idle wait, which is
    // the signature that makes it a measurement instead of a difference: the
    // screen was pinned at h100/00/00/00, so `t` could not move and did not.
    // eRope_73 alone gave 0.1 ms; the other 0.2 is spread over ~19 drawables,
    // consistent with ~36 call sites each paying a little. The A/B capture even
    // carried one drawable MORE (n020 vs n019), so the figure is biased low.
    //
    // THE CORRECTION: the first draft said the four signed divides would each be
    // a libgcc call. Read at ao262.99's disassembly they are NOT -- GCC emitted
    // inline `shar` chains (5 for /32, 4 for /16) plus the sign-rounding
    // branches. What it did emit, and the draft missed, is ONE libgcc call PER
    // TILE inside the loop: `jsr @r9` -> ___ashlsi3_r0 for SetTile's mask, next
    // to a read-modify-write halfword. 0xc0 bytes of code against 0x4 for this
    // neutralised form. bt1136 again: an optimisation is read in the generated
    // code, never in the source -- including when the source is a comment.
    //
    // It is a PURE DELETION with nothing traded against it, which is the only
    // shape of change this port ships since ao242.13 measured what a trade costs.
    //
    // The state is left declared and the memsets/merges are untouched, exactly as
    // the ao242.12 note at VRender_406A60 asks: a future reader would need the
    // invariant, and removing state is not what this change is for.
    (void) x; (void) y; (void) width; (void) height; (void) idx;
    return;
#else
    x = std::max(x, 0);
    y = std::max(y, 0);

    width = std::min(width, 639);
    height = std::min(height, 239);

    for (s32 tileX = x / 32; tileX <= width / 32; tileX++)
    {
        for (s32 tileY = y / 16; tileY <= height / 16; tileY++)
        {
            field_58_20x16_dirty_bits[idx].SetTile(tileX, tileY, true);
        }
    }
#endif
}

void ScreenManager::InvalidateRect_Layer3_406F20(s32 x, s32 y, s32 width, s32 height)
{
    InvalidateRect_406E40(x, y, width, height, 3);
}


void ScreenManager::InvalidateRect_406D80(s32 x, s32 y, s32 width, s32 height, s32 idx)
{
    InvalidateRect_406E40(x, y, width, height, idx + 4);
}

void ScreenManager::VScreenChanged()
{
    // Empty
}

void ScreenManager::VUpdate()
{
    // Empty
}


s32 ScreenManager::GetTPage(TPageMode tp, TPageAbr abr, s32* xpos, s32* ypos)
{
    const s16 clampedYPos = *ypos & 0xFF00;
    const s16 clampedXPos = *xpos & 0xFFC0;
    *xpos -= clampedXPos;
    *ypos -= clampedYPos;
    return PSX_getTPage_4965D0(tp, abr, clampedXPos, clampedYPos);
}

void ScreenManager::VRender(PrimHeader** ppOt)
{
    VRender_406A60(ppOt);
}

void ScreenManager::VRender_406A60(PrimHeader** ppOt)
{
    if (!(field_36_flags & 1)) // Render enabled flag ?
    {
        return;
    }

    PSX_DrawSync_496750(0);

#ifdef TETHYS_SATURN
    // SATURN (ao242.12) THE PSX DIRTY-RECTANGLE RECOMPOSITION IS DEAD WEIGHT HERE,
    // and it was the most expensive dead weight in the frame.
    //
    // WHAT THIS LOOP IS. On PSX the background is a 640x240 area of the frame
    // buffer and damaged 32x16 tiles are re-blitted every frame as sprites: 300
    // tiles (20 x 15), five dirty-bit sets tested per tile, two OT primitives
    // added per surviving tile. On Saturn the background is not a frame buffer at
    // all -- it is a VDP2 NBG1 bitmap uploaded once per screen by
    // Tethys_UploadCamBlob and displayed whole -- so there is nothing to repair.
    //
    // WHAT IT COST, measured (docs/FRAME_BUDGET.md, ao242.11, real hardware):
    // 3.3-3.8 ms of EVERY tick of EVERY screen, and largest on the emptiest one.
    // On a screen drawing two textured rects it was 3.8 of the 4.5 ms render
    // phase -- 84 %. It then costs a second time: the primitives it emits are
    // walked in the ordering table before SaturnRenderer::Draw(Prim_Sprt&)
    // recognises them by their modal tpage and discards them ("the VDP2 CAM is
    // displayed whole, these are pure duplication"). That discard is the `p`
    // column, 111-170 a tick on every screen including the two-rect one.
    //
    // WHY SKIPPING IT CANNOT CHANGE A PIXEL. The renderer already threw every one
    // of these primitives away, so not producing them is an identity rather than
    // an approximation. And the state they read is write-only otherwise: grepping
    // AO for field_58_20x16_dirty_bits finds initialisers, memsets and SetTile --
    // this loop is the ONLY reader in the codebase.
    //
    // The bookkeeping below (sub_406FF0's index rotation, the OR, the memset) is
    // deliberately KEPT. It is ~40 stores, it holds the invariant that a future
    // reader would need, and removing state is not what this change is for.
    if (false)
#endif
    for (s32 i = 0; i < 300; i++)
    {
        SprtTPage* pSpriteTPage = &field_18_screen_sprites[i];

        const s32 spriteX = pSpriteTPage->mSprt.mBase.vert.x;
        const s32 spriteY = pSpriteTPage->mSprt.mBase.vert.y;

        Layer layer = Layer::eLayer_0;
        if (field_58_20x16_dirty_bits[4].GetTile(spriteX / 32, spriteY / 16))
        {
            if (!(field_58_20x16_dirty_bits[field_2E_idx].GetTile(spriteX / 32, spriteY / 16)) && !(field_58_20x16_dirty_bits[field_30_y_idx].GetTile(spriteX / 32, spriteY / 16)) && !(field_58_20x16_dirty_bits[field_32_x_idx].GetTile(spriteX / 32, spriteY / 16)) && !(field_58_20x16_dirty_bits[3].GetTile(spriteX / 32, spriteY / 16)))
            {
                continue;
            }
            layer = Layer::eLayer_FG1_37;
        }
        else if (field_58_20x16_dirty_bits[5].GetTile(spriteX / 32, spriteY / 16))
        {
            if (!(field_58_20x16_dirty_bits[field_2E_idx].GetTile(spriteX / 32, spriteY / 16)) && !(field_58_20x16_dirty_bits[field_30_y_idx].GetTile(spriteX / 32, spriteY / 16)) && !(field_58_20x16_dirty_bits[field_32_x_idx].GetTile(spriteX / 32, spriteY / 16)) && !(field_58_20x16_dirty_bits[3].GetTile(spriteX / 32, spriteY / 16)))
            {
                continue;
            }
            layer = Layer::eLayer_FG1_Half_18;
        }
        else
        {
            if (!(field_58_20x16_dirty_bits[field_32_x_idx].GetTile(spriteX / 32, spriteY / 16)) && !(field_58_20x16_dirty_bits[field_30_y_idx].GetTile(spriteX / 32, spriteY / 16)) && !(field_58_20x16_dirty_bits[3].GetTile(spriteX / 32, spriteY / 16)))
            {
                continue;
            }
            layer = Layer::eLayer_1;
        }

        OrderingTable_Add_498A80(OtLayer(ppOt, layer), &pSpriteTPage->mSprt.mBase.header);
        OrderingTable_Add_498A80(OtLayer(ppOt, layer), &pSpriteTPage->mTPage.mBase);
    }

    sub_406FF0();

    for (s32 i = 0; i < 20; i++)
    {
        field_58_20x16_dirty_bits[field_32_x_idx].mData[i] |= field_58_20x16_dirty_bits[3].mData[i];
    }

    memset(&field_58_20x16_dirty_bits[3], 0, sizeof(field_58_20x16_dirty_bits[3]));
    return;
}

void ScreenManager::sub_406FF0()
{
    // NOTE: The algorithm calling Add_Dirty_Area_48D910 has not been implemented
    // as its not actually used.

    field_32_x_idx = field_30_y_idx;
    field_30_y_idx = field_2E_idx;
    field_2E_idx = (field_2E_idx + 1) % 3;
    memset(
        &field_58_20x16_dirty_bits[field_2E_idx],
        0,
        sizeof(field_58_20x16_dirty_bits[field_2E_idx]));
}

ScreenManager* ScreenManager::vdtor_407290(s32 flags)
{
    dtor_487DF0();
    if (flags & 1)
    {
        ao_delete_free_447540(this);
    }
    return this;
}

} // namespace AO
