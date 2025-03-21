/*
 * Copyright 2019 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "tools/window/mac/GaneshMetalWindowContext_mac.h"

#include "tools/window/mac/MacWindowInfo.h"
#include "tools/window/mac/MacWindowGLUtils.h"
#include "tools/window/MetalWindowContext.h"
#include "tools/window/mac/MacWindowInfo.h"

#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAConstraintLayoutManager.h>

using skwindow::DisplayParams;
using skwindow::MacWindowInfo;
using skwindow::internal::MetalWindowContext;

namespace {

class MetalWindowContext_mac : public MetalWindowContext {
public:
    MetalWindowContext_mac(const MacWindowInfo&, std::unique_ptr<const DisplayParams>);

    ~MetalWindowContext_mac() override;

    bool onInitializeContext() override;
    void onDestroyContext() override;

    void resize(int w, int h) override;

private:
    NSView* fMainView;
};

MetalWindowContext_mac::MetalWindowContext_mac(const MacWindowInfo& info,
                                               std::unique_ptr<const DisplayParams> params)
        : MetalWindowContext(std::move(params)), fMainView(info.fMainView) {
    // any config code here (particularly for msaa)?

    this->initializeContext();
}

MetalWindowContext_mac::~MetalWindowContext_mac() { this->destroyContext(); }

bool MetalWindowContext_mac::onInitializeContext() {
    SkASSERT(nil != fMainView);

    fMetalLayer = [CAMetalLayer layer];
    fMetalLayer.device = fDevice.get();
    fMetalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;

    // resize ignores the passed values and uses the fMainView directly.
    this->resize(0, 0);

    BOOL useVsync = fDisplayParams->disableVsync() ? NO : YES;
    fMetalLayer.displaySyncEnabled = useVsync;  // TODO: need solution for 10.12 or lower
    fMetalLayer.layoutManager = [CAConstraintLayoutManager layoutManager];
    fMetalLayer.autoresizingMask = kCALayerHeightSizable | kCALayerWidthSizable;
    fMetalLayer.contentsGravity = kCAGravityTopLeft;
    fMetalLayer.magnificationFilter = kCAFilterNearest;
    NSColorSpace* cs = fMainView.window.colorSpace;
    fMetalLayer.colorspace = cs.CGColorSpace;

    fMainView.layer = fMetalLayer;
    fMainView.wantsLayer = YES;

    return true;
}

void MetalWindowContext_mac::onDestroyContext() {}

void MetalWindowContext_mac::resize(int w, int h) {
    CGFloat backingScaleFactor = skwindow::GetBackingScaleFactor(fMainView);
    CGSize backingSize = fMainView.bounds.size;
    backingSize.width *= backingScaleFactor;
    backingSize.height *= backingScaleFactor;

    fMetalLayer.drawableSize = backingSize;
    fMetalLayer.contentsScale = backingScaleFactor;

    fWidth = backingSize.width;
    fHeight = backingSize.height;
}

}  // anonymous namespace

namespace skwindow {

std::unique_ptr<WindowContext> MakeGaneshMetalForMac(const MacWindowInfo& info,
                                                     std::unique_ptr<const DisplayParams> params) {
    std::unique_ptr<WindowContext> ctx(new MetalWindowContext_mac(info, std::move(params)));
    if (!ctx->isValid()) {
        return nullptr;
    }
    return ctx;
}

}  // namespace skwindow
