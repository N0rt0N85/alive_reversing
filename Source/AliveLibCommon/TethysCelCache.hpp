// SATURN (D-A2, Exoddus port): THE CEL CACHE AND THE SLAVE LZSS PREFETCH WALK,
// in one header that both games' Animation translation units can include.
//
// It is AO's machinery (AliveLibAO/Animation.cpp, 427.ao.3 to 445.ao.1) moved
// here VERBATIM in behaviour, so Exoddus uses it now and Oddysee can move onto
// it in an AO maintenance build (AO keeps its own copy until then: changing the
// AO object is the owner's call, and the Oddysee guard proves nothing moved).
// The design notes live with AO's copy; the short form:
//   * the shared LWRAM decode scratch (src/main.cxx, 16 KiB without a cartridge)
//     holds the most recent decoded cels, bump-allocated DOWNWARD from its top
//     and never below an eighth of it; a cel too big to cache decodes at offset 0
//     UPWARD and evicts only what it overwrites;
//   * the key is the cel's source pointer plus its decoded length, so a moved or
//     freed block makes a miss, and the resource manager flushes the cache on
//     every free and every compaction anyway (Tethys_DbufScratchForget);
//   * Tethys_CelPrefetch posts the NEXT tick's cels to the slave SH-2
//     (src/slave_lzss.cxx), Tethys_CelFence settles them before the sprite flush
//     (src/sys_saturn.cxx Tethys_PresentFrameOnce).
//
// HOW TO INCLUDE IT. Once, in the Animation translation unit, AFTER the game's
// Animation.hpp and in the scope where its AnimationHeader, FrameInfoHeader,
// FrameHeader, CompressionType, AnimFlags, AnimationBase and Animation names
// resolve (global for Exoddus, inside namespace AO for Oddysee), with three
// macros defined first:
//   TETHYS_CEL_ANIM_LIST            the DynamicArrayT<AnimationBase>* of the game
//   TETHYS_CEL_REFUSE_BLOCK(pBlock) a block whose cels must never be cached or
//                                   prefetched (AO: the blood block it clusters)
//   TETHYS_CEL_PREFETCH_OFF()       an extra reason to post nothing this tick
//                                   (AO: the GameSpeak menu's chant glow)
// and optionally
//   TETHYS_CEL_SKIP_ANIM(pBase)     an animation whose next cel is not posted
//                                   (Exoddus 11.22: off screen, its frames wait
//                                   for a draw); asked last, once per candidate
// It defines (extern "C") Tethys_CelFence, Tethys_DbufScratchForget and
// Tethys_CelPrefetch, the cache gauges Tethys_gDbufMemoHits, Tethys_gDbufCelLive
// and Tethys_gDbufCelWraps, and static helpers for the decoder:
// TethysCelFind, TethysCelAlloc, TethysCelEvictLow, Tethys_BlockPtrSane, and
// TethysCelLatchVptr (the prefetch walk's type test, called from vDecode).
#pragma once

#if !defined(TETHYS_CEL_ANIM_LIST) || !defined(TETHYS_CEL_REFUSE_BLOCK) || !defined(TETHYS_CEL_PREFETCH_OFF)
#error "TethysCelCache.hpp: define TETHYS_CEL_ANIM_LIST, TETHYS_CEL_REFUSE_BLOCK and TETHYS_CEL_PREFETCH_OFF first"
#endif
#if !defined(TETHYS_CEL_SKIP_ANIM)
#define TETHYS_CEL_SKIP_ANIM(pBase) false
#endif

// src/sys_saturn.cxx: the shared scratch, bound by src/main.cxx (both configurations).
extern "C" unsigned int Tethys_gDbufScratchBytes;
extern "C" unsigned char* Tethys_gDbufScratch;
// src/slave_lzss.cxx
extern "C" const unsigned char Tethys_kSlzOn;
extern "C" unsigned char Tethys_gSlzActive;
extern "C" unsigned char Tethys_gSlzDead;
extern "C" unsigned int Tethys_gSlzHit;
extern "C" void Tethys_SlzBegin();
extern "C" int Tethys_SlzAdd(const unsigned char* src, unsigned char* dst, unsigned int len);
extern "C" int Tethys_SlzRoom();
extern "C" void Tethys_SlzDrop(int job);
extern "C" void Tethys_SlzKick();
extern "C" void Tethys_SlzJoin();
extern "C" int Tethys_SlzJobDone(int job);
extern "C" void Tethys_CelFence();
// The game's resource manager: payload bytes of a live Animation block, 0 for
// anything else (AO ResourceManager.cpp, AE ResourceManager.cpp twin).
extern "C" unsigned int Tethys_AnimBlockBytes(unsigned char** ppRes);

// 427.ao.8: the per-entry cap admits the largest effect cel (3,520 B) and Abe's
// biggest type-5 cels (3,072 B); 24 slots == kSlzMaxJobs (src/slave_lzss.cxx).
static const unsigned int kTethysCelMax = 3520u;
static const unsigned int kTethysCelSlots = 24u;
// 445.ao.1: who owes an entry its bytes. >= 0 a job of the batch in flight (never
// served until Tethys_CelFence settles it); Slave = written by the slave and not
// used yet (its first hit counts in Tethys_gSlzHit); Mine = everything else.
static const int kTethysCelMine = -1;
static const int kTethysCelSlave = -2;

struct TethysCelEntry
{
    const unsigned char* src;
    unsigned int len;
    unsigned int off;
    int job;
};
static TethysCelEntry sTethysCel[kTethysCelSlots];
static unsigned int sTethysCelN = 0;
static unsigned int sTethysCelLow = 0; // 0 == not yet armed
static const void* sTethysAnimVptr = nullptr; // latched by the first vDecode

extern "C" unsigned int Tethys_gDbufMemoHits = 0; // 'sv' on overlay row 3
extern "C" unsigned int Tethys_gDbufCelLive = 0;  // 'e'  on overlay row 28
extern "C" unsigned int Tethys_gDbufCelWraps = 0; // 'ev' on overlay row 28

// bt863: a resource block pointer must land in a real memory region.
static inline bool Tethys_BlockPtrSane(const unsigned char* b)
{
    const unsigned int a = reinterpret_cast<unsigned int>(b);
    return (a >= 0x02400000u && a < 0x02800000u)   // cart, cached
        || (a >= 0x22400000u && a < 0x22800000u)   // cart, uncached
        || (a >= 0x06000000u && a < 0x06100000u)   // HWRAM
        || (a >= 0x00200000u && a < 0x00300000u);  // LWRAM
}

// Every function that reads or reshapes the cache settles an open batch first,
// so no caller has to know whether the slave is running.
#define TETHYS_CEL_QUIESCE()   \
    if (Tethys_gSlzActive)     \
    {                          \
        Tethys_CelFence();     \
    }

// 427.ao.8: the floor is an eighth of the scratch (14,336 B of cache without a
// cartridge); a cel too big to cache may overwrite [0, len).
static inline unsigned int TethysCelFloor()
{
    return Tethys_gDbufScratchBytes / 8u;
}

extern "C" void Tethys_DbufScratchForget()
{
    TETHYS_CEL_QUIESCE(); // the slave may be writing into what this forgets
    sTethysCelN = 0;
    sTethysCelLow = Tethys_gDbufScratchBytes;
    Tethys_gDbufCelLive = 0;
}

// Index of this cel's entry, or -1. No side effects.
static long TethysCelLookup(const unsigned char* src, unsigned int len)
{
    for (unsigned int i = 0; i < sTethysCelN; i++)
    {
        if (sTethysCel[i].src == src && sTethysCel[i].len == len)
        {
            return static_cast<long>(i);
        }
    }
    return -1;
}

// Offset of this cel inside the scratch, or -1.
static long TethysCelFind(const unsigned char* src, unsigned int len)
{
    TETHYS_CEL_QUIESCE();
    const long i = TethysCelLookup(src, len);
    if (i < 0)
    {
        return -1;
    }
    if (sTethysCel[i].job == kTethysCelSlave)
    {
        sTethysCel[i].job = kTethysCelMine;
        Tethys_gSlzHit++;
    }
    return static_cast<long>(sTethysCel[i].off);
}

// Reserve room for a cel about to be decoded, or -1 if it cannot be cached.
// 427.ao.7: a ring, not a reset -- the pointer wraps and an allocation drops only
// the entries whose bytes it overwrites, so the cache holds the most recent cels.
static long TethysCelAlloc(const unsigned char* src, unsigned int len)
{
    TETHYS_CEL_QUIESCE();
    if (len == 0u || len > kTethysCelMax)
    {
        return -1;
    }
    const unsigned int need = (len + 3u) & ~3u;
    const unsigned int low = TethysCelFloor();
    if (Tethys_gDbufScratchBytes <= low || need > Tethys_gDbufScratchBytes - low)
    {
        return -1;
    }
    if (sTethysCelLow == 0u || sTethysCelLow > Tethys_gDbufScratchBytes)
    {
        sTethysCelLow = Tethys_gDbufScratchBytes; // first use
    }
    if (sTethysCelLow < low + need)
    {
        sTethysCelLow = Tethys_gDbufScratchBytes; // the POINTER wraps, not the table
        Tethys_gDbufCelWraps++;
    }
    const unsigned int celOff = sTethysCelLow - need;
    {
        unsigned int k = 0;
        for (unsigned int i = 0; i < sTethysCelN; i++)
        {
            const unsigned int a = sTethysCel[i].off;
            const unsigned int b = a + ((sTethysCel[i].len + 3u) & ~3u);
            if (b <= celOff || a >= celOff + need)
            {
                sTethysCel[k++] = sTethysCel[i]; // no overlap: it survives
            }
            else
            {
                // 445.ao.1: an entry of the batch being built loses its bytes, so
                // its job must never run.
                Tethys_SlzDrop(sTethysCel[i].job);
            }
        }
        sTethysCelN = k;
    }
    if (sTethysCelN >= kTethysCelSlots)
    {
        // The table ran out: forget the OLDEST (entry 0, allocation order).
        Tethys_SlzDrop(sTethysCel[0].job);
        for (unsigned int i = 1; i < sTethysCelN; i++)
        {
            sTethysCel[i - 1] = sTethysCel[i];
        }
        sTethysCelN--;
    }
    sTethysCelLow = celOff;
    sTethysCel[sTethysCelN].src = src;
    sTethysCel[sTethysCelN].len = len;
    sTethysCel[sTethysCelN].off = sTethysCelLow;
    sTethysCel[sTethysCelN].job = kTethysCelMine; // the prefetch walk overwrites it
    sTethysCelN++;
    Tethys_gDbufCelLive = sTethysCelN;
    return static_cast<long>(sTethysCelLow);
}

// A cel too big to cache owns [0, len) of the scratch: drop what it overwrites.
static void TethysCelEvictLow(unsigned int len)
{
    TETHYS_CEL_QUIESCE();
    if (len <= TethysCelFloor())
    {
        return; // cannot reach any entry
    }
    unsigned int k = 0;
    for (unsigned int i = 0; i < sTethysCelN; i++)
    {
        if (sTethysCel[i].off >= len)
        {
            sTethysCel[k++] = sTethysCel[i];
        }
    }
    if (k != sTethysCelN)
    {
        sTethysCelN = k;
        Tethys_gDbufCelLive = k;
        unsigned int lowest = Tethys_gDbufScratchBytes;
        for (unsigned int i = 0; i < k; i++)
        {
            if (sTethysCel[i].off < lowest)
            {
                lowest = sTethysCel[i].off;
            }
        }
        sTethysCelLow = lowest;
    }
}

// 445.ao.1: close the slave's batch and settle every entry it owed. An entry the
// slave never reached is dropped: its bytes are whatever the scratch held before.
extern "C" void Tethys_CelFence()
{
    if (!Tethys_gSlzActive)
    {
        return;
    }
    Tethys_SlzJoin();
    unsigned int k = 0;
    for (unsigned int i = 0; i < sTethysCelN; i++)
    {
        TethysCelEntry e = sTethysCel[i];
        if (e.job >= 0)
        {
            if (!Tethys_SlzJobDone(e.job))
            {
                continue;
            }
            e.job = kTethysCelSlave;
        }
        sTethysCel[k++] = e;
    }
    sTethysCelN = k;
    Tethys_gDbufCelLive = k;
}

// The prefetch walk's type test: the list may hold other AnimationBase kinds
// whose fields are not an Animation's; there is no RTTI, so the vtable pointer
// tells them apart. Latched by the first Animation that decodes.
static inline void TethysCelLatchVptr(const void* pAnim)
{
    if (!sTethysAnimVptr)
    {
        sTethysAnimVptr = *reinterpret_cast<const void* const*>(pAnim);
    }
}

// 445.ao.1: one animation that will decode on the NEXT tick (its frame counter
// stands at 1): mirror the decoder's frame step without its side effects, and
// post that cel to the slave if the scratch path would take it. Every refusal
// means "the master decodes it next tick, as it always did"; every offset is
// bounded by the block's own size here.
static void TethysPrefetchOne(Animation* pAnim)
{
    unsigned char** const ppBlock = pAnim->field_20_ppBlock;
    if (!ppBlock || !*ppBlock || !Tethys_BlockPtrSane(*ppBlock) || pAnim->field_84_vram_rect.w <= 0)
    {
        return;
    }
    const unsigned char* const pBlock = *ppBlock;
    const unsigned int blockBytes = Tethys_AnimBlockBytes(ppBlock);
    const unsigned int tableOff = static_cast<unsigned int>(pAnim->field_18_frame_table_offset);
    if (blockBytes < 12u || (tableOff & 3u) || tableOff > blockBytes - 12u)
    {
        return;
    }
    const AnimationHeader* const pHead = reinterpret_cast<const AnimationHeader*>(pBlock + tableOff);
    const int nFrames = pHead->field_2_num_frames;
    if (nFrames <= 0 || static_cast<unsigned int>(nFrames) > (blockBytes - tableOff - 8u) / 4u)
    {
        return;
    }
    if (nFrames == 1 && pAnim->field_4_flags.Get(AnimFlags::eBit12_ForwardLoopCompleted))
    {
        return; // the decoder leaves before decoding anything
    }
    int frame = pAnim->field_92_current_frame;
    if (pAnim->field_4_flags.Get(AnimFlags::eBit19_LoopBackwards))
    {
        frame = static_cast<short>(frame - 1);
        if (frame < pHead->field_4_loop_start_frame)
        {
            frame = pAnim->field_4_flags.Get(AnimFlags::eBit8_Loop) ? nFrames - 1 : frame + 1;
        }
    }
    else
    {
        frame = static_cast<short>(frame + 1);
        if (frame >= nFrames)
        {
            frame = pAnim->field_4_flags.Get(AnimFlags::eBit8_Loop) ? pHead->field_4_loop_start_frame : frame - 1;
        }
    }
    if (frame < 0 || frame >= nFrames)
    {
        return;
    }
    const unsigned int infoOff = pHead->mFrameOffsets[frame];
    if ((infoOff & 3u) || infoOff >= blockBytes || blockBytes - infoOff < sizeof(FrameInfoHeader))
    {
        return;
    }
    const unsigned int headOff = reinterpret_cast<const FrameInfoHeader*>(pBlock + infoOff)->field_0_frame_header_offset;
    if ((headOff & 3u) || headOff >= blockBytes || blockBytes - headOff < sizeof(FrameHeader))
    {
        return;
    }
    const FrameHeader* const pFrame = reinterpret_cast<const FrameHeader*>(pBlock + headOff);
    if (pFrame->field_7_compression_type != CompressionType::eType_4_RLE
        && pFrame->field_7_compression_type != CompressionType::eType_5_RLE)
    {
        return;
    }
    const unsigned char* const pSrc = reinterpret_cast<const unsigned char*>(&pFrame->field_8_width2);
    const unsigned int len = *reinterpret_cast<const unsigned int*>(pSrc);
    if (len == 0u || len > kTethysCelMax || len > Tethys_gDbufScratchBytes)
    {
        return;
    }
    if (TETHYS_CEL_REFUSE_BLOCK(pBlock) // composed per object, never cached
        || TethysCelLookup(pSrc, len) >= 0 // cached already, or asked for by this walk
        || !Tethys_SlzRoom())
    {
        return;
    }
    const long off = TethysCelAlloc(pSrc, len);
    if (off < 0)
    {
        return;
    }
    // TethysCelAlloc appends: the new entry is the last one. If the add were ever
    // refused the entry would be a cel with no bytes behind it, so take it back.
    const int job = Tethys_SlzAdd(pSrc, Tethys_gDbufScratch + off, len);
    if (job < 0)
    {
        sTethysCelN--;
        Tethys_gDbufCelLive = sTethysCelN;
        return;
    }
    sTethysCel[sTethysCelN - 1].job = job;
}

// The game loop, right after AnimateAll: hand the next tick's cels to the slave.
extern "C" void Tethys_CelPrefetch()
{
    if (!Tethys_kSlzOn || Tethys_gSlzDead || Tethys_gDbufScratch == nullptr
        || TETHYS_CEL_PREFETCH_OFF() || sTethysAnimVptr == nullptr || !TETHYS_CEL_ANIM_LIST)
    {
        return;
    }
    TETHYS_CEL_QUIESCE(); // a batch with no present since: settle it before reusing the list
    Tethys_SlzBegin();
    for (int i = 0; i < TETHYS_CEL_ANIM_LIST->Size(); i++)
    {
        AnimationBase* const pBase = TETHYS_CEL_ANIM_LIST->ItemAt(i);
        if (!pBase)
        {
            break;
        }
        if (pBase->field_4_flags.Get(AnimFlags::eBit2_Animate)
            && pBase->field_E_frame_change_counter == 1
            && *reinterpret_cast<const void* const*>(pBase) == sTethysAnimVptr
            && !TETHYS_CEL_SKIP_ANIM(pBase))
        {
            TethysPrefetchOne(static_cast<Animation*>(pBase));
        }
    }
    Tethys_SlzKick();
}
