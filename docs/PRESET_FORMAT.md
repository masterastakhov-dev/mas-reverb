# MASTER SPACE: формат пресетов (schema 1)

Пресет — это текстовый файл JSON в UTF-8, по одному пресету на файл, расширение `.json`.

```json
{
  "format": "MASTER SPACE Preset",
  "schemaVersion": 1,
  "pluginVersion": "1.2.0",
  "name": "Studio A Natural",
  "category": "Studio Rooms",
  "author": "masterastakhovsound",
  "description": "Neutral tracking room, about 0.7 s.",
  "engine": "1.2",
  "parameters": {
    "engine": "1.2",
    "algo": "Studio Room",
    "routing": "Aux",
    "decay": 0.72,
    "matfront": "Plaster",
    "eron": true
  },
  "irFile": "/path/to/impulse.wav"
}
```

## Поля

| Поле | Обязательно | Смысл |
|---|---|---|
| `format` | да (или отсутствует) | Всегда `MASTER SPACE Preset`. Файл с другим значением не загружается. |
| `schemaVersion` | да | Версия формата. Сейчас 1. Если поля нет, файл считается версией 0. |
| `pluginVersion` | нет | Версия плагина, которая записала файл. |
| `name`, `category`, `author`, `description` | name — да | Метаданные. Категория — одна из перечисленных ниже. |
| `engine` | нет | Информационная копия `parameters.engine`. |
| `parameters` | да | Все звуковые параметры по их ID (полный список ниже). |
| `irFile` | нет | Путь к импульсу, только для режима IR. |

Категории: Studio Rooms, Chambers, Halls, Plates, Vocal Spaces, Drum Spaces, Ambiences, Early Reflections, Creative Spaces, Signature Collection, User Presets (без категории). Favorites — это не категория: список избранного хранится отдельно.

## Значения

Все значения записываются в тех же единицах, что видны в плагине: проценты 0–100, мс, с, дБ, Гц, м, множители.

- Списки выбора записываются текстом (`"Studio Room"`, `"Plaster"`, `"1.1 Legacy"`). Индекс-число тоже принимается.
- Переключатели записываются как `true` / `false`.

Параметры: `engine algo mode routing depth decay size predelay erlevel latelevel diffusion density shape spread width mix output locklevel locktone lockdecay lockwidth roomw roomd roomh srcx lisx lisy matfront matback matleft matright matfloor matceiling eron erdensity erdiffusion ercolor erwidth erstart erstop erpredelay lateon latedelay latewidth latecolor lfmult lmmult hmmult hfmult xlow xmid xhigh lowcut highcut modchar modrate moddepth modrandom modsmooth balance crossfeed decorr irlength`.

## Совместимость и миграция

Как обрабатываются отклонения от формата:

- **Отсутствующий параметр** получает значение по умолчанию.
- **Отсутствующий `engine`** означает «1.1 Legacy», если файл записан плагином версии младше 1.2 или если в корне файла стоит `"engine": "1.1 Legacy"`. Иначе — «1.2».
- **Неизвестные параметры** (из будущих версий) игнорируются.
- **Значения вне диапазона и нечитаемые значения** приводятся к диапазону или заменяются значением по умолчанию.
- **Файл новее программы** (`schemaVersion` больше 1) загружается, то, что программа не знает, пропускается.
- **Переименованные параметры** читаются под новым ID через таблицу `renamedParameters()` в `PresetManager.cpp`. Сейчас она пуста.

Обо всех таких исправлениях плагин сообщает в браузере пресетов (строка под описанием).

## Где лежат файлы

| | macOS | Windows |
|---|---|---|
| Пользовательские | `~/Library/Audio/Presets/masterastakhovsound/MASTER SPACE/User/<Категория>/` | `%APPDATA%\masterastakhovsound\MASTER SPACE\Presets\User\` |
| Коллекции (только чтение) | `…/MASTER SPACE/Collections/<Коллекция>/` | то же |
| Избранное, автор по умолчанию | `…/MASTER SPACE/favorites.json`, `settings.json` | то же |

Фабричные пресеты встроены в плагин и не могут быть изменены. Обновление фабричной библиотеки не трогает пользовательские файлы.

Чтобы добавить новую библиотеку (например, Signature Collection), достаточно положить JSON-файлы в `Collections/<название>/` — код DSP менять не нужно. Фабричные пресеты собираются из `presets/factory/*.json` при сборке. Генератор: `tools/make_factory_presets.py`, проверка громкости и стабильности: `tools/check_presets.py`.

## Надёжность записи

- Файл сначала пишется во временный скрытый файл рядом с целевым, проверяется обратным чтением и только потом переименовывается поверх целевого. Прерванная запись не портит существующий пресет.
- Файлы не читаются и не пишутся из аудиопотока.
- Повреждённые файлы в библиотеке пропускаются, их список показывается внизу вкладки PRESETS.

## Состояние в проекте DAW

В проекте сохраняются:

- все параметры;
- `stateVersion` (2 = 1.2);
- имя и ключ текущего пресета;
- второй слот A/B, активный слот, включён ли MATCH и его поправка.

Проект без параметра `engine` (сохранённый в 1.1) открывается в режиме «1.1 Legacy». Нечитаемое состояние игнорируется: текущие настройки сохраняются.
