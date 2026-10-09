# FH6 · Highball / M2 画质预设与重配教程

这是本仓库的**独立附加工具**，不走应用宝，不更改光遇或剑星的安装方案。
它恢复游戏内 FSR 超分和车辆优先的画质设置，并提供私人备份、预览和撤销。
**它不是“一键安装 FH6”或“稳定插帧到 60 帧”的兼容层。**

## 实测范围与未解决问题

2026-10-08 本机环境：基础版 M2（10 核 GPU、16 GB），macOS 27.0.1，
Highball 0.10.12，Wine 11 `x64-crossover26.3-r20`，D3DMetal 4.0b2。
游戏能够进入实际游玩；本工具已执行本机配置写入与读回检查。
配置文件版本为 `UserConfig Version="52"`。自动兼容设置只接受以上 r20 引擎，
未知引擎/配置版本会停止，而不是修改 DLL 固定偏移。

截至该日期，**粉色碎片状多层痛车涂装仍未修复**，每秒固定卡顿也未根治。
更改纹理/锐化不等于修复涂装资源，不能承诺这些设置能解决它们。
其他芯片、未来版本和长期插帧稳定性尚未验证。

## 换电脑重新配置

1. 从 [Highball 官方网站](https://gethighball.com/)或[官方发行页](https://github.com/gauthierpiarrette/highball/releases)安装 Highball。
2. 在 Highball 中创建 Windows 游戏环境，默认目录名用 `Games`。
   通过其 Steam/FH6 游戏入口应用官方配方并安装引擎，使用合法购买的 Steam 游戏。
   登录 Steam 和 Xbox 后，至少正常启动游戏并退出一次，使画质 XML 落盘。
   [维护者的 FH6 配方](https://github.com/gauthierpiarrette/highball-db/blob/main/recipes/games/forza-horizon-6.json)
   说明了 macOS 27、D3DMetal 4 与 Intel Arc 身份设置要求。
   配方可能随上游更新；不要把本工具的 r20 限制理解成上游永远只支持 r20。
3. 在 Highball **停止整个游戏环境**，再退出 Highball 和 Windows Steam。
   不能只关闭游戏画面：后台进程可能在退出时覆盖设置。
4. 安装 Python **3.9 或更新版本**（例如 [Python 官方安装器](https://www.python.org/downloads/macos/)）。
5. 下载本仓库 ZIP 并解压，在 `fh6` 文件夹双击：
   - `restore-fsr.command`：历史输出 **2560×1600＋FSR 超分**，不是原生内部渲染。
   - `performance-fsr.command`：**1920×1200＋FSR 超分**，基础 M2 的轻负载备选。
   macOS 拦截时可右键→打开；不需要 sudo。
6. 从 **Highball 中的 FH6 游戏入口**启动游戏，不要直接运行 EXE，也不要从应用宝的 Steam 启动另一套环境。
   两个 Steam 环境的登录状态不能视为同一个。首次启动/改变设置后可能需要重新优化着色器。

分辨率应匹配自己的屏幕比例；16:9 屏幕可在终端选择 1920×1080。
2560×1600 输出不代表基础 M2 能跑满 60 帧。FPS 上限和真正测得的 FPS 不同。

## 命令行、不同环境名与预览

先在解压后的 `fh6` 目录打开终端：

```bash
python3 configure.py status
python3 configure.py setup --resolution 1920x1200
python3 configure.py setup --resolution 1920x1200 --apply
```

`setup` 不带 `--apply` **只预览，不写文件**。默认分辨率为 2560×1600。
非 `Games` 环境可在每条命令后加 `--bottle "你的环境目录名"`。
非默认安装位置加 `--highball-home "/你的/Highball目录"`。
用户名和配置位置会自动发现，不硬编码作者的用户名。
如果有多个 Windows 用户生成了 FH6 配置，脚本会拒绝猜测。

如果新机器的 r20 环境还缺 GPU 兼容设置，可明确执行：

```bash
python3 configure.py setup --prepare-runtime --apply
```

该选项合并 D3DMetal 渲染器、msync、FH6 的 Intel Arc A750 标识及 `D3DM_MTL4=0`。
它不下载引擎，不安装来源不明的补丁，不替换其他游戏的设置/Steam 快捷方式。
渲染器、同步和 Metal 后端是 **bottle 级别** 设置：共享该环境的其他游戏也会使用它们。
`D3DM_MTL4=0` 是 D3DMetal 4 的 Metal 3 后端开关，**不是降级到 D3DMetal 3**。
若上游更新到别的引擎，先使用上游配方；不要强行改 engineID 冒充 r20。
仅恢复画质时不需此选项。

## 预设实际修改什么

- 历史 `FSR3Mode=2`；保持同一配置枚举，不把它未经核对地标成某个画质档名。
  在游戏视频菜单检查实际显示的 FSR 档位。
- 设置输出尺寸、60 Hz 下 `PresentInterval=1`，垂直同步与 FPS 显示开启。
  `FrameRate=3` 沿用历史值；不证明测得 60 FPS。
- 车辆 LOD 优先、主车细节 Ultra；环境纹理中档、几何中档，阴影/反射/雾低档，光追关闭。
- FSR 锐化 0.70，关闭动态优化和运动模糊。
- 不更改音量、输入、镜头/FOV，也不恢复先前极小的纹理流送预算。
- 本预设设置 `DLSSGMode=0`，**不自动启用 OptiScaler 或 Highball 插帧**。
  如果此前自行启用了 DLL 注入或 bottle 插帧，它不会移除/关闭那些第三方改动；应先用干净的上游环境。

## 插帧为什么单独保存，而不当作成功默认项

FSR **超分**和 FSR **帧生成**是两件事。以前命名为 working 的本地备份中：
Highball `frameGen=1`（关闭），OptiScaler `Enabled=false / FGInput=nofg / FGOutput=nofg`。
这不证明曾获得稳定插帧。

更早的实验尝试用 Streamline/DLSSG 的运动矢量、深度和 HUDless 输入，
交给 OptiScaler/FSRFG 输出。虽然日志有 DLL 初始化和资源标记，
NGX feature requirements 曾失败（`0xbad00002`）；还出现 HUD 重影、固定卡顿、
崩溃和约 70 GB 内存占用。因此不能把 DLL 加载、帧率上限或 UI 上的开关当作成功证据。

`experimental-optiscaler-fg.ini` **仅存档实验关键参数**，并非完整 INI 或完整兼容补丁包。
需要自己从 [OptiScaler 官方项目](https://github.com/optiscaler/OptiScaler)取得相匹配的版本与所需 FSRFG 库，
按其说明备份并安装，再把这些键合并到该版本的 INI。
不能用它替换整个 INI，也不能随便把旧 `version.dll` 改名激活。
游戏的 DLSSG 能力检测、Streamline 链路及 Wine/D3DMetal 支持还必须实际通过；
**当前未提供能让 FH6/M2 一键稳定开启这条插帧路径的方案**。
实验必须检查 UI 合成、实际生成帧计数和长期内存变化；菜单开关开启并不算验收。
不要在不确认游戏/联机政策时注入 DLL。

Highball 另一条 LSFG/Metal 路径需要引擎的插帧组件，以及自己合法购买的
Lossless Scaling 对应 `lsfg-vk.dll`。本机 r20 安装没有可用的该组件。
而且这是画面光流路径，不是用户要求的游戏运动矢量＋深度路径，
不能拿它冒充 FSRFG 的 HUD 正确合成方案。第三方付费/专有 DLL 不在本仓库分发。

## 备份与撤销

每次实际更改前，原文件被完整备份到本机该环境的
`highball/fh6-presets/backups/<时间-随机编号>/`，终端会显示确切目录。
备份可能含机器标识或环境信息，**不要上传到公共 GitHub**。
备份目录权限 0700、文件 0600；修改用原子替换，第二个文件写入失败时回滚。

先退出游戏环境和 Highball，再执行：

```bash
python3 configure.py restore --backup "/终端显示的完整备份目录"
python3 configure.py restore --backup "/终端显示的完整备份目录" --apply
```

如果游戏或你在之后又改了文件，撤销会拒绝覆盖，以免丢掉新设置。
此时可以用备份手动比较；没有强制覆盖/删存档功能。
仓库不包含作者的存档、账号、注册表、涂装文件、DLL 或私人诊断日志。

## 检查与测试

```bash
python3 -m unittest discover -s fh6 -p 'test_*.py' -v
```

从仓库根目录执行。测试使用临时合成配置，不访问真实游戏环境。
涵盖保留非画质设置、幂等、未知版本拒绝、备份恢复、并发修改、失败回滚和进程检查。
这属于配置工具测试，**不是长期游戏稳定性或 60 FPS 的验证**。
