#include "stdafx.h"
#include "PathDataExtensions.hpp"
#include "LCDScreen.hpp"
#include "../relive_lib/PathDataExtensionsTypes.hpp"
#include "../relive_lib/MapWrapper.hpp"

struct MudCounts final
{
    s32 mTotal = 300;
    s32 mBadEnding = 255;
    s32 mGoodEnding = 150;
};

// SATURN: this table is 17 x 99 records of {300, 255, 150} and NOTHING in the
// program ever writes one -- the per-path overrides come from the JSON path
// data, which this port does not load.  So all 1683 entries hold the member
// initialisers, byte for byte, and the array costs 20,196 bytes of .rodata to
// answer a question three constants already answer.
//
// Kept as a struct rather than three literals so that restoring the table is a
// one-line revert if per-path mud counts ever start being loaded.
#if TETHYS_SATURN
static constexpr MudCounts kMudDefaults{};
    #define TETHYS_MUD(lvlId, pathNum) kMudDefaults
#else
static MudCounts sMudExtData[static_cast<u32>(LevelIds::eCredits_16) + 1][99];
    #define TETHYS_MUD(lvlId, pathNum) sMudExtData[static_cast<u32>(MapWrapper::ToAE(lvlId))][pathNum]
#endif

s32 Path_GetTotalMuds(EReliveLevelIds lvlId, u32 pathNum)
{
    return TETHYS_MUD(lvlId, pathNum).mTotal;
}

s32 Path_BadEndingMuds(EReliveLevelIds lvlId, u32 pathNum)
{
    return TETHYS_MUD(lvlId, pathNum).mBadEnding;
}

s32 Path_GoodEndingMuds(EReliveLevelIds lvlId, u32 pathNum)
{
    return TETHYS_MUD(lvlId, pathNum).mGoodEnding;
}

//static u8* sPathExtData[static_cast<u32>(LevelIds::eCredits_16) + 1] = {};

template <typename T>
static void SetAndLog(const char_type* propertyName, T& dst, T newVal)
{
    if (dst != newVal)
    {
        LOG_INFO("Update %s from %d to %d", propertyName, static_cast<s32>(dst), static_cast<s32>(newVal));
        dst = newVal;
    }
}

