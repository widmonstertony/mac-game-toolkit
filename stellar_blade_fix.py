#!/usr/bin/env python3
"""Repair Stellar Blade startup and character clarity in Tencent YYB Wine."""

from __future__ import annotations

import re
import hashlib
import struct
import time
import zlib
from pathlib import Path

from sky_yyb_fix import (
    BackupSet,
    PUBLIC_MMKV,
    PUBLIC_MMKV_CRC,
    SYSTEM_REG,
    USER_REG,
    atomic_write,
    open_new_path,
    restore_from,
    stop_related_processes,
    upsert_reg_value,
)


PACKAGE = "com.tencent.macexe.com.steampowered.steam.3489700"
APP_ID = "3489700"
PREFIX = USER_REG.parent
GAME_ROOT = PREFIX / "drive_c/Program Files (x86)/Steam/steamapps/common/StellarBlade"
CONFIG_ROOT = PREFIX / "drive_c/users/tencentyyb/AppData/Local/SB/Saved/Config/WindowsNoEditor"
USER_SETTINGS = CONFIG_ROOT / "GameUserSettings.ini"
ENGINE_INI = CONFIG_ROOT / "Engine.ini"
SHORTCUT_URL = PREFIX / "drive_c/users/tencentyyb/Desktop/剑星.url"
SHORTCUT_APP = Path("/Applications/腾讯应用宝") / f"{PACKAGE}.app"
STEAM_USERDATA = PREFIX / "drive_c/Program Files (x86)/Steam/userdata"
YYB_GPTK = Path.home() / (
    "Library/Application Support/com.tencent.yybmac/ExeEngineDownload/"
    "wine-engine.app/Contents/Frameworks/render/gptk"
)
CROSSOVER_GPTK = Path(
    "/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/"
    "lib64/apple_gptk"
)

# YYB engine 1.2.2 currently bundles the GPTK 4.0 beta 2 D3DMetal build below.
# On the verified M4 Pro it rejects six Stellar Blade compute shaders with
# MetalIRConverter UnsupportedInstruction/error:7.  CrossOver's signed GPTK 2.1
# set compiles those same shaders.  Never copy a merely similar or newer set:
# all source files used by the transaction must match these exact digests.
D3DMETAL_4_BETA2_SHA256 = "5d5784029ee4ea50895b967dce8dccd7471bdfa97dd749415e96c72b36a6c8d0"
D3DMETAL_2_1_FILES = {
    "external/D3DMetal.framework/Versions/A/D3DMetal": "9908d7990c28d3a25c8530b96ab703588b2c7d49dfb06155d24b130638b6cf99",
    "external/D3DMetal.framework/Versions/A/Resources/Info.plist": "47d3036cfc02fa46f2684945ec67a41d1ebd79d5ca1d15557f001340525fe599",
    "external/D3DMetal.framework/Versions/A/Resources/LICENSE": "553d0035773ddd1590045f8fdc3a4c6ead31e36336721aeca8421e88ed1c9f80",
    "external/D3DMetal.framework/Versions/A/Resources/default.metallib": "6c29e8ba4b1444d3c305b3fce6715dc56d37600ae4e8516a7eaad5737606b134",
    "external/D3DMetal.framework/Versions/A/Resources/libdxccontainer.dylib": "c2af30a2e6b57c47bcc7524d5a6000cb5fe8328901feb92d94cdf4d7ea06aa30",
    "external/D3DMetal.framework/Versions/A/Resources/libdxcompiler.dylib": "ef53466b2a56628707bbe0271cd2404aef2d685f98181bc407b7fcae4eccd358",
    "external/D3DMetal.framework/Versions/A/Resources/libdxilconv.dylib": "92813b6249d99ef7fe547f9e4220e484b7000e3d059a6484c23b4e47ea3d4f6d",
    "external/D3DMetal.framework/Versions/A/Resources/libmetalirconverter.dylib": "7fc3664c71ce813f59474f5f19ba69dcbb499bd7cc77e97fd7a67280b8ba2f8d",
    "external/D3DMetal.framework/Versions/A/Resources/version.plist": "0f3d423aa8b9eef37df412d4a1742727e5fe9a4ea58985d4215f03eb3f864391",
    "external/D3DMetal.framework/Versions/A/_CodeSignature/CodeResources": "7a925c1ad4dbe6233ce543cb13dbabbbafc76824fe19908f1b19de0a043eda21",
    "external/libd3dshared.dylib": "402ace6dd1c1c2ce58bb15ac68e64829b809fe3f5bdc344298cb4a8365ce1ae6",
    "wine/x86_64-windows/atidxx64.dll": "18995adb10bed163b7f58ab184c747b1ae6447043292d3497a515bc32e5972b5",
    "wine/x86_64-windows/d3d11.dll": "13a621833929d7ced7761d0cf9f9b57765345633dfb83161c2735e9cfb57b0f5",
    "wine/x86_64-windows/d3d12.dll": "dca51fb33c5d79d36dae95131205b96c837db5c2579d0bfd5fcf3d15e887b324",
    "wine/x86_64-windows/dxgi.dll": "5e1d256b455f744979092f901a0baeb0053ab291f53e0f6b65f5354043b56d57",
}
D3DMETAL_MAIN = "external/D3DMetal.framework/Versions/A/D3DMetal"

WIDTH = 3840
HEIGHT = 2160
LAUNCH_OPTIONS = (
    "-NoSplash -NoStartupMovies -log "
    f"-ResX={WIDTH} -ResY={HEIGHT} -Fullscreen"
)


class RepairError(RuntimeError):
    pass


def file_sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def patch_d3dmetal_compat(backups: BackupSet) -> str | None:
    """Replace only the verified GPTK 4 beta set that drops SB shaders."""
    target_main = YYB_GPTK / D3DMETAL_MAIN
    source_main = CROSSOVER_GPTK / D3DMETAL_MAIN
    if not target_main.exists() or not source_main.exists():
        return None

    target_digest = file_sha256(target_main)
    stable_digest = D3DMETAL_2_1_FILES[D3DMETAL_MAIN]
    if target_digest not in (D3DMETAL_4_BETA2_SHA256, stable_digest):
        # A future YYB engine may contain a fixed build.  Do not downgrade an
        # unknown version based on filename or version text alone.
        return None

    for relative, digest in D3DMETAL_2_1_FILES.items():
        source = CROSSOVER_GPTK / relative
        if not source.is_file() or file_sha256(source) != digest:
            raise RepairError("本机 CrossOver D3DMetal 2.1 组件不完整或版本不匹配。")

    changed = 0
    for relative, digest in D3DMETAL_2_1_FILES.items():
        source = CROSSOVER_GPTK / relative
        target = YYB_GPTK / relative
        if target.is_file() and file_sha256(target) == digest:
            continue
        if not target.parent.exists():
            raise RepairError(f"应用宝 GPTK 目录结构异常：{target.parent}")
        backups.capture(target)
        atomic_write(target, source.read_bytes(), source.stat().st_mode & 0o777)
        if file_sha256(target) != digest:
            raise RepairError(f"D3DMetal 2.1 写入后校验失败：{target}")
        changed += 1

    return "D3DMetal 2.1 compute-shader compatibility" if changed else "D3DMetal 2.1 already active"


def set_ini_values(text: str, section: str, values: dict[str, str]) -> str:
    header = re.compile(rf"(?mi)^\[{re.escape(section)}\]\s*$")
    match = header.search(text)
    if not match:
        if text and not text.endswith("\n"):
            text += "\n"
        text += f"\n[{section}]\n"
        match = header.search(text)
        assert match is not None
    end = text.find("\n[", match.end())
    if end < 0:
        end = len(text)
    block = text[match.start():end]
    for key, value in values.items():
        line = f"{key}={value}"
        pattern = re.compile(rf"(?mi)^{re.escape(key)}=.*$")
        if pattern.search(block):
            block = pattern.sub(line, block)
        else:
            block = block.rstrip("\n") + "\n" + line + "\n"
    return text[:match.start()] + block + text[end:]


def patch_game_settings(backups: BackupSet) -> None:
    if not USER_SETTINGS.exists():
        raise RepairError(f"未找到剑星配置：{USER_SETTINGS}")
    backups.capture(USER_SETTINGS)
    text = USER_SETTINGS.read_text(encoding="utf-8-sig")
    text = set_ini_values(
        text,
        "/Script/SB.SBGameUserSettings",
        {
            "Sharpen": "0.800000",
            "SharpenFSR3": "0.750000",
            "bFirstRun": "False",
            "bHDDMode": "False",
            "FrameLimit": "FrameLimit_120",
            "EnviromentObjectDetail": "SB_GAMEUSERSETTINGS_LOW",
            "CharacterObjectDetail": "SB_GAMEUSERSETTINGS_HIGH",
            "EnviromentTextures": "SB_GAMEUSERSETTINGS_LOW",
            "CharacterTextures": "SB_GAMEUSERSETTINGS_VERYHIGH",
            "VolumetricFog": "SB_GAMEUSERSETTINGS_OFF",
            "ShadowQuality": "SB_GAMEUSERSETTINGS_LOW",
            "EffectQuality": "SB_GAMEUSERSETTINGS_LOW",
            "EnvironmentQuality": "SB_GAMEUSERSETTINGS_LOW",
            "Lighting": "SB_GAMEUSERSETTINGS_LOW",
            "FoliageQuality": "SB_GAMEUSERSETTINGS_LOW",
            "AmbientOcclusion": "SB_GAMEUSERSETTINGS_OFF",
            "DepthOfField": "SB_GAMEUSERSETTINGS_OFF",
            "ScreenSpaceReflection": "SB_GAMEUSERSETTINGS_OFF",
            "SceneColorFringeQuality": "SB_GAMEUSERSETTINGS_OFF",
            "GrainQuality": "SB_GAMEUSERSETTINGS_OFF",
            "MaterialQuality": "SB_GAMEUSERSETTINGS_HIGH",
            "AntiAliasing": "SB_GAMEUSERSETTINGS_LOW",
            "NvidiaDLSS": "SB_GAMEUSERSETTINGS_OFF",
            "NvidiaFrameGeneration": "SB_GAMEUSERSETTINGS_OFF",
            "NvidiaReflexLowLatency": "SB_GAMEUSERSETTINGS_OFF",
            # EXTENSION1 is the game's FSR 3 Native AA preset.  Unlike the
            # Performance preset it keeps a 100% internal render percentage,
            # which avoids reconstructing Eve from a 1080p source image.
            "AmdFSR3": "SB_GAMEUSERSETTINGS_EXTENSION1",
            "AmdFrameInterpolation": "SB_GAMEUSERSETTINGS_LOW",
            "IntelXeSS": "SB_GAMEUSERSETTINGS_OFF",
            "AnimationQuality": "SB_GAMEUSERSETTINGS_HIGH",
            "CharacterViewDistance": "1.000000",
            "EnviromentObjectViewDistance": "0.000000",
            "UpscalerType": "SB_GAMEUSERSETTINGS_MEDIUM",
            "SavedNvidiaFrameGeneration": "SB_GAMEUSERSETTINGS_OFF",
            "SavedNvidiaReflexLowLatency": "SB_GAMEUSERSETTINGS_OFF",
            "SavedAmdFrameInterpolation": "SB_GAMEUSERSETTINGS_LOW",
            "bVSync": "False",
            "bUseVSync": "False",
            "bUseDynamicResolution": "False",
            "ResolutionSizeX": str(WIDTH),
            "ResolutionSizeY": str(HEIGHT),
            "LastUserConfirmedResolutionSizeX": str(WIDTH),
            "LastUserConfirmedResolutionSizeY": str(HEIGHT),
            "DesiredScreenWidth": str(WIDTH),
            "DesiredScreenHeight": str(HEIGHT),
            "LastUserConfirmedDesiredScreenWidth": str(WIDTH),
            "LastUserConfirmedDesiredScreenHeight": str(HEIGHT),
            "FullscreenMode": "0",
            "PreferredFullscreenMode": "0",
            "FrameRateLimit": "120.000000",
        },
    )
    text = set_ini_values(text, "ScalabilityGroups", {"sg.ResolutionQuality": "100.000000"})
    atomic_write(USER_SETTINGS, text.encode("utf-8"))


def patch_movie_settings(backups: BackupSet) -> None:
    backups.capture(ENGINE_INI)
    text = ENGINE_INI.read_text(encoding="utf-8-sig") if ENGINE_INI.exists() else ""
    text = set_ini_values(
        text,
        "/Script/MoviePlayer.MoviePlayerSettings",
        {
            "bWaitForMoviesToComplete": "False",
            "bMoviesAreSkippable": "True",
        },
    )
    text = set_ini_values(
        text,
        "SystemSettings",
        {
            "r.MotionBlurQuality": "0",
            "r.MotionBlur.Amount": "0",
            "r.MotionBlur.Max": "0",
            "r.MotionBlurPerObjectSize": "0",
            "r.MotionBlur.Scale": "0",
            "r.MotionBlur.Separate": "0",
            "r.DepthOfFieldQuality": "0",
            "r.SceneColorFringeQuality": "0",
            "r.SceneColorFringe.Max": "0",
            "r.Tonemapper.GrainQuantization": "0",
            "r.FidelityFX.FSR3.Enabled": "1",
            "r.FidelityFX.FSR3.QualityMode": "0",
            "r.FidelityFX.FSR3.AdjustMipBias": "1",
            "r.FidelityFX.FSR3.Sharpness": "0.800000",
            "r.FidelityFX.FSR3.DeDither": "2",
            "r.ScreenPercentage": "100",
            "r.Velocity.EnableVertexDeformation": "1",
            "r.FidelityFX.FI.Enabled": "1",
            "r.VSync": "0",
            "t.MaxFPS": "120",
            # Zero means an unlimited UE texture-streaming pool.  YYB reports
            # only an 8 GiB Windows adapter even on a 48 GiB unified-memory
            # Mac, which otherwise triggers the game's blurry-outfit fallback.
            "r.Streaming.PoolSize": "0",
            "r.Streaming.LimitPoolSizeToVRAM": "0",
            "r.Streaming.MipBias": "0",
            "r.Streaming.Boost": "2",
            "r.Streaming.FullyLoadUsedTextures": "1",
            # Keep the player skeletal mesh at its highest authored LOD while
            # allowing the environment quality controls above to remain low.
            "r.SkeletalMeshLODBias": "-3",
            # Reduce temporal smearing without disabling the temporal history
            # required by FSR 3 Native AA and frame interpolation.
            "r.TemporalAA.Algorithm": "1",
            "r.TemporalAACurrentFrameWeight": "0.500000",
            "r.TemporalAASamples": "4",
        },
    )
    atomic_write(ENGINE_INI, text.encode("utf-8"), 0o600)


def patch_wine_dpi(backups: BackupSet) -> None:
    for path in (USER_REG, SYSTEM_REG):
        if not path.exists():
            raise RepairError(f"Wine 注册表不存在：{path}")
        backups.capture(path)

    user = USER_REG.read_text(encoding="utf-8")
    user = upsert_reg_value(user, "Software\\\\Wine\\\\Mac Driver", "RetinaMode", '"Y"')
    user = upsert_reg_value(user, "Software\\\\Wine\\\\Direct3D", "VideoMemorySize", '"8192"')
    layers = "Software\\\\Microsoft\\\\Windows NT\\\\CurrentVersion\\\\AppCompatFlags\\\\Layers"
    for executable in (
        r"C:\\Program Files (x86)\\Steam\\steamapps\\common\\StellarBlade\\SB.exe",
        r"C:\\Program Files (x86)\\Steam\\steamapps\\common\\StellarBlade\\SB\\Binaries\\Win64\\SB-Win64-Shipping.exe",
    ):
        user = upsert_reg_value(user, layers, executable, '"~ HIGHDPIAWARE"')
    atomic_write(USER_REG, user.encode("utf-8"))

    system = SYSTEM_REG.read_text(encoding="utf-8")
    ifeo = "Software\\\\Microsoft\\\\Windows NT\\\\CurrentVersion\\\\Image File Execution Options"
    for name in ("SB.exe", "SB-Win64-Shipping.exe"):
        system = upsert_reg_value(system, ifeo + "\\\\" + name, "dpiAwareness", "dword:00000002")
    atomic_write(SYSTEM_REG, system.encode("utf-8"))


def varint(value: int) -> bytes:
    result = bytearray()
    while value >= 128:
        result.append((value & 127) | 128)
        value >>= 7
    result.append(value)
    return bytes(result)


def patch_retina_entry(backups: BackupSet) -> None:
    if not PUBLIC_MMKV.exists() or not PUBLIC_MMKV_CRC.exists():
        raise RepairError("未找到应用宝高清配置数据库。")
    blob = bytearray(PUBLIC_MMKV.read_bytes())
    meta = bytearray(PUBLIC_MMKV_CRC.read_bytes())
    actual_size = struct.unpack_from("<I", blob, 0)[0]
    if actual_size <= 0 or actual_size + 4 > len(blob) or len(meta) < 40:
        raise RepairError("应用宝高清配置数据库结构异常。")
    crc = zlib.crc32(blob[4:4 + actual_size]) & 0xFFFFFFFF
    if struct.unpack_from("<I", meta, 0)[0] != crc:
        raise RepairError("应用宝高清配置正在被占用，请完全退出应用宝后重试。")
    version = struct.unpack_from("<I", meta, 4)[0]
    if version >= 3 and struct.unpack_from("<I", meta, 28)[0] != actual_size:
        raise RepairError("应用宝高清配置元数据长度不一致。")
    if any(meta[12:28]) or (len(meta) >= 112 and any(meta[104:112])):
        raise RepairError("不支持加密或带过期字段的应用宝高清配置。")

    key = b"exe_app_retina_zoom_ratio_" + PACKAGE.encode("ascii")
    record = varint(len(key)) + key + b"\x04\x032.0"
    end = actual_size + 4
    needed = end + len(record)
    if needed > len(blob):
        blob.extend(b"\0" * (((needed + 16383) // 16384) * 16384 - len(blob)))
    blob[end:needed] = record
    actual_size += len(record)
    struct.pack_into("<I", blob, 0, actual_size)
    crc = zlib.crc32(blob[4:4 + actual_size]) & 0xFFFFFFFF
    struct.pack_into("<I", meta, 0, crc)
    struct.pack_into("<I", meta, 8, (struct.unpack_from("<I", meta, 8)[0] + 1) & 0xFFFFFFFF)
    if version >= 3:
        struct.pack_into("<III", meta, 28, actual_size, actual_size, crc)
    backups.capture(PUBLIC_MMKV)
    backups.capture(PUBLIC_MMKV_CRC)
    atomic_write(PUBLIC_MMKV, bytes(blob))
    atomic_write(PUBLIC_MMKV_CRC, bytes(meta))


def patch_shortcut(backups: BackupSet) -> None:
    if not SHORTCUT_URL.exists():
        raise RepairError(f"未找到剑星 Steam 快捷方式：{SHORTCUT_URL}")
    backups.capture(SHORTCUT_URL)
    encoded = LAUNCH_OPTIONS.replace(" ", "%20").replace("=", "%3D")
    text = SHORTCUT_URL.read_text(encoding="ascii")
    url = f"steam://run/{APP_ID}//{encoded}/"
    text = re.sub(r"(?mi)^URL=.*$", f"URL={url}", text)
    atomic_write(SHORTCUT_URL, text.encode("ascii"))


def patch_steam_launch_options(backups: BackupSet) -> None:
    configs = list(STEAM_USERDATA.glob("*/config/localconfig.vdf"))
    if not configs:
        raise RepairError("未找到 Steam 用户配置。")
    changed = 0
    for path in configs:
        text = path.read_text(encoding="utf-8")
        apps = re.search(r"(?m)^\t{4}\"apps\"\s*$", text)
        if not apps:
            continue
        prefix = text[:apps.end()]
        suffix = text[apps.end():]
        block = re.compile(
            rf"(?ms)^(\t{{5}}\"{APP_ID}\"\s*\n\t{{5}}\{{\n)(.*?)(^\t{{5}}\}})"
        )
        match = block.search(suffix)
        if not match:
            continue
        body = match.group(2)
        option_line = f'\t\t\t\t\t\t"LaunchOptions"\t\t"{LAUNCH_OPTIONS}"\n'
        option_re = re.compile(r'(?m)^\t{6}"LaunchOptions".*\n?')
        if option_re.search(body):
            body = option_re.sub(option_line, body)
        else:
            body = option_line + body
        replacement = match.group(1) + body + match.group(3)
        suffix = suffix[:match.start()] + replacement + suffix[match.end():]
        backups.capture(path)
        atomic_write(path, (prefix + suffix).encode("utf-8"))
        changed += 1
    if not changed:
        raise RepairError("Steam 配置里没有找到剑星的启动项。")


def apply() -> tuple[Path, str | None]:
    if not (GAME_ROOT / "SB/Binaries/Win64/SB-Win64-Shipping.exe").exists():
        raise RepairError("没有找到完整的剑星安装。")
    stop_related_processes()
    backups = BackupSet()
    try:
        d3dmetal_status = patch_d3dmetal_compat(backups)
        patch_game_settings(backups)
        patch_movie_settings(backups)
        patch_wine_dpi(backups)
        patch_retina_entry(backups)
        patch_shortcut(backups)
        patch_steam_launch_options(backups)
        backups.finish()
    except Exception:
        restore_from(backups.root, announce=False)
        raise
    return backups.root, d3dmetal_status


def launch() -> None:
    if not SHORTCUT_APP.exists():
        raise RepairError(f"未找到启动台剑星入口：{SHORTCUT_APP}")
    open_new_path(SHORTCUT_APP)


def main() -> int:
    try:
        backup, d3dmetal_status = apply()
        print("剑星修复完成：")
        print("  ✓ 应用宝子入口 Retina 2×")
        print(f"  ✓ 输出与内部渲染 {WIDTH}×{HEIGHT}，FSR 3 Native AA")
        print("  ✓ FSR 3 插帧开启，目标上限 120 FPS（不锁 60）")
        print("  ✓ 人物最高纹理/骨骼 LOD 并常驻已用贴图，环境画质保持低档")
        print("  ✓ 跳过启动影片等待，并启用启动日志")
        if d3dmetal_status:
            print(f"  ✓ {d3dmetal_status}")
        print("  ✓ 剑星进程 High-DPI 感知与 8 GB 显存预算")
        print(f"  ✓ 原配置备份：{backup}")
        time.sleep(1)
        launch()
        return 0
    except RepairError as exc:
        print(f"错误：{exc}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
