#pragma once

#include <stdint.h>

using u8 = uint8_t;
using s8 = signed char;
using char_type = char;

using u16 = uint16_t;
using s16 = int16_t;

#if TETHYS_SATURN // SATURN: newlib/SH defines int32_t as long; upstream ABI expects s32==int
using u32 = unsigned int;
using s32 = int;
#else
using u32 = uint32_t;
using s32 = int32_t;
#endif

using f32 = float;
using f64 = double;

using u64 = uint64_t;
using s64 = int64_t;

using Bool32 = long;
