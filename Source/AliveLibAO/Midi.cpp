#include "stdafx_ao.h"

#include "relive_config.h"

#include "Midi.hpp"
#include "Function.hpp"
#include "PathData.hpp"
#include "Abe.hpp"
#include "ResourceManager.hpp"
#include "LvlArchive.hpp"
#include "BackgroundMusic.hpp"
#include "MusicController.hpp"
#include "AmbientSound.hpp"
#include "Sound.hpp"
#include "Sys_common.hpp"
#include "../AliveLibAE/Io.hpp"

#include "Sfx.hpp"
#include "../AliveLibAE/Sfx.hpp"

#include "../AliveLibAE/Sound/PsxSpuApi.hpp"
#include "../AliveLibAE/Sound/Midi.hpp"
#include "../AliveLibAE/Sound/Sound.hpp"
#include "../AliveLibAE/PathData.hpp"


namespace AO {

const s32 kSeqTableSizeAO = 164;

ALIVE_VAR(1, 0x9F12D8, SeqIds, sSeq_Ids_word_9F12D8, {});
ALIVE_VAR(1, 0x9F1DC4, u16, sSnd_ReloadAbeResources_9F1DC4, 0);
ALIVE_VAR(1, 0x9F1DBC, OpenSeqHandle*, sSeqDataTable_9F1DBC, nullptr);
ALIVE_VAR(1, 0x9F1DC0, s16, sSeqsPlaying_count_word_9F1DC0, 0);
ALIVE_VAR(1, 0x9F1DB8, SoundBlockInfo*, sLastLoadedSoundBlockInfo_9F1DB8, nullptr);
ALIVE_VAR(1, 0x4D0018, s16, sSFXPitchVariationEnabled_4D0018, true);
ALIVE_VAR(1, 0x4D0000, s16, sNeedToHashSeqNames_4D0000, 1);

// I think this is the burrrrrrrrrrrrrrrrrrrr loading sound
const SoundBlockInfo soundBlock = {"MONK.VH", "MONK.VB", -1, nullptr};
ALIVE_VAR(1, 0x4D0008, SoundBlockInfo, sMonkVh_Vb_4D0008, soundBlock);

class AOMidiVars final : public IMidiVars
{
public:
    virtual SeqIds& sSeq_Ids_word() override
    {
        return sSeq_Ids_word_9F12D8;
    }

    virtual u16& sSnd_ReloadAbeResources() override
    {
        return sSnd_ReloadAbeResources_9F1DC4;
    }

    virtual OpenSeqHandle*& sSeqDataTable() override
    {
        return sSeqDataTable_9F1DBC;
    }

    virtual s16& sSeqsPlaying_count_word() override
    {
        return sSeqsPlaying_count_word_9F1DC0;
    }

    virtual ::SoundBlockInfo*& sLastLoadedSoundBlockInfo() override
    {
        return reinterpret_cast<::SoundBlockInfo*&>(sLastLoadedSoundBlockInfo_9F1DB8);
    }

    virtual s16& sSFXPitchVariationEnabled() override
    {
        return sSFXPitchVariationEnabled_4D0018;
    }

    virtual s16& sNeedToHashSeqNames() override
    {
        return sNeedToHashSeqNames_4D0000;
    }

    virtual ::SoundBlockInfo& sMonkVh_Vb() override
    {
        return reinterpret_cast<::SoundBlockInfo&>(sMonkVh_Vb_4D0008);
    }

    virtual s32 MidiTableSize() override
    {
        return kSeqTableSizeAO;
    }

    virtual s16 FreeResource_Impl(u8* handle) override
    {
        return ResourceManager::FreeResource_Impl_4555B0(handle);
    }

    virtual u8** GetLoadedResource(u32 type, u32 resourceID, u16 addUseCount, u16 bLock) override
    {
        return ResourceManager::GetLoadedResource_4554F0(type, resourceID, addUseCount, bLock);
    }

    virtual s16 FreeResource(u8** handle) override
    {
        return ResourceManager::FreeResource_455550(handle);
    }

    virtual u8** Allocate_New_Locked_Resource(u32 type, u32 id, u32 size) override
    {
        return ResourceManager::Allocate_New_Locked_Resource_454F80(type, id, size);
    }

    virtual void LoadingLoop(s16 bShowLoadingIcon) override
    {
        ResourceManager::LoadingLoop_41EAD0(bShowLoadingIcon);
    }

    virtual void Reclaim_Memory(u32 size) override
    {
        ResourceManager::Reclaim_Memory_455660(size);
    }

    virtual u8** Alloc_New_Resource(u32 type, u32 id, u32 size) override
    {
        return ResourceManager::Alloc_New_Resource_454F20(type, id, size);
    }

    virtual s16 LoadResourceFile(const char_type* pFileName, ::Camera* pCamera) override
    {
        return ResourceManager::LoadResourceFileWrapper(pFileName, reinterpret_cast<Camera*>(pCamera));
    }
};

static AOMidiVars sAoMidiVars;

ALIVE_VAR(1, 0xA8918E, s16, sGlobalVolumeLevel_right_A8918E, 0);
ALIVE_VAR(1, 0xA8918C, s16, sGlobalVolumeLevel_left_A8918C, 0);
ALIVE_VAR(1, 0xABF8C0, VabUnknown, s512_byte_ABF8C0, {});
ALIVE_ARY(1, 0xA9289C, u8, kMaxVabs, sVagCounts_A9289C, {});
ALIVE_ARY(1, 0xA92898, u8, kMaxVabs, sProgCounts_A92898, {});
ALIVE_ARY(1, 0xABF8A0, VabHeader*, 4, spVabHeaders_ABF8A0, {});
#ifdef TETHYS_SATURN
// SATURN: ~180 KB of VAG/sound tables live in LWRAM; the platform layer
// (src/main.cxx) allocates them and binds them through Tethys_BindSoundTables()
// before Init_Sound_DynamicArrays_And_Others_41CD20 runs.
static ConvertedVagTable* gpConvertedVagTable_Saturn = nullptr;
static SoundEntryTable* gpSoundEntryTable16_Saturn = nullptr;

void Tethys_BindSoundTables(void* pVagTable, void* pSoundEntryTable)
{
    gpConvertedVagTable_Saturn = static_cast<ConvertedVagTable*>(pVagTable);
    gpSoundEntryTable16_Saturn = static_cast<SoundEntryTable*>(pSoundEntryTable);
}

void Tethys_SoundTablesSizes(u32* pVagTableSize, u32* pSoundEntryTableSize)
{
    *pVagTableSize = sizeof(ConvertedVagTable);
    *pSoundEntryTableSize = sizeof(SoundEntryTable);
}
#else
ALIVE_VAR(1, 0xA9B8A0, ConvertedVagTable, sConvertedVagTable_A9B8A0, {});
ALIVE_VAR(1, 0xA928A0, SoundEntryTable, sSoundEntryTable16_A928A0, {});
#endif
ALIVE_VAR(1, 0xAC07C0, MidiChannels, sMidi_Channels_AC07C0, {});
ALIVE_VAR(1, 0xABFB40, MidiSeqSongsTable, sMidiSeqSongs_ABFB40, {});
ALIVE_VAR(1, 0xA89198, s32, sMidi_Inited_dword_A89198, 0);
ALIVE_VAR(1, 0xA89194, u32, sMidiTime_A89194, 0);
ALIVE_VAR(1, 0xA89190, s8, sbDisableSeqs_A89190, 0);
ALIVE_VAR(1, 0x4E8FD8, u32, sLastTime_4E8FD8, 0xFFFFFFFF);
ALIVE_VAR(1, 0xA8919C, u8, sControllerValue_A8919C, 0);

EXPORT s32 CC MIDI_ParseMidiMessage_49DD30(s32 idx);
EXPORT void CC SsUtKeyOffV_49EE50(s16 idx);

class AOPsxSpuApiVars final : public IPsxSpuApiVars
{
public:
    virtual s16& sGlobalVolumeLevel_right() override
    {
        return sGlobalVolumeLevel_right_A8918E;
    }

    virtual s16& sGlobalVolumeLevel_left() override
    {
        return sGlobalVolumeLevel_left_A8918C;
    }

    virtual VabUnknown& s512_byte() override
    {
        return s512_byte_ABF8C0;
    }

    virtual u8* sVagCounts() override
    {
        return sVagCounts_A9289C;
    }

    virtual u8* sProgCounts() override
    {
        return sProgCounts_A92898;
    }

    virtual VabHeader** spVabHeaders() override
    {
        return spVabHeaders_ABF8A0;
    }

    virtual ConvertedVagTable& sConvertedVagTable() override
    {
#ifdef TETHYS_SATURN
        return *gpConvertedVagTable_Saturn; // SATURN: LWRAM, bound by Tethys_BindSoundTables
#else
        return sConvertedVagTable_A9B8A0;
#endif
    }

    virtual SoundEntryTable& sSoundEntryTable16() override
    {
#ifdef TETHYS_SATURN
        return *gpSoundEntryTable16_Saturn; // SATURN: LWRAM, bound by Tethys_BindSoundTables
#else
        return sSoundEntryTable16_A928A0;
#endif
    }

    virtual MidiChannels& sMidi_Channels() override
    {
        return sMidi_Channels_AC07C0;
    }

    virtual MIDI_SeqSong& sMidiSeqSongs(s32 idx) override
    {
        if (idx < 0 || idx >= 32)
        {
            ALIVE_FATAL("sMidiSeqSongs out of bounds");
        }
        return sMidiSeqSongs_ABFB40.table[idx];
    }

    virtual s32& sMidi_Inited_dword() override
    {
        return sMidi_Inited_dword_A89198;
    }

    virtual u32& sMidiTime() override
    {
        return sMidiTime_A89194;
    }

    virtual Bool32& sSoundDatIsNull() override
    {
        return mSoundDatIsNull;
    }

    virtual s8& sbDisableSeqs() override
    {
        return sbDisableSeqs_A89190;
    }

    virtual u32& sLastTime() override
    {
        return sLastTime_4E8FD8;
    }

    virtual u32& sMidi_WaitUntil() override
    {
        // Always 0 in AO cause it dont exist
        return mMidi_WaitUntil;
    }

    virtual IO_FileHandleType& sSoundDatFileHandle() override
    {
        // Should never be called
#if TETHYS_SATURN // SATURN: -fno-exceptions; unreachable stub aborts instead
        abort();
#else
        throw std::logic_error("The method or operation is not implemented.");
#endif
    }

    virtual u8& sControllerValue() override
    {
        return sControllerValue_A8919C;
    }

    virtual void MIDI_ParseMidiMessage(s32 idx) override
    {
        MIDI_ParseMidiMessage_49DD30(idx);
    }

    virtual void SsUtKeyOffV(s32 idx) override
    {
        SsUtKeyOffV_49EE50(static_cast<s16>(idx));
    }

private:
    Bool32 mSoundDatIsNull = FALSE; // Pretend we have sounds dat opened so AE funcs work
    u32 mMidi_WaitUntil = 0;
};

static AOPsxSpuApiVars sAoSpuVars;

EXPORT void CC SND_Reset_476BA0()
{
    SND_Reset_4C9FB0();
}

EXPORT void CC SsUtAllKeyOff_49EDE0(s32 mode)
{
    SsUtAllKeyOff_4FDFE0(mode);
}

EXPORT void CC SND_Stop_All_Seqs_4774D0()
{
    SND_Stop_All_Seqs_4CA850();
}

EXPORT void CC SsSeqCalledTbyT_49E9F0()
{
    SsSeqCalledTbyT_4FDC80();
}

EXPORT s32 CC SND_New_492790(SoundEntry* pSnd, s32 sampleLength, s32 sampleRate, s32 bitsPerSample, s32 isStereo)
{
    return SND_New_4EEFF0(pSnd, sampleLength, sampleRate, bitsPerSample, isStereo);
}

EXPORT s32 CC SND_Load_492F40(SoundEntry* pSnd, const void* pWaveData, s32 waveDataLen)
{
    return SND_Load_4EF680(pSnd, pWaveData, waveDataLen);
}

EXPORT s16 CC SsVabOpenHead_49CFB0(VabHeader* pVabHeader)
{
    return SsVabOpenHead_4FC620(pVabHeader);
}

EXPORT void CC SND_Stop_Channels_Mask_4774A0(s32 mask)
{
    SND_Stop_Channels_Mask_4CA810(mask);
}

EXPORT s16 CC SND_SEQ_PlaySeq_4775A0(SeqId idx, s32 repeatCount, s16 bDontStop)
{
    return SND_SEQ_PlaySeq_4CA960(static_cast<u16>(idx), static_cast<s16>(repeatCount), bDontStop);
}

EXPORT void CC SND_Seq_Stop_477A60(SeqId idx)
{
    SND_SEQ_Stop_4CAE60(static_cast<u16>(idx));
}
EXPORT s16 CC SND_SsIsEos_DeInlined_477930(SeqId idx)
{
    return static_cast<s16>(SND_SsIsEos_DeInlined_4CACD0(static_cast<u16>(idx)));
}

EXPORT s32 CC SND_PlayEx_493040(const SoundEntry* pSnd, s32 panLeft, s32 panRight, f32 freq, MIDI_Channel* pMidiStru, s32 playFlags, s32 priority)
{
    return SND_PlayEx_4EF740(pSnd, panLeft, panRight, freq, pMidiStru, playFlags, priority);
}

EXPORT s32 CC SND_Get_Buffer_Status_491D40(s32 idx)
{
    return SND_Get_Buffer_Status_4EE8F0(idx);
}

// TODO: Check correct one
EXPORT s32 CC SND_Buffer_Set_Frequency_493820(s32 idx, f32 freq)
{
    return SND_Buffer_Set_Frequency_4EFC00(idx, freq);
}

// TODO: Check is 2nd one
EXPORT s32 CC SND_Buffer_Set_Frequency_493790(s32 idx, f32 freq)
{
    return SND_Buffer_Set_Frequency_4EFC00(idx, freq);
}

EXPORT void CC SsSeqStop_49E6E0(s16 idx)
{
    SsSeqStop_4FD9C0(idx);
}

EXPORT void CC MIDI_SetTempo_49E8F0(s16 idx, s16 kZero, s16 tempo)
{
    MIDI_SetTempo_4FDB80(idx, kZero, tempo);
}

EXPORT s32 CC MIDI_Allocate_Channel_49D660(s32 not_used, s32 priority)
{
    return MIDI_Allocate_Channel_4FCA50(not_used, priority);
}

// NOTE: Impl is not the same as AE
EXPORT s32 CC MIDI_PlayerPlayMidiNote_49D730(s32 vabId, s32 program, s32 note, s32 leftVolume, s32 rightVolume, s32 volume)
{
    auto vabId_ = vabId;
    auto leftVolume_ = leftVolume;
    auto v7 = ((program | (vabId << 8)) >> 8) & 0x1F;
    auto noteKeyNumber = (note >> 8) & 0x7F;
    //auto v9 = 0;
    auto v32 = rightVolume;
    auto usedChannelBits = 0;

    if (GetSpuApiVars()->sVagCounts()[v7])
    {
        for (s32 i = 0; i < 24; i++)
        {
            auto pAdsr = &GetSpuApiVars()->sMidi_Channels().channels[i].field_1C_adsr;
            if (!pAdsr->field_3_state
                || pAdsr->field_0_seq_idx != v7
                || pAdsr->field_1_program != ((program | (vabId << 8)) & 0x7F)
                || pAdsr->field_2_note_byte1 != noteKeyNumber)
            {
                // No match
            }
            else
            {
                SsUtKeyOffV_49EE50(static_cast<s16>(i));
                break;
            }
        }
    }

    auto bLeftVolLessThanZero = leftVolume < 0;
    if (!leftVolume)
    {
        if (!rightVolume)
        {
            return 0;
        }
        bLeftVolLessThanZero = 0;
    }

    if (bLeftVolLessThanZero || rightVolume < 0)
    {
        return 0;
    }

    auto volume_ = volume;
    if (!volume)
    {
        return 0;
    }

    if (!GetSpuApiVars()->sVagCounts()[vabId_])
    {
        return 0;
    }

    auto k16Counter = 16;
    auto pVagOff = &GetSpuApiVars()->sConvertedVagTable().table[0][program + (vabId_ << 7)][0];
    while (1)
    {
        if (!pVagOff->field_D_vol || pVagOff->field_8_min > noteKeyNumber || pVagOff->field_9_max < noteKeyNumber)
        {
        }
        else
        {
            auto vag_vol = pVagOff->field_D_vol;
            auto vag_num = pVagOff->field_10_vag;
            auto panLeft = vag_vol * (u16) GetSpuApiVars()->sGlobalVolumeLevel_left() * volume_ * leftVolume_ >> 21;
            auto panRight = vag_vol * (u16) GetSpuApiVars()->sGlobalVolumeLevel_right() * volume_ * v32 >> 21;
            auto bPanLeftLessThanZero = panLeft < 0;
            auto playFlags = ((u32) pVagOff->field_C >> 2) & 1;

            if (panLeft || panRight)
            {
                if (!panLeft)
                {
                    bPanLeftLessThanZero = 0;
                }

                if (!bPanLeftLessThanZero && panRight >= 0)
                {
                    if (((u32) pVagOff->field_C >> 2) & 1)
                    {
                        if (panLeft > 90)
                        {
                            panLeft = 90;
                        }
                        if (panRight > 90)
                        {
                            panRight = 90;
                        }
                    }
                    auto maxPan = panRight;
                    if (panLeft >= panRight)
                    {
                        maxPan = panLeft;
                    }

                    auto midiChannel = MIDI_Allocate_Channel_49D660(maxPan, pVagOff->field_E_priority);
                    auto midiChannel_ = midiChannel;
                    if (midiChannel >= 0)
                    {
                        auto pChannel = &GetSpuApiVars()->sMidi_Channels().channels[midiChannel];
                        auto bUnknown = playFlags
                                     && (pVagOff->field_0_adsr_attack
                                         || pVagOff->field_2_adsr_sustain_level
                                         || pVagOff->field_4_adsr_decay != 16
                                         || pVagOff->field_6_adsr_release >= 33u);
                        pChannel->field_C_vol = maxPan;
                        if (bUnknown)
                        {
                            auto v23 = pVagOff->field_0_adsr_attack;
                            pChannel->field_1C_adsr.field_3_state = 1;
                            auto v24 = v23 * (127 - volume);
                            auto v25 = pVagOff->field_4_adsr_decay;
                            pChannel->field_1C_adsr.field_4_attack = static_cast<u16>(v24 >> 6);
                            pChannel->field_1C_adsr.field_6_sustain = pVagOff->field_2_adsr_sustain_level;
                            v24 = pVagOff->field_6_adsr_release;
                            pChannel->field_1C_adsr.field_8_decay = v25;
                            pChannel->field_1C_adsr.field_A_release = (u16) v24;
                            if (pChannel->field_1C_adsr.field_4_attack)
                            {
                                panLeft = 2;
                                maxPan = 2;
                                v32 = 2;
                                leftVolume_ = 2;
                                panRight = 2;
                            }
                        }
                        else if (playFlags)
                        {
                            pChannel->field_1C_adsr.field_3_state = -1;
                        }
                        else
                        {
                            pChannel->field_1C_adsr.field_3_state = -2;
                        }
                        auto priority = pVagOff->field_E_priority;
                        pChannel->field_8_left_vol = maxPan;
                        auto priority_ = priority;
                        pChannel->field_4_priority = priority;
                        auto midi_time = GetSpuApiVars()->sMidiTime();
                        pChannel->field_18_rightVol = playFlags;
                        pChannel->field_14_time = midi_time;
                        pChannel->field_1C_adsr.field_0_seq_idx = (u8) vabId;
                        pChannel->field_1C_adsr.field_1_program = (u8) program;
                        auto v29 = pVagOff->field_A_shift_cen;
                        pChannel->field_1C_adsr.field_2_note_byte1 = BYTE1(note) & 0x7F;
#ifdef TETHYS_SATURN
                        // SATURN: exponent was (note-v29)/256 semitones --
                        // x256 = note-v29 directly (AUDIO_VIDEO_PLAN §2.5,
                        // the note-on f64 pow on an FPU-less SH-2)
                        const f32 freq = Tethys_Pow12_Saturn(note - v29);
                        pChannel->field_10_freq = freq;
#else
                        auto freq = pow(1.059463094359, (f64)(note - v29) * 0.00390625);
                        pChannel->field_10_freq = (f32) freq;
#endif
                        SND_PlayEx_493040(
                            &GetSpuApiVars()->sSoundEntryTable16().table[vabId][vag_num],
                            panLeft,
                            panRight,
                            (f32) freq,
                            pChannel,
                            playFlags,
                            priority_);
                        volume_ = volume;
                        usedChannelBits |= 1 << midiChannel_;
                    }
                }
            }
            noteKeyNumber = (note >> 8) & 0x7F;
        }
        ++pVagOff;
        if (!--k16Counter)
        {
            return usedChannelBits;
        }
    }
    return 0;
}

EXPORT s32 CC MIDI_PlayerPlayMidiNote_49DAD0(s32 vabId, s32 program, s32 note, s32 leftVol, s32 rightVol, s32 volume)
{
    if (rightVol >= 64)
    {
        return MIDI_PlayerPlayMidiNote_49D730(vabId, program, note, leftVol * (127 - rightVol) / 64, leftVol, volume);
    }
    else
    {
        return MIDI_PlayerPlayMidiNote_49D730(vabId, program, note, leftVol, leftVol * rightVol / 64, volume);
    }
}


EXPORT s32 CC SND_Stop_Sample_At_Idx_493570(s32 idx)
{
    return SND_Stop_Sample_At_Idx_4EFA90(idx);
}

// NOTE!!! not the same as AE
EXPORT void CC SsUtKeyOffV_49EE50(s16 idx)
{
    const auto adsr_state = GetSpuApiVars()->sMidi_Channels().channels[idx].field_1C_adsr.field_3_state;
    auto pChannel = &GetSpuApiVars()->sMidi_Channels().channels[idx];
    if ((adsr_state <= 0 || adsr_state >= 4) && adsr_state != -1)
    {
        if (adsr_state == 4)
        {
            pChannel->field_1C_adsr.field_3_state = 0;
            SND_Stop_Sample_At_Idx_493570(pChannel->field_0_sound_buffer_field_4);
        }
    }
    else
    {
        pChannel->field_1C_adsr.field_3_state = 4;
        pChannel->field_C_vol = pChannel->field_8_left_vol;
        if (!pChannel->field_1C_adsr.field_A_release)
        {
            pChannel->field_1C_adsr.field_A_release = 125;
        }
        pChannel->field_14_time = GetSpuApiVars()->sMidiTime();
    }
}

enum MidiEvent
{
    NoteOff_80 = 0x80,
    NoteOn_90 = 0x90,
    Aftertouch_A0 = 0xA0,
    ControllerChange_B0 = 0xB0,
    ProgramChange_C0 = 0xC0,
    ChannelPressure_D0 = 0xD0,
    PitchBend_E0 = 0xE0,
    OtherCommands_F0 = 0xF0
};

EXPORT s32 CC MIDI_ParseMidiMessage_49DD30(s32 idx)
{
    MIDI_SeqSong* pCtx = &GetSpuApiVars()->sMidiSeqSongs(idx);
    u8** ppSeqData = &pCtx->field_0_seq_data;
    if (pCtx->field_4_time <= GetSpuApiVars()->sMidiTime())
    {
        while (1)
        {
            const u8 curMidiByte = MIDI_ReadByte_4FD6B0(pCtx);

            struct MidiData final
            {
                u8 status;
                u8 param1;
                u8 param2;

                [[nodiscard]] u8 EventType() const
                {
                    return status & 0xF0;
                }
                [[nodiscard]] u8 Channel() const
                {
                    return status & 0x0F;
                }
            };
            MidiData data = {};

            u8 statusByte = curMidiByte;
            if (curMidiByte < MidiEvent::OtherCommands_F0)
            {
                u8 param1 = 0;
                if (curMidiByte >= MidiEvent::NoteOff_80)
                {
                    param1 = MIDI_ReadByte_4FD6B0(pCtx);
                    pCtx->field_2A_running_status = curMidiByte;
                }
                else
                {
                    if (!pCtx->field_2A_running_status)
                    {
                        return 0;
                    }
                    param1 = curMidiByte;
                    statusByte = pCtx->field_2A_running_status;
                }

                data.status = statusByte;
                data.param1 = param1;
                if (data.EventType() != MidiEvent::ProgramChange_C0 && data.EventType() != MidiEvent::ChannelPressure_D0)
                {
                    data.param2 = MIDI_ReadByte_4FD6B0(pCtx);
                }

                switch (data.EventType())
                {
                    case MidiEvent::NoteOff_80:
                    {
                        // Cant see how the ADSR compare would ever be true, the logic makes no sense
                        const u8 program = pCtx->field_32_progVols[data.Channel()].field_0_program;
                        const s32 programShifted = ((s32) program >> 8);
                        const s32 vab_id = (pCtx->field_seq_idx << 8) | (programShifted & 0x1F);
                        if (GetSpuApiVars()->sVagCounts()[vab_id])
                        {
                            for (s16 i = 0; i < 24; i++)
                            {
                                MIDI_ADSR_State* pAdsr = &GetSpuApiVars()->sMidi_Channels().channels[i].field_1C_adsr;
                                if (!pAdsr->field_3_state
                                    || pAdsr->field_0_seq_idx != (((pCtx->field_seq_idx << 8) | programShifted) & 0x1F)
                                    || pAdsr->field_1_program != (((pCtx->field_seq_idx << 8) | programShifted) & 0x7F)
                                    || pAdsr->field_2_note_byte1 != (data.param1 & 0x7F))
                                {
                                    // not a match
                                }
                                else
                                {
                                    SsUtKeyOffV_49EE50(i);
                                    break;
                                }
                            }
                        }
                        break;
                    }

                    case MidiEvent::NoteOn_90:
                    {
                        MIDI_ProgramVolume* pProgVol = &pCtx->field_32_progVols[data.Channel()];
                        auto r_vol = pProgVol->field_2_right_vol;
                        auto note = data.param1 << 8;
                        auto program = pProgVol->field_0_program;
                        auto l_vol = (s16)((u32)(pProgVol->field_1_left_vol * pCtx->field_C_volume) >> 7);

                        auto freq = data.param2;
                        MIDI_PlayerPlayMidiNote_49DAD0(pCtx->field_seq_idx, program, note, l_vol, r_vol, freq); // Note: inlined
                        break;
                    }

                    case MidiEvent::ControllerChange_B0:
                        switch (data.param1 & 0x7F)
                        {
                            case 6u:
                            case 0x26u:
                                switch (GetSpuApiVars()->sControllerValue())
                                {
                                    case 20:                                         // set loop
                                        pCtx->field_24_loop_start = (u8*) ppSeqData; // ???
                                        pCtx->field_2C_loop_count = data.param2;
                                        break;

                                    case 30: // loop
                                        if (pCtx->field_24_loop_start)
                                        {
                                            if (pCtx->field_2C_loop_count > 0)
                                            {
                                                *ppSeqData = pCtx->field_24_loop_start;
                                                if (pCtx->field_2C_loop_count < 127)
                                                {
                                                    GetSpuApiVars()->sControllerValue() = 0;
                                                    pCtx->field_2C_loop_count--;
                                                }
                                            }
                                        }
                                        break;

                                    case 40:
                                    {
                                        auto pFn = pCtx->field_20_fn_ptr;
                                        if (pFn)
                                        {
                                            //((void(__cdecl*)(s32, _DWORD, _DWORD))pFn)(idx, 0, BYTE2(cmd));
                                            GetSpuApiVars()->sControllerValue() = 0;
                                        }
                                        break;
                                    }

                                    default:
                                        GetSpuApiVars()->sControllerValue() = 0;
                                        break;
                                }
                                break;

                            case 0x63u:
                                GetSpuApiVars()->sControllerValue() = data.param2; // BYTE2(midiEvent);
                                break;

                            default:
                                break;
                        }
                        break;

                    case MidiEvent::ProgramChange_C0:
                        pCtx->field_32_progVols[data.Channel()].field_0_program = data.param1;
                        break;

                    case MidiEvent::PitchBend_E0:
                    {
                        const s32 prog_num = pCtx->field_32_progVols[data.Channel()].field_0_program;

                        // Inlined MIDI_PitchBend
#ifdef TETHYS_SATURN
                        // SATURN: exponent was x/128 semitones (x256 = x*2);
                        // this pow ran ahead of a 24-channel loop (§2.5)
                        const f32 freq_conv = Tethys_Pow12_Saturn((s32) (s16) (((data.param1) - 0x4000) >> 4) * 2);
#else
                        const f32 freq_conv = (f32) pow(1.059463094359, (f64)(s16)(((data.param1) - 0x4000) >> 4) * 0.0078125);
#endif

                        for (s32 i = 0; i < 24; i++)
                        {
                            MIDI_Channel* pChannel = &GetSpuApiVars()->sMidi_Channels().channels[i];
                            if (pChannel->field_1C_adsr.field_1_program == prog_num)
                            {
                                const f32 freq_1 = freq_conv * pChannel->field_10_freq;
                                SND_Buffer_Set_Frequency_493790(i, freq_1);
                            }
                        }
                        break;
                    }

                    default:
                        break;
                }
            }
            else if (curMidiByte == MidiEvent::OtherCommands_F0 || curMidiByte == 0xF7)
            {
                const s32 lenToSkip = MIDI_Read_Var_Len_4FD0D0(pCtx);
                MIDI_SkipBytes_4FD6C0(pCtx, lenToSkip);
            }
            else if (curMidiByte == 0xFF) // Sysex len
            {
                u8 metaEvent = MIDI_ReadByte_4FD6B0(pCtx);
                if (metaEvent == 0x2F) // End of track
                {
                    if (pCtx->field_18_repeatCount)
                    {
                        pCtx->field_18_repeatCount--;

                        if (!pCtx->field_18_repeatCount)
                        {
                            SsSeqStop_49E6E0(static_cast<s16>(idx)); // Note: inlined
                            return 1;
                        }
                    }
                    // Reset to start
                    pCtx->field_0_seq_data = pCtx->field_1C_pSeqData;
                }
                else
                {
                    const s32 tempoChange = MIDI_Read_Var_Len_4FD0D0(pCtx);
                    if (tempoChange)
                    {
                        if (metaEvent == 0x51) // Tempo in microseconds per quarter note (24-bit value)
                        {
                            const s32 tempoByte3 = MIDI_ReadByte_4FD6B0(pCtx) << 16;
                            const s32 tempoByte2 = MIDI_ReadByte_4FD6B0(pCtx) << 8;
                            // TODO: Argument is truncated
                            const s32 fullTempo = tempoByte3 | tempoByte2 | MIDI_ReadByte_4FD6B0(pCtx);
                            MIDI_SetTempo_49E8F0(static_cast<s16>(idx), 0, static_cast<s16>(fullTempo));
                        }
                        else
                        {
                            MIDI_SkipBytes_4FD6C0(pCtx, tempoChange);
                        }
                    }
                }
            }

            const s32 timeStamp = MIDI_Read_Var_Len_4FD0D0(pCtx);
            if (timeStamp)
            {
                pCtx->field_4_time = timeStamp * pCtx->field_14_tempo / 1000 + pCtx->field_4_time;
                if (pCtx->field_4_time > GetSpuApiVars()->sMidiTime())
                {
                    return 1;
                }
            }
        } // Loop end
    }
    return 1;
}

EXPORT void CC SND_Shutdown_476EC0()
{
    SND_Shutdown_4CA280();
}

EXPORT void CC SND_SEQ_SetVol_477970(SeqId idx, s16 volLeft, s16 volRight)
{
    SND_SEQ_SetVol_4CAD20(static_cast<u16>(idx), volLeft, volRight);
}

static VabBodyRecord* IterateVBRecords(VabBodyRecord* ret, s32 i_3)
{
    for (s32 i = 0; i < i_3; i++)
    {
        ret = (VabBodyRecord*) ((s8*) ret
                                + ret->field_0_length_or_duration
                                + 8);
    }
    return ret;
}

// Loads vab body sample data to memory
EXPORT void CC SsVabTransBody_49D3E0(VabBodyRecord* pVabBody, s16 vabId)
{
    if (vabId < 0)
    {
        return;
    }

    VabHeader* pVabHeader = GetSpuApiVars()->spVabHeaders()[vabId];
    const s32 vagCount = GetSpuApiVars()->sVagCounts()[vabId];

    for (s32 i = 0; i < vagCount; i++)
    {
        SoundEntry* pEntry = &GetSpuApiVars()->sSoundEntryTable16().table[vabId][i];

        if (!(i & 7))
        {
            SsSeqCalledTbyT_49E9F0();
        }

        memset(pEntry, 0, sizeof(SoundEntry));

        // SATURN: S9 -- the converted .VB is a uniform PCM8 bank (signed s8,
        // 1 byte/sample, tools/converter/vab.py) and each record's field_4
        // carries the playback BASE RATE +-(44100>>k) instead of being
        // unused (docs/AUDIO_VIDEO_PLAN.md §3.2 pinned contract).  sampleLen
        // is therefore = length in bytes (was PCM16 length/2).
        s32 sampleLen = -1;
        if (pVabHeader && i >= 0)
        {
            sampleLen = IterateVBRecords(pVabBody, i)->field_0_length_or_duration;
        }

        if (sampleLen > 0)
        {
            VabBodyRecord* v10 = nullptr;
            if (pVabHeader && i >= 0)
            {
                v10 = IterateVBRecords(pVabBody, i);
            }

            const u8 unused_field = v10->field_4_unused >= 0 ? 0 : 4;
            for (s32 prog = 0; prog < 128; prog++)
            {
                for (s32 tone = 0; tone < 16; tone++)
                {
                    auto pVag = &GetSpuApiVars()->sConvertedVagTable().table[vabId][prog][tone];
                    if (pVag->field_10_vag == i)
                    {
                        pVag->field_C = unused_field;

                        if (!(unused_field & 4) && !pVag->field_0_adsr_attack && pVag->field_6_adsr_release)
                        {
                            pVag->field_6_adsr_release = 0;
                        }
                    }
                }
            }

            // SATURN: S9 -- rate from the record (magnitude; the sign is the
            // loop flag captured above), depth 8-bit, and DIRECT-FEED the
            // resource memory to the backend upload instead of the original
            // malloc+memcpy round-trip (largest record 87,808 B -- a
            // transient that outgrows the TLSF pools at the memory-tightest
            // phase; the Saturn SND_Load writes sound RAM straight from the
            // source bytes).
            const s32 recRate = v10->field_4_unused >= 0 ? v10->field_4_unused : -v10->field_4_unused;
            if (!SND_New_492790(pEntry, sampleLen, recRate, 8u, 0))
            {
                if (sampleLen)
                {
                    SND_Load_492F40(pEntry, &v10->field_8_fileOffset, sampleLen);
                }
            }
        }
    }
}

EXPORT s16 CC SND_VAB_Load_476CB0(SoundBlockInfo* pSoundBlockInfo, s16 vabId)
{
    // Fail if no file name
    if (!pSoundBlockInfo->field_0_vab_header_name)
    {
        return 0;
    }

    // Find the VH file record
    LvlFileRecord* pVabHeaderFile = sLvlArchive_4FFD60.Find_File_Record_41BED0(pSoundBlockInfo->field_0_vab_header_name);

    // SATURN: a missing VH (e.g. no MONK.VH in a level) derefs null here --
    // the S4 null-handle class (AUDIO_VIDEO_PLAN §3.3); fail soft like the
    // missing-VB path below.
    if (!pVabHeaderFile)
    {
        return 0;
    }

    s32 headerSize = pVabHeaderFile->field_10_num_sectors << 11;

    u8** ppVabHeader = ResourceManager::Allocate_New_Locked_Resource_454F80(ResourceManager::Resource_VabHeader, vabId, headerSize);

    // SATURN: unchecked Allocate_New_Locked null = *null reads the BIOS
    // vector then Read_File writes ~2-4 KB through a wild pointer (same S4
    // class); the alloc can fail exactly at the memory-tightest load phase.
    if (!ppVabHeader)
    {
        return 0;
    }

    pSoundBlockInfo->field_C_pVabHeader = *ppVabHeader;
    sLvlArchive_4FFD60.Read_File_41BE40(pVabHeaderFile, *ppVabHeader);

    // Find the VB file record
    LvlFileRecord* pVabBodyFile = sLvlArchive_4FFD60.Find_File_Record_41BED0(pSoundBlockInfo->field_4_vab_body_name);
    if (!pVabBodyFile)
    {
        // For some reason its acceptable to assume we have a VH with no VB, but the VH must always exist, this happens for MONK.VB
        return 0;
    }

    s32 vabBodySize = pVabBodyFile->field_10_num_sectors << 11;

    // Load the VB file data
    u8** ppVabBody = ResourceManager::Alloc_New_Resource_454F20(ResourceManager::Resource_VabBody, vabId, vabBodySize);
    if (!ppVabBody)
    {
        // Maybe filed due to OOM cause its huge, free the abe resources and try again
        if (!GetMidiVars()->sSnd_ReloadAbeResources())
        {
            GetMidiVars()->sSnd_ReloadAbeResources() = TRUE;
            // SATURN: at boot the VABs load before any Abe exists -- the
            // original derefs null here (crash class documented in
            // tools/converter/vab.py); the Reclaim below still runs.
            if (sActiveHero_507678)
            {
                sActiveHero_507678->Free_Resources_422870();
            }
        }

        // Compact/reclaim any other memory we can too
        ResourceManager::Reclaim_Memory_455660(0);

        // If it fails again there is no recovery, in either case caller will restore abes resources
        ppVabBody = ResourceManager::Alloc_New_Resource_454F20(ResourceManager::Resource_VabBody, vabId, vabBodySize);
        if (!ppVabBody)
        {
            return 0;
        }
    }

    sLvlArchive_4FFD60.Read_File_41BE40(pVabBodyFile, *ppVabBody);
    pSoundBlockInfo->field_8_vab_id = SsVabOpenHead_49CFB0(reinterpret_cast<VabHeader*>(pSoundBlockInfo->field_C_pVabHeader));
    // SATURN: a VH-declared id outside kMaxVabs would make TransBody index
    // the fixed VAG/entry tables out of bounds -- on Saturn those live in
    // the VDP1 VRAM tail, and the overrun lands in the FRAMEBUFFER with no
    // crash (S9 review).  Real data uses ids 0..1; reject anything else.
    if (pSoundBlockInfo->field_8_vab_id < 0 || pSoundBlockInfo->field_8_vab_id >= kMaxVabs)
    {
        ResourceManager::FreeResource_455550(ppVabBody);
        return 0;
    }
    SsVabTransBody_49D3E0(reinterpret_cast<VabBodyRecord*>(*ppVabBody), static_cast<s16>(pSoundBlockInfo->field_8_vab_id));
    SsVabTransCompleted_4FE060(SS_WAIT_COMPLETED);

    // Now the sound samples are loaded we don't need the VB data anymore
    ResourceManager::FreeResource_455550(ppVabBody);
    return 1;
}

EXPORT void CC SND_Load_VABS_477040(SoundBlockInfo* pSoundBlockInfo, s32 reverb)
{
    SoundBlockInfo* pSoundBlockInfoIter = pSoundBlockInfo;
    GetMidiVars()->sSnd_ReloadAbeResources() = FALSE;
    if (GetMidiVars()->sLastLoadedSoundBlockInfo() != reinterpret_cast<::SoundBlockInfo*>(pSoundBlockInfo))
    {
        SsUtReverbOff_4FE350();
        SsUtSetReverbDepth_4FE380(0, 0);
        SpuClearReverbWorkArea_4FA690(4);

        if (GetMidiVars()->sMonkVh_Vb().field_8_vab_id < 0)
        {
            SND_VAB_Load_476CB0(reinterpret_cast<SoundBlockInfo*>(&GetMidiVars()->sMonkVh_Vb()), 32);
        }

        GetMidiVars()->sLastLoadedSoundBlockInfo() = reinterpret_cast<::SoundBlockInfo*>(pSoundBlockInfo);

        s16 vabId = 0;
        while (SND_VAB_Load_476CB0(pSoundBlockInfoIter, vabId))
        {
            ++vabId;
            ++pSoundBlockInfoIter;
        }

        if (GetMidiVars()->sSnd_ReloadAbeResources())
        {
            ResourceManager::Reclaim_Memory_455660(0);
            Abe::Load_Basic_Resources_4228A0();
        }

        SsUtSetReverbDepth_4FE380(reverb, reverb);
        SsUtReverbOn_4FE340();
    }
}

EXPORT void CC SND_Load_Seqs_477AB0(OpenSeqHandleAE* pSeqTable, const char_type* bsqFileName)
{
    SND_Load_Seqs_Impl(
        reinterpret_cast<::OpenSeqHandle*>(pSeqTable),
        bsqFileName);
}

EXPORT s16 CC SND_SEQ_Play_477760(SeqId idx, s32 repeatCount, s16 volLeft, s16 volRight)
{
    const auto ret = SND_SEQ_PlaySeq_4CA960(static_cast<u16>(idx), static_cast<s16>(repeatCount), 1); // TODO ??

    OpenSeqHandle* pOpenSeq = &GetMidiVars()->sSeqDataTable()[static_cast<u16>(idx)];

    // Clamp vol
    s16 clampedVolLeft = volLeft;
    if (clampedVolLeft <= 10)
    {
        clampedVolLeft = 10;
    }
    else
    {
        if (clampedVolLeft >= 127)
        {
            clampedVolLeft = 127;
        }
    }

    s16 clampedVolRight = volRight;
    if (clampedVolRight <= 10)
    {
        clampedVolRight = 10;
    }
    else
    {
        if (clampedVolRight >= 127)
        {
            clampedVolRight = 127;
        }
    }

    // Can happen in the board room as there is no music
    if (pOpenSeq->field_A_id_seqOpenId != -1)
    {
        SsSeqSetVol_4FDAC0(pOpenSeq->field_A_id_seqOpenId, clampedVolLeft, clampedVolRight);
    }
    return ret;
}

static ::SfxDefinition ToAeSfxDef(const SfxDefinition* sfxDef)
{
    ::SfxDefinition aeDef = {};
    aeDef.field_0_block_idx = static_cast<s8>(sfxDef->field_0_block_idx);
    aeDef.field_1_program = static_cast<s8>(sfxDef->field_4_program);
    aeDef.field_2_note = static_cast<s8>(sfxDef->field_8_note);
    aeDef.field_3_default_volume = static_cast<s8>(sfxDef->field_C_default_volume);
    aeDef.field_4_pitch_min = sfxDef->field_E_pitch_min;
    aeDef.field_6_pitch_max = sfxDef->field_10_pitch_max;
    return aeDef;
}

EXPORT s32 CC SFX_SfxDefinition_Play_477330(const SfxDefinition* sfxDef, s16 volLeft, s16 volRight, s16 pitch_min, s16 pitch_max)
{
    const ::SfxDefinition aeDef = ToAeSfxDef(sfxDef);
    return SFX_SfxDefinition_Play_4CA700(&aeDef, volLeft, volRight, pitch_min, pitch_max);
}

EXPORT s32 CC SFX_SfxDefinition_Play_4770F0(const SfxDefinition* sfxDef, s32 vol, s32 pitch_min, s32 pitch_max)
{
    const ::SfxDefinition aeDef = ToAeSfxDef(sfxDef);
    return SFX_SfxDefinition_Play_4CA420(&aeDef, static_cast<s16>(vol), static_cast<s16>(pitch_min), static_cast<s16>(pitch_max));
}

EXPORT void CC SND_Init_Real_476E40()
{
}

EXPORT void CC SND_Init_476E40()
{
    SetSpuApiVars(&sAoSpuVars);
    SetMidiApiVars(&sAoMidiVars);

    GetSoundAPI().SND_Load = SND_Load_492F40;
    GetSoundAPI().SND_PlayEx = SND_PlayEx_493040;
    GetSoundAPI().SND_Get_Buffer_Status = SND_Get_Buffer_Status_491D40;
    GetSoundAPI().SND_Stop_Sample_At_Idx = SND_Stop_Sample_At_Idx_493570;
    GetSoundAPI().SND_Buffer_Set_Frequency2 = SND_Buffer_Set_Frequency_493820;

    // SND_Buffer_Set_Frequency1 SND_Buffer_Set_Frequency_493790

    SND_Init_4CA1F0();
    SND_Restart_SetCallBack(SND_Restart_476340);
    SND_StopAll_SetCallBack(SND_StopAll_4762D0);
}

EXPORT void CC SND_StopAll_4762D0()
{
    MusicController::EnableMusic_443900(0);

    if (sBackgroundMusic_seq_id_4CFFF8 >= 0)
    {
        SND_Seq_Stop_477A60(static_cast<SeqId>(sBackgroundMusic_seq_id_4CFFF8));
    }

    SND_Reset_Ambiance_4765E0();
    SND_Stop_All_Seqs_4774D0();

    for (s32 i = 0; i < gBaseGameObject_list_9F2DF0->Size(); i++)
    {
        BaseGameObject* pObj = gBaseGameObject_list_9F2DF0->ItemAt(i);
        if (!pObj)
        {
            break;
        }

        if (!pObj->field_6_flags.Get(BaseGameObject::eDead_Bit3))
        {
            pObj->VStopAudio();
        }
    }

    SsUtAllKeyOff_49EDE0(0);
}

} // namespace AO
