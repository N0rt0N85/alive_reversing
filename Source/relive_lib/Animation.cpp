#include "stdafx.h"
#include "Animation.hpp"
#include "../AliveLibAE/Compression.hpp"
#include "Compression.hpp"
#include "PsxDisplay.hpp"
#include "Renderer/IRenderer.hpp"
#include "GameType.hpp"
#include "ResourceManagerWrapper.hpp"
#include "AnimResources.hpp"
#include <algorithm>
#include "FatalError.hpp"

const AnimRecord PerGameAnimRec(AnimId id)
{
    if (GetGameType() == GameType::eAe)
    {
        return AnimRec(id);
    }
    else
    {
        return AO::AnimRec(id);
    }
}

const AnimRecord PerGameBgAnimRec(s32 toFindResId)
{
    if (GetGameType() == GameType::eAe)
    {
        return BgAnimRec(toFindResId);
    }
    else
    {
        return AO::BgAnimRec(toFindResId);
    }
}

void Animation::DecompressFrame()
{
    const PerFrameInfo* pFrameInfoHeader = Get_FrameHeader(-1); // -1 = use current frame
    if (pFrameInfoHeader->mPointCount > 0)
    {
        Invoke_CallBacks();
    }

    // TODO
    //UploadTexture(pFrameHeader, vram_rect, width_bpp_adjusted);
}

void Animation::VRender(s32 xpos, s32 ypos, OrderingTable& ot, s16 width, s32 height)
{
    if (!GetRender())
    {
        return;
    }

    const PerFrameInfo* pFrameInfoHeader = Get_FrameHeader(-1);

    s16 x_true     = static_cast<s16>(xpos);
    s16 width_true = static_cast<s16>(width);

    // (AE) Scale X and Width values *before* any maths
    if (GetGameType() == GameType::eAe)
    {
        x_true     = PsxToPCX(x_true);
        width_true = PsxToPCX(width_true);
    }

    FP scaled_width = {};
    FP scaled_height = {};

    if (width_true)
    {
        scaled_width = FP_FromInteger(width_true);
        scaled_height = FP_FromInteger(height);
    }
    else
    {
        if (GetGameType() == GameType::eAo)
        {
            scaled_width = FP_FromInteger(PCToPsxX(pFrameInfoHeader->mWidth, 20));
        }
        else
        {
            scaled_width = FP_FromInteger(pFrameInfoHeader->mWidth);
        }

        scaled_height = FP_FromInteger(pFrameInfoHeader->mHeight);
    }

    FP xOffset_scaled = {};
    FP yOffset_scaled = {};

    if (GetIgnorePosOffset())
    {
        xOffset_scaled = FP_FromInteger(0);
        yOffset_scaled = FP_FromInteger(0);
    }
    else
    {
        xOffset_scaled = FP_FromInteger(pFrameInfoHeader->mXOffset);
        yOffset_scaled = FP_FromInteger(pFrameInfoHeader->mYOffset);
    }

    mPoly.SetSemiTransparent(GetSemiTrans());
    mPoly.SetShadeTex(GetBlending());

    mPoly.SetRGB0(mRgb.r & 0xFF, mRgb.g & 0xFF, mRgb.b & 0xFF);

    // TODO: u/v overflow on big sprites
    const u8 u0 = static_cast<u8>(pFrameInfoHeader->mWidth) - 1;
    const u8 v0 = 0;
    const u8 u1 = 0;
    const u8 v1 = static_cast<u8>(pFrameInfoHeader->mHeight) - 1;

    if (mSpriteScale != FP_FromInteger(1))
    {
        // Apply scale to x/y pos
        scaled_height *= mSpriteScale;
        scaled_width  *= mSpriteScale;

        // (AE) Add 1 if half scale
        if (GetGameType() == GameType::eAe && mSpriteScale == FP_FromDouble(0.5))
        {
            scaled_height += FP_FromDouble(1.0);
            scaled_width  += FP_FromDouble(1.0);
        }

        // Apply scale to x/y offset
        xOffset_scaled = (xOffset_scaled * mSpriteScale);
        yOffset_scaled = (yOffset_scaled * mSpriteScale) - FP_FromInteger(1);
    }

    s16 polyXPos = x_true;
    const bool kFlipX = GetFlipX();

    if (kFlipX)
    {
        if (GetGameType() == GameType::eAo)
        {
            polyXPos -= FP_GetExponent(xOffset_scaled + scaled_width + FP_FromDouble(0.499));
        }
        else
        {
            polyXPos -= FP_GetExponent(xOffset_scaled + FP_FromDouble(0.499));
            polyXPos -= FP_GetExponent(scaled_width + FP_FromDouble(0.499));
        }
    }
    else
    {
        polyXPos += FP_GetExponent(xOffset_scaled + FP_FromDouble(0.499));
    }

    s16 polyYPos = static_cast<s16>(ypos);
    const bool kFlipY = GetFlipY();

    if (kFlipY)
    {
        if (GetGameType() == GameType::eAo)
        {
            polyYPos -= FP_GetExponent(yOffset_scaled + scaled_height + FP_FromDouble(0.499));
        }
        else
        {
            polyYPos -= FP_GetExponent(yOffset_scaled + FP_FromDouble(0.499));
            polyYPos -= FP_GetExponent(scaled_height + FP_FromDouble(0.499));
        }
    }
    else
    {
        polyYPos += FP_GetExponent(yOffset_scaled + FP_FromDouble(0.499));
    }

    mPoly.SetUV0(kFlipX ? u0 : u1, kFlipY ? v1 : v0);
    mPoly.SetUV1(kFlipX ? u1 : u0, kFlipY ? v1 : v0);
    mPoly.SetUV2(kFlipX ? u0 : u1, kFlipY ? v0 : v1);
    mPoly.SetUV3(kFlipX ? u1 : u0, kFlipY ? v0 : v1);

    s16 final_polyX1 = polyXPos;
    s16 final_polyX2 = FP_GetExponent(scaled_width - FP_FromDouble(0.501)) + final_polyX1;
    const s16 final_polyY2 = FP_GetExponent(scaled_height - FP_FromDouble(0.501)) + polyYPos;

    // (AO) Scale X and Width values at the end here
    if (GetGameType() == GameType::eAo)
    {
        final_polyX1 = PsxToPCX(polyXPos);
        final_polyX2 = FP_GetExponent(PsxToPCX(scaled_width) - FP_FromDouble(0.501)) + final_polyX1;
    }

    mPoly.SetXY0(final_polyX1, polyYPos);
    mPoly.SetXY1(final_polyX2, polyYPos);
    mPoly.SetXY2(final_polyX1, final_polyY2);
    mPoly.SetXY3(final_polyX2, final_polyY2);

    mPoly.SetBlendMode(GetBlendMode());

    mPoly.mFlipX = kFlipX;
    mPoly.mFlipY = kFlipY;
    mPoly.mAnim = this;

    ot.Add(GetRenderLayer(), &mPoly);
}

void Animation::VCleanUp()
{
    AnimationBase::gAnimations->Remove_Item(this);
}

void Animation::VDecode()
{
    if (DecodeCommon())
    {
        DecompressFrame();
    }
}

bool Animation::DecodeCommon()
{
    if (mAnimRes.mJsonPtr->mFrames.size() == 1 && GetForwardLoopCompleted())
    {
        return false;
    }

    bool isLastFrame = false;
    if (GetLoopBackwards())
    {
        // Loop backwards
        const s32 prevFrameNum = --mCurrentFrame;
        SetFrameChangeCounter(mFrameDelay);

        if (prevFrameNum < static_cast<s32>(mAnimRes.mJsonPtr->mAttributes.mLoopStartFrame))
        {
            if (GetLoop())
            {
                // Loop to last frame
                mCurrentFrame = static_cast<s32>(mAnimRes.mJsonPtr->mFrames.size()) - 1;
            }
            else
            {
                // Stay on current frame
                SetFrameChangeCounter(0);
                mCurrentFrame = prevFrameNum + 1;
            }

            // For some reason eForwardLoopCompleted isn't set when going backwards
        }

        // Is first (last since running backwards) frame?
        if (mCurrentFrame == 0)
        {
            isLastFrame = true;
        }
    }
    else
    {
        // Loop forwards
        const s32 nextFrameNum = ++mCurrentFrame;
        SetFrameChangeCounter(mFrameDelay);

        // Animation reached end point
        if (nextFrameNum >= static_cast<s32>(mAnimRes.mJsonPtr->mFrames.size()))
        {
            if (GetLoop())
            {
                // Loop back to loop start frame
                mCurrentFrame = mAnimRes.mJsonPtr->mAttributes.mLoopStartFrame;
            }
            else
            {
                // Stay on current frame
                mCurrentFrame = nextFrameNum - 1;
                SetFrameChangeCounter(0);
            }

            SetForwardLoopCompleted(true);
        }

        // Is last frame ?
        if (mCurrentFrame == static_cast<s32>(mAnimRes.mJsonPtr->mFrames.size() - 1))
        {
            isLastFrame = true;
        }
    }

    if (isLastFrame)
    {
        SetIsLastFrame(true);
    }
    else
    {
        SetIsLastFrame(false);
    }

    return true;
}

void Animation::Invoke_CallBacks()
{
    if (!mFnPtrArray)
    {
        return;
    }

    const PerFrameInfo* pFrameHeaderCopy = Get_FrameHeader(-1);
    for (u32 i = 0; i < pFrameHeaderCopy->mPointCount; i++)
    {
        const auto pFnCallBack = mFnPtrArray[pFrameHeaderCopy->mPoints[i].mIndex];
        if (!pFnCallBack)
        {
            break;
        }
        // NOTE: the call back can alter "i"
        pFnCallBack(mGameObj, i, pFrameHeaderCopy->mPoints[i]);
    }
}

s16 Animation::Set_Animation_Data(AnimResource& pAnimRes)
{
    // SATURN: resolve the animation HERE, the moment it is actually played.
    //
    // The engine's contract is that an actor loads its whole motion set in its
    // constructor and GetAnimRes is a pure lookup.  On a PC that is a good
    // trade.  Measured on Saturn: ONE Mudokon's kMudMotionAnimIds is 60
    // animations and 747 KB of texels, against a 732 KB heap -- so a single
    // background character on a neighbouring screen cannot be built at all,
    // and no amount of reclaiming elsewhere changes that.
    //
    // So LoadAnimation returns a DECLARATION (the id, no bytes) and the bytes
    // arrive here and in Init, the only two places an animation is ever
    // consumed.  An actor then costs what it plays instead of what it might
    // play.  Everything downstream is unchanged, including the two sites that
    // bypass GetAnimRes (HoistRocksEffect indexes mLoadedAnims directly,
    // AnimationCallBacks uses a local resource) -- which is exactly why the
    // resolve lives at the choke point and not in the lookup.
    // SATURN: resolve a COPY, never the caller's slot.
    //
    // pAnimRes is the actor's own mLoadedAnims[i] and lives as long as the
    // actor, so resolving it IN PLACE pinned every motion the actor ever played
    // for the actor's whole life -- and PurgeUnusedAnimations' use_count()==1
    // test could never reach any of them.  That is the ratchet behind the
    // tester's "OOM want 15008 free 43108": a heap with plenty left over and no
    // contiguous block in it.  Init (below) has always resolved a copy; this is
    // the one site that did not.
    AnimResource resolved = pAnimRes;
    ResourceManagerWrapper::ResolveAnimation(resolved);

    auto oldPal = mAnimRes.mCurPal;

    mAnimRes = resolved;

    // Keep the custom pal that was set
    if (oldPal)
    {
        mAnimRes.mCurPal = oldPal;
    }

    // Read through mAnimRes, which is already assigned: same value, and one
    // fewer place for a later edit to leave a stale `pAnimRes.` behind.
    mFrameDelay = mAnimRes.mJsonPtr->mAttributes.mFrameRate;

    SetForwardLoopCompleted(false);
    SetIsLastFrame(false);
    SetLoopBackwards(false);
    SetLoop(false);

    if (mAnimRes.mJsonPtr->mAttributes.mLoop)
    {
        SetLoop(true);
    }

    SetFrameChangeCounter(1);
    mCurrentFrame = -1;

    VDecode();

    // Reset to start frame
    SetFrameChangeCounter(1);
    mCurrentFrame = -1;

    return 1;
}

void Animation::Init(const AnimResource& ppAnimData, BaseGameObject* pGameObj)
{
    // TODO extra - init to 0's first - this may be wrong if any bits are explicitly set before this is called
    SetAnimate(false);
    SetRender(false);
    SetFlipX(false);
    SetFlipY(false);
    SetSwapXY(false);
    SetLoop(false);
    SetForwardLoopCompleted(false);
    SetSemiTrans(false);
    SetBlending(false);
    SetIsLastFrame(false);
    SetLoopBackwards(false);
    SetIgnorePosOffset(false);

    mAnimRes = ppAnimData;
    // SATURN: the other consumption point -- see Set_Animation_Data above.
    // Resolving the COPY rather than the source is deliberate: ppAnimData is a
    // const ref, and the resource manager caches, so the caller's declaration
    // costs one map lookup if it is ever played again.  No const_cast needed.
    ResourceManagerWrapper::ResolveAnimation(mAnimRes);
    mFnPtrArray = nullptr;

    mGameObj = pGameObj;

    SetFlipX(false);
    SetFlipY(false);
    SetSwapXY(false);
    SetAnimate(true);
    SetRender(true);

    SetLoop(mAnimRes.mJsonPtr->mAttributes.mLoop);

    SetSemiTrans(false);
    SetBlending(true);

    mFrameDelay = mAnimRes.mJsonPtr->mAttributes.mFrameRate;
    SetFrameChangeCounter(1);
    mCurrentFrame = -1;
    SetBlendMode(relive::TBlendModes::eBlend_0);
    mSpriteScale = FP_FromInteger(1);

    // NOTE: OG bug or odd compiler code gen? Why isn't it using the passed in list which appears to always be this anyway ??
    AnimationBase::gAnimations->Push_Back(this);

    // Get first frame decompressed/into VRAM
    VDecode();

    SetFrameChangeCounter(1);
    SetCurrentFrame(-1);
}

void Animation::SetFrame(s32 newFrame)
{
    if (newFrame == -1)
    {
        newFrame = 0;
    }

    if (newFrame > static_cast<s32>(mAnimRes.mJsonPtr->mFrames.size()))
    {
        newFrame = static_cast<s32>(mAnimRes.mJsonPtr->mFrames.size());
    }

    SetFrameChangeCounter(1);
    mCurrentFrame = newFrame - 1;
}

const PerFrameInfo* Animation::Get_FrameHeader(s32 frame)
{
    if (frame < -1 || frame == -1)
    {
        frame = mCurrentFrame != -1 ? mCurrentFrame : 0;
    }

    // SATURN: two changes, both about being able to READ this failure.
    //
    // The null check is first because without it this is not a bounds error at
    // all: on SH-2 there is no MMU, so mJsonPtr->mFrames.size() through a null
    // pointer quietly reads address 0 and returns whatever lives there, and the
    // comparison below then fires with a message that names the wrong problem.
    // That is exactly how an unresolved animation presented -- as "frame out of
    // bounds" -- and it cost a diagnosis.
    //
    // The bound is also `>=` now, not `>`: at frame == size() the old test
    // passed and the return below indexed one PAST the last frame.
    if (!mAnimRes.mJsonPtr)
    {
        ALIVE_FATAL("anim %d unresolved, frame %d", static_cast<s32>(mAnimRes.mId), frame);
    }

    if (frame >= static_cast<s32>(mAnimRes.mJsonPtr->mFrames.size()))
    {
        ALIVE_FATAL("anim %d frame %d of %d", static_cast<s32>(mAnimRes.mId), frame,
                    static_cast<s32>(mAnimRes.mJsonPtr->mFrames.size()));
    }

    return &mAnimRes.mJsonPtr->mFrames[frame];
}

void Animation::Get_Frame_Rect(PSX_RECT* pRect)
{
    Poly_FT4* pPoly = &mPoly;
    if (!GetIgnorePosOffset())
    {
        Poly_FT4_Get_Rect(pRect, pPoly);
        return;
    }

    const auto min_x0_x1 = std::min(pPoly->X0(), pPoly->X1());
    const auto min_x2_x3 = std::min(pPoly->X2(), pPoly->X3());
    pRect->x = std::min(min_x0_x1, min_x2_x3);

    const auto max_x0_x1 = std::max(pPoly->X0(), pPoly->X1());
    const auto max_x2_x3 = std::max(pPoly->X2(), pPoly->X3());
    pRect->w = std::max(max_x0_x1, max_x2_x3);

    const auto min_y0_y1 = std::min(pPoly->Y0(), pPoly->Y1());
    const auto min_y2_y3 = std::min(pPoly->Y2(), pPoly->Y3());
    pRect->y = std::min(min_y0_y1, min_y2_y3);

    const auto max_y0_y1 = std::max(pPoly->Y0(), pPoly->Y1());
    const auto max_y2_y3 = std::max(pPoly->Y2(), pPoly->Y3());
    pRect->h = std::max(max_y0_y1, max_y2_y3);
}

u32 Animation::Get_Frame_Count()
{
    return static_cast<u32>(mAnimRes.mJsonPtr->mFrames.size());
}

void Animation::LoadPal(std::shared_ptr<AnimationPal>& pal)
{
    // Override the pal with another one
    mAnimRes.mCurPal = pal;
}

void Animation::LoadPal(const PalResource& pal)
{
    // Override the pal with another one
    mAnimRes.mCurPal = pal.mPal;
}

void Animation::ReloadPal()
{
    // Put the original pal back
    mAnimRes.mCurPal = mAnimRes.mPngPtr->mPal;
}

void Animation::Get_Frame_Offset(s16* pBoundingX, s16* pBoundingY)
{
    const PerFrameInfo* pFrameHeader = Get_FrameHeader(-1);
    *pBoundingX = static_cast<s16>(pFrameHeader->mXOffset);
    *pBoundingY = static_cast<s16>(pFrameHeader->mYOffset);
}


void Animation::Get_Frame_Width_Height(s16* pWidth, s16* pHeight)
{
    const PerFrameInfo* pFrameHeader = Get_FrameHeader(-1);
    *pWidth = static_cast<s16>(pFrameHeader->mWidth);
    *pHeight = static_cast<s16>(pFrameHeader->mHeight);
}
