# Журнал настройки

## 2026-11-01 — уточнение по полному журналу терминала

Источник: предоставленный владельцем вывод команд. Сырой файл не включён: он содержит сетевые адреса, идентификатор учётной записи, серийный номер и WWN диска. Указанные ниже результаты относятся к моменту выполнения команд, а не к непрерывному мониторингу.

### Подтверждено

- На новом HDD создана GPT и один раздел ext4. Историческая последовательность **разрушительных** команд: `sudo parted -s /dev/sda mklabel gpt`; `sudo parted -s -a optimal /dev/sda mkpart primary ext4 1MiB 100%`; `sudo partprobe /dev/sda`; `sudo mkfs.ext4 -m 0 -L immich-data /dev/sda1`. Это запись уже выполненных действий, не инструкция для повторного запуска.
- `/mnt/photos` действительно смонтирован с UUID `f8d17a00-1f7e-4ed9-86be-2a71af0587b7`, ext4, `rw,relatime`.
- `/mnt/photos/immich`: владелец `nick:nick`, права `755`.
- CPU Intel Core i5-6500T @ 2.50 GHz, 4 логических CPU, x86_64.
- RAM 7,5 GiB, доступно 6,8 GiB; swap 6,2 GiB, не использовался.
- На `/` свободно около 98 GiB, на HDD около 3,6 TiB.
- Docker установлен из официального APT-репозитория для Debian trixie; сервис active/enabled, hello-world успешен.
- `docker ps` вывел только заголовок: запущенных контейнеров на момент проверки нет. Отсутствие остановленных контейнеров этой командой не проверялось.
- Ранее в том же журнале `nutrition-bot`, `adguardvpn-bot`, `tailscaled`, `tailscale-relay` были active/enabled; failed units отсутствовали. Эти проверки предшествовали установке Docker. Доступ через Tailscale уже используется; сетевые адреса здесь не публикуются.

### Версии пакетов из журнала установки

| Пакет | Версия |
| --- | --- |
| docker-ce / docker-ce-cli / docker-ce-rootless-extras | `5:29.9.2-1~debian.13~trixie` |
| docker-compose-plugin | `5.6.1-1~debian.13~trixie` |
| containerd.io | `2.4.2-3~debian.13~trixie` |
| docker-buildx-plugin | `0.38.1-1~debian.13~trixie` |

### Точка продолжения

Immich пока не установлен. Следующий шаг — прочитать фактическую запись `/mnt/photos` в `fstab`, проверить существующие контейнеры и конфигурационные каталоги. Затем выбрать релиз и настроить запуск с проверкой HDD. Не повторять разметку, форматирование и установку Docker.

Следующие диагностические команды предложены, **их результат пока не получен**:

```bash
awk '$1 !~ /^#/ && $2 == "/mnt/photos" {print}' /etc/fstab
sudo findmnt --verify --verbose
sudo docker ps -a --format 'table {{.Names}}\t{{.Status}}\t{{.Ports}}'
ls -ld /home/nick/immich-app /opt/immich 2>/dev/null
systemctl is-active nutrition-bot adguardvpn-bot tailscaled tailscale-relay
```

Если указанных каталогов нет, `ls` ничего не выведет — это нормально. Проверка `fstab` может показывать другие пути; перед отправкой вывода скрыть сетевые адреса или учётные данные, если они встретятся. Не отправлять `.env`.

## 2026-11-01 — результаты проверки перед подготовкой Compose

Результаты предыдущего блока получены:

- Запись HDD: `UUID=f8d17a00-1f7e-4ed9-86be-2a71af0587b7 /mnt/photos ext4 defaults,nofail,x-systemd.device-timeout=15s 0 2`.
- `findmnt --verify --verbose`: Success, no errors or warnings detected.
- `docker ps -a`: только заголовок, контейнеров нет (включая остановленные).
- `ls` не вывел каталогов `/home/nick/immich-app` и `/opt/immich`; stderr был скрыт, поэтому окончательная проверка отсутствия будет при создании нового каталога.
- `nutrition-bot`, `adguardvpn-bot`, `tailscaled`, `tailscale-relay`: все active после установки Docker.
- Перезагрузка и проверка загрузки с HDD пока не выполнены.

Для следующего шага выбран [Immich v3.3.5](https://github.com/immich-app/immich/releases/tag/v3.3.5), который официальный releases/latest вернул при проверке. Это выбор релиза, не подтверждение установки.

### Предложенный следующий шаг — пока не подтверждён

Скачать официальные шаблоны в новый закрытый каталог без запуска контейнеров. Выполнять блок целиком в Bash; при ошибке подкоманд оболочка блока завершится, текущая SSH-сессия останется открытой:

```bash
(
set -eu
umask 077
mkdir -m 700 /home/nick/immich-app
cd /home/nick/immich-app
curl -fL --retry 3 https://github.com/immich-app/immich/releases/download/v3.3.5/docker-compose.yml -o docker-compose.yml
curl -fL --retry 3 https://github.com/immich-app/immich/releases/download/v3.3.5/example.env -o example.env
ls -l docker-compose.yml example.env
cat docker-compose.yml
)
```

Если каталог уже существует, остановиться и проверить его содержимое, не удалять и не перезаписывать. На этом шаге `.env` с рабочим паролем ещё не создаётся. После получения Compose предстоит задать пути, пароль, loopback-порт и защиту автозапуска при отсутствии HDD; `docker compose up` пока не выполнять.

## 2026-11-01 — шаблоны v3.3.5 скачаны

Подтверждён результат предыдущего шага: `docker-compose.yml` (2844 байта) и `example.env` (979 байт), оба `nick:nick`, права 600. В Compose четыре сервиса: immich-server, immich-machine-learning, redis (Valkey), database (PostgreSQL). У всех исходно `restart: always`; порт сервера `2283:2283` опубликован без ограничения интерфейса. Файлы только скачаны, контейнеры не запускались.

Следующий предложенный, ещё не подтверждённый шаг: [подготовка конфигурации](CONFIGURE.md). Выбрано управление запуском через отдельный systemd-сервис; для исключения обхода проверки HDD контейнерные restart policies будут отключены. Сервис и проверка UUID пока не созданы.

## 2026-11-01 — конфигурация подготовлена

Получен успешный вывод проверки: UPLOAD_LOCATION=/mnt/photos/immich, DB_DATA_LOCATION=/home/nick/immich-app/postgres, TZ=Europe/Moscow, IMMICH_VERSION=v3.3.5. Порт 127.0.0.1:2283:2283; четыре restart: "no"; `.env` — 600 nick:nick. Пароль не передавался. Контейнеры пока не запускались. Вставка команд в терминале визуально искажена, но итоговые строки проверок присутствуют.

Следующий этап: [установка сервиса и первый запуск](SERVICE.md). Файлы сервиса подготовлены в репозитории; их выполнение на сервере пока не подтверждено.
