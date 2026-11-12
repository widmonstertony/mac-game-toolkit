# 《剑星》全屏鼠标捕获 / Stellar Blade fullscreen pointer confinement

只在原生 Steam 的《剑星》（3489700）中启用。目标是在选中的全屏游戏内
把系统光标限制在游戏内容区域，防止从边缘移到 iPad 或其他显示器。
不关闭 Universal Control、自动重连或多指系统手势，也不修改图形、音频、存档和缓存。

## 为什么需要

X11 Driver 下的 GrabFullscreen 设置不能配置 macOS Cocoa 窗口驱动。
[Wine 的 win32u 输入代码](https://github.com/wine-mirror/wine/blob/master/dlls/win32u/input.c)
对全虚拟桌面大小的裁剪存在“无需限制”的分支；Windows 中看不到的 iPad 边缘
不会因此得到保护。这是相关源码机制，尚不能单凭它证明本机每一次越界的调用原因。

这个组件在原生窗口层补足全屏边界，使用
[Wine 自己也使用的 AppKit 窗口限制接口](https://github.com/wine-mirror/wine/blob/master/dlls/winemac.drv/cocoa_cursorclipping.m)
`setMouseConfinementRect:`。该接口未公开，组件先检查接口是否存在；不存在则保持原样并记录原因。
不是全局鼠标钩子，不要求额外辅助功能权限。

选中、可见、未最小化的《剑星》全屏窗口才增加限制；边界向内留两点，使用逻辑坐标适配 Retina。
Wine 在全屏时把裁剪清零也不会丢失这层保护。
切换 App、窗口失焦或最小化时释放，重新选中全屏游戏时恢复。
游戏自己要求的更小裁剪区域仍被保留。普通窗口模式不额外限制。
使用窗口／焦点通知，无轮询服务，不拦截按键、触控板手势或鼠标点击。

## 安装、测试与停用

退出剑星。首次修改它的启动选项时也要退出原生 Steam，然后执行：

```sh
python3 wine11-shared/mouse/install.py
```

仅增加剑星的 `NOTPROTON_STELLAR_POINTER=1` 启动开关，注册 Protonish 插件并本机编译签名。
现支持工具 `protonish-highball-x64-crossover26.3-r28`；不替换整个 Wine 引擎。
已存在的超分、插帧和显示选项原样保留。
停用时移除该开关或改成 `NOTPROTON_STELLAR_POINTER=0`，下一次启动生效。
安装前后的剑星启动选项单独记录到 `protonish/backups/stellar-pointer-*/launch-options.json`，不包含账户凭据。

回归检查：

```sh
python3 -m unittest discover -s wine11-shared/mouse -p 'test_*.py'
clang -O1 -Wall -Wextra -Werror -fobjc-arc -arch arm64 -arch x86_64 \
  -framework AppKit wine11-shared/mouse/test-confinement.m -o /tmp/test-stellar-confinement
/tmp/test-stellar-confinement
arch -x86_64 /tmp/test-stellar-confinement
```

原生 AppKit 回归检查包含：全屏限制、Wine 清零后仍限制、更小裁剪、失焦释放／恢复、
最小化、普通窗口和其他窗口隔离。它使用测试窗口，不是游戏操作。
最终还需要在本机全屏剑星中验证：持续向 iPad 边缘转视角、Command-Tab 切回、
多指手势切换，再切回游戏，退出后鼠标正常。回归通过不等于已完成这项实机验证。

## English

This is a game-local native pointer-confinement supplement for Stellar Blade (3489700)
launched by Protonish from native Steam. It uses the same runtime-checked, undocumented
AppKit confinement selector as Wine's macOS driver. It keeps the focused fullscreen
game's pointer inside its content area, preserving tighter Wine clips, and releases on
focus loss, app switching or minimization. Windowed and unrelated windows are unchanged.

It does not disable Universal Control, alter iPad reconnection settings, intercept
gestures/clicks, change game graphics/audio/cache, or install a polling service.
Enable via the installer above after closing the game and, for first-time launch-option
edits, native Steam. Remove `NOTPROTON_STELLAR_POINTER=1` to disable it next launch.
The exact supported Protonish tool and regression commands are listed above.
Native API tests cover lifecycle behavior; real-game edge crossing and system gestures
still require user validation with the connected iPad.
