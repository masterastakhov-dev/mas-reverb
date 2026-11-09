#!/bin/bash
# MASTER VERB / ALENTEY VERB — build and install on this Mac (no full Xcode needed).
# Right-click this file -> Open (first time), then wait.

cd "$(dirname "$0")" || exit 1

pause() { echo ""; read -n 1 -s -r -p "Нажми любую клавишу, чтобы закрыть окно..."; echo ""; }
fail()  { echo ""; echo "ОШИБКА: $1"; pause; exit 1; }

echo "=== MASTER VERB и ALENTEY VERB: сборка на этом Mac ==="

# 1. Apple Command Line Tools (compiler + SDK)
if ! xcode-select -p >/dev/null 2>&1; then
    echo "Нужны Command Line Tools от Apple."
    echo "Сейчас откроется окно установки: нажми «Установить», дождись окончания"
    echo "и запусти этот файл ещё раз."
    xcode-select --install >/dev/null 2>&1 || true
    pause; exit 1
fi

# 2. CMake (use installed one, otherwise download a portable copy)
CMAKE="$(command -v cmake || true)"
for c in /Applications/CMake.app/Contents/bin/cmake /opt/homebrew/bin/cmake /usr/local/bin/cmake; do
    [ -z "$CMAKE" ] && [ -x "$c" ] && CMAKE="$c"
done
if [ -z "$CMAKE" ]; then
    V=3.31.6
    echo "Скачиваю CMake $V..."
    mkdir -p .tools
    curl -fL --progress-bar -o .tools/cmake.tar.gz \
        "https://github.com/Kitware/CMake/releases/download/v$V/cmake-$V-macos-universal.tar.gz" \
        || fail "не удалось скачать CMake (проверь интернет)"
    tar -xzf .tools/cmake.tar.gz -C .tools || fail "не удалось распаковать CMake"
    CMAKE="$PWD/.tools/cmake-$V-macos-universal/CMake.app/Contents/bin/cmake"
fi
echo "CMake: $CMAKE"

# 3. Configure + build (first run downloads JUCE, takes a few minutes)
"$CMAKE" -B build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES="$(uname -m)" || fail "настройка проекта не удалась"
"$CMAKE" --build build --target MasterVerb_AU MasterVerb_VST3 AlenteyVerb_AU AlenteyVerb_VST3 \
    -j "$(sysctl -n hw.ncpu)" || fail "сборка не удалась — пришли Claude текст из этого окна"

# 4. Sign, install and validate every variant
AU_DIR="$HOME/Library/Audio/Plug-Ins/Components"
VST3_DIR="$HOME/Library/Audio/Plug-Ins/VST3"
mkdir -p "$AU_DIR" "$VST3_DIR"

install_variant() {   # <cmake target> <product name> <AU code>
    local ART="build/$1_artefacts/Release" NAME="$2" CODE="$3"
    codesign --force --deep --sign - "$ART/AU/$NAME.component" >/dev/null 2>&1
    codesign --force --deep --sign - "$ART/VST3/$NAME.vst3" >/dev/null 2>&1
    rm -rf "$AU_DIR/$NAME.component" "$VST3_DIR/$NAME.vst3"
    cp -R "$ART/AU/$NAME.component" "$AU_DIR/"
    cp -R "$ART/VST3/$NAME.vst3" "$VST3_DIR/"
    killall -9 AudioComponentRegistrar >/dev/null 2>&1 || true
    sleep 2
    echo ""
    echo "Проверка $NAME (auval)..."
    if auval -v aufx "$CODE" Mast 2>&1 | tee "build/auval-$CODE.log" | grep -q "AU VALIDATION SUCCEEDED"; then
        echo "AU VALIDATION SUCCEEDED"
    else
        echo "auval сообщил о проблеме — отчёт: build/auval-$CODE.log"
    fi
    echo "Установлен: $AU_DIR/$NAME.component и $VST3_DIR/$NAME.vst3"
}

install_variant MasterVerb  "MASTER VERB"  MVrb
install_variant AlenteyVerb "ALENTEY VERB" AVrb

echo ""
echo "Готово! Перезапусти DAW. В Logic: Audio FX > Audio Units > masterastakhovsound"
pause
