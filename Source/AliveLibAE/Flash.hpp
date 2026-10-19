#pragma once

#include "../AliveLibCommon/FunctionFwd.hpp"
#include "EffectBase.hpp"
#include "Layer.hpp"

class Flash final : public EffectBase
{
public:
    EXPORT Flash* ctor_428570(Layer layer, u8 r, u8 g, u8 b, s32 /*not_used*/, TPageAbr abr, s32 time);

    virtual BaseGameObject* VDestructor(s32 flags) override;
    virtual void VUpdate() override;

private:
    EXPORT Flash* vdtor_4285F0(s32 flags);
    EXPORT void vUpdate_428640();

private:
    s32 field_78_flash_time;
};
ALIVE_ASSERT_SIZEOF(Flash, 0x7C);

#ifdef TETHYS_SATURN
#include "TethysObjectHooks.hpp"
// SATURN (U1d T1): every ae_new<Flash> site null-tests its result: soft OOM.
TETHYS_AE_SOFT_OOM_TYPE(Flash);
#endif
