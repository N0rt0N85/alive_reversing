#include "stdafx.h"
#include "AnimationBase.hpp"
#include "Function.hpp"
#include "stdlib.hpp"
#include "Sys_common.hpp"

void AnimationBase::vDecode_40AC90()
{
    ALIVE_FATAL("Should never be called");
}

void AnimationBase::vRender_40B820(s32 /*xpos*/, s32 /*ypos*/, PrimHeader** /*pOt*/, s16 /*width*/, s32 /*height*/)
{
    ALIVE_FATAL("Should never be called");
    //    return 0;
}

s8 AnimationBase::Animation_v_40BEE0(s16 /*a2*/, s16 /*a3*/, s32 /*a4*/, s16 /*a5*/, s16 /*op1*/)
{
    NOT_IMPLEMENTED();
    LOG_INFO("Animation_v_40BEE0");
    return 0;
}

#ifdef TETHYS_SATURN
// SATURN (bt1128 twin): the animate census the overlay reads (src/sys_saturn.cxx):
// animations walked (the loop's exit index, one add per call) and decodes made.
extern "C" unsigned int Tethys_gAnWalk;
extern "C" unsigned int Tethys_gAnDecode;
#endif

void CC AnimationBase::AnimateAll_40AC20(DynamicArrayT<AnimationBase>* pAnims)
{
    auto i = 0;
    for (; i < pAnims->Size(); i++)
    {
        AnimationBase* pAnim = pAnims->ItemAt(i);
        if (!pAnim)
        {
            break;
        }

        if (!pAnim->field_4_flags.Get(AnimFlags::eBit2_Animate))
        {
            continue;
        }

        if (pAnim->field_E_frame_change_counter > 0)
        {
            pAnim->field_E_frame_change_counter--;
            if (pAnim->field_E_frame_change_counter == 0)
            {
#ifdef TETHYS_SATURN
                Tethys_gAnDecode++;
#endif
                pAnim->vDecode_40AC90();
            }
        }
    }
#ifdef TETHYS_SATURN
    Tethys_gAnWalk += static_cast<unsigned int>(i);
#endif
}
