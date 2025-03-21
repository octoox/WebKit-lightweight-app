/*
 * Copyright (C) 2019 Apple Inc. All rights reserved.
 * Copyright (C) 2019, 2024 Igalia S.L.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "ScrollingTreeOverflowScrollProxyNodeCoordinated.h"

#if ENABLE(ASYNC_SCROLLING) && USE(COORDINATED_GRAPHICS)
#include "CoordinatedPlatformLayer.h"
#include "Logging.h"
#include "ScrollingStateOverflowScrollProxyNode.h"
#include "ScrollingStateTree.h"
#include "ScrollingTree.h"

namespace WebCore {

Ref<ScrollingTreeOverflowScrollProxyNodeCoordinated> ScrollingTreeOverflowScrollProxyNodeCoordinated::create(ScrollingTree& scrollingTree, ScrollingNodeID nodeID)
{
    return adoptRef(*new ScrollingTreeOverflowScrollProxyNodeCoordinated(scrollingTree, nodeID));
}

ScrollingTreeOverflowScrollProxyNodeCoordinated::ScrollingTreeOverflowScrollProxyNodeCoordinated(ScrollingTree& scrollingTree, ScrollingNodeID nodeID)
    : ScrollingTreeOverflowScrollProxyNode(scrollingTree, nodeID)
{
}

ScrollingTreeOverflowScrollProxyNodeCoordinated::~ScrollingTreeOverflowScrollProxyNodeCoordinated() = default;

bool ScrollingTreeOverflowScrollProxyNodeCoordinated::commitStateBeforeChildren(const ScrollingStateNode& stateNode)
{
    if (stateNode.hasChangedProperty(ScrollingStateNode::Property::Layer))
        m_layer = static_cast<CoordinatedPlatformLayer*>(stateNode.layer());

    return ScrollingTreeOverflowScrollProxyNode::commitStateBeforeChildren(stateNode);
}

void ScrollingTreeOverflowScrollProxyNodeCoordinated::applyLayerPositions()
{
    FloatPoint scrollOffset = computeLayerPosition();

    LOG_WITH_STREAM(Scrolling, stream << "ScrollingTreeOverflowScrollProxyNodeCoordinated " << scrollingNodeID() << " applyLayerPositions: setting bounds origin to " << scrollOffset);

    m_layer->setBoundsOriginForScrolling(scrollOffset);
}

} // namespace WebCore

#endif // ENABLE(ASYNC_SCROLLING) && USE(COORDINATED_GRAPHICS)
