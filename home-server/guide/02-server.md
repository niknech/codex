# 2. Управление сервером

[Оглавление](../../README.md) · [Назад](01-connect.md) · [Далее](03-nutrition-bot.md)

Здесь все команды выполняются **НА СЕРВЕРЕ после SSH**.

## Быстрая проверка после включения

```bash
hostname
uptime
systemctl --failed --no-pager
systemctl is-active nutrition-bot adguardvpn-bot tailscaled
systemctl is-active nick-wifi-guard nick-wifi-vpn nick-wifi-ap nick-wifi-addresses
systemctl is-active docker immich
findmnt --mountpoint /mnt/photos
df -hT / /mnt/photos
```

Ожидаем NICK-SERV, пустой список упавших служб, `active` у используемых служб и отдельный ext4-диск на `/mnt/photos`. Если HDD не примонтирован, `df` может показать системный раздел — это не исправная медиатека. Поэтому важен именно `findmnt --mountpoint`.

После этого отправь сообщение боту, открой Immich и новый сайт с телефона в NICK-VPN при выключенном мобильном интернете. Работающий процесс не доказывает работу приложения.

## Запуск и автозапуск — разные вещи

Выбирай одну нужную команду:

| Команда | Действие |
|---|---|
| `systemctl status nutrition-bot --no-pager` | Состояние |
| `sudo systemctl start nutrition-bot` | Запуск сейчас |
| `sudo systemctl stop nutrition-bot` | Остановка сейчас |
| `sudo systemctl restart nutrition-bot` | Короткая остановка и запуск |
| `systemctl is-enabled nutrition-bot` | Проверка автозапуска |
| `sudo systemctl enable nutrition-bot` | Автозапуск при загрузке, без запуска сейчас |
| `sudo systemctl disable --now nutrition-bot` | Остановить и убрать автозапуск |
| `sudo systemctl enable --now nutrition-bot` | Вернуть автозапуск и запустить |

`active (exited)` нормально для одноразовых служб guard/AP. `disabled` или `static` у вспомогательной службы не всегда ошибка: её запускает зависимая служба. Для Wi-Fi проверяй автозапуск `nick-wifi-addresses`, не включай всё подряд.

## Прочитать ошибки

```bash
sudo journalctl -u nutrition-bot -n 80 --no-pager
```

Это последние 80 сообщений бота. Для другого приложения замени имя службы. Смотреть новые события:

```bash
sudo journalctl -u nutrition-bot -f
```

Ctrl+C закроет просмотр, не бота. Ошибки ОС текущей загрузки:

```bash
sudo journalctl -b -p err --no-pager -n 80
```

## Место, память и температура

```bash
df -hT / /mnt/photos
df -i / /mnt/photos
free -h
uptime
sudo docker stats --no-stream
```

`Avail` — свободное место; `IUse%` — использование количества файлов. В памяти смотри `available`: Linux занимает свободную RAM кешем. Load average — очередь работы, не проценты CPU. В Docker 100% примерно соответствует одному ядру.

Если установлен sensors: `sensors`. Для HDD сначала проверь имя устройства:

```bash
lsblk -o NAME,SIZE,MODEL,FSTYPE,LABEL,MOUNTPOINTS
```

Если Seagate по-прежнему `/dev/sda`:

```bash
sudo smartctl -H -A /dev/sda
```

PASSED не заменяет копию. При щелчках HDD, ошибках чтения или росте Pending/Uncorrectable останови массовую загрузку и Immich, сначала сохрани данные. При обычной нагрузке распознавания смотри раздел 6.

## Обновить Debian

Выбери время допустимого перерыва. Проверь копии, место и независимый LAN-доступ.

```bash
sudo apt update
apt list --upgradable
sudo apt upgrade
```

Прочитай список и вопросы установщика. При замене изменённого конфигурационного файла сравни версии. `full-upgrade`, удаление пакетов и переход на новую Debian не являются обычной уборкой. Обновление пакетов может перезапустить службы; повтори проверку после включения. После обновления Docker отдельно проверь Immich.

Immich и самостоятельно установленный AdGuard обновляются отдельно. `git pull` тоже не обновляет работающий бот.

## Перезагрузить или выключить

Не делай этого во время переноса базы, обновления, копирования или массовой загрузки. Закрепи LAN-адрес на роутере; имей возможность подойти к серверу при сбое.

```bash
sudo findmnt --verify --verbose
sudo /usr/local/sbin/immich-check-storage
```

При ошибке остановись. Если всё хорошо, выбери одно:

| Действие | Команда |
|---|---|
| Перезагрузка | `sudo reboot` |
| Выключение | `sudo poweroff` |

SSH оборвётся, все приложения временно станут недоступны. После reboot подожди 1–2 минуты, подключись и повтори проверку. После poweroff нужна кнопка питания или отдельно настроенное удалённое включение. Не выдёргивай питание вместо завершения работы.
