/*
 * Copyright (C) 2024 Apple Inc. All rights reserved.
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

#import "config.h"

#if ENABLE(SCREEN_TIME)

#import "InstanceMethodSwizzler.h"
#import "TestWKWebView.h"
#import <ScreenTime/STWebHistory.h>
#import <ScreenTime/STWebpageController.h>
#import <WebKit/WKPreferencesPrivate.h>
#import <WebKit/WKWebViewConfiguration.h>
#import <WebKit/WKWebViewPrivate.h>
#import <WebKit/WKWebsiteDataStore.h>
#import <WebKit/_WKFeature.h>
#import <pal/cocoa/ScreenTimeSoftLink.h>
#import <wtf/RetainPtr.h>

static void *blockedStateObserverChangeKVOContext = &blockedStateObserverChangeKVOContext;
static bool stateDidChange = false;

static RetainPtr<TestWKWebView> webViewForScreenTimeTests(WKWebViewConfiguration *configuration = nil)
{
    if (!configuration)
        configuration = adoptNS([[WKWebViewConfiguration alloc] init]).autorelease();

    auto preferences = [configuration preferences];
    for (_WKFeature *feature in [WKPreferences _features]) {
        if ([feature.key isEqualToString:@"ScreenTimeEnabled"])
            [preferences _setEnabled:YES forFeature:feature];
    }
    return adoptNS([[TestWKWebView alloc] initWithFrame:CGRectMake(0, 0, 400, 300) configuration:configuration]);
}

static void testSuppressUsageRecordingWithDataStore(RetainPtr<WKWebsiteDataStore>&& websiteDataStore, bool suppressUsageRecordingExpectation)
{
    __block bool done = false;
    __block bool suppressUsageRecording = false;

    InstanceMethodSwizzler swizzler {
        PAL::getSTWebpageControllerClass(),
        @selector(setSuppressUsageRecording:),
        imp_implementationWithBlock(^(id object, bool value) {
            suppressUsageRecording = value;
            done = true;
        })
    };

    RetainPtr configuration = adoptNS([[WKWebViewConfiguration alloc] init]);
    [configuration setWebsiteDataStore:websiteDataStore.get()];

    RetainPtr webView = webViewForScreenTimeTests(configuration.get());
    [webView synchronouslyLoadHTMLString:@""];

    TestWebKitAPI::Util::run(&done);

    EXPECT_EQ(suppressUsageRecordingExpectation, suppressUsageRecording);
}

@interface STWebpageController ()
@property (setter=setURLIsBlocked:) BOOL URLIsBlocked;
@end

@interface STWebpageController (Staging_138865295)
@property (nonatomic, copy) NSString *profileIdentifier;
@end

@interface WKWebView (Internal)
- (STWebpageController *)_screenTimeWebpageController;
@end

@interface BlockedStateObserver : NSObject
- (instancetype)initWithWebView:(TestWKWebView *)webView;
@end

@implementation BlockedStateObserver {
    RetainPtr<TestWKWebView> _webView;
}

- (instancetype)initWithWebView:(TestWKWebView *)webView
{
    if (!(self = [super init]))
        return nil;

    _webView = webView;
    [_webView addObserver:self forKeyPath:@"_isBlockedByScreenTime" options:(NSKeyValueObservingOptionOld | NSKeyValueObservingOptionNew) context:&blockedStateObserverChangeKVOContext];
    return self;
}

- (void)observeValueForKeyPath:(NSString *)keyPath ofObject:(id)object change:(NSDictionary *)change context:(void *)context
{
    if (context == &blockedStateObserverChangeKVOContext) {
        stateDidChange = true;
        return;
    }

    [super observeValueForKeyPath:keyPath ofObject:object change:change context:context];
}
@end

static BOOL blurredViewIsPresent(TestWKWebView *webView)
{
#if PLATFORM(IOS_FAMILY)
    for (UIView *subview in [webView subviews]) {
        if ([subview isKindOfClass:[UIVisualEffectView class]])
            return true;
    }
#else
    for (NSView *subview in [webView subviews]) {
        if ([subview isKindOfClass:[NSVisualEffectView class]])
            return true;
    }
#endif
    return false;
}

static BOOL systemScreenTimeBlockingViewIsPresent(TestWKWebView *webView)
{
    RetainPtr controller = [webView _screenTimeWebpageController];
#if PLATFORM(IOS_FAMILY)
    for (UIView *subview in [webView subviews]) {
        if (subview == [controller view])
            return true;
    }
#else
    for (NSView *subview in [webView subviews]) {
        if (subview == [controller view])
            return true;
    }
#endif
    return false;
}

static RetainPtr<TestWKWebView> testShowsSystemScreenTimeBlockingView(bool showsSystemScreenTimeBlockingView)
{
    RetainPtr configuration = adoptNS([[WKWebViewConfiguration alloc] init]);
    [configuration _setShowsSystemScreenTimeBlockingView:showsSystemScreenTimeBlockingView];

    RetainPtr webView = webViewForScreenTimeTests(configuration.get());
    [webView synchronouslyLoadHTMLString:@""];

    RetainPtr controller = [webView _screenTimeWebpageController];
    [controller setURLIsBlocked:YES];

    EXPECT_EQ(showsSystemScreenTimeBlockingView, [configuration _showsSystemScreenTimeBlockingView]);

    // Check if ScreenTime's blocking view is hidden or not.
    EXPECT_EQ(showsSystemScreenTimeBlockingView, systemScreenTimeBlockingViewIsPresent(webView.get()));

    // Check if WebKit's blurred blocking view is added and in the view hierarchy or not.
    EXPECT_EQ(!showsSystemScreenTimeBlockingView, blurredViewIsPresent(webView.get()));

    return webView;
}

TEST(ScreenTime, IsBlockedByScreenTimeTrue)
{
    RetainPtr webView = webViewForScreenTimeTests();
    [webView synchronouslyLoadHTMLString:@""];

    RetainPtr controller = [webView _screenTimeWebpageController];
    [controller setURLIsBlocked:YES];

    EXPECT_TRUE([webView _isBlockedByScreenTime]);
}

TEST(ScreenTime, IsBlockedByScreenTimeFalse)
{
    RetainPtr webView = webViewForScreenTimeTests();

    RetainPtr controller = [webView _screenTimeWebpageController];
    [controller setURLIsBlocked:NO];

    [webView synchronouslyLoadHTMLString:@""];

    EXPECT_FALSE([webView _isBlockedByScreenTime]);
}

TEST(ScreenTime, IsBlockedByScreenTimeMultiple)
{
    RetainPtr webView = webViewForScreenTimeTests();

    RetainPtr controller = [webView _screenTimeWebpageController];
    [controller setURLIsBlocked:YES];
    [controller setURLIsBlocked:NO];

    [webView synchronouslyLoadHTMLString:@""];

    EXPECT_FALSE([webView _isBlockedByScreenTime]);
}

TEST(ScreenTime, IsBlockedByScreenTimeKVO)
{
    RetainPtr webView = webViewForScreenTimeTests();
    auto observer = adoptNS([[BlockedStateObserver alloc] initWithWebView:webView.get()]);

    [webView synchronouslyLoadHTMLString:@""];

    RetainPtr controller = [webView _screenTimeWebpageController];
    [controller setURLIsBlocked:YES];

    TestWebKitAPI::Util::run(&stateDidChange);

    EXPECT_TRUE([webView _isBlockedByScreenTime]);

    stateDidChange = false;

    [controller setURLIsBlocked:NO];

    TestWebKitAPI::Util::run(&stateDidChange);

    EXPECT_FALSE([webView _isBlockedByScreenTime]);

    stateDidChange = false;

    [controller setURLIsBlocked:YES];

    TestWebKitAPI::Util::run(&stateDidChange);

    EXPECT_TRUE([webView _isBlockedByScreenTime]);
}

TEST(ScreenTime, IdentifierNil)
{
    if (![PAL::getSTWebpageControllerClass() instancesRespondToSelector:@selector(setProfileIdentifier:)])
        return;

    __block bool done = false;
    __block NSString * identifier = @"testing123";

    InstanceMethodSwizzler swizzler {
        PAL::getSTWebpageControllerClass(),
        @selector(setProfileIdentifier:),
        imp_implementationWithBlock(^(id object, NSString *profileIdentifier) {
            identifier = profileIdentifier;
            done = true;
        })
    };

    RetainPtr webView = webViewForScreenTimeTests();
    [webView synchronouslyLoadHTMLString:@""];

    TestWebKitAPI::Util::run(&done);

    EXPECT_NULL(identifier);
}

TEST(ScreenTime, IdentifierString)
{
    if (![PAL::getSTWebpageControllerClass() instancesRespondToSelector:@selector(setProfileIdentifier:)])
        return;

    __block bool done = false;
    __block NSString * identifier = @"";

    InstanceMethodSwizzler swizzler {
        PAL::getSTWebpageControllerClass(),
        @selector(setProfileIdentifier:),
        imp_implementationWithBlock(^(id object, NSString *profileIdentifier) {
            identifier = profileIdentifier;
            done = true;
        })
    };

    RetainPtr uuid = [NSUUID UUID];
    RetainPtr websiteDataStore = [WKWebsiteDataStore dataStoreForIdentifier:uuid.get()];

    RetainPtr configuration = adoptNS([[WKWebViewConfiguration alloc] init]);
    [configuration setWebsiteDataStore:websiteDataStore.get()];

    RetainPtr webView = webViewForScreenTimeTests(configuration.get());
    [webView synchronouslyLoadHTMLString:@""];

    TestWebKitAPI::Util::run(&done);

    RetainPtr uuidString = [uuid UUIDString];

    EXPECT_WK_STREQ(identifier, uuidString.get());
}

TEST(ScreenTime, PersistentSession)
{
    testSuppressUsageRecordingWithDataStore([WKWebsiteDataStore defaultDataStore], false);
}

TEST(ScreenTime, NonPersistentSession)
{
    testSuppressUsageRecordingWithDataStore([WKWebsiteDataStore nonPersistentDataStore], true);
}

TEST(ScreenTime, ShowSystemScreenTimeBlockingTrue)
{
    testShowsSystemScreenTimeBlockingView(true);
}

TEST(ScreenTime, ShowSystemScreenTimeBlockingFalse)
{
    testShowsSystemScreenTimeBlockingView(false);
}

TEST(ScreenTime, ShowSystemScreenTimeBlockingFalseAndRemoved)
{
    RetainPtr webView = testShowsSystemScreenTimeBlockingView(false);
    RetainPtr controller = [webView _screenTimeWebpageController];
    [controller setURLIsBlocked:NO];
    EXPECT_FALSE([[webView configuration] _showsSystemScreenTimeBlockingView]);
    // Check if blurred blocking view is removed when URLIsBlocked is false.
    EXPECT_FALSE(blurredViewIsPresent(webView.get()));
}

#endif
