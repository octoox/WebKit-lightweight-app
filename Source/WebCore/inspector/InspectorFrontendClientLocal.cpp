/*
 * Copyright (C) 2010 Google Inc. All rights reserved.
 * Copyright (C) 2015-2024 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "InspectorFrontendClientLocal.h"

#include "Chrome.h"
#include "DOMWrapperWorld.h"
#include "Document.h"
#include "ExceptionDetails.h"
#include "FloatRect.h"
#include "FrameLoadRequest.h"
#include "FrameLoader.h"
#include "InspectorController.h"
#include "InspectorFrontendHost.h"
#include "InspectorPageAgent.h"
#include "LocalFrame.h"
#include "LocalFrameView.h"
#include "Logging.h"
#include "Page.h"
#include "ScriptController.h"
#include "ScriptSourceCode.h"
#include "Settings.h"
#include "Timer.h"
#include "UserGestureIndicator.h"
#include "WindowFeatures.h"
#include <JavaScriptCore/FrameTracers.h>
#include <JavaScriptCore/InspectorBackendDispatchers.h>
#include <JavaScriptCore/JSCJSValueInlines.h>
#include <wtf/Deque.h>
#include <wtf/RunLoop.h>
#include <wtf/TZoneMallocInlines.h>
#include <wtf/text/CString.h>
#include <wtf/text/StringToIntegerConversion.h>

namespace WebCore {

using namespace Inspector;
WTF_MAKE_TZONE_ALLOCATED_IMPL(InspectorFrontendClientLocal);
WTF_MAKE_TZONE_ALLOCATED_IMPL(InspectorFrontendClientLocal::Settings);

static constexpr ASCIILiteral inspectorAttachedHeightSetting = "inspectorAttachedHeight"_s;
static const unsigned defaultAttachedHeight = 300;
static const float minimumAttachedHeight = 250.0f;
static const float maximumAttachedHeightRatio = 0.75f;
static const float minimumAttachedWidth = 500.0f;
static const float minimumAttachedInspectedWidth = 320.0f;

class InspectorBackendDispatchTask : public RefCounted<InspectorBackendDispatchTask> {
    WTF_MAKE_TZONE_ALLOCATED(InspectorBackendDispatchTask);
public:
    static Ref<InspectorBackendDispatchTask> create(InspectorController* inspectedPageController)
    {
        return adoptRef(*new InspectorBackendDispatchTask(inspectedPageController));
    }

    void dispatch(const String& message)
    {
        ASSERT_ARG(message, !message.isEmpty());

        m_messages.append(message);
        scheduleOneShot();
    }

    void reset()
    {
        m_messages.clear();
        m_inspectedPageController = nullptr;
    }

private:
    InspectorBackendDispatchTask(InspectorController* inspectedPageController)
        : m_inspectedPageController(inspectedPageController)
    {
        ASSERT_ARG(inspectedPageController, inspectedPageController);
    }

    void scheduleOneShot()
    {
        if (m_hasScheduledTask)
            return;
        m_hasScheduledTask = true;

        // The frontend can be closed and destroy the owning frontend client before or in the
        // process of dispatching the task, so keep a protector reference here.
        RunLoop::protectedCurrent()->dispatch([this, protectedThis = Ref { *this }] {
            m_hasScheduledTask = false;
            dispatchOneMessage();
        });
    }

    void dispatchOneMessage()
    {
        // Owning frontend client may have been destroyed after the task was scheduled.
        if (!m_inspectedPageController) {
            ASSERT(m_messages.isEmpty());
            return;
        }

        if (!m_messages.isEmpty())
            Ref { *m_inspectedPageController }->dispatchMessageFromFrontend(m_messages.takeFirst());

        if (!m_messages.isEmpty() && m_inspectedPageController)
            scheduleOneShot();
    }

    WeakPtr<InspectorController> m_inspectedPageController;
    Deque<String> m_messages;
    bool m_hasScheduledTask { false };
};

WTF_MAKE_TZONE_ALLOCATED_IMPL(InspectorBackendDispatchTask);

String InspectorFrontendClientLocal::Settings::getProperty(const String&)
{
    return String();
}

void InspectorFrontendClientLocal::Settings::setProperty(const String&, const String&)
{
}

void InspectorFrontendClientLocal::Settings::deleteProperty(const String&)
{
}

InspectorFrontendClientLocal::InspectorFrontendClientLocal(InspectorController* inspectedPageController, Page* frontendPage, std::unique_ptr<Settings> settings)
    : m_inspectedPageController(inspectedPageController)
    , m_frontendPage(frontendPage)
    , m_settings(WTFMove(settings))
    , m_dockSide(DockSide::Undocked)
    , m_dispatchTask(InspectorBackendDispatchTask::create(inspectedPageController))
    , m_frontendAPIDispatcher(InspectorFrontendAPIDispatcher::create(*frontendPage))
{
    m_frontendPage->settings().setAllowFileAccessFromFileURLs(true);
}

InspectorFrontendClientLocal::~InspectorFrontendClientLocal()
{
    if (m_frontendHost)
        m_frontendHost->disconnectClient();
    m_frontendPage = nullptr;
    m_inspectedPageController = nullptr;
    m_dispatchTask->reset();
}

void InspectorFrontendClientLocal::resetState()
{
    m_settings->deleteProperty(inspectorAttachedHeightSetting);
}

Page* InspectorFrontendClientLocal::frontendPage()
{
    return m_frontendPage.get();
}

void InspectorFrontendClientLocal::windowObjectCleared()
{
    if (m_frontendHost)
        m_frontendHost->disconnectClient();
    
    m_frontendHost = InspectorFrontendHost::create(this, frontendPage());
    m_frontendHost->addSelfToGlobalObjectInWorld(debuggerWorld());
}

void InspectorFrontendClientLocal::frontendLoaded()
{
    // Call setDockingUnavailable before bringToFront. If we display the inspector window via bringToFront first it causes
    // the call to canAttachWindow to return the wrong result on Windows.
    // Calling bringToFront first causes the visibleHeight of the inspected page to always return 0 immediately after.
    // Thus if we call canAttachWindow first we can avoid this problem. This change does not cause any regressions on Mac.
    setDockingUnavailable(!canAttachWindow());
    bringToFront();

    m_frontendAPIDispatcher->frontendLoaded();
}

void InspectorFrontendClientLocal::pagePaused()
{
    // NOTE: pagePaused() and pageUnpaused() do not suspend/unsuspend the frontend API dispatcher
    // for this subclass of InspectorFrontendClient. The inspected page and the frontend page
    // exist in the same web process, so messages need to be sent even while the debugger is paused.
    // Suspending here would stall out later commands that resume the debugger, causing the test to time out.
}

void InspectorFrontendClientLocal::pageUnpaused()
{
}

UserInterfaceLayoutDirection InspectorFrontendClientLocal::userInterfaceLayoutDirection() const
{
    return m_frontendPage->userInterfaceLayoutDirection();
}

void InspectorFrontendClientLocal::requestSetDockSide(DockSide dockSide)
{
    if (dockSide == DockSide::Undocked) {
        detachWindow();
        setAttachedWindow(dockSide);
    } else if (canAttachWindow()) {
        attachWindow(dockSide);
        setAttachedWindow(dockSide);
    }
}

bool InspectorFrontendClientLocal::canAttachWindow()
{
    // Don't allow attaching to another inspector -- two inspectors in one window is too much!
    RefPtr inspectedPageController = m_inspectedPageController.get();
    bool isInspectorPage = inspectedPageController->inspectionLevel() > 0;
    if (isInspectorPage)
        return false;

    // If we are already attached, allow attaching again to allow switching sides.
    if (m_dockSide != DockSide::Undocked)
        return true;

    // Don't allow the attach if the window would be too small to accommodate the minimum inspector size.
    Ref mainFrame = inspectedPageController->inspectedPage().mainFrame();
    unsigned inspectedPageHeight = mainFrame->virtualView()->visibleHeight();
    unsigned inspectedPageWidth = mainFrame->virtualView()->visibleWidth();
    unsigned maximumAttachedHeight = inspectedPageHeight * maximumAttachedHeightRatio;
    return minimumAttachedHeight <= maximumAttachedHeight && minimumAttachedWidth <= inspectedPageWidth;
}

void InspectorFrontendClientLocal::setDockingUnavailable(bool unavailable)
{
    m_frontendAPIDispatcher->dispatchCommandWithResultAsync("setDockingUnavailable"_s, { JSON::Value::create(unavailable) });
}

RefPtr<InspectorController> InspectorFrontendClientLocal::protectedInspectedPageController() const
{
    return m_inspectedPageController.get();
}

void InspectorFrontendClientLocal::changeAttachedWindowHeight(unsigned height)
{
    unsigned totalHeight = m_frontendPage->mainFrame().virtualView()->visibleHeight() + protectedInspectedPageController()->inspectedPage().mainFrame().virtualView()->visibleHeight();
    unsigned attachedHeight = constrainedAttachedWindowHeight(height, totalHeight);
    m_settings->setProperty(inspectorAttachedHeightSetting, String::number(attachedHeight));
    setAttachedWindowHeight(attachedHeight);
}

void InspectorFrontendClientLocal::changeAttachedWindowWidth(unsigned width)
{
    unsigned totalWidth = m_frontendPage->mainFrame().virtualView()->visibleWidth() + protectedInspectedPageController()->inspectedPage().mainFrame().virtualView()->visibleWidth();
    unsigned attachedWidth = constrainedAttachedWindowWidth(width, totalWidth);
    setAttachedWindowWidth(attachedWidth);
}

void InspectorFrontendClientLocal::changeSheetRect(const FloatRect& rect)
{
    setSheetRect(rect);
}

void InspectorFrontendClientLocal::openURLExternally(const String& url)
{
    RefPtr localMainFrame = protectedInspectedPageController()->inspectedPage().localMainFrame();
    if (!localMainFrame)
        return;
    Ref mainFrame = *localMainFrame;

    UserGestureIndicator indicator { IsProcessingUserGesture::Yes, mainFrame->document() };

    FrameLoadRequest frameLoadRequest { *mainFrame->document(), mainFrame->document()->securityOrigin(), { }, blankTargetFrameName(), InitiatedByMainFrame::Unknown };

    auto [frame, created] = WebCore::createWindow(mainFrame, WTFMove(frameLoadRequest), { });
    if (!frame)
        return;
    RefPtr localFrame = dynamicDowncast<LocalFrame>(frame.get());
    if (!localFrame)
        return;

    ASSERT(localFrame->opener() == mainFrame.ptr());
    localFrame->page()->setOpenedByDOM();
    localFrame->page()->setOpenedByDOMWithOpener(true);

    // FIXME: Why do we compute the absolute URL with respect to |frame| instead of |mainFrame|?
    ResourceRequest resourceRequest { localFrame->document()->completeURL(url) };
    FrameLoadRequest frameLoadRequest2 { mainFrame->protectedDocument().releaseNonNull(), mainFrame->document()->securityOrigin(), WTFMove(resourceRequest), selfTargetFrameName(), InitiatedByMainFrame::Unknown };
    localFrame->loader().changeLocation(WTFMove(frameLoadRequest2));
}

void InspectorFrontendClientLocal::moveWindowBy(float x, float y)
{
    FloatRect frameRect = m_frontendPage->chrome().windowRect();
    frameRect.move(x, y);
    m_frontendPage->chrome().setWindowRect(frameRect);
}

void InspectorFrontendClientLocal::setAttachedWindow(DockSide dockSide)
{
    ASCIILiteral side = [&] {
        switch (dockSide) {
        case DockSide::Right:
            return "right"_s;
        case DockSide::Left:
            return "left"_s;
        case DockSide::Bottom:
            return "bottom"_s;
        case DockSide::Undocked:
            break;
        }
        return "undocked"_s;
    }();

    m_dockSide = dockSide;

    m_frontendAPIDispatcher->dispatchCommandWithResultAsync("setDockSide"_s, { JSON::Value::create(String { side }) });
}

void InspectorFrontendClientLocal::restoreAttachedWindowHeight()
{
    unsigned inspectedPageHeight = protectedInspectedPageController()->inspectedPage().mainFrame().virtualView()->visibleHeight();
    String value = m_settings->getProperty(inspectorAttachedHeightSetting);
    unsigned preferredHeight = value.isEmpty() ? defaultAttachedHeight : parseIntegerAllowingTrailingJunk<unsigned>(value).value_or(0);

    // This call might not go through (if the window starts out detached), but if the window is initially created attached,
    // InspectorController::attachWindow is never called, so we need to make sure to set the attachedWindowHeight.
    // FIXME: Clean up code so we only have to call setAttachedWindowHeight in InspectorController::attachWindow
    setAttachedWindowHeight(constrainedAttachedWindowHeight(preferredHeight, inspectedPageHeight));
}

std::optional<bool> InspectorFrontendClientLocal::evaluationResultToBoolean(InspectorFrontendAPIDispatcher::EvaluationResult result)
{
    if (!result)
        return std::nullopt;

    auto valueOrException = result.value();
    if (!valueOrException) {
        LOG(Inspector, "Encountered exception while evaluating upon the frontend: %s", valueOrException.error().message.utf8().data());
        return std::nullopt;
    }

    return valueOrException.value().toBoolean(m_frontendAPIDispatcher->frontendGlobalObject());
}

bool InspectorFrontendClientLocal::isDebuggingEnabled()
{
    return evaluationResultToBoolean(m_frontendAPIDispatcher->dispatchCommandWithResultSync("isDebuggingEnabled"_s)).value_or(false);
}

void InspectorFrontendClientLocal::setDebuggingEnabled(bool enabled)
{
    m_frontendAPIDispatcher->dispatchCommandWithResultAsync("setDebuggingEnabled"_s, { JSON::Value::create(enabled) });
}

bool InspectorFrontendClientLocal::isTimelineProfilingEnabled()
{
    return evaluationResultToBoolean(m_frontendAPIDispatcher->dispatchCommandWithResultSync("isTimelineProfilingEnabled"_s)).value_or(false);
}

void InspectorFrontendClientLocal::setTimelineProfilingEnabled(bool enabled)
{
    m_frontendAPIDispatcher->dispatchCommandWithResultAsync("setTimelineProfilingEnabled"_s, { JSON::Value::create(enabled) });
}

bool InspectorFrontendClientLocal::isProfilingJavaScript()
{
    return evaluationResultToBoolean(m_frontendAPIDispatcher->dispatchCommandWithResultSync("isProfilingJavaScript"_s)).value_or(false);
}

void InspectorFrontendClientLocal::startProfilingJavaScript()
{
    m_frontendAPIDispatcher->dispatchCommandWithResultAsync("startProfilingJavaScript"_s);
}

void InspectorFrontendClientLocal::stopProfilingJavaScript()
{
    m_frontendAPIDispatcher->dispatchCommandWithResultAsync("stopProfilingJavaScript"_s);
}

void InspectorFrontendClientLocal::showConsole()
{
    m_frontendAPIDispatcher->dispatchCommandWithResultAsync("showConsole"_s);
}

void InspectorFrontendClientLocal::showResources()
{
    m_frontendAPIDispatcher->dispatchCommandWithResultAsync("showResources"_s);
}

void InspectorFrontendClientLocal::showMainResourceForFrame(LocalFrame* frame)
{
    String frameId = protectedInspectedPageController()->ensurePageAgent().frameId(frame);
    m_frontendAPIDispatcher->dispatchCommandWithResultAsync("showMainResourceForFrame"_s, { JSON::Value::create(frameId) });
}

unsigned InspectorFrontendClientLocal::constrainedAttachedWindowHeight(unsigned preferredHeight, unsigned totalWindowHeight)
{
    return roundf(std::max(minimumAttachedHeight, std::min<float>(preferredHeight, totalWindowHeight * maximumAttachedHeightRatio)));
}

unsigned InspectorFrontendClientLocal::constrainedAttachedWindowWidth(unsigned preferredWidth, unsigned totalWindowWidth)
{
    return roundf(std::max(minimumAttachedWidth, std::min<float>(preferredWidth, totalWindowWidth - minimumAttachedInspectedWidth)));
}

void InspectorFrontendClientLocal::sendMessageToBackend(const String& message)
{
    m_dispatchTask->dispatch(message);
}

bool InspectorFrontendClientLocal::isUnderTest()
{
    return m_inspectedPageController->isUnderTest();
}

unsigned InspectorFrontendClientLocal::inspectionLevel() const
{
    return protectedInspectedPageController()->inspectionLevel() + 1;
}

Page* InspectorFrontendClientLocal::inspectedPage() const
{
    RefPtr inspectedPageController = m_inspectedPageController.get();
    return inspectedPageController ? &inspectedPageController->inspectedPage() : nullptr;
}

} // namespace WebCore
