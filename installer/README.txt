MAS Reverb — masterastakhovsound
================================

Установка
1. Распакуй архив.
2. Нажми правой кнопкой на «Install MAS Reverb.command» → «Открыть» → «Открыть»
   (правый клик нужен один раз, потому что скрипт не подписан Apple).
3. Перезапусти DAW. В Logic: Audio FX → Audio Units → masterastakhovsound → MAS Reverb.

Установка вручную (Терминал)
  cp -R "MAS Reverb.component" ~/Library/Audio/Plug-Ins/Components/
  cp -R "MAS Reverb.vst3" ~/Library/Audio/Plug-Ins/VST3/
  xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/"MAS Reverb.component" ~/Library/Audio/Plug-Ins/VST3/"MAS Reverb.vst3"

Ручки
  PRE-DELAY   0–250 мс   задержка перед хвостом
  SIZE        0–100 %    размер пространства (длина линий, плотность)
  DECAY       0.2–20 с   время реверберации (RT60)
  HF DAMPING  0–100 %    затухание верха в хвосте: больше — темнее
  LOW CUT     Off–1 кГц  срез низа в ревере
  MOD         0–100 %    модуляция хвоста (гладкость, «хорус»)
  MIX         0–100 %    сухой/обработанный. На посыле ставь 100 %
Двойной клик по ручке — значение по умолчанию.

Плагин подписан ad-hoc: для своего компьютера этого достаточно.
