#pragma once

struct FixedPoint;
using FP = FixedPoint;

u32 Math_FixedPoint_Multiply(s32 op1, s32 op2);
u32 Math_FixedPoint_Divide(s32 op1, s32 op2);
s16 Math_RandomRange(s16 min, s16 max);
u8 Math_NextRandom();

FP Math_Cosine(u8 v);
FP Math_Sine(u8 v);
FP Math_Cosine(FP fp);
FP Math_Sine(FP fp);

s32 Math_Distance(s32 x1, s32 y1, s32 x2, s32 y2);

#ifdef TETHYS_SATURN
// SATURN: the UXB blink-pattern code calls pow(10, n) purely to isolate a
// decimal digit.  libm's pow is double math and the SH-2 has no FPU, so this
// exact integer form replaces it: n is a digit index, and 10^9 still fits in
// s32.  Exponents outside 0..9 cannot occur (the pattern is a decimal number).
inline s32 Math_IntPow10(s32 exp)
{
    s32 r = 1;
    for (s32 i = 0; i < exp; i++)
    {
        r *= 10;
    }
    return r;
}
#endif

FP Math_Tan(FP value1, FP value2);
s32 Math_SquareRoot_Int(s32 value);
FP Math_SquareRoot_FP(FP value);


extern u8 gRandomBytes[256];
