#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>
#import <objc/runtime.h>
#import <dispatch/dispatch.h>
#import <dlfcn.h>
#import <unistd.h>

static const CGFloat kFeverTopInset = 91.0;
static NSRect (*originalConstrainFrameRect)(id, SEL, NSRect, NSScreen *);
static NSRect (*originalEnforceMenuBarAvoidance)(id, SEL, NSRect, NSScreen *);
static void (*originalSetFrameAndWineFrame)(id, SEL, NSRect);
static void log_hook(NSString *message);

static BOOL is_fever_window(NSWindow *window)
{
    return [window.title isEqualToString:@"网易发烧游戏"] &&
        window.frame.size.width >= 1000.0 &&
        window.frame.size.height >= 600.0;
}

typedef int CGSConnectionID;
typedef CGSConnectionID (*CGSDefaultConnectionFn)(void);
typedef CGError (*CGSMoveWindowFn)(CGSConnectionID, CGWindowID, CGPoint *);

static CGError move_server_window(CGWindowID windowID, CGPoint point)
{
    static CGSDefaultConnectionFn defaultConnection;
    static CGSMoveWindowFn moveWindow;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        void *skyLight = dlopen(
            "/System/Library/PrivateFrameworks/SkyLight.framework/SkyLight",
            RTLD_LAZY | RTLD_LOCAL);
        if (!skyLight) return;
        defaultConnection =
            (CGSDefaultConnectionFn)dlsym(skyLight, "_CGSDefaultConnection");
        moveWindow = (CGSMoveWindowFn)dlsym(skyLight, "CGSMoveWindow");
    });
    if (!defaultConnection || !moveWindow) return kCGErrorFailure;
    CGSConnectionID connection = defaultConnection();
    return moveWindow(connection, windowID, &point);
}

static void log_hook(NSString *message)
{
    NSString *path = [NSHomeDirectory() stringByAppendingPathComponent:
        @"Library/Application Support/SkyYYBMacFix/window-hook.log"];
    NSFileHandle *handle = [NSFileHandle fileHandleForWritingAtPath:path];
    if (!handle) {
        [[NSFileManager defaultManager] createFileAtPath:path contents:nil attributes:nil];
        handle = [NSFileHandle fileHandleForWritingAtPath:path];
    }
    [handle seekToEndOfFile];
    NSString *line = [message stringByAppendingString:@"\n"];
    [handle writeData:[line dataUsingEncoding:NSUTF8StringEncoding]];
    [handle closeFile];
}

static NSRect feverEnforceMenuBarAvoidance(
    id self, SEL selector, NSRect frame, NSScreen *screen)
{
    NSWindow *window = (NSWindow *)self;
    if (is_fever_window(window)) return frame;
    return originalEnforceMenuBarAvoidance ?
        originalEnforceMenuBarAvoidance(self, selector, frame, screen) : frame;
}

static BOOL is_fever_main_frame(NSRect frame)
{
    return frame.size.width >= 1000.0 && frame.size.height >= 600.0;
}

static BOOL frames_match(NSRect left, NSRect right)
{
    return fabs(left.origin.x - right.origin.x) <= 2.0 &&
        fabs(left.origin.y - right.origin.y) <= 2.0 &&
        fabs(left.size.width - right.size.width) <= 2.0 &&
        fabs(left.size.height - right.size.height) <= 2.0;
}

static BOOL is_fever_owner(id object)
{
    NSString *title = [object respondsToSelector:@selector(title)] ? [object title] : @"";
    NSString *process = NSProcessInfo.processInfo.processName ?: @"";
    return [title isEqualToString:@"网易发烧游戏"] ||
        [process containsString:@"FeverGamesInstaller"];
}

static NSRect maximized_fever_frame(NSWindow *window, NSScreen *screen)
{
    NSRect target = window.frame;
    CGFloat menuBarHeight =
        NSMaxY(screen.frame) - NSMaxY(screen.visibleFrame);
    CGFloat offscreenTop = MAX(0.0, kFeverTopInset - menuBarHeight);
    target.origin.x = NSMinX(screen.visibleFrame);
    target.origin.y = NSMinY(screen.visibleFrame);
    target.size.width = screen.visibleFrame.size.width;
    target.size.height =
        NSMaxY(screen.frame) + offscreenTop - target.origin.y;
    return target;
}

static void feverSetFrameAndWineFrame(id self, SEL selector, NSRect frame)
{
    NSWindow *window = (NSWindow *)self;
    if (is_fever_owner(self) && is_fever_main_frame(frame)) {
        NSScreen *screen = window.screen ?: NSScreen.mainScreen;
        if (screen) {
            NSRect target = maximized_fever_frame(window, screen);
            // Fever repeatedly reapplies its saved 1240x650 client geometry,
            // even after the native window is already maximized. Calling the
            // original Wine setter with the same forced target on every pass
            // emits a resize event and makes CEF repaint continuously. Keep
            // Wine's cached maximized rectangle and suppress only that
            // redundant rollback; the first real resize still goes through.
            if (frames_match(window.frame, target)) return;
            frame = target;
        }
    }
    originalSetFrameAndWineFrame(self, selector, frame);
}

static NSRect feverConstrainFrameRect(id self, SEL selector, NSRect frame, NSScreen *screen)
{
    NSRect constrained = originalConstrainFrameRect(self, selector, frame, screen);
    BOOL isFever = is_fever_owner(self);
    if (!isFever || !screen || !is_fever_main_frame(frame)) return constrained;

    // The maximized frame intentionally includes Fever's hidden decoration
    // above the menu bar.  AppKit would pull that rectangle back down and make
    // the fixed-width web UI look clipped in the upper-left.  Preserve Wine's
    // requested rectangle; setFrameAndWineFrame: keeps rendering and hit
    // testing synchronized while Retina backing remains independent.
    if (!NSEqualRects(frame, constrained)) {
        log_hook([NSString stringWithFormat:
            @"preserved requested %.0f,%.0f %.0fx%.0f instead of constrained %.0f,%.0f %.0fx%.0f",
            frame.origin.x, frame.origin.y, frame.size.width, frame.size.height,
            constrained.origin.x, constrained.origin.y,
            constrained.size.width, constrained.size.height]);
    }
    return frame;
}

static BOOL move_fever_owner_window(void)
{
    BOOL found = NO;
    for (NSWindow *window in NSApp.windows) {
        if (![window.title isEqualToString:@"网易发烧游戏"]) continue;
        // Wine creates a temporary 640x480 titled window before the real
        // launcher.  Its windowNumber is -1, so treating it as the launcher
        // makes CGSMoveWindow fail with kCGErrorIllegalArgument and causes the
        // polling loop to stop before the usable window exists.
        if (!is_fever_main_frame(window.frame) || window.windowNumber <= 0) continue;
        NSRect before = window.frame;
        NSScreen *screen = window.screen ?: NSScreen.mainScreen;
        if (!screen) continue;

        NSRect target = maximized_fever_frame(window, screen);
        CGFloat menuBarHeight =
            NSMaxY(screen.frame) - NSMaxY(screen.visibleFrame);
        CGFloat offscreenTop = MAX(0.0, kFeverTopInset - menuBarHeight);
        // Maximize dynamically in AppKit/Wine logical coordinates instead of
        // assuming the old 1280x800 M2 panel.  Retina remains enabled for a 2x
        // backing surface.  Include the hidden Wine decoration above the
        // visible desktop, and stop above the Dock at the bottom.
        CGFloat expectedServerY = -offscreenTop;
        CGRect serverBounds = CGRectNull;
        CFArrayRef info = CGWindowListCopyWindowInfo(
            kCGWindowListOptionIncludingWindow,
            (CGWindowID)window.windowNumber);
        if (info && CFArrayGetCount(info)) {
            NSDictionary *entry = (__bridge NSDictionary *)CFArrayGetValueAtIndex(info, 0);
            CGRectMakeWithDictionaryRepresentation(
                (__bridge CFDictionaryRef)entry[(id)kCGWindowBounds], &serverBounds);
        }
        if (info) CFRelease(info);

        BOOL nativeFrameMatches = frames_match(before, target);
        BOOL serverFrameMatches = !CGRectIsNull(serverBounds) &&
            fabs(serverBounds.origin.y - expectedServerY) <= 2.0 &&
            fabs(serverBounds.size.width - target.size.width) <= 2.0 &&
            fabs(serverBounds.size.height - target.size.height) <= 2.0;
        if (nativeFrameMatches && serverFrameMatches) {
            return YES;
        }

        // WineWindow overrides NSWindow's normal setters and may immediately
        // restore its cached Windows rectangle.  Its own internal method
        // updates both the native NSWindow frame and Wine's cached frame, so
        // rendering, hit testing and future dragging share one geometry.
        SEL setFrameAndWineFrame = NSSelectorFromString(@"setFrameAndWineFrame:");
        if ([window respondsToSelector:setFrameAndWineFrame]) {
            typedef void (*SetWineFrameFn)(id, SEL, NSRect);
            SetWineFrameFn setter = (SetWineFrameFn)[window methodForSelector:setFrameAndWineFrame];
            setter(window, setFrameAndWineFrame, target);
        } else {
            [window setFrame:target display:YES animate:NO];
        }

        // AppKit refuses to send a borderless Wine window above the menu bar
        // even after Wine has accepted the frame.  Move the WindowServer
        // window only after setFrameAndWineFrame: has synchronized Wine's
        // cached rectangle.  Unlike the old visual-only workaround, both the
        // event-coordinate source and the on-screen hit region now describe
        // the same target rectangle.
        CGPoint serverPoint = CGPointMake(target.origin.x, expectedServerY);
        CGError moveError = move_server_window(
            (CGWindowID)window.windowNumber, serverPoint);
        serverBounds = CGRectNull;
        info = CGWindowListCopyWindowInfo(
            kCGWindowListOptionIncludingWindow,
            (CGWindowID)window.windowNumber);
        if (info && CFArrayGetCount(info)) {
            NSDictionary *entry = (__bridge NSDictionary *)CFArrayGetValueAtIndex(info, 0);
            CGRectMakeWithDictionaryRepresentation(
                (__bridge CFDictionaryRef)entry[(id)kCGWindowBounds], &serverBounds);
        }
        log_hook([NSString stringWithFormat:
            @"pid=%d synchronized frame id=%ld move-error=%d frame-before=%.0f,%.0f %.0fx%.0f target=%.0f,%.0f frame-after=%.0f,%.0f server=%.0f,%.0f %.0fx%.0f",
            getpid(), (long)window.windowNumber,
            moveError,
            before.origin.x, before.origin.y, before.size.width, before.size.height,
            target.origin.x, target.origin.y,
            window.frame.origin.x, window.frame.origin.y,
            serverBounds.origin.x, serverBounds.origin.y,
            serverBounds.size.width, serverBounds.size.height]);
        if (info) CFRelease(info);
        found = frames_match(window.frame, target) &&
            !CGRectIsNull(serverBounds) &&
            fabs(serverBounds.origin.y - expectedServerY) <= 2.0 &&
            fabs(serverBounds.size.width - target.size.width) <= 2.0 &&
            fabs(serverBounds.size.height - target.size.height) <= 2.0;
        if (found) break;
    }
    return found;
}

static void install_hook(void)
{
    for (int attempt = 0; attempt < 1200; attempt++) {
        Class wineWindow = objc_getClass("WineWindow");
        Method method = wineWindow ? class_getInstanceMethod(
            wineWindow, @selector(constrainFrameRect:toScreen:)) : NULL;
        if (method) {
            IMP replacement = (IMP)feverConstrainFrameRect;
            originalConstrainFrameRect = (void *)method_setImplementation(method, replacement);
            SEL enforceSelector = NSSelectorFromString(
                @"_enforceMenuBarAvoidanceForFrame:onScreen:");
            Method enforceMethod = class_getInstanceMethod(wineWindow, enforceSelector);
            if (enforceMethod) {
                originalEnforceMenuBarAvoidance =
                    (void *)method_getImplementation(enforceMethod);
                const char *types = method_getTypeEncoding(enforceMethod);
                if (!class_addMethod(wineWindow, enforceSelector,
                    (IMP)feverEnforceMenuBarAvoidance, types)) {
                    Method ownEnforceMethod = class_getInstanceMethod(
                        wineWindow, enforceSelector);
                    method_setImplementation(
                        ownEnforceMethod, (IMP)feverEnforceMenuBarAvoidance);
                }
            }
            SEL setFrameSelector = NSSelectorFromString(@"setFrameAndWineFrame:");
            Method setFrameMethod = class_getInstanceMethod(wineWindow, setFrameSelector);
            if (setFrameMethod) {
                originalSetFrameAndWineFrame =
                    (void *)method_setImplementation(
                        setFrameMethod, (IMP)feverSetFrameAndWineFrame);
            }
            log_hook([NSString stringWithFormat:
                @"pid=%d process=%@ WineWindow menu-bar avoidance bypass installed",
                getpid(), NSProcessInfo.processInfo.processName]);
            int stableSamples = 0;
            for (int windowAttempt = 0; windowAttempt < 600; windowAttempt++) {
                __block BOOL positioned = NO;
                dispatch_sync(dispatch_get_main_queue(), ^{
                    positioned = move_fever_owner_window();
                });
                stableSamples = positioned ? stableSamples + 1 : 0;
                // Once AppKit no longer rewrites the frame, five seconds of
                // stability is enough.  Stop polling so subsequent native
                // dragging remains entirely under the user's control.
                if (windowAttempt >= 50 && stableSamples >= 25) {
                    log_hook([NSString stringWithFormat:
                        @"pid=%d top edge stable for %d samples",
                        getpid(), stableSamples]);
                    break;
                }
                usleep(200000);
            }
            return;
        }
        usleep(100000);
    }
    log_hook(@"WineWindow class did not appear before timeout");
}

__attribute__((constructor))
static void yyb_window_hook_entry(void)
{
    @autoreleasepool {
        log_hook([NSString stringWithFormat:@"pid=%d process=%@ YYB window hook loaded",
            getpid(), NSProcessInfo.processInfo.processName]);
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
            @autoreleasepool { install_hook(); }
        });
    }
}
