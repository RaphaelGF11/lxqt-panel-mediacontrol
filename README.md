# Media Controls for LXQt Panel

**This project was generated with AI using OpenAI Codex.**

Media Controls is a native C++ and Qt widget for LXQt Panel. It controls MPRIS-compatible media players directly over D-Bus. The default controls are left-click for play or pause, scroll up for the next track, and scroll down for the previous track. When several players are open, the widget prioritizes active playback.

The configuration dialog supports custom play and pause icons, icon size, widget dimensions, and mouse actions. The dialog and the widget’s name in the panel follow the session language. English is the fallback.

## Supported environments

| Environment | Target versions | Architectures | Linkers |
| --- | --- | --- | --- |
| GNU/Linux, Ubuntu 24.04 ABI | LXQt Panel 1.4, Qt 5.15, glibc 2.39 | amd64, i386, arm64, armhf, riscv64, ppc64el, s390x | GNU ld.bfd, GNU gold, LLVM LLD, and mold. Gold is unavailable for RISC-V. |
| Native Termux, standard `com.termux` prefix | LXQt Panel 2.5.1, Qt 6.12, Bionic, Android API 24 or later | aarch64, arm, i686, x86_64 | LLVM LLD from Android NDK r30 |

The i386 builds require a compatible LXQt Panel 1.4 installation. Ubuntu 24.04 does not provide that panel package for i386. A panel running inside a Termux proot distribution uses the package for the guest distribution’s ABI.

## Installation

Select a compatible package from the [GitHub releases](../../releases). Only one Media Controls package should be installed at a time.

The GNU/Linux package ending in `_all.deb` contains binaries for all seven GNU/Linux architectures. The panel loads the GNU ld.bfd build for its architecture. Separate packages are available for each architecture and linker.

```sh
# GNU/Linux, multi-architecture package
pkexec apt install ./lxqt-panel-mediacontrol-gnu_1.2.0_all.deb

# GNU/Linux, amd64 build linked with mold
pkexec apt install ./lxqt-panel-mediacontrol-mold_1.2.0_amd64.deb

# Native Termux, arm64 build with the X11 repository enabled
apt install ./lxqt-panel-mediacontrol-lld_1.2.0_aarch64.deb
```

After installation, restart LXQt Panel. Use **Configure Panel → Widgets → +** to add **Media Controls**. Existing widget settings are retained across upgrades. Right-click the widget and select its configuration entry to customize it.

## Translations

The project includes 34 complete Qt Linguist catalogs covering all 35 widget strings. English is built into the source as the fallback. The catalogs include Arabic, Bengali, Bulgarian, Catalan, Chinese (Simplified and Traditional), Czech, Danish, Dutch, Finnish, French, German, Greek, Hebrew, Hindi, Hungarian, Indonesian, Italian, Japanese, Korean, Norwegian Bokmål, Persian, Polish, Portuguese (Portugal and Brazil), Romanian, Russian, Slovak, Spanish, Swedish, Thai, Turkish, Ukrainian, and Vietnamese.

The `.ts` files in [translations](translations) are editable in Qt Linguist. CMake compiles them with `lrelease` and embeds the resulting catalogs in the module. The localized plugin names and descriptions are stored in [mediacontrol.desktop](mediacontrol.desktop).

## Building from source

A build requires CMake, a C++17 compiler, the target LXQt Panel headers, and the Widgets, D-Bus, and LinguistTools components of the same Qt major version as the panel. Set `LXQT_MEDIA_QT_MAJOR` to `6` for Qt 6. The install library directory must match the panel’s plugin directory.

```sh
nice -n 19 ionice -c 3 cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DLXQT_MEDIA_QT_MAJOR=5 -DCMAKE_INSTALL_PREFIX=/usr
nice -n 19 ionice -c 3 cmake --build build --parallel 1
```

## License

The widget source code is distributed under the [MIT License](LICENSE).
