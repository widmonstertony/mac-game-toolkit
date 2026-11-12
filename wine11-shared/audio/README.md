# 《黑神话：悟空》音频修复 / Wukong audio fix

2026-11-12：保存用户在本机确认消除背景音乐、对白噼啪和断续的版本。
这次包含缓冲保护和断流平滑处理；不是前一个只提升线程优先级的版本。
用户的实机反馈是目前的听感验证依据，不代表已覆盖所有设备或场景。

## 范围与原理

- 只在原生 Steam 的《黑神话：悟空》（2358720）启用，保留其他游戏的音频、图形、存档和启动配置。
- 音频生产线程和 Wine 音频时钟获得实时调度；音频计时周期缩短至 2.5 ms。
- Wine 环形缓冲至少保留六个周期，防止 AudioUnit 初始化和启动阶段把 I/O 缓冲缩得过小。
- 对 float32 交错音频的缓冲耗尽部分做衰减，恢复时做最多 64 帧交叉淡入，避免突然归零产生爆音。这可以减轻断流的听感，不能补回缺失的音频内容。
- 仅在悟空前缀中禁用 `EpicWebHelper.exe` 的 `winegstreamer`，保留音频主链。

源码源自 [Highball audiobuf](https://github.com/gauthierpiarrette/highball/blob/8aacc6b0a682422939bbc663d0a83b2db3458431/spike/audiobuf/audiobuf.c)，按 GPL-3.0-only 保存；许可证见 [COPYING](COPYING)。

## 安装与版本限制

先退出悟空；若要首次修改启动选项，也要退出原生 Steam。执行：

```sh
python3 wine11-shared/audio/install.py
```

脚本本机编译、签名组件并注册到 Protonish。现有游戏文件不需要重下。
已安装修复且声音正常的设备无需再次安装。

**此版本读取 Wine 私有音频流结构，只支持已验证的 x86_64 Wine 引擎**
`highball-x64-crossover26.3-r28`；不应直接注入 ARM64/FEX Wine 或其他版本。
安装器在修改前核对 `root/lib/wine/x86_64-unix/winecoreaudio.so` SHA-256：

```text
893be23ca5eefe4811c2732c4fa326fef2e0c5fc024264cf7db453365caf637d
```

不匹配就停止，不能通过仅改工具名或功能位来绕过结构验证。
生成的 dylib 包含两种机器码，不代表两种 Wine ABI 都受支持。

组件：`~/Library/Application Support/protonish/game-components/2358720/libwukong-audiort.dylib`。
启用选项为悟空独有的 `NOTPROTON_WUKONG_AUDIO_RT=1`。
安装记录位于 `~/Library/Application Support/protonish/backups/wukong-audio-*`。
这些本地备份可能含账户设置，不上传 GitHub。

恢复时退出悟空，在它的 Steam 启动选项中移除上述开关即可停用组件；
若需精确回到某次安装前的状态，退出相关前缀和 Steam 后恢复该次安装记录中的文件。
不要把其他设备的 `localconfig.vdf` 或 `user.reg` 整份覆盖到本机。

## English

This records the user's locally confirmed fix for sustained crackling and dropouts in
Black Myth: Wukong. It adds buffer protection and underrun smoothing to the earlier
audio-clock scheduling hook. It is enabled only for app 2358720 through Protonish;
other games retain their settings.

The hook promotes audio producer/clock threads, reduces the timer period to 2.5 ms,
keeps at least six periods in the Wine ring buffer, and protects the AudioUnit I/O
buffer during initialization/start. Float32 interleaved underruns fade out and recover
with a 64-frame crossfade. This masks discontinuities; it does not reconstruct missing
audio. A prefix-local EpicWebHelper override is also preserved.

**Private Wine stream offsets are engine-specific.** The installer accepts only the
SHA-256-pinned x86_64 winecoreaudio module above. The universal dylib is not a promise
of ARM64/FEX Wine compatibility. The source is GPL-3.0-only, derived from the linked
Highball commit; no proprietary engine or game binaries are distributed.

Exit Wukong before installing. On first installation, exit native Steam as well if its
launch options need updating. Run the Python installer above, then launch normally
from native Steam. Existing working installations need no reinstall. Remove only
`NOTPROTON_WUKONG_AUDIO_RT=1` from Wukong's launch options to disable the component.
