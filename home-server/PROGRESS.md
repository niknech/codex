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
