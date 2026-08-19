#include "CompressionType_4Or5.hpp"

#include "PtrStream.hpp"

// 0xxx xxxx = string of literals (1 to 128)
// 1xxx xxyy yyyy yyyy = copy from y bytes back, x bytes

// SATURN (bt1136) THIS LOOP WAS REWRITTEN AND THE REWRITE WAS REVERTED. The
// record is here rather than in a commit message because the idea is an obvious
// one and someone -- probably me -- will have it again.
//
// WHY IT LOOKED RIGHT. bt1135's brackets showed AnimateAll IS vDecode (the walk
// is 0.6-1.5 %) and that decompression is 28-57 % of it, x3.2 at constant output
// volume. In cart mode the resource heap, and so every
// Resource_DecompressionBuffer, lives in the cartridge -- A-bus, ~4x LWRAM, no
// write burst -- and the SH-2 cache is write-through with no write allocate, so
// every byte store below is its own bus transaction: 3,577,200 of them for R1's
// animations alone. Widening 86.3 % of them (measured: 25.6 % literal runs,
// 60.7 % non-overlapping matches, 13.7 % overlapping, which can never widen)
// into longword stores is a 4:1 cut in A-bus writes.
//
// WHY IT IS NOT HERE. Two measurements, in this order.
//   1. FIELD, and it was a REGRESSION: (dc-cp)/kw went 0.1604 -> 0.2419 on Ymir,
//      +51 % on 4/4 captures, while cp/kw held at 9.73 -> 10.03. The copy is the
//      control and it did not move, so the two builds are comparable in units.
//   2. THE ARITHMETIC, which says it can never be otherwise. GCC compiles THIS
//      loop into 5 instructions per byte -- `mov.b @rS+,rX / dt / mov.b rX,@rD /
//      bf.s / add #1,rD`, with the stream pointer register-resident and
//      post-incrementing. (That also refutes the premise the rewrite was built
//      on: PtrStream's `const u8**` double indirection does NOT defeat the
//      optimiser here.) The widened body is 19 instructions per 4 bytes =
//      4.75/byte, so the whole advantage is 0.25 insn/byte -- against a FIXED
//      per-run cost of ~38 instructions (an alignment head and a tail at 10
//      insn/byte apiece plus ~8 of setup). Break-even is a 152-BYTE RUN. The
//      format caps a match token at 34 bytes and the measured averages are 6.0
//      (literal) and 10.3 (match). Even the longest possible match loses.
// A first shape was worse still -- `while (n && (dst & 3))` makes GCC re-test
// alignment every iteration, SIXTEEN instructions per head byte -- and fixing
// that only moved the break-even, it did not cross it.
//
// WHAT SURVIVES, and where the same problem should be attacked instead: the bus
// cost is real, it is simply not reachable from inside this loop. The lever with
// no instruction cost at all is the BUFFER'S PLACEMENT -- moving
// Resource_DecompressionBuffer out of the cart heap into LWRAM takes the same
// writes from ~4x to ~2.1x on 100 % of them rather than 63 %, and adds not one
// instruction. That is a hardware-slot question (Ymir does not model the bus),
// and it is the right one to spend the slot on. Do not re-derive the widening.
void CompressionType_4Or5_Decompress(const u8* pData, u8* decompressedData)
{
    PtrStream stream(&pData);

    // Get the length of the destination buffer
    u32 nDestinationLength = 0;
    stream.Read(nDestinationLength);

    u32 dstPos = 0;
    while (dstPos < nDestinationLength)
    {
        // get code byte
        const u8 c = stream.ReadU8();

        // 0x80 = 0b10000000 = RLE flag
        // 0xc7 = 0b01111100 = bytes to use for length
        // 0x03 = 0b00000011
        if (c & 0x80)
        {
            // Figure out how many bytes to copy.
            const u32 nCopyLength = ((c & 0x7C) >> 2) + 3;

            // The last 2 bits plus the next byte gives us the destination of the copy
            const u8 c1 = stream.ReadU8();
            const u32 nPosition = ((c & 0x03) << 8) + c1 + 1;
            const u32 startIndex = dstPos - nPosition;

            for (u32 i = 0; i < nCopyLength; i++)
            {
                decompressedData[dstPos++] = decompressedData[startIndex + i];
            }
        }
        else
        {
            // Here the value is the number of literals to copy
            for (s32 i = 0; i < c + 1; i++)
            {
                decompressedData[dstPos++] = stream.ReadU8();
            }
        }
    }
}
