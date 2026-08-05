#include "stdafx_ao.h"
#include "ResourceManager.hpp"
#include "Function.hpp"
#include "Particle.hpp"
#include "stdlib.hpp"
#include "PsxDisplay.hpp"
#include "Psx.hpp"
#include "PsxRender.hpp"
#include "ScreenManager.hpp"
#include "Game.hpp"
#include "LvlArchive.hpp"
#include "Map.hpp"
#include "Sys.hpp"
#include "GameAutoPlayer.hpp"

namespace AO {

ALIVE_VAR(1, 0x5009E0, DynamicArrayT<ResourceManager::ResourceManager_FileRecord>*, ObjList_5009E0, nullptr);

ALIVE_VAR(1, 0x9F0E48, u32, sManagedMemoryUsedSize_9F0E48, 0);
ALIVE_VAR(1, 0x9F0E4C, u32, sPeakedManagedMemUsage_9F0E4C, 0);

ALIVE_VAR(1, 0x5076A0, s16, bHideLoadingIcon_5076A0, 0);
ALIVE_VAR(1, 0x5076A4, s32, loading_ticks_5076A4, 0);
ALIVE_VAR(1, 0x9F0E38, s16, sResources_Pending_Loading_9F0E38, 0);
ALIVE_VAR(1, 0x9F0E50, s16, sAllocationFailed_9F0E50, 0);



ALIVE_VAR(1, 0x50EE2C, ResourceManager::ResourceHeapItem*, sFirstLinkedListItem_50EE2C, nullptr);
ALIVE_VAR(1, 0x50EE28, ResourceManager::ResourceHeapItem*, sSecondLinkedListItem_50EE28, nullptr);

#ifdef TETHYS_SATURN
// SATURN: the 5.12 MB static heap does not even link on Saturn (HWRAM = 1 MB).
// The shrunk heap lives in LWRAM; the platform layer (src/main.cxx) allocates
// it and binds it through Tethys_BindResourceHeap() before Init_454DA0 runs.
// Sizing: R1P15C01's REAL resident set measured at S4 is 817,980 B with the
// FG1 CHNK block still unallocated -- 819200 left 1.2 KB of margin and the
// FG1 ctor's unchecked Allocate_New_Locked_Resource returned null (wild
// writes through *nullptr). The VAG/entry sound tables moved out of LWRAM
// (main.cxx: VDP1 VRAM tail until the S9 SCSP move) to fund this growth.
// S5: +20 KB more, funded by the CLUT mirror's move to the same tail --
// the S4 construct peak (1,002,460) came within 1,060 B of the previous
// 1,003,520 cap and the LCDScreen FntP alloc died exactly there.
// NOT higher: the heap is ONE LWRAM TLSF malloc and TLSF's good-fit search
// rounds the request up by its size-class granularity (16 KB in the
// 512K..1M band) -- 1,036,288 rounded crosses into the 2^20 class, which
// no block in a 1 MB pool can satisfy ("LWRAM alloc: resource heap" boot
// fatal with 1,045,380 B reported free). 1,024,000 + 16,383 stays inside
// the top fl19 class and matches the pool's ~1,045 K block.
u32 kResHeapSize = 1024000; // SATURN: runtime now (cart mode raises it)
u8* sResourceHeap_50EE38 = nullptr;

void Tethys_BindResourceHeap(u8* pHeap)
{
    sResourceHeap_50EE38 = pHeap;
}

u32 Tethys_ResHeapSize()
{
    return kResHeapSize;
}

// SATURN cart mode (S8): when a RAM cartridge is present the whole resource
// heap relocates into it (main.cxx BindLowWorkRamBlocks) and grows past the
// LWRAM/TLSF good-fit ceiling of 1,036,288 (a raw cart pointer bypasses TLSF).
// MUST be called before Tethys_BindResourceHeap + Init_454DA0. Every internal
// reader of kResHeapSize (Init extent, the Move_Resources out-of-heap sentinel,
// Reclaim, the diagnostics) tracks the new value automatically -- the name is
// unchanged -- so a stale-small size (false "out of heap") cannot happen.
void Tethys_SetResHeapSize(u32 n)
{
    kResHeapSize = n;
}
#else
const u32 kResHeapSize = 5120000;
ALIVE_ARY(1, 0x50EE38, u8, kResHeapSize, sResourceHeap_50EE38, {}); // Huge 5.4 MB static resource buffer
#endif

const u32 kLinkedListArraySize = 375;
ALIVE_ARY(1, 0x50E270, ResourceManager::ResourceHeapItem, kLinkedListArraySize, sResourceLinkedList_50E270, {});

ALIVE_VAR(1, 0x50EE30, u8*, spResourceHeapStart_50EE30, nullptr);
ALIVE_VAR(1, 0x9F0E3C, u8*, spResourceHeapEnd_9F0E3C, nullptr);

// TODO: move to correct location
EXPORT void CC Odd_Sleep_48DD90(u32 /*dwMilliseconds*/)
{
    NOT_IMPLEMENTED();
}

ALIVE_VAR(1, 0x507714, s32, gFilesPending_507714, 0);
ALIVE_VAR(1, 0x50768C, s16, bLoadingAFile_50768C, 0);

#ifdef TETHYS_SATURN
// SATURN: LoadingFile state-0 allocation retry counter (wedge detector --
// see the fatal in VUpdate_41E900).
static u32 Tethys_gState0Retries = 0;

// SATURN sticky resources (S7): the flip protocol frees the old camera's
// refs BEFORE the new camera queues its list, so shared heavyweights -- the
// walking-Slig set, ~380 K across SLIG.BND + SLIGZ.BND + 7 SLG*.BAN --
// are freed and re-read whole on EVERY flip between Slig screens. The
// re-read's whole-file staging (SLIG.BND alone = 82 sectors = 167,936 B
// contiguous ON TOP of the new camera's set) is what wedged C02->C03 with
// us=993,080/1,024,000. Files matched by Tethys_StickyName get ONE extra
// permanent ref on their requested (type,id) at first load: the chunks then
// survive camera teardown, the next queue's dedup finds them resident, and
// the file is never staged again. Scope: R1 is Slig-land, every P15 camera
// wants the set. Tethys_ReleaseStickyResources() drops the extra refs at
// LEVEL change (Map.cpp SATURN hook) so the next level does not inherit it.
struct TethysStickyEntry
{
    u32 type;
    u32 id;
};
static TethysStickyEntry sTethysStickyPending[20] = {};
static s32 sTethysStickyPendingCount = 0;
static TethysStickyEntry sTethysStickyHeld[20] = {};
static s32 sTethysStickyHeldCount = 0;

static bool Tethys_StickyName(const char_type* pFileName)
{
    return strncmp(pFileName, "SLG", 3) == 0
        || strcmp(pFileName, "SLIG.BND") == 0
        || strcmp(pFileName, "SLIGZ.BND") == 0;
}

static bool Tethys_StickyIn(const TethysStickyEntry* pTable, s32 count, u32 type, u32 id)
{
    for (s32 i = 0; i < count; i++)
    {
        if (pTable[i].type == type && pTable[i].id == id)
        {
            return true;
        }
    }
    return false;
}

// Called at every LoadResource request carrying a file name (the only place
// names exist). Resident already -> take the permanent ref NOW; else mark
// pending for On_Loaded_446C10.
static void Tethys_StickyRequest(const char_type* pFileName, u32 type, u32 id)
{
    if (!Tethys_StickyName(pFileName)
        || Tethys_StickyIn(sTethysStickyHeld, sTethysStickyHeldCount, type, id))
    {
        return;
    }
    if (ResourceManager::GetLoadedResource_4554F0(type, id, 1, 0)) // the permanent ref
    {
        if (sTethysStickyHeldCount < 20)
        {
            sTethysStickyHeld[sTethysStickyHeldCount].type = type;
            sTethysStickyHeld[sTethysStickyHeldCount].id = id;
            sTethysStickyHeldCount++;
        }
        return;
    }
    if (!Tethys_StickyIn(sTethysStickyPending, sTethysStickyPendingCount, type, id)
        && sTethysStickyPendingCount < 20)
    {
        sTethysStickyPending[sTethysStickyPendingCount].type = type;
        sTethysStickyPending[sTethysStickyPendingCount].id = id;
        sTethysStickyPendingCount++;
    }
}

// Called from On_Loaded_446C10 once the chunk exists in the heap.
static void Tethys_StickyMaterialize(u32 type, u32 id)
{
    for (s32 i = 0; i < sTethysStickyPendingCount; i++)
    {
        if (sTethysStickyPending[i].type == type && sTethysStickyPending[i].id == id)
        {
            if (ResourceManager::GetLoadedResource_4554F0(type, id, 1, 0) // the permanent ref
                && sTethysStickyHeldCount < 20)
            {
                sTethysStickyHeld[sTethysStickyHeldCount].type = type;
                sTethysStickyHeld[sTethysStickyHeldCount].id = id;
                sTethysStickyHeldCount++;
            }
            sTethysStickyPending[i] = sTethysStickyPending[--sTethysStickyPendingCount];
            return;
        }
    }
}

void Tethys_ForgetAbsentResources(); // SATURN bt990: defined below

// Level change: drop every permanent ref (the freed chunks then reclaim
// normally) and forget everything. Wired in Map.cpp's level-change block.
void Tethys_ReleaseStickyResources()
{
    for (s32 i = 0; i < sTethysStickyHeldCount; i++)
    {
        u8** ppRes = ResourceManager::GetLoadedResource_4554F0(
            sTethysStickyHeld[i].type, sTethysStickyHeld[i].id, 0, 0);
        if (ppRes)
        {
            ResourceManager::FreeResource_455550(ppRes);
        }
    }
    sTethysStickyHeldCount = 0;
    sTethysStickyPendingCount = 0;
    Tethys_ForgetAbsentResources(); // SATURN bt990: names are per-LVL
}

// SATURN (bt990): NAMES THIS LEVEL'S LVL GENUINELY DOES NOT CONTAIN.
// R1.LVL ships 209 file records and DOGBLOW.BAN is not one of them -- there are
// no Slogs in RuptureFarms, so the PSX data never shipped the Slog gib
// animation in this level. LoadRockTypes_454370 still asks for it on every
// grenade path (ThrowableArray.cpp:49, unconditional -- no disable_resources
// bit on that route), and so do Factory_TimedMine / Factory_SecurityOrb / the
// mine and bomb factories whenever a TLV leaves its bit clear. The OG answers
// with a silent nullptr here and a LOG_ERROR in CheckResourceIsLoaded, and
// plays on; both were hardened into fatals on this port, which is what killed a
// field run at fr295670 with "CD miss DOGBLOW.BAN".
// This is a CLASS, not one file: of the 195 file names referenced by AliveLibAO,
// 79 are absent from R1.LVL -- ELMBLOW.BAN and DOGKNFD.BAN sit on the very same
// factory lines as DOGBLOW.BAN, and SLOG.BND / PARAMITE.BND / SCRAB.BND /
// ELMSTART.BND come through the list loader. Fixing one name would just have
// moved the fatal to the next screen.
// The hardening still earns its keep for every OTHER miss -- a name that IS in
// the index but never materializes is the .ctors bug that made
// LoadResourcesFromList("SLIG.BND") a silent no-op at S4. So tolerate exactly
// "not in the archive index", remember the (type,id) so the paired
// CheckResourceIsLoaded stays quiet for it, and keep the fatal otherwise.
// Visible, never silent: the count rides the AF row (rs) of the overlay, which
// survives a fatal screen.
static TethysStickyEntry sTethysAbsent[32] = {};
static s32 sTethysAbsentCount = 0;
static bool sTethysAbsentOverflow = false;
extern "C" volatile s32 Tethys_gAbsentRes = 0; // overlay gauge: absent-name skips

static bool Tethys_ArchiveHas(const char_type* pFileName)
{
    return sLvlArchive_4FFD60.Find_File_Record_41BED0(pFileName) != nullptr;
}

// Name-only route (LoadResourceFile_4551E0): nothing to remember but the count.
static void Tethys_NoteAbsentFile()
{
    Tethys_gAbsentRes++;
}

static void Tethys_NoteAbsent(u32 type, u32 id)
{
    Tethys_gAbsentRes++;
    if (Tethys_StickyIn(sTethysAbsent, sTethysAbsentCount, type, id))
    {
        return;
    }
    if (sTethysAbsentCount < 32)
    {
        sTethysAbsent[sTethysAbsentCount].type = type;
        sTethysAbsent[sTethysAbsentCount].id = id;
        sTethysAbsentCount++;
    }
    else
    {
        // A level referencing more than 32 absent names is data reality, not a
        // fault; re-arming the fatal here would only move the crash. Go
        // permanently tolerant and let rs say so.
        sTethysAbsentOverflow = true;
    }
}

static bool Tethys_IsAbsent(u32 type, u32 id)
{
    return sTethysAbsentOverflow
        || Tethys_StickyIn(sTethysAbsent, sTethysAbsentCount, type, id);
}

void Tethys_ForgetAbsentResources()
{
    sTethysAbsentCount = 0;
    sTethysAbsentOverflow = false;
}

// Wedge diagnostics (S7 round 6): a staging block that cannot fit is now a
// GENUINE working-set overflow (post-round-5 the CAM no longer stages -- the
// remaining wedges are factory BNDs like EXPLODE.BND on Slig+Mudokon+explosion
// screens whose resident set exceeds the 1,024,000 B heap). Name the SCREEN
// (sCameraBeingLoaded's .CAM) on the forensic fatal so a field photo says
// exactly which camera overflowed -- the input to deciding critical-path
// (asset reduction) vs skippable. The stuck file's sector count is already on
// overlay row 5 (q/st/sz), so it is not re-formatted here (keeps .text down).
static void Tethys_WedgeFatal()
{
    static char_type msg[40];
    char_type* p = msg;
    for (const char_type* s = "LWRAM wedge "; *s; s++)
    {
        *p++ = *s;
    }
    Camera* pCam = sCameraBeingLoaded_507C98;
    const char_type* nm = (pCam && pCam->field_1E_fileName[0]) ? pCam->field_1E_fileName : "?";
    for (const char_type* s = nm; *s && p < &msg[39]; s++)
    {
        *p++ = *s;
    }
    *p = 0;
    ALIVE_FATAL(msg);
}
#endif

class LoadingFile final  : public BaseGameObject
{
public:
    EXPORT LoadingFile* ctor_41E8A0(s32 pos, s32 size, TLoaderFn pFn, void* fnArg, Camera* pArray)
    {
        ctor_487E10(0); // DON'T add to BGE list

        SetVTable(this, 0x4BB088);

        gFilesPending_507714++;

        field_6_flags.Set(Options::eSurviveDeathReset_Bit9);
        field_6_flags.Set(Options::eUpdateDuringCamSwap_Bit10);

        field_14_fn = pFn;
        field_18_fn_arg = fnArg;
        field_10_size = size;

        field_4_typeId = Types::eLoadingFile_39;
        field_1C_pCamera = pArray;

        PSX_Pos_To_CdLoc_49B340(pos, &field_2A_cdLoc);

        field_28_state = 0;

        gLoadingFiles->Push_Back(this);

        return this;
    }

    EXPORT BaseGameObject* dtor_41E870()
    {
        SetVTable(this, 0x4BB088);

        gLoadingFiles->Remove_Item(this);

        gFilesPending_507714--;

        if (field_28_state != 0)
        {
            if (field_28_state != 7)
            {
                bLoadingAFile_50768C = 0;
            }
        }
        return dtor_487DF0();
    }

    EXPORT void DestroyOnState0_41EA50()
    {
        if (field_28_state == 0)
        {
            field_6_flags.Set(BaseGameObject::eDead_Bit3);
        }
    }

    virtual void VUpdate() override
    {
        VUpdate_41E900();
    }

    EXPORT void VUpdate_41E900()
    {
        switch (field_28_state)
        {
            case 0:
                if (!bLoadingAFile_50768C)
                {
                    field_20_ppRes = ResourceManager::Allocate_New_Block_454FE0(field_10_size << 11, ResourceManager::eFirstMatching);
                    if (field_20_ppRes)
                    {
                        ResourceManager::Header* pHeader = ResourceManager::Get_Header_455620(field_20_ppRes);
                        field_24_readBuffer = pHeader;
                        pHeader->field_8_type = ResourceManager::Resource_Pend;
                        ResourceManager::Increment_Pending_Count_4557A0();
                        bLoadingAFile_50768C = 1;
                        field_28_state = 1;
#ifdef TETHYS_SATURN
                        Tethys_gState0Retries = 0;
#endif
                    }
                    else
                    {
                        ResourceManager::Reclaim_Memory_455660(200000u);
#ifdef TETHYS_SATURN
                        // SATURN: on the 1,024,000 B heap this retry loop is
                        // the S7 wedge mode -- the whole-file staging block
                        // (field_10_size << 11) cannot fit, Reclaim no-ops
                        // while resources are pending, LoadingLoop never
                        // yields to the destroy pass, and the screen freezes
                        // forever with no CD activity (first hit: SLIG.BND,
                        // 82 sectors, at the C02->C03 flip).
                        ++Tethys_gState0Retries;

                        // Sticky-until-pressure (S7 round 4): the sticky Slig
                        // set (~250-300 K permanent) plus Abe's resident banks
                        // leaves ~68 K free at flip time, and a flip must
                        // stage the WHOLE new .CAM contiguously (72-111 K in
                        // R1P15 -- field round 4 wedged on R1P15C10.CAM, 41
                        // sectors, us 955,696). When staging cannot be
                        // served: drop the sticky refs (chunks with no other
                        // holder free up; ones the new queue already re-
                        // requested survive via their camera ref) and force a
                        // full compaction with the pending guard masked --
                        // safe here because bLoadingAFile==0 gates this state,
                        // so no file owns a read buffer and every pending
                        // file is buffer-less at state 0; live chunks are
                        // handle-addressed (the PSX ran this same compactor
                        // under gameplay) and eLocked ones are skipped. Cost:
                        // the next Slig screen re-stages its set once, into
                        // the ~300 K that just opened. Three passes because
                        // one list walk does not fully compact.
                        if (Tethys_gState0Retries == 10)
                        {
                            Tethys_ReleaseStickyResources();
                            const s16 savedPending = sResources_Pending_Loading_9F0E38;
                            sResources_Pending_Loading_9F0E38 = 0;
                            ResourceManager::Reclaim_Memory_455660(0);
                            ResourceManager::Reclaim_Memory_455660(0);
                            ResourceManager::Reclaim_Memory_455660(0);
                            sResources_Pending_Loading_9F0E38 = savedPending;
                        }

                        // Still stuck after the pressure release: turn ~10 s
                        // of silent spin into the forensic death screen. The
                        // fatal names the screen + the stuck file's sectors
                        // (Tethys_WedgeFatal), and the heap top-8 names the
                        // occupants.
                        if (Tethys_gState0Retries > 600)
                        {
                            Tethys_WedgeFatal();
                        }
#endif
                    }
                }
                break;

            case 1:
                if (PSX_CD_File_Seek_49B670(2, &field_2A_cdLoc))
                {
                    field_28_state = 2;
                }
                break;

            case 2:
                if (PSX_CD_File_Read_49B8B0(field_10_size, field_24_readBuffer))
                {
                    field_28_state = 3;
                    const s32 ioRet = PSX_CD_FileIOWait_49B900(1);
                    if (ioRet <= 0)
                    {
                        field_28_state = ioRet != -1 ? 4 : 1;
                    }
                    break;
                }
                break;

            case 3:
            {
                const s32 ioRet = PSX_CD_FileIOWait_49B900(1);
                if (ioRet <= 0)
                {
                    field_28_state = ioRet != -1 ? 4 : 1;
                }
                break;
            }

            case 4:
                ResourceManager::Move_Resources_To_DArray_455430(
                    field_20_ppRes,
                    &field_1C_pCamera->field_0_array,
                    static_cast<u32>(field_10_size) << 11); // SATURN: block bound
                field_28_state = 5;
                break;

            case 5:
                if (field_14_fn)
                {
                    field_14_fn(field_18_fn_arg);
                }
                field_28_state = 6;
                bLoadingAFile_50768C = 0;
                break;

            case 6:
                ResourceManager::Decrement_Pending_Count_4557B0();
                field_6_flags.Set(BaseGameObject::eDead_Bit3);
                field_28_state = 7;
                break;

            default:
                return;
        }
    }

    virtual void VScreenChanged() override
    {
        // Stay alive
    }


    virtual BaseGameObject* VDestructor(s32 flags) override
    {
        return Vdtor_41EBB0(flags);
    }

    EXPORT LoadingFile* Vdtor_41EBB0(s32 flags)
    {
        dtor_41E870();
        if (flags & 1)
        {
            ao_delete_free_447540(this);
        }
        return this;
    }

#ifdef TETHYS_SATURN
    friend void Tethys_LoadingProbe(s32* pCount, s32* pState, s32* pSizeSectors);
#endif

    s32 field_10_size;
    TLoaderFn field_14_fn;
    void* field_18_fn_arg;
    Camera* field_1C_pCamera;
    u8** field_20_ppRes;
    void* field_24_readBuffer;
    s16 field_28_state;
    CdlLOC field_2A_cdLoc;
    s16 field_2E_pad;
};
ALIVE_ASSERT_SIZEOF(LoadingFile, 0x30);

#ifdef TETHYS_SATURN
// SATURN: is this a plausible pointer for heap bookkeeping? The S5 wild-jump
// fatal (pc ffffe1fa) froze a death screen whose rows 7+ never printed: the
// heap walkers below chased a corrupted chain into their OWN nested CPU
// exception. Every deref in the forensics must be range-checked.
static bool Tethys_PtrSane(const void* p)
{
    const u32 a = reinterpret_cast<u32>(p);
    if (a & 3)
    {
        return false;
    }
    return (a >= 0x06000000u && a < 0x06100000u) // HWRAM
        || (a >= 0x00200000u && a < 0x00300000u) // LWRAM
        || (a >= 0x02400000u && a < 0x02800000u) // cart RAM cached window (up to 4MB, S8 cart mode)
        || (a >= 0x22400000u && a < 0x22800000u); // cart uncached window (bt910 TETHYS_CART_UNCACHED A/B)
}

// SATURN: continuous physical-heap integrity check (live overlay row):
// 0 = every header stride is sane up to the heap end, else the byte offset
// of the first corrupt header. Localizes WHEN corruption starts instead of
// only seeing its wild-jump aftermath.
s32 Tethys_HeapCheck()
{
    const u8* pBase = sResourceHeap_50EE38;
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
            return static_cast<s32>(pCur - pBase) | 1; // never 0
        }
        pCur += pHdr->field_0_size;
    }
    return 0;
}

// SATURN (S7): physical heap usage by stride walk -- used bytes + live block
// count of non-Free headers. The no-leak gate CANNOT use
// sManagedMemoryUsedSize_9F0E48: Allocate_New_Block adds the ROUNDED REQUEST
// (:1086) but Split_block declines to split when the remainder < a Header
// (:954-978) and FreeResource later subtracts the block's LARGER real size
// (:1359) -- up to 15 B of drift per alloc/free cycle, accumulating every
// lap. The walk reads what is physically there. Returns 0 on success, -1 on
// a corrupt stride (same detection as Tethys_HeapCheck).
s32 Tethys_HeapUsage(u32* pUsedBytes, u32* pLiveBlocks)
{
    *pUsedBytes = 0;
    *pLiveBlocks = 0;
    const u8* pBase = sResourceHeap_50EE38;
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

// SATURN: fatal-time heap accounting (src/sys_saturn.cxx death screen) --
// the idx-th biggest non-Free block of the resource heap, by selection scan
// over the used chain (~174 nodes; only runs on the frozen fatal screen).
void Tethys_HeapTop(s32 idx, u32* pType, u32* pId, u32* pSize)
{
    *pType = 0;
    *pId = 0;
    *pSize = 0;
    u32 prevSize = 0xFFFFFFFFu;
    u32 prevId = 0xFFFFFFFFu;
    for (s32 rank = 0; rank <= idx; rank++)
    {
        u32 bestSize = 0;
        u32 bestType = 0;
        u32 bestId = 0;
        u32 guard = 0;
        for (ResourceManager::ResourceHeapItem* pItem = sFirstLinkedListItem_50EE2C;
             pItem && guard <= 375u;
             pItem = pItem->field_4_pNext, guard++)
        {
            if (!Tethys_PtrSane(pItem) || !Tethys_PtrSane(pItem->field_0_ptr))
            {
                break; // corrupted chain: report what accumulated so far
            }
            ResourceManager::Header* pHdr = ResourceManager::Get_Header_455620(&pItem->field_0_ptr);
            if (pHdr->field_8_type == ResourceManager::Resource_Free)
            {
                continue;
            }
            // Strictly below the previous rank; ties broken by id so equal
            // sizes are enumerated once each.
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
            }
        }
        prevSize = bestSize;
        prevId = bestId;
        *pType = bestType;
        *pId = bestId;
        *pSize = bestSize;
    }
}

// SATURN: is (type, id) resident? -> block size, or -1.
s32 Tethys_HeapFind(u32 type, u32 id)
{
    u32 guard = 0;
    for (ResourceManager::ResourceHeapItem* pItem = sFirstLinkedListItem_50EE2C;
         pItem && guard <= 375u;
         pItem = pItem->field_4_pNext, guard++)
    {
        if (!Tethys_PtrSane(pItem) || !Tethys_PtrSane(pItem->field_0_ptr))
        {
            return -3; // corrupted chain
        }
        ResourceManager::Header* pHdr = ResourceManager::Get_Header_455620(&pItem->field_0_ptr);
        if (pHdr->field_8_type == type && pHdr->field_C_id == id)
        {
            return static_cast<s32>(pHdr->field_0_size);
        }
    }
    return -1;
}

// SATURN: same question asked of the PHYSICAL heap (header-to-header by
// size stride, list ignored). List-find -1 but raw-scan hit = the linked
// list lost the block (Reclaim compactor suspect -- code the PC build
// never exercises: its 5.12 MB heap feels no pressure at boot).
s32 Tethys_HeapScanRaw(u32 type, u32 id)
{
    const u8* pCur = sResourceHeap_50EE38;
    const u8* pEnd = sResourceHeap_50EE38 + kResHeapSize;
    u32 guard = 0;
    while (pCur + sizeof(ResourceManager::Header) <= pEnd && guard++ <= 2048u)
    {
        const ResourceManager::Header* pHdr = reinterpret_cast<const ResourceManager::Header*>(pCur);
        if (pHdr->field_8_type == type && pHdr->field_C_id == id)
        {
            return static_cast<s32>(pHdr->field_0_size);
        }
        if (pHdr->field_0_size < sizeof(ResourceManager::Header) || (pHdr->field_0_size & 3))
        {
            return -2; // stride corrupt: physical chain unwalkable past here
        }
        pCur += pHdr->field_0_size;
    }
    return -1;
}

// SATURN: which resident block CONTAINS pointer p? Physical stride walk
// (same hazards as Tethys_HeapScanRaw). Returns the offset of p inside the
// block (header included) with the owner's type/id in the out params, or
// -1 (p outside the heap / stride corrupt before reaching it). Names the
// object whose data a rogue OT prim lives in.
s32 Tethys_HeapOwner(const void* p, u32* pType, u32* pId)
{
    *pType = 0;
    *pId = 0;
    const u8* pTarget = static_cast<const u8*>(p);
    const u8* pCur = sResourceHeap_50EE38;
    const u8* pEnd = sResourceHeap_50EE38 + kResHeapSize;
    if (pTarget < pCur || pTarget >= pEnd)
    {
        return -1;
    }
    u32 guard = 0;
    while (pCur + sizeof(ResourceManager::Header) <= pEnd && guard++ <= 2048u)
    {
        const ResourceManager::Header* pHdr = reinterpret_cast<const ResourceManager::Header*>(pCur);
        if (pHdr->field_0_size < sizeof(ResourceManager::Header) || (pHdr->field_0_size & 3))
        {
            return -1; // stride corrupt before reaching p
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

// SATURN: S4 soft-hang forensics for the platform overlay (src/sys_saturn.cxx)
// -- the LoadingFile class is TU-local, so its head state is exposed here.
void Tethys_LoadingProbe(s32* pCount, s32* pState, s32* pSizeSectors)
{
    *pCount = -1;
    *pState = -1;
    *pSizeSectors = -1;
    if (gLoadingFiles && Tethys_PtrSane(gLoadingFiles))
    {
        *pCount = gLoadingFiles->Size();
        for (s32 i = 0; i < gLoadingFiles->Size(); i++)
        {
            LoadingFile* pFile = static_cast<LoadingFile*>(gLoadingFiles->ItemAt(i));
            if (pFile && !Tethys_PtrSane(pFile))
            {
                *pState = -3; // corrupted list
                break;
            }
            if (pFile)
            {
                *pState = pFile->field_28_state;
                *pSizeSectors = pFile->field_10_size;
                break;
            }
        }
    }
}
#endif

void CC Game_ShowLoadingIcon_445EB0()
{
    const AnimRecord& rec = AO::AnimRec(AnimId::Loading_Icon2);
    u8** ppRes = ResourceManager::GetLoadedResource_4554F0(ResourceManager::Resource_Animation, rec.mResourceId, 1, 0);
    if (ppRes)
    {
        auto pParticle = ao_new<Particle>();
        if (pParticle)
        {
            pParticle->ctor_478880(FP_FromInteger(0), FP_FromInteger(0), rec.mFrameTableOffset, rec.mMaxW, rec.mMaxH, ppRes);
        }

        pParticle->field_10_anim.field_4_flags.Clear(AnimFlags::eBit15_bSemiTrans);
        pParticle->field_10_anim.field_4_flags.Set(AnimFlags::eBit16_bBlending);

        pParticle->field_10_anim.field_C_layer = Layer::eLayer_0;

        PrimHeader* local_ot[42] = {};
        PSX_DRAWENV drawEnv = {};

        PSX_SetDefDrawEnv_495EF0(&drawEnv, 0, 0, 640, 240);
        PSX_PutDrawEnv_495DD0(&drawEnv);
        PSX_DrawSync_496750(0);
        PSX_ClearOTag_496760(local_ot, 42);

        pParticle->field_10_anim.vRender(320, 220, local_ot, 0, 0);

        PSX_DrawOTag_4969F0(local_ot);
        PSX_DrawSync_496750(0);

        PSX_ClearOTag_496760(local_ot, 42);

        pParticle->field_10_anim.vRender(320, gPsxDisplay_504C78.field_2_height + 220, local_ot, 0, 0);

        PSX_DrawOTag_4969F0(local_ot);
        PSX_DrawSync_496750(0);

        PSX_DISPENV dispEnv = {};
        PSX_SetDefDispEnv_4959D0(&dispEnv, 0, 0, 640, 240);
        PSX_PutDispEnv_495CE0(&dispEnv);
        pParticle->field_6_flags.Set(BaseGameObject::eDead_Bit3);
        bHideLoadingIcon_5076A0 = TRUE;
    }
}


void CC ResourceManager::On_Loaded_446C10(ResourceManager_FileRecord* pLoaded)
{
    for (s32 i = 0; i < pLoaded->field_10_file_sections_dArray.Size(); i++)
    {
        ResourceManager_FilePartRecord* pFilePart = pLoaded->field_10_file_sections_dArray.ItemAt(i);
        if (!pFilePart)
        {
            break;
        }

        u8** ppRes = ResourceManager::GetLoadedResource_4554F0(
            pFilePart->field_0_type,
            pFilePart->field_4_res_id,
            1,
            0);

        if (ppRes)
        {
            pFilePart->field_8_pCamera->field_0_array.Push_Back(ppRes);
#ifdef TETHYS_SATURN
            Tethys_StickyMaterialize(pFilePart->field_0_type, pFilePart->field_4_res_id); // SATURN: see the sticky block
#endif
        }

        ao_delete_free_447540(pFilePart);
    }

    // pLoaded is done with now, remove it
    ObjList_5009E0->Remove_Item(pLoaded);

    if (pLoaded)
    {
        // And destruct/free it
        pLoaded->dtor_447510();
        ao_delete_free_447540(pLoaded);
    }
}

void CC ResourceManager::LoadResource_446C90(const char_type* pFileName, u32 type, u32 resourceId, LoadMode loadMode, s16 bDontLoad)
{
    if (bDontLoad)
    {
        return;
    }

#ifdef TETHYS_SATURN
    Tethys_StickyRequest(pFileName, type, resourceId); // SATURN: see the sticky block above LoadingFile
#endif

    u8** ppExistingRes = ResourceManager::GetLoadedResource_4554F0(type, resourceId, 1, 0);
    if (ppExistingRes)
    {
        sCameraBeingLoaded_507C98->field_0_array.Push_Back(ppExistingRes);
        return;
    }

#ifdef TETHYS_SATURN
    // SATURN (bt990): the name is not in this level's archive index -- OG-legal
    // (see the absent-name block). Remember the (type,id) so the factory's own
    // CheckResourceIsLoaded, which runs on the LoadMode::LoadTlvs pass a moment
    // later, does not fatal on the resource this request could never produce.
    if (!Tethys_ArchiveHas(pFileName))
    {
        Tethys_NoteAbsent(type, resourceId);
        return;
    }
#endif

    if (loadMode == LoadMode::LoadResourceFromList_1)
    {
        for (s32 i = 0; i < ObjList_5009E0->Size(); i++)
        {
            ResourceManager_FileRecord* pExistingFileRec = ObjList_5009E0->ItemAt(i);
            if (!pExistingFileRec)
            {
                break;
            }

            ResourcesToLoadList* pListToLoad = pExistingFileRec->field_4_pResourcesToLoadList;
            bool found = false;
            if (pListToLoad)
            {
                if (pListToLoad->field_0_count > 0)
                {
                    for (s32 j = 0; j < pListToLoad->field_0_count; j++)
                    {
                        if (type == pListToLoad->field_4_items[j].field_0_type && resourceId == pListToLoad->field_4_items[j].field_4_res_id)
                        {
                            found = true;
                            break;
                        }
                    }
                }
            }
            else if (type == pExistingFileRec->field_8_type && resourceId == pExistingFileRec->field_C_resourceId)
            {
                found = true;
            }

            if (found)
            {
                auto pFilePart = ao_new<ResourceManager_FilePartRecord>();
                pFilePart->field_8_pCamera = sCameraBeingLoaded_507C98;
                pFilePart->field_0_type = type;
                pFilePart->field_4_res_id = resourceId;
                pExistingFileRec->field_10_file_sections_dArray.Push_Back(pFilePart);
                return;
            }
        }

        auto pFileRec = ao_new<ResourceManager_FileRecord>();
        if (pFileRec)
        {
            pFileRec->field_10_file_sections_dArray.ctor_4043E0(10);
            pFileRec->field_0_fileName = pFileName;
            pFileRec->field_4_pResourcesToLoadList = nullptr;
            pFileRec->field_8_type = type;
            pFileRec->field_C_resourceId = resourceId;

            auto pFilePart = ao_new<ResourceManager_FilePartRecord>();
            pFilePart->field_0_type = type;
            pFilePart->field_4_res_id = resourceId;
            pFilePart->field_8_pCamera = sCameraBeingLoaded_507C98;

            pFileRec->field_10_file_sections_dArray.Push_Back(pFilePart);

            pFileRec->field_1C_pGameObjFileRec = ResourceManager::LoadResourceFile(
                pFileName,
                ResourceManager::On_Loaded_446C10,
                pFileRec);
            ObjList_5009E0->Push_Back(pFileRec);
        }
    }
    else if (loadMode == LoadMode::LoadResource_2)
    {
        ResourceManager::LoadResourceFile_455270(pFileName, nullptr);
        u8** ppRes = ResourceManager::GetLoadedResource_4554F0(type, resourceId, 1, 0);
        if (ppRes)
        {
            sCameraBeingLoaded_507C98->field_0_array.Push_Back(ppRes);
        }
    }
}

void CC ResourceManager::LoadResourcesFromList_446E80(const char_type* pFileName, ResourcesToLoadList* pTypeAndIdList, LoadMode loadMode, s16 bDontLoad)
{
    // Debug_Print_Stub_48DD70("Requesting tag res %s\n", pFileName);

    if (bDontLoad)
    {
        return;
    }

#ifdef TETHYS_SATURN
    // SATURN: sticky heavyweights come through THIS entry too -- SLIG.BND
    // (the 82-sector wedge file) is requested as a list (Factory.cpp:784,
    // kSligResources_4BD1CC), not via LoadResource_446C90.
    for (s32 i = 0; i < pTypeAndIdList->field_0_count; i++)
    {
        Tethys_StickyRequest(pFileName,
                             pTypeAndIdList->field_4_items[i].field_0_type,
                             pTypeAndIdList->field_4_items[i].field_4_res_id);
    }
#endif

    // Check if all resources are already loaded
    bool allResourcesLoaded = true;
    for (s32 i = 0; i < pTypeAndIdList->field_0_count; i++)
    {
        while (!ResourceManager::GetLoadedResource_4554F0(
            pTypeAndIdList->field_4_items[i].field_0_type,
            pTypeAndIdList->field_4_items[i].field_4_res_id,
            0,
            0))
        {
            // A resource we need is missing
            allResourcesLoaded = false;
            break;
        }
    }

    // All resources that we required are already loaded
    if (allResourcesLoaded)
    {
        for (s32 i = 0; i < pTypeAndIdList->field_0_count; i++)
        {
            sCameraBeingLoaded_507C98->field_0_array.Push_Back(GetLoadedResource_4554F0(
                pTypeAndIdList->field_4_items[i].field_0_type,
                pTypeAndIdList->field_4_items[i].field_4_res_id,
                1,
                0));
        }
        return;
    }

#ifdef TETHYS_SATURN
    // SATURN (bt990): whole BNDs are level-scoped too -- SLOG.BND, PARAMITE.BND,
    // SCRAB.BND, ELMSTART.BND and ABEWELM.BND are referenced by factories yet
    // absent from R1.LVL. Same contract as LoadResource_446C90: tolerate the
    // index miss and remember every id in the list so their paired
    // CheckResourceIsLoaded calls stay quiet. After the resident check above, so
    // a list that IS already loaded still takes its refs.
    if (!Tethys_ArchiveHas(pFileName))
    {
        for (s32 i = 0; i < pTypeAndIdList->field_0_count; i++)
        {
            Tethys_NoteAbsent(pTypeAndIdList->field_4_items[i].field_0_type,
                              pTypeAndIdList->field_4_items[i].field_4_res_id);
        }
        return;
    }
#endif

    if (loadMode == LoadMode::LoadResourceFromList_1)
    {
        for (s32 i = 0; i < ObjList_5009E0->Size(); i++)
        {
            ResourceManager_FileRecord* pFileRec = ObjList_5009E0->ItemAt(i);
            if (!pFileRec)
            {
                break;
            }

            if (!strcmp(pFileName, pFileRec->field_0_fileName))
            {
                if (pTypeAndIdList->field_0_count == 0)
                {
                    return;
                }

                for (s32 j = 0; j < pTypeAndIdList->field_0_count; j++)
                {
                    auto pPart = ao_new<ResourceManager_FilePartRecord>();
                    pPart->field_0_type = pTypeAndIdList->field_4_items[j].field_0_type;
                    pPart->field_4_res_id = pTypeAndIdList->field_4_items[j].field_4_res_id;
                    pPart->field_8_pCamera = sCameraBeingLoaded_507C98;
                    pFileRec->field_10_file_sections_dArray.Push_Back(pPart);
                }
                return;
            }
        }

        auto pNewFileRec = ao_new<ResourceManager_FileRecord>();
        if (pNewFileRec)
        {
            pNewFileRec->field_10_file_sections_dArray.ctor_4043E0(10);
        }

        pNewFileRec->field_0_fileName = pFileName;
        pNewFileRec->field_4_pResourcesToLoadList = pTypeAndIdList;
        pNewFileRec->field_8_type = 0;
        pNewFileRec->field_C_resourceId = 0;

        // Check if all resources are already loaded
        if ((pTypeAndIdList->field_0_count & ~0x80000000))
        {
            for (s32 j = 0; j < pTypeAndIdList->field_0_count; j++)
            {
                auto pNewFilePart = ao_new<ResourceManager_FilePartRecord>();
                pNewFilePart->field_0_type = pTypeAndIdList->field_4_items[j].field_0_type;
                pNewFilePart->field_4_res_id = pTypeAndIdList->field_4_items[j].field_4_res_id;
                pNewFilePart->field_8_pCamera = sCameraBeingLoaded_507C98;
                pNewFileRec->field_10_file_sections_dArray.Push_Back(pNewFilePart);
            }
        }

        pNewFileRec->field_1C_pGameObjFileRec = ResourceManager::LoadResourceFile(
            pFileName,
            ResourceManager::On_Loaded_446C10,
            pNewFileRec);
        ObjList_5009E0->Push_Back(pNewFileRec);
    }
    else if (loadMode == LoadMode::LoadResource_2)
    {
        ResourceManager::LoadResourceFile_455270(pFileName, nullptr);
        for (s32 j = 0; j < pTypeAndIdList->field_0_count; j++)
        {
            u8** ppLoadedRes = ResourceManager::GetLoadedResource_4554F0(
                pTypeAndIdList->field_4_items[j].field_0_type,
                pTypeAndIdList->field_4_items[j].field_4_res_id,
                1,
                0);

            if (ppLoadedRes)
            {
                sCameraBeingLoaded_507C98->field_0_array.Push_Back(ppLoadedRes);
            }
        }
    }
}

void CC ResourceManager::WaitForPendingResources_41EA60(BaseGameObject* pObj)
{
    for (s32 i = 0; i < gLoadingFiles->Size(); i++)
    {
        BaseGameObject* pObjIter = gLoadingFiles->ItemAt(i);
        if (!pObjIter)
        {
            break;
        }

        auto pLoadingFile = static_cast<LoadingFile*>(pObjIter);
        if (!pObj || pObj == pLoadingFile->field_18_fn_arg)
        {
            while (pLoadingFile->field_28_state != 0)
            {
                if (pLoadingFile->field_6_flags.Get(BaseGameObject::eDead_Bit3))
                {
                    break;
                }
                pLoadingFile->VUpdate();
            }
            pLoadingFile->field_6_flags.Set(BaseGameObject::eDead_Bit3);
        }
    }
}

EXPORT void CC ResourceManager::LoadingLoop_41EAD0(s16 bShowLoadingIcon)
{
    GetGameAutoPlayer().DisableRecorder();


    while (gFilesPending_507714 > 0)
    {
        SYS_EventsPump_44FF90();

        for (s32 i = 0; i < gLoadingFiles->Size(); i++)
        {
            BaseGameObject* pObjIter = gLoadingFiles->ItemAt(i);
            if (!pObjIter)
            {
                break;
            }

            if (!pObjIter->field_6_flags.Get(BaseGameObject::eDead_Bit3))
            {
                pObjIter->VUpdate();
            }

            if (pObjIter->field_6_flags.Get(BaseGameObject::eDead_Bit3))
            {
                i = gLoadingFiles->RemoveAt(i);
                pObjIter->VDestructor(1);
            }
        }

        Odd_Sleep_48DD90(16u);
        PSX_VSync_496620(0);

        loading_ticks_5076A4++;

        if (bShowLoadingIcon)
        {
            if (!bHideLoadingIcon_5076A0 && loading_ticks_5076A4 > 180)
            {
                Game_ShowLoadingIcon_445EB0();
            }
        }
    }

     GetGameAutoPlayer().EnableRecorder();
}

void CC ResourceManager::Free_Resources_For_Camera_447170(Camera* pCamera)
{
    for (s32 i = 0; i < ObjList_5009E0->Size(); i++)
    {
        ResourceManager_FileRecord* pObjIter = ObjList_5009E0->ItemAt(i);
        if (!pObjIter)
        {
            break;
        }

        if (pObjIter->field_1C_pGameObjFileRec->field_28_state == 0)
        {
            // Remove/free file parts that belong to the cameraa
            auto pFileSecsArray = &pObjIter->field_10_file_sections_dArray;
            for (s32 j = 0; j < pFileSecsArray->Size(); j++)
            {
                ResourceManager_FilePartRecord* pFilePartRecord = pFileSecsArray->ItemAt(j);
                if (!pFilePartRecord)
                {
                    break;
                }

                if (pFilePartRecord->field_8_pCamera == pCamera)
                {
                    j = pFileSecsArray->RemoveAt(j);

                    // Only delete the record we just removed
                    ao_delete_free_447540(pFilePartRecord);
                }
                else
                {
                    LOG_WARNING("OG bug fix 0x" << pFilePartRecord << " would have been deleted here!");
                }
            }

            // Free the containing record if its section array is now empty
            if (pObjIter->field_10_file_sections_dArray.Empty())
            {
                if (pObjIter->field_1C_pGameObjFileRec)
                {
                    pObjIter->field_1C_pGameObjFileRec->DestroyOnState0_41EA50();
                }

                i = ObjList_5009E0->RemoveAt(i);
                pObjIter->dtor_447510();
                ao_delete_free_447540(pObjIter);
            }
        }
    }
}

s32 CC ResourceManager::SEQ_HashName_454EA0(const char_type* seqFileName)
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
        s8 letter = seqFileName[index];
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

void CC ResourceManager::Init_454DA0()
{
    for (s32 i = 1; i < kLinkedListArraySize - 1; i++)
    {
        sResourceLinkedList_50E270[i].field_0_ptr = nullptr;
        sResourceLinkedList_50E270[i].field_4_pNext = &sResourceLinkedList_50E270[i + 1];
    }

    sResourceLinkedList_50E270[kLinkedListArraySize - 1].field_4_pNext = nullptr;

    sResourceLinkedList_50E270[0].field_0_ptr = &sResourceHeap_50EE38[sizeof(Header)];
    sResourceLinkedList_50E270[0].field_4_pNext = nullptr;

    Header* pHeader = Get_Header_455620(&sResourceLinkedList_50E270[0].field_0_ptr);
    pHeader->field_0_size = kResHeapSize;
    pHeader->field_8_type = Resource_Free;

    sFirstLinkedListItem_50EE2C = &sResourceLinkedList_50E270[0];
    sSecondLinkedListItem_50EE28 = &sResourceLinkedList_50E270[1];

    spResourceHeapStart_50EE30 = &sResourceHeap_50EE38[0];
    spResourceHeapEnd_9F0E3C = &sResourceHeap_50EE38[kResHeapSize - 1];
}

ResourceManager::ResourceHeapItem* ResourceManager::Push_List_Item()
{
    auto old = sSecondLinkedListItem_50EE28;
#ifdef TETHYS_SATURN
    // SATURN: the kLinkedListArraySize (375) node pool is a HARD cap on live
    // blocks; exhaustion used to null-write in the callers (Split_block /
    // Move_Resources) -> the BIOS-vector crash class. A bigger cart heap can
    // hold more concurrent blocks, so name it: a diagnosable fatal, not a wild
    // crash. If this ever fires, grow the node array (into cart RAM, not the
    // tight HWRAM .bss pool).
    if (!old)
    {
        ALIVE_FATAL("resource node pool exhausted (375)");
    }
#endif
    sSecondLinkedListItem_50EE28 = old->field_4_pNext;
    return old;
}


void ResourceManager::Pop_List_Item(ResourceHeapItem* pListItem)
{
    pListItem->field_0_ptr = nullptr;
    pListItem->field_4_pNext = sSecondLinkedListItem_50EE28; // point to the current
    sSecondLinkedListItem_50EE28 = pListItem;                // set current to old
}

ResourceManager::ResourceHeapItem* ResourceManager::Split_block(ResourceManager::ResourceHeapItem* pItem, s32 size)
{
    Header* pToSplit = Get_Header_455620(&pItem->field_0_ptr);
    const u32 sizeForNewRes = pToSplit->field_0_size - size;
    if (sizeForNewRes >= sizeof(Header))
    {
        ResourceHeapItem* pNewListItem = ResourceManager::Push_List_Item();
        pNewListItem->field_4_pNext = pItem->field_4_pNext; // New item points to old
        pItem->field_4_pNext = pNewListItem;                // Old item points to new

        pNewListItem->field_0_ptr = pItem->field_0_ptr + size; // Point the split point

        // Init header of split item
        Header* pHeader = Get_Header_455620(&pNewListItem->field_0_ptr);
        pHeader->field_0_size = sizeForNewRes;
        pHeader->field_8_type = Resource_Free;
        pHeader->field_4_ref_count = 0;
        pHeader->field_C_id = 0;

        // Update old size
        pToSplit->field_0_size = size;
    }

    return pItem;
}

#ifdef TETHYS_SATURN
// SATURN round 5: CAM streaming. Renderer sinks (src/renderer_saturn.cxx):
// Begin latches VDP2 VRAM + CRAM bank on first call; Palette writes the CAM's
// 256 CRAM entries; Pixels CPU-copies a run of the 320x224 plane into VDP2
// (row-stride remap 320->512 inside); End stamps the flip timer.
extern "C" void Tethys_CamStreamBegin();
extern "C" void Tethys_CamStreamPalette(const u8* pal512);
extern "C" void Tethys_CamStreamPixels(u32 absOff, const u8* src, u32 len);
extern "C" void Tethys_CamStreamEnd();
// SATURN (bt872): wipe the foreground sprite layer and PRESENT it before the
// new background is DMA'd to VDP2, so the old screen's foreground disappears
// FIRST (user request, 3rd ask). Renderer-side; see Tethys_ClearForegroundAndPresent.
extern "C" void Tethys_ClearForegroundAndPresent();
// EXACTLY 2,560 B of HWRAM scratch (bt978: the 71,680 B latch-only sCamPix
// is GONE -- the renderer keeps only this small dedicated buffer): [0..2047]
// is the CD sector bounce (4-aligned, HWRAM -> the seam's fast path),
// [2048..2559] assembles the 512 B palette for the one-shot CRAM write.
// HARD CONTRACT: never read/write past [2559] -- the renderer's live CAM
// palette staging (sCamClut) sits in the adjacent .bss. The boot-time
// ACTORPAL.R1 read (src/main.cxx) borrows [0..2047] too, capped at one
// sector by cd_saturn.cxx.
extern "C" u8* Tethys_CamStreamScratch();

namespace {
// Forward, sector-at-a-time reader over one CD file record. Re-seeks before
// every read (the seam cursor is a single global -- LvlArchive.cpp precedent)
// and treats only a FileIOWait of -1 as a hard error (the Saturn seam returns
// 0 for "done", never 1). A failed read leaves the cursor unmoved, so the
// bounded retry re-seeks (OpenArchive precedent).
struct CamSectorReader
{
    u8*  sec;       // 2048 B bounce (renderer scratch slice, HWRAM 4-aligned)
    s32  basePos;   // file-relative start sector of the record
    s32  numSec;    // sectors in the record
    s32  nextSec;   // next sector index to fetch
    u32  bufOff;    // bytes consumed within sec
    u32  bufLen;    // valid bytes in sec (0 => none loaded yet)
    bool bad;

    void Init(u8* bounce, s32 base, s32 count)
    {
        sec = bounce;
        basePos = base;
        numSec = count;
        nextSec = 0;
        bufOff = 0;
        bufLen = 0;
        bad = false;
    }

    bool Fill()
    {
        if (nextSec >= numSec)
        {
            bad = true;
            return false;
        }
        for (s32 attempt = 0; attempt < 8; attempt++)
        {
            CdlLOC loc;
            PSX_Pos_To_CdLoc_49B340(basePos + nextSec, &loc);
            if (PSX_CD_File_Seek_49B670(2, &loc)
                && PSX_CD_File_Read_49B8B0(1, sec)
                && PSX_CD_FileIOWait_49B900(0) != -1)
            {
                nextSec++;
                bufOff = 0;
                bufLen = 2048;
                return true;
            }
        }
        bad = true;
        return false;
    }

    // Copy n bytes forward into dst, spanning sector refills as needed.
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
            for (u32 i = 0; i < take; i++)
            {
                dst[i] = sec[bufOff + i];
            }
            dst += take;
            bufOff += take;
            n -= take;
        }
    }

    // Route n bytes of the pixel plane to VDP2 with no intermediate copy;
    // absOff = the plane offset of the first byte produced by this call.
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

// Fail loud naming the offending .CAM (project rule -- LoadResourceFile_4551E0
// hardened the sibling path the same way). [[noreturn]] via ALIVE_FATAL.
static void Tethys_CamStreamFatal(const char_type* pWhat, const char_type* pName)
{
    static char_type msg[40];
    char_type* p = msg;
    for (const char_type* s = pWhat; *s && p < &msg[31]; s++)
    {
        *p++ = *s;
    }
    for (const char_type* s = pName; *s && p < &msg[39]; s++)
    {
        *p++ = *s;
    }
    *p = 0;
    ALIVE_FATAL(msg);
}

void CC ResourceManager::Tethys_StreamCamFile(Camera* pCamera, bool bitsOnly)
{
    LvlFileRecord* pRec = sLvlArchive_4FFD60.Find_File_Record_41BED0(pCamera->field_1E_fileName);
    if (!pRec)
    {
        Tethys_CamStreamFatal("CAM stream: CD miss ", pCamera->field_1E_fileName);
    }

    // SATURN (bt872): the foreground must vanish BEFORE the background swaps.
    // Present one blank-foreground frame now, while VDP2 still shows the OLD
    // background, so the old screen's sprites are gone the instant the new
    // Bits land (otherwise the frozen VDP1 buffer paints them over the new bg
    // for the whole synchronous stall). The sprite list was already emptied by
    // Tethys_OnScreenChange at the top of ScreenChange_4444D0; this commits it.
    Tethys_ClearForegroundAndPresent();

    // Latch VDP2 first (Begin's one-time LoadBitmap + re-blank; bt978: its
    // DMA source is a fake HWRAM span, the scratch is a separate dedicated
    // buffer -- no ordering constraint between them remains).
    Tethys_CamStreamBegin();

    u8* scratch = Tethys_CamStreamScratch();
    u8* palBuf = scratch + 2048;

    CamSectorReader rd;
    rd.Init(scratch, sLvlArchive_4FFD60.field_4_cd_pos + pRec->field_C_start_sector, pRec->field_10_num_sectors);

    // Walk the BE chunk chain (Bits, [FG1], [Anim], End!) by header size. The
    // ONLY clean exit is the End! sentinel; a truncated read (rd.bad -- 8-retry
    // CD fault or short record) or a malformed size means a torn background and
    // possibly a half-written FG1 chunk, so it must fail loud with the file
    // name (project rule; matches LoadResourceFile_4551E0's "CD miss") rather
    // than commit a partial camera whose Create_FG1s would then walk garbage.
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
            Tethys_CamStreamFatal("CAM stream: bad chunk size ", pCamera->field_1E_fileName);
        }
        const u32 payloadLen = hdr.field_0_size - sizeof(Header);

        if (hdr.field_8_type == Resource_Bits)
        {
            // Payload = u16 w | u16 h | 256 x u16 CRAM | 320*240 8bpp indices.
            if (payloadLen < 4u + 512u) // underflow guard for pixLen below
            {
                Tethys_CamStreamFatal("CAM stream: short Bits ", pCamera->field_1E_fileName);
            }
            u8 wh[4];
            rd.Read(wh, 4);
            const u32 w = (static_cast<u32>(wh[0]) << 8) | wh[1];
            const u32 h = (static_cast<u32>(wh[2]) << 8) | wh[3];
            if (w != 320 || h != 240) // SATURN bt989: 320x240 native vertical
            {
                Tethys_CamStreamFatal("CAM stream: bad Bits hdr ", pCamera->field_1E_fileName);
            }
            rd.Read(palBuf, 512);
            Tethys_CamStreamPalette(palBuf);
            rd.Pixels(0, payloadLen - 4u - 512u); // 76,800 -> VDP2, never heaped
            if (bitsOnly)
            {
                // SATURN (bt817): respawn background refresh. The Bits chunk is
                // ALWAYS first (converter order Bits,[FG1],[Anim],End!), so upload
                // it and STOP -- do not walk the FG1/Anim chunks (the reused
                // camera's field_0_array already holds them; re-pushing would
                // double-free in Camera::dtor). Leave field_30_flags untouched.
                if (rd.bad)
                {
                    Tethys_CamStreamFatal("CAM refresh: torn ", pCamera->field_1E_fileName);
                }
                Tethys_CamStreamEnd();
                return;
            }
        }
        else
        {
            if (bitsOnly)
            {
                // SATURN (bt817): invariant guard -- Bits must be the first chunk;
                // never heap-allocate on the refresh path (see bitsOnly above).
                Tethys_CamStreamFatal("CAM refresh: Bits not first ", pCamera->field_1E_fileName);
            }
            // Fabricate a heap chunk (FG1/Anim) exactly as Move_Resources
            // would: type/id from the on-disk header, ref_count 1, one push
            // into the camera array (Create_FG1s scans it by type; the dtor
            // frees each entry once -> balanced). Our own alloc-retry stands
            // in for the LoadingFile state-0 pressure machinery we bypass
            // (Reclaim runs here -- sResources_Pending_Loading is 0).
            u8** ppRes = nullptr;
            for (s32 attempt = 0; attempt < 4 && !ppRes; attempt++)
            {
                ppRes = Alloc_New_Resource_454F20(hdr.field_8_type, hdr.field_C_id, payloadLen);
                if (!ppRes)
                {
                    Reclaim_Memory_455660(0);
                }
            }
            if (!ppRes)
            {
                ALIVE_FATAL("CAM stream: chunk OOM");
            }
            rd.Read(*ppRes, payloadLen); // *ppRes = payload start (after Alloc's 16 B header)
            if (rd.bad)
            {
                break; // truncated payload -- do NOT commit; fatal below
            }
            pCamera->field_0_array.Push_Back(ppRes);
        }

        if (rd.bad)
        {
            break; // truncated mid-Bits (palette/pixels) -- fatal below
        }
    }

    Tethys_CamStreamEnd();

    if (!sawEnd)
    {
        Tethys_CamStreamFatal("CAM stream: torn ", pCamera->field_1E_fileName);
    }
    pCamera->field_30_flags |= 1u; // camera resources ready (no field_C_ppBits: Bits live in VDP2)
}
#endif

LoadingFile* CC ResourceManager::LoadResourceFile_4551E0(const char_type* pFileName, TLoaderFn fnOnLoad, Camera* pCamera1, Camera* pCamera2)
{
    LvlFileRecord* pFileRec = sLvlArchive_4FFD60.Find_File_Record_41BED0(pFileName);
    if (!pFileRec)
    {
#ifdef TETHYS_SATURN
        // SATURN (bt990): RESTORED TO THE OG SILENT SKIP. This used to fatal
        // ("CD miss <name>") on the theory that every requested name must exist
        // on this path -- it does not. LoadRockTypes_454370 asks every grenade
        // path for DOGBLOW.BAN (ThrowableArray.cpp:49) and R1.LVL has no such
        // record, because RuptureFarms has no Slogs; the PSX data is simply
        // level-scoped and the engine is built to shrug. See the absent-name
        // block above: 79 of the 195 names AliveLibAO references are not in
        // R1.LVL. rs on the AF row counts the skips so this is never silent.
        Tethys_NoteAbsentFile();
#endif
        return nullptr;
    }

    auto pLoadingFile = ao_new<LoadingFile>();
    if (pLoadingFile)
    {
        pLoadingFile->ctor_41E8A0(
            sLvlArchive_4FFD60.field_4_cd_pos + pFileRec->field_C_start_sector,
            pFileRec->field_10_num_sectors,
            fnOnLoad,
            pCamera1,
            pCamera2);
    }

    return pLoadingFile;
}

#ifdef TETHYS_SATURN
extern "C" [[noreturn]] void Tethys_Fatal(const char_type* msg); // SATURN bt814
#endif
u8** ResourceManager::Alloc_New_Resource_Impl(u32 type, u32 id, u32 size, bool locked, BlockAllocMethod allocType, bool bReclaimOnFail)
{
    u8** ppNewRes = Allocate_New_Block_454FE0(size + sizeof(Header), allocType);
    if (!ppNewRes && bReclaimOnFail)
    {
        // SATURN (bt828): the reclaim retry runs Reclaim_Memory_455660, which
        // COMPACTS the heap -- it memmoves every non-locked USED block to a new
        // address and rewrites its handle. Handle-holders survive, but any live
        // object that cached a RAW deref (u8*) of a moved block is left with a
        // DANGLING non-null pointer -> a later write through it stomps live system
        // state (the 0x20000226 SGL-sync region) -> the next indirect dispatch
        // wild-jumps -> silent SH-2 reset (root of the death-on-mine crash: the
        // mine's 4.8 KB cosmetic falling-rocks 3DGibs alloc forced this on the
        // memory-walled R1P15 heap). RELIVE never hit this on PC's 5 MB heap.
        // Callers that pass bReclaimOnFail=false are cosmetic/best-effort: they
        // must tolerate a null (their own guard skips the effect) rather than pay
        // a heap compaction. Default true preserves original behaviour everywhere
        // else. See Allocate_New_Locked_Resource_BestEffort.
        // Failed, try to reclaim some memory and try again.
        Reclaim_Memory_455660(0);
        ppNewRes = Allocate_New_Block_454FE0(size + sizeof(Header), allocType);
    }

    if (ppNewRes)
    {
        Header* pHeader = Get_Header_455620(ppNewRes);
        pHeader->field_8_type = type;
        pHeader->field_C_id = id;
        pHeader->field_4_ref_count = 1;
        pHeader->field_6_flags = locked ? ResourceHeaderFlags::eLocked : 0;
    }
#ifdef TETHYS_SATURN
    else if (bReclaimOnFail)
    {
        // SATURN (bt814): the null-handle bug class. Upstream callers do NOT check
        // this handle for null (PC's 5.12 MB heap never fails); on SH-2 a returned
        // null is *deref'd -> BIOS reset-vector region -> detonates a frame later.
        // Fail LOUD instead of a silent null. Never fires in normal play. (bt828:
        // fourcc+hex detail dropped to save pool; the resource type shows as the
        // faulting call site's map address on the death screen.) best-effort
        // callers (bReclaimOnFail=false) intentionally tolerate null and skip this.
        Tethys_Fatal("RES NULL");
    }
#endif
    return ppNewRes;
}

u8** CC ResourceManager::Alloc_New_Resource_454F20(u32 type, u32 id, u32 size)
{
    return Alloc_New_Resource_Impl(type, id, size, false, BlockAllocMethod::eFirstMatching);
}

u8** CC ResourceManager::Allocate_New_Locked_Resource_454F80(u32 type, u32 id, u32 size)
{
    return Alloc_New_Resource_Impl(type, id, size, true, BlockAllocMethod::eLastMatching);
}


EXPORT u8** CC ResourceManager::Allocate_New_Block_454FE0(u32 sizeBytes, BlockAllocMethod allocMethod)
{
    ResourceHeapItem* pListItem = sFirstLinkedListItem_50EE2C;
    ResourceHeapItem* pHeapMem = nullptr;
    const u32 size = (sizeBytes + 3) & ~3u; // Rounding ??
    Header* pHeaderToUse = nullptr;
    while (pListItem)
    {
        // Is it a free block?
        Header* pResHeader = Get_Header_455620(&pListItem->field_0_ptr);
        if (pResHeader->field_8_type == Resource_Free)
        {
            // Keep going till we hit a block that isn't free
            for (ResourceHeapItem* i = pListItem->field_4_pNext; i; i = pListItem->field_4_pNext)
            {
                Header* pHeader = Get_Header_455620(&i->field_0_ptr);
                if (pHeader->field_8_type != Resource_Free)
                {
                    break;
                }

                // Combine up the free blocks
                pResHeader->field_0_size += pHeader->field_0_size;
                pListItem->field_4_pNext = i->field_4_pNext;
                Pop_List_Item(i);
            }

            // Size will be bigger now that we've freed at least 1 resource
            if (pResHeader->field_0_size >= size)
            {
                switch (allocMethod)
                {
                    case BlockAllocMethod::eFirstMatching:
                        // Use first matching item
                        sManagedMemoryUsedSize_9F0E48 += size;
                        if (sManagedMemoryUsedSize_9F0E48 >= sPeakedManagedMemUsage_9F0E4C)
                        {
                            sPeakedManagedMemUsage_9F0E4C = sManagedMemoryUsedSize_9F0E48;
                        }
                        return &Split_block(pListItem, size)->field_0_ptr;
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
        sAllocationFailed_9F0E50 = 1;
        return nullptr;
    }

    sManagedMemoryUsedSize_9F0E48 += size;
    if (sManagedMemoryUsedSize_9F0E48 >= sPeakedManagedMemUsage_9F0E4C)
    {
        sPeakedManagedMemUsage_9F0E4C = sManagedMemoryUsedSize_9F0E48;
    }

    switch (allocMethod)
    {
            // Note: eFirstMatching case not possible here as pHeapMem case would have early returned
        case BlockAllocMethod::eNearestMatching:
            return &ResourceManager::Split_block(pHeapMem, size)->field_0_ptr;

        case BlockAllocMethod::eLastMatching:
            if (pHeaderToUse->field_0_size - size >= sizeof(Header))
            {
                return &Split_block(pHeapMem, pHeaderToUse->field_0_size - size)->field_4_pNext->field_0_ptr;
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

s16 CC ResourceManager::LoadResourceFileWrapper(const char_type* filename, Camera* pCam)
{
    return LoadResourceFile_455270(filename, pCam, BlockAllocMethod::eFirstMatching);
}

EXPORT s16 CC ResourceManager::LoadResourceFile_455270(const char_type* filename, Camera* pCam, BlockAllocMethod allocMethod)
{
    // Note: None gPcOpenEnabled_508BF0 block not impl as never used

    ResourceManager::LoadingLoop_41EAD0(0);

    LvlFileRecord* pFileRec = sLvlArchive_4FFD60.Find_File_Record_41BED0(filename);
    if (!pFileRec)
    {
        return 0;
    }

    const s32 size = pFileRec->field_10_num_sectors << 11;
    u8** ppRes = ResourceManager::Allocate_New_Block_454FE0(size, allocMethod);
    if (!ppRes)
    {
        ResourceManager::Reclaim_Memory_455660(0);
        ppRes = ResourceManager::Allocate_New_Block_454FE0(size, allocMethod);
        if (!ppRes)
        {
            return 0;
        }
    }

    // NOTE: Not sure why this is done twice, perhaps the above memory compact can invalidate the ptr?
    pFileRec = sLvlArchive_4FFD60.Find_File_Record_41BED0(filename);
    if (!pFileRec)
    {
        return 0;
    }

    if (!sLvlArchive_4FFD60.Read_File_41BE40(pFileRec, Get_Header_455620(ppRes)))
    {
        FreeResource_455550(ppRes);
        return 0;
    }

    ResourceManager::Move_Resources_To_DArray_455430(ppRes, &pCam->field_0_array,
                                                      static_cast<u32>(size)); // SATURN: block bound
    return 1;
}

s16 CC ResourceManager::Move_Resources_To_DArray_455430(u8** ppRes, DynamicArrayT<u8*>* pArray, u32 blockBytes)
{
    auto pItemToAdd = (ResourceHeapItem*) ppRes;
    Header* pHeader = Get_Header_455620(ppRes);
#ifdef TETHYS_SATURN
    // SATURN: end of the loaded file block. The chunk walk below strides by
    // field_0_size; if the on-disk chain lacks a clean terminator it over-runs
    // the block into ADJACENT heap. On LWRAM the adjacent bytes read as a size
    // >= the 1 MB heap and the guard below (>= kResHeapSize) stopped it. With
    // the heap relocated to the 4 MB cart, kResHeapSize is ~4 MB, so an adjacent
    // "size" in (1 MB, 4 MB] slips through and fabricates a resource pointing
    // into the zeroed cart tail -> the wild jump to 0x026d27c0. Bounding the
    // walk to the block makes the over-run detectable regardless of heap size.
    u8* const pBlockEnd = blockBytes
        ? reinterpret_cast<u8*>(Get_Header_455620(ppRes)) + blockBytes
        : nullptr;
#endif
    if (pHeader->field_8_type != Resource_End)
    {
        while (pHeader->field_8_type != Resource_Pend
               && pHeader->field_0_size
               && !(pHeader->field_0_size & 3))
        {
            if (pArray)
            {
                pArray->Push_Back((u8**) pItemToAdd);
                pHeader->field_4_ref_count++;
            }

            pHeader = (Header*) ((s8*) pHeader + pHeader->field_0_size);

            // Out of heap space
#ifdef TETHYS_SATURN
            // SATURN: positional bound first (the over-run past the block is the
            // real corruption signal), then the original size backstop.
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

            ResourceHeapItem* pNewListItem = Push_List_Item();
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
            pHeader->field_0_size = static_cast<u32>(spResourceHeapEnd_9F0E3C - (u8*) pHeader);
        }

        sManagedMemoryUsedSize_9F0E48 -= pHeader->field_0_size;
    }

    return 1;
}

u8** CC ResourceManager::GetLoadedResource_4554F0(u32 type, u32 resourceId, s16 addUseCount, s16 bLock)
{
    // Iterate all list items
    ResourceHeapItem* pListIter = sFirstLinkedListItem_50EE2C;
    while (pListIter)
    {
        // Find something that matches the type and resource ID
        Header* pResHeader = Get_Header_455620(&pListIter->field_0_ptr);
        if (pResHeader->field_8_type == type && pResHeader->field_C_id == resourceId)
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


void ResourceManager::CheckResourceIsLoaded(u32 type, AOResourceID resourceId)
{
    u8** ppRes = GetLoadedResource_4554F0(type, resourceId, FALSE, FALSE);
    if (!ppRes)
    {
        LOG_ERROR("Resource not loaded type " << type << " resource Id " << resourceId);
#ifdef TETHYS_SATURN
        // SATURN (bt990): the resource could never load -- its FILE is not in
        // this level's archive index (DOGBLOW.BAN in R1 and 78 more). The OG
        // stops at the LOG_ERROR above on exactly this case, so do the same;
        // rs on the AF row already counted the skip. Every other miss is still
        // a genuine fault and still fatal (it caught the S4 .ctors bug).
        if (Tethys_IsAbsent(type, static_cast<u32>(resourceId)))
        {
            return;
        }

        // The death screen is the only log -- name the culprit
        // (type fourcc, low byte first + decimal id) in the fatal message.
        static char_type msg[40];
        char_type* p = msg;
        for (const char_type* s = "Res missing "; *s; s++)
        {
            *p++ = *s;
        }
        for (s32 i = 0; i < 4; i++)
        {
            *p++ = static_cast<char_type>((type >> (8 * i)) & 0xFF);
        }
        *p++ = ' ';
        u32 id = static_cast<u32>(resourceId);
        char_type digits[10];
        s32 n = 0;
        do
        {
            digits[n++] = static_cast<char_type>('0' + (id % 10));
            id /= 10;
        } while (id);
        while (n)
        {
            *p++ = digits[--n];
        }
        *p = 0;
        ALIVE_FATAL(msg);
#else
        ALIVE_FATAL("Resource not loaded");
#endif
    }
}

void ResourceManager::CheckResourceIsLoaded(u32 type, std::initializer_list<AOResourceID>& resourceIds)
{
    for (const auto& resourceId : resourceIds)
    {
        CheckResourceIsLoaded(type, resourceId);
    }
}

s16 CC ResourceManager::FreeResource_455550(u8** handle)
{
    // Note: Checks for ptrs of 0xCDCDCDCD and 0xDDDDDDDD removed
    // because these can only come from the MSVCRT debug runtimes
    if (!handle)
    {
        return 1;
    }
    return FreeResource_Impl_4555B0(*handle);
}


s16 CC ResourceManager::FreeResource_Impl_4555B0(u8* handle)
{
    if (handle)
    {
        Header* pHeader = Get_Header_455620(&handle);
        if (pHeader->field_4_ref_count)
        {
            // Decrement ref count, if its not zero then we can't free it yet
            pHeader->field_4_ref_count--;
            if (pHeader->field_4_ref_count > 0)
            {
                return 0;
            }
            pHeader->field_8_type = Resource_Free;
            pHeader->field_6_flags = 0;
            sManagedMemoryUsedSize_9F0E48 -= pHeader->field_0_size;
        }
    }
    return 1;
}

ResourceManager::Header* CC ResourceManager::Get_Header_455620(u8** ppRes)
{
    return reinterpret_cast<Header*>((*ppRes - sizeof(Header)));
}

void CC ResourceManager::Reclaim_Memory_455660(u32 sizeToReclaim)
{
    if (sResources_Pending_Loading_9F0E38 != 0)
    {
        return;
    }

    // If we failed to allocate a block or no size was passed then attempt to reclaim the whole heap
    if (sAllocationFailed_9F0E50 || sizeToReclaim == 0)
    {
        sizeToReclaim = kResHeapSize;
        sAllocationFailed_9F0E50 = 0;
    }

    ResourceHeapItem* pListItem = sFirstLinkedListItem_50EE2C;
    ResourceHeapItem* pToUpdate = nullptr;

    while (pListItem)
    {
        Header* pCurrentHeader = Get_Header_455620(&pListItem->field_0_ptr);
        if (pCurrentHeader->field_8_type == Resource_Free)
        {
            ResourceHeapItem* pNext = pListItem->field_4_pNext;
            if (!pNext)
            {
                return;
            }

            Header* pNextHeader = Get_Header_455620(&pNext->field_0_ptr);
            if (pNextHeader->field_8_type == Resource_Free)
            {
                // Next block is also free, so we can merge them together
                ResourceHeapItem* pToRemove = pListItem->field_4_pNext;
                pCurrentHeader->field_0_size += pNextHeader->field_0_size;
                pListItem->field_4_pNext = pNext->field_4_pNext;
                Pop_List_Item(pToRemove);
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
                        memmove(pDataStart + offset, pDataStart, sizeToMove);
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
                        sFirstLinkedListItem_50EE2C = pNext;
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

void CC ResourceManager::Increment_Pending_Count_4557A0()
{
    sResources_Pending_Loading_9F0E38++;
}

void CC ResourceManager::Decrement_Pending_Count_4557B0()
{
    sResources_Pending_Loading_9F0E38--;
}

void CC ResourceManager::Set_Header_Flags_4557D0(u8** ppRes, s16 flags)
{
    Get_Header_455620(ppRes)->field_6_flags |= flags;
}


void CC ResourceManager::Clear_Header_Flags_4557F0(u8** ppRes, s16 flags)
{
    Get_Header_455620(ppRes)->field_6_flags &= ~flags;
}

s32 CC ResourceManager::Is_Resources_Pending_4557C0()
{
    return sResources_Pending_Loading_9F0E38 > 0 ? 1 : 0;
}

void CC ResourceManager::Free_Resource_Of_Type_455810(u32 type)
{
    ResourceHeapItem* pListItem = sFirstLinkedListItem_50EE2C;
    while (pListItem)
    {
        // Free it if the type matches and its not flagged as never free
        Header* pHeader = Get_Header_455620(&pListItem->field_0_ptr);
        if (pHeader->field_8_type == type && !(pHeader->field_6_flags & ResourceHeaderFlags::eNeverFree))
        {
            pHeader->field_8_type = Resource_Free;
            pHeader->field_6_flags = 0;
            pHeader->field_4_ref_count = 0;

            sManagedMemoryUsedSize_9F0E48 -= pHeader->field_0_size;
        }
        pListItem = pListItem->field_4_pNext;
    }
}

} // namespace AO
