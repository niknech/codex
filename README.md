# Telegram Keyword Deleter (TDLib, Windows, Console)

Консольное приложение на Python для Windows, которое ищет и удаляет сообщения в Telegram по ключевым словам через **TDLib** (официальный клиентский API Telegram).

## Возможности

- Авторизация по:
  - номеру телефона,
  - коду из Telegram/SMS,
  - 2FA-паролю (если включен).
- Хранение сессии в `./tdlib_data`.
- Чтение ключевых слов из `keywords.txt` (UTF-8, по одному слову/фразе на строку).
- Поиск совпадений по подстроке с нормализацией:
  - `lower()`
  - замена `ё` → `е`
- Выбор области обработки:
  - конкретный чат из списка,
  - все чаты,
  - чат по username.
- Выбор диапазона дат.
- `dry-run` режим (ничего не удаляет, только показывает найденное).
- Режим `delete-for-all` (пытается удалить для всех, если разрешено; иначе пропускает).
- Логи удаления/совпадений в `deleted_log.csv`.
- Базовая устойчивость: обработка ошибок TDLib, небольшие задержки, корректное завершение.

---

## Требования

- Windows 10/11
- Python 3.10+
- TDLib (`tdjson.dll`) и зависимые DLL
- Telegram `api_id` и `api_hash`

---

## Где получить `api_id` и `api_hash`

1. Откройте: https://my.telegram.org
2. Войдите под своим номером.
3. Перейдите в **API development tools**.
4. Создайте приложение и получите:
   - `api_id`
   - `api_hash`

---

## Установка TDLib для Windows

Нужна библиотека `tdjson.dll` + зависимости (например, `libcrypto-*.dll`, `zlib1.dll` и т.д., в зависимости от сборки).

### Вариант A (проще): скачать готовую сборку

1. Скачайте готовые бинарники TDLib для Windows (из проверенного источника/релиза).
2. Поместите `tdjson.dll` и все зависимые DLL в папку проекта, например:
   ```
   .\tdlib\
     tdjson.dll
     ...другие dll...
   ```
3. Укажите путь в `config.json`:
   ```json
   "tdlib_library_path": "./tdlib/tdjson.dll"
   ```

### Вариант B: собрать TDLib самостоятельно

Официальный репозиторий TDLib содержит инструкции сборки:
https://github.com/tdlib/td

После сборки:
- найдите `tdjson.dll`,
- скопируйте его и зависимости в проект,
- пропишите путь в `config.json`.

---

## Установка проекта

```bash
python -m venv .venv
.venv\Scripts\activate
python -m pip install --upgrade pip
```

Внешних Python-зависимостей нет: используется стандартная библиотека.

---

## Настройка

1. Скопируйте пример конфига:
   ```bash
   copy config.json.example config.json
   ```
2. Отредактируйте `config.json`:
   - `api_id`
   - `api_hash`
   - `tdlib_library_path`
3. Заполните `keywords.txt` (UTF-8, по одному ключевому слову/фразе в строке).

Пример `keywords.txt`:
```txt
спам
реклама
test phrase
```

---

## Запуск

```bash
python app.py --config config.json --keywords keywords.txt --csv deleted_log.csv
```

Параметры:
- `--config` — путь к JSON-конфигу (по умолчанию `config.json`)
- `--keywords` — путь к файлу ключевых слов (по умолчанию `keywords.txt`)
- `--csv` — путь к CSV-логу (по умолчанию `deleted_log.csv`)

---

## Сценарий работы

При запуске приложение:

1. Подключается к TDLib.
2. Выполняет авторизацию (если сессии нет):
   - телефон,
   - код,
   - 2FA.
3. Предлагает выбрать чат(ы):
   - один из списка,
   - все,
   - по username.
4. Запрашивает диапазон дат и режимы (`dry-run`, `delete-for-all`, подробный лог).
5. Постранично получает историю чатов (`getChatHistory`), проверяет:
   - текстовые сообщения,
   - caption у медиа.
6. При совпадении:
   - в `dry-run`: печатает совпадение,
   - иначе удаляет сообщение.
7. Добавляет запись в `deleted_log.csv`.

---

## Формат `deleted_log.csv`

Колонки:
- `chat_id`
- `chat_title`
- `message_id`
- `date`
- `matched_keyword`
- `text_snippet`

---

## Пример `config.json`

```json
{
  "api_id": 123456,
  "api_hash": "0123456789abcdef0123456789abcdef",
  "tdlib_library_path": "./tdlib/tdjson.dll",
  "database_directory": "./tdlib_data",
  "files_directory": "./tdlib_data/files",
  "system_language_code": "ru",
  "device_model": "Windows PC",
  "system_version": "Windows 10",
  "application_version": "1.0",
  "use_test_dc": false
}
```

---

## Компиляция в `.exe` (опционально)

Можно собрать standalone exe через PyInstaller.

1. Установите:
   ```bash
   pip install pyinstaller
   ```
2. Соберите:
   ```bash
   pyinstaller --onefile app.py
   ```
3. Скопируйте рядом с `dist\app.exe`:
   - `config.json`
   - `keywords.txt`
   - папку/файлы TDLib DLL

> Важно: `tdjson.dll` и его зависимости должны быть доступны по пути из `config.json`.

---

## Ограничения и замечания

- Удаление "для всех" зависит от прав/тайминга и типа чата.
- Для некоторых сообщений Telegram не разрешит удаление для всех — такие сообщения пропускаются в режиме `delete-for-all`.
- Используйте аккуратно: действия необратимы.

---

## Файлы проекта

- `app.py` — основной код приложения.
- `config.json.example` — шаблон конфигурации.
- `keywords.txt` — ключевые слова (создайте/заполните).
- `deleted_log.csv` — создаётся автоматически в процессе.
