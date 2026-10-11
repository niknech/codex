# Бот питания — полезные команды

Памятка для Никиты, 11 октября 2026. Сервер: Debian, `nick@100.111.125.124`.

Копируй блок целиком. Пометки **СЕРВЕР** и **НОУТБУК** указывают, где его выполнять.
Ввод пароля sudo не отображается — это нормально. Ключи и пароли в чат не отправляй.

## 1. Подключиться к серверу

**НОУТБУК — PowerShell или Git Bash:**

```bash
ssh nick@100.111.125.124
```

Дома можно использовать `ssh nick@192.168.1.91`.
После подключения приглашение выглядит как `nick@NICK-SERV:…$`.
Выйти: `exit`. Бот после выхода продолжит работать.

## 2. Добавить пользователя и перезапустить бота

Пользователь отправляет твоему боту `/id` и передаёт тебе число.
Следующий блок спросит ID, добавит его в `config.env`, сохранив существующие ID,
создаст резервную копию настроек и перезапустит службу. Повторный ID не дублируется.
Если запуск не удался, скрипт вернёт прежние настройки и попробует запустить старую конфигурацию.
Перезапуск на несколько секунд прерывает работу бота.

**СЕРВЕР — скопируй целиком:**

```bash
read -r -p 'Telegram ID нового пользователя: ' new_user_id
sudo python3 - "$new_user_id" <<'PY'
import os
import re
import shutil
import subprocess
import sys
import tempfile
from datetime import datetime
from pathlib import Path

user_id = sys.argv[1].strip()
if not re.fullmatch(r'[1-9][0-9]{0,18}', user_id) or int(user_id) > 9223372036854775807:
    raise SystemExit('Нужен положительный числовой Telegram ID. Настройки не изменены.')

config = Path('/etc/nutrition-bot/config.env')
original = config.read_text(encoding='utf-8')
pattern = r'(?m)^BOT_ALLOWED_USER_IDS=([^\r\n]*)$'
matches = list(re.finditer(pattern, original))
if len(matches) != 1:
    raise SystemExit('Ожидалась одна строка BOT_ALLOWED_USER_IDS. Проверь config.env вручную.')
old_value = matches[0].group(1).strip().strip('"\'')
ids = [part.strip() for part in old_value.split(',') if part.strip()]
if any(not re.fullmatch(r'[1-9][0-9]{0,18}', part) or int(part) > 9223372036854775807 for part in ids):
    raise SystemExit('В старом списке ID неверный формат. Настройки не изменены.')
ids = list(dict.fromkeys(ids + [user_id]))
updated = re.sub(pattern, 'BOT_ALLOWED_USER_IDS=' + ','.join(ids), original)

backup = config.with_name('config.env.before-user-' + datetime.now().strftime('%Y%m%d-%H%M%S-%f'))
shutil.copy2(config, backup)
os.chmod(backup, 0o600)
print('Копия настроек:', backup, flush=True)

def write_config(text):
    fd, name = tempfile.mkstemp(prefix='.config-', dir=config.parent)
    try:
        with os.fdopen(fd, 'w', encoding='utf-8', newline='\n') as out:
            out.write(text)
            out.flush()
            os.fsync(out.fileno())
        os.chmod(name, 0o600)
        os.replace(name, config)
    finally:
        if os.path.exists(name):
            os.unlink(name)

write_config(updated)
result = subprocess.run(['systemctl', 'restart', 'nutrition-bot'])
active = result.returncode == 0 and subprocess.run(
    ['systemctl', 'is-active', '--quiet', 'nutrition-bot']).returncode == 0
if not active:
    write_config(original)
    subprocess.run(['systemctl', 'restart', 'nutrition-bot'])
    raise SystemExit('Новые настройки не запустились. Прежние возвращены. Проверь журнал службы.')
print('ID добавлен:', user_id)
print('Всего ID в списке:', len(ids))
print('Бот перезапущен. Новый пользователь теперь должен отправить /start.')
PY
```

Если команда `python3` отсутствует: `sudo apt install python3`, затем повтори блок.
При `BOT_PUBLIC_REGISTRATION=true` доступ уже открыт всем; список ID ограничивает доступ только при `false`.

**Убрать пользователя:** открой настройки, удали его ID вместе с лишней запятой,
сохрани и перезапусти. История пользователя останется в базе.

```bash
sudo nano /etc/nutrition-bot/config.env
```

Сохранение в nano: **Ctrl+O → Enter → Ctrl+X**. Затем:

```bash
sudo systemctl restart nutrition-bot
```

Свой ID: `239804441`. Не убирай его случайно. При публичной регистрации удаление ID из списка не закрывает доступ.

## 3. Выгрузить базу на ноутбук для DB Browser

Сначала делаем согласованный снимок средствами SQLite, затем скачиваем.
Останавливать бота не нужно. Файл снимка на сервере при повторении обновляется.

**СЕРВЕР:**

```bash
sudo install -d -o nick -g nick -m 700 /home/nick/nutrition-export &&
sudo sqlite3 /var/lib/nutrition-bot/bot.sqlite3 ".backup '/home/nick/nutrition-export/snapshot.sqlite3'" &&
sudo chmod 600 /home/nick/nutrition-export/snapshot.sqlite3 &&
sudo chown nick:nick /home/nick/nutrition-export/snapshot.sqlite3 &&
sqlite3 -readonly /home/nick/nutrition-export/snapshot.sqlite3 'PRAGMA integrity_check;'
```

Ожидаем `ok`. Далее открой **отдельное окно PowerShell на ноутбуке**, не SSH:

```powershell
New-Item -ItemType Directory -Force "$HOME\Documents\nutrition-backups" | Out-Null
$snapshotName = 'bot-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '.sqlite3'
$snapshotPath = Join-Path "$HOME\Documents\nutrition-backups" $snapshotName
scp nick@100.111.125.124:/home/nick/nutrition-export/snapshot.sqlite3 "$snapshotPath"
if ($LASTEXITCODE -ne 0) { throw 'Не удалось скачать базу' }
Write-Output "База сохранена: $snapshotPath"
```

Открой полученный файл в DB Browser for SQLite, вкладка **Browse Data / Просмотр данных**.
В `entries` — дневник, в `users` — пользователи, в `goals` — нормы.
Это копия: правки в ней не меняют сервер. Не загружай её обратно поверх рабочей базы — потеряются новые записи.
Не скачивай основной файл работающей БД напрямую: свежие записи могут быть в WAL.

## 4. Сколько пользователей и кто пользуется

Все команды ниже — **СЕРВЕР**.

Количество пользователей, уже взаимодействовавших с ботом с разрешённым доступом:

```bash
sudo sqlite3 -readonly -header -box /var/lib/nutrition-bot/bot.sqlite3 \
  'SELECT COUNT(*) AS registered_users FROM users;'
```

Список с количеством неотменённых записей и последней датой дневника:

```bash
sudo sqlite3 -readonly -header -box /var/lib/nutrition-bot/bot.sqlite3 "
SELECT u.id AS telegram_id, COUNT(e.id) AS records, MAX(e.day) AS latest_diary_date
FROM users u
LEFT JOIN entries e ON e.user=u.id AND e.deleted=0
GROUP BY u.id
ORDER BY records DESC;"
```

Даты здесь относятся к дневнику, а не ко времени последнего визита.
Пользователи, у которых отозван доступ, могут оставаться в `users`.
Список разрешённых ID и режим доступа:

```bash
sudo grep -E '^BOT_(ALLOWED_USER_IDS|PUBLIC_REGISTRATION)=' /etc/nutrition-bot/config.env
```

Уникальные пользователи с неотменёнными записями за последние 7 суток по времени создания записи:

```bash
sudo sqlite3 -readonly -header -box /var/lib/nutrition-bot/bot.sqlite3 "
SELECT COUNT(DISTINCT user) AS users_with_records_7d
FROM entries WHERE deleted=0 AND utc >= unixepoch('now','-7 days');"
```

Это активность по записям. Количество пользователей «онлайн» бот не отслеживает.

## 5. Состояние, запуск, остановка и логи

**СЕРВЕР:**

```bash
systemctl status nutrition-bot --no-pager
```

Перезапуск:

```bash
sudo systemctl restart nutrition-bot
```

Остановить до следующего запуска/перезагрузки:

```bash
sudo systemctl stop nutrition-bot
```

Запустить:

```bash
sudo systemctl start nutrition-bot
```

Выключить вместе с автозапуском:

```bash
sudo systemctl disable --now nutrition-bot
```

Вернуть работу и автозапуск:

```bash
sudo systemctl enable --now nutrition-bot
```

Последние 60 строк журнала:

```bash
sudo journalctl -u nutrition-bot -n 60 --no-pager
```

Смотреть новые события в реальном времени; **Ctrl+C** завершает только просмотр:

```bash
sudo journalctl -u nutrition-bot -f
```

Какая версия выбрана:

```bash
readlink -f /opt/nutrition-bot/current
```

## 6. Резервные копии

Сделать копию сейчас — **СЕРВЕР**:

```bash
sudo /usr/local/sbin/nutrition-bot-backup
```

Скрипт напечатает путь к копии. Два запуска в одну секунду могут столкнуться по имени; в таком случае повтори позже.

Список файлов, расписание и результат последней задачи:

```bash
sudo ls -lh /var/backups/nutrition-bot/
systemctl list-timers nutrition-bot-backup.timer --no-pager
sudo journalctl -u nutrition-bot-backup.service -n 20 --no-pager
```

Ежедневная копия — около 04:00–04:15 по времени сервера. Автоматические копии старше 14 дней удаляются.
Периодически выполняй раздел 3: копия на ноутбуке защищает от потери диска сервера.

## 7. Красивое чтение базы прямо в терминале

**СЕРВЕР:**

```bash
sudo sqlite3 -readonly /var/lib/nutrition-bot/bot.sqlite3
```

Далее команды **внутри SQLite**:

```sql
.headers on
.mode box
.tables
SELECT id, user, kind, day, category, amount, kcal, protein, fat, carbs
FROM entries WHERE deleted=0 ORDER BY id DESC LIMIT 15;
.quit
```

`amount`: для воды — мл, для веса — граммы. КБЖУ относятся ко всей порции.
Правки рабочей базы делай по полной инструкции администратора: остановка → backup → транзакция → проверка → запуск.

## 8. Настройки и AI

**СЕРВЕР:**

```bash
sudo nano /etc/nutrition-bot/config.env
```

Основные строки:

```ini
BOT_ALLOWED_USER_IDS=239804441
BOT_PUBLIC_REGISTRATION=false
AI_ENABLED=true
AI_PROVIDER=openai
AI_MODEL=gpt-5-nano
AI_DAILY_USER_LIMIT=10
AI_DAILY_GLOBAL_LIMIT=50
```

Это справочный пример, не команда замены файла: не стирай остальных пользователей и свои настройки.
После изменения: `sudo systemctl restart nutrition-bot`.
Для временного отключения AI установи `AI_ENABLED=false`.
AI-лимиты — количество попыток за день UTC, а не сумма в долларах.

Ключ OpenAI редактируется отдельно:

```bash
sudo nano /etc/nutrition-bot/ai_api_key
```

После сохранения:

```bash
sudo chmod 600 /etc/nutrition-bot/ai_api_key &&
sudo systemctl restart nutrition-bot
```

В файл ключа вставляется только ключ одной строкой. Не печатай его через `cat` в чат.
Перед сменой модели проверь её доступность и поддержку параметров запросов бота.

## 9. Быстрая диагностика сети и диска

**СЕРВЕР:**

```bash
curl -I --connect-timeout 10 --max-time 15 https://api.telegram.org
curl -sS -o /dev/null -w 'OpenAI HTTP: %{http_code}\n' --connect-timeout 10 --max-time 15 https://api.openai.com/v1/models
df -h /var/lib/nutrition-bot /var/backups/nutrition-bot
tailscale status
```

OpenAI 401 в этой проверке ожидаем: запрос отправлен без ключа. Он подтверждает доступ к API, но не исправность ключа/модели.
`active` у службы бота или Sota не доказывает, что сеть работает.
Telegram 409 часто означает, что бот с тем же токеном запущен ещё и на ноутбуке.

## 10. Остановить тестового бота на ноутбуке

**НОУТБУК — PowerShell:**

```powershell
cd "$HOME\.codex\.chatgpt-projects\g-p-6ac7d12dd7b881918deadeb1f982636f\github-repo\nutrition-bot"
.\data\local.ps1 stop
```

Проверить состояние: `.\data\local.ps1 status`.
Не запускай локального и серверного бота одновременно с одним токеном.

Полный порядок сборки, выпуска, SQL-правок и отката — в соседнем файле `ADMIN_GUIDE.md`.
