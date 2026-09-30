#include "stdafx_ao.h"
#include "Function.hpp"
#include "BaseBomb.hpp"
#include "ResourceManager.hpp"
#include "stdlib.hpp"
#include "ParticleBurst.hpp"
#include "ScreenShake.hpp"
#include "Midi.hpp"
#include "Events.hpp"
#include "Flash.hpp"
#include "Particle.hpp"
#include "BaseAliveGameObject.hpp"

// SATURN 427.ao.8 -- ONE NAME, BECAUSE TWO SITES HAVE TO AGREE ON THIS NUMBER.
//
// field_BC_sprite_scale IS NOT A RENDERING FIELD ON A BOMB, and that is the whole
// bug. DealDamageRect_417A50 uses it as the SCALE-PLANE DISCRIMINATOR: it damages
// an object only when
//
//     field_BC_sprite_scale == pObj->field_BC_sprite_scale * 2.75
//
// which is AO's way of writing "the bomb and the target are on the same plane",
// full size or the half size a background path uses. The bomb's own scale is set
// to `scale * 2.75` in the constructor, so the equality reduces to
// `scale == pObj scale` and nothing else.
//
// 427.ao.7 pre-shrank the blast cels to 7/10 (tools/converter/anim.py,
// PRESCALE_CELS) and multiplied the bomb's scale by 10/7 to put the same picture
// back on screen. That is correct for the DRAW and it silently made the equality
// above unsatisfiable, because only ONE of the two sides moved. A mine therefore
// damaged nothing at all, which the tester found in one sentence: "abe ne meurt
// plus". Not just Abe: no slig, no mudokon, nothing in blast range.
//
// I had asked myself this exact question before shipping and answered it wrong.
// I checked that the damage RECT is built from field_E4_scale, which is true, and
// concluded field_BC_sprite_scale was cosmetic -- without reading the per-object
// FILTER twenty lines further down. Half of the damage path is not the path.
//
// The number now has a name and BOTH sites read it, so they cannot drift again.
// The equality stays exact in fixed point: both sides are the same FP multiply
// applied to equal operands, so they are bit-identical whenever the planes match,
// exactly as they were with AO's bare 2.75.
#ifdef TETHYS_SATURN
#define TETHYS_BOMB_SPRITE_SCALE (2.75 * 1000.0 / 827.0)
#else
#define TETHYS_BOMB_SPRITE_SCALE 2.75
#endif

namespace AO {

ALIVE_VAR(1, 0x4FFA4C, s16, word_4FFA4C, 0);

void BaseBomb::VUpdate_417580()
{
    PSX_RECT rect = {};

    Event_Broadcast_417220(kEvent_2, this);
    Event_Broadcast_417220(kEvent_14, this);
    Event_Broadcast_417220(kEventSuspiciousNoise_10, this);

    switch (field_10_anim.field_92_current_frame)
    {
        case 0:
            rect.x = FP_GetExponent(FP_FromInteger(-30) * field_E4_scale);
            rect.w = FP_GetExponent(FP_FromInteger(30) * field_E4_scale);
            rect.y = FP_GetExponent(FP_FromInteger(-20) * field_E4_scale);
            rect.h = FP_GetExponent(FP_FromInteger(20) * field_E4_scale);
            DealDamageRect_417A50(&rect);
            break;

        case 1:
            rect.x = FP_GetExponent(FP_FromInteger(-50) * field_E4_scale);
            rect.w = FP_GetExponent(FP_FromInteger(50) * field_E4_scale);
            rect.y = FP_GetExponent(FP_FromInteger(-30) * field_E4_scale);
            rect.h = FP_GetExponent(FP_FromInteger(30) * field_E4_scale);
            DealDamageRect_417A50(&rect);
            break;

        case 2:
            rect.x = FP_GetExponent(FP_FromInteger(-80) * field_E4_scale);
            rect.w = FP_GetExponent(FP_FromInteger(80) * field_E4_scale);
            rect.y = FP_GetExponent(FP_FromInteger(-40) * field_E4_scale);
            rect.h = FP_GetExponent(FP_FromInteger(40) * field_E4_scale);
            DealDamageRect_417A50(&rect);
            break;

        case 3:
        {
            ParticleBurst* pParticleBurst = ao_new<ParticleBurst>();
            if (pParticleBurst)
            {
                pParticleBurst->ctor_40D0F0(
                    field_A8_xpos,
                    field_AC_ypos,
                    20,
                    field_BC_sprite_scale,
                    BurstType::eBigRedSparks_3);
            }


            Flash* pFlash = ao_new<Flash>();
            if (pFlash)
            {
                pFlash->ctor_41A810(Layer::eLayer_Above_FG1_39, 255u, 255u, 255u);
            }

            rect.x = FP_GetExponent(FP_FromInteger(-113) * field_E4_scale);
            rect.w = FP_GetExponent(FP_FromInteger(113) * field_E4_scale);
            rect.y = FP_GetExponent(FP_FromInteger(-50) * field_E4_scale);
            rect.h = FP_GetExponent(FP_FromInteger(50) * field_E4_scale);
            DealDamageRect_417A50(&rect);
            break;
        }

        case 4:
        {
            Flash* pFlash = ao_new<Flash>();
            if (pFlash)
            {
                pFlash->ctor_41A810(Layer::eLayer_Above_FG1_39, 255u, 255u, 255u, 1, TPageAbr::eBlend_1, 1);
            }
            break;
        }

        case 7:
        {
            ParticleBurst* pParticleBurst = ao_new<ParticleBurst>();
            if (pParticleBurst)
            {
                pParticleBurst->ctor_40D0F0(
                    field_A8_xpos,
                    field_AC_ypos,
                    20,
                    field_BC_sprite_scale,
                    BurstType::eBigRedSparks_3);
            }

            Flash* pFlash = ao_new<Flash>();
            if (pFlash)
            {
                pFlash->ctor_41A810(Layer::eLayer_Above_FG1_39, 255u, 255u, 255u);
            }
            break;
        }

        default:
            break;
    }

    // SATURN 427.ao.7: 3 -> 1, AND IT IS A FIDELITY TRADE THE TESTER ASKED FOR
    // BY NAME ("si c'est trop lourd on les rapproche ?").
    //
    // This second blast is the SAME animation as BaseBomb's own, mirrored, at the
    // same position, started this many frames later; both advance one frame per
    // tick, so the offset is permanent and the two NEVER share a cel. That is
    // what makes the mine the one burst in the game the cel cache cannot serve
    // -- every other effect puts its instances on the same frame in the same
    // tick and dedups for free.
    //   Holding a three-frame gap means holding FOUR consecutive cels: 10,880 B
    // pre-shrunk, plus 3,728 for the rest of the explosion, against a 16,384 B
    // scratch that also has to decode. It does not fit. At one frame it is two
    // cels, 9,168 B in all, 74 % of the enlarged cache -- and the second
    // animation's decode disappears entirely.
    //   WHAT IT COSTS ON SCREEN: the mirrored blast appears two ticks earlier and
    // the whole effect ends two ticks sooner, 33 -> 31 ticks. Revert by putting
    // the 3 back; nothing else depends on the number.
#ifdef TETHYS_SATURN
    if (field_10_anim.field_92_current_frame == 1)
#else
    if (field_10_anim.field_92_current_frame == 3)
#endif
    {
        const AnimRecord& rec = AO::AnimRec(AnimId::Explosion_Mine);
        u8** ppRes = ResourceManager::GetLoadedResource_4554F0(ResourceManager::Resource_Animation, rec.mResourceId, 1, 0);
        if (ppRes)
        {
            Particle* pParticle = ao_new<Particle>();
            if (pParticle)
            {
                pParticle->ctor_478880(
                    field_A8_xpos,
                    field_AC_ypos,
                    rec.mFrameTableOffset,
                    rec.mMaxW,
                    rec.mMaxH,
                    ppRes);
#ifdef TETHYS_SATURN
                // SATURN 427.ao.3: THE FOUR ASSIGNMENTS MOVED INSIDE THE GUARD.
                // Upstream writes them after an `else { pParticle = nullptr; }`,
                // i.e. it dereferences the null it just assigned. On PC that is
                // unreachable because operator new never returns null; here
                // ao_new_malloc_447520 returns a SOFT NULL under heap pressure
                // (the whole point of the refusal policy), and a mine explodes
                // at exactly the moment the heap is fullest. The result would be
                // a write through address ~0x30, which is a wild write, not a
                // fault. Same family as the Animation Init refusal that produced
                // "no more fatal, but the Mudokon is gone": a refused allocation
                // must cost this second sprite, never the machine.
#endif
                pParticle->field_10_anim.field_4_flags.Set(AnimFlags::eBit5_FlipX);
                pParticle->field_CC_bApplyShadows &= ~1u;
                pParticle->field_10_anim.field_B_render_mode = TPageAbr::eBlend_1;
                pParticle->field_BC_sprite_scale = field_BC_sprite_scale * FP_FromDouble(0.7);
            }
        }
    }

    if (field_10_anim.field_4_flags.Get(AnimFlags::eBit12_ForwardLoopCompleted))
    {
        field_6_flags.Set(BaseGameObject::eDead_Bit3);
    }
}

void BaseBomb::VUpdate()
{
    VUpdate_417580();
}

void BaseBomb::DealDamageRect_417A50(const PSX_RECT* pRect)
{
    if (gBaseAliveGameObjects_4FC8A0)
    {
        s16 min_w_x = pRect->w;
        if (pRect->x <= pRect->w)
        {
            min_w_x = pRect->x;
        }

        auto min_x_w = pRect->w;
        if (pRect->w <= pRect->x)
        {
            min_x_w = pRect->x;
        }

        auto min_y_h = pRect->h;
        if (pRect->y <= pRect->h)
        {
            min_y_h = pRect->y;
        }

        s16 min_h_y = pRect->h;
        if (pRect->h <= pRect->y)
        {
            min_h_y = pRect->y;
        }

        auto right = FP_GetExponent(field_A8_xpos) + min_x_w;
        auto left = FP_GetExponent(field_A8_xpos) + min_w_x;
        auto top = FP_GetExponent(field_AC_ypos) + min_y_h;
        auto bottom = FP_GetExponent(field_AC_ypos) + min_h_y;

        if ((abs(left) & 1023) < 256)
        {
            left -= 656;
        }

        if ((abs(right) & 1023) > 624)
        {
            right += 656;
        }

        if (top % 480 < 120)
        {
            top -= 240;
        }

        if (bottom % 480 > 360)
        {
            bottom += 240;
        }

        for (s32 i = 0; i < gBaseAliveGameObjects_4FC8A0->Size(); i++)
        {
            BaseAliveGameObject* pObj = gBaseAliveGameObjects_4FC8A0->ItemAt(i);
            if (!pObj)
            {
                break;
            }

            const s16 obj_xpos = FP_GetExponent(pObj->field_A8_xpos);
            if (obj_xpos >= left && obj_xpos <= right)
            {
                const s16 obj_ypos = FP_GetExponent(pObj->field_AC_ypos);
                // SATURN: the constant is shared with the constructor below.
                if (obj_ypos >= top && obj_ypos <= bottom && field_BC_sprite_scale == (pObj->field_BC_sprite_scale * FP_FromDouble(TETHYS_BOMB_SPRITE_SCALE)))
                {
                    pObj->VTakeDamage(this);
                }
            }
        }
    }
}

BaseBomb* BaseBomb::ctor_4173A0(FP xpos, FP ypos, s32 /*unused*/, FP scale)
{
    ctor_417C10();
    SetVTable(this, 0x4BAA00);
    field_4_typeId = Types::eBaseBomb_30;

    const AnimRecord& rec = AO::AnimRec(AnimId::Explosion_Mine);
    u8** ppRes = ResourceManager::GetLoadedResource_4554F0(ResourceManager::Resource_Animation, rec.mResourceId, 1, 0);
    Animation_Init_417FD0(rec.mFrameTableOffset, rec.mMaxW, rec.mMaxH, ppRes, 1);

    field_10_anim.field_4_flags.Clear(AnimFlags::eBit18_IsLastFrame);

    field_10_anim.field_B_render_mode = TPageAbr::eBlend_1;

    field_10_anim.field_A_b = 128;
    field_10_anim.field_9_g = 128;
    field_10_anim.field_8_r = 128;

    field_E4_scale = scale;

    if (scale == FP_FromInteger(1))
    {
        field_10_anim.field_C_layer = Layer::eLayer_Foreground_36;
    }
    else
    {
        field_10_anim.field_C_layer = Layer::eLayer_Foreground_Half_17;
    }

    field_CC_bApplyShadows &= ~1u;
    // SATURN 427.ao.7: THE BLAST CEL IS PRE-SHRUNK TO 7/10 IN THE PACK
    // (tools/converter/anim.py, PRESCALE_CELS), so this carries the inverse and
    // the explosion is the same size on screen as it has always been.
    //   WHY IT IS ONE LINE AND NOT TWO. The Particle spawned at frame 1 above
    // takes `field_BC_sprite_scale * 0.7` from THIS value, so it inherits the
    // correction; correcting it again would make the second blast 43 % too big.
    //   WHY IT IS SAFE TO MAGNIFY FURTHER. 427.ao.5 restored Animation::VRender's
    // upscale branch in the renderer, which sizes the VDP1 command from the QUAD
    // and stretches the texture into it, pad included, at the measured ratio.
    //   AND WHY IT IS A NAMED CONSTANT: see the header of this file. The damage
    // filter compares against the same number, and 427.ao.7 moved only this side.
    field_BC_sprite_scale = scale * FP_FromDouble(TETHYS_BOMB_SPRITE_SCALE);

    field_A8_xpos = xpos;
    field_AC_ypos = ypos;

    auto pScreenShake = ao_new<ScreenShake>();
    if (pScreenShake)
    {
        pScreenShake->ctor_4624D0(1);
    }

    auto pParticleBurst = ao_new<ParticleBurst>();
    if (pParticleBurst)
    {
        pParticleBurst->ctor_40D0F0(
            field_A8_xpos,
            field_AC_ypos,
            35,
            field_E4_scale,
            BurstType::eFallingRocks_0);
    }

    PSX_RECT damageRect = {
        FP_GetExponent(FP_FromInteger(-10) * field_E4_scale),
        FP_GetExponent(FP_FromInteger(-10) * field_E4_scale),
        FP_GetExponent(FP_FromInteger(10) * field_E4_scale),
        FP_GetExponent(FP_FromInteger(10) * field_E4_scale)};
    DealDamageRect_417A50(&damageRect);

    word_4FFA4C = 0;
    SND_SEQ_PlaySeq_4775A0(SeqId::eExplosion1_21, 1, 1);

    return this;
}

BaseGameObject* BaseBomb::VDestructor(s32 flags)
{
    dtor_417D10();

    if (flags & 1)
    {
        ao_delete_free_447540(this);
    }
    return this;
}

} // namespace AO
