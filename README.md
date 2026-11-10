# masterastakhovsound plugins

Audio Unit и VST3 для macOS (Apple Silicon + Intel). В репозитории три плагина:

| Плагин | Папка | AU-код | Что это |
|---|---|---|---|
| **MASTER SPACE 1.1** | `Source/MasterSpace` | `MSpc` | пространственный ревербератор: виртуальная комната, Depth, Early/Late, локи |
| **MASTER SPACE 1.0** | `Source/MasterSpaceRef` | `MSp0` | версия 1.0 без изменений, отдельным плагином для A/B |
| **MASTER VERB 1.2** | `Source/MasterVerb` | `MVrb` | простой алгоритмический хол |
| **ALENTEY VERB 1.2** | `Source/MasterVerb` | `AVrb` | тот же MASTER VERB под другим именем |
| **ALEX HOOK VERB 1.2** | `Source/MasterVerb` | `AHvb` | тот же MASTER VERB под другим именем |

## MASTER SPACE — «Don't add reverb. Create space.»

- **Spatial DNA** — комната-«коробка» с размерами W/L/H, шесть поверхностей с материалами
  (Absorber, Curtain, Wood, Plaster, Concrete, Glass), положение источника и слушателя.
  Ранние отражения считаются методом мнимых источников (до 5-го порядка), с поглощением
  материалов по полосам, затуханием в воздухе, рассеиванием на поверхностях и «тенью головы».
- **DEPTH** — двигает источник вглубь комнаты; согласованно меняются время и уровень отражений,
  прямой/отражённый, верх прямого звука и ширина.
- **Locks** — Level, Tone, Decay, Width: что держать постоянным при движении Depth.
- **Insert / Aux** — в Insert прямой звук тоже уходит в комнату, в Aux — только пространство (100 % wet).
- **Late** — FDN на 16 линий, 4-полосный RT60 (0.1–4×) с кроссоверами, Shape/Spread нарастания,
  отдельные Density и Diffusion, модуляция (Smooth / Random / Vintage / Modern). Алгоритмы Studio Room, Chamber, Hall.
- **IR** — свой импульс (WAV/AIFF/FLAC), свёртка без задержки.
- Нулевая латентность, 44.1–96 кГц. Страницы: MAIN, SPACE, EARLY, LATE, DECAY EQ, MODULATION, STEREO, ADVANCED.

Движок — `Source/MasterSpace/MSEngine.h` (без зависимостей от JUCE).

### 1.1: без металлической окраски

- Все всепропускные звенья читают задержку кубической интерполяцией. С линейной интерполяцией
  звено перестаёт быть всепропускным на верхах и превращается в гребёнку с шагом 1/задержка;
  в петле обратной связи эта гребёнка копится каждый проход и слышна как металл.
- Ранние отражения: вместо рекурсивной цепочки диффузоров (одинаковый фильтр на всех отражениях,
  то есть фиксированная гребёнка) каждое отражение превращается в своё облачко прихода, разное
  в левом и правом канале; рассеяние только выше ~1,5 кГц, низ остаётся зеркальным.
- Хвост: два модулированных поглощающих всепропускных звена на линию вместо одного статичного,
  минимальная суммарная длина петли (0,55 с) против редкой сетки мод в маленьких комнатах,
  затухание верха задано явно (дБ/с) и не зависит от частоты дискретизации.
- Объём, плотность, глубина, локи, время затухания и громкость откалиброваны по 1.0.

Тесты окраски: `python3 tests/metal.py` (сравнение с 1.0: `MS_OLD_RENDER=<путь к render_space 1.0>`).

## Скачать

Каждый push в `main` собирает MASTER SPACE и MASTER SPACE 1.0 (для A/B) на macOS (GitHub Actions) и проверяет его через `auval -strict`.
Готовый архив: вкладка **Actions** → последний успешный запуск → артефакты **MASTER-SPACE-macOS** и **MASTER-SPACE-1.0-AB-macOS**
(`.component`, `.vst3`, установщик и инструкция).

## Сборка на своём Mac одним файлом

Скачай репозиторий (Code → Download ZIP), распакуй и открой `build-mac.command`
(правый клик → Открыть). Скрипт сам поставит недостающее (Command Line Tools, CMake),
соберёт AU + VST3, установит их и проверит через `auval`.

## Сборка вручную

Нужны Xcode и CMake (`brew install cmake`).

```bash
cmake -B build -G Xcode -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build --config Release --target MasterSpace_AU MasterSpace_VST3
# MASTER VERB / ALENTEY VERB / ALEX HOOK VERB: MasterVerb_AU, AlenteyVerb_AU, AlexHookVerb_AU (+ _VST3)
```

Плагины появятся в `build/<Target>_artefacts/Release/`.

## Офлайн-тесты движков (Linux/macOS)

```bash
pip install numpy scipy
g++ -O2 -std=c++17 -o tests/render_space tests/render_space.cpp
python3 tests/analyze_space.py   # depth, локи, материалы, размер комнаты, гребёнка, автоматизация, CPU
python3 tests/metal.py           # металлическая окраска: звон Early/Late, моды, регулярность задержек

g++ -O2 -std=c++17 -o tests/render tests/render.cpp
python3 tests/analyze.py         # MASTER VERB: RT60, стерео, стабильность
```

## Лицензия фреймворка

Плагины собраны на [JUCE](https://juce.com). Для личного использования ограничений нет;
перед продажей или публичной раздачей проверь условия лицензии JUCE.
