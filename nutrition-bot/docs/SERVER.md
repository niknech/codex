# Установка, выпуск, резервные копии и откат

Команды ниже предназначены для Debian/Ubuntu с systemd. Сначала проверь версию
и архитектуру. Сервер этой задачей автоматически не меняется. Для домашнего
бота создай отдельный рабочий токен; не используй ноутбучный тестовый.

## Подключение и зависимости

В PowerShell ноутбука (адрес из твоей исходной инструкции):

```powershell
ssh nick@100.111.125.124
# Или дома: ssh nick@192.168.1.91
```

На сервере:

```bash
cat /etc/os-release
uname -m
sudo apt update
sudo apt install build-essential cmake ninja-build pkg-config \
  libcurl4-openssl-dev nlohmann-json3-dev libsqlite3-dev sqlite3 \
  libhowardhinnant-date-dev tzdata gnuplot-nox fonts-dejavu-core ca-certificates
```

## Подготовка релиза на ноутбуке

Сначала объедини проверенный PR. В WSL, в корне `~/projects/codex`:

```bash
git switch main
git pull --ff-only
git status --short  # должно быть пусто; иначе сначала разберись с изменениями
git tag -a nutrition-v0.1.0 -m "Nutrition bot first release"
git push origin nutrition-v0.1.0
git archive --format=tar.gz --output=/tmp/nutrition-v0.1.0.tar.gz nutrition-v0.1.0:nutrition-bot
scp /tmp/nutrition-v0.1.0.tar.gz nick@100.111.125.124:/home/nick/
```

Тег неизменяемо обозначает релиз. Для следующего выпуска используй новое имя.
Архив содержит только каталог nutrition-bot из коммита: без остального monorepo,
рабочей базы и секретов. Если WSL не видит Tailscale-маршрут, скопируй архив в
Windows и выполни scp в PowerShell, либо используй домашний адрес.

## Сборка и первая установка на сервере

```bash
mkdir -p ~/builds/nutrition-v0.1.0
tar -xzf ~/nutrition-v0.1.0.tar.gz -C ~/builds/nutrition-v0.1.0
cd ~/builds/nutrition-v0.1.0
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

Следующий блок выполняется один раз. Если пользователь или current уже существуют,
проверь существующую установку и перейди к разделу обновления.

```bash
sudo useradd --system --user-group --home-dir /var/lib/nutrition-bot --shell /usr/sbin/nologin nutritionbot
sudo install -d -o nutritionbot -g nutritionbot -m 700 /var/lib/nutrition-bot
sudo install -d -o root -g root -m 700 /etc/nutrition-bot /var/backups/nutrition-bot
sudo install -d -o root -g root -m 755 /opt/nutrition-bot/releases/v0.1.0
sudo install -m 755 build/nutrition-bot /opt/nutrition-bot/releases/v0.1.0/nutrition-bot
sudo ln -s /opt/nutrition-bot/releases/v0.1.0 /opt/nutrition-bot/current
sudo install -m 600 deploy/config.env.example /etc/nutrition-bot/config.env
sudo nano /etc/nutrition-bot/config.env
```

В config.env задай настоящий BOT_ALLOWED_USER_IDS. AI пока оставь false.
Ниже файлы создаются **только если ещё не существуют**, чтобы не стереть токены:

```bash
sudo sh -c 'umask 077; test -e /etc/nutrition-bot/telegram_token || : > /etc/nutrition-bot/telegram_token'
sudo sh -c 'umask 077; test -e /etc/nutrition-bot/ai_api_key || : > /etc/nutrition-bot/ai_api_key'
sudo nano /etc/nutrition-bot/telegram_token
sudo chmod 600 /etc/nutrition-bot/telegram_token /etc/nutrition-bot/ai_api_key
sudo install -m 644 deploy/nutrition-bot.service /etc/systemd/system/nutrition-bot.service
sudo systemd-analyze verify /etc/systemd/system/nutrition-bot.service
sudo systemctl daemon-reload
sudo systemctl enable --now nutrition-bot
systemctl status nutrition-bot --no-pager
sudo journalctl -u nutrition-bot -n 100 --no-pager
```

В telegram_token одна строка рабочего токена. AI-файл может оставаться пустым,
пока AI_ENABLED=false. Токен не надо присылать агенту и не надо коммитить.

В Telegram проверь `/start`, `/water 250`, отмену, вес, ручную еду, `/today`,
статистику и график. Затем `sudo systemctl restart nutrition-bot` и повторно
проверь сохранность данных. Удали тестовые записи через `/entries`.
`active (running)` само по себе не доказывает, что Telegram доступен.

## Проверка окружения службы без запуска polling

Чтобы выполнить self-test с теми же файлами и пользователем:

```bash
sudo systemctl stop nutrition-bot
sudo systemd-run --wait --pipe --collect --unit=nutrition-bot-check \
  -p User=nutritionbot -p Group=nutritionbot \
  -p WorkingDirectory=/var/lib/nutrition-bot \
  -p EnvironmentFile=/etc/nutrition-bot/config.env \
  -p LoadCredential=telegram_token:/etc/nutrition-bot/telegram_token \
  -p LoadCredential=ai_api_key:/etc/nutrition-bot/ai_api_key \
  /opt/nutrition-bot/current/nutrition-bot --self-test
sudo systemctl start nutrition-bot
```

## Ежедневные резервные копии

Из каталога распакованного релиза:

```bash
sudo install -m 755 deploy/backup.sh /usr/local/sbin/nutrition-bot-backup
sudo install -m 644 deploy/nutrition-bot-backup.service /etc/systemd/system/
sudo install -m 644 deploy/nutrition-bot-backup.timer /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now nutrition-bot-backup.timer
sudo systemctl start nutrition-bot-backup.service
sudo journalctl -u nutrition-bot-backup.service -n 20 --no-pager
systemctl list-timers nutrition-bot-backup.timer
```

Скрипт использует SQLite `.backup`, проверяет integrity_check и удаляет только
свои резервные файлы старше 14 дней. Не копируй один .sqlite3 работающей WAL-базы
обычным cp. Хотя бы одну копию регулярно сохраняй вне сервера.

Пробное восстановление (подставь точное имя копии из журнала):

```bash
sudo install -d -m 700 /var/backups/nutrition-bot/restore-test
sudo sqlite3 /var/backups/nutrition-bot/restore-test/test.sqlite3 \
  ".restore '/var/backups/nutrition-bot/ИМЯ_КОПИИ.sqlite3'"
sudo sqlite3 /var/backups/nutrition-bot/restore-test/test.sqlite3 'PRAGMA integrity_check; SELECT COUNT(*), SUM(kcal) FROM entries WHERE deleted=0;'
```

Сравни количество и сумму с тем же запросом на выбранной резервной копии.
Тестовая база не заменяет рабочую.

## Обновление до v0.2.0

Подготовь новый тег `nutrition-v0.2.0`, передай архив, распакуй в новую папку,
собери и выполни тесты, как выше. Старый бот пока работает. Затем:

```bash
sudo install -d -m 755 /opt/nutrition-bot/releases/v0.2.0
sudo install -m 755 build/nutrition-bot /opt/nutrition-bot/releases/v0.2.0/nutrition-bot
readlink /opt/nutrition-bot/current
sudo systemctl stop nutrition-bot
sudo /usr/local/sbin/nutrition-bot-backup
# Запиши напечатанное имя backup и предыдущую версию!
sudo test ! -e /opt/nutrition-bot/current.next
sudo ln -s /opt/nutrition-bot/releases/v0.2.0 /opt/nutrition-bot/current.next
sudo mv -Tf /opt/nutrition-bot/current.next /opt/nutrition-bot/current
sudo systemctl start nutrition-bot
systemctl status nutrition-bot --no-pager
sudo journalctl -u nutrition-bot -n 100 --no-pager
```

Выполняй строки по очереди и останавливайся при любой ошибке. Если current.next
остался от предыдущей попытки, сначала проверь `readlink`; не удаляй его вслепую.
Новые миграции сначала проверяются на копии базы. ExecStartPre применит их при старте.

## Откат

Если схема совместима со старым релизом: останови службу и тем же способом
переключи current на старую папку, затем запусти и проверь Telegram.

При несовместимой схеме сначала останови службу, сделай отдельную копию текущей БД,
затем восстанови согласованную с прежним бинарником резервную копию:

```bash
sudo systemctl stop nutrition-bot
sudo /usr/local/sbin/nutrition-bot-backup
sudo sqlite3 /var/lib/nutrition-bot/bot.sqlite3 \
  ".restore '/var/backups/nutrition-bot/ВЫБРАННАЯ_КОПИЯ.sqlite3'"
sudo chown nutritionbot:nutritionbot /var/lib/nutrition-bot/bot.sqlite3
sudo sqlite3 /var/lib/nutrition-bot/bot.sqlite3 'PRAGMA integrity_check;'
# Переключи current на соответствующий старый релиз, затем start.
```

Ни один другой процесс в этот момент не должен держать базу. Не накладывай старый
файл поверх WAL/SHM. Записи после выбранной копии нужно перенести отдельно:
они исчезнут при восстановлении. Такой откат требует осознанного решения владельца.

## Диагностика

| Симптом | Проверка |
|---|---|
| 401 | Рабочий токен и лишние пробелы в файле |
| 409 | Второй экземпляр с этим токеном или активный webhook |
| Нет ответа | Сеть до api.telegram.org, allowlist, журнал |
| Нет AI | AI_ENABLED, ключ, модель, согласие, доступ API и лимиты |
| Нет PNG | `/usr/bin/gnuplot`, шрифты, место во временном каталоге |
| Нет старых данных | BOT_DB_PATH и путь запущенного релиза |
| Нет напоминаний | `/reminders on`, нормы, timezone, окно в один час |

В журнале нет содержимого еды или секретов. Не отключай проверку TLS для исправления сети.
