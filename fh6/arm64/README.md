# Forza Horizon 6 on Apple Silicon

[中文说明](README.zh.md)

This is the **Highball Wine 11.18 ARM64/FEX + KosmicKrisp r4** setup. It keeps the existing Windows Steam game, settings and cloud sync in Highball. The Play button in native macOS Steam hands the launch to that existing Windows Steam session. The setup includes the FH6 Chinese error-dialog fix, the 7.1.4 audio downmix, and a vehicle-first graphics preset.

## Install on another Mac

1. Install [Highball 0.10.26](https://github.com/gauthierpiarrette/highball/releases/tag/v0.10.26) and [Proton-ish](https://github.com/MagnusPladsen/Proton-ish). Set up the ARM64/FEX + KosmicKrisp preview engine **arm64-kk-wine11.18-r4** in a bottle named **Forza Horizon 6**. In Highball, use the game's Windows route so its VKD3D recipe is applied. Sign into Windows Steam and Xbox normally, then run and quit the game once. The game must be installed in this bottle's Steam library. In macOS Steam, retain the working Proton-ish compatibility selection for FH6.
2. Install the Highball CLI if it is absent. With Xcode Command Line Tools installed, build the matching CLI from upstream:

   ```sh
   git clone --branch v0.10.26 https://github.com/gauthierpiarrette/highball.git
   cd highball
   swift build -c release --product highball
   hb="$HOME/Library/Application Support/Highball"
   mkdir -p "$hb/bin" "$hb/db"
   test -e "$hb/bin/highball" || install -m 755 .build/release/highball "$hb/bin/highball"
   test -e "$hb/db/games" || ln -s /Applications/Highball.app/Contents/Resources/db-games "$hb/db/games"
   ```

3. Quit FH6 and the **Windows Steam inside this bottle**. From this repository's root, run:

   ```sh
   python3 fh6/arm64/install.py
   python3 fh6/arm64/install.py --apply
   ```

   Python 3.9 or newer is required. By default the installer retains the screen resolution already configured. For the original 3472 × 2266 panel, add `--native 3472x2266` to both commands. Use the actual pixel dimensions of the other Mac's screen.
4. Paste the printed launch option into **macOS Steam → Forza Horizon 6 → Properties → General → Launch Options**. Keep the quotes and `%command%`. Press **Play** in the existing Steam library entry.

The installer stages the checked-in payload after checking its SHA-256, backs up the touched configuration locally, and refuses an unknown Wine build or a conflicting DLL. It only raises player-car detail, streamed textures and shader quality, and disables dynamic optimization and motion blur. FH6's bundled FSR 3 remains the working choice. The earlier FSR 4/2× OptiScaler experiment hit FHE21 and remains outside this profile. The audio path passed a synthetic 12-channel test on an M5 Max; exact in-game tire audio and cold native Steam launch had yet to be verified on the original machine.

## Rebuild or revert

The binaries in `payload/arm64-r4.zip` contain only the rebuilt Wine audio modules and the local Unicode dialog helper. [Wine license](src/LICENSE) and [LGPL 2.1 text](src/COPYING.LIB) apply to the Wine modules. Their corresponding source is [Wine 11.18](https://dl.winehq.org/wine/source/11.x/wine-11.18.tar.xz) plus [Highball's pinned engine patch tree](https://github.com/gauthierpiarrette/highball-engine/tree/8b315e7), including audio patches 0022 and 0023. The script verifies both source archives before applying the patches. The dialog and test sources are under `src/` and use this repository's MIT license.

To rebuild, use macOS Command Line Tools, Bison and the universal [llvm-mingw 20261006 toolchain](https://github.com/mstorsjo/llvm-mingw/releases/tag/20261006). Run `LLVM_MINGW_BIN=/path/to/llvm-mingw/bin bash fh6/arm64/build.sh`. It builds into a temporary directory.

To revert, quit FH6 and its Windows Steam, remove the new macOS Steam launch option or restore its previous value, and use the installer's printed backup to restore `bottle.json`, `user.reg`, and `UserConfigSelections`. Remove the installed `version.dll` only if it is the helper from this package. The dormant overlay can stay on disk once the old bottle config is restored.
