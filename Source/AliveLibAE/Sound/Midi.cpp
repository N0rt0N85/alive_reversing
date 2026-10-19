#include "stdafx.h"
#include "Sound/Midi.hpp"
#include "Function.hpp"
#include "Sound/Sound.hpp"
#include "PathData.hpp"
#include "ResourceManager.hpp"
#include "Game.hpp"
#include "LvlArchive.hpp"
#include "stdlib.hpp"
#include "Abe.hpp"
#include "MusicController.hpp"
#include "BackgroundMusic.hpp"
#include "Sys.hpp"
#include "Io.hpp"

#include "Sfx.hpp"
#include "PathData.hpp"

#include "PsxSpuApi.hpp"
#include "AmbientSound.hpp"


EXPORT void CC SFX_SetPitch_4CA510(const SfxDefinition* pSfx, s32 channelsBits, s16 pitch);

const s32 kSeqTableSizeAE = 144;

static TSNDStopAll sSNDStopAllCallBack = nullptr;
static TSNDRestart sSNDRestartCallBack = nullptr;

ALIVE_VAR(1, 0xBB2354, SeqIds, sSeq_Ids_word_BB2354, {});
ALIVE_VAR(1, 0xbb2e3e, u16, sSnd_ReloadAbeResources_BB2E3E, 0);
ALIVE_VAR(1, 0xbb2e38, OpenSeqHandle*, sSeqDataTable_BB2E38, nullptr);
ALIVE_VAR(1, 0xbb2e3c, s16, sSeqsPlaying_count_word_BB2E3C, 0);
ALIVE_VAR(1, 0xbb2e34, SoundBlockInfo*, sLastLoadedSoundBlockInfo_BB2E34, nullptr);
ALIVE_VAR(1, 0x560f58, s16, sSFXPitchVariationEnabled_560F58, true);
ALIVE_VAR(1, 0x560f40, s16, sNeedToHashSeqNames_560F40, 1);

// I think this is the burrrrrrrrrrrrrrrrrrrr loading sound
const SoundBlockInfo soundBlock = {"MONK.VH", "MONK.VB", -1, nullptr};

ALIVE_VAR(1, 0x560F48, SoundBlockInfo, sMonkVh_Vb_560F48, soundBlock);

class AEMidiVars final : public IMidiVars
{
public:
    virtual SeqIds& sSeq_Ids_word() override
    {
        return sSeq_Ids_word_BB2354;
    }

    virtual u16& sSnd_ReloadAbeResources() override
    {
        return sSnd_ReloadAbeResources_BB2E3E;
    }

    virtual OpenSeqHandle*& sSeqDataTable() override
    {
        return sSeqDataTable_BB2E38;
    }

    virtual s16& sSeqsPlaying_count_word() override
    {
        return sSeqsPlaying_count_word_BB2E3C;
    }

    virtual SoundBlockInfo*& sLastLoadedSoundBlockInfo() override
    {
        return sLastLoadedSoundBlockInfo_BB2E34;
    }

    virtual s16& sSFXPitchVariationEnabled() override
    {
        return sSFXPitchVariationEnabled_560F58;
    }

    virtual s16& sNeedToHashSeqNames() override
    {
        return sNeedToHashSeqNames_560F40;
    }

    virtual SoundBlockInfo& sMonkVh_Vb() override
    {
        return sMonkVh_Vb_560F48;
    }

    virtual s32 MidiTableSize() override
    {
        return kSeqTableSizeAE;
    }

    virtual s16 FreeResource_Impl(u8* handle) override
    {
        return ResourceManager::FreeResource_Impl_49C360(handle);
    }

    virtual u8** GetLoadedResource(u32 type, u32 resourceID, u16 addUseCount, u16 bLock) override
    {
        return ResourceManager::GetLoadedResource_49C2A0(type, resourceID, addUseCount, bLock);
    }

    virtual s16 FreeResource(u8** handle) override
    {
        return ResourceManager::FreeResource_49C330(handle);
    }

    virtual u8** Allocate_New_Locked_Resource(u32 type, u32 id, u32 size) override
    {
        return ResourceManager::Allocate_New_Locked_Resource_49BF40(type, id, size);
    }

    virtual void LoadingLoop(s16 bShowLoadingIcon) override
    {
        pResourceManager_5C1BB0->LoadingLoop_465590(bShowLoadingIcon);
    }

    virtual void Reclaim_Memory(u32 size) override
    {
        ResourceManager::Reclaim_Memory_49C470(size);
    }

    virtual u8** Alloc_New_Resource(u32 type, u32 id, u32 size) override
    {
        return ResourceManager::Alloc_New_Resource_49BED0(type, id, size);
    }

    virtual s16 LoadResourceFile(const char_type* pFileName, Camera* pCamera) override
    {
        return ResourceManager::LoadResourceFile_49C170(pFileName, pCamera);
    }
};

// SATURN 453.ao.2: THE EXODDUS DEFAULT STAYS, ON EVERY BUILD. 450.ao.1 started
// this pointer (and gSpuVars, PsxSpuApi.cpp) at null, on the belief that
// SND_Init_476E40 installing AO's vars is the first sound call of the boot. It
// is not: PSX_VSync_4F6170 (sys_saturn.cxx) ticks SsSeqCalledTbyT_4FDC80 from
// the first frame, and its first virtual call read a vtable at address 0 and
// jumped into the BIOS (453.ao.1, boot crash on Ymir, pc e0ff600c). Until AO
// installs its own, the AE instance is what every early caller reads.
static AEMidiVars sAEMidiVars;
static IMidiVars* spMidiVars = &sAEMidiVars; // Default to AE vars

EXPORT IMidiVars* GetMidiVars()
{
    return spMidiVars;
}

EXPORT void SetMidiApiVars(IMidiVars* pVars)
{
    spMidiVars = pVars;
}

EXPORT void CC SND_Free_All_VABS_4C9EB0()
{
    SoundBlockInfo* pIter = GetMidiVars()->sLastLoadedSoundBlockInfo();
    while (pIter && pIter->field_4_vab_body_name)
    {
        GetMidiVars()->FreeResource_Impl(pIter->field_C_pVabHeader);
        pIter->field_C_pVabHeader = nullptr;
        SsVabClose_4FC5B0(pIter->field_8_vab_id);
        pIter->field_8_vab_id = -1;
        pIter++;
    }
    GetMidiVars()->sLastLoadedSoundBlockInfo() = nullptr;
}

EXPORT void CC SND_Free_All_Seqs_4C9F40()
{
    for (s32 i = 0; i < GetMidiVars()->MidiTableSize(); i++)
    {
        if (GetMidiVars()->sSeqDataTable()[i].field_C_ppSeq_Data)
        {
            u8** ppRes = GetMidiVars()->GetLoadedResource(ResourceManager::Resource_Seq, GetMidiVars()->sSeqDataTable()[i].field_4_generated_res_id, 0, 0);
            GetMidiVars()->FreeResource(ppRes);
            GetMidiVars()->sSeqDataTable()[i].field_C_ppSeq_Data = nullptr;
        }
    }
}

EXPORT void SND_Reset_4C9FB0()
{
    SND_Stop_All_Seqs_4CA850();
    SND_Free_All_Seqs_4C9F40();
    SND_Free_All_VABS_4C9EB0();
    SsSetMVol_4FC360(100, 100);
}


#if defined(TETHYS_GAME_AE)
// SATURN (Exoddus, stream R4): THE LEVEL'S SOUND BANK ON THE SATURN (decisions
// D-So2, D-So4; U6 F4, S5, S8; U1b T9, T10). AO's 449.ao.6 loader
// (AliveLibAO/Midi.cpp SND_VAB_Load_476CB0 and its reload) on Exoddus's names:
//   - the VH in a locked block that is refused softly, null-tested (the PC
//     dereferenced an unchecked allocation);
//   - the VB never whole in the heap: a window, the whole bank when the heap
//     can hold it (one CD read), else 128, 64, 32 KB, walked forward by
//     src/snd_vb_stream_ae.cxx, each record becoming a sample in
//     Tethys_SsVabTransBody_Streamed (Sound/PsxSpuApi.cpp): PCM8 at the
//     record's base rate, no short-sound doubling, the reverb bit kept;
//   - under 32 KB of heap the level plays mute (soft, counted) instead of
//     the PC's free-Abe-and-retry, which dereferenced a null sActiveHero at
//     the menu (sSnd_ReloadAbeResources is never set);
//   - MONK is not loaded (D-So2): its VH declares id 0 like every level bank,
//     which evicts it at once (U6 2.3); 4 KB of locked heap and one CD
//     command saved per level and per film exit.
#include "Psx.hpp" // the CD seam: PSX_Pos_To_CdLoc_4FADD0, PSX_CD_File_Seek/Read/FileIOWait
#include "TethysVbStream.hpp"

// SATURN (Exoddus, U1b T9): a sound-less disc, for a hardware test that must
// rule the sound out. 1 = no bank is ever read (no CD time, no window): every
// note and effect fails soft at the sSoundDatIsNull gate, the SEQs still load.
// Off by default; `make TETHYS_GAME=AE` passes it once the Makefile carries the
// knob (requests.md, R4 -> lead).
#ifndef TETHYS_AE_MUTE
#define TETHYS_AE_MUTE 0
#endif

bool Tethys_SsVabTransBody_Streamed(const TethysVbSource& src, s16 vabId, u8* pWin, s32 winBytes); // Sound/PsxSpuApi.cpp
u32 Tethys_ResHeapSize();                    // ResourceManager.cpp: the bound heap's capacity
extern "C" u32 Tethys_gSnd[10];              // src/sound_saturn.cxx: [8] banks streamed whole, [9] partly

// SATURN (Exoddus, U6 S10): the last bank load, level or film exit, for the
// overlay's sound row: [0] its milliseconds (CD reads and uploads), [1] the
// window in KB (0 = refused, the level plays mute), [2] the heap's free KB when
// the window was asked, [3] bank loads since boot. With Tethys_gSnd[5] (a note
// on a record that has no sample: must stay 0 through play, the condition of
// the converter's emptied unreachable records, C4 D5) this is the survey.
extern "C" u32 Tethys_gSndBank[4] = {};

namespace {

// The window ladder (AO 449.ao.6 Tethys_VbWindowAlloc): firstAsk, then 128, 64
// and 32 KB. A rung the heap cannot hold is skipped by arithmetic, never by a
// failed allocation (a refusal with reclaim compacts the heap for nothing);
// the 32 KB floor is always tried. Id 127 is outside kMaxVabs: scratch.
u8** Tethys_VbWindowAlloc(s32 firstAsk, s32* pWinBytes)
{
    s32 winBytes = firstAsk;
    {
        const u32 cap = Tethys_ResHeapSize();
        const u32 used = sManagedMemoryUsedSize_AB4A04;
        Tethys_gSndBank[2] = cap >= used ? (cap - used) >> 10 : 0;
    }
    for (;;)
    {
        const u32 cap = Tethys_ResHeapSize();
        const u32 used = sManagedMemoryUsedSize_AB4A04;
        const bool affordable = cap >= used && (cap - used) >= static_cast<u32>(winBytes) + 16u;
        u8** ppWin = nullptr;
        if (affordable || winBytes <= 32768)
        {
            ppWin = ResourceManager::Alloc_New_Resource_ImplEx(ResourceManager::Resource_VabBody, 127, winBytes,
                                                              false, ResourceManager::BlockAllocMethod::eFirstMatching,
                                                              true /*bReclaimOnFail*/, false /*bFatalOnFail*/);
        }
        if (ppWin || winBytes <= 32768)
        {
            *pWinBytes = winBytes;
            Tethys_gSndBank[1] = ppWin ? static_cast<u32>(winBytes) >> 10 : 0;
            return ppWin;
        }
        winBytes = winBytes > 131072 ? 131072 : (winBytes > 65536 ? 65536 : 32768);
    }
}

// The streamer's byte source: whole sectors of one LVL record, through the
// same seam LvlArchive::Read_File_4330A0 uses. The read's RESULT is checked
// (Read_File only asks FileIOWait, which on the Saturn tests that a handle
// exists): a failed read must not upload the previous window's bytes.
bool Tethys_VbReadSectors(void* ctx, s32 first, s32 sectors, u8* pDst)
{
    const LvlFileRecord* pRec = static_cast<const LvlFileRecord*>(ctx);
    CdlLOC loc = {};
    PSX_Pos_To_CdLoc_4FADD0(static_cast<s32>(sLvlArchive_5BC520.field_4_cd_pos) + pRec->field_C_start_sector + first, &loc);
    PSX_CD_File_Seek_4FB1E0(2, &loc);
    return PSX_CD_File_Read_4FB210(sectors, pDst) > 0 && PSX_CD_FileIOWait_4FB260(0) != -1;
}

// One bank's samples through the window: counted [8] whole, [9] partial (the
// samples that landed play, the others are silent: SND_LoadChunk keys every
// entry to what arrived).
void Tethys_VbStream(LvlFileRecord* pVabBodyFile, s16 vabId, u8* pWin, s32 winBytes)
{
    const u32 t0 = SYS_GetTicks();
    const TethysVbSource src = {&Tethys_VbReadSectors, pVabBodyFile, pVabBodyFile->field_10_num_sectors << 11};
    if (Tethys_SsVabTransBody_Streamed(src, vabId, pWin, winBytes))
    {
        Tethys_gSnd[8]++;
    }
    else
    {
        Tethys_gSnd[9]++;
    }
    SsVabTransCompleted_4FE060(SS_WAIT_COMPLETED);
    Tethys_gSndBank[0] = SYS_GetTicks() - t0;
    Tethys_gSndBank[3]++;
}

} // namespace

EXPORT s16 CC SND_VAB_Load_4C9FE0(SoundBlockInfo* pSoundBlockInfo, s16 vabId)
{
    if (!pSoundBlockInfo->field_0_vab_header_name)
    {
        return 0;
    }
    LvlFileRecord* pVabHeaderFile = sLvlArchive_5BC520.Find_File_Record_433160(pSoundBlockInfo->field_0_vab_header_name);
    if (!pVabHeaderFile)
    {
        return 0; // the PC dereferenced it
    }
    const s32 headerSize = pVabHeaderFile->field_10_num_sectors << 11;
    // Allocate_New_Locked_Resource_49BF40's block (locked, last fit, compaction
    // first), refused softly: the guard below could never run behind the fatal.
    u8** ppVabHeader = ResourceManager::Alloc_New_Resource_ImplEx(ResourceManager::Resource_VabHeader, vabId, headerSize,
                                                                  true, ResourceManager::BlockAllocMethod::eLastMatching,
                                                                  true /*bReclaimOnFail*/, false /*bFatalOnFail*/);
    if (!ppVabHeader)
    {
        return 0;
    }
    pSoundBlockInfo->field_C_pVabHeader = *ppVabHeader;
    sLvlArchive_5BC520.Read_File_4330A0(pVabHeaderFile, *ppVabHeader);
    GetMidiVars()->LoadingLoop(0);

    LvlFileRecord* pVabBodyFile = sLvlArchive_5BC520.Find_File_Record_433160(pSoundBlockInfo->field_4_vab_body_name);
    if (!pVabBodyFile)
    {
        return 0; // a VH without a VB: the PC's own tolerance
    }
    // The window comes BEFORE SsVabOpenHead, as the bank did: a refusal leaves
    // no open head without samples. The whole bank first: one CD read.
    const s32 vabBodySize = pVabBodyFile->field_10_num_sectors << 11;
    s32 winBytes = 0;
    u8** ppWin = Tethys_VbWindowAlloc(vabBodySize > 32768 ? vabBodySize : 32768, &winBytes);
    if (!ppWin)
    {
        return 0; // under 32 KB free: this level plays mute (counted on the rn row)
    }
    pSoundBlockInfo->field_8_vab_id = SsVabOpenHead_4FC620(reinterpret_cast<VabHeader*>(pSoundBlockInfo->field_C_pVabHeader));
    if (pSoundBlockInfo->field_8_vab_id < 0 || pSoundBlockInfo->field_8_vab_id >= kMaxVabs)
    {
        ResourceManager::FreeResource_49C330(ppWin);
        return 0;
    }
    Tethys_VbStream(pVabBodyFile, static_cast<s16>(pSoundBlockInfo->field_8_vab_id), *ppWin, winBytes);
    ResourceManager::FreeResource_49C330(ppWin);
    return 1;
}

// After a Cinepak film the SGL 68K driver owned sound RAM (src/movie_cinepak.cxx
// RestoreScspBackend): every sample of the level's bank goes back up through the
// re-opened backend. AO's Tethys_SND_VAB_Reload_Saturn (373.ao.1, 420.ao.1,
// 449.ao.6) on Exoddus's chain: the still-RESIDENT locked VH is reused (a second
// Allocate_New_Locked would leak one per film), SsVabOpenHead refills the tables
// (its close and SND_Free sweep are inert on the fresh backend, its sequencer
// tick gated off: the caller holds sbDisableSeqs), then the window streams the
// VB again. One window for the chain, 128 KB first (4 reads for a 249-sector
// bank instead of 16 at 32 KB, measured cost per read 78.6 ms + 6.6 ms/sector),
// the ladder skipping what the heap cannot hold; the film's borrowed block is
// back before this runs. No MONK (D-So2). sLastLoadedSoundBlockInfo is left as
// it is, so SND_Load_VABS_4CA350 keeps refusing a duplicate full load.
void Tethys_SND_VAB_Reload_Saturn()
{
#if TETHYS_AE_MUTE
    return;
#else
    SoundBlockInfo* pIter = GetMidiVars()->sLastLoadedSoundBlockInfo();
    if (!pIter)
    {
        return; // a film before any level (the boot logos): nothing was resident
    }
    s32 winBytes = 0;
    u8** ppWin = Tethys_VbWindowAlloc(131072, &winBytes);
    if (!ppWin)
    {
        return; // counted on the rn row; the level stays mute until it reloads
    }
    for (; pIter->field_0_vab_header_name; pIter++)
    {
        if (!pIter->field_C_pVabHeader || !pIter->field_4_vab_body_name
            || pIter->field_8_vab_id < 0 || pIter->field_8_vab_id >= kMaxVabs)
        {
            continue; // never loaded: nothing to restore
        }
        LvlFileRecord* pVabBodyFile = sLvlArchive_5BC520.Find_File_Record_433160(pIter->field_4_vab_body_name);
        if (!pVabBodyFile)
        {
            continue;
        }
        pIter->field_8_vab_id = SsVabOpenHead_4FC620(reinterpret_cast<VabHeader*>(pIter->field_C_pVabHeader));
        if (pIter->field_8_vab_id < 0 || pIter->field_8_vab_id >= kMaxVabs)
        {
            continue;
        }
        Tethys_VbStream(pVabBodyFile, static_cast<s16>(pIter->field_8_vab_id), *ppWin, winBytes);
    }
    ResourceManager::FreeResource_49C330(ppWin);
#endif
}
#else
EXPORT s16 CC SND_VAB_Load_4C9FE0(SoundBlockInfo* pSoundBlockInfo, s16 vabId)
{
    // Fail if no file name
    if (!pSoundBlockInfo->field_0_vab_header_name)
    {
        return 0;
    }

    // Find the VH file record
    s32 headerSize = 0;
    LvlFileRecord* pVabHeaderFile = sLvlArchive_5BC520.Find_File_Record_433160(pSoundBlockInfo->field_0_vab_header_name);
    if (sbEnable_PCOpen_5CA4B0)
    {
        headerSize = pVabHeaderFile->field_14_file_size;
    }
    else
    {
        headerSize = pVabHeaderFile->field_10_num_sectors << 11;
    }

    // Load the VH file data
    u8** ppVabHeader = GetMidiVars()->Allocate_New_Locked_Resource(ResourceManager::Resource_VabHeader, vabId, headerSize);
    pSoundBlockInfo->field_C_pVabHeader = *ppVabHeader;
    sLvlArchive_5BC520.Read_File_4330A0(pVabHeaderFile, *ppVabHeader);
    GetMidiVars()->LoadingLoop(0);

    // Find the VB file record
    LvlFileRecord* pVabBodyFile = sLvlArchive_5BC520.Find_File_Record_433160(pSoundBlockInfo->field_4_vab_body_name);
    if (!pVabBodyFile)
    {
        // For some reason its acceptable to assume we have a VH with no VB, but the VH must always exist, this happens for MONK.VB
        return 0;
    }

    s32 vabBodySize = 0;
    if (sbEnable_PCOpen_5CA4B0)
    {
        vabBodySize = pVabBodyFile->field_14_file_size;
    }
    else
    {
        vabBodySize = pVabBodyFile->field_10_num_sectors << 11; // TODO * 4096 ?
    }

    // Load the VB file data
    u8** ppVabBody = GetMidiVars()->Alloc_New_Resource(ResourceManager::Resource_VabBody, vabId, vabBodySize);
    if (!ppVabBody)
    {
        // Maybe filed due to OOM cause its huge, free the abe resources and try again
        if (!GetMidiVars()->sSnd_ReloadAbeResources())
        {
            GetMidiVars()->sSnd_ReloadAbeResources() = TRUE;
            sActiveHero_5C1B68->Free_Resources_44D420();
        }

        // Compact/reclaim any other memory we can too
        GetMidiVars()->Reclaim_Memory(0);

        // If it fails again there is no recovery, in either case caller will restore abes resources
        ppVabBody = GetMidiVars()->Alloc_New_Resource(ResourceManager::Resource_VabBody, vabId, vabBodySize);
        if (!ppVabBody)
        {
            return 0;
        }
    }

    // Now we can read the actual VB data
    sLvlArchive_5BC520.Read_File_4330A0(pVabBodyFile, *ppVabBody);

    // Convert the records in the header to internal representation
    pSoundBlockInfo->field_8_vab_id = SsVabOpenHead_4FC620(reinterpret_cast<VabHeader*>(pSoundBlockInfo->field_C_pVabHeader));

    // Load actual sample data

    SsVabTransBody_4FC840(reinterpret_cast<VabBodyRecord*>(*ppVabBody), static_cast<s16>(pSoundBlockInfo->field_8_vab_id));

    SsVabTransCompleted_4FE060(SS_WAIT_COMPLETED);

    // Now the sound samples are loaded we don't need the VB data anymore
    GetMidiVars()->FreeResource(ppVabBody);
    return 1;
}
#endif

// TODO: PSX has VSyncCallback here 0x800592dc

EXPORT s32 CC MIDI_Play_Single_Note_4CA1B0(s32 vabIdAndProgram, s32 note, s32 leftVol, s32 rightVol)
{
    // NOTE: word_BB2E40 is used as a guard here, but it is never read anywhere else
    return SsVoKeyOn_4FCF10(vabIdAndProgram, note, static_cast<u16>(leftVol), static_cast<u16>(rightVol));
}

EXPORT void CC SND_Init_4CA1F0()
{
    SSInit_4FC230();
    SsSetTableSize_4FE0B0(nullptr, 16, 1);
    SsUtSetReverbDepth_4FE380(0, 0);
    SsUtSetReverbType_4FE360(4);
    SsUtReverbOn_4FE340();
    SsSetTickMode_4FDC20(4096);
    VSyncCallback_4F8C40(SND_CallBack_4020A4);
    SpuInitHot_4FC320();
    SsSetMVol_4FC360(100, 100);
    memset(&GetMidiVars()->sSeq_Ids_word(), -1, sizeof(SeqIds));
    GetMidiVars()->sSeqsPlaying_count_word() = 0;
}

// SND_SetMono_NoRefs_4CA310

// SND_SetStereo_NoRefs_4CA330

EXPORT void SND_Shutdown_4CA280()
{
    SND_Reset_4C9FB0();

    if (GetMidiVars()->sMonkVh_Vb().field_8_vab_id >= 0)
    {
        GetMidiVars()->FreeResource_Impl(GetMidiVars()->sMonkVh_Vb().field_C_pVabHeader);
        GetMidiVars()->sMonkVh_Vb().field_C_pVabHeader = nullptr;

        SsVabClose_4FC5B0(GetMidiVars()->sMonkVh_Vb().field_8_vab_id);
        GetMidiVars()->sMonkVh_Vb().field_8_vab_id = -1;
    }

    SsSetMVol_4FC360(0, 0);
    SsUtReverbOff_4FE350();
    SsUtSetReverbDepth_4FE380(0, 0);
    SsEnd_4FC350();

    // TODO: PSX calls a func that just does SpuQuit() here and nothing else
    SsExt_StopPlayingSamples();

    SsExt_CloseAllVabs();

    GetSoundAPI().SND_SsQuit();
}


#if defined(TETHYS_GAME_AE)
// SATURN (Exoddus, stream R4): the PC's flow minus MONK (D-So2) and minus the
// Abe-resources dance (the window loader never frees them, see SND_VAB_Load
// above). With the mute knob no bank is read at all; the block list is still
// recorded, because every effect looks its vab id up through it.
EXPORT void CC SND_Load_VABS_4CA350(SoundBlockInfo* pSoundBlockInfo, s32 reverb)
{
    SoundBlockInfo* pSoundBlockInfoIter = pSoundBlockInfo;
    GetMidiVars()->sSnd_ReloadAbeResources() = FALSE;
    if (GetMidiVars()->sLastLoadedSoundBlockInfo() != pSoundBlockInfo)
    {
        SsUtReverbOff_4FE350();
        SsUtSetReverbDepth_4FE380(0, 0);
        SpuClearReverbWorkArea_4FA690(4);

        GetMidiVars()->sLastLoadedSoundBlockInfo() = pSoundBlockInfo;
#if !TETHYS_AE_MUTE
        s16 vabId = 0;
        while (SND_VAB_Load_4C9FE0(pSoundBlockInfoIter, vabId))
        {
            ++vabId;
            ++pSoundBlockInfoIter;
        }
#endif
        SsUtSetReverbDepth_4FE380(reverb, reverb);
        SsUtReverbOn_4FE340();
    }
}
#else
EXPORT void CC SND_Load_VABS_4CA350(SoundBlockInfo* pSoundBlockInfo, s32 reverb)
{
    SoundBlockInfo* pSoundBlockInfoIter = pSoundBlockInfo;
    GetMidiVars()->sSnd_ReloadAbeResources() = FALSE;
    if (GetMidiVars()->sLastLoadedSoundBlockInfo() != pSoundBlockInfo)
    {
        SsUtReverbOff_4FE350();
        SsUtSetReverbDepth_4FE380(0, 0);
        SpuClearReverbWorkArea_4FA690(4);

        if (GetMidiVars()->sMonkVh_Vb().field_8_vab_id < 0)
        {
            SND_VAB_Load_4C9FE0(&GetMidiVars()->sMonkVh_Vb(), 32);
        }

        GetMidiVars()->sLastLoadedSoundBlockInfo() = pSoundBlockInfo;

        s16 vabId = 0;
        while (SND_VAB_Load_4C9FE0(pSoundBlockInfoIter, vabId))
        {
            ++vabId;
            ++pSoundBlockInfoIter;
        }

        // Put abes resources back if we had to unload them to fit the VB in memory
        if (GetMidiVars()->sSnd_ReloadAbeResources())
        {
            GetMidiVars()->Reclaim_Memory(0);
            sActiveHero_5C1B68->Load_Basic_Resources_44D460();
        }

        SsUtSetReverbDepth_4FE380(reverb, reverb);
        SsUtReverbOn_4FE340();
    }
}
#endif


s32 CC SFX_SfxDefinition_Play_4CA420(const SfxDefinition* sfxDef, s16 volume, s16 pitch_min, s16 pitch_max)
{
    if (!volume)
    {
        volume = sfxDef->field_3_default_volume;
    }

    if (pitch_min == 0x7FFF)
    {
        pitch_min = sfxDef->field_4_pitch_min;
    }

    if (pitch_max == 0x7FFF)
    {
        pitch_max = sfxDef->field_6_pitch_max;
    }

    if (volume < 1)
    {
        volume = 1;
    }
    else if (volume >= 127)
    {
        volume = 127;
    }

    // Note: Inlined in psx
    auto midiHandle = MIDI_Play_Single_Note_4CA1B0(
        sfxDef->field_1_program | (GetMidiVars()->sLastLoadedSoundBlockInfo()[sfxDef->field_0_block_idx].field_8_vab_id << 8),
        sfxDef->field_2_note << 8,
        volume,
        volume);

    if (!GetMidiVars()->sSFXPitchVariationEnabled())
    {
        return 0;
    }

    if (pitch_min || pitch_max)
    {
        // Note: Inlined in psx
        SFX_SetPitch_4CA510(sfxDef, midiHandle, Math_RandomRange_496AB0(pitch_min, pitch_max));
    }

    return midiHandle;
}


EXPORT void CC SFX_SetPitch_4CA510(const SfxDefinition* pSfx, s32 channelsBits, s16 pitch)
{
    s32 v3 = 0;
    s16 v4 = 0;

    if (pitch >= 0)
    {
        v3 = (pitch >> 7) & 0xFFFF;
        v4 = pitch & 127;
    }
    else
    {
        v3 = -1 - (-pitch >> 7);
        v4 = 127 - (-(s8) pitch & 127);
    }

    for (s16 i = 0; i < 24; i++) // TODO: use kNumChannels
    {
        if ((1 << i) & channelsBits)
        {
            const s16 vabId = 0;   // Not used by target func
            const s16 program = 0; // Not used by target func
            SsUtChangePitch_4FDF70(i, program, vabId, pSfx->field_2_note, 0, static_cast<s16>(static_cast<s32>(pSfx->field_2_note) + v3), v4);
        }
    }
}

EXPORT s32 CC SND_4CA5D0(s32 program, s32 vabId, s32 note, s16 vol, s16 min, s16 max)
{
    s32 volClamped = 0;
    if (vol < 10)
    {
        volClamped = 10;
    }
    else
    {
        volClamped = vol;
        if (vol >= 127)
        {
            volClamped = 127;
        }
    }

    // Note: Inlined in psx
    const s32 channelBits = MIDI_Play_Single_Note_4CA1B0(vabId | ((s16) program << 8), note << 8, volClamped, volClamped);
    if (!GetMidiVars()->sSFXPitchVariationEnabled())
    {
        return 0;
    }

    if (min || max)
    {
        s16 randomValue = Math_RandomRange_496AB0(min, max);

        s32 v9;  // edi
        s16 v10; // bx
        if (randomValue >= 0)
        {
            v9 = (randomValue >> 7) & 0xFFFF;
            v10 = randomValue & 127;
        }
        else
        {
            v9 = -1 - (-randomValue >> 7);
            v10 = 127 - (-(s8) randomValue & 127);
        }

        for (s16 i = 0; i < 24; i++) // TODO: Use kNumChannels
        {
            if ((1 << i) & channelBits)
            {
                SsUtChangePitch_4FDF70(i, program, vabId, static_cast<s16>(note), 0, static_cast<s16>(v9 + note), v10);
            }
        }
    }
    return channelBits;
}

s32 CC SFX_SfxDefinition_Play_4CA700(const SfxDefinition* sfxDef, s16 volLeft, s16 volRight, s16 pitch_min, s16 pitch_max)
{
    if (pitch_min == 0x7FFF)
    {
        pitch_min = sfxDef->field_4_pitch_min;
    }

    if (pitch_max == 0x7FFF)
    {
        pitch_max = sfxDef->field_6_pitch_max;
    }

    if (volLeft < 10)
    {
        volLeft = 10;
    }
    else if (volLeft >= 127)
    {
        volLeft = 127;
    }

    if (volRight < 10)
    {
        volRight = 10;
    }
    else if (volRight >= 127)
    {
        volRight = 127;
    }

    // Note: Inlined in psx
    auto midiHandle = MIDI_Play_Single_Note_4CA1B0(
        sfxDef->field_1_program | (GetMidiVars()->sLastLoadedSoundBlockInfo()[sfxDef->field_0_block_idx].field_8_vab_id << 8),
        sfxDef->field_2_note << 8,
        volLeft,
        volRight);

    if (!GetMidiVars()->sSFXPitchVariationEnabled())
    {
        return 0;
    }

    if (pitch_min || pitch_max)
    {
        SFX_SetPitch_4CA510(sfxDef, midiHandle, Math_RandomRange_496AB0(pitch_min, pitch_max));
    }

    return midiHandle;
}

EXPORT void CC SND_Stop_Channels_Mask_4CA810(u32 bitMask)
{
    for (s32 i = 0; i < 24; i++) // TODO: Constant
    {
        // Does the index match a bit in the bitmask?
        if ((1 << i) & bitMask)
        {
            // Turn it off
            GetSpuApiVars()->SsUtKeyOffV(static_cast<s16>(i));
        }
    }
}

EXPORT void SND_Stop_All_Seqs_4CA850()
{
    // TODO: Why is there 16 of these but 32 of sMidiStruct2Ary32_C13400? Seems like they should match in size
    GetMidiVars()->sSeqsPlaying_count_word() = 0;
    for (s16 i = 0; i < 16; i++)
    {
        if (GetMidiVars()->sSeq_Ids_word().ids[i] >= 0)
        {
            if (SsIsEos_4FDA80(i, 0))
            {
                SsSeqStop_4FD9C0(i);
            }
            SsSeqClose_4FD8D0(i);
            GetMidiVars()->sSeqDataTable()[GetMidiVars()->sSeq_Ids_word().ids[i]].field_A_id_seqOpenId = -1;
            GetMidiVars()->sSeq_Ids_word().ids[i] = -1;
        }
    }
}


EXPORT void SND_Seq_Stop_4CA8E0()
{
    for (s16 i = 0; i < 16; i++)
    {
        if (GetMidiVars()->sSeq_Ids_word().ids[i] >= 0)
        {
            if (!SsIsEos_4FDA80(i, 0))
            {
                SsSeqClose_4FD8D0(i);
                GetMidiVars()->sSeqDataTable()[GetMidiVars()->sSeq_Ids_word().ids[i]].field_A_id_seqOpenId = -1;
                GetMidiVars()->sSeq_Ids_word().ids[i] = -1;
                GetMidiVars()->sSeqsPlaying_count_word()--;
            }
        }
    }
}

EXPORT s16 CC SND_SEQ_PlaySeq_4CA960(u16 idx, s16 repeatCount, s16 bDontStop)
{
    OpenSeqHandle& rec = GetMidiVars()->sSeqDataTable()[idx];
    if (!rec.field_C_ppSeq_Data)
    {
        return 0;
    }

    if (rec.field_A_id_seqOpenId < 0)
    {
        if (GetMidiVars()->sSeqsPlaying_count_word() >= 16)
        {
            // Note: Inlined in psx
            SND_Seq_Stop_4CA8E0();
            if (GetMidiVars()->sSeqsPlaying_count_word() >= 16)
            {
                return 0;
            }
        }

        const s32 vabId = GetMidiVars()->sLastLoadedSoundBlockInfo()[rec.field_8_sound_block_idx].field_8_vab_id;
        rec.field_A_id_seqOpenId = SsSeqOpen_4FD6D0(rec.field_C_ppSeq_Data, static_cast<s16>(vabId));

        GetMidiVars()->sSeq_Ids_word().ids[rec.field_A_id_seqOpenId] = idx;
        GetMidiVars()->sSeqsPlaying_count_word()++;
    }
    else if (SsIsEos_4FDA80(rec.field_A_id_seqOpenId, 0))
    {
        if (!bDontStop)
        {
            return 0;
        }
        SsSeqStop_4FD9C0(rec.field_A_id_seqOpenId);
    }

    // Clamp vol
    s16 clampedVol = rec.field_9_volume;
    if (clampedVol <= 10)
    {
        clampedVol = 10;
    }
    else
    {
        if (clampedVol >= 127)
        {
            clampedVol = 127;
        }
    }

    SsSeqSetVol_4FDAC0(rec.field_A_id_seqOpenId, clampedVol, clampedVol);
    if (repeatCount)
    {
        SsSeqPlay_4FD900(rec.field_A_id_seqOpenId, 1, repeatCount);
    }
    else
    {
        SsSeqPlay_4FD900(rec.field_A_id_seqOpenId, 1, 0);
    }

    return 1;
}


EXPORT s16 CC SND_SEQ_Play_4CAB10(u16 idx, s16 repeatCount, s16 volLeft, s16 volRight)
{
    OpenSeqHandle& rec = GetMidiVars()->sSeqDataTable()[idx];
    if (!rec.field_C_ppSeq_Data)
    {
        return 0;
    }

    // SEQ isn't in use
    if (rec.field_A_id_seqOpenId < 0)
    {
        // Too many playing
        if (GetMidiVars()->sSeqsPlaying_count_word() >= 16)
        {
            // Stop any SEQs that are done
            SND_Seq_Stop_4CA8E0();

            // If none where done then can't continue
            if (GetMidiVars()->sSeqsPlaying_count_word() >= 16)
            {
                return 0;
            }
        }

        // Open the SEQ
        const s16 vabId = static_cast<s16>(GetMidiVars()->sLastLoadedSoundBlockInfo()[rec.field_8_sound_block_idx].field_8_vab_id);
        rec.field_A_id_seqOpenId = SsSeqOpen_4FD6D0(rec.field_C_ppSeq_Data, vabId);

        // Index into the IDS via the seq ID and map it to the index
        GetMidiVars()->sSeq_Ids_word().ids[rec.field_A_id_seqOpenId] = idx;
        GetMidiVars()->sSeqsPlaying_count_word()++;
    }
    else if (SsIsEos_4FDA80(rec.field_A_id_seqOpenId, 0))
    {
        SsSeqStop_4FD9C0(rec.field_A_id_seqOpenId);
    }

    // Clamp left
    s16 clampedVolLeft = 0;
    if (volLeft < 10)
    {
        clampedVolLeft = 10;
    }
    else if (volLeft >= 127)
    {
        clampedVolLeft = 127;
    }
    else
    {
        clampedVolLeft = volLeft;
    }

    // Clamp right
    s16 clampedVolRight = 0;
    if (volRight < 10)
    {
        clampedVolRight = 10;
    }
    else
    {
        clampedVolRight = volRight;
        if (volRight >= 127)
        {
            clampedVolRight = 127;
        }
    }

    SsSeqSetVol_4FDAC0(rec.field_A_id_seqOpenId, clampedVolLeft, clampedVolRight);

    if (repeatCount)
    {
        SsSeqPlay_4FD900(rec.field_A_id_seqOpenId, 1, repeatCount);
    }
    else
    {
        SsSeqPlay_4FD900(rec.field_A_id_seqOpenId, 1, 0);
    }

    return 1;
}


EXPORT s32 CC SND_SsIsEos_DeInlined_4CACD0(u16 idx)
{
    OpenSeqHandle* pRec = &GetMidiVars()->sSeqDataTable()[idx];
    if (pRec->field_A_id_seqOpenId != -1 && pRec->field_C_ppSeq_Data)
    {
        return SsIsEos_4FDA80(pRec->field_A_id_seqOpenId, 0) != 0;
    }
    return 0;
}


EXPORT void CC SND_SEQ_SetVol_4CAD20(s32 idx, s16 volLeft, s16 volRight)
{
    u16 limitedIdx = idx & 0xFFFF;
    if (GetMidiVars()->sSeqDataTable()[limitedIdx].field_A_id_seqOpenId != -1
        && GetMidiVars()->sSeqDataTable()[limitedIdx].field_C_ppSeq_Data
        && SND_SsIsEos_DeInlined_4CACD0(limitedIdx))
    {
        SsSeqSetVol_4FDAC0(GetMidiVars()->sSeqDataTable()[limitedIdx].field_A_id_seqOpenId, volLeft, volRight);
    }
}


EXPORT void CC SND_SEQ_Stop_4CAE60(u16 idx)
{
    if (GetMidiVars()->sSeqDataTable()[idx].field_A_id_seqOpenId != -1 && GetMidiVars()->sSeqDataTable()[idx].field_C_ppSeq_Data)
    {
        if (SsIsEos_4FDA80(GetMidiVars()->sSeqDataTable()[idx].field_A_id_seqOpenId, 0))
        {
            SsSeqStop_4FD9C0(GetMidiVars()->sSeqDataTable()[idx].field_A_id_seqOpenId);
        }
    }
}

EXPORT void CC SND_Load_Seqs_Impl(OpenSeqHandle* pSeqTable, const char_type* bsqFileName)
{
    if (pSeqTable && bsqFileName)
    {
        GetMidiVars()->sSeqDataTable() = pSeqTable;

        // Generate resource ids from hashing the name if we haven't already
        if (GetMidiVars()->sNeedToHashSeqNames())
        {
            for (s32 i = 0; i < GetMidiVars()->MidiTableSize(); i++)
            {
                GetMidiVars()->sSeqDataTable()[i].field_C_ppSeq_Data = nullptr;
                GetMidiVars()->sSeqDataTable()[i].field_A_id_seqOpenId = -1;
                GetMidiVars()->sSeqDataTable()[i].field_4_generated_res_id = ResourceManager::SEQ_HashName_49BE30(GetMidiVars()->sSeqDataTable()[i].field_0_mBsqName);
            }
            GetMidiVars()->sNeedToHashSeqNames() = FALSE;
        }

        // Load the BSQ
        GetMidiVars()->Reclaim_Memory(0);
        GetMidiVars()->LoadResourceFile(bsqFileName, nullptr);

        // Get a pointer to each SEQ
        for (s32 i = 0; i < GetMidiVars()->MidiTableSize(); i++)
        {
            u8** ppSeq = GetMidiVars()->GetLoadedResource(ResourceManager::Resource_Seq, GetMidiVars()->sSeqDataTable()[i].field_4_generated_res_id, 1, 1);
            if (ppSeq)
            {
                GetMidiVars()->sSeqDataTable()[i].field_C_ppSeq_Data = *ppSeq;
            }
            else
            {
                GetMidiVars()->sSeqDataTable()[i].field_C_ppSeq_Data = nullptr;
            }
        }
    }
}

EXPORT void CC SND_Load_Seqs_4CAED0(OpenSeqHandle* pSeqTable, const char_type* bsqFileName)
{
    SND_Load_Seqs_Impl(pSeqTable, bsqFileName);
}

EXPORT s8 CC SND_Seq_Table_Valid_4CAFE0()
{
    return GetMidiVars()->sSeqDataTable() != 0;
}

// So AO can redirect SND_StopAll_4CB060 to its own func when called from SYS_ funcs
EXPORT void SND_StopAll_SetCallBack(TSNDStopAll cb)
{
    sSNDStopAllCallBack = cb;
}

EXPORT void CC SND_StopAll_4CB060()
{
    if (sSNDStopAllCallBack)
    {
        sSNDStopAllCallBack();
    }
    else
    {
        MusicController::EnableMusic_47FE10(FALSE);
        BackgroundMusic::Stop_4CB000();
        SND_Reset_Ambiance_4CB4B0();
        SND_Stop_All_Seqs_4CA850();
        for (s32 i = 0; i < gBaseGameObject_list_BB47C4->Size(); i++)
        {
            BaseGameObject* pObj = gBaseGameObject_list_BB47C4->ItemAt(i);
            if (!pObj)
            {
                break;
            }

            if (!pObj->field_6_flags.Get(BaseGameObject::eDead_Bit3))
            {
                pObj->VStopAudio();
            }
        }
        SsUtAllKeyOff_4FDFE0(0);
    }
}


// So AO can redirect SND_Restart_4CB0E0 to its own func when called from SYS_ funcs
EXPORT void SND_Restart_SetCallBack(TSNDRestart cb)
{
    sSNDRestartCallBack = cb;
}

EXPORT void CC SND_Restart_4CB0E0()
{
    if (sSNDRestartCallBack)
    {
        sSNDRestartCallBack();
    }
    else
    {
        MusicController::EnableMusic_47FE10(TRUE);
        BackgroundMusic::Play_4CB030();
        Start_Sounds_For_Objects_In_Near_Cameras_4CBB60();
    }
}

// Next -> Background music object

// Next -> Scoped seq object

// Next -> SND_Init_Ambiance_4CB480, SND_Reset_Ambiance_4CB4B0, Start_Sounds_for_TLV_4CB530, Start_Slig_sounds_4CB980
// Stop_slig_sounds_4CBA70 Path::Start_Sounds_For_Objects_In_Camera_4CBAF0, Start_Sounds_For_Objects_In_Near_Cameras_4CBB60
