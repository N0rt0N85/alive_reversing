#include "stdafx.h"
#include "ResourceManager.hpp"
#include "Function.hpp"
#include "Psx.hpp"
#include "Game.hpp"
#include "stdlib.hpp"
#include "LvlArchive.hpp"
#include "ScreenManager.hpp" // Camera
#include "Particle.hpp"
#include "PsxRender.hpp"
#include "PsxDisplay.hpp"
#include "Sys.hpp"
#include "GameAutoPlayer.hpp"
#ifdef TETHYS_SATURN
#include "Map.hpp" // SATURN: gMap_5C3030 (the film restore) and CameraName (the path preload)
#endif

ALIVE_VAR(1, 0x5C1BB0, ResourceManager*, pResourceManager_5C1BB0, nullptr);

ALIVE_VAR(1, 0xab4a04, u32, sManagedMemoryUsedSize_AB4A04, 0);
ALIVE_VAR(1, 0xab4a08, u32, sPeakedManagedMemUsage_AB4A08, 0);

ALIVE_VAR(1, 0x5C1B96, s16, sbLoadingInProgress_5C1B96, 0);
ALIVE_VAR(1, 0x5C1BAA, s16, bHideLoadingIcon_5C1BAA, 0);
ALIVE_VAR(1, 0x5C1BAC, s32, loading_ticks_5C1BAC, 0);
ALIVE_VAR(1, 0xAB49F4, s16, sResources_Pending_Loading_AB49F4, 0);
ALIVE_VAR(1, 0xAB4A0C, s16, sAllocationFailed_AB4A0C, 0);

ALIVE_VAR(1, 0x5D29EC, ResourceManager::ResourceHeapItem*, sFirstLinkedListItem_5D29EC, nullptr);
ALIVE_VAR(1, 0x5D29E8, ResourceManager::ResourceHeapItem*, sSecondLinkedListItem_5D29E8, nullptr);

#ifdef TETHYS_SATURN
// SATURN (decision D-B4, the AO regime of AliveLibAO/ResourceManager.cpp:33-78):
// the 5,120,000 B static heap does not fit a Saturn (and under the AE linker
// script's recipe L1 it would land in LWRAM's .bss and run off its end: the
// script's LWRAM ASSERT refuses that link). The heap is a pointer bound by the
// platform before Init_49BCE0 runs: src/main.cxx sizes it (the cartridge minus
// the .CAM cache, or without one the LWRAM pool minus its tenants) and calls
// Tethys_SetResHeapSize then Tethys_BindResourceHeap. Every reader of
// kResHeapSize (Init's extent, Move_Resources' out-of-heap sentinel, Reclaim)
// follows the runtime value: the name is unchanged.
u32 kResHeapSize = 0; // SATURN: runtime, set by src/main.cxx before the bind
u8* sResourceHeap_5D29F4 = nullptr;

void Tethys_BindResourceHeap(u8* pHeap)
{
    sResourceHeap_5D29F4 = pHeap;
}

u32 Tethys_ResHeapSize()
{
    return kResHeapSize;
}

// MUST be called before Tethys_BindResourceHeap + Init_49BCE0.
void Tethys_SetResHeapSize(u32 n)
{
    kResHeapSize = n;
}
#else
const u32 kResHeapSize = 5120000;
ALIVE_ARY(1, 0x5D29F4, u8, kResHeapSize, sResourceHeap_5D29F4, {}); // Huge 5.4 MB static resource buffer
#endif

const u32 kLinkedListArraySize = 375;
ALIVE_ARY(1, 0x5D1E30, ResourceManager::ResourceHeapItem, kLinkedListArraySize, sResourceLinkedList_5D1E30, {});

ALIVE_VAR(1, 0xAB49F8, u8*, spResourceHeapEnd_AB49F8, nullptr);

#ifdef TETHYS_SATURN
// ============================================================================
// SATURN (stream R1, decision D-R7): AO's Saturn resource machinery, copied and
// fitted to Exoddus's loader (AliveLibAO/ResourceManager.cpp, its SATURN blocks).
// The names src/ calls are AO's (C linkage where AO's are, C++ where
// src/game_alias.hpp declares them); the bodies read AE's state: the singleton
// pResourceManager_5C1BB0 with its FIFO of file records, one file in flight at a
// time through vLoadFile_StateMachine_464A70, where AO had one LoadingFile
// object per file.
//
// NOT ported, and why (stream R1 log): AO's sticky references (their file list
// is a measurement of Oddysee's R1, U1a A19: an Exoddus list needs an Exoddus
// re-read census first), and the absent-name (type, id) memo (it only quieted
// AO's CheckResourceIsLoaded, which Exoddus does not have: the skip and its
// count are kept).
// ============================================================================
extern "C" [[noreturn]] void Tethys_Fatal(const char_type* msg);
extern "C" u32 Tethys_RawTicks();                          // src/sys_saturn.cxx, ~208 a ms
extern "C" void Tethys_DbufScratchForget();                // TethysCelCache.hpp, in AE Animation.cpp
extern "C" void Tethys_MoveDown(void* pDst, const void* pSrc, u32 n); // src/stdlib_saturn.cxx
extern "C" void Tethys_WedgeCensus(void);                  // src/sys_saturn.cxx
extern "C" void Tethys_ResNullReport(u32 n, u32 type, u32 id, u32 size,
                                     u32 used, u32 peak, u32 cap, u32 vf, u32 pf); // src/sys_saturn.cxx
extern "C" void Tethys_PresentLoadingIcon();               // src/renderer_saturn.cxx
extern "C" void Tethys_CamPinsInit(u8* block, const char_type* const* names,
                                   const u32* sectors, u32 count); // src/cam_cache.cxx
extern "C" volatile u32 Tethys_gCartHeapBytes;             // src/main.cxx

// ---- gauges read by the overlay and the death screens (src/sys_saturn.cxx).
// Tethys_gMissingRes / Tethys_gLastMissId: an object found its resource absent
// at construction (the class of event that leaves a sprite missing, AO bt1126);
// written by the object side (stream R2), declared here because AO's are.
extern "C" volatile s32 Tethys_gMissingRes = 0;
extern "C" volatile u32 Tethys_gLastMissId = 0;
// rs: requests for a file this level's LVL does not hold (AO bt990): skipped,
// because the data is level-scoped, and counted, never silent.
extern "C" volatile s32 Tethys_gAbsentRes = 0;
// AO 454.ao.1: pp = compactions that met a locked path block they would have
// moved; tg = Loader walks stopped on a TLV offset outside the object block
// (AE Path.cpp).
extern "C" volatile u32 Tethys_gPathPinned = 0;
extern "C" volatile u32 Tethys_gTlvGuard = 0;
// AO ao262.2/ao262.10: rn = allocations refused to a caller that handles the
// null, and the biggest of them by (type, id, size).
extern "C" volatile u32 Tethys_gResSoftNull = 0;
extern "C" volatile u32 Tethys_gSoftType = 0;
extern "C" volatile u32 Tethys_gSoftId = 0;
extern "C" volatile u32 Tethys_gSoftSize = 0;

// Fail loud naming the offending record (AO's Tethys_CamStreamFatal, also the
// named fatal of the loader's own checks).
[[noreturn]] static void Tethys_NamedFatal(const char_type* pWhat, const char_type* pName)
{
    static char_type msg[40];
    char_type* p = msg;
    for (const char_type* s = pWhat; *s && p < &msg[31]; s++)
    {
        *p++ = *s;
    }
    for (const char_type* s = pName; s && *s && p < &msg[39]; s++)
    {
        *p++ = *s;
    }
    *p = 0;
    Tethys_Fatal(msg);
}

static bool Tethys_IsCamName(const char_type* pName)
{
    // Scanned to the terminator: a record name field is not null-guaranteed at
    // 12 characters (LvlArchive.hpp), callers pass terminated strings.
    s32 n = 0;
    while (pName && pName[n])
    {
        n++;
    }
    return n >= 4 && pName[n - 4] == '.' && pName[n - 3] == 'C' && pName[n - 2] == 'A' && pName[n - 1] == 'M';
}

// AO bt993: the screen name the wedge fatal reports, latched at the camera load
// (the camera being loaded is gone by the time the loading loop spins into the
// wedge).
static char_type sTethysLoadingCam[12] = {};

static void Tethys_NoteCamLoad(const char_type* pFileName)
{
    s32 i = 0;
    if (pFileName)
    {
        for (; pFileName[i] && i < 11; i++)
        {
            sTethysLoadingCam[i] = pFileName[i];
        }
    }
    sTethysLoadingCam[i] = 0;
}

// AO bt1061: the non-.CAM record bytes a screen change asks for, and the biggest
// record's start sector, counted when the loader creates a file record (the
// census of a flip), reset at T0 by the renderer (Tethys_OnScreenChange).
static u32 sFlipTopBytes = 0;
static u32 sFlipTopSector = 0;
static u32 sFlipRecBytes = 0;

static void Tethys_NoteFlipFile(const char_type* pName)
{
    if (Tethys_IsCamName(pName))
    {
        return; // the background is counted by the stream, not here
    }
    const LvlFileRecord* pRec = sLvlArchive_5BC520.Find_File_Record_433160(pName);
    if (!pRec)
    {
        return;
    }
    const u32 bytes = static_cast<u32>(pRec->field_10_num_sectors) << 11;
    sFlipRecBytes += bytes;
    if (bytes > sFlipTopBytes)
    {
        sFlipTopBytes = bytes;
        sFlipTopSector = static_cast<u32>(pRec->field_C_start_sector);
    }
}

extern "C" void Tethys_FlipRecordReset()
{
    sFlipTopBytes = 0;
    sFlipTopSector = 0;
    sFlipRecBytes = 0;
}

extern "C" u32 Tethys_FlipTopSector()
{
    return sFlipTopSector;
}

extern "C" u32 Tethys_FlipRecKb()
{
    return sFlipRecBytes >> 10;
}

// AO bt990, the gate shared by the request entry points (LoadResource,
// LoadResourcesFromList, LoadResourceFile): false = this level's LVL does not
// hold the name, the request is skipped and counted. On the PC the request would
// reach State_Wait_For_Load_Request and dereference the null record (U1a 3.2).
// A skipped request's loaded-callback never runs: the PC would have crashed at
// that point, so nothing that worked there changes.
static bool Tethys_RequestReadable(const char_type* pFileName)
{
    if (!sLvlArchive_5BC520.Find_File_Record_433160(pFileName))
    {
        Tethys_gAbsentRes++;
        return false;
    }
    return true;
}

// AO ao261.24/ao262.13/ao262.15: the state-1 wedge, a staging block that cannot
// be placed. A retry counter, a pressure release, and past 600 retries (10 s)
// the forensic death screen naming the screen, the size that did not fit (KiB)
// and the record's CdlLOC (BCD, raw).
static u32 sTethysState0Retries = 0;
static u32 sTethysWedgeBytes = 0;
static u8 sTethysWedgeLoc[3] = {0, 0, 0};
// SATURN 455.ao.1 (AO twin): reads of the file in hand that came back 0. The CD
// seam recovers medium faults itself, so these are structural and never heal.
static u32 sTethysReadFails = 0;

extern "C" u32 Tethys_WedgeBytes()
{
    return sTethysWedgeBytes;
}

// 455.ao.1: pTag "wz " = the staging block would not fit (state 0), "cd " = the
// record cannot be read. Same evidence line: screen, KiB, CdlLOC.
[[noreturn]] static void Tethys_WedgeFatal(const char_type* pTag)
{
    Tethys_WedgeCensus(); // the occupant list first, while the heap is intact
    // Budget, counted with the 14 columns Tethys_Fatal prepends: 14 + 3 "wz " +
    // 8 name + 1 + 5 digits + 1 'k' + 1 + 6 nibbles = 39 of 40.
    static char_type msg[32];
    char_type* p = msg;
    for (const char_type* s = pTag; *s; s++)
    {
        *p++ = *s;
    }
    const char_type* nm = sTethysLoadingCam[0] ? sTethysLoadingCam : "?";
    for (const char_type* s = nm; *s && *s != '.' && p < &msg[11]; s++)
    {
        *p++ = *s;
    }
    *p++ = ' ';
    char_type digits[8];
    s32 nd = 0;
    u32 v = sTethysWedgeBytes >> 10;
    do
    {
        digits[nd++] = static_cast<char_type>('0' + static_cast<s32>(v % 10u));
        v /= 10u;
    } while (v != 0u && nd < 5);
    while (nd > 0)
    {
        *p++ = digits[--nd];
    }
    *p++ = 'k';
    static const char_type kHex[] = "0123456789abcdef";
    *p++ = ' ';
    for (s32 b = 0; b < 3; b++)
    {
        *p++ = kHex[(sTethysWedgeLoc[b] >> 4) & 15];
        *p++ = kHex[sTethysWedgeLoc[b] & 15];
    }
    *p = 0;
    Tethys_Fatal(msg);
}

// ---- AO 442.ao.1/442.ao.3 twin (U1a A20): THE MENU PINS. Three main-menu pages
// kept in RAM WITHOUT a cartridge, so coming back to them costs no CD at all.
// One LOCKED block, last-fit at the heap top (the movie borrow's recipe, out of
// the compaction flow), sized to exactly the records it holds, taken only when
// the heap would still have kTethysMenuPinMargin left beside it; handed back
// FIRST whenever anything else needs the room (a film, a save without a cart,
// the menu leaving, a staging read that cannot be placed, the allocator's last
// chance before a refusal). With a cartridge it never arms: the path span holds
// every menu page (src/cam_cache.cxx).
//   Exoddus's pages (MainMenu.hpp MainMenuCams): 1 the main menu, every other
// page returns to it (42 sectors); 2 the GameSpeak character choice, every
// speech page returns to it (31); 4 Load (66). Strict priority: the first page
// that does not fit ends the list. The GameSpeak speech files are far beyond any
// margin (GLKSPEAK.BAN 375 sectors, 768,000 B staged, on an 834,760 B no-cart
// heap): visiting one takes the pins back through the staging valve, and the
// next pinned page re-arms them when the room is there again.
static const char_type* const kTethysMenuPins[3] = {"STP01C01.CAM", "STP01C02.CAM", "STP01C04.CAM"};
static const u32 kTethysMenuPinMargin = 262144;
// The largest pinned page's own chunks (STP01C04's FG1, 134,288 B) + slack: a
// stream that reads or fills a pin while the heap is shorter than this gives the
// pins back BEFORE it starts (they cannot be lent back mid-stream).
static const u32 kTethysCamStreamReserve = 143360;
static const u32 kTethysMenuPinResId = 0x4E49504D; // 'MPIN', readable in a heap top-8
static u8** sppTethysMenuPins = nullptr;
static u32 sTethysMenuPinBytes = 0;
static u32 sTethysMenuPinPeakSnap = 0;
static bool sTethysPinsBusy = false;

// Does p point into the pins' block? (Locked, so compaction never moves it.)
static bool Tethys_MenuPinsHold(const u8* p)
{
    return sppTethysMenuPins && p && p >= *sppTethysMenuPins
        && p < *sppTethysMenuPins + (sTethysMenuPinBytes - sizeof(ResourceManager::Header));
}

// Give the block back. Refused (false) when there is nothing to give or while a
// .CAM streams from or into it; true = the bytes are free again.
bool Tethys_MenuPinsRelease()
{
    if (!sppTethysMenuPins || sTethysPinsBusy)
    {
        return false;
    }
    Tethys_CamPinsInit(nullptr, nullptr, nullptr, 0);
    ResourceManager::FreeResource_49C330(sppTethysMenuPins);
    sppTethysMenuPins = nullptr;
    // pk without the block: every peak taken while it was held included it.
    const u32 pk = sPeakedManagedMemUsage_AB4A08 - sTethysMenuPinBytes;
    sPeakedManagedMemUsage_AB4A08 = (pk > sTethysMenuPinPeakSnap) ? pk : sTethysMenuPinPeakSnap;
    sTethysMenuPinBytes = 0;
    return true;
}

static bool Tethys_NameIs(const char_type* a, const char_type* b)
{
    return strcmp(a, b) == 0;
}

// Called at the top of every .CAM stream, before the cache is consulted.
static void Tethys_MenuPinsBeforeStream(const char_type* pCamName)
{
    if (Tethys_gCartHeapBytes != 0)
    {
        return;
    }
    bool pinned = false;
    for (s32 i = 0; i < 3; i++)
    {
        if (Tethys_NameIs(pCamName, kTethysMenuPins[i]))
        {
            pinned = true;
        }
    }
    if (!pinned)
    {
        // Any other page: the stream does not touch the pins, so the allocator
        // can still take them back mid-stream if its chunks need the room.
        // 442.ao.3: EXCEPT THE LOADING PAGES, the menu committing to leave
        // (13: a game, 22: a demo). What runs next makes LOCKED last-fit blocks
        // that outlive the menu, which would land right under the pins and
        // split the heap once they go: release them first.
        if (Tethys_NameIs(pCamName, "STP01C13.CAM") || Tethys_NameIs(pCamName, "STP01C22.CAM"))
        {
            Tethys_MenuPinsRelease();
        }
        return;
    }
    if (sppTethysMenuPins)
    {
        // This stream will read or fill a pin, and then nothing may take the
        // block back: if its own chunks might not fit beside it, release now.
        if (kResHeapSize - sManagedMemoryUsedSize_AB4A04 < kTethysCamStreamReserve)
        {
            Tethys_MenuPinsRelease();
        }
        return;
    }
    // Arm on a pinned page only: by then the menu's own set is loaded.
    const char_type* names[3];
    u32 sectors[3];
    u32 n = 0;
    u32 total = 0;
    const u32 freeBytes = kResHeapSize - sManagedMemoryUsedSize_AB4A04;
    for (s32 i = 0; i < 3; i++)
    {
        const LvlFileRecord* pRec = sLvlArchive_5BC520.Find_File_Record_433160(kTethysMenuPins[i]);
        if (!pRec)
        {
            break;
        }
        const u32 bytes = static_cast<u32>(pRec->field_10_num_sectors) << 11;
        if (total + bytes + sizeof(ResourceManager::Header) + kTethysMenuPinMargin > freeBytes)
        {
            break;
        }
        names[n] = kTethysMenuPins[i];
        sectors[n] = static_cast<u32>(pRec->field_10_num_sectors);
        total += bytes;
        n++;
    }
    if (n == 0)
    {
        return;
    }
    sTethysMenuPinPeakSnap = sPeakedManagedMemUsage_AB4A08;
    u8** ppBlock = ResourceManager::Alloc_New_Resource_ImplEx(
        ResourceManager::Resource_DecompressionBuffer, kTethysMenuPinResId,
        total, true, ResourceManager::eLastMatching,
        false /*bReclaimOnFail*/, false /*bFatalOnFail*/);
    if (!ppBlock)
    {
        sPeakedManagedMemUsage_AB4A08 = sTethysMenuPinPeakSnap;
        return; // fragmented right now: no pins, the menu reads as it always did
    }
    sppTethysMenuPins = ppBlock;
    sTethysMenuPinBytes = total + sizeof(ResourceManager::Header);
    Tethys_CamPinsInit(*ppBlock, names, sectors, n);
}

// ---- AO P7/360.ao.2 twin (U7-T8, A21): THE MOVIE-TIME HEAP BORROW. A playing
// film pauses the whole game, so the Cinepak buffers borrow ONE locked block of
// this heap instead of requiring a cartridge (src/movie_cinepak.cxx). Stage 1: a
// plain last-fit allocation (heap top, away from the compaction flow; locked so
// no reclaim moves it). Stage 2, only when nothing is mid-read: two compactions,
// then one retry that may compact once more. Soft: null = the film tier falls
// back. The peak gauge is snapshotted and restored on release, so a transient
// never contaminates pk. Type Resource_DecompressionBuffer: never swept by type
// and self-describing in a heap top-8.
static u32 sTethysMovieBorrowPeakSnap = 0;
static const u32 kTethysMovieResId = 0x49564F4D; // 'MOVI'
// 360.ao.2's latches: used before stage 1, the gate (bit0 resources pending,
// bit1 files queued), used after the pressure recipe (0 = skipped), the ask.
extern "C" u32 Tethys_gMbUsed0 = 0;
extern "C" u32 Tethys_gMbGate = 0;
extern "C" u32 Tethys_gMbUsed1 = 0;
extern "C" u32 Tethys_gMbAsk = 0;

u8** Tethys_MovieBorrowHeap(u32 bytes)
{
    // 442.ao.1: the menu pins sit exactly where this block wants to go (the heap
    // top), and a film from the menu is the moment they are not needed.
    Tethys_MenuPinsRelease();
    sTethysMovieBorrowPeakSnap = sPeakedManagedMemUsage_AB4A08;
    u8** ppBlock = nullptr;
    Tethys_gMbUsed0 = sManagedMemoryUsedSize_AB4A04;
    Tethys_gMbGate = 0;
    Tethys_gMbUsed1 = 0;
    Tethys_gMbAsk = bytes;
    // Cheap pre-check: do not even walk the list when the arithmetic says no
    // (16 = the block Header the allocator adds).
    if (kResHeapSize - sManagedMemoryUsedSize_AB4A04 >= bytes + 16)
    {
        ppBlock = ResourceManager::Alloc_New_Resource_ImplEx(
            ResourceManager::Resource_DecompressionBuffer, kTethysMovieResId,
            bytes, true, ResourceManager::eLastMatching,
            false /*bReclaimOnFail*/, false /*bFatalOnFail*/);
    }
    s32 queued = 0;
    s32 state = 0;
    s32 sectors = 0;
    ResourceManager::Tethys_LoadingProbeImpl(&queued, &state, &sectors);
    if (sResources_Pending_Loading_AB49F4 != 0)
    {
        Tethys_gMbGate |= 1u;
    }
    if (queued > 0)
    {
        Tethys_gMbGate |= 2u;
    }
    if (!ppBlock && Tethys_gMbGate == 0)
    {
        ResourceManager::Reclaim_Memory_49C470(0);
        ResourceManager::Reclaim_Memory_49C470(0);
        Tethys_gMbUsed1 = sManagedMemoryUsedSize_AB4A04;
        ppBlock = ResourceManager::Alloc_New_Resource_ImplEx(
            ResourceManager::Resource_DecompressionBuffer, kTethysMovieResId,
            bytes, true, ResourceManager::eLastMatching,
            true /*bReclaimOnFail*/, false /*bFatalOnFail*/);
    }
    if (!ppBlock)
    {
        sPeakedManagedMemUsage_AB4A08 = sTethysMovieBorrowPeakSnap;
    }
    return ppBlock;
}

void Tethys_MovieReleaseHeap(u8** ppBlock)
{
    if (ppBlock)
    {
        ResourceManager::FreeResource_49C330(ppBlock);
    }
    sPeakedManagedMemUsage_AB4A08 = sTethysMovieBorrowPeakSnap;
}

// ---- AO 446.ao.12 twin (U1c T09): THE SAVE LIBRARY'S BUFFERS (16 + 8 KB), lent
// for ONE save operation on the no-cart path (src/save_saturn.cxx BupOpen).
// Locked and last-fit like the movie's block, but NO compaction: the game is
// paused, not torn down. The menu pins go back only if the plain ask fails.
// nullptr = no room: that one save or load fails and says so, nothing is held.
static u32 sTethysBupPeakSnap = 0;
static const u32 kTethysBupResId = 0x45564153; // 'SAVE'

u8** Tethys_BupBorrowHeap(u32 bytes)
{
    sTethysBupPeakSnap = sPeakedManagedMemUsage_AB4A08;
    u8** ppBlock = ResourceManager::Alloc_New_Resource_ImplEx(
        ResourceManager::Resource_DecompressionBuffer, kTethysBupResId,
        bytes, true, ResourceManager::eLastMatching, false, false);
    if (!ppBlock && Tethys_MenuPinsRelease())
    {
        ppBlock = ResourceManager::Alloc_New_Resource_ImplEx(
            ResourceManager::Resource_DecompressionBuffer, kTethysBupResId,
            bytes, true, ResourceManager::eLastMatching, false, false);
    }
    if (!ppBlock)
    {
        sPeakedManagedMemUsage_AB4A08 = sTethysBupPeakSnap;
    }
    return ppBlock;
}

void Tethys_BupReleaseHeap(u8** ppBlock)
{
    if (ppBlock)
    {
        ResourceManager::FreeResource_49C330(ppBlock);
    }
    sPeakedManagedMemUsage_AB4A08 = sTethysBupPeakSnap; // a loan, never a tenant
}
#endif

// TODO: Move to own file
EXPORT void CCSTD sub_465BC0(s32 /*a1*/)
{
    NOT_IMPLEMENTED();
}

// TODO: Move to own file
EXPORT void CC Game_ShowLoadingIcon_482D80()
{
    const AnimRecord& loadingRec = AnimRec(AnimId::Loading_Icon2);
    u8** ppLoadingAnimRes = ResourceManager::GetLoadedResource_49C2A0(ResourceManager::Resource_Animation, loadingRec.mResourceId, FALSE, FALSE);
    if (!ppLoadingAnimRes)
    {
        return;
    }
    PSX_Display_Buffer dispBuffer = {};

    Particle* pParticle = ae_new<Particle>();
#ifdef TETHYS_SATURN
    // SATURN (U1d T1): null-tested like Particle.cpp's own two sites, so that
    // Particle may take the soft OOM policy (no icon rather than a crash).
    if (!pParticle)
    {
        return;
    }
#endif
    pParticle->ctor_4CC4C0(FP_FromInteger(0), FP_FromInteger(0), loadingRec.mFrameTableOffset, loadingRec.mMaxW, loadingRec.mMaxH, ppLoadingAnimRes);

    // TODO: May need to clear all other low word bits ?
    pParticle->field_20_animation.field_4_flags.Clear(AnimFlags::eBit15_bSemiTrans);
    pParticle->field_20_animation.field_4_flags.Set(AnimFlags::eBit16_bBlending);

    pParticle->field_20_animation.field_C_render_layer = Layer::eLayer_0;
    PSX_SetDefDrawEnv_4F5AA0(&dispBuffer.field_0_draw_env, 0, 0, 640, 240);
    PSX_PutDrawEnv_4F5980(&dispBuffer.field_0_draw_env);
    PSX_DrawSync_4F6280(0);

    // This was doing something odd with OT index.. I think its trying to simulate f64 buffering by
    // using other parts of the OT while another part is drawn, but it was bugged because it cleared the other anyway
    // on PC it seems fine to just always start at zero.
    PSX_ClearOTag_4F6290(dispBuffer.field_70_ot_buffer, 43);
    pParticle->field_20_animation.vRender_40B820(320, 220, dispBuffer.field_70_ot_buffer, 0, 0);
    PSX_DrawOTag_4F6540(dispBuffer.field_70_ot_buffer);
    PSX_DrawSync_4F6280(0);

#ifdef TETHYS_SATURN
    // SATURN (AO 418.ao.2 twin, U1a A16): THE ICON WAS DRAWN, THEN ERASED, THEN
    // NEVER SHOWN. The PSX draws it once into each half of VRAM (its two frame
    // buffers) and relies on neither being redrawn during the load. On Saturn
    // every DrawOTag starts a fresh sprite list, so the second, off-screen copy
    // wiped the first, and even the first was never PRESENTED: the flip holds the
    // sprite layer blank and faded for the whole load, and LoadingLoop's VSync(0)
    // presents nothing. One draw, and the renderer presents it through the blank.
    Tethys_PresentLoadingIcon();
#else
    PSX_ClearOTag_4F6290(dispBuffer.field_70_ot_buffer, 43);
    pParticle->field_20_animation.vRender_40B820(320, 640 - 164, dispBuffer.field_70_ot_buffer, 0, 0);
    PSX_DrawOTag_4F6540(dispBuffer.field_70_ot_buffer);
    PSX_DrawSync_4F6280(0);
#endif

    PSX_SetDefDispEnv_4F55A0(&dispBuffer.field_5C_disp_env, 0, 0, 640, 240);
    PSX_PutDispEnv_4F5890(&dispBuffer.field_5C_disp_env);
    pParticle->field_6_flags.Set(BaseGameObject::eDead_Bit3);
    bHideLoadingIcon_5C1BAA = 1;
}

ResourceManager* ResourceManager::ctor_464910()
{
    BaseGameObject_ctor_4DBFA0(1, 0);

    field_20_files_pending_loading.ctor_40CA60(3);
    field_48_dArray.ctor_40CA60(3);

    field_6_flags.Set(BaseGameObject::eSurviveDeathReset_Bit9);
    field_6_flags.Set(BaseGameObject::eUpdateDuringCamSwap_Bit10);
    SetVTable(this, 0x545EBC); // vTbl_ResourceManager_545EBC

    SetType(AETypes::eResourceManager_70);
    field_2C_pFileItem = nullptr;
    field_30_start_sector = 0;
    field_34_num_sectors = 0;
    field_38_ppRes = {};
    field_3C_pLoadingHeader = nullptr;
    field_42_state = State_Wait_For_Load_Request;
    field_40_seek_attempts = 0;
    return this;
}

BaseGameObject* ResourceManager::vdtor_4649B0(s32 flags)
{
    dtor_4649E0();
    if (flags & 1)
    {
        ae_delete_free_495540(this);
    }
    return this;
}

void ResourceManager::dtor_4649E0()
{
    SetVTable(this, 0x545EBC); // vTbl_ResourceManager_545EBC
    Shutdown_465610();
    field_48_dArray.dtor_40CAD0();
    field_20_files_pending_loading.dtor_40CAD0();
    BaseGameObject_dtor_4DBEC0();
}

void ResourceManager::vLoadFile_StateMachine_464A70()
{
    switch (field_42_state)
    {
        case State_Wait_For_Load_Request:
            // NOTE: Pruned branches here from stub that was hard coded to return 0
            if (!field_20_files_pending_loading.IsEmpty())
            {
                field_2C_pFileItem = field_20_files_pending_loading.ItemAt(0);

                LvlFileRecord* pLvlFileRec1 = sLvlArchive_5BC520.Find_File_Record_433160(field_2C_pFileItem->field_0_fileName);
#ifdef TETHYS_SATURN
                // SATURN (AO bt990, U1a A11): the PC dereferences the record
                // unchecked. Every request path skips an absent name already
                // (Tethys_RequestReadable); a record that still arrives here
                // without one (the archive changed under a queued request) is
                // dropped and counted, as a request for it would have been.
                if (!pLvlFileRec1)
                {
                    Tethys_gAbsentRes++;
                    ResourceManager_FileRecord* pDrop = field_2C_pFileItem;
                    for (s32 k = 0; k < pDrop->field_10_file_sections_dArray.Size(); k++)
                    {
                        ResourceManager_FilePartRecord_18* pPart = pDrop->field_10_file_sections_dArray.ItemAt(k);
                        if (!pPart)
                        {
                            break;
                        }
                        ae_delete_free_495540(pPart);
                    }
                    field_20_files_pending_loading.Remove_Item(pDrop);
                    ae_non_zero_free_495560(pDrop->field_0_fileName);
                    pDrop->dtor_464EA0();
                    ae_delete_free_495540(pDrop);
                    field_2C_pFileItem = nullptr;
                    break;
                }
#endif
                field_34_num_sectors = pLvlFileRec1->field_10_num_sectors;
                field_30_start_sector = pLvlFileRec1->field_C_start_sector + sLvlArchive_5BC520.field_4_cd_pos;
                PSX_Pos_To_CdLoc_4FADD0(field_30_start_sector, &field_44_cdLoc);

                sbLoadingInProgress_5C1B96 = 1;
                field_42_state = State_Allocate_Memory_For_File;
            }
            break;

        case State_Allocate_Memory_For_File:
            field_38_ppRes = ResourceManager::Allocate_New_Block_49BFB0(field_34_num_sectors << 11, BlockAllocMethod::eFirstMatching);
            if (field_38_ppRes)
            {
                field_3C_pLoadingHeader = Get_Header_49C410(field_38_ppRes);
                field_3C_pLoadingHeader->field_8_type = Resource_Pend;
                ResourceManager::Increment_Pending_Count_49C5F0();
                field_42_state = State_Seek_To_File;
#ifdef TETHYS_SATURN
                sTethysState0Retries = 0;
                sTethysReadFails = 0; // 455.ao.1: a new file, a new count
#endif
            }
            else
            {
                // Failed to allocate, free some memory and loop around for another go
                ResourceManager::Reclaim_Memory_49C470(200000);
#ifdef TETHYS_SATURN
                // SATURN (AO ao261.24/ao262.13-15 twin, U1a A11): the staging
                // block of the whole file cannot be placed. On the PC this loop
                // spins until it can; on an 834 KB heap it can spin forever, the
                // screen frozen with no CD activity. Here the pending count is 0
                // (it rises only once a block is placed), so the compaction above
                // already runs every frame; what was missing is the relief valve
                // and the named death. The menu pins (a lender) go back at the
                // first refusal, the frame they are needed; every 100th retry
                // from the 10th compacts the whole heap three times (one list
                // walk does not fully compact); past 600 retries (10 s) the
                // forensic fatal names the screen, the size and the record.
                sTethysWedgeBytes = static_cast<u32>(field_34_num_sectors) << 11;
                sTethysWedgeLoc[0] = field_44_cdLoc.field_0_minute;
                sTethysWedgeLoc[1] = field_44_cdLoc.field_1_second;
                sTethysWedgeLoc[2] = field_44_cdLoc.field_2_sector;
                ++sTethysState0Retries;
                if (Tethys_MenuPinsRelease() || sTethysState0Retries % 100 == 10)
                {
                    ResourceManager::Reclaim_Memory_49C470(0);
                    ResourceManager::Reclaim_Memory_49C470(0);
                    ResourceManager::Reclaim_Memory_49C470(0);
                }
                if (sTethysState0Retries > 600)
                {
                    Tethys_WedgeFatal("wz ");
                }
#endif
            }
            break;

        case State_Seek_To_File:
            if (!PSX_CD_File_Seek_4FB1E0(2, &field_44_cdLoc))
            {
                if (field_40_seek_attempts < 20u)
                {
                    field_40_seek_attempts++;
                    return;
                }

                sub_465BC0(0); // Crashes if forced to be called.. ? Seems to display the can't find demo/fmv message

                while (!PSX_CD_File_Seek_4FB1E0(2, &field_44_cdLoc))
                {
                    // Do nothing
                }
            }
            field_42_state = State_Read_Sectors_ASync;
            field_40_seek_attempts = 0;
            break;

        case State_Read_Sectors_ASync:
            if (PSX_CD_File_Read_4FB210(field_34_num_sectors, field_3C_pLoadingHeader))
            {
                field_42_state = State_Wait_For_Read_Complete;
                const s32 bWaitRet = PSX_CD_FileIOWait_4FB260(1);
                if (bWaitRet <= 0)
                {
                    field_42_state = bWaitRet != -1 ? State_File_Read_Completed : State_Seek_To_File;
                }
            }
            else
            {
                field_42_state = State_Seek_To_File;
#ifdef TETHYS_SATURN
                // SATURN 455.ao.1 (AO twin): the seek this goes back to always
                // succeeds on the Saturn (a cursor move), so the 20-try branch
                // above never fires and a read that keeps failing looped for
                // ever. A medium fault no longer comes back from the CD seam
                // (src/cd_saturn.cxx recovers it, with a screen): a 0 here is
                // structural and never heals. Eight times, then name the record.
                if (++sTethysReadFails > 8)
                {
                    sTethysWedgeBytes = static_cast<u32>(field_34_num_sectors) << 11;
                    sTethysWedgeLoc[0] = field_44_cdLoc.field_0_minute;
                    sTethysWedgeLoc[1] = field_44_cdLoc.field_1_second;
                    sTethysWedgeLoc[2] = field_44_cdLoc.field_2_sector;
                    Tethys_WedgeFatal("cd ");
                }
#endif
            }
            break;

        case State_Wait_For_Read_Complete:
        {
            const s32 bWaitRet = PSX_CD_FileIOWait_4FB260(1);
            if (bWaitRet <= 0)
            {
                field_42_state = bWaitRet != -1 ? State_File_Read_Completed : State_Seek_To_File;
            }
        }
        break;

        case State_File_Read_Completed:
#if defined(TETHYS_GAME_AE)
            // SATURN: the walk is bounded by the block this state machine read.
            Move_Resources_To_DArray_49C1C0(field_38_ppRes, &field_48_dArray, static_cast<u32>(field_34_num_sectors) << 11);
#else
            Move_Resources_To_DArray_49C1C0(field_38_ppRes, &field_48_dArray);
#endif
            field_42_state = State_Load_Completed;
            break;

        case State_Load_Completed:
            sbLoadingInProgress_5C1B96 = 0;
            OnResourceLoaded_464CE0();
            field_48_dArray.field_4_used_size = 0; // TODO: Needs to be private
            Decrement_Pending_Count_49C610();
            field_42_state = State_Wait_For_Load_Request;
            break;

        default:
            return;
    }
}

void ResourceManager::OnResourceLoaded_464CE0()
{
    // Iterate every section in the loaded file
    DynamicArrayIter fileSectionsArrayIter = {};
    fileSectionsArrayIter.field_0_pDynamicArray = &field_2C_pFileItem->field_10_file_sections_dArray;
    fileSectionsArrayIter.field_4_idx = 0;
    while (fileSectionsArrayIter.field_4_idx < field_2C_pFileItem->field_10_file_sections_dArray.Size())
    {
        ResourceManager_FilePartRecord_18* pFilePart = field_2C_pFileItem->field_10_file_sections_dArray.ItemAt(fileSectionsArrayIter.field_4_idx);
        fileSectionsArrayIter.field_4_idx++;

        if (!pFilePart)
        {
            break;
        }

        if (pFilePart->field_0_type || pFilePart->field_4_id)
        {
            // Find matching file part
            u8** ppRes = nullptr;
            for (s32 i = 0; i < field_48_dArray.Size(); i++)
            {
                if (field_48_dArray.ItemAt(i))
                {
                    Header* pHeader = Get_Header_49C410(field_48_dArray.ItemAt(i));
                    if (pHeader->field_C_id == pFilePart->field_4_id && pHeader->field_8_type == pFilePart->field_0_type)
                    {
                        ppRes = field_48_dArray.ItemAt(i);
                        break;
                    }
                }
            }

            if (!ppRes)
            {
                LOG_ERROR("File part not found id " << pFilePart->field_4_id << " type " << pFilePart->field_0_type);
                ALIVE_FATAL("File part not found");
            }

            if (pFilePart->field_14_bAddUseCount)
            {
                Inc_Ref_Count_49C310(ppRes);
            }

            if (pFilePart->field_8_pCamera)
            {
                pFilePart->field_8_pCamera->field_0.Push_Back_40CAF0(ppRes);
            }

            if (pFilePart->field_10_pFn)
            {
                pFilePart->field_10_pFn(pFilePart->field_C_fn_arg_pCamera);
            }
        }
        else
        {
            if (pFilePart->field_14_bAddUseCount || pFilePart->field_8_pCamera)
            {
                for (s32 i = 0; i < field_48_dArray.Size(); i++)
                {
                    u8** pItem2 = field_48_dArray.ItemAt(i);
                    if (!pItem2)
                    {
                        break;
                    }

                    if (pFilePart->field_14_bAddUseCount)
                    {
                        Inc_Ref_Count_49C310(pItem2);
                    }

                    if (pFilePart->field_8_pCamera)
                    {
                        pFilePart->field_8_pCamera->field_0.Push_Back_40CAF0(pItem2);
                    }
                }
            }

            if (pFilePart->field_10_pFn)
            {
                pFilePart->field_10_pFn(pFilePart->field_C_fn_arg_pCamera);
            }
        }
        fileSectionsArrayIter.Remove_At_Iter_40CCA0();
        ae_delete_free_495540(pFilePart);
    }

    // Remove from pending files
    field_20_files_pending_loading.Remove_Item(field_2C_pFileItem);

    // Free/destruct the removed item
    ae_non_zero_free_495560(field_2C_pFileItem->field_0_fileName);
    if (field_2C_pFileItem)
    {
        field_2C_pFileItem->dtor_464EA0();
        ae_delete_free_495540(field_2C_pFileItem);
    }
    field_2C_pFileItem = nullptr;
}

void ResourceManager::ResourceManager_FileRecord::dtor_464EA0()
{
    field_10_file_sections_dArray.dtor_40CAD0();
}

void ResourceManager::LoadResource_464EE0(const char_type* pFileItem, u32 type, u32 resourceID, Camera* pCamera, Camera* pFnArg, ResourceManager::TLoaderFn pFn, s16 bAddUseCount)
{
    u8** pLoadedRes = GetLoadedResource_49C2A0(type, resourceID, 1, 0);
    if (pLoadedRes)
    {
        if (pCamera)
        {
            pCamera->field_0.Push_Back_40CAF0(pLoadedRes);
        }

        if (pFn)
        {
            pFn(pFnArg);
        }
        return;
    }

#ifdef TETHYS_SATURN
    // SATURN (AO bt990, U1a A11): a name this level's LVL does not hold is
    // skipped and counted (rs), after the resident check like AO's twin.
    if (!Tethys_RequestReadable(pFileItem))
    {
        return;
    }
#endif

    for (s32 i = 0; i < field_20_files_pending_loading.Size(); i++)
    {
        ResourceManager_FileRecord* pFileRec = field_20_files_pending_loading.ItemAt(i);
        if (!pFileRec)
        {
            break;
        }

        ResourcesToLoadList* pResourceToLoadList = pFileRec->field_4_pResourcesToLoadList;
        if (pResourceToLoadList)
        {
            if (pResourceToLoadList->field_0_count > 0)
            {
                bool found = false;
                for (s32 j = 0; j < pResourceToLoadList->field_0_count; j++)
                {
                    if (type == pResourceToLoadList->field_4_items[j].field_0_type && resourceID == pResourceToLoadList->field_4_items[j].field_4_res_id)
                    {
                        found = true;
                        break;
                    }
                }

                if (found)
                {
                    auto pNewFilePart = ae_new<ResourceManager_FilePartRecord_18>();
                    pNewFilePart->field_0_type = type;
                    pNewFilePart->field_10_pFn = pFn;
                    pNewFilePart->field_8_pCamera = pCamera;
                    pNewFilePart->field_14_bAddUseCount = bAddUseCount;
                    pNewFilePart->field_4_id = resourceID;
                    pNewFilePart->field_C_fn_arg_pCamera = pFnArg;
                    pFileRec->field_10_file_sections_dArray.Push_Back(pNewFilePart);
                    return;
                }
            }
        }
        else if (type == pFileRec->field_8_type && resourceID == pFileRec->field_C_id)
        {
            auto pNewFilePart = ae_new<ResourceManager_FilePartRecord_18>();
            pNewFilePart->field_0_type = type;
            pNewFilePart->field_10_pFn = pFn;
            pNewFilePart->field_8_pCamera = pCamera;
            pNewFilePart->field_14_bAddUseCount = bAddUseCount;
            pNewFilePart->field_4_id = resourceID;
            pNewFilePart->field_C_fn_arg_pCamera = pFnArg;
            pFileRec->field_10_file_sections_dArray.Push_Back(pNewFilePart);
            return;
        }
    }

#ifdef TETHYS_SATURN
    Tethys_NoteFlipFile(pFileItem); // bt1061: a new file record = a read this flip asks for
#endif
    auto pNewFileRec = ae_new<ResourceManager_FileRecord>();
    pNewFileRec->field_10_file_sections_dArray.ctor_40CA60(3);
    pNewFileRec->field_0_fileName = reinterpret_cast<char_type*>(ae_malloc_non_zero_4954F0(strlen(pFileItem) + 1));
    strcpy(pNewFileRec->field_0_fileName, pFileItem);
    pNewFileRec->field_4_pResourcesToLoadList = 0;
    pNewFileRec->field_8_type = type;
    pNewFileRec->field_C_id = resourceID;

    auto pNewFilePart1 = ae_new<ResourceManager_FilePartRecord_18>();
    pNewFilePart1->field_8_pCamera = pCamera;
    pNewFilePart1->field_C_fn_arg_pCamera = pFnArg;
    pNewFilePart1->field_10_pFn = pFn;
    pNewFilePart1->field_0_type = type;
    pNewFilePart1->field_4_id = resourceID;
    pNewFilePart1->field_14_bAddUseCount = bAddUseCount;
    pNewFileRec->field_10_file_sections_dArray.Push_Back(pNewFilePart1);
    field_20_files_pending_loading.Push_Back(pNewFileRec);
}

void ResourceManager::LoadResourcesFromList_465150(const char_type* pFileName, ResourceManager::ResourcesToLoadList* pTypeAndIdList, Camera* pCamera, Camera* pFnArg, ResourceManager::TLoaderFn pFn, s16 addUseCount)
{
    // Already loaded flag ??
    if (!(pTypeAndIdList->field_0_count & ~0x80000000))
    {
        return;
    }

    // Check if all resources are already loaded
    bool allResourcesLoaded = true;
    for (s32 i = 0; i < pTypeAndIdList->field_0_count; i++)
    {
        if (!GetLoadedResource_49C2A0(
                pTypeAndIdList->field_4_items[i].field_0_type,
                pTypeAndIdList->field_4_items[i].field_4_res_id,
                0, 0))
        {
            // A resource we need is missing
            allResourcesLoaded = false;
            break;
        }
    }

    // All resources that we required are already loaded
    if (allResourcesLoaded && pCamera) // mlg: Check if pCamera is null, so we can spawn objects easier.
    {
        for (s32 i = 0; i < pTypeAndIdList->field_0_count; i++)
        {
            // Add to array optionally bumping the ref count
            pCamera->field_0.Push_Back_40CAF0(GetLoadedResource_49C2A0(
                pTypeAndIdList->field_4_items[i].field_0_type,
                pTypeAndIdList->field_4_items[i].field_4_res_id,
                addUseCount,
                0));
        }
        return;
    }

#ifdef TETHYS_SATURN
    // SATURN (AO bt990, U1a A11): whole BNDs are level-scoped too; after the
    // resident check, so a list that IS loaded still takes its refs.
    if (!Tethys_RequestReadable(pFileName))
    {
        return;
    }
#endif

    // Check if we already have a file record or not
    ResourceManager_FileRecord* pFoundFileRecord = nullptr;
    for (s32 fileIdx1 = 0; fileIdx1 < field_20_files_pending_loading.Size(); fileIdx1++)
    {
        ResourceManager_FileRecord* pFileRec = field_20_files_pending_loading.ItemAt(fileIdx1);
        if (!pFileRec)
        {
            break;
        }

        if (strcmp(pFileName, pFileRec->field_0_fileName) == 0)
        {
            pFoundFileRecord = pFileRec;
            break;
        }
    }

    // Create a new record or use the one we found
    auto pNewFileRec = pFoundFileRecord ? pFoundFileRecord : ae_new<ResourceManager_FileRecord>();
    if (!pFoundFileRecord)
    {
        // Only do ctor stuff if we created a new record
#ifdef TETHYS_SATURN
        Tethys_NoteFlipFile(pFileName); // bt1061
#endif

        // TODO: De-inline ctor
        pNewFileRec->field_10_file_sections_dArray.ctor_40CA60(3);
        pNewFileRec->field_0_fileName = reinterpret_cast<char_type*>(ae_malloc_non_zero_4954F0(strlen(pFileName) + 1));
        strcpy(pNewFileRec->field_0_fileName, pFileName);
        pNewFileRec->field_4_pResourcesToLoadList = pTypeAndIdList;
        pNewFileRec->field_8_type = 0;
        pNewFileRec->field_C_id = 0;
    }

    // Create a file part record for each item
    for (s32 i = 0; i < pTypeAndIdList->field_0_count; i++)
    {
        auto pNewFilePart = ae_new<ResourceManager_FilePartRecord_18>();
        pNewFilePart->field_0_type = pTypeAndIdList->field_4_items[i].field_0_type;
        pNewFilePart->field_4_id = pTypeAndIdList->field_4_items[i].field_4_res_id;
        pNewFilePart->field_8_pCamera = pCamera;
        pNewFilePart->field_10_pFn = pFn;
        pNewFilePart->field_C_fn_arg_pCamera = pFnArg;
        pNewFilePart->field_14_bAddUseCount = addUseCount;
        pNewFileRec->field_10_file_sections_dArray.Push_Back(pNewFilePart);
    }

    // Only add to array if we created it
    if (!pFoundFileRecord)
    {
        field_20_files_pending_loading.Push_Back(pNewFileRec);
    }
}

void ResourceManager::LoadResourceFile_465460(const char_type* filename, Camera* pCam, Camera* pCam2, ResourceManager::TLoaderFn pFn, s16 bAddUseCount)
{
#ifdef TETHYS_SATURN
    // SATURN (decision D-R1, U1a A4/A14): a .CAM record is a CZ4 container whose
    // Bits payload belongs to VDP2, never a chunk chain this loader could stage
    // and walk (Move_Resources would read the container header as a chunk). It
    // streams NOW, synchronously, into the camera: Map::Load_Path_Items_482C10
    // calls the stream itself, so this is the boot splash's route (Game.cpp:552,
    // STP01C25 into a local Camera) and the backstop for any other caller. The
    // order matches the PC's: the camera was queued first, then read first.
    if (Tethys_IsCamName(filename))
    {
        if (!pCam)
        {
            Tethys_NamedFatal("CAM without camera ", filename);
        }
        Tethys_StreamCamFileNamed(pCam, filename, false);
        if (pFn)
        {
            pFn(pCam2);
        }
        return;
    }
    if (!Tethys_RequestReadable(filename))
    {
        return; // AO bt990: absent from this level's LVL, skipped and counted
    }
    Tethys_NoteFlipFile(filename); // bt1061
#endif
    auto pFileRecord = ae_new<ResourceManager_FileRecord>();
    if (pFileRecord)
    {
        // TODO: De-inline this ctor
        pFileRecord->field_10_file_sections_dArray.ctor_40CA60(3);
    }

    pFileRecord->field_0_fileName = reinterpret_cast<char_type*>(ae_malloc_non_zero_4954F0(strlen(filename) + 1));
    strcpy(pFileRecord->field_0_fileName, filename);
    pFileRecord->field_4_pResourcesToLoadList = 0;
    pFileRecord->field_8_type = 0;
    pFileRecord->field_C_id = 0;

    auto pFilePart = ae_new<ResourceManager_FilePartRecord_18>();
    // TODO: De-inline this ctor
    pFilePart->field_8_pCamera = pCam;
    pFilePart->field_C_fn_arg_pCamera = pCam2;
    pFilePart->field_10_pFn = pFn;
    pFilePart->field_0_type = 0;
    pFilePart->field_4_id = 0;
    pFilePart->field_14_bAddUseCount = bAddUseCount;
    // Add the part to the file
    pFileRecord->field_10_file_sections_dArray.Push_Back(pFilePart);

    // Add the file to the array
    field_20_files_pending_loading.Push_Back(pFileRecord);
}

void ResourceManager::LoadingLoop_465590(s16 bShowLoadingIcon)
{
    GetGameAutoPlayer().DisableRecorder();

    while (!field_20_files_pending_loading.IsEmpty())
    {
        SYS_EventsPump_494580();
        VUpdate(); // vLoadFile_StateMachine_464A70 - process loading of files
        PSX_VSync_4F6170(0);
        const s32 ticks = loading_ticks_5C1BAC++ + 1;
        if (bShowLoadingIcon && !bHideLoadingIcon_5C1BAA && ticks > 180)
        {
            // Render everything in the ordering table including the loading icon
            Game_ShowLoadingIcon_482D80();
        }
    }

    GetGameAutoPlayer().EnableRecorder();
}

void ResourceManager::Shutdown_465610()
{
    // Clear out every file in the files array
    auto pFilesArray = &field_20_files_pending_loading;
    s16 fileIdx = 0;
    DynamicArrayIter iter;
    iter.field_0_pDynamicArray = pFilesArray;
    iter.field_4_idx = 0;
    while (fileIdx < pFilesArray->Size())
    {
        // Iterate and clear out the file parts in this item
        ResourceManager_FileRecord* pFileRec = pFilesArray->ItemAt(fileIdx);
        iter.field_4_idx = fileIdx + 1;
        if (!pFileRec)
        {
            break;
        }

        auto pFileSectionsArray = &pFileRec->field_10_file_sections_dArray;
        s16 fileSectionIdx = 0;
        DynamicArrayIter fileSectionsIter;
        fileSectionsIter.field_0_pDynamicArray = &pFileRec->field_10_file_sections_dArray;
        fileSectionsIter.field_4_idx = 0;

        while (fileSectionIdx < pFileSectionsArray->Size())
        {
            ResourceManager_FilePartRecord_18* pFileSection = pFileSectionsArray->ItemAt(fileSectionIdx);
            fileSectionsIter.field_4_idx = fileSectionIdx + 1;
            if (!pFileSection)
            {
                break;
            }
            fileSectionsIter.Remove_At_Iter_40CCA0();
            ae_delete_free_495540(pFileSection);

            fileSectionIdx = fileSectionsIter.field_4_idx;
        }

        iter.Remove_At_Iter_40CCA0();
        ae_non_zero_free_495560(pFileRec->field_0_fileName);

        pFileRec->dtor_464EA0();
        ae_delete_free_495540(pFileRec);

        fileIdx = iter.field_4_idx;
    }
}

void ResourceManager::Free_Resources_For_Camera_4656F0(const Camera* pCamera)
{
    DynamicArrayIter filesIter;
    filesIter.field_0_pDynamicArray = &field_20_files_pending_loading;
    filesIter.field_4_idx = 0;
    while (filesIter.field_4_idx < filesIter.field_0_pDynamicArray->field_4_used_size)
    {
        ResourceManager_FileRecord* pFileItem = field_20_files_pending_loading.ItemAt(filesIter.field_4_idx);
        filesIter.field_4_idx++;

        if (!pFileItem)
        {
            break;
        }

        if (pFileItem != field_2C_pFileItem)
        {
            DynamicArrayIter filePartsIter;
            filePartsIter.field_0_pDynamicArray = &pFileItem->field_10_file_sections_dArray;
            filePartsIter.field_4_idx = 0;
            while (filePartsIter.field_4_idx < pFileItem->field_10_file_sections_dArray.Size())
            {
                ResourceManager_FilePartRecord_18* pFilePartItem = pFileItem->field_10_file_sections_dArray.ItemAt(filePartsIter.field_4_idx);
                filePartsIter.field_4_idx++;
                if (!pFilePartItem)
                {
                    break;
                }

                if (pFilePartItem->field_C_fn_arg_pCamera == pCamera)
                {
                    filePartsIter.Remove_At_Iter_40CCA0();
                    ae_delete_free_495540(pFilePartItem);
                }
            }

            if (pFileItem->field_10_file_sections_dArray.IsEmpty())
            {
                filesIter.Remove_At_Iter_40CCA0();
                ae_non_zero_free_495560(pFileItem->field_0_fileName);
                pFileItem->dtor_464EA0();
                ae_delete_free_495540(pFileItem);
            }
        }
    }
}

BaseGameObject* ResourceManager::VDestructor(s32 flags)
{
    return vdtor_4649B0(flags);
}

void ResourceManager::VUpdate()
{
    vLoadFile_StateMachine_464A70();
}

EXPORT void ResourceManager::VScreenChanged_464EC0()
{
    // Empty
}

void ResourceManager::VScreenChanged()
{
    VScreenChanged_464EC0();
}

ResourceManager::ResourceManager()
{
}

#ifdef TETHYS_SATURN
extern "C" [[noreturn]] void Tethys_Fatal(const char_type* msg);
#endif

void CC ResourceManager::Init_49BCE0()
{
#ifdef TETHYS_SATURN
    // SATURN: a heap that was never bound is a seam fault, named, not a write
    // through a null pointer at the first allocation.
    if (!sResourceHeap_5D29F4 || kResHeapSize < sizeof(Header) * 2)
    {
        Tethys_Fatal("AE resource heap not bound");
    }
#endif
    for (s32 i = 1; i < kLinkedListArraySize - 1; i++)
    {
        sResourceLinkedList_5D1E30[i].field_0_ptr = nullptr;
        sResourceLinkedList_5D1E30[i].field_4_pNext = &sResourceLinkedList_5D1E30[i + 1];
    }

    sResourceLinkedList_5D1E30[kLinkedListArraySize - 1].field_4_pNext = nullptr;

    sResourceLinkedList_5D1E30[0].field_0_ptr = &sResourceHeap_5D29F4[sizeof(Header)];
    sResourceLinkedList_5D1E30[0].field_4_pNext = nullptr;

    Header* pHeader = Get_Header_49C410(&sResourceLinkedList_5D1E30[0].field_0_ptr);
    pHeader->field_0_size = kResHeapSize;
    pHeader->field_8_type = Resource_Free;

    sFirstLinkedListItem_5D29EC = &sResourceLinkedList_5D1E30[0];
    sSecondLinkedListItem_5D29E8 = &sResourceLinkedList_5D1E30[1];

    // TODO: Check this is correct
    spResourceHeapEnd_AB49F8 = &sResourceHeap_5D29F4[kResHeapSize - 1];
}

ResourceManager::ResourceHeapItem* CC ResourceManager::Push_List_Item_49BD70()
{
    auto old = sSecondLinkedListItem_5D29E8;
#ifdef TETHYS_SATURN
    // SATURN (AO twin, U1a A11): the 375-node pool is a HARD cap on live blocks;
    // exhausted, the callers (Split_block, Move_Resources) would write through a
    // null node: name it instead.
    if (!old)
    {
        Tethys_Fatal("resource node pool exhausted (375)");
    }
#endif
    sSecondLinkedListItem_5D29E8 = sSecondLinkedListItem_5D29E8->field_4_pNext;
    return old;
}

void CC ResourceManager::Pop_List_Item_49BD90(ResourceManager::ResourceHeapItem* pListItem)
{
    pListItem->field_0_ptr = nullptr;
    pListItem->field_4_pNext = sSecondLinkedListItem_5D29E8; // point to the current
    sSecondLinkedListItem_5D29E8 = pListItem;                // set current to old
}

ResourceManager::ResourceHeapItem* CC ResourceManager::Split_block_49BDC0(ResourceManager::ResourceHeapItem* pItem, s32 size)
{
    Header* pToSplit = Get_Header_49C410(&pItem->field_0_ptr);
    const u32 sizeForNewRes = pToSplit->field_0_size - size;
    if (sizeForNewRes >= sizeof(Header))
    {
        ResourceHeapItem* pNewListItem = ResourceManager::Push_List_Item_49BD70();
        pNewListItem->field_4_pNext = pItem->field_4_pNext; // New item points to old
        pItem->field_4_pNext = pNewListItem;                // Old item points to new

        pNewListItem->field_0_ptr = pItem->field_0_ptr + size; // Point the split point

        // Init header of split item
        Header* pHeader = Get_Header_49C410(&pNewListItem->field_0_ptr);
        pHeader->field_0_size = sizeForNewRes;
        pHeader->field_8_type = Resource_Free;
        pHeader->field_4_ref_count = 0;
        pHeader->field_C_id = 0;

        // Update old size
        pToSplit->field_0_size = size;
    }

    return pItem;
}

s32 CC ResourceManager::SEQ_HashName_49BE30(const char_type* seqFileName)
{
    // Clamp max len
    size_t seqFileNameLength = strlen(seqFileName) - 1;
    if (seqFileNameLength > 8)
    {
        seqFileNameLength = 8;
    }

    // Iterate each s8 to calculate hash
    u32 hashId = 0;
    for (size_t index = 0; index < seqFileNameLength; index++)
    {
        char_type letter = seqFileName[index];
        if (letter == '.')
        {
            break;
        }

        const u32 temp = 10 * hashId;
        if (letter < '0' || letter > '9')
        {
            if (letter >= 'a')
            {
                if (letter <= 'z')
                {
                    letter -= ' ';
                }
            }
            hashId = letter % 10 + temp;
        }
        else
        {
            hashId = index || letter != '0' ? temp + letter - '0' : temp + 9;
        }
    }
    return hashId;
}

#ifdef TETHYS_SATURN
// SATURN (AO ao262.2/ao262.3/bt814 twin, U1d T2): the allocator's two
// decisions, split. bReclaimOnFail = compact and retry (it MOVES every unlocked
// block: never from a caller holding a raw pointer into one); bFatalOnFail = a
// refusal is the named RES NULL death screen, else the null is handed back to a
// caller written to take it and counted (`rn`). Before any counted refusal the
// menu pins (a loan, never a tenant) go back and the ask gets one more try.
// Alloc_New_Resource_Impl is ImplEx(..., true, true): upstream's behaviour (one
// compaction and a retry) plus the fatal, because upstream's callers do not
// test the handle and on SH-2 a null *deref reads the BIOS vector table.
u8** ResourceManager::Alloc_New_Resource_ImplEx(u32 type, u32 id, u32 size, bool locked, ResourceManager::BlockAllocMethod allocType, bool bReclaimOnFail, bool bFatalOnFail)
{
    u8** ppNewRes = Allocate_New_Block_49BFB0(size + sizeof(Header), allocType);
    if (!ppNewRes && bReclaimOnFail)
    {
        Reclaim_Memory_49C470(0);
        ppNewRes = Allocate_New_Block_49BFB0(size + sizeof(Header), allocType);
        if (!ppNewRes && Tethys_MenuPinsRelease())
        {
            Reclaim_Memory_49C470(0);
            ppNewRes = Allocate_New_Block_49BFB0(size + sizeof(Header), allocType);
        }
    }

    if (ppNewRes)
    {
        Header* pHeader = Get_Header_49C410(ppNewRes);
        pHeader->field_8_type = type;
        pHeader->field_C_id = id;
        pHeader->field_4_ref_count = 1;
        pHeader->field_6_flags = locked ? ResourceHeaderFlags::eLocked : 0;
        return ppNewRes;
    }
    if (!bReclaimOnFail)
    {
        return nullptr; // a loan or a best-effort ask: refused quietly (bt828)
    }
    if (!bFatalOnFail)
    {
        Tethys_gResSoftNull++;
        if (size > Tethys_gSoftSize) // ao262.10: keep the BIGGEST refusal, not the last
        {
            Tethys_gSoftType = type;
            Tethys_gSoftId = id;
            Tethys_gSoftSize = size;
        }
        return nullptr;
    }
    // ao262.2: name the allocation BEFORE dying, onto rows the fatal does not
    // touch (plain globals only, no heap walk: bt815).
    Tethys_ResNullReport(0u, type, id, size, sManagedMemoryUsedSize_AB4A04, sPeakedManagedMemUsage_AB4A08,
                         kResHeapSize, 0u, 0u);
    Tethys_Fatal("RES NULL");
}
#endif

u8** ResourceManager::Alloc_New_Resource_Impl(u32 type, u32 id, u32 size, bool locked, ResourceManager::BlockAllocMethod allocType)
{
#ifdef TETHYS_SATURN
    return Alloc_New_Resource_ImplEx(type, id, size, locked, allocType, true, true);
#else
    u8** ppNewRes = Allocate_New_Block_49BFB0(size + sizeof(Header), allocType);
    if (!ppNewRes)
    {
        // Failed, try to reclaim some memory and try again.
        Reclaim_Memory_49C470(0);
        ppNewRes = Allocate_New_Block_49BFB0(size + sizeof(Header), allocType);
    }

    if (ppNewRes)
    {
        Header* pHeader = Get_Header_49C410(ppNewRes);
        pHeader->field_8_type = type;
        pHeader->field_C_id = id;
        pHeader->field_4_ref_count = 1;
        pHeader->field_6_flags = locked ? ResourceHeaderFlags::eLocked : 0;
    }

    return ppNewRes;
#endif
}

u8** CC ResourceManager::Alloc_New_Resource_49BED0(u32 type, u32 id, u32 size)
{
    return Alloc_New_Resource_Impl(type, id, size, false, BlockAllocMethod::eFirstMatching);
}

u8** CC ResourceManager::Allocate_New_Locked_Resource_49BF40(u32 type, u32 id, u32 size)
{
    return Alloc_New_Resource_Impl(type, id, size, true, BlockAllocMethod::eLastMatching);
}

u8** CC ResourceManager::Allocate_New_Block_49BFB0(s32 sizeBytes, BlockAllocMethod allocMethod)
{
    ResourceHeapItem* pListItem = sFirstLinkedListItem_5D29EC;
    ResourceHeapItem* pHeapMem = nullptr;
    const u32 size = (sizeBytes + 3) & ~3u; // Rounding ??
    Header* pHeaderToUse = nullptr;
    while (pListItem)
    {
        // Is it a free block?
        Header* pResHeader = Get_Header_49C410(&pListItem->field_0_ptr);
        if (pResHeader->field_8_type == Resource_Free)
        {
            // Keep going till we hit a block that isn't free
            for (ResourceHeapItem* i = pListItem->field_4_pNext; i; i = pListItem->field_4_pNext)
            {
                Header* pHeader = Get_Header_49C410(&i->field_0_ptr);
                if (pHeader->field_8_type != Resource_Free)
                {
                    break;
                }

                // Combine up the free blocks
                pResHeader->field_0_size += pHeader->field_0_size;
                pListItem->field_4_pNext = i->field_4_pNext;
                Pop_List_Item_49BD90(i);
            }

            // Size will be bigger now that we've freed at least 1 resource
            if (pResHeader->field_0_size >= size)
            {
                switch (allocMethod)
                {
                    case BlockAllocMethod::eFirstMatching:
                        // Use first matching item
                        sManagedMemoryUsedSize_AB4A04 += size;
                        if (sManagedMemoryUsedSize_AB4A04 >= sPeakedManagedMemUsage_AB4A08)
                        {
                            sPeakedManagedMemUsage_AB4A08 = sManagedMemoryUsedSize_AB4A04;
                        }
                        return &Split_block_49BDC0(pListItem, size)->field_0_ptr;
                    case BlockAllocMethod::eNearestMatching:
                        // Find nearest matching item
                        if (pResHeader->field_0_size < pHeaderToUse->field_0_size)
                        {
                            pHeapMem = pListItem;
                            pHeaderToUse = pResHeader;
                        }
                        break;
                    case BlockAllocMethod::eLastMatching:
                        // Will always to set to the last most free item
                        pHeapMem = pListItem;
                        pHeaderToUse = pResHeader;
                        break;
                }
            }
        }

        pListItem = pListItem->field_4_pNext;
    }

    if (!pHeapMem)
    {
        // Allocation failure
        sAllocationFailed_AB4A0C = 1;
        return nullptr;
    }

    sManagedMemoryUsedSize_AB4A04 += size;
    if (sManagedMemoryUsedSize_AB4A04 >= sPeakedManagedMemUsage_AB4A08)
    {
        sPeakedManagedMemUsage_AB4A08 = sManagedMemoryUsedSize_AB4A04;
    }

    switch (allocMethod)
    {
        // Note: eFirstMatching case not possible here as pHeapMem case would have early returned
        case BlockAllocMethod::eNearestMatching:
            return &ResourceManager::Split_block_49BDC0(pHeapMem, size)->field_0_ptr;

        case BlockAllocMethod::eLastMatching:
            if (pHeaderToUse->field_0_size - size >= sizeof(Header))
            {
                return &Split_block_49BDC0(pHeapMem, pHeaderToUse->field_0_size - size)->field_4_pNext->field_0_ptr;
            }
            else
            {
                // No need to split as the size must be exactly the size of a resource header
                return &pHeapMem->field_0_ptr;
            }
            break;

        // Should be impossible to get here
        default:
            return nullptr;
    }
}

s32 CC ResourceManager::LoadResourceFile_49C130(const char_type* filename, TLoaderFn pFn, Camera* a4, Camera* pCamera)
{
    pResourceManager_5C1BB0->LoadResourceFile_465460(filename, pCamera, a4, pFn, pCamera != nullptr);
    return 0;
}

s16 CC ResourceManager::LoadResourceFile_49C170(const char_type* pFileName, Camera* pCamera)
{
    pResourceManager_5C1BB0->LoadResourceFile_465460(pFileName, pCamera, nullptr, nullptr, pCamera != nullptr);
    pResourceManager_5C1BB0->LoadingLoop_465590(0);
    return 1;
}

#if defined(TETHYS_GAME_AE)
s16 CC ResourceManager::Move_Resources_To_DArray_49C1C0(u8** ppRes, DynamicArrayT<u8*>* pArray, u32 blockBytes)
#else
s16 CC ResourceManager::Move_Resources_To_DArray_49C1C0(u8** ppRes, DynamicArrayT<u8*>* pArray)
#endif
{
    ResourceHeapItem* pItemToAdd = (ResourceHeapItem*) ppRes;
    Header* pHeader = Get_Header_49C410(&pItemToAdd->field_0_ptr);
#if defined(TETHYS_GAME_AE)
    // SATURN (AO's twin, AliveLibAO/ResourceManager.cpp:3540): the end of the
    // loaded file block. The chunk walk strides by field_0_size; a chain that
    // lacks a clean terminator would run past the block into the adjacent heap
    // and fabricate a resource there. The positional bound catches the over-run
    // whatever the heap size (the size backstop below alone is heap-relative).
    u8* const pBlockEnd = blockBytes ? reinterpret_cast<u8*>(pHeader) + blockBytes : nullptr;
#endif
    if (pHeader->field_8_type != Resource_End)
    {
        // While we're not at the end resource and the pointer/size are a multiple of 4? (how can they not be when they are all allocated aligned??)
        while (pHeader->field_8_type != Resource_Pend && pHeader->field_0_size && !(pHeader->field_0_size & 3))
        {
            if (pArray)
            {
                pArray->Push_Back_40CAF0(pItemToAdd);
            }

            pHeader = (Header*) ((s8*) pHeader + pHeader->field_0_size);

            // Out of heap space
#if defined(TETHYS_GAME_AE)
            // SATURN: positional bound first (the over-run past the block is
            // the real corruption signal), then the original size backstop.
            if ((pBlockEnd && reinterpret_cast<u8*>(pHeader) >= pBlockEnd)
                || pHeader->field_0_size >= kResHeapSize)
            {
                return 1;
            }
#else
            if (pHeader->field_0_size >= kResHeapSize)
            {
                return 1;
            }
#endif

            ResourceHeapItem* pNewListItem = ResourceManager::Push_List_Item_49BD70();
            pNewListItem->field_4_pNext = pItemToAdd->field_4_pNext;
            pItemToAdd->field_4_pNext = pNewListItem;
            pNewListItem->field_0_ptr = (u8*) &pHeader[1]; // point after header
            pItemToAdd = pNewListItem;

            // No more resources to add
            if (pHeader->field_8_type == Resource_End)
            {
                break;
            }
        }
    }

    if (pHeader)
    {
        pHeader->field_8_type = Resource_Free;
        if (pItemToAdd->field_4_pNext)
        {
            // Size of next item - location of current res
            // TODO 64bit warning
            pHeader->field_0_size = static_cast<u32>(pItemToAdd->field_4_pNext->field_0_ptr - (u8*) pHeader - sizeof(Header));
        }
        else
        {
            // Isn't a next item so use ptr to end of heap - location of current res
            // TODO: 64bit warning
            pHeader->field_0_size = static_cast<u32>(spResourceHeapEnd_AB49F8 - (u8*) pHeader);
        }
        sManagedMemoryUsedSize_AB4A04 -= pHeader->field_0_size;
    }
    return 1;
}

u8** CC ResourceManager::GetLoadedResource_49C2A0(u32 type, u32 resourceID, u16 addUseCount, u16 bLock)
{
    // Iterate all list items
    ResourceHeapItem* pListIter = sFirstLinkedListItem_5D29EC;
    while (pListIter)
    {
        // Find something that matches the type and resource ID
        Header* pResHeader = Get_Header_49C410(&pListIter->field_0_ptr);
        if (pResHeader->field_8_type == type && pResHeader->field_C_id == resourceID)
        {
            if (addUseCount)
            {
                pResHeader->field_4_ref_count++;
            }

            if (bLock)
            {
                pResHeader->field_6_flags |= ResourceHeaderFlags::eLocked;
            }

            return &pListIter->field_0_ptr;
        }

        pListIter = pListIter->field_4_pNext;
    }
    return nullptr;
}

void CC ResourceManager::Inc_Ref_Count_49C310(u8** ppRes)
{
    Get_Header_49C410(ppRes)->field_4_ref_count++;
}

s16 CC ResourceManager::FreeResource_49C330(u8** handle)
{
    if (!handle)
    {
        return 1;
    }
    return FreeResource_Impl_49C360(*handle);
}

s16 CC ResourceManager::FreeResource_Impl_49C360(u8* handle)
{
    if (handle)
    {
        Header* pHeader = Get_Header_49C410(&handle);
        if (pHeader->field_4_ref_count)
        {
            // Decrement ref count, if its not zero then we can't free it yet
            pHeader->field_4_ref_count--;
            if (pHeader->field_4_ref_count > 0)
            {
                return 0;
            }
#ifdef TETHYS_SATURN
            // SATURN (AO 427.ao.5/429.ao.1 twin, stream R2's request): the cel
            // cache (AliveLibCommon/TethysCelCache.hpp) keys a decoded cel on
            // its source ADDRESS inside a resource block. Once this block is
            // marked free its address can be handed to another resource whose
            // cel could land at the same address with the same length: a false
            // hit serving the wrong pixels. Flush here, where a block REALLY
            // frees (a decrement that leaves holders frees nothing), exactly as
            // AO does; the compactor keeps its own flush (Reclaim_Memory_49C470).
            Tethys_DbufScratchForget();
#endif
            pHeader->field_8_type = Resource_Free;
            pHeader->field_6_flags = 0;
            sManagedMemoryUsedSize_AB4A04 -= pHeader->field_0_size;
        }
    }
    return 1;
}

ResourceManager::Header* CC ResourceManager::Get_Header_49C410(u8** ppRes)
{
    return reinterpret_cast<Header*>((*ppRes - sizeof(Header)));
}

#ifdef TETHYS_SATURN
// SATURN (AO ao262.11 twin, AliveLibAO/ResourceManager.cpp:3806-3831; asked by
// AliveLibCommon/TethysCelCache.hpp and AE Animation.cpp): the payload bytes of
// a live Animation block, 0 for any handle that is not one. It lets Animation
// bound a frame-table offset against the block it is about to apply it to
// without importing the Header layout. The type test is the guard: a handle
// that is not an Animation chunk reports 0 and the caller passes unchanged.
extern "C" u32 Tethys_AnimBlockBytes(u8** ppRes)
{
    if (!ppRes || !*ppRes || (reinterpret_cast<u32>(*ppRes) & 3u))
    {
        return 0;
    }
    const ResourceManager::Header* pHdr = ResourceManager::Get_Header_49C410(ppRes);
    if (pHdr->field_8_type != ResourceManager::Resource_Animation)
    {
        return 0;
    }
    // field_0_size counts the header too; the caller wants payload bytes.
    if (pHdr->field_0_size <= sizeof(ResourceManager::Header))
    {
        return 0;
    }
    return pHdr->field_0_size - sizeof(ResourceManager::Header);
}
#endif

void CC ResourceManager::Reclaim_Memory_49C470(u32 sizeToReclaim)
{
    if (sResources_Pending_Loading_AB49F4 != 0)
    {
        return;
    }

#ifdef TETHYS_SATURN
    // SATURN (AO 427.ao.5 twin, stream R2's request): the compactor MOVES
    // blocks, so every cel address the cache holds may now belong to another
    // resource (a block sliding into a vacated address can reproduce an old
    // cel's address with an equal length). Flush; the next decode refills.
    Tethys_DbufScratchForget();
#endif

    // If we failed to allocate a block or no size was passed then attempt to reclaim the whole heap
    if (sAllocationFailed_AB4A0C || sizeToReclaim == 0)
    {
        sizeToReclaim = kResHeapSize;
        sAllocationFailed_AB4A0C = 0;
    }

    ResourceHeapItem* pListItem = sFirstLinkedListItem_5D29EC;
    ResourceHeapItem* pToUpdate = nullptr;

    while (pListItem)
    {
        Header* pCurrentHeader = Get_Header_49C410(&pListItem->field_0_ptr);
        if (pCurrentHeader->field_8_type == Resource_Free)
        {
            ResourceHeapItem* pNext = pListItem->field_4_pNext;
            if (!pNext)
            {
                return;
            }

            Header* pNextHeader = Get_Header_49C410(&pNext->field_0_ptr);
            if (pNextHeader->field_8_type == Resource_Free)
            {
                // Next block is also free, so we can merge them together
                ResourceHeapItem* pToRemove = pListItem->field_4_pNext;
                pCurrentHeader->field_0_size += pNextHeader->field_0_size;
                pListItem->field_4_pNext = pNext->field_4_pNext;
                Pop_List_Item_49BD90(pToRemove);
            }
            else
            {
                u32 sizeToMove = 0;
                if (pNextHeader->field_6_flags & ResourceHeaderFlags::eOnlyAHeader)
                {
                    sizeToMove = sizeof(Header);
                }
                else
                {
                    sizeToMove = pNextHeader->field_0_size;
                }

                if (pNextHeader->field_6_flags & ResourceHeaderFlags::eLocked || sizeToMove > sizeToReclaim)
                {
#ifdef TETHYS_SATURN
                    // SATURN (AO 454.ao.1 twin, U1a A10): free space right below a
                    // path block that fits the move: without its lock
                    // (Map::GetPathResourceBlockPtrs) this compaction would have
                    // moved it under Path::Loader_4DB800's raw Path_TLV*. `pp`.
                    if (pNextHeader->field_8_type == Resource_Path && sizeToMove <= sizeToReclaim)
                    {
                        Tethys_gPathPinned++;
                    }
#endif
                    // Locked or trying to move more than requested, skip to next
                    pToUpdate = pListItem;
                    pListItem = pListItem->field_4_pNext;
                }
                else
                {
                    sizeToReclaim -= sizeToMove;
                    const u32 savedSize = pCurrentHeader->field_0_size;
                    u8* pDataStart = pNext->field_0_ptr - sizeof(Header);
                    if (sizeToMove > 0)
                    {
                        const size_t offset = (s8*) pCurrentHeader - (s8*) pNextHeader;
#ifdef TETHYS_SATURN
                        // SATURN (AO 446.ao.9 twin): by longwords, not newlib's
                        // byte memmove (blocks and headers are 4-aligned).
                        Tethys_MoveDown(pDataStart + offset, pDataStart, sizeToMove);
#else
                        memmove(pDataStart + offset, pDataStart, sizeToMove);
#endif
                    }

                    // Get resource header after the current one
                    Header* pNextResHeader = (Header*) ((s8*) pCurrentHeader + pCurrentHeader->field_0_size);
                    pNextResHeader->field_0_size = savedSize;
                    pNextResHeader->field_8_type = Resource_Free;

                    pNext->field_0_ptr = (u8*) &pCurrentHeader[1];     // Data starts after header
                    pListItem->field_0_ptr = (u8*) &pNextResHeader[1]; // Data starts after header
                    pListItem->field_4_pNext = pNext->field_4_pNext;
                    pNext->field_4_pNext = pListItem;

                    if (pToUpdate)
                    {
                        pToUpdate->field_4_pNext = pNext;
                    }
                    else
                    {
                        sFirstLinkedListItem_5D29EC = pNext;
                    }
                    pToUpdate = pNext;
                }
            }
        }
        else
        {
            // Not a free block, so move to the next item
            pToUpdate = pListItem;
            pListItem = pListItem->field_4_pNext;
        }
    }
}

void CC ResourceManager::Increment_Pending_Count_49C5F0()
{
    ++sResources_Pending_Loading_AB49F4;
}
void CC ResourceManager::Decrement_Pending_Count_49C610()
{
    --sResources_Pending_Loading_AB49F4;
}

void CC ResourceManager::Set_Header_Flags_49C650(u8** ppRes, s16 flags)
{
    Get_Header_49C410(ppRes)->field_6_flags |= flags;
}

void CC ResourceManager::Free_Resource_Of_Type_49C6B0(u32 type)
{
    ResourceHeapItem* pListItem = sFirstLinkedListItem_5D29EC;
    while (pListItem)
    {
        // Free it if the type matches and its not flagged as never free
        Header* pHeader = Get_Header_49C410(&pListItem->field_0_ptr);
        if (pHeader->field_8_type == type && !(pHeader->field_6_flags & ResourceHeaderFlags::eNeverFree))
        {
#ifdef TETHYS_SATURN
            // SATURN: this sweep frees blocks WITHOUT FreeResource_Impl (the
            // quickload/death level reload frees every Anim and Palt at once,
            // Map::GoTo_Camera_481890), so the cel-cache flush rule of
            // FreeResource_Impl_49C360 applies here too: a freed Animation
            // block's address may be re-tenanted before any compaction runs.
            // AO has no flush at its twin; one call per freed block, rare.
            Tethys_DbufScratchForget();
#endif
            pHeader->field_8_type = Resource_Free;
            pHeader->field_6_flags = 0;
            pHeader->field_4_ref_count = 0;

            sManagedMemoryUsedSize_AB4A04 -= pHeader->field_0_size;
        }
        pListItem = pListItem->field_4_pNext;
    }
}

void CC ResourceManager::NoEffect_49C700()
{
    // NOTE: Does nothing because the real func just seems to try to tally
    // up some sort of stat that is never used.
}

#ifdef TETHYS_SATURN
// ============================================================================
// SATURN (stream R1, U1a A12): HEAP FORENSICS AND THE LOADER PROBES, AO's
// (AliveLibAO/ResourceManager.cpp:1293-1633) over Exoddus's heap and singleton.
// Read by src/sys_saturn.cxx (overlay rows, the wedge census, the death screens),
// src/renderer_saturn.cxx (the OT fatal's owner) and src/cd_saturn.cxx (the
// record merge). Every walk is bounded and range-checks its pointers: they run
// on death screens over a heap that may be the reason for the death.
// ============================================================================

// Is p a plausible pointer for heap bookkeeping (HWRAM, LWRAM, cartridge)?
static bool Tethys_PtrSane(const void* p)
{
    const u32 a = reinterpret_cast<u32>(p);
    if (a & 3)
    {
        return false;
    }
    return (a >= 0x06000000u && a < 0x06100000u)     // HWRAM
        || (a >= 0x00200000u && a < 0x00300000u)     // LWRAM
        || (a >= 0x02400000u && a < 0x02800000u)     // cartridge, cached
        || (a >= 0x22400000u && a < 0x22800000u);    // cartridge, uncached
}

// 0 = every header stride is sane up to the heap end, else the byte offset of
// the first corrupt header (never 0).
s32 Tethys_HeapCheck()
{
    const u8* pBase = sResourceHeap_5D29F4;
    if (!pBase)
    {
        return 0;
    }
    const u8* pCur = pBase;
    const u8* pEnd = pBase + kResHeapSize;
    u32 guard = 0;
    while (pCur + sizeof(ResourceManager::Header) <= pEnd && guard++ <= 4096u)
    {
        const ResourceManager::Header* pHdr = reinterpret_cast<const ResourceManager::Header*>(pCur);
        if (pHdr->field_0_size < sizeof(ResourceManager::Header) || (pHdr->field_0_size & 3)
            || pCur + pHdr->field_0_size > pEnd)
        {
            return static_cast<s32>(pCur - pBase) | 1;
        }
        pCur += pHdr->field_0_size;
    }
    return 0;
}

// Physical usage by stride walk: used bytes and live (non-Free) blocks. The
// counter sManagedMemoryUsedSize_AB4A04 drifts by design (a split declined for
// a remainder under a Header is subtracted at its larger real size on free), so
// a leak gate reads what is physically there. -1 = corrupt stride.
s32 Tethys_HeapUsage(u32* pUsedBytes, u32* pLiveBlocks)
{
    *pUsedBytes = 0;
    *pLiveBlocks = 0;
    const u8* pBase = sResourceHeap_5D29F4;
    if (!pBase)
    {
        return 0;
    }
    const u8* pCur = pBase;
    const u8* pEnd = pBase + kResHeapSize;
    u32 guard = 0;
    while (pCur + sizeof(ResourceManager::Header) <= pEnd && guard++ <= 4096u)
    {
        const ResourceManager::Header* pHdr = reinterpret_cast<const ResourceManager::Header*>(pCur);
        if (pHdr->field_0_size < sizeof(ResourceManager::Header) || (pHdr->field_0_size & 3)
            || pCur + pHdr->field_0_size > pEnd)
        {
            return -1;
        }
        if (pHdr->field_8_type != ResourceManager::Resource_Free)
        {
            *pUsedBytes += pHdr->field_0_size;
            (*pLiveBlocks)++;
        }
        pCur += pHdr->field_0_size;
    }
    return 0;
}

// AO 425.ao.1: the largest CONTIGUOUS free run (adjacent free blocks coalesced,
// as Allocate_New_Block does before measuring a candidate). Close to the free
// total = the heap is full; far below it = fragmented. They need opposite work.
u32 Tethys_HeapLargestFree()
{
    const u8* pBase = sResourceHeap_5D29F4;
    if (!pBase)
    {
        return 0;
    }
    const u8* pCur = pBase;
    const u8* pEnd = pBase + kResHeapSize;
    u32 best = 0;
    u32 run = 0;
    u32 guard = 0;
    while (pCur + sizeof(ResourceManager::Header) <= pEnd && guard++ <= 4096u)
    {
        const ResourceManager::Header* pHdr = reinterpret_cast<const ResourceManager::Header*>(pCur);
        if (pHdr->field_0_size < sizeof(ResourceManager::Header) || (pHdr->field_0_size & 3)
            || pCur + pHdr->field_0_size > pEnd)
        {
            return best; // corrupt stride: report what was measured
        }
        if (pHdr->field_8_type == ResourceManager::Resource_Free)
        {
            run += pHdr->field_0_size;
            if (run > best)
            {
                best = run;
            }
        }
        else
        {
            run = 0;
        }
        pCur += pHdr->field_0_size;
    }
    return best;
}

// The idx-th biggest non-Free block (type, id, size, use count), by selection
// scan over the used chain; only ever runs on a frozen fatal or wedge screen.
void Tethys_HeapTop(s32 idx, u32* pType, u32* pId, u32* pSize, u32* pRef)
{
    *pType = 0;
    *pId = 0;
    *pSize = 0;
    *pRef = 0;
    u32 prevSize = 0xFFFFFFFFu;
    u32 prevId = 0xFFFFFFFFu;
    for (s32 rank = 0; rank <= idx; rank++)
    {
        u32 bestSize = 0;
        u32 bestType = 0;
        u32 bestId = 0;
        u32 bestRef = 0;
        u32 guard = 0;
        for (ResourceManager::ResourceHeapItem* pItem = sFirstLinkedListItem_5D29EC;
             pItem && guard <= kLinkedListArraySize;
             pItem = pItem->field_4_pNext, guard++)
        {
            if (!Tethys_PtrSane(pItem) || !Tethys_PtrSane(pItem->field_0_ptr))
            {
                break; // corrupted chain: report what accumulated so far
            }
            ResourceManager::Header* pHdr = ResourceManager::Get_Header_49C410(&pItem->field_0_ptr);
            if (pHdr->field_8_type == ResourceManager::Resource_Free)
            {
                continue;
            }
            // Strictly below the previous rank; ties broken by id so equal sizes
            // are enumerated once each.
            if (pHdr->field_0_size > prevSize
                || (pHdr->field_0_size == prevSize && pHdr->field_C_id >= prevId))
            {
                continue;
            }
            if (pHdr->field_0_size > bestSize)
            {
                bestSize = pHdr->field_0_size;
                bestType = pHdr->field_8_type;
                bestId = pHdr->field_C_id;
                bestRef = static_cast<u32>(pHdr->field_4_ref_count);
            }
        }
        prevSize = bestSize;
        prevId = bestId;
        *pType = bestType;
        *pId = bestId;
        *pSize = bestSize;
        *pRef = bestRef;
    }
}

// Is (type, id) resident? -> block size, -1 absent, -3 corrupted chain.
s32 Tethys_HeapFind(u32 type, u32 id)
{
    u32 guard = 0;
    for (ResourceManager::ResourceHeapItem* pItem = sFirstLinkedListItem_5D29EC;
         pItem && guard <= kLinkedListArraySize;
         pItem = pItem->field_4_pNext, guard++)
    {
        if (!Tethys_PtrSane(pItem) || !Tethys_PtrSane(pItem->field_0_ptr))
        {
            return -3;
        }
        ResourceManager::Header* pHdr = ResourceManager::Get_Header_49C410(&pItem->field_0_ptr);
        if (pHdr->field_8_type == type && pHdr->field_C_id == id)
        {
            return static_cast<s32>(pHdr->field_0_size);
        }
    }
    return -1;
}

// The same question asked of the PHYSICAL heap (header to header by size, the
// list ignored): a list miss with a raw hit = the list lost the block.
s32 Tethys_HeapScanRaw(u32 type, u32 id)
{
    const u8* pCur = sResourceHeap_5D29F4;
    const u8* pEnd = sResourceHeap_5D29F4 + kResHeapSize;
    u32 guard = 0;
    while (pCur && pCur + sizeof(ResourceManager::Header) <= pEnd && guard++ <= 2048u)
    {
        const ResourceManager::Header* pHdr = reinterpret_cast<const ResourceManager::Header*>(pCur);
        if (pHdr->field_8_type == type && pHdr->field_C_id == id)
        {
            return static_cast<s32>(pHdr->field_0_size);
        }
        if (pHdr->field_0_size < sizeof(ResourceManager::Header) || (pHdr->field_0_size & 3))
        {
            return -2;
        }
        pCur += pHdr->field_0_size;
    }
    return -1;
}

// Which resident block CONTAINS p? Offset of p inside it (header included) with
// the owner's type and id, or -1 (outside the heap, or a corrupt stride before
// reaching it). Names the object whose data a rogue OT primitive lives in.
s32 Tethys_HeapOwner(const void* p, u32* pType, u32* pId)
{
    *pType = 0;
    *pId = 0;
    const u8* pTarget = static_cast<const u8*>(p);
    const u8* pCur = sResourceHeap_5D29F4;
    const u8* pEnd = sResourceHeap_5D29F4 + kResHeapSize;
    if (!pCur || pTarget < pCur || pTarget >= pEnd)
    {
        return -1;
    }
    u32 guard = 0;
    while (pCur + sizeof(ResourceManager::Header) <= pEnd && guard++ <= 2048u)
    {
        const ResourceManager::Header* pHdr = reinterpret_cast<const ResourceManager::Header*>(pCur);
        if (pHdr->field_0_size < sizeof(ResourceManager::Header) || (pHdr->field_0_size & 3))
        {
            return -1;
        }
        if (pTarget < pCur + pHdr->field_0_size)
        {
            *pType = pHdr->field_8_type;
            *pId = pHdr->field_C_id;
            return static_cast<s32>(pTarget - pCur);
        }
        pCur += pHdr->field_0_size;
    }
    return -1;
}

// AO's S4 loader probe over Exoddus's singleton: files queued, the state of the
// one in flight, and its size in sectors (-1 = no loader yet, -3 = corrupt).
void ResourceManager::Tethys_LoadingProbeImpl(s32* pCount, s32* pState, s32* pSizeSectors)
{
    *pCount = -1;
    *pState = -1;
    *pSizeSectors = -1;
    ResourceManager* pRm = pResourceManager_5C1BB0;
    if (!pRm || !Tethys_PtrSane(pRm))
    {
        return;
    }
    *pCount = pRm->field_20_files_pending_loading.Size();
    *pState = pRm->field_42_state;
    *pSizeSectors = pRm->field_42_state != State_Wait_For_Load_Request ? pRm->field_34_num_sectors : 0;
}

void Tethys_LoadingProbe(s32* pCount, s32* pState, s32* pSizeSectors)
{
    ResourceManager::Tethys_LoadingProbeImpl(pCount, pState, pSizeSectors);
}

// AO 448.ao.4: the reads still waiting their turn, for the CD seam's merge
// (src/cd_saturn.cxx Tethys_CdMergedRead): the position (start sector plus the
// archive base, the space PSX_CD_File_Seek takes) and the length of every file
// record queued in the FIFO and not yet in flight. A screen queues its files
// before LoadingLoop reads the first one, so the seam sees the whole list.
s32 ResourceManager::Tethys_PendingReadsImpl(s32* pPos, s32* pSec, s32 max)
{
    ResourceManager* pRm = pResourceManager_5C1BB0;
    if (!pRm || !Tethys_PtrSane(pRm))
    {
        return 0;
    }
    s32 k = 0;
    for (s32 i = 0; i < pRm->field_20_files_pending_loading.Size() && k < max; i++)
    {
        ResourceManager_FileRecord* pFile = pRm->field_20_files_pending_loading.ItemAt(i);
        if (!pFile)
        {
            break;
        }
        if (pFile == pRm->field_2C_pFileItem && pRm->field_42_state != State_Wait_For_Load_Request)
        {
            continue; // the one being read (or allocated) right now
        }
        const LvlFileRecord* pRec = sLvlArchive_5BC520.Find_File_Record_433160(pFile->field_0_fileName);
        if (!pRec)
        {
            continue;
        }
        pPos[k] = pRec->field_C_start_sector + static_cast<s32>(sLvlArchive_5BC520.field_4_cd_pos);
        pSec[k] = pRec->field_10_num_sectors;
        k++;
    }
    return k;
}

extern "C" s32 Tethys_PendingReads(s32* pPos, s32* pSec, s32 max)
{
    return ResourceManager::Tethys_PendingReadsImpl(pPos, pSec, max);
}

// ============================================================================
// SATURN (decision D-R1, U1a A4): THE .CAM STREAM, AO's round 5 port
// (AliveLibAO/ResourceManager.cpp:2232-3186) on Exoddus's Camera.
//
// A converted AE camera record (tools/ae/ae_cam_convert.py, impl/C2.md) is a CZ4
// container (`CZ4\1` | u32 BE raw length | {u16 BE clen | LZ4 block} per 8 KiB)
// around a big-endian chunk chain: `Bits` first (u16 320 | u16 240 | 256 CRAM
// words | 76,800 indices), then FG1/Anim/Font/Demo chunks, then End!. It is
// never staged whole in the heap: Bits goes palette then pixels straight to VDP2
// NBG1 (never heaped), every other chunk is fabricated as a heap chunk with one
// reference and pushed into the camera's field_0 (the camera dtor frees each
// once), and field_30_flags bit 0 is set. field_C_pCamRes stays null: the guarded
// DecompressCameraToVRam_40EF60 then has nothing to upload.
// ============================================================================
extern "C" void Tethys_CamStreamBegin();
extern "C" void Tethys_CamStreamPalette(const u8* pal512);
extern "C" void Tethys_CamStreamPixels(u32 absOff, const u8* src, u32 len);
extern "C" void Tethys_CamStreamEnd();
extern "C" void Tethys_ClearForegroundAndPresent();
extern "C" u8* Tethys_CamStreamBulk(u32* pSectors);
extern "C" u8* Tethys_CamCacheFind(const char_type* name, u32 sectors);
extern "C" u8* Tethys_CamCacheClaim(const char_type* name, u32 sectors);
extern "C" void Tethys_CamCacheCommit(u8* claimed);
extern "C" u8* Tethys_CamCachePathFind(s32 start, u32 sectors);
extern "C" void Tethys_CamCacheNoteServed(const char_type* name, u32 sectors, const u8* mem);
extern "C" void Tethys_CamCachePreserveServed();
extern "C" u8* Tethys_CamCachePathClaim(u32 sectors);
extern "C" void Tethys_CamCachePathCommit(s32 base, u32 sectors);
extern "C" u32 Tethys_Lz4Decode(const u8* src, u32 srcLen, u8* dst, u32 dstCap);
extern "C" const u32 Tethys_kLz4Block;
extern "C" u8* Tethys_CamStreamZOut();
extern "C" u8* Tethys_CamStreamZIn();
extern "C" u8* Tethys_CamStreamVram(u32* pSectors);
extern "C" s32 Tethys_CdStreamStart(s32 numSectors, void* pDst);
extern "C" s32 Tethys_CdStreamPoll(u32* pBytes);
extern "C" void Tethys_CdStreamStop();
extern "C" s32 Tethys_CdStreamBusy();
extern "C" s32 Tethys_CamCacheHasSlab();
extern "C" u32 Tethys_gCamStreamed;
extern "C" u32 Tethys_gCamPrefetched;
extern "C" void Tethys_FlipStreamMark();
extern "C" u32 Tethys_gFlipSplitRaw[3];
static s32 sCamPrefetchPos = -1; // 446.ao.8: start sector of the read begun at T0, -1 = none
static s32 sCamPrefetchSec = 0;

namespace {
// 446.ao.4: a CPU read of VDP2 VRAM costs ~2 us whatever its width, so the B1
// stream buffer is only read as aligned longwords, split into bytes here
// (big-endian: b[0] is the lowest address).
static void CopyFromVram(u8* dst, const u8* src, u32 n)
{
    const u32* p = reinterpret_cast<const u32*>(reinterpret_cast<u32>(src) & ~3u);
    u32 k = reinterpret_cast<u32>(src) & 3u;
    while (n)
    {
        union
        {
            u32 w;
            u8 b[4];
        } t;
        t.w = *p++;
        for (; k < 4u && n; k++, n--)
        {
            *dst++ = t.b[k];
        }
        k = 0;
    }
}

static void CopyLongs(u8* dst, const u8* src, u32 take)
{
    u32 i = 0;
    if (((reinterpret_cast<u32>(dst) | reinterpret_cast<u32>(src)) & 3u) == 0)
    {
        u32* pD = reinterpret_cast<u32*>(dst);
        const u32* pS = reinterpret_cast<const u32*>(src);
        const u32 words = take >> 2;
        for (u32 k = 0; k < words; k++)
        {
            pD[k] = pS[k];
        }
        i = words << 2;
    }
    for (; i < take; i++)
    {
        dst[i] = src[i];
    }
}

// Forward, chunk-at-a-time reader over one CD record (AO's CamSectorReader,
// bt1019 bounce, bt1061 cache, bt1064 LZ4, 446.ao.4 one-read B1 stream, 446.ao.7
// marks). Raw layer: sectors, from the CD, the cartridge cache or a menu pin.
// Output layer: what Read/Pixels consume (the raw buffer, or a decoded block).
struct CamSectorReader
{
    u8* bounce;     // CD destination, the LWRAM bulk block
    const u8* mem;  // a cache or pin HIT: the whole record already in RAM
    u8* mirror;     // a cache or pin FILL: each CD read copied here on the way
    s32 basePos;    // file-relative start sector of the record
    s32 numSec;     // sectors in the record
    s32 nextSec;    // next sector index to fetch
    s32 secCap;     // sectors the bounce holds
    const u8* rsec; // current raw buffer
    u32 rOff;
    u32 rLen;
    const u8* sec;  // current output buffer
    u32 bufOff;
    u32 bufLen;
    u8* zOut;       // 8,192 B decoded block
    u8* zIn;        // 8,240 B compressed block
    u32 zRemain;    // uncompressed bytes still owed by the container header
    bool z;
    u8* vram;       // the B1 buffer the whole record is landing in, else null
    u32 vLanded;
    bool bad;

    void Init(u8* pBounce, s32 base, s32 count, s32 cap)
    {
        bounce = pBounce;
        mem = nullptr;
        mirror = nullptr;
        basePos = base;
        numSec = count;
        nextSec = 0;
        secCap = cap > 0 ? cap : 1;
        rsec = nullptr;
        rOff = 0;
        rLen = 0;
        sec = nullptr;
        bufOff = 0;
        bufLen = 0;
        zOut = nullptr;
        zIn = nullptr;
        zRemain = 0;
        z = false;
        vram = nullptr;
        vLanded = 0;
        bad = false;
    }

    bool RawFill()
    {
        const u32 t0 = Tethys_RawTicks();
        const bool ok = RawFillInner();
        Tethys_gFlipSplitRaw[0] += Tethys_RawTicks() - t0; // row 20 `w`
        return ok;
    }

    bool RawFillInner()
    {
        if (vram)
        {
            return StreamFill();
        }
        if (nextSec >= numSec)
        {
            bad = true;
            return false;
        }
        if (mem)
        {
            rsec = mem + (static_cast<u32>(nextSec) << 11);
            rOff = 0;
            rLen = static_cast<u32>(numSec - nextSec) << 11;
            nextSec = numSec;
            return true;
        }
        // ONE read for the whole record when it fits the B1 buffer (not on a
        // cache fill, which copies whole reads into its slot through the bounce).
        if (nextSec == 0 && !mirror && StreamStart())
        {
            return StreamFill();
        }
        s32 want = numSec - nextSec;
        if (want > secCap)
        {
            want = secCap;
        }
        for (s32 attempt = 0; attempt < 8; attempt++)
        {
            // bt1019: down to one sector after two failed wide attempts (a wide
            // read can fail structurally at the end of the .LVL).
            const s32 n = (attempt < 2) ? want : 1;
            CdlLOC loc;
            PSX_Pos_To_CdLoc_4FADD0(basePos + nextSec, &loc);
            if (PSX_CD_File_Seek_4FB1E0(2, &loc)
                && PSX_CD_File_Read_4FB210(n, bounce)
                && PSX_CD_FileIOWait_4FB260(0) != -1)
            {
                if (mirror)
                {
                    CopyLongs(mirror + (static_cast<u32>(nextSec) << 11), bounce, static_cast<u32>(n) << 11);
                }
                nextSec += n;
                rsec = bounce;
                rOff = 0;
                rLen = static_cast<u32>(n) << 11;
                return true;
            }
        }
        bad = true;
        return false;
    }

    bool StreamStart()
    {
        u32 cap = 0;
        u8* v = Tethys_CamStreamVram(&cap);
        if (!v || static_cast<u32>(numSec) > cap)
        {
            return false; // over the B1 buffer (the 6 big AE records): the bounce path
        }
        // 446.ao.8: the screen change may have started this very read at T0;
        // adopt it while it is still in flight.
        const bool adopt = sCamPrefetchPos == basePos && sCamPrefetchSec == numSec && Tethys_CdStreamBusy();
        sCamPrefetchPos = -1;
        if (adopt)
        {
            Tethys_gCamPrefetched++;
        }
        else
        {
            CdlLOC loc;
            PSX_Pos_To_CdLoc_4FADD0(basePos, &loc);
            if (!PSX_CD_File_Seek_4FB1E0(2, &loc) || !Tethys_CdStreamStart(numSec, v))
            {
                return false; // nothing consumed yet: the bounce path takes over
            }
        }
        vram = v;
        vLanded = 0;
        nextSec = numSec;
        Tethys_gCamStreamed++;
        return true;
    }

    // Wait until a sector more than was handed out has landed (or the read has
    // ended); a read that stops moving for 5 s is torn (the caller names it).
    bool StreamFill()
    {
        const u32 t0 = Tethys_RawTicks();
        for (;;)
        {
            u32 bytes = 0;
            const s32 st = Tethys_CdStreamPoll(&bytes);
            bytes &= ~3u;
            if (bytes >= vLanded + 2048u || (st == 0 && bytes > vLanded))
            {
                rsec = vram + vLanded;
                rOff = 0;
                rLen = bytes - vLanded;
                vLanded = bytes;
                return true;
            }
            if (st <= 0 || Tethys_RawTicks() - t0 > 5u * 208000u)
            {
                Tethys_CdStreamStop();
                bad = true;
                return false;
            }
        }
    }

    // After the walk: let GFS report the read complete, else stop it, so no read
    // is left writing into B1.
    void StreamClose()
    {
        if (vram)
        {
            u32 bytes = 0;
            for (u32 k = 0; k < 64u && Tethys_CdStreamPoll(&bytes) > 0; k++)
            {
            }
            Tethys_CdStreamStop();
            vram = nullptr;
        }
    }

    // Copy n RAW bytes forward, spanning refills (container path only).
    void RawRead(u8* dst, u32 n)
    {
        while (n && !bad)
        {
            if (rOff >= rLen && !RawFill())
            {
                return;
            }
            u32 take = rLen - rOff;
            if (take > n)
            {
                take = n;
            }
            const u8* pSrc = &rsec[rOff];
            if (vram)
            {
                const u32 tR = Tethys_RawTicks();
                CopyFromVram(dst, pSrc, take);
                Tethys_gFlipSplitRaw[2] += Tethys_RawTicks() - tR; // row 20 `r`
            }
            else
            {
                CopyLongs(dst, pSrc, take);
            }
            dst += take;
            rOff += take;
            n -= take;
        }
    }

    // Decide the record's format once, by peeking: both forms stay legal. The
    // B1 buffer is read by longwords only (CopyFromVram), the others directly.
    bool Open()
    {
        if (!RawFill())
        {
            return false;
        }
        u8 head[8] = {};
        if (rLen >= 8u)
        {
            if (vram)
            {
                CopyFromVram(head, rsec, 8);
            }
            else
            {
                CopyLongs(head, rsec, 8);
            }
        }
        if (head[0] == 'C' && head[1] == 'Z' && head[2] == '4' && head[3] == 1)
        {
            z = true;
            zRemain = (static_cast<u32>(head[4]) << 24) | (static_cast<u32>(head[5]) << 16)
                    | (static_cast<u32>(head[6]) << 8) | static_cast<u32>(head[7]);
            rOff = 8;
            zOut = Tethys_CamStreamZOut();
            zIn = Tethys_CamStreamZIn();
            return true;
        }
        // Raw record: the peeked buffer IS the first output buffer.
        sec = rsec;
        bufOff = 0;
        bufLen = rLen;
        rOff = rLen;
        return true;
    }

    bool Fill()
    {
        if (!z)
        {
            if (!RawFill())
            {
                return false;
            }
            sec = rsec;
            bufOff = 0;
            bufLen = rLen;
            rOff = rLen;
            return true;
        }
        if (zRemain == 0u)
        {
            bad = true; // the walk wants more than the header promised
            return false;
        }
        u8 hdr[2];
        RawRead(hdr, 2);
        if (bad)
        {
            return false;
        }
        const u32 clen = (static_cast<u32>(hdr[0]) << 8) | static_cast<u32>(hdr[1]);
        if (clen == 0u || clen > 8240u) // the converter's own bound (ae_lz4.py)
        {
            bad = true;
            return false;
        }
        RawRead(zIn, clen);
        if (bad)
        {
            return false;
        }
        const u32 want = (zRemain < Tethys_kLz4Block) ? zRemain : Tethys_kLz4Block;
        const u32 tD = Tethys_RawTicks();
        const u32 got = Tethys_Lz4Decode(zIn, clen, zOut, want);
        Tethys_gFlipSplitRaw[1] += Tethys_RawTicks() - tD; // row 20 `d`
        if (got != want) // short or long is corruption; malformed input returns 0
        {
            bad = true;
            return false;
        }
        zRemain -= got;
        sec = zOut;
        bufOff = 0;
        bufLen = got;
        return true;
    }

    // Copy n bytes forward into dst, spanning refills.
    void Read(u8* dst, u32 n)
    {
        while (n && !bad)
        {
            if (bufOff >= bufLen && !Fill())
            {
                return;
            }
            u32 take = bufLen - bufOff;
            if (take > n)
            {
                take = n;
            }
            CopyLongs(dst, &sec[bufOff], take);
            dst += take;
            bufOff += take;
            n -= take;
        }
    }

    // Route n bytes of the pixel plane to VDP2 with no intermediate copy; absOff
    // = the plane offset of the first byte produced by this call.
    void Pixels(u32 absOff, u32 n)
    {
        u32 produced = 0;
        while (n && !bad)
        {
            if (bufOff >= bufLen && !Fill())
            {
                return;
            }
            u32 take = bufLen - bufOff;
            if (take > n)
            {
                take = n;
            }
            Tethys_CamStreamPixels(absOff + produced, &sec[bufOff], take);
            bufOff += take;
            produced += take;
            n -= take;
        }
    }
};
} // namespace

// AO 426.ao.4 (cartridge only): PRELOAD A WHOLE PATH'S BACKGROUNDS IN ONE CD
// COMMAND, called by Map::GoTo_Camera_481890 at a path change before camera[0]
// is built. The path's CameraName grid is the authoritative list of its screens;
// the converted packs keep a path's .CAM records adjacent (first-use order, C1),
// so the span is read blind. Every failure is a miss, never a fault: no cart, a
// path larger than the slab, a record the archive does not have, a short read.
extern "C" void Tethys_CamPathPreload(const void* pCamNames, s32 count)
{
    if (!pCamNames || count <= 0)
    {
        return;
    }
    s32 lo = 0x7FFFFFFF;
    s32 hi = -1;
    const char_type* pN = static_cast<const char_type*>(pCamNames);
    for (s32 i = 0; i < count; i++, pN += sizeof(CameraName))
    {
        if (!pN[0])
        {
            continue; // empty grid cell
        }
        char_type nm[16] = {};
        for (s32 k = 0; k < static_cast<s32>(sizeof(CameraName)); k++)
        {
            nm[k] = pN[k];
        }
        nm[8] = '.';
        nm[9] = 'C';
        nm[10] = 'A';
        nm[11] = 'M';
        const LvlFileRecord* pRec = sLvlArchive_5BC520.Find_File_Record_433160(nm);
        if (!pRec)
        {
            continue;
        }
        const s32 end = pRec->field_C_start_sector + pRec->field_10_num_sectors;
        if (pRec->field_C_start_sector < lo)
        {
            lo = pRec->field_C_start_sector;
        }
        if (end > hi)
        {
            hi = end;
        }
    }
    if (hi < 0 || lo >= hi)
    {
        return;
    }
    // 427.ao.7: before the span is re-reserved (the record being saved sits in
    // the span this call hands to the next path).
    Tethys_CamCachePreserveServed();
    const u32 span = static_cast<u32>(hi - lo);
    u8* pDst = Tethys_CamCachePathClaim(span);
    if (!pDst)
    {
        return; // no cart, or this path wants more than the slab can lend
    }
    CdlLOC loc = {};
    PSX_Pos_To_CdLoc_4FADD0(static_cast<s32>(sLvlArchive_5BC520.field_4_cd_pos) + lo, &loc);
    if (PSX_CD_File_Seek_4FB1E0(2, &loc)
        && PSX_CD_File_Read_4FB210(static_cast<s32>(span), pDst)
        && PSX_CD_FileIOWait_4FB260(0) != -1)
    {
        Tethys_CamCachePathCommit(lo, span);
    }
    else
    {
        Tethys_CamCachePathClaim(0); // unreserve: the slots come back
    }
}

// AO 446.ao.8: THE NEW SCREEN'S .CAM READ STARTS AT T0 (Map::ScreenChange_480B80),
// so its CD latency runs under the teardown and the heap compaction; the reader
// adopts it when the record matches, any other read in between stops it.
void ResourceManager::Tethys_PrefetchCamFile(const char_type* pName)
{
    sCamPrefetchPos = -1;
    if (Tethys_CamCacheHasSlab())
    {
        return; // a cart mirrors the miss through the bounce: no stream to start
    }
    LvlFileRecord* pRec = sLvlArchive_5BC520.Find_File_Record_433160(pName);
    u32 cap = 0;
    u8* v = Tethys_CamStreamVram(&cap);
    if (!pRec || !v)
    {
        return;
    }
    const s32 numSec = pRec->field_10_num_sectors;
    if (numSec <= 0 || static_cast<u32>(numSec) > cap)
    {
        return;
    }
    const s32 pos = static_cast<s32>(sLvlArchive_5BC520.field_4_cd_pos) + pRec->field_C_start_sector;
    CdlLOC loc;
    PSX_Pos_To_CdLoc_4FADD0(pos, &loc);
    if (PSX_CD_File_Seek_4FB1E0(2, &loc) && Tethys_CdStreamStart(numSec, v))
    {
        sCamPrefetchPos = pos;
        sCamPrefetchSec = numSec;
    }
}

void ResourceManager::Tethys_StreamCamFile(Camera* pCamera, bool bitsOnly)
{
    Tethys_StreamCamFileNamed(pCamera, pCamera->field_1E_cam_name, bitsOnly);
}

void ResourceManager::Tethys_StreamCamFileNamed(Camera* pCamera, const char_type* pName, bool bitsOnly)
{
    Tethys_NoteCamLoad(pName);  // bt993: name the screen for the wedge fatal
    Tethys_FlipStreamMark();    // 446.ao.7: row 20 `h` ends here
    LvlFileRecord* pRec = sLvlArchive_5BC520.Find_File_Record_433160(pName);
    if (!pRec)
    {
        Tethys_NamedFatal("CAM stream: CD miss ", pName);
    }
    // 442.ao.1: the menu pins arm on their first page, or go back to the heap
    // BEFORE a stream it could not otherwise serve.
    Tethys_MenuPinsBeforeStream(pName);

    // bt872/418.ao.2: the foreground must vanish BEFORE the background swaps, and
    // the blank buffer must be the one on screen when the new Bits land (VDP1 is
    // double-buffered and nothing presents during the synchronous stream).
    Tethys_ClearForegroundAndPresent();

    // Latch VDP2 first (the one-time LoadBitmap and re-blank).
    Tethys_CamStreamBegin();

    // bt1019: one LWRAM block, the sector bounce then 512 B of palette assembly
    // just past it (the palette must not alias the bounce it is copied out of).
    u32 bulkSectors = 0;
    u8* bounce = Tethys_CamStreamBulk(&bulkSectors);
    u8* palBuf = bounce + (bulkSectors << 11);

    CamSectorReader rd;
    rd.Init(bounce, static_cast<s32>(sLvlArchive_5BC520.field_4_cd_pos) + pRec->field_C_start_sector,
            pRec->field_10_num_sectors, static_cast<s32>(bulkSectors));

    // bt1061 / 426.ao.4: the path span first (cartridge), then the slots (a
    // cartridge, or without one the menu pins). Best-effort, null without them.
    const u32 camSectors = static_cast<u32>(pRec->field_10_num_sectors);
    rd.mem = Tethys_CamCachePathFind(pRec->field_C_start_sector, camSectors);
    if (rd.mem)
    {
        Tethys_CamCacheNoteServed(pName, camSectors, rd.mem); // 427.ao.7
    }
    else
    {
        rd.mem = Tethys_CamCacheFind(pName, camSectors);
    }
    // Only a FULL walk may fill a slot: the bitsOnly refresh stops after Bits.
    if (!rd.mem && !bitsOnly)
    {
        rd.mirror = Tethys_CamCacheClaim(pName, camSectors);
    }
    // 446.ao.8: served from RAM or mirrored through the bounce, the record needs
    // no stream: stop the one the screen change started.
    if ((rd.mem || rd.mirror) && sCamPrefetchPos >= 0)
    {
        Tethys_CdStreamStop();
        sCamPrefetchPos = -1;
    }
    // 442.ao.1: busy only when this stream reads from the pins or fills one.
    sTethysPinsBusy = Tethys_MenuPinsHold(rd.mem) || Tethys_MenuPinsHold(rd.mirror);

    if (!rd.Open())
    {
        Tethys_NamedFatal("CAM stream: open ", pName);
    }

    // Walk the BE chunk chain (Bits, [FG1...], [Anim/Font/Demo], End!) by header
    // size. The ONLY clean exit is End!: a truncated read or a malformed size
    // means a torn background and possibly a half-written chunk, so it fails
    // loud with the record's name rather than commit a partial camera.
    bool sawEnd = false;
    for (;;)
    {
        Header hdr;
        rd.Read(reinterpret_cast<u8*>(&hdr), sizeof(Header));
        if (rd.bad)
        {
            break;
        }
        if (hdr.field_8_type == Resource_End)
        {
            sawEnd = true;
            break;
        }
        if (hdr.field_0_size < sizeof(Header) || (hdr.field_0_size & 3u))
        {
            Tethys_NamedFatal("CAM stream: bad chunk size ", pName);
        }
        const u32 payloadLen = hdr.field_0_size - sizeof(Header);

        if (hdr.field_8_type == Resource_Bits)
        {
            // Payload = u16 w | u16 h | 256 x u16 CRAM | 320*240 8bpp indices.
            if (payloadLen < 4u + 512u)
            {
                Tethys_NamedFatal("CAM stream: short Bits ", pName);
            }
            u8 wh[4];
            rd.Read(wh, 4);
            const u32 w = (static_cast<u32>(wh[0]) << 8) | wh[1];
            const u32 h = (static_cast<u32>(wh[2]) << 8) | wh[3];
            if (w != 320 || h != 240)
            {
                Tethys_NamedFatal("CAM stream: bad Bits hdr ", pName);
            }
            rd.Read(palBuf, 512);
            Tethys_CamStreamPalette(palBuf);
            rd.Pixels(0, payloadLen - 4u - 512u); // 76,800 -> VDP2, never heaped
            if (bitsOnly)
            {
                // A refresh of a camera that keeps its chunks (a film drew over
                // NBG1): Bits is always first, upload it and STOP. Re-pushing the
                // other chunks would double-free them in Camera::dtor_480E00.
                rd.StreamClose();
                if (rd.bad)
                {
                    Tethys_NamedFatal("CAM refresh: torn ", pName);
                }
                Tethys_CamStreamEnd();
                sTethysPinsBusy = false;
                return;
            }
        }
        else
        {
            if (bitsOnly)
            {
                Tethys_NamedFatal("CAM refresh: Bits not first ", pName);
            }
            // Fabricate the heap chunk exactly as Move_Resources would place it:
            // type and id from the record's header, ref 1, one push into the
            // camera's array (Create_FG1s and the factories find it by type and
            // id; Camera::dtor_480E00 frees each entry once). AO 449.ao.3: an FG1
            // chunk may be refused softly (a screen without its foreground beats
            // a dead run; its one consumer is Map::Create_FG1s_480F10); every
            // other kind stays fatal, factories fetch it by id. A refused chunk
            // is read through palBuf (spent once Bits is loaded) so the stream
            // and a cache mirror stay aligned.
            const bool bFg1 = hdr.field_8_type == Resource_FG1;
            u8** ppRes = Alloc_New_Resource_ImplEx(hdr.field_8_type, hdr.field_C_id, payloadLen, false,
                                                   BlockAllocMethod::eFirstMatching, true, !bFg1);
            if (!ppRes)
            {
                for (u32 left = payloadLen; left && !rd.bad;)
                {
                    const u32 n = left < 512u ? left : 512u;
                    rd.Read(palBuf, n);
                    left -= n;
                }
            }
            else
            {
                rd.Read(*ppRes, payloadLen);
                if (rd.bad)
                {
                    break; // truncated payload: do NOT commit; fatal below
                }
                pCamera->field_0.Push_Back_40CAF0(ppRes);
            }
        }

        if (rd.bad)
        {
            break;
        }
    }

    rd.StreamClose();
    Tethys_CamStreamEnd();

    if (!sawEnd)
    {
        // bt1061: a torn fill needs no explicit abandon, the slot was never
        // published, it stays invalid and is re-claimed later.
        Tethys_NamedFatal("CAM stream: torn ", pName);
    }
    // bt1061: publish only now, past End! and past the torn check.
    Tethys_CamCacheCommit(rd.mirror);
    sTethysPinsBusy = false;
    pCamera->field_30_flags |= 1; // resources ready (no field_C_pCamRes: Bits live in VDP2)
}

// U1a A21 / U7-T7: THE GAME-NEUTRAL FILM RESTORE for src/movie_ae.cxx (stream R4),
// the AE twin of what AO's movie_stub.cxx RestoreBackground does with AO types.
// A film drew over NBG1; the post-film calls of the engine
// (DecompressCameraToVRam_40EF60 from CameraSwapper, Abe, Glukkon, MainMenu)
// find a null handle on Saturn and repaint nothing, so the film layer calls this
// after trying the renderer's reveal (Tethys_CamRevealHidden): it re-streams the
// current camera's background (Bits only, the chunks stay as they are).
// Returns 1 when a camera was re-streamed, 0 when there is none (boot, or a film
// between two levels).
extern "C" s32 Tethys_RestreamCurrentCamera()
{
    Camera* pCam = gMap_5C3030.field_2C_camera_array[0];
    if (!pCam || !(pCam->field_30_flags & 1))
    {
        return 0;
    }
    ResourceManager::Tethys_StreamCamFile(pCam, true);
    return 1;
}
#endif // TETHYS_SATURN
