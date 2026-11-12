/* SPDX-License-Identifier: MIT
 * Game-local native window confinement. No global input tap or UC preferences.
 */
#import <AppKit/AppKit.h>
#import <objc/runtime.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Wine uses this same AppKit selector. It is undocumented; check at runtime. */
@interface NSWindow (SBConfinement)
- (NSRect)mouseConfinementRect;
- (void)setMouseConfinementRect:(NSRect)rect;
@end

static void (*originalSet)(id, SEL, NSRect);
static char requestedKey, ownedKey;
static BOOL suspended;
static BOOL refreshPending;
#ifdef SB_CONFINEMENT_TEST
static BOOL testAppActive = YES;
#endif

static BOOL appActive(void)
{
#ifdef SB_CONFINEMENT_TEST
    return testAppActive && !suspended;
#else
    return NSApp.isActive && !NSApp.isHidden && !suspended;
#endif
}

static BOOL targetWindow(NSWindow *window)
{
    NSString *name = [[window.title lowercaseString]
        stringByReplacingOccurrencesOfString:@" " withString:@""];
    return [name isEqualToString:@"stellarblade"];
}

static BOOL fullscreenWindow(NSWindow *window)
{
    if (window.styleMask & NSWindowStyleMaskFullScreen) return YES;
    NSScreen *screen = window.screen;
    // Wine can keep a titled NSWindow with its title bar outside the display.
    // Require the CONTENT to cover the screen: a normal maximized window does not.
    NSView *view = window.contentView;
    if (!screen || !view) return NO;
    NSRect content = [window convertRectToScreen:[view convertRect:view.bounds toView:nil]];
    return NSContainsRect(NSInsetRect(content, -1.0, -1.0), screen.frame);
}

static NSRect fallbackRect(NSWindow *window)
{
    if (!appActive() || !window.isKeyWindow || !window.isVisible ||
        window.isMiniaturized || window.attachedSheet || !fullscreenWindow(window))
        return NSZeroRect;
    NSView *view = window.contentView;
    if (!view) return NSZeroRect;
    NSRect rect = [view convertRect:view.bounds toView:nil];
    // Use points, not backing pixels. Keep away from the Universal Control edge.
    if (rect.size.width <= 4.0 || rect.size.height <= 4.0) return NSZeroRect;
    return NSInsetRect(rect, 2.0, 2.0);
}

static void applyWindow(NSWindow *window)
{
    NSValue *requested = objc_getAssociatedObject(window, &requestedKey);
    BOOL owned = [objc_getAssociatedObject(window, &ownedKey) boolValue];
    if (!targetWindow(window) && !owned) return;
    if (!requested) {
        requested = [NSValue valueWithRect:window.mouseConfinementRect];
        objc_setAssociatedObject(window, &requestedKey, requested, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    }
    NSRect wineRect = requested.rectValue;
    NSRect fallback = targetWindow(window) ? fallbackRect(window) : NSZeroRect;
    BOOL capture = !NSIsEmptyRect(fallback);
    NSRect effective = wineRect;
    if (capture) {
        // A narrower, explicit Wine clip remains authoritative.
        effective = NSIsEmptyRect(wineRect) ? fallback : NSIntersectionRect(wineRect, fallback);
        if (NSIsEmptyRect(effective)) effective = fallback;
    } else if (!appActive() || !window.isKeyWindow || !window.isVisible || window.isMiniaturized) {
        effective = NSZeroRect;
    }
    if (owned != capture) {
        fprintf(stderr, "stellar-pointer: %s window=%ld\n", capture ? "confined" : "released",
                (long)window.windowNumber);
    }
    objc_setAssociatedObject(window, &ownedKey, @(capture), OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    originalSet(window, @selector(setMouseConfinementRect:), effective);
}

static void refreshAll(void)
{
    for (NSWindow *window in NSApp.windows) applyWindow(window);
}

static void queueRefresh(void)
{
    // Called only on the main queue, no polling or recurring timer.
    if (refreshPending) return;
    refreshPending = YES;
    dispatch_async(dispatch_get_main_queue(), ^{
        refreshPending = NO;
        refreshAll();
    });
}

static void confinementSet(id object, SEL selector, NSRect rect)
{
    NSWindow *window = object;
    if (![NSThread isMainThread]) {
        originalSet(object, selector, rect);
        dispatch_async(dispatch_get_main_queue(), ^{ queueRefresh(); });
        return;
    }
    if (!targetWindow(window)) {
        objc_setAssociatedObject(window, &requestedKey, nil, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        objc_setAssociatedObject(window, &ownedKey, nil, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        originalSet(object, selector, rect);
        return;
    }
    objc_setAssociatedObject(window, &requestedKey, [NSValue valueWithRect:rect],
                             OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    applyWindow(window);
}

static BOOL installHook(void)
{
    if (originalSet) return YES;
    SEL selector = @selector(setMouseConfinementRect:);
    Method method = class_getInstanceMethod([NSWindow class], selector);
    if (!method || ![NSWindow instancesRespondToSelector:@selector(mouseConfinementRect)]) {
        fprintf(stderr, "stellar-pointer: native confinement unavailable; unchanged\n");
        return NO;
    }
    originalSet = (void (*)(id, SEL, NSRect))method_setImplementation(method, (IMP)confinementSet);
    return YES;
}

#ifndef SB_CONFINEMENT_TEST
__attribute__((constructor)) static void beginConfinement(void)
{
    const char *enabled = getenv("HB_STELLAR_CONFINEMENT");
    if (!enabled || strcmp(enabled, "1")) return;
    dispatch_async(dispatch_get_main_queue(), ^{
        @autoreleasepool {
            if (!installHook()) return;
            NSNotificationCenter *center = NSNotificationCenter.defaultCenter;
            NSArray<NSNotificationName> *events = @[
                NSWindowDidBecomeKeyNotification, NSWindowDidResignKeyNotification,
                NSWindowDidResizeNotification, NSWindowDidMoveNotification,
                NSWindowDidChangeScreenNotification, NSWindowDidEnterFullScreenNotification,
                NSWindowDidExitFullScreenNotification, NSWindowDidMiniaturizeNotification,
                NSWindowDidDeminiaturizeNotification, NSApplicationDidFinishLaunchingNotification,
                NSApplicationDidHideNotification, NSApplicationDidUnhideNotification
            ];
            for (NSNotificationName name in events) {
                [center addObserverForName:name object:nil queue:NSOperationQueue.mainQueue
                    usingBlock:^(NSNotification *note) { (void)note; queueRefresh(); }];
            }
            [center addObserverForName:NSApplicationWillResignActiveNotification object:nil
                queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *note) {
                    (void)note; suspended = YES; refreshAll();
                }];
            [center addObserverForName:NSApplicationDidBecomeActiveNotification object:nil
                queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *note) {
                    (void)note; suspended = NO; queueRefresh();
                }];
            [center addObserverForName:NSWindowWillCloseNotification object:nil
                queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *note) {
                    NSWindow *window = note.object;
                    if (objc_getAssociatedObject(window, &requestedKey))
                        originalSet(window, @selector(setMouseConfinementRect:), NSZeroRect);
                }];
            fprintf(stderr, "stellar-pointer: native fullscreen confinement ready\n");
            queueRefresh();
        }
    });
}
#endif
