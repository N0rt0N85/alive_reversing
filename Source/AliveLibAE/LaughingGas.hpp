#pragma once

#include "../AliveLibCommon/FunctionFwd.hpp"
#include "BaseGameObject.hpp"
#include "Path.hpp"
#include "Primitives.hpp"

#if defined(TETHYS_SATURN) && defined(TETHYS_AE_GAS)
// SATURN: the look of the gas is src/gas_ae.cxx's (AE port decision D-G2): the
// zone, white or laughing and the amount at the ctor (with the 36 phase draws of
// Init_432980), every update's amount in place of sub_4328A0 (with its 16 draws),
// the end at the dtor. C linkage hides the parameter list from the linker, so
// src/gas_ae.cxx includes this header and the compiler checks both sides.
extern "C" void Tethys_GasBegin(s32 x, s32 y, s32 right, s32 bottom, s32 white, s32 amountFp);
extern "C" void Tethys_GasTick(s32 amountFp);
extern "C" void Tethys_GasEnd();
#endif

// TODO: These can be combined
struct Path_LaughingGas_Data final
{
    Choice_short field_0_bLaughing_gas;
    s16 field_2_laughing_gas_switch_id;
    s16 field_4_red_percent;
    s16 field_6_blue_percent;
    s16 field_8_green_percent;
    s16 field_A_padding;
};
ALIVE_ASSERT_SIZEOF_ALWAYS(Path_LaughingGas_Data, 0xC);

struct Path_LaughingGas final : public Path_TLV
{
    Choice_short field_10_bLaughing_gas;
    s16 field_12_laughing_gas_switch_id;
    s16 field_14_red_percent;
    s16 field_16_green_percent;
    s16 field_18_blue_percent;
    s16 field_1A_padding;
};
ALIVE_ASSERT_SIZEOF_ALWAYS(Path_LaughingGas, 0x1C);


class LaughingGas final : public BaseGameObject
{
public:
    EXPORT LaughingGas* ctor_432400(Layer layer, s32 notUsed, Path_LaughingGas* pTlv, s32 tlvInfo);

    EXPORT void dtor_432B80();

    virtual BaseGameObject* VDestructor(s32 flags) override;

    virtual void VScreenChanged() override;

    virtual void VUpdate() override;

    virtual void VRender(PrimHeader** ppOt) override;

private:
    EXPORT void vScreenChanged_432DE0();

    EXPORT void Init_432980();

    EXPORT LaughingGas* vdtor_432670(s32 flags);

    EXPORT void vRender_432D10(PrimHeader** ppOt);

#if !defined(TETHYS_SATURN) // SATURN: the field is src/gas_ae.cxx's, fixed point (D-G2)
    EXPORT void DoRender_432740();
#endif

    EXPORT s16 CounterOver_432DA0();

    EXPORT void vUpdate_432C40();

#if !defined(TETHYS_SATURN) // SATURN: the field is src/gas_ae.cxx's, fixed point (D-G2)
    EXPORT f32 Calc_X_4326A0(f32* a2, s32 xIndex);
    EXPORT f32 Calc_Y_4326F0(f32* a2, s32 yIndex);
#endif

    EXPORT void sub_4328A0();

private:
    s32 field_20_padding;
    s32 field_24_tlvInfo;
    s16 field_28_y;
    s16 field_2A_x;
    s16 field_2C_h;
    s16 field_2E_w;
    s32 field_30_padding;
    s16 field_34_bEnabled;
    Choice_short field_36_bLaughing_gas;
    s32 field_38_padding;
    s32 field_3C_padding;
    s32 field_40_padding;
    s32 field_44_padding;
    Path_LaughingGas_Data field_48_tlv_data;
    FP field_54_amount_on;
    Layer field_58_layer;
    s16 field_5A_padding;

    Prim_GasEffect field_5C_prim;

#if !defined(TETHYS_SATURN)
    // SATURN: out (12,664 B of float tables and the pixel buffer's pointer):
    // the phases, controls and bases live in src/gas_ae.cxx in fixed point, and
    // the field is drawn into VDP2 VRAM instead of a 2 * wc * hc malloc (D-G2).
    f32 field_7C_gas_y[6][6];
    f32 field_10C_gas_x[6][6];

    u16* field_19C_pMem;

    struct Data final
    {
        f32 array_4[7];
    };

    Data field_1A0_x_data[321];  // (640+2)/2
    Data field_24D0_y_data[121]; // (480+4)/4

    s32 field_31F8_w_count;
    s32 field_31FC_h_count;
#endif
};
#if !defined(TETHYS_SATURN)
ALIVE_ASSERT_SIZEOF(LaughingGas, 0x3200);
#endif

ALIVE_VAR_EXTERN(s32, gGasInstanceCount_5BC214);
ALIVE_VAR_EXTERN(s16, gLaughingGasOn_5C1BA4);
