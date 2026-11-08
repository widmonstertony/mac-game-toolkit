# 《地平线 6》Apple Silicon 安装

[English](README.md)

适用于 **Highball Wine 11.18 ARM64/FEX＋KosmicKrisp r4**。原生 macOS Steam 库里的「开始」会把请求交给现有容器里的 Windows Steam。安装包包含中文错误弹窗、7.1.4 转双声道音频修复和优先保证自车涂装清晰度的画质配置。

1. 安装 [Highball 0.10.26](https://github.com/gauthierpiarrette/highball/releases/tag/v0.10.26) 和 [Proton-ish](https://github.com/MagnusPladsen/Proton-ish)。在 Highball 的 **Forza Horizon 6** 容器启用 **arm64-kk-wine11.18-r4**，从 Highball 的 Windows 版入口应用该游戏配方。用自己的 Windows Steam 和 Xbox 账号登录，使用容器 Steam 库里的现有安装，进游戏并正常退出一次。原生 Steam 保留能启动 FH6 的 Proton-ish 兼容工具。
2. 如果还没有 Highball CLI，按[英文页第二步](README.md#install-on-another-mac)安装匹配版本。
3. 退出游戏及其容器内的 Windows Steam，在仓库根目录先预览再安装。需要 Python 3.9 或更新版本。

   ```sh
   python3 fh6/arm64/install.py
   python3 fh6/arm64/install.py --apply
   ```

   默认沿用这台设备已有的游戏输出分辨率。原来那台 3472 × 2266 屏幕可加 `--native 3472x2266`，其他设备填自己的原生像素尺寸。
4. 把脚本打印的启动选项粘贴到 **macOS Steam → 《地平线 6》→ 属性 → 通用 → 启动选项**。保留引号和 `%command%`，之后点原来的「开始」即可。

安装器会校验组件哈希，保存本机配置备份，并拒绝未知引擎或会覆盖现有 DLL 的安装。当前可玩配置使用游戏自带 FSR 3。此前尝试的 FSR 4＋2× 插帧出现 FHE21，未纳入可玩配置。音频补丁在 M5 Max 上通过了 12 声道测试，游戏内轮胎声和原生 Steam 冷启动仍待实测。

音频模块的对应源码、许可证、重建和撤销方法见[英文说明](README.md#rebuild-or-revert)。
