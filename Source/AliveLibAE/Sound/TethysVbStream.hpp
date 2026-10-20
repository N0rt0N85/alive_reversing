#pragma once

// SATURN (Exoddus, stream R4, decision D-So4): THE BANK STREAMER'S INTERFACE.
// One declaration for both sides of the seam: the walker (src/snd_vb_stream_ae.cxx,
// built inside src/sound_saturn.cxx) and the Exoddus glue that feeds it a level's
// .VB record and turns its records into samples (AliveLibAE/Sound/Midi.cpp).
// C++ linkage, so a signature that drifts fails the link.
//
// The walk: AO's 449.ao.6 window reader without the game. The .VB contract
// (tools/ae/ae_bank.py): num_vags records {s32 BE length, s32 BE +-base rate,
// s8 pcm[length]}, length a multiple of 4 (<= 65,536), a 0-length record has no
// payload, the rate's sign is the loop flag.

#include "../../AliveLibCommon/Types.hpp"

// Where the bytes come from: `sectors` whole 2,048-byte sectors of the record,
// starting at its sector `first`, into pDst. true = all of them arrived.
struct TethysVbSource final
{
    bool (*readSectors)(void* ctx, s32 first, s32 sectors, u8* pDst);
    void* ctx;
    s32 fileBytes; // the record's whole sectors (num_sectors << 11)
};

// What the records become. onRecord sees every record header in order (a
// 0-length one too) and answers whether it wants that record's payload, which
// then arrives in order through onChunk as [offset, offset + len) pieces, each
// offset and len a multiple of 4. A false from onChunk stops the walk.
struct TethysVbSink final
{
    bool (*onRecord)(void* ctx, s32 index, s32 lengthBytes, s32 rateRaw);
    bool (*onChunk)(void* ctx, s32 index, const u8* p, s32 offset, s32 len);
    void* ctx;
};

// Walks `count` records through the window pWin (winBytes, floored to whole
// sectors, at least one). true = every record walked and every wanted payload
// delivered; false = a read failed, the bank left its contract, or the sink
// refused a chunk (what was delivered before stays delivered).
bool Tethys_VbStreamWalk(const TethysVbSource& src, const TethysVbSink& sink, s32 count, u8* pWin, s32 winBytes);
