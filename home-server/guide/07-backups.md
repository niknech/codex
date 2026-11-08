# 7. Резервные копии и восстановление

[Оглавление](../../README.md) · [Назад](06-immich.md) · [Далее](08-troubleshooting.md)

## Что нужно сохранять

| Компонент | Комплект восстановления |
|---|---|
| Бот | Согласованная база, config и секреты, точный релиз программы |
| Immich | Вся медиатека, согласованная база, Compose, .env, версии и служба |
| VPN / Wi-Fi | Авторизация AdGuard, профиль NetworkManager, nick-wifi, units и drop-ins, версия tun2socks |
| ОС | fstab, информация о дисках, ключи доступа в защищённом хранилище |

Копия на том же диске не защищает от его отказа. GitHub хранит инструкции и код, а не личные базы, фото и пароли. Внешнее копирование Immich по прошлому решению отложено до покупки HDD; наличие свежей независимой копии не подтверждено. Описанные ниже действия сами не выполнялись.

## Бот: сделать копию сейчас

**СЕРВЕР:**

```bash
sudo /usr/local/sbin/nutrition-bot-backup
```

Если скрипт установлен, он создаст SQLite-снимок, проверит его и напечатает путь. Если отсутствует, используй снимок ниже; установка таймера описана в [SERVER.md](../../nutrition-bot/docs/SERVER.md).

Проверить расписание и результат:

```bash
systemctl list-timers nutrition-bot-backup.timer --no-pager
sudo journalctl -u nutrition-bot-backup.service -n 30 --no-pager
sudo ls -lh /var/backups/nutrition-bot
```

Успешный таймер не доказывает, что копия есть вне сервера.

## Бот: скачать согласованный снимок

**СЕРВЕР — вставить целиком:**

```bash
(
set -eu
sudo test -s /var/lib/nutrition-bot/bot.sqlite3
sudo install -d -o nick -g nick -m 700 /home/nick/nutrition-export
sudo sqlite3 /var/lib/nutrition-bot/bot.sqlite3 ".backup '/home/nick/nutrition-export/snapshot.sqlite3'"
sudo chmod 600 /home/nick/nutrition-export/snapshot.sqlite3
sudo chown nick:nick /home/nick/nutrition-export/snapshot.sqlite3
test "$(sqlite3 -readonly /home/nick/nutrition-export/snapshot.sqlite3 'PRAGMA integrity_check;')" = ok
echo 'Снимок готов, проверка OK'
)
```

Повторный запуск обновляет этот экспортный снимок. Для просмотра останавливать бота не нужно; для редактирования с возвратом сначала останови по разделу 3.

**НОУТБУК — новое окно PowerShell:**

```powershell
New-Item -ItemType Directory -Force "$HOME\Documents\nutrition-backups" | Out-Null
$backupName = 'bot-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '.sqlite3'
$backupPath = Join-Path "$HOME\Documents\nutrition-backups" $backupName
scp nick@192.168.1.91:/home/nick/nutrition-export/snapshot.sqlite3 "$backupPath"
if ($LASTEXITCODE -ne 0) { throw 'Не удалось скачать базу' }
Write-Output $backupPath
```

Вне дома используй Tailscale-адрес. Открой файл в DB Browser, выполни `PRAGMA integrity_check;` (ожидается `ok`), проверь последние записи. Не загружай эту базу в GitHub. Возврат — по разделу 3, с остановкой и сохранением текущего состояния.

## Immich: что означает встроенная копия

Дамп базы в `/mnt/photos/immich/backups` содержит сведения о пользователях и фотографиях, но не сами оригиналы. В интерфейсе администратора проверь наличие свежих дампов; при необходимости создай новый через очередь задач. Сохранение только `library` или только дампа недостаточно. [Документация резервирования Immich](https://docs.immich.app/administration/backup-and-restore/).

## Immich: независимая копия с остановкой

Это простой вариант для существующей установки: остановить весь стек и сохранить медиатеку вместе с закрытым каталогом PostgreSQL. Восстанавливается на совместимой архитектуре и **том же образе PostgreSQL**, с теми же версиями расширений. Для переноса между версиями нужен логический дамп по официальной инструкции. Основа процедуры — [копирование полностью остановленного кластера PostgreSQL](https://www.postgresql.org/docs/14/backup-file.html).

Предусловия: отдельный исправный Linux-совместимый диск уже смонтирован в `/mnt/backup`, поддерживает права Unix, места достаточно; `rsync` установлен. Настройка и форматирование внешнего диска здесь не выполняются. По `lsblk` убедись, что это физически другой накопитель, а не раздел HDD с фотографиями.

**СЕРВЕР — только проверка:**

```bash
lsblk -o NAME,SIZE,MODEL,FSTYPE,MOUNTPOINTS
findmnt --mountpoint /mnt/backup
df -hT /mnt/backup /mnt/photos /home/nick/immich-app
sudo du -sh /mnt/photos/immich /home/nick/immich-app
sudo /usr/local/sbin/immich-check-storage
command -v rsync
```

Если диск или место не подтверждены, остановись. Копия содержит секреты; храни диск физически защищённым, предпочтительно зашифрованным.

Следующий блок может надолго остановить Immich. Он создаёт новую папку без перезаписи прошлых копий. **СЕРВЕР:**

```bash
sudo bash <<'SH'
set -eu
umask 077
mountpoint -q /mnt/backup
/usr/local/sbin/immich-check-storage
test -d /home/nick/immich-app/postgres
command -v rsync >/dev/null
snapshot=$(mktemp -d /mnt/backup/immich-XXXXXXXX)
docker inspect --format '{{.Name}} {{.Config.Image}} {{.Image}}' \
  immich_server immich_machine_learning immich_postgres immich_redis > "$snapshot/images.txt"
systemctl cat immich > "$snapshot/immich.service.txt"
cp -p /usr/local/sbin/immich-check-storage "$snapshot/"
cp -p /etc/fstab "$snapshot/fstab.reference"
systemctl stop immich
for container in immich_server immich_machine_learning immich_postgres immich_redis; do
  test "$(docker inspect --format '{{.State.Running}}' "$container")" = false
done
rsync -aHAX --numeric-ids /mnt/photos/immich/ "$snapshot/media/"
rsync -aHAX --numeric-ids /home/nick/immich-app/ "$snapshot/app/"
rsync -aHAXnci --numeric-ids /mnt/photos/immich/ "$snapshot/media/" > "$snapshot/verify-media.txt"
rsync -aHAXnci --numeric-ids /home/nick/immich-app/ "$snapshot/app/" > "$snapshot/verify-app.txt"
test ! -s "$snapshot/verify-media.txt"
test ! -s "$snapshot/verify-app.txt"
touch "$snapshot/COPY-VERIFIED"
systemctl start immich
printf 'Копия: %s\n' "$snapshot"
SH
```

Проверка с контрольными суммами может быть долгой. При ошибке блок остановится, Immich может остаться выключенным. Не помечай незавершённую папку успешной копией; исправь причину. Если отменяешь копирование, после проверки HDD запусти `sudo systemctl start immich` и проверь приложение. Маркер COPY-VERIFIED означает совпадение файлов, **не выполненное пробное восстановление**.

## Пробное восстановление Immich

1. Выбери завершённую копию; сохрани её неизменной. Не тренируйся на единственной рабочей медиатеке.
2. Подготовь отдельное окружение с теми же версиями из `images.txt`; не подключай его к рабочим данным.
3. При остановленных контейнерах скопируй `media/` в новый UPLOAD_LOCATION, `app/postgres/` — в пустой каталог БД, сохраняя числовые владельцы и права. Верни Compose/.env из `app/`, исправив пути и адреса для тестового окружения.
4. Восстанови проверку диска и службу под новое окружение осознанно: UUID нового HDD отличается. Не отключай проверку ради запуска пустой папки.
5. Запусти тестовый стек. Проверь вход, пользователей, альбомы, несколько фото и видео, скачивание оригиналов. Запиши дату и результат.
6. Только при реальной аварии после проверки переключай клиентов. Не накладывай старую БД на работающий PostgreSQL.

Для восстановления логического дампа используй процедуру **точной версии** Immich. В версиях с Maintenance доступно восстановление из интерфейса; оно заменяет текущую базу. Дамп и медиатека должны соответствовать друг другу.

## Копия настроек сервера, бота и сети

Сохрани приватно: `/etc/nutrition-bot`, `/opt/nutrition-bot/releases`, `/etc/nick-wifi`, `/etc/NetworkManager/system-connections`, `/home/nick/.local/share/adguardvpn-cli`, используемые units и drop-ins из `/etc/systemd/system`, `/etc/fstab`. Для точного переноса tun2socks сохрани `/usr/local/bin/tun2socks` и сведения о версии. Проверяй существование путей; копируй с правами root в закрытый каталог на независимом диске.

Не возвращай весь `/etc` поверх новой ОС. Сначала восстанови доступ по LAN/SSH, затем отдельные службы и их зависимости. Tailscale на новом сервере зарегистрируй контролируемо, а не клонируй идентичность одновременно работающего узла.
