#pragma once

// SATURN (Exoddus port, stream R2): the C-linkage hooks between the Exoddus
// objects, AE Animation.cpp and src/renderer_saturn.cxx, declared ONCE. The
// linker does not check an extern "C" signature, so every definition and every
// caller includes this header and the compiler checks them against one
// declaration (a silent arity mismatch is how a seam breaks). Exoddus only: no
// Oddysee translation unit includes it.

#include "../AliveLibCommon/Types.hpp"
#include "stdlib.hpp"
#include "ResourceManager.hpp"

// ------------------------------------------------- soft resource allocations
// D-B4 / U1d T2 (AO ao262.2 policy): the resource allocator is a named fatal by
// default; the cosmetic sites that handle a null ask for a soft refusal
// explicitly. `reclaim` = compact and retry once (it MOVES unlocked blocks:
// never true for a caller holding a raw pointer into one, and false where a
// cosmetic effect must not trigger a compaction at the worst moment, AO bt828).
// R1's AE ResourceManager provides Alloc_New_Resource_ImplEx and defines
// TETHYS_AE_RES_IMPLEX with it in every Exoddus build. The other branch is NOT
// soft on Saturn: Alloc_New_Resource_Impl ends in the "RES NULL" fatal there
// (review RV1). It only exists for a header read outside the Exoddus build, and
// no such translation unit calls this function.
inline u8** Tethys_ResAllocSoft(u32 type, u32 id, u32 size, bool locked, bool reclaim)
{
#if defined(TETHYS_AE_RES_IMPLEX)
    return ResourceManager::Alloc_New_Resource_ImplEx(type, id, size, locked,
                                                      locked ? ResourceManager::BlockAllocMethod::eLastMatching
                                                             : ResourceManager::BlockAllocMethod::eFirstMatching,
                                                      reclaim, false /*never fatal*/);
#else
    (void) reclaim;
    return locked ? ResourceManager::Allocate_New_Locked_Resource_49BF40(type, id, size)
                  : ResourceManager::Alloc_New_Resource_49BED0(type, id, size);
#endif
}

// ------------------------------------------------------------- soft OOM types
// U1d T1 (AO bt976 twin): a cosmetic type whose EVERY ae_new<T> site null-tests
// the result opts into the soft allocator (an exhausted pool = no debris, not
// the named fatal). The primary template and the allocator live in AE
// stdlib.hpp (stream R1, which defines TETHYS_AE_SOFT_OOM with them); until
// then the opt-in compiles to nothing. Use after the class, at namespace scope:
//   TETHYS_AE_SOFT_OOM_TYPE(Blood);
#if defined(TETHYS_AE_SOFT_OOM)
#define TETHYS_AE_SOFT_OOM_TYPE(T)              \
    template <>                                 \
    struct Tethys_SoftOomAE<T>                  \
    {                                           \
        static constexpr bool value = true;     \
    }
#else
#define TETHYS_AE_SOFT_OOM_TYPE(T) static_assert(true, "soft OOM trait not provided yet")
#endif

// ------------------------------------------------- one-call segment bursts
// AO 448.ao.7: a burst's descriptor (Tethys_PrimBurst, Primitives_common.hpp)
// rides at the tail of the items' own resource block: the header, then one
// index byte per item, rounded to 4. Used by the web lines (U1d T22).
inline u32 Tethys_BurstTailBytes(s32 items)
{
    return (static_cast<u32>(sizeof(Tethys_PrimBurst)) - 4u + static_cast<u32>(items) + 3u) & ~3u;
}

// ---------------------------------------------------------------- Animation.cpp
// D-A1: the converter compacts Anim chunks and records 'TSAT' at payload+8; a
// compiled frame-table offset (kAnimRecords) becomes the block's actual one.
// Idempotent; no record or no block = the offset unchanged.
extern "C" s32 Tethys_TranslateFrameTable(u8** ppBlock, s32 compiledOff);
// The inverse, for quicksaves that store or recognise a COMPILED offset
// (MineCar's save tokens, Slurg's stored table): exact, see Animation.cpp.
extern "C" s32 Tethys_UntranslateFrameTable(u8** ppBlock, s32 runtimeOff);

// ------------------------------------------------- src/renderer_saturn.cxx
// ZapLine (AO 390.ao.1 gauges, defined by the renderer for both games):
// `h` = beams queued (cumulative), `s` = beams refused their buffers.
extern "C" u32 Tethys_gZapLink;
extern "C" u32 Tethys_gZapAllocFail;
// Sprites OFFERED to the last flush (bt1008 `su`): ParticleBurst sizes a new
// burst by the room left in the frame's 128 VDP1 commands.
extern "C" u32 Tethys_gSubmit;
// The iris (AO 449.ao.4, CircularFade): the SPOTLITE cel's rect in PC pixels
// and lines (inclusive), its X flip, the fade level 0..255. SIX arguments on
// both sides: grep Tethys_IrisDraw before changing either.
extern "C" void Tethys_IrisDraw(s32 x0, s32 y0, s32 x1, s32 y1, s32 flip, s32 fade);
// Invisibility (U1d T19): every sprite drawn with this PSX CLUT word is skipped
// while hidden (hide 1), drawn again after hide 0. InvisibleEffect arms it once
// the target's palette has faded to black, and always disarms it.
extern "C" void Tethys_ActorHide(s32 clut, s32 hide);
// The actor palette pack of a level (U10 R08): ACTORPAL.<lvl2>;1 seeds CRAM bank
// 2, or clears the seeds when the level has none (inert today). Call where no
// CD file is mid-read and no camera streams (the level change).
extern "C" void Tethys_ActorPackForLevel(const char* lvl2);

// ---------------------------------------------------------- src/chant_glow_ae.cxx
// The GameSpeak chant glow (AO's route 3, src/chant_glow.cxx, ported): the menu
// arms it when Abe starts chanting (MainMenu.cpp, where eBit24_Chant_Seq_Playing
// is set) and disarms it when another animation replaces the chant or the menu
// goes; while the gate is up, Animation::UploadTexture hands each freshly
// decoded cel to the composite before the upload. SIX arguments to the arm and
// FIVE to the composite on both sides: grep the names before changing either.
extern "C" volatile s32 Tethys_gChantGlowOn;
extern "C" void Tethys_ChantGlowArmAE(void* pHeadAnim, u8** ppOrb, s32 chantTable, s32 orbTable, s32 abeX, s32 abeY);
extern "C" void Tethys_ChantGlowDisarmAE();
extern "C" s32 Tethys_ChantGlowCompositeAE(void* pAnim, u8* pCel, s32 psxW, s32 psxH, s32 capBytes);
