/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GFX_TILEDCONTENTHOST_H
#define GFX_TILEDCONTENTHOST_H

#include <stdint.h>                     // for uint16_t
#include <stdio.h>                      // for FILE
#include <algorithm>                    // for swap
#include "ContentHost.h"                // for ContentHost
#include "TiledLayerBuffer.h"           // for TiledLayerBuffer, etc
#include "CompositableHost.h"
#include "mozilla/Assertions.h"         // for MOZ_ASSERT, etc
#include "mozilla/Attributes.h"         // for MOZ_OVERRIDE
#include "mozilla/RefPtr.h"             // for RefPtr
#include "mozilla/gfx/Point.h"          // for Point
#include "mozilla/gfx/Rect.h"           // for Rect
#include "mozilla/gfx/Types.h"          // for Filter
#include "mozilla/layers/CompositorTypes.h"  // for TextureInfo, etc
#include "mozilla/layers/LayersSurfaces.h"  // for SurfaceDescriptor
#include "mozilla/layers/LayersTypes.h"  // for LayerRenderState, etc
#include "mozilla/layers/TextureHost.h"  // for TextureHost
#include "mozilla/layers/TiledContentClient.h"
#include "mozilla/mozalloc.h"           // for operator delete
#include "nsRegion.h"                   // for nsIntRegion
#include "nscore.h"                     // for nsACString

#if defined(MOZ_WIDGET_GONK) && ANDROID_VERSION >= 17
#include <ui/Fence.h>
#endif

class gfxReusableSurfaceWrapper;
struct nsIntPoint;
struct nsIntRect;
struct nsIntSize;

namespace mozilla {
namespace gfx {
class Matrix4x4;
}

namespace layers {

class Compositor;
class ISurfaceAllocator;
class Layer;
class ThebesBufferData;
struct EffectChain;


class TileHost {
public:
  // Constructs a placeholder TileHost. See the comments above
  // TiledLayerBuffer for more information on what this is used for;
  // essentially, this is a sentinel used to represent an invalid or blank
  // tile.
  TileHost()
  {}

  // Constructs a TileHost from a gfxSharedReadLock and TextureHost.
  TileHost(gfxSharedReadLock* aSharedLock,
               TextureHost* aTextureHost,
               TextureHost* aTextureHostOnWhite,
               TextureSource* aSource,
               TextureSource* aSourceOnWhite)
    : mSharedLock(aSharedLock)
    , mTextureHost(aTextureHost)
    , mTextureHostOnWhite(aTextureHostOnWhite)
    , mTextureSource(aSource)
    , mTextureSourceOnWhite(aSourceOnWhite)
  {}

  TileHost(const TileHost& o) {
    mTextureHost = o.mTextureHost;
    mTextureHostOnWhite = o.mTextureHostOnWhite;
    mTextureSource = o.mTextureSource;
    mTextureSourceOnWhite = o.mTextureSourceOnWhite;
    mSharedLock = o.mSharedLock;
  }
  TileHost& operator=(const TileHost& o) {
    if (this == &o) {
      return *this;
    }
    mTextureHost = o.mTextureHost;
    mTextureHostOnWhite = o.mTextureHostOnWhite;
    mTextureSource = o.mTextureSource;
    mTextureSourceOnWhite = o.mTextureSourceOnWhite;
    mSharedLock = o.mSharedLock;
    return *this;
  }

  bool operator== (const TileHost& o) const {
    return mTextureHost == o.mTextureHost;
  }
  bool operator!= (const TileHost& o) const {
    return mTextureHost != o.mTextureHost;
  }

  bool IsPlaceholderTile() const { return mTextureHost == nullptr; }

  void ReadUnlock() {
    if (mSharedLock) {
      mSharedLock->ReadUnlock();
    }
  }

  void DumpTexture(std::stringstream& aStream) {
    // TODO We should combine the OnWhite/OnBlack here an just output a single image.
    CompositableHost::DumpTextureHost(aStream, mTextureHost);
  }

  RefPtr<gfxSharedReadLock> mSharedLock;
  CompositableTextureHostRef mTextureHost;
  CompositableTextureHostRef mTextureHostOnWhite;
  mutable CompositableTextureSourceRef mTextureSource;
  mutable CompositableTextureSourceRef mTextureSourceOnWhite;
};

class TiledLayerBufferComposite
  : public TiledLayerBuffer<TiledLayerBufferComposite, TileHost>
{
  friend class TiledLayerBuffer<TiledLayerBufferComposite, TileHost>;

public:
  typedef TiledLayerBuffer<TiledLayerBufferComposite, TileHost>::Iterator Iterator;

  TiledLayerBufferComposite();
  TiledLayerBufferComposite(ISurfaceAllocator* aAllocator,
                            const SurfaceDescriptorTiles& aDescriptor,
                            const nsIntRegion& aOldPaintedRegion,
                            Compositor* aCompositor);

  TileHost GetPlaceholderTile() const { return TileHost(); }

  // Stores the absolute resolution of the containing frame, calculated
  // by the sum of the resolutions of all parent layers' FrameMetrics.
  const CSSToParentLayerScale2D& GetFrameResolution() { return mFrameResolution; }

  void ReadUnlock();

  void ReleaseTextureHosts();

  /**
   * This will synchronously upload any necessary texture contents, making the
   * sources immediately available for compositing. For texture hosts that
   * don't have an internal buffer, this is unlikely to actually do anything.
   */
  void Upload();

  void SetCompositor(Compositor* aCompositor);

  bool HasDoubleBufferedTiles() { return mHasDoubleBufferedTiles; }

  bool IsValid() const { return mIsValid; }

#if defined(MOZ_WIDGET_GONK) && ANDROID_VERSION >= 17
  virtual void SetReleaseFence(const android::sp<android::Fence>& aReleaseFence);
#endif

  // Recycle callback for TextureHost.
  // Used when TiledContentClient is present in client side.
  static void RecycleCallback(TextureHost* textureHost, void* aClosure);

protected:
  TileHost ValidateTile(TileHost aTile,
                        const nsIntPoint& aTileRect,
                        const nsIntRegion& dirtyRect);

  // do nothing, the desctructor in the texture host takes care of releasing resources
  void ReleaseTile(TileHost aTile) {}

  void SwapTiles(TileHost& aTileA, TileHost& aTileB) { std::swap(aTileA, aTileB); }

  void UnlockTile(TileHost aTile) {}
  void PostValidate(const nsIntRegion& aPaintRegion) {}
private:
  CSSToParentLayerScale2D mFrameResolution;
  bool mHasDoubleBufferedTiles;
  bool mIsValid;
};

/**
 * ContentHost for tiled PaintedLayers. Since tiled layers are special snow
 * flakes, we have a unique update process. All the textures that back the
 * tiles are added in the usual way, but Updated is called on the host side
 * in response to a message that describes the transaction for every tile.
 * Composition happens in the normal way.
 *
 * TiledContentHost has a TiledLayerBufferComposite which keeps hold of the tiles.
 * Each tile has a reference to a texture host. During the layers transaction, we
 * receive a list of descriptors for the client-side tile buffer tiles
 * (UseTiledLayerBuffer). If we receive two transactions before a composition,
 * we immediately unlock and discard the unused buffer.
 *
 * When the content host is composited, we first validate the TiledLayerBuffer
 * (Upload), which calls Updated on each tile's texture host to make sure the
 * texture data has been uploaded. For single-buffered tiles, we unlock at this
 * point, for double-buffered tiles we unlock and discard the last composited
 * buffer after compositing a new one. Rendering takes us to RenderTile which
 * is similar to Composite for non-tiled ContentHosts.
 */
class TiledContentHost : public ContentHost,
                         public TiledLayerComposer
{
public:
  explicit TiledContentHost(const TextureInfo& aTextureInfo);

protected:
  ~TiledContentHost();

public:
  virtual LayerRenderState GetRenderState() MOZ_OVERRIDE
  {
    return LayerRenderState();
  }


  virtual bool UpdateThebes(const ThebesBufferData& aData,
                            const nsIntRegion& aUpdated,
                            const nsIntRegion& aOldValidRegionBack,
                            nsIntRegion* aUpdatedRegionBack) MOZ_OVERRIDE
  {
    NS_ERROR("N/A for tiled layers");
    return false;
  }

  const nsIntRegion& GetValidLowPrecisionRegion() const MOZ_OVERRIDE
  {
    return mLowPrecisionTiledBuffer.GetValidRegion();
  }

  const nsIntRegion& GetValidRegion() const MOZ_OVERRIDE
  {
    return mTiledBuffer.GetValidRegion();
  }

  virtual void SetCompositor(Compositor* aCompositor) MOZ_OVERRIDE
  {
    CompositableHost::SetCompositor(aCompositor);
    mTiledBuffer.SetCompositor(aCompositor);
    mLowPrecisionTiledBuffer.SetCompositor(aCompositor);
    mOldTiledBuffer.SetCompositor(aCompositor);
    mOldLowPrecisionTiledBuffer.SetCompositor(aCompositor);
  }

  virtual bool UseTiledLayerBuffer(ISurfaceAllocator* aAllocator,
                                   const SurfaceDescriptorTiles& aTiledDescriptor) MOZ_OVERRIDE;

  void Composite(EffectChain& aEffectChain,
                 float aOpacity,
                 const gfx::Matrix4x4& aTransform,
                 const gfx::Filter& aFilter,
                 const gfx::Rect& aClipRect,
                 const nsIntRegion* aVisibleRegion = nullptr) MOZ_OVERRIDE;

  virtual CompositableType GetType() MOZ_OVERRIDE { return CompositableType::CONTENT_TILED; }

  virtual TiledLayerComposer* AsTiledLayerComposer() MOZ_OVERRIDE { return this; }

  virtual void Attach(Layer* aLayer,
                      Compositor* aCompositor,
                      AttachFlags aFlags = NO_FLAGS) MOZ_OVERRIDE;

  virtual void Detach(Layer* aLayer = nullptr,
                      AttachFlags aFlags = NO_FLAGS) MOZ_OVERRIDE;

  virtual void Dump(std::stringstream& aStream,
                    const char* aPrefix="",
                    bool aDumpHtml=false) MOZ_OVERRIDE;

  virtual void PrintInfo(std::stringstream& aStream, const char* aPrefix) MOZ_OVERRIDE;

#if defined(MOZ_WIDGET_GONK) && ANDROID_VERSION >= 17
  /**
   * Store a fence that will signal when the current buffer is no longer being read.
   * Similar to android's GLConsumer::setReleaseFence()
   */
  virtual void SetReleaseFence(const android::sp<android::Fence>& aReleaseFence)
  {
    mTiledBuffer.SetReleaseFence(aReleaseFence);
    mLowPrecisionTiledBuffer.SetReleaseFence(aReleaseFence);
  }
#endif

private:

  void RenderLayerBuffer(TiledLayerBufferComposite& aLayerBuffer,
                         const gfxRGBA* aBackgroundColor,
                         EffectChain& aEffectChain,
                         float aOpacity,
                         const gfx::Filter& aFilter,
                         const gfx::Rect& aClipRect,
                         nsIntRegion aMaskRegion,
                         gfx::Matrix4x4 aTransform);

  // Renders a single given tile.
  void RenderTile(const TileHost& aTile,
                  const gfxRGBA* aBackgroundColor,
                  EffectChain& aEffectChain,
                  float aOpacity,
                  const gfx::Matrix4x4& aTransform,
                  const gfx::Filter& aFilter,
                  const gfx::Rect& aClipRect,
                  const nsIntRegion& aScreenRegion,
                  const nsIntPoint& aTextureOffset,
                  const nsIntSize& aTextureBounds);

  void EnsureTileStore() {}

  TiledLayerBufferComposite    mTiledBuffer;
  TiledLayerBufferComposite    mLowPrecisionTiledBuffer;
  TiledLayerBufferComposite    mOldTiledBuffer;
  TiledLayerBufferComposite    mOldLowPrecisionTiledBuffer;
  bool                         mPendingUpload;
  bool                         mPendingLowPrecisionUpload;
};

}
}

#endif
