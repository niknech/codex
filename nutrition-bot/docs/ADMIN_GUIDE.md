# Администрирование Telegram-бота питания

Инструкция для Никиты. Проверено по коду и серверу 11 октября 2026 года.

## 1. Как устроена установка

Ноутбук → исходники и разработка → GitHub → архив проверенного коммита → сборка Linux на сервере → переключение службы.

`git push` сохраняет код на GitHub, но не обновляет сервер. Команда сборки создаёт программу, но сама по себе не переключает службу. База данных хранится отдельно от программы, поэтому обычное обновление её не заменяет.

| Что | Где |
|---|---|
| Корень Git-репозитория на ноутбуке | `C:\Users\Nikita\.codex\.chatgpt-projects\g-p-6ac7d12dd7b881918deadeb1f982636f\github-repo` |
| Проект бота | `nutrition-bot` внутри репозитория |
| Репозиторий | https://github.com/niknech/codex |
| Доработка AI | ветка `nutrition-openai`, PR https://github.com/niknech/codex/pull/7 |
| Сервер | Debian, `NICK-SERV`, пользователь `nick` |
| SSH через Tailscale | `ssh nick@100.111.125.124` |
| SSH в домашней сети | `ssh nick@192.168.1.91` |
| Активная программа | `/opt/nutrition-bot/current/nutrition-bot` |
| Текущий каталог версии на дату проверки | `/opt/nutrition-bot/releases/ai-4e98e5d` |
| Исходники этой версии на сервере | `/home/nick/builds/nutrition-ai-4e98e5d` |
| Рабочая база сервера | `/var/lib/nutrition-bot/bot.sqlite3` |
| Настройки | `/etc/nutrition-bot/config.env` |
| Ключ Telegram | `/etc/nutrition-bot/telegram_token` |
| Ключ OpenAI | `/etc/nutrition-bot/ai_api_key` |
| Служба | `/etc/systemd/system/nutrition-bot.service` |
| Резервные копии | `/var/backups/nutrition-bot/` |

`current` — символическая ссылка на каталог конкретной версии. Служба запускает программу через эту ссылку. Старые каталоги релизов нужны для отката.

На ноутбуке отдельная база: `nutrition-bot\data\local.sqlite3`. Она не синхронизируется с серверной. Старые локальные записи автоматически на сервер не переносятся.

## 2. Где редактировать код

Открывай в редакторе папку **github-repo**, а изменения делай в **nutrition-bot**. Можно читать код и давать задания агенту вместо ручного редактирования. Каталог `sources` рядом с репозиторием — синхронизируемые справочные материалы; его не редактируем.

Не редактируй исходники внутри `build`, `build-next` или `build/_deps`: там результаты сборки и сторонние библиотеки. Серверную папку сборки используем для воспроизводимой сборки выпуска, а основной код меняем на ноутбуке.

| Файл/каталог относительно nutrition-bot | Назначение и что там менять |
|---|---|
| `src/main.cpp` | Запуск, аргументы командной строки, потоки, получение событий Telegram, остановка |
| `src/bot.cpp` | Основное меню, команды, обработка кнопок, подтверждение и сохранение записей, статистика |
| `src/ui.cpp` | Пошаговые экраны еды и веса, рубрики, готовые значения, календарь, выбор AI |
| `src/domain.cpp` | Числа и даты, правила проверки данных, суммы КБЖУ, нормы, формат итогов и оценки еды |
| `src/ai.cpp` | Инструкция модели, JSON-схема запроса, разбор ответа OpenAI. Здесь изменяется поведение «сразу оценивать порцию» |
| `src/worker.cpp` | Очередь AI, дневные лимиты, напоминания, отправка сообщений, обработка сетевых ошибок |
| `src/http.cpp` | libcurl, HTTPS-запросы, тайм-ауты, Telegram API |
| `src/database.cpp` | SQLite, SQL-запросы, транзакции, миграции и резервная копия |
| `src/config.cpp` | Чтение настроек и секретов, проверка конфигурации |
| `src/chart.cpp` | Построение PNG-графиков через gnuplot |
| `include/*.hpp` | Объявления типов, классов и функций — аналог заголовочных `.h` в C |
| `migrations/*.sql` | Версии структуры базы |
| `tests/tests.cpp` | Проверки логики, AI-очереди, сохранения, backup и других основных операций |
| `tests/ui_tests.cpp` | Проверки сценариев меню, кнопок, календаря, категорий и миграции |
| `CMakeLists.txt` | Список исходников, зависимости, цели сборки и тестов |
| `deploy/` | Шаблоны службы, настроек и ежедневных копий |
| `scripts/rebuild-local.ps1` | Пересборка и перезапуск локального Windows-бота |

Пример пути еды: Telegram → `Bot::message` → экран в `ui.cpp` → черновик `drafts` → очередь `jobs` → запрос из `ai.cpp` → карточка оценки → кнопка сохранения → `entries`. Пока пользователь не подтвердил, КБЖУ черновика не входят в итоги.

Для знающего C: `Bot` похож на набор функций с общим контекстом; ссылки `Database&` и `Config&` передают существующие объекты без копирования. RAII освобождает ресурс в деструкторе. Например, незавершённая `Transaction` автоматически откатывает SQL при выходе из области видимости или исключении. Основные C-библиотеки SQLite и CURL здесь обёрнуты таким способом.

## 3. Как заказывать доработку и вести Git

Хорошая постановка: «Когда я нажимаю X и ввожу Y, хочу видеть Z; в базе должно сохраняться W. Реализуй, проверь тестами, объясни изменённые функции и подготовь PR». Пример: «Добавь кнопку воды 350 мл, покажи новый итог и возможность отменить именно эту запись».

В **Git Bash на ноутбуке**, корень репозитория:

```bash
cd ~/.codex/.chatgpt-projects/g-p-6ac7d12dd7b881918deadeb1f982636f/github-repo
git status --short
git branch --show-current
```

Если статус не пустой, сначала разберись с изменениями; не делай `reset --hard` или `clean -fd`. Незакоммиченные изменения могут принадлежать текущей доработке.

Перед новой независимой задачей дождись объединения нужных предыдущих изменений. В частности, сервер уже использует AI-код из `nutrition-openai`; наличие его на сервере не означает, что PR №7 объединён в `main`. Проверь PR перед выпуском из `main`.

После объединения, при чистом рабочем дереве:

```bash
git switch main &&
git pull --ff-only &&
git switch -c feature/my-change
```

После редактирования и тестов: `git diff`, затем добавь конкретные изменённые файлы через `git add`, проверь `git diff --cached`, сделай commit и push. Открой PR, дождись проверок и объедини. Секреты, базы, логи и сборки в Git не добавляем; не обходи `.gitignore` через `git add -f`.

## 4. Сборка на ноутбуке: Windows, PowerShell

У тебя уже настроены MinGW и CMake. WSL для текущей установки не требуется. Старый `docs/DEVELOPMENT.md` описывает альтернативный сценарий WSL, а не фактическую сборку на этом ноутбуке.

Для сборки **без запуска** локального бота:

```powershell
cd "$HOME\.codex\.chatgpt-projects\g-p-6ac7d12dd7b881918deadeb1f982636f\github-repo\nutrition-bot"
$env:PATH = "C:\msys64\mingw64\bin;$env:PATH"
$env:BOT_TZDATA_DIR = (Resolve-Path build/tzdata).Path
cmake --build build -j2
if ($LASTEXITCODE -ne 0) { throw 'Сборка не прошла' }
ctest --test-dir build --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Тесты не прошли' }
```

Результат — `build\nutrition-bot.exe`. Если этот файл занят работающим локальным ботом, сначала ` .\data\local.ps1 stop`.

Если нужны также backup и автоматический запуск локального бота:

```powershell
.\scripts\rebuild-local.ps1
```

**Этот скрипт запускает бота после сборки.** Для параллельной разработки нужен отдельный тестовый бот и его токен в `.secrets\telegram_token`. Если используешь серверный токен, сначала останови серверную службу. Одновременно запускать два polling-процесса с одним токеном нельзя. Тесты CTest настоящих запросов OpenAI не делают: качество модели проверяется отдельным тестовым вводом.

## 5. Подготовка выпуска для сервера

Пример следующего тега — `nutrition-v0.2.0`. Если такое имя уже занято, выбери новое; опубликованные теги не передвигаем. Ошибочный старый `nutrition-v0.1.0` не использовать: он создан до появления каталога бота в `main`.

В **Git Bash на ноутбуке**, из корня репозитория, после объединения PR:

```bash
(
  set -e
  git switch main
  git pull --ff-only
  test -z "$(git status --porcelain)" || { echo 'Есть локальные изменения'; exit 1; }
  git cat-file -e HEAD:nutrition-bot/CMakeLists.txt
  # Убедись по PR и git log, что нужные AI-изменения вошли в main.
  git log -5 --oneline
  release=nutrition-v0.2.0
  git tag -a "$release" -m "Nutrition bot v0.2.0"
  git push origin "$release"
  git archive --format=tar.gz --output="/tmp/$release.tar.gz" "$release:nutrition-bot"
  test -s "/tmp/$release.tar.gz"
  tar -tzf "/tmp/$release.tar.gz"
  scp "/tmp/$release.tar.gz" nick@100.111.125.124:/home/nick/
)
```

Скобки и `set -e` останавливают блок при ошибке, не закрывая терминал. Если push тега не прошёл, локальный тег уже может существовать: проверь `git show nutrition-v0.2.0`, не повторяй создание вслепую. В архив входят только отслеживаемые файлы каталога из выбранного коммита. Windows `.exe` на Linux не переносим — собираем заново.

## 6. Сборка выпуска на сервере

Из Git Bash или PowerShell: `ssh nick@100.111.125.124`. Далее команды выполняются **на сервере**:

```bash
(
  set -e
  release=nutrition-v0.2.0
  test -s "$HOME/$release.tar.gz"
  test ! -e "$HOME/builds/$release" || { echo 'Папка уже существует; проверь предыдущую попытку'; exit 1; }
  mkdir -p "$HOME/builds/$release"
  tar -xzf "$HOME/$release.tar.gz" -C "$HOME/builds/$release"
  cd "$HOME/builds/$release"
  cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
  cmake --build build -j2
  ctest --test-dir build --output-on-failure
)
```

Ожидаем `100% tests passed`. Если сборка оборвалась, исправь причину и повтори configure/build/test из уже существующей папки. Текущая служба во время сборки продолжает работать.

## 7. Установка проверенного выпуска

На сервере, только после успешных тестов. Не создавай пользователя `nutritionbot` повторно и не копируй шаблон `config.env.example` поверх рабочих настроек.

```bash
(
  set -e
  cd ~/builds/nutrition-v0.2.0
  test -x build/nutrition-bot
  previous=$(readlink -f /opt/nutrition-bot/current)
  printf 'Предыдущая версия: %s\n' "$previous"
  test ! -e /opt/nutrition-bot/releases/v0.2.0
  test ! -e /opt/nutrition-bot/current.next
  test ! -L /opt/nutrition-bot/current.next
  sudo install -d -m 755 /opt/nutrition-bot/releases/v0.2.0
  sudo install -m 755 build/nutrition-bot /opt/nutrition-bot/releases/v0.2.0/nutrition-bot
  sudo systemctl stop nutrition-bot
  sudo /usr/local/sbin/nutrition-bot-backup
  sudo ln -s /opt/nutrition-bot/releases/v0.2.0 /opt/nutrition-bot/current.next
  sudo mv -Tf /opt/nutrition-bot/current.next /opt/nutrition-bot/current
  sudo systemctl start nutrition-bot
)
systemctl status nutrition-bot --no-pager
sudo journalctl -u nutrition-bot -n 40 --no-pager
```

Запиши старый путь и имя backup, напечатанные блоком. При ошибке после остановки бот может остаться остановленным; не продолжай следующие шаги до диагностики. Служба при старте проверяет настройки и применяет миграции.

Затем в Telegram проверь `/start`, воду, вес, AI-еду и итоги. `active (running)` доказывает наличие процесса, но не доступ к Telegram/OpenAI.

Если изменён только код программы, `daemon-reload` не нужен. Если выпуск меняет сам `.service`, отдельно установи обновлённый файл, выполни `sudo systemctl daemon-reload` и перезапуск. Изменения структуры БД проверяй сначала на копии; одной успешной компиляции недостаточно.

Скрипты `enable-ai-50d162d.sh` и `update-ai-4e98e5d.sh` — помощники для конкретных старых коммитов. Они не получают «последнюю версию». Для новых выпусков используй новый тег и каталог.

## 8. Управление работающим ботом

```bash
systemctl status nutrition-bot --no-pager       # состояние
sudo systemctl stop nutrition-bot              # остановить
sudo systemctl start nutrition-bot             # запустить
sudo systemctl restart nutrition-bot           # перезапустить
sudo journalctl -u nutrition-bot -n 60 --no-pager # последние события
sudo journalctl -u nutrition-bot -f             # следить; Ctrl+C закрывает просмотр
readlink -f /opt/nutrition-bot/current           # какой релиз выбран
```

`stop` не отключает автозапуск после перезагрузки. Для длительного выключения: `sudo systemctl disable --now nutrition-bot`. Вернуть автозапуск: `sudo systemctl enable --now nutrition-bot`.

Настройки редактируются через `sudo nano /etc/nutrition-bot/config.env`; после сохранения нужен restart. Там `BOT_ALLOWED_USER_IDS`, timezone, флаг AI, модель и лимиты запросов. Несколько Telegram ID указываются через запятую. Нормы питания и напоминания каждого пользователя хранятся в БД и меняются через меню/команды бота.

Смена модели не требует пересборки: меняется `AI_MODEL`, затем restart и тестовая оценка. Доступность новой модели и поддержку параметров запроса нужно проверить. Дневные лимиты AI считают попытки, включая неуспешные, по дню UTC; это число запросов, а не точный денежный бюджет.

## 9. База данных: чтение

Это SQLite, один основной файл, отдельного сервера БД нет. Работающий бот использует WAL: рядом могут находиться `bot.sqlite3-wal` и `bot.sqlite3-shm`. Их не удаляем вручную. Не переносим только основной файл работающей базы обычным `cp` или `scp`: последние записи могут находиться в WAL.

Самый безопасный повседневный способ исправлений — интерфейс бота. `/entries` позволяет отменить запись, затем внести правильную; вес за ту же дату бот заменяет после подтверждения. Прямой SQL нужен для администрирования, он обходит проверки программы.

Просмотр рабочей базы без возможности записи:

```bash
sudo sqlite3 -readonly /var/lib/nutrition-bot/bot.sqlite3
```

Внутри SQLite:

```sql
.headers on
.mode column
.tables
.schema entries
SELECT version, datetime(applied_at,'unixepoch') AS applied_utc FROM schema_migrations;
SELECT id, kind, day, category, amount, kcal, protein, fat, carbs, deleted
FROM entries WHERE user=239804441 ORDER BY id DESC LIMIT 20;
.quit
```

## 10. Что хранится в таблицах

| Таблица | Содержимое |
|---|---|
| `users` | Telegram ID, часовой пояс, напоминания, согласие AI |
| `entries` | Сохранённые еда, вода и вес |
| `goals` | Нормы питания и воды, действующие с определённой даты |
| `drafts` | Неподтверждённые и завершённые черновики, оценка JSON, версия |
| `sessions` | Текущий шаг ввода и временные данные диалога |
| `jobs` | AI-задания и попытки; используются также для дневных лимитов |
| `inbox` | Принятые обновления Telegram и отметка обработки |
| `outbox` | Исходящие сообщения, попытки и отметка отправки |
| `state` | Смещение Telegram и номера экранов |
| `notices` | Учёт уже отправленных напоминаний |
| `schema_migrations` | Версии схемы |

В `entries`: `kind` = `meal`, `water`, `weight`; `day` = локальная дата `YYYY-MM-DD`; `utc` = Unix timestamp; `category` = `breakfast`, `lunch`, `dinner`, `unspecified`. `amount`: вода в миллилитрах, вес в граммах (82,4 кг = 82400). `kcal` — килокалории, `protein/fat/carbs` — граммы **всей порции**. `items` — JSON состава еды; итоговые числа и JSON должны согласовываться. `deleted=1` — отменённая запись, которая не входит в итоги. `draft` защищает от повторного сохранения черновика.

Не чисти технические таблицы «для порядка»: сброс `offset`, `inbox` или `outbox` может привести к повторной обработке/отправке; удаление `jobs` меняет учёт лимита AI. Не изменяй `schema_migrations` вручную.

## 11. Как безопасно исправить запись SQL

Сначала найди конкретный ID через просмотр. Ниже **пример**: запись `123` принадлежит тебе и содержит ошибочный вес. Замени 123 на реальный ID. Не запускай пример без проверки строки.

На сервере:

```bash
sudo systemctl stop nutrition-bot &&
sudo /usr/local/sbin/nutrition-bot-backup &&
sudo -u nutritionbot sqlite3 /var/lib/nutrition-bot/bot.sqlite3
```

В SQLite:

```sql
.headers on
.mode column
PRAGMA foreign_keys=ON;
BEGIN IMMEDIATE;
SELECT id,user,kind,day,amount,deleted FROM entries WHERE id=123;
UPDATE entries SET amount=82400
WHERE id=123 AND user=239804441 AND kind='weight' AND deleted=0;
SELECT changes() AS changed_rows;
SELECT id,user,kind,day,amount,deleted FROM entries WHERE id=123;
```

Проверь владельца, вид записи, дату и число изменённых строк. Ожидаем ровно одну. Если всё верно:

```sql
COMMIT;
PRAGMA integrity_check;
.quit
```

Если ошибся — **вместо COMMIT** выполни `ROLLBACK;`, затем `.quit`. Не выполняй оба варианта подряд. Если изменения уже закоммичены, ROLLBACK их не отменит — потребуется отдельное исправление или восстановление.

Затем в оболочке сервера:

```bash
sudo systemctl start nutrition-bot
systemctl status nutrition-bot --no-pager
```

`integrity_check` должен вернуть `ok`; это проверка структуры SQLite, а не правильности введённого веса.

Пример мягкого удаления (в такой же транзакции с backup и проверками):

```sql
UPDATE entries SET deleted=1
WHERE id=123 AND user=239804441 AND deleted=0;
```

Исправление рубрики еды:

```sql
UPDATE entries SET category='dinner'
WHERE id=123 AND user=239804441 AND kind='meal' AND deleted=0;
```

Исправление КБЖУ еды лучше делать через удаление и повторный ввод в боте. Простое изменение `kcal` оставляет старый `items` JSON: получится несогласованная запись. Не меняй дату веса на занятую другим активным измерением: действует уникальное ограничение одного веса на пользователя и день.

## 12. Резервные копии и восстановление

Ежедневный таймер запланирован на 04:00 по времени сервера с задержкой до 15 минут. `Persistent=true` позволяет выполнить пропущенную задачу после включения. Скрипт использует SQLite backup, проверяет `integrity_check` и удаляет свои копии старше 14 дней. Сегодня, 11 октября, таймер уже запускался; следующий запуск на момент проверки — 12 октября в 04:09 МСК.

```bash
sudo /usr/local/sbin/nutrition-bot-backup
sudo ls -lh /var/backups/nutrition-bot/
systemctl list-timers nutrition-bot-backup.timer --no-pager
sudo journalctl -u nutrition-bot-backup.service -n 25 --no-pager
```

Копии на том же диске не спасают от поломки диска. Для копии вне сервера выбери точное имя файла из списка и подставь вместо `ИМЯ_КОПИИ.sqlite3`.

На сервере:

```bash
sudo install -o nick -g nick -m 600 /var/backups/nutrition-bot/ИМЯ_КОПИИ.sqlite3 /home/nick/nutrition-backup.sqlite3
```

На ноутбуке, PowerShell (сохраняем вне репозитория):

```powershell
New-Item -ItemType Directory -Force "$HOME\Documents\nutrition-backups" | Out-Null
$backupName = 'bot-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.sqlite3'
scp nick@100.111.125.124:/home/nick/nutrition-backup.sqlite3 "$HOME\Documents\nutrition-backups\$backupName"
```

После проверки копии можно удалить только временный `/home/nick/nutrition-backup.sqlite3` на сервере. Такая копия содержит личный дневник — не публикуй её. Её можно открыть в локальном просмотрщике SQLite для изучения. Не возвращай отредактированную старую копию поверх живой базы: потеряются новые записи.

Для восстановления сначала останови бота, сохрани текущее состояние и восстанови выбранный backup через SQLite. **Это заменяет всю базу и убирает записи после выбранной копии.**

```bash
sudo systemctl stop nutrition-bot &&
sudo /usr/local/sbin/nutrition-bot-backup &&
sudo sqlite3 /var/lib/nutrition-bot/bot.sqlite3 ".restore '/var/backups/nutrition-bot/ИМЯ_КОПИИ.sqlite3'" &&
sudo chown nutritionbot:nutritionbot /var/lib/nutrition-bot/bot.sqlite3 &&
sudo sqlite3 /var/lib/nutrition-bot/bot.sqlite3 'PRAGMA integrity_check;'
```

Убедись в `ok`, совместимости схемы с выбранной программой и отсутствии другого процесса, открывшего эту БД. Затем `sudo systemctl start nutrition-bot` и проверка дневника. Старый основной файл не накладываем поверх файлов WAL.

## 13. Откат программы

Если схема базы совместима, можно вернуть предыдущий каталог релиза. Например, для возврата к проверенному на дату инструкции `ai-4e98e5d`:

```bash
(
  set -e
  test -x /opt/nutrition-bot/releases/ai-4e98e5d/nutrition-bot
  test ! -e /opt/nutrition-bot/current.rollback
  test ! -L /opt/nutrition-bot/current.rollback
  sudo systemctl stop nutrition-bot
  sudo /usr/local/sbin/nutrition-bot-backup
  sudo ln -s /opt/nutrition-bot/releases/ai-4e98e5d /opt/nutrition-bot/current.rollback
  sudo mv -Tf /opt/nutrition-bot/current.rollback /opt/nutrition-bot/current
  sudo systemctl start nutrition-bot
)
```

Если новый выпуск изменил схему несовместимо, одного переключения недостаточно. Нужен согласованный backup до миграции и при необходимости предыдущие настройки. Для текущей AI-доработки схема не менялась; версия схемы — 2. При новых изменениях добавляются новые миграции и обновляется их применение в C++; старые SQL-файлы уже выпущенных миграций не переписываются.

## 14. Частые проблемы

| Симптом | Что проверить |
|---|---|
| Нет `build/nutrition-bot` | `pwd`; нужен каталог исходников нужного выпуска и успешная сборка |
| Бот молчит, служба active | Журнал, доступ к Telegram, правильный бот/токен, allowlist |
| Telegram 409 | Второй запущенный экземпляр с тем же токеном или webhook |
| Telegram/OpenAI 401 | Соответствующий ключ, отсутствие пробелов, перезапуск после смены |
| OpenAI 429 | Баланс API или лимит частоты; отдельно есть дневной лимит самого бота |
| OpenAI 403 | Доступ аккаунта и сервера к API |
| AI недоступен в меню | `AI_ENABLED`, перезапуск и фактически выбранный каталог релиза |
| После «обновления» старые кнопки | Начни новый диалог через меню; проверь current и журнал старта |
| Пропала история | Проверь `BOT_DB_PATH`, сервер/ноутбук и выбранного пользователя |
| Нет графика | gnuplot, шрифты, место на диске |
| Не создаются backup | Журнал backup-службы, свободное место, права |

Проверка сети без секретов:

```bash
curl -I --connect-timeout 10 --max-time 15 https://api.telegram.org
curl -sS -o /dev/null -w 'HTTP %{http_code}\n' --connect-timeout 10 --max-time 15 https://api.openai.com/v1/models
df -h /var/lib/nutrition-bot /var/backups/nutrition-bot
```

Для OpenAI ответ 401 **без ключа** ожидаем: сервер достиг API, но это ещё не подтверждает работоспособность ключа и модели. Не отключай TLS-проверку. `sotad active` тоже не доказывает, что VPN подключён: ранее служба работала без туннеля. После изменений VPN проверяй и Telegram/OpenAI, и доступ SSH через Tailscale.

Пароли, ключи и строки подписки VPN не публикуй. После смены файла ключа перезапусти службу: systemd передаёт credentials при запуске. Закрытие SSH не останавливает systemd-бота.

## 15. Короткая памятка

1. Меняем исходники на ноутбуке в отдельной ветке.
2. Собираем, проверяем тесты и поведение тестового бота.
3. Commit → push → PR → проверки → merge.
4. Новый тег → архив только nutrition-bot → сервер.
5. Собираем Linux, запускаем тесты, пока старая служба работает.
6. Останавливаем службу → backup → переключаем current → запускаем → проверяем Telegram.
7. Для изменений БД: сначала просмотр, затем остановка, backup, транзакция, проверка, COMMIT или ROLLBACK, запуск.

Можно поручить мне весь цикл: «Сделай доработку, объясни модули, проверь тесты, подготовь PR и серверный выпуск». Пароль sudo и секреты остаются у тебя; если они нужны для последнего шага, я подготовлю конкретную команду для твоего терминала.
