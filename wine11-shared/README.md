# 应用宝共享 Wine 11 容器

这套工具把应用宝中的轻量 Windows 入口桥接到同一个 Highball/Wine 11
环境，而不是为每个启动器复制一套 Wine 前缀。

本机在 2026-11-03 验证的共享环境为：

```text
~/Library/Application Support/com.tencent.yybmac.wine.engine/FH6Runtime/
└── bottles/FH6-Clean/
    ├── Steam / FH6
    ├── EA App
    ├── Epic Games Launcher
    └── Rockstar Games Launcher
```

应用宝仍负责显示和打开入口；Windows 小启动器通过自定义 URL Scheme
唤起原生 macOS 桥接器，再由 `highball run FH6-Clean ...` 启动目标程序。
桥接器会过滤应用宝旧 Wine 注入的 `DYLD`、GStreamer 和渲染器环境变量，
避免两套 Wine Framework 混用。

## 重新安装入口

前提：`FH6Runtime`、`FH6-Clean` 和其中的启动器程序已经存在。仓库不会复制、
下载或分发 Wine、D3DMetal、Steam、EA、Epic、Rockstar、游戏、账号或存档。

安装 Windows 启动器需要 Homebrew 的 MinGW-w64：

```bash
brew install mingw-w64
```

然后双击 `install.command`，或在终端执行：

```bash
./wine11-shared/install.command
```

脚本会：

1. 本机编译并签名 EA、Epic、Rockstar 三个 macOS URL 桥接器；
2. 本机编译三个不含第三方代码的 Windows 小入口；
3. 把入口写入应用宝 `apps.db`，写入前保留带时间戳的备份；
4. 为三个程序保存 DXMT 的逐程序设置；FH6 的 D3DMetal 设置不变；
5. 不修改 Plex、游戏文件、项目源码或存档。

## 已验证结果

- EA App 主程序与 `EABackgroundService.exe` 可从应用宝入口启动；
- Epic Games Launcher 可从应用宝入口启动；
- Rockstar Launcher 及其 Crashpad 守护进程可从应用宝入口启动；
- 三次进程路径均落在 `FH6Runtime/bottles/FH6-Clean`；
- 删除 `/Applications/CrossOver.app` 和旧 CrossOver bottle 后再次启动 EA 仍成功。

账号服务可能把新 Wine 前缀视为新设备，因此首次打开仍可能要求重新登录。

## 隐私与恢复

`apps.db` 备份保存在原文件旁边，文件名以
`apps.db.codex-before-launchers-` 开头。它可能包含本机已安装应用清单，不要上传。
仓库的 `.gitignore` 已排除 `apps.db`、注册表、存档、DLL 与普通 EXE。

源码目录只包含原创桥接代码：

- `macos/Bridge.swift`：原生 URL 桥接器；
- `macos/*-Info.plist`：三个入口的目标路径和 URL Scheme；
- `windows/launcher.c`：应用宝旧 Wine 内运行的小入口；
- `install_apps_db.py`：原子更新应用宝 JSON 数据库；
- `install.command`：本机构建与安装流程。
