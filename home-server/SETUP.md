# Подготовка хранилища и Docker

Команды ниже предназначены для Bash на Debian-сервере. Выполнять разделы по отдельности с проверкой результата. На работающем сервере повторная установка и форматирование не требуются.

## Диск: проверка без изменений

```bash
hostnamectl
cat /etc/os-release
lsblk -o NAME,SIZE,MODEL,FSTYPE,LABEL,UUID,MOUNTPOINTS
sudo blkid /dev/sda1
sudo smartctl -a /dev/sda
findmnt --mountpoint /mnt/photos
df -hT / /mnt/photos
stat -c '%U:%G %a %n' /mnt/photos/immich
```

Имена `/dev/sda` могут измениться после перезагрузки. Сопоставлять модель, размер и UUID; не выбирать диск только по букве устройства. `smartctl` при необходимости устанавливается через `sudo apt install smartmontools`.

## Форматирование уже выполнено — НЕ повторять

HDD уже содержит ext4 с UUID из README. Создание новой таблицы разделов, `mkfs`, `wipefs` без `-n` и перезапись диска через `dd` могут уничтожить фотографии и метаданные. При проблемах с монтированием форматирование не является способом восстановления. На новом сменном диске UUID будет другим.

Дополнительный журнал подтвердил выполненную разметку GPT, один раздел от 1 MiB до конца диска и ext4 с меткой `immich-data`, без резервирования блоков для root (`-m 0`). Исторические команды сохранены в [журнале](PROGRESS.md); повторять их на существующем диске нельзя.

## Монтирование существующего ext4

В переписке выполнены:

```bash
sudo mkdir -p /mnt/photos
sudo cp /etc/fstab /etc/fstab.backup-immich
sudo nano /etc/fstab
sudo systemctl daemon-reload
sudo mount /mnt/photos
findmnt /mnt/photos
df -hT /mnt/photos
sudo mkdir -p /mnt/photos/immich
sudo chown nick:nick /mnt/photos/immich
```

Не повторять копирование поверх исторического `fstab.backup-immich`: для будущих изменений использовать новую копию:

```bash
sudo cp -a /etc/fstab "/etc/fstab.backup-$(date +%Y%m%d-%H%M%S)"
sudo nano /etc/fstab
```

Точная строка исходного `/etc/fstab` не показана. Ниже **предлагаемый образец**, а не установленный факт; сначала сверить существующую запись и не добавлять дубликат:

```fstab
UUID=f8d17a00-1f7e-4ed9-86be-2a71af0587b7 /mnt/photos ext4 defaults 0 2
```

Этот вариант без `nofail` может перевести загрузку в emergency mode, если HDD недоступен. `nofail` позволяет продолжить загрузку, но без отдельной защиты запуска Immich увеличивает риск записи в пустой каталог на системном NVMe. Выбор делать вместе с настройкой зависимостей сервисов.

После правки:

```bash
sudo findmnt --verify --verbose
sudo systemctl daemon-reload
# Только если раздел ещё не смонтирован:
sudo mount /mnt/photos
findmnt --mountpoint /mnt/photos -o SOURCE,UUID,FSTYPE,OPTIONS,TARGET
```

Создавать каталог и менять владельца следует только после подтверждения правильного UUID. Не применять рекурсивный `chown` к будущему каталогу PostgreSQL.

## Docker: инструкция для новой установки Debian 13

Docker уже установлен; ниже воспроизводимая процедура по [официальной инструкции Docker](https://docs.docker.com/engine/install/debian/), а не полный исторический журнал установки. Установка без фиксации версий получает доступные на тот момент пакеты, не обязательно 29.9.2 / v5.6.1.

Сначала проверить наличие конфликтующих пакетов; не удалять используемый другим сервисом runtime вслепую:

```bash
dpkg -l docker.io docker-compose docker-doc docker-buildx podman-docker containerd runc
```

На чистой системе без конфликтов:

```bash
sudo apt update
sudo apt install ca-certificates curl
sudo install -m 0755 -d /etc/apt/keyrings
sudo curl -fsSL https://download.docker.com/linux/debian/gpg -o /etc/apt/keyrings/docker.asc
sudo chmod a+r /etc/apt/keyrings/docker.asc
sudo tee /etc/apt/sources.list.d/docker.sources >/dev/null <<EOF
Types: deb
URIs: https://download.docker.com/linux/debian
Suites: trixie
Components: stable
Architectures: $(dpkg --print-architecture)
Signed-By: /etc/apt/keyrings/docker.asc
EOF
sudo apt update
sudo apt install docker-ce docker-ce-cli containerd.io docker-buildx-plugin docker-compose-plugin
sudo systemctl enable --now docker
sudo systemctl is-active docker
sudo systemctl is-enabled docker
docker --version
docker compose version
sudo docker run --rm hello-world
```

Для точного воспроизведения сначала получить доступные версии через `apt-cache madison docker-ce docker-ce-cli docker-compose-plugin`, сверить их с [журналом установки](PROGRESS.md) и установить явно. Старые версии могут отсутствовать в репозитории; не заменять их молча и не откатывать работающий Docker ради совпадения с журналом.

Команды используют `sudo`: добавление пользователя в группу `docker` даёт полномочия, практически эквивалентные root. Членство `nick` в этой группе не подтверждено.

## Docker и UFW

Публикация порта контейнера может обходить обычные правила UFW: одного `ufw deny` недостаточно. Не считать будущий порт 2283 закрытым без проверки с другого устройства. Для первого запуска использовать привязку к `127.0.0.1` и SSH-туннель. Не включать перенаправление порта на роутере до выбора защищённого способа доступа.

Для backend iptables правила фильтрации Docker обычно размещают в `DOCKER-USER`; для backend nftables подход отличается, этой цепочки может не быть. Сначала определить фактический backend. Не отключать управление iptables Docker вслепую — это может нарушить сеть контейнеров. См. [Docker: packet filtering and firewalls](https://docs.docker.com/engine/network/packet-filtering-firewalls/).
