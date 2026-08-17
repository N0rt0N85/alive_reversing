#pragma once

#include <stdint.h>

using u8 = uint8_t;
using s8 = signed char;
using char_type = char;

using u16 = uint16_t;
using s16 = int16_t;

#if TETHYS_SATURN
// SATURN: newlib/SH defines int32_t as `long`, not `int`.  They are the same
// WIDTH but distinct TYPES, and C++ overload resolution and template argument
// deduction both care: `SwapBytes<unsigned>` stops matching a u32 field, an
// `int` argument matches neither `bool` nor `s32`, and a qsort comparator
// returning s32 is not `int(*)(...)`.  Upstream never sees any of it because
// int and int32_t coincide on MSVC/Linux-x86.  This is the SAME edit the AO
// port carries in AliveLibCommon/Types.hpp -- fixing the family at the root
// rather than one symbol at a time.
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

//using Bool32 = long;
