#include "stdafx_ao.h"
#include "Function.hpp"
#include "VRam.hpp"
#include "../AliveLibAE/VRam.hpp"
#include "Renderer/IRenderer.hpp"

namespace AO {


EXPORT void CC Pal_Reset_4476C0(u16 /*a1*/, u16 /*a2*/)
{
    Pal_Area_Init_483080(0, 240, 640, 32);
}


EXPORT s16 CC Pal_Allocate_4476F0(PSX_RECT* pRect, u32 paletteColorCount)
{
    return Pal_Allocate_483110(pRect, paletteColorCount);
}

EXPORT void CC Pal_Free_447870(PSX_Point xy, s16 palDepth)
{
    Pal_free_483390(xy, palDepth);
}


void CC Pal_Copy_4479D0(PSX_Point point, s16 w, u16* pPalData, PSX_RECT* rect)
{
    rect->y = point.field_2_y;
    rect->x = point.field_0_x;
    rect->w = w;
    rect->h = 1;
    PSX_StoreImage_496320(rect, pPalData);
}

void CC Pal_Set_447990(PSX_Point xy, s16 w, const u8* palData, PSX_RECT* rect)
{
    rect->y = xy.field_2_y;
    rect->x = xy.field_0_x;
    rect->w = w;
    rect->h = 1;
    IRenderer::GetRenderer()->PalSetData(IRenderer::PalRecord{xy.field_0_x, xy.field_2_y, w}, palData);
}

u32 CC Pal_Make_Colour_447950(u8 r, u8 g, u8 b, s16 bOpaque)
{
    AE_IMPLEMENTED();
    return Pal_Make_Colour_4834C0(r, g, b, bOpaque);
}

EXPORT void CC Vram_alloc_explicit_4507F0(s16 x, s16 y, s16 w, s16 h)
{
    AE_IMPLEMENTED();
    Vram_alloc_explicit_4955F0(x, y, w, h);
}

EXPORT void CC Vram_reset_450840()
{
    AE_IMPLEMENTED();
    Vram_init_495660();
}

void CC Vram_free_450CE0(PSX_Point xy, PSX_Point wh)
{
    AE_IMPLEMENTED();
    Vram_free_495A60(xy, wh);
}

#ifdef TETHYS_SATURN
// SATURN: forensics for the garbage VRAM rect (1024,0,0,-15616) seen on the
// S4 death screens -- h == 0xC300 is 195 byte-swapped, i.e. somebody fed
// this allocator dimensions read raw from little-endian data (Font atlas
// suspect: FNT payloads are deliberately left LE until S8). Record-only so
// boot continues; the death screen prints hits/w/h/caller (sys_saturn row).
extern "C" volatile u32 Tethys_gVramBad[4] = {}; // hits, last w, last h, last caller RA
static inline void Tethys_NoteVramDims(s32 w, s32 h, u32 ra)
{
    if (w <= 0 || h <= 0 || w > 1024 || h > 512)
    {
        Tethys_gVramBad[0] = Tethys_gVramBad[0] + 1;
        Tethys_gVramBad[1] = static_cast<u32>(w);
        Tethys_gVramBad[2] = static_cast<u32>(h);
        Tethys_gVramBad[3] = ra;
    }
}
#endif

s16 CC vram_alloc_450B20(u16 width, s16 height, u16 colourDepth, PSX_RECT* pRect)
{
    AE_IMPLEMENTED();
#ifdef TETHYS_SATURN
    Tethys_NoteVramDims(width, height, reinterpret_cast<u32>(__builtin_return_address(0)));
#endif
    return Vram_alloc_4956C0(width, height, colourDepth, pRect);
}

EXPORT s32 CC vram_alloc_450860(s16 width, s16 height, PSX_RECT* pRect)
{
    AE_IMPLEMENTED();
#ifdef TETHYS_SATURN
    Tethys_NoteVramDims(width, height, reinterpret_cast<u32>(__builtin_return_address(0)));
#endif
    return Vram_alloc_4956C0(width, height, 16, pRect);
}

} // namespace AO
