# MASTER VERB / ALENTEY VERB

Алгоритмический ревербератор **masterastakhovsound** — Audio Unit и VST3 для macOS.
Из одного кода собираются два плагина с разными названиями: **MASTER VERB** и **ALENTEY VERB**
(варианты задаются в `CMakeLists.txt` через `mas_add_variant`).

Ручки: Pre-Delay, Size, Decay (RT60), HF Damping, Low Cut, Mod, Mix.

## Скачать

Каждый push в `main` собирает плагин на macOS (GitHub Actions) и проверяет его через `auval`.
Готовые архивы: вкладка **Actions** → последний успешный запуск → артефакты **MASTER-VERB-macOS** и **ALENTEY-VERB-macOS**.
В каждом — `.component`, `.vst3`, установщик и инструкция.

## Сборка на своём Mac одним файлом

Скачай репозиторий (Code → Download ZIP), распакуй и открой `build-mac.command`
(правый клик → Открыть). Скрипт сам поставит недостающее (Command Line Tools, CMake),
соберёт AU + VST3, установит их в папки плагинов и проверит через `auval`.

## Сборка вручную

Нужны Xcode и CMake (`brew install cmake`).

```bash
cmake -B build -G Xcode -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build --config Release --target MasterVerb_AU MasterVerb_VST3 AlenteyVerb_AU AlenteyVerb_VST3
```

Плагины появятся в `build/MasterVerb_artefacts/Release/` и `build/AlenteyVerb_artefacts/Release/`.

## Алгоритм

Собственная реализация «восьмёрочного» бака по открытой схеме из статьи
J. Dattorro, *Effect Design, Part 1* (JAES, 1997): независимая стерео-диффузия входа,
затухание, откалиброванное по RT60, масштабируемый размер, демпфирование ВЧ,
срез НЧ, pre-delay и модуляция двумя LFO с эрмитовой интерполяцией.
Код DSP — `Source/MasterVerbEngine.h`, без зависимостей.

Офлайн-тест движка (Linux/macOS):

```bash
g++ -O2 -std=c++17 -o tests/render tests/render.cpp
python3 tests/analyze.py   # RT60, стерео-корреляция, стабильность
```

## Лицензия фреймворка

Плагин собран на [JUCE](https://juce.com). Для личного использования ограничений нет;
перед продажей или публичной раздачей проверь условия лицензии JUCE.
