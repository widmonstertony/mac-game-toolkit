/* SPDX-License-Identifier: MIT */
#define SB_CONFINEMENT_TEST 1
#import "stellar-confinement.m"
#include <assert.h>

@interface TestWindow : NSWindow
@property BOOL testKey;
@property BOOL testVisible;
@property BOOL testMini;
@end
@implementation TestWindow
- (BOOL)isKeyWindow { return self.testKey; }
- (BOOL)isVisible { return self.testVisible; }
- (BOOL)isMiniaturized { return self.testMini; }
@end

int main(void)
{
    @autoreleasepool {
        [NSApplication sharedApplication];
        assert(installHook());
        NSScreen *screen = NSScreen.mainScreen;
        assert(screen);
        TestWindow *window = [[TestWindow alloc] initWithContentRect:screen.frame
            styleMask:NSWindowStyleMaskBorderless backing:NSBackingStoreBuffered defer:NO];
        window.title = @"StellarBlade";
        window.testKey = YES;
        window.testVisible = YES;
        [window setMouseConfinementRect:NSZeroRect];
        NSRect expected = NSInsetRect([window.contentView convertRect:window.contentView.bounds toView:nil], 2, 2);
        assert(NSEqualRects(window.mouseConfinementRect, expected));
        // A Wine clear must not turn off foreground fullscreen confinement.
        [window setMouseConfinementRect:NSZeroRect];
        assert(NSEqualRects(window.mouseConfinementRect, expected));
        // Preserve a game-requested narrower clip.
        NSRect narrow = NSInsetRect(expected, 50, 50);
        [window setMouseConfinementRect:narrow];
        assert(NSEqualRects(window.mouseConfinementRect, narrow));
        [window setMouseConfinementRect:NSZeroRect];
        // Release before app deactivation completes (Command-Tab / Mission Control).
        suspended = YES;
        applyWindow(window);
        assert(NSIsEmptyRect(window.mouseConfinementRect));
        suspended = NO;
        applyWindow(window);
        assert(NSEqualRects(window.mouseConfinementRect, expected));
        // Lose key, minimize, or deactivate: all release, then restore on focus.
        window.testKey = NO;
        applyWindow(window);
        assert(NSIsEmptyRect(window.mouseConfinementRect));
        window.testKey = YES;
        window.testMini = YES;
        applyWindow(window);
        assert(NSIsEmptyRect(window.mouseConfinementRect));
        window.testMini = NO;
        testAppActive = NO;
        applyWindow(window);
        assert(NSIsEmptyRect(window.mouseConfinementRect));
        testAppActive = YES;
        applyWindow(window);
        assert(NSEqualRects(window.mouseConfinementRect, expected));
        // Windowed mode adds no capture; Wine's own explicit clip is restored.
        [window setFrame:NSMakeRect(screen.frame.origin.x + 100, screen.frame.origin.y + 100, 800, 500) display:NO];
        applyWindow(window);
        assert(NSIsEmptyRect(window.mouseConfinementRect));
        [window setMouseConfinementRect:NSMakeRect(20, 20, 100, 100)];
        assert(NSEqualRects(window.mouseConfinementRect, NSMakeRect(20, 20, 100, 100)));
        // Other windows stay untouched even when screen-sized.
        window.title = @"Steam";
        [window setFrame:screen.frame display:NO];
        [window setMouseConfinementRect:NSZeroRect];
        assert(NSIsEmptyRect(window.mouseConfinementRect));
        (void)queueRefresh; // The production path uses notification-driven refreshes.
        puts("PASS: native AppKit rect, clear override, narrow clip, focus release/restore, minimize, windowed and non-game windows");
    }
    return 0;
}
