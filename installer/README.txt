MASTER VERB — masterastakhovsound
================================

Установка
1. Распакуй архив.
2. Нажми правой кнопкой на «Install MASTER VERB.command» → «Открыть» → «Открыть».
   Если macOS всё равно не даёт открыть: Системные настройки → Конфиденциальность
   и безопасность → внизу «Всё равно открыть» (скрипт не подписан Apple).
3. Перезапусти DAW. В Logic: Audio FX → Audio Units → masterastakhovsound → MASTER VERB.

Установка вручную (Терминал)
  cp -R "MASTER VERB.component" ~/Library/Audio/Plug-Ins/Components/
  cp -R "MASTER VERB.vst3" ~/Library/Audio/Plug-Ins/VST3/
  xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/"MASTER VERB.component" ~/Library/Audio/Plug-Ins/VST3/"MASTER VERB.vst3"

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
