# Сервис Immich с проверкой диска

Статус: файлы подготовлены, установка на сервер и первый запуск ещё не подтверждены.

Файлы: [проверка хранилища](systemd/immich-check-storage.sh), [systemd unit](systemd/immich.service). Установка: скрипт в `/usr/local/sbin/immich-check-storage` (root:root, 755), unit в `/etc/systemd/system/immich.service` (root:root, 644). Исходники скачивать из конкретного коммита этого репозитория. Существующие файлы перед заменой проверить и сохранить.

## Поведение

- Все контейнеры имеют `restart: "no"`; старт и рестарты выполняет systemd.
- `RequiresMountsFor` запрашивает mount даже при `nofail`; `BindsTo` и `After` связывают сервис с mount и Docker.
- Перед каждым стартом проверяются mount, точный UUID, ext4, режим rw и каталог медиатеки. При ошибке контейнеры не запускаются.
- Compose работает на переднем плане с `--abort-on-container-exit`. Выход любого контейнера останавливает остальные, после чего systemd перезапускает стек. Это затрагивает весь Immich, не другие сервисы.
- После трёх быстрых неудачных запусков за 300 секунд автоматические попытки прекращаются. После устранения причины: `sudo systemctl reset-failed immich` и `sudo systemctl start immich`.
- Состояние `active` означает работу процесса Compose, а не готовность всех контейнеров: отдельно проверять health и HTTP.
- Образы заранее скачиваются через `sudo docker compose pull`; unit использует `--pull never`.
- При остановке Docker или исчезновении mount Immich останавливается. После возвращения зависимости может потребоваться ручной `systemctl start immich`.
- Это защита запуска и реакция на исчезновение mount, не гарантия при любой физической неисправности HDD. Отказ диска может оставить mount активным; нужны мониторинг и резервные копии.
- Прямой `docker compose up` обходит проверку. В дальнейшем использовать `systemctl start/stop/restart immich`.

## Проверка и первый запуск

После установки файлов:

```bash
sudo /usr/local/sbin/immich-check-storage
sudo systemctl daemon-reload
sudo systemd-analyze verify /etc/systemd/system/immich.service
cd /home/nick/immich-app
sudo docker compose pull
sudo systemctl enable --now immich
sudo systemctl status immich --no-pager -l
sudo docker compose ps -a
curl -I --max-time 10 http://127.0.0.1:2283
```

HTTP может быть недоступен в первые секунды инициализации; повторить после проверки состояния контейнеров. При ошибке: `sudo journalctl -u immich -b --no-pager -n 80`; перед отправкой логов проверить отсутствие персональных данных. Не выводить полный Compose config и `.env`.

Проверку отказа при неверном UUID, загрузку после reboot и восстановление после сбоя предстоит выполнить отдельно до реального импорта. Не отключать HDD физически для теста работающей базы/медиатеки.

Источники: [Compose up](https://docs.docker.com/reference/cli/docker/compose/up/), [systemd dependencies](https://www.freedesktop.org/software/systemd/man/latest/systemd.unit.html).
