#include "stdafx.h"
#include "Primitives.hpp"
#include "Function.hpp"
#include "stdlib.hpp"
#include "Game.hpp"

#ifdef TETHYS_SATURN
// SATURN: named death handler (src/main.cxx) for the OT debug trap below.
extern "C" [[noreturn]] void Tethys_Fatal(const char_type* msg);
#endif

void Primitives_ForceLink()
{ }


void CC Init_SetTPage_4F5B60(Prim_SetTPage* pPrim, s32 /*notUsed1*/, s32 /*notUsed2*/, s32 tpage)
{
    SetUnknown(&pPrim->mBase);
    SetCode(&pPrim->mBase, PrimTypeCodes::eSetTPage);
    pPrim->field_C_tpage = tpage;
}

void CC Init_PrimClipper_4F5B80(Prim_PrimClipper* pPrim, const PSX_RECT* pClipRect)
{
    SetUnknown(&pPrim->mBase);
    SetCode(&pPrim->mBase, PrimTypeCodes::ePrimClipper);
    pPrim->field_C_x = pClipRect->x;
    pPrim->field_E_y = pClipRect->y;
    pPrim->mBase.header.mRect.w = pClipRect->w;
    pPrim->mBase.header.mRect.h = pClipRect->h;
}

void CC InitType_ScreenOffset_4F5BB0(Prim_ScreenOffset* pPrim, const PSX_Pos16* pOffset)
{
    SetUnknown(&pPrim->mBase);
    SetCode(&pPrim->mBase, PrimTypeCodes::eScreenOffset);
    pPrim->field_C_xoff = pOffset->x;
    pPrim->field_E_yoff = pOffset->y;
}


void CC Sprt_Init_4F8910(Prim_Sprt* pPrim)
{
    SetNumLongs(&pPrim->mBase.header, 4);
    SetUnknown(&pPrim->mBase.header);
    SetCode(&pPrim->mBase.header, PrimTypeCodes::eSprt);
}



void CC PolyG3_Init_4F8890(Poly_G3* pPrim)
{
    SetNumLongs(&pPrim->mBase.header, 6);
    SetUnknown(&pPrim->mBase.header);
    SetCode(&pPrim->mBase.header, PrimTypeCodes::ePolyG3);
}

void CC PolyG4_Init_4F88B0(Poly_G4* pPrim)
{
    SetNumLongs(&pPrim->mBase.header, 8);
    SetUnknown(&pPrim->mBase.header);
    SetCode(&pPrim->mBase.header, PrimTypeCodes::ePolyG4);
}

void CC PolyF4_Init_4F8830(Poly_F4* pPrim)
{
    SetNumLongs(&pPrim->mBase.header, 5);
    SetUnknown(&pPrim->mBase.header);
    SetCode(&pPrim->mBase.header, PrimTypeCodes::ePolyF4);
}

void PolyGT4_Init(Poly_GT4* pPrim)
{
    SetNumLongs(&pPrim->mBase.header, 99); // TODO: Num longs never used by anything?
    SetUnknown(&pPrim->mBase.header);
    SetCode(&pPrim->mBase.header, PrimTypeCodes::ePolyGT4);
}

void CC Poly_FT4_Get_Rect_409DA0(PSX_RECT* pRect, const Poly_FT4* pPoly)
{
    if (PSX_Prim_Code_Without_Blending_Or_SemiTransparency(pPoly->mBase.header.rgb_code.code_or_pad) == PrimTypeCodes::ePolyFT4)
    {
        pRect->x = pPoly->mBase.vert.x;
        pRect->y = pPoly->mBase.vert.y;
        pRect->w = pPoly->mVerts[2].mVert.x;
        pRect->h = pPoly->mVerts[2].mVert.y;
    }
    else
    {
        pRect->h = 0;
        pRect->w = 0;
        pRect->y = 0;
        pRect->x = 0;
    }
}

void CC Poly_Set_Blending_4F8A20(PrimHeader* pPrim, s32 bFlag1)
{
    SetUnknown(pPrim);
    if (bFlag1)
    {
        pPrim->rgb_code.code_or_pad |= 1;
    }
    else
    {
        pPrim->rgb_code.code_or_pad &= ~1;
    }
}

void CC Poly_Set_SemiTrans_4F8A60(PrimHeader* pPrim, s32 bSemiTrans)
{
    SetUnknown(pPrim);
    if (bSemiTrans)
    {
        pPrim->rgb_code.code_or_pad |= 2;
    }
    else
    {
        pPrim->rgb_code.code_or_pad &= ~2;
    }
}

#ifdef TETHYS_SATURN
// SATURN: is p a plausible OT node? HWRAM/LWRAM cached windows, 4-aligned
// (mirrors src/renderer_saturn.cxx IsOtNodePlausible -- keep in sync).
static bool Tethys_OtPtrOk(u32 a)
{
    if (a & 3)
    {
        return false;
    }
    // SATURN S8: the resource heap can live in the cart's cached window now, so
    // a resource-heap-allocated prim (Animation dbuf poly, FG1 chunk, sprite
    // dbuf) is a LEGIT cart address -- accept it (same range as Tethys_PtrSane,
    // ResourceManager.cpp). This guard was NOT updated when S8 moved the heap
    // to the cart: the first heavy cart screen queued a valid poly at 0x024xxxxx
    // and it read here as a wild tag -> the "OTa 026d27c0 ..." cart-mode fatal.
    return (a >= 0x06000000u && a < 0x06100000u) // HWRAM
        || (a >= 0x00200000u && a < 0x00300000u) // LWRAM
        || (a >= 0x02400000u && a < 0x02800000u) // cart RAM cached window (S8 cart heap)
        || (a >= 0x22400000u && a < 0x22800000u); // cart uncached window (bt910 TETHYS_CART_UNCACHED A/B)
}
#endif

void CC OrderingTable_Add_4F8AA0(PrimHeader** ppOt, PrimHeader* pItem)
{
#ifdef TETHYS_SATURN
    // SATURN: ADD-TIME chain validation. The S4 walk-side guard caught a
    // clipper whose tag went wild but cannot tell WHEN it went bad; failing
    // here instead names the queueing call site (ra) and whether the rot is
    // the incoming prim (p) or the bucket head it would link to (h).
    const u32 itemAddr = reinterpret_cast<u32>(pItem);
    const u32 headAddr = reinterpret_cast<u32>(*ppOt);
    if (!Tethys_OtPtrOk(itemAddr) || (headAddr != 0xFFFFFFFFu && !Tethys_OtPtrOk(headAddr)))
    {
        static char_type msg[40];
        char_type* p = msg;
        for (const char_type* s = "OTa "; *s; s++)
        {
            *p++ = *s;
        }
        const u32 vals[3] = {itemAddr, headAddr, reinterpret_cast<u32>(__builtin_return_address(0))};
        for (s32 v = 0; v < 3; v++)
        {
            for (s32 i = 0; i < 8; i++)
            {
                const u32 nib = (vals[v] >> (28 - 4 * i)) & 0xF;
                *p++ = static_cast<char_type>(nib < 10 ? '0' + nib : 'a' + nib - 10);
            }
            *p++ = ' ';
        }
        *--p = 0;
        Tethys_Fatal(msg);
    }
#endif
    // Debugging code, rendering type 2 will currently crash.
    // I can't see where in the code type 2 is ever used but somehow it must be
    // given it crashed in standalone rendering type 2. Type may also be 0 with blending enabled.
    if (pItem->rgb_code.code_or_pad == 2 || pItem->rgb_code.code_or_pad == 0)
    {
#ifdef TETHYS_SATURN
        // SATURN: upstream leftover debug trap -- a mute abort() cost a
        // debugging round-trip; name the code and the CALLER (the VRender
        // that queued the uninitialized prim; resolve via build/Tethys.map).
        static char_type msg[36];
        char_type* p = msg;
        for (const char_type* s = "OT code "; *s; s++)
        {
            *p++ = *s;
        }
        *p++ = static_cast<char_type>('0' + pItem->rgb_code.code_or_pad);
        for (const char_type* s = " ra 0x"; *s; s++)
        {
            *p++ = *s;
        }
        const u32 ra = reinterpret_cast<u32>(__builtin_return_address(0));
        for (s32 i = 0; i < 8; i++)
        {
            const u32 nib = (ra >> (28 - 4 * i)) & 0xF;
            *p++ = static_cast<char_type>(nib < 10 ? '0' + nib : 'a' + nib - 10);
        }
        *p = 0;
        Tethys_Fatal(msg);
#else
        abort();
#endif
    }

    // Get current OT ptr
    PrimHeader* pOt = *ppOt;

    // OT points to the new item
    *ppOt = pItem;

    // Item points back to whatever used to be in the OT, either a pointer to the next OT element
    // or the previously added prim.
    pItem->tag = pOt;
}

PrimHeader** OtLayer(PrimHeader** ppOt, Layer layer)
{
    return &ppOt[static_cast<u32>(layer)];
}

s32 CC PSX_getTPage_4F60E0(TPageMode tp, TPageAbr abr, s32 x, s16 y)
{
    return ((((static_cast<s8>(tp)) & 0x3) << 7) | (((static_cast<s8>(abr)) & 0x3) << 5) | (((y) &0x100) >> 4) | (((x) &0x3ff) >> 6) | (((y) &0x200) << 2));
}

EXPORT s32 CC PSX_getClut_4F6350(s32 x, s32 y)
{
    return (y << 6) | ((x >> 4) & 63);
}
