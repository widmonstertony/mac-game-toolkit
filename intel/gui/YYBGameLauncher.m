#import <Cocoa/Cocoa.h>

@interface YYBFlippedView : NSView
@end

@implementation YYBFlippedView
- (BOOL)isFlipped { return YES; }
@end

@interface YYBAppDelegate : NSObject <NSApplicationDelegate>
@property(nonatomic, strong) NSWindow *window;
@property(nonatomic, strong) NSStackView *content;
@property(nonatomic, strong) NSTextField *statusLabel;
@property(nonatomic, strong) NSButton *syncButton;
@property(nonatomic, strong) NSButton *qualityButton;
@property(nonatomic, strong) NSMutableArray<NSURL *> *knownApps;
@property(nonatomic, assign) BOOL working;
@end

@implementation YYBAppDelegate

- (void)applicationDidFinishLaunching:(NSNotification *)notification {
    (void)notification;
    self.knownApps = [NSMutableArray array];
    [self buildWindow];
    [self reloadLibrary];
    [self.window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender {
    (void)sender;
    return YES;
}

- (NSURL *)supportRoot {
    return [[[NSFileManager defaultManager] homeDirectoryForCurrentUser]
        URLByAppendingPathComponent:@"Library/Application Support/YYBIntelLauncher"
        isDirectory:YES];
}

- (NSTextField *)label:(NSString *)text size:(CGFloat)size weight:(NSFontWeight)weight {
    NSTextField *label = [NSTextField labelWithString:text];
    label.font = [NSFont systemFontOfSize:size weight:weight];
    label.maximumNumberOfLines = 0;
    label.lineBreakMode = NSLineBreakByWordWrapping;
    return label;
}

- (NSButton *)button:(NSString *)title action:(SEL)action {
    NSButton *button = [NSButton buttonWithTitle:title target:self action:action];
    button.bezelStyle = NSBezelStyleRounded;
    button.controlSize = NSControlSizeLarge;
    return button;
}

- (void)buildWindow {
    NSRect frame = NSMakeRect(0, 0, 940, 700);
    self.window = [[NSWindow alloc]
        initWithContentRect:frame
                  styleMask:(NSWindowStyleMaskTitled |
                             NSWindowStyleMaskClosable |
                             NSWindowStyleMaskMiniaturizable |
                             NSWindowStyleMaskResizable)
                    backing:NSBackingStoreBuffered
                      defer:NO];
    self.window.title = @"Windows 游戏";
    self.window.minSize = NSMakeSize(760, 560);
    [self.window center];

    NSView *root = self.window.contentView;
    NSStackView *toolbar = [NSStackView stackViewWithViews:@[]];
    toolbar.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    toolbar.alignment = NSLayoutAttributeCenterY;
    toolbar.spacing = 10;
    toolbar.edgeInsets = NSEdgeInsetsMake(12, 18, 12, 18);
    toolbar.translatesAutoresizingMaskIntoConstraints = NO;

    NSTextField *title = [self label:@"Windows 游戏" size:22 weight:NSFontWeightBold];
    [toolbar addArrangedSubview:title];
    [toolbar addArrangedSubview:[NSView new]];
    NSButton *reveal = [self button:@"在访达中显示" action:@selector(revealApps:)];
    self.syncButton = [self button:@"同步到启动台" action:@selector(syncLaunchpad:)];
    [toolbar addArrangedSubview:reveal];
    [toolbar addArrangedSubview:self.syncButton];

    NSScrollView *scroll = [[NSScrollView alloc] initWithFrame:NSZeroRect];
    scroll.hasVerticalScroller = YES;
    scroll.drawsBackground = NO;
    scroll.translatesAutoresizingMaskIntoConstraints = NO;
    YYBFlippedView *document = [[YYBFlippedView alloc] initWithFrame:NSMakeRect(0, 0, 900, 600)];
    self.content = [NSStackView stackViewWithViews:@[]];
    self.content.orientation = NSUserInterfaceLayoutOrientationVertical;
    self.content.alignment = NSLayoutAttributeLeading;
    self.content.spacing = 18;
    self.content.edgeInsets = NSEdgeInsetsMake(22, 24, 28, 24);
    self.content.translatesAutoresizingMaskIntoConstraints = NO;
    [document addSubview:self.content];
    scroll.documentView = document;

    NSVisualEffectView *statusBar = [[NSVisualEffectView alloc] initWithFrame:NSZeroRect];
    statusBar.material = NSVisualEffectMaterialHeaderView;
    statusBar.blendingMode = NSVisualEffectBlendingModeWithinWindow;
    statusBar.translatesAutoresizingMaskIntoConstraints = NO;
    self.statusLabel = [self label:@"正在读取已安装的 Windows 应用…" size:13 weight:NSFontWeightRegular];
    self.statusLabel.textColor = NSColor.secondaryLabelColor;
    self.statusLabel.translatesAutoresizingMaskIntoConstraints = NO;
    [statusBar addSubview:self.statusLabel];

    [root addSubview:toolbar];
    [root addSubview:scroll];
    [root addSubview:statusBar];
    [NSLayoutConstraint activateConstraints:@[
        [toolbar.topAnchor constraintEqualToAnchor:root.topAnchor],
        [toolbar.leadingAnchor constraintEqualToAnchor:root.leadingAnchor],
        [toolbar.trailingAnchor constraintEqualToAnchor:root.trailingAnchor],
        [scroll.topAnchor constraintEqualToAnchor:toolbar.bottomAnchor],
        [scroll.leadingAnchor constraintEqualToAnchor:root.leadingAnchor],
        [scroll.trailingAnchor constraintEqualToAnchor:root.trailingAnchor],
        [scroll.bottomAnchor constraintEqualToAnchor:statusBar.topAnchor],
        [statusBar.leadingAnchor constraintEqualToAnchor:root.leadingAnchor],
        [statusBar.trailingAnchor constraintEqualToAnchor:root.trailingAnchor],
        [statusBar.bottomAnchor constraintEqualToAnchor:root.bottomAnchor],
        [statusBar.heightAnchor constraintGreaterThanOrEqualToConstant:44],
        [self.statusLabel.leadingAnchor constraintEqualToAnchor:statusBar.leadingAnchor constant:20],
        [self.statusLabel.trailingAnchor constraintEqualToAnchor:statusBar.trailingAnchor constant:-20],
        [self.statusLabel.centerYAnchor constraintEqualToAnchor:statusBar.centerYAnchor],
        [self.content.topAnchor constraintEqualToAnchor:document.topAnchor],
        [self.content.leadingAnchor constraintEqualToAnchor:document.leadingAnchor],
        [self.content.trailingAnchor constraintEqualToAnchor:document.trailingAnchor],
        [self.content.bottomAnchor constraintEqualToAnchor:document.bottomAnchor],
        [document.widthAnchor constraintEqualToAnchor:scroll.contentView.widthAnchor],
    ]];
}

- (NSDictionary *)infoForApp:(NSURL *)app {
    return [NSDictionary dictionaryWithContentsOfURL:
        [app URLByAppendingPathComponent:@"Contents/Info.plist"]] ?: @{};
}

- (NSArray<NSURL *> *)managedGameApps {
    NSFileManager *manager = [NSFileManager defaultManager];
    NSURL *homeApps = [[manager homeDirectoryForCurrentUser]
        URLByAppendingPathComponent:@"Applications" isDirectory:YES];
    NSArray<NSURL *> *roots = @[
        [NSURL fileURLWithPath:@"/Applications" isDirectory:YES],
        homeApps,
    ];
    NSMutableDictionary<NSString *, NSURL *> *found = [NSMutableDictionary dictionary];
    for (NSURL *root in roots) {
        NSArray<NSURL *> *children = [manager contentsOfDirectoryAtURL:root
                                           includingPropertiesForKeys:nil
                                                              options:NSDirectoryEnumerationSkipsHiddenFiles
                                                                error:nil];
        for (NSURL *app in children) {
            if (![app.pathExtension.lowercaseString isEqualToString:@"app"]) continue;
            NSDictionary *info = [self infoForApp:app];
            if (![info[@"YYBIntelLaunchpadManaged"] boolValue]) continue;
            NSString *identifier = info[@"CFBundleIdentifier"] ?: app.path;
            found[identifier] = app;
        }
    }
    return [found.allValues sortedArrayUsingComparator:^NSComparisonResult(NSURL *a, NSURL *b) {
        NSString *nameA = [self infoForApp:a][@"CFBundleDisplayName"] ?: a.lastPathComponent;
        NSString *nameB = [self infoForApp:b][@"CFBundleDisplayName"] ?: b.lastPathComponent;
        return [nameA localizedStandardCompare:nameB];
    }];
}

- (NSView *)headerView {
    NSStackView *row = [NSStackView stackViewWithViews:@[]];
    row.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    row.alignment = NSLayoutAttributeTop;
    row.spacing = 16;

    NSImageView *icon = [[NSImageView alloc] initWithFrame:NSMakeRect(0, 0, 68, 68)];
    icon.image = [NSImage imageWithSystemSymbolName:@"gamecontroller.fill"
                           accessibilityDescription:@"Windows 游戏"];
    icon.contentTintColor = NSColor.systemBlueColor;
    [icon.widthAnchor constraintEqualToConstant:68].active = YES;
    [icon.heightAnchor constraintEqualToConstant:68].active = YES;

    NSStackView *text = [NSStackView stackViewWithViews:@[]];
    text.orientation = NSUserInterfaceLayoutOrientationVertical;
    text.alignment = NSLayoutAttributeLeading;
    text.spacing = 6;
    [text addArrangedSubview:[self label:@"腾讯 PC 游戏环境" size:22 weight:NSFontWeightBold]];
    NSTextField *detail = [self label:
        @"统一打开 Windows 版 Steam、网易启动器和已安装游戏。每个项目同时是一个正常的 macOS 应用，拥有独立 Dock 图标和退出行为。"
                                  size:14
                                weight:NSFontWeightRegular];
    detail.textColor = NSColor.secondaryLabelColor;
    [text addArrangedSubview:detail];
    [row addArrangedSubview:icon];
    [row addArrangedSubview:text];
    return row;
}

- (NSButton *)cardForApp:(NSURL *)app detail:(NSString *)detail {
    NSDictionary *info = [self infoForApp:app];
    NSString *name = info[@"CFBundleDisplayName"] ?: info[@"CFBundleName"]
        ?: app.URLByDeletingPathExtension.lastPathComponent;
    NSButton *card = [NSButton buttonWithTitle:
        [NSString stringWithFormat:@"%@\n%@", name, detail]
                                             target:self
                                             action:@selector(openApp:)];
    card.identifier = app.path;
    // Read our generated ICNS directly. NSWorkspace may retain the generic
    // icon it cached before the wrapper gained Contents/Resources, which made
    // the Launchpad icon correct while this library card stayed stale.
    NSString *iconName = info[@"CFBundleIconFile"];
    NSImage *appIcon = nil;
    if (iconName.length) {
        if (!iconName.pathExtension.length) iconName = [iconName stringByAppendingPathExtension:@"icns"];
        NSString *iconPath = [[app.path stringByAppendingPathComponent:@"Contents/Resources"]
            stringByAppendingPathComponent:iconName];
        appIcon = [[NSImage alloc] initWithContentsOfFile:iconPath];
    }
    card.image = appIcon ?: [[NSWorkspace sharedWorkspace] iconForFile:app.path];
    card.imagePosition = NSImageAbove;
    card.imageScaling = NSImageScaleProportionallyUpOrDown;
    card.font = [NSFont systemFontOfSize:14 weight:NSFontWeightSemibold];
    card.bezelStyle = NSBezelStyleTexturedRounded;
    card.controlSize = NSControlSizeLarge;
    card.lineBreakMode = NSLineBreakByTruncatingTail;
    card.toolTip = [NSString stringWithFormat:@"打开 %@", name];
    [card.widthAnchor constraintEqualToConstant:220].active = YES;
    [card.heightAnchor constraintEqualToConstant:142].active = YES;
    [self.knownApps addObject:app];
    return card;
}

- (NSView *)gridForApps:(NSArray<NSURL *> *)apps launcher:(BOOL)launcher {
    NSGridView *grid = [[NSGridView alloc] init];
    grid.rowSpacing = 14;
    grid.columnSpacing = 14;
    NSMutableArray<NSView *> *row = [NSMutableArray array];
    for (NSURL *app in apps) {
        NSDictionary *info = [self infoForApp:app];
        NSString *detail = launcher ? @"Windows 启动器" :
            ([info[@"YYBPlatform"] isEqual:@"steam"] ? @"Steam 游戏" : @"网易游戏");
        [row addObject:[self cardForApp:app detail:detail]];
        if (row.count == 3) {
            [grid addRowWithViews:row];
            [row removeAllObjects];
        }
    }
    if (row.count) {
        while (row.count < 3) {
            NSView *spacer = [NSView new];
            [spacer.widthAnchor constraintEqualToConstant:220].active = YES;
            [row addObject:spacer];
        }
        [grid addRowWithViews:row];
    }
    return grid;
}

- (NSView *)qualityPanel {
    NSBox *box = [[NSBox alloc] initWithFrame:NSZeroRect];
    box.boxType = NSBoxCustom;
    box.cornerRadius = 14;
    box.fillColor = [NSColor controlBackgroundColor];
    box.borderColor = [NSColor separatorColor];
    box.borderWidth = 1;

    NSStackView *stack = [NSStackView stackViewWithViews:@[]];
    stack.orientation = NSUserInterfaceLayoutOrientationVertical;
    stack.alignment = NSLayoutAttributeLeading;
    stack.spacing = 9;
    stack.edgeInsets = NSEdgeInsetsMake(16, 18, 16, 18);
    stack.translatesAutoresizingMaskIntoConstraints = NO;
    [stack addArrangedSubview:[self label:@"高清模式 · 默认开启" size:16 weight:NSFontWeightSemibold]];
    NSTextField *features = [self label:
        @"✓ Retina 2× 后备缓冲    ✓ 200% Windows DPI    ✓ 光·遇目标帧率 60 FPS"
                                    size:13
                                  weight:NSFontWeightRegular];
    features.textColor = NSColor.secondaryLabelColor;
    [stack addArrangedSubview:features];
    self.qualityButton = [self button:@"重新应用高清设置" action:@selector(applyQuality:)];
    [stack addArrangedSubview:self.qualityButton];
    [box addSubview:stack];
    [NSLayoutConstraint activateConstraints:@[
        [stack.topAnchor constraintEqualToAnchor:box.topAnchor],
        [stack.leadingAnchor constraintEqualToAnchor:box.leadingAnchor],
        [stack.trailingAnchor constraintEqualToAnchor:box.trailingAnchor],
        [stack.bottomAnchor constraintEqualToAnchor:box.bottomAnchor],
        [box.widthAnchor constraintEqualToConstant:720],
    ]];
    return box;
}

- (void)reloadLibrary {
    for (NSView *view in self.content.arrangedSubviews.copy) {
        [self.content removeArrangedSubview:view];
        [view removeFromSuperview];
    }
    [self.knownApps removeAllObjects];
    [self.content addArrangedSubview:[self headerView]];

    NSMutableArray<NSURL *> *launchers = [NSMutableArray array];
    for (NSString *name in @[@"Steam（Windows）", @"网易游戏启动器"]) {
        NSURL *app = [NSURL fileURLWithPath:
            [NSString stringWithFormat:@"/Applications/%@.app", name]
                             isDirectory:YES];
        if ([[NSFileManager defaultManager] fileExistsAtPath:app.path]) {
            [launchers addObject:app];
        }
    }
    if (launchers.count) {
        [self.content addArrangedSubview:[self label:@"启动器" size:18 weight:NSFontWeightBold]];
        [self.content addArrangedSubview:[self gridForApps:launchers launcher:YES]];
    }

    NSArray<NSURL *> *games = [self managedGameApps];
    [self.content addArrangedSubview:[self label:@"我的游戏" size:18 weight:NSFontWeightBold]];
    if (games.count) {
        [self.content addArrangedSubview:[self gridForApps:games launcher:NO]];
    } else {
        NSTextField *empty = [self label:
            @"还没有检测到已安装游戏。在 Steam 或网易启动器内安装后，点击“同步到启动台”。"
                                      size:14
                                    weight:NSFontWeightRegular];
        empty.textColor = NSColor.secondaryLabelColor;
        [self.content addArrangedSubview:empty];
    }
    [self.content addArrangedSubview:[self qualityPanel]];

    NSURL *server = [[self supportRoot]
        URLByAppendingPathComponent:@".runtime/wine-engine.app/Contents/MacOS/wineserver"];
    BOOL engineReady = [[NSFileManager defaultManager] isExecutableFileAtPath:server.path];
    self.statusLabel.stringValue = engineReady
        ? [NSString stringWithFormat:@"引擎已就绪 · %lu 个启动器 · %lu 个游戏",
            (unsigned long)launchers.count, (unsigned long)games.count]
        : @"腾讯 PC 游戏引擎尚未安装完整";
}

- (void)setWorking:(BOOL)working message:(NSString *)message {
    self.working = working;
    self.syncButton.enabled = !working;
    self.qualityButton.enabled = !working;
    self.statusLabel.stringValue = message;
}

- (void)runTool:(NSString *)relative arguments:(NSArray<NSString *> *)arguments
        progress:(NSString *)progress success:(NSString *)success {
    if (self.working) return;
    NSURL *tool = [[self supportRoot] URLByAppendingPathComponent:relative];
    if (![[NSFileManager defaultManager] isExecutableFileAtPath:tool.path]) {
        self.statusLabel.stringValue = [NSString stringWithFormat:@"组件还没有安装：%@", tool.lastPathComponent];
        return;
    }
    [self setWorking:YES message:progress];
    NSTask *task = [[NSTask alloc] init];
    NSPipe *pipe = [NSPipe pipe];
    task.executableURL = tool;
    task.arguments = arguments;
    task.standardOutput = pipe;
    task.standardError = pipe;
    __weak typeof(self) weakSelf = self;
    task.terminationHandler = ^(NSTask *finished) {
        NSData *data = [pipe.fileHandleForReading readDataToEndOfFile];
        NSString *output = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding] ?: @"";
        dispatch_async(dispatch_get_main_queue(), ^{
            typeof(self) self = weakSelf;
            if (!self) return;
            [self reloadLibrary];
            if (finished.terminationStatus == 0) {
                [self setWorking:NO message:success];
            } else {
                NSString *trimmed = [output stringByTrimmingCharactersInSet:
                    [NSCharacterSet whitespaceAndNewlineCharacterSet]];
                [self setWorking:NO message:trimmed.length ? trimmed : @"操作没有完成"];
            }
        });
    };
    NSError *error = nil;
    if (![task launchAndReturnError:&error]) {
        [self setWorking:NO message:error.localizedDescription];
    }
}

- (void)openApp:(NSButton *)sender {
    NSURL *url = sender.identifier.length
        ? [NSURL fileURLWithPath:sender.identifier isDirectory:YES]
        : nil;
    if (!url || ![[NSWorkspace sharedWorkspace] openURL:url]) {
        self.statusLabel.stringValue = @"macOS 没有成功打开这个应用。";
    } else {
        self.statusLabel.stringValue = [NSString stringWithFormat:@"已打开 %@", url.URLByDeletingPathExtension.lastPathComponent];
    }
}

- (void)syncLaunchpad:(id)sender {
    (void)sender;
    [self runTool:@"bin/sync-launchpad-apps.py"
        arguments:@[]
         progress:@"正在检测游戏并同步到启动台…"
          success:@"启动台同步完成"];
}

- (void)applyQuality:(id)sender {
    (void)sender;
    [self runTool:@"bin/apply-intel-retina.py"
        arguments:@[@"--fps", @"60"]
         progress:@"正在重新应用 Retina 2× 与 60 FPS…"
          success:@"高清设置已应用：Retina 2×、200% DPI、60 FPS"];
}

- (void)revealApps:(id)sender {
    (void)sender;
    if (self.knownApps.count) {
        [[NSWorkspace sharedWorkspace] activateFileViewerSelectingURLs:self.knownApps];
    }
}

@end

int main(int argc, const char *argv[]) {
    (void)argc;
    (void)argv;
    @autoreleasepool {
        NSApplication *application = [NSApplication sharedApplication];
        YYBAppDelegate *delegate = [[YYBAppDelegate alloc] init];
        application.delegate = delegate;
        [application run];
    }
    return 0;
}
