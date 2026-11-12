# Get started
- Install VSCodium (or VSCode)
    - Open this file with it and press `[Ctrl]+[Shift]+[V]`
- Install Arduino IDE Legacy 1.8.19 (or latest legacy)
    - Open and press `[Ctrl]+[Shift]+[I]`
    - Close
- Windows:
    - Install MSys2
    - Use UCRT64
    - Upgrade the system: `pacman -Syu`
    - Upgrade packages: `pacman -Su`
    - Install developer packages: `pacman -S git unzip mingw-w64-ucrt-x86_64-toolchain base-devel mingw-w64-ucrt-x86_64-SDL2 mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-astyle mingw-w64-ucrt-x86_64-ccache`
- macOS (Homebrew): `brew install git sdl2 cmake make astyle ccache`
- Linux:
    - Debian/Ubuntu: `sudo apt install git unzip build-essential cmake libsdl2-dev astyle ccache`
    - Arch: `sudo pacman -S git unzip base-devel cmake sdl2 astyle ccache`
    - Fedora: `sudo dnf install @development-tools git unzip cmake SDL2-devel astyle ccache`
- Verify installed versions: `git --version; sdl2-config --version; cmake --version; gcc --version; g++ --version; ccache --version`
- VSCodium (or VSCode) install extensions:
    - `C/C++` (v1.24.4 via .vsix for VSCodium or latest for VSCode)
    - `Serial Monitor` (via .vsix for VSCodium or latest for VSCode)
    - `Arduino Community Edition v0.7.2` (via .vsix for VSCodium or latest for VSCode)
    - `C/C++ Themes`
    - `CMake Tools`
    - Install `esp32 by Espressif Systems Version 3.3.2` board:
        - Press `[Ctrl]+[Shift]+[P]` and run `Arduino: Library Manager`
        - Press `[Ctrl]+[Shift]+[P]` and run `Arduino: Board Manager` to install these board

# Bash in VSCodium Terminal on Windows
- Add this JSON code into settings.json, press `[Ctrl]+[Shift]+[P]`, then `Preferences: Open User Settings (JSON)`, so add:
```
  "terminal.integrated.profiles.windows": {
        "UCRT64 Bash": {
            "source": "Bash",
            "path": "C:/msys64/usr/bin/bash.exe",
            "args": [
                "--login",
                "-i"
            ],
            "env": {
                "MSYSTEM": "UCRT64",
                "CHERE_INVOKING": "1",
                "MSYS2_PATH_TYPE": "inherit"
            }
        }
    },
    "terminal.integrated.defaultProfile.windows": "UCRT64 Bash",
```
- From now on, use just the `UCRT64 Bash` terminal (press `[Ctrl]+[']`) into `coletor` directory.

# Prepare to ccache operational on Windows
```
mkdir /usr/lib/ccache/
cd /usr/lib/ccache/
ln -s /ucrt64/bin/ccache.exe c++.exe
ln -s /ucrt64/bin/ccache.exe c89-gcc.exe
ln -s /ucrt64/bin/ccache.exe c99-gcc.exe
ln -s /ucrt64/bin/ccache.exe cc.exe
ln -s /ucrt64/bin/ccache.exe g++.exe
ln -s /ucrt64/bin/ccache.exe g++-14.exe
ln -s /ucrt64/bin/ccache.exe g++-15.exe
ln -s /ucrt64/bin/ccache.exe gcc.exe
ln -s /ucrt64/bin/ccache.exe gcc-13.exe
ln -s /ucrt64/bin/ccache.exe gcc-14.exe
ln -s /ucrt64/bin/ccache.exe gcc-15.exe
ln -s /ucrt64/bin/ccache.exe x86_64-linux-gnu-g++.exe
ln -s /ucrt64/bin/ccache.exe x86_64-linux-gnu-g++-14.exe
ln -s /ucrt64/bin/ccache.exe x86_64-linux-gnu-g++-15.exe
ln -s /ucrt64/bin/ccache.exe x86_64-linux-gnu-gcc.exe
ln -s /ucrt64/bin/ccache.exe x86_64-linux-gnu-gcc-13.exe
ln -s /ucrt64/bin/ccache.exe x86_64-linux-gnu-gcc-14.exe
ln -s /ucrt64/bin/ccache.exe x86_64-linux-gnu-gcc-15.exe
```

# Apply patch to boards.txt and platform.txt files to arduino board manager directory
- Go into `coletor` directory
- Linux:
```
for boards in ~/.arduino15/packages/esp32/hardware/esp32/*/boards.txt; do
    patch $boards boards_t_display_16MB_SPIFFS.patch
done
for platform in ~/.arduino15/packages/esp32/hardware/esp32/*/platform.txt; do
    patch $platform platform_ccache_and_rtos.patch
done
```
- Windows:
```
for boards in /c/users/"$USER"/AppData/Local/Arduino15/packages/esp32/hardware/esp32/*/boards.txt; do
    patch "$boards" boards_t_display_16MB_SPIFFS.patch
done
for platform in /c/users/"$USER"/AppData/Local/Arduino15/packages/esp32/hardware/esp32/*/platform.txt; do
    patch "$platform" platform_ccache_and_rtos.patch
    sed -i 's/^\(compiler\..*\.cmd={compiler\.prefix}[^\.]*\)$/\1.bat/' "$platform"
done
```

# Configure CCache for ESP32 compiler
- For Linux:
```
cd ~/.arduino15/packages/esp32/tools/esp-x32/*/bin/
for compiler in xtensa-esp*; do
    echo -e '#!/bin/bash\n\n/usr/bin/ccache' "`pwd`/$compiler" '$@' > ccache-$compiler
    chmod +x ccache-$compiler
done
```
- For Windows:
```
cd /c/users/"$USER"/AppData/Local/Arduino15/packages/esp32/tools/esp-x32/*/bin
for compiler in xtensa-esp*; do
    echo -e 'set MSYSTEM=UCRT64\nset CHERE_INVOKING=1\nset MSYS2_PATH_TYPE=inherit\nC:/msys64/usr/bin/bash.exe -l -c "ccache '"'`pwd`/$compiler'"' `echo '"'%*' | tr '\\\\' '/'"'`' > ccache-${compiler%.exe}.bat
done
```
- Remove ccache on Windows (recommended - bad performance with ccache on Windows):
```
for platform in /c/users/"$USER"/AppData/Local/Arduino15/packages/esp32/hardware/esp32/*/platform.txt; do
    sed -i 's/^\(compiler\..*\.cmd={compiler\.prefix}[^\.]*\)\.bat$/\1/;s/ccache-//' "$platform"
done
```
- To monitor ccache statistics:
```
while true; do clear; ccache.exe -s; sleep.exe 1; done
```

# Clone LVGL with SDL port to VSCode
- Go into `coletor` directory
```
cd ..
git clone --recursive https://github.com/lvgl/lv_port_pc_vscode
```

# Apply patch to simulator.code-workspace to use bash and ccache on Windows
```
cd lv_port_pc_vscode
cp -av ../coletor/* ../coletor/.vscode .
patch -p1 < simulator.code-workspace.patch
```

# Remove unnecessary diretories
```
rm -rf config FreeRTOS
```

# Change HAL `src` to `src_hal` and `lv_conf.h` to support HAL simulator with SDL2 and ESP32
```
patch -p1 < lv_conf.h.patch
sed -i 's/src\/main.c/main.cpp bletag.cpp/g;s/src/src_hal/g' CMakeLists.txt
mv src src_hal
```

# Change default font to bold and add more characters
```
wget -qO- https://raw.githubusercontent.com/nvm-sh/nvm/v0.40.3/install.sh | bash
export NVM_DIR="$HOME/.nvm"
[ -s "$NVM_DIR/nvm.sh" ] && \. "$NVM_DIR/nvm.sh"  # This loads nvm
[ -s "$NVM_DIR/bash_completion" ] && \. "$NVM_DIR/bash_completion"  # This loads nvm bash_completion
nvm install node
npm i lv_font_conv -g

patch -p1 < built_in_font_gen.py.patch
cp -av Montserrat-Bold.ttf ./lvgl/scripts/built_in_font/
cd lvgl/scripts/built_in_font/
sed -i 's/.\/built_in_font_gen.py/python3 built_in_font_gen.py/;s/\.encode(.utf-8.)//' generate_all.py
python3 generate_all.py
cd ../../..
```

# Arduino libraries to compile for ESP32
- [Ctrl]+[Shift]+[P] => `Arduino: Library Manager`
    - `lvgl` by kisvegabor
    - `TFT_eSPI` by Bodmer
    - `NimBLE-Arduino` by H2zero
    - `ESP32-BLE-Keyboard` from github.com/JuniorPolegato and make NimBLE enable (`#define USE_NIMBLE` on `BleKeyboard.h`)
        ```
        wget https://github.com/JuniorPolegato/ESP32-BLE-Keyboard/archive/refs/heads/master.zip
        unzip master.zip
        rm master.zip
	    sed -i 's/#undef USE_NIMBLE/#define USE_NIMBLE/' ESP32-BLE-Keyboard-master/BleKeyboard.h

        # Windows
        mv ESP32-BLE-Keyboard-master /c/users/"$USER"/Documents/Arduino/libraries/ESP32-BLE-Keyboard

        # Linux
        mv ESP32-BLE-Keyboard-master ~/Arduino/libraries/ESP32-BLE-Keyboard
        ```

# Open workspace with VSCodium
```
codium simulator.code-workspace
```


# To build and run
- Linux: `[Ctrl]+[F5]` (just `[F5]` for debug)
- Windows: `[Ctrl]+[Shift]+[B]`
- ESP32: `[Ctrl]+[Alt]+[U]` (`[Ctrl]+[Alt]+[I]` just for build)
