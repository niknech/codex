# Подготовка конфигурации v3.3.5

Статус: команды предложены владельцу, результат выполнения пока не получен. Предусловие: новый каталог `/home/nick/immich-app` с официальными `docker-compose.yml` и `example.env` релиза v3.3.5, без рабочей `.env`.

План: оригиналы на HDD, PostgreSQL на NVMe, порт только localhost. `restart: "no"` исключает самостоятельный старт контейнеров при запуске Docker; управление жизненным циклом будет возложено на отдельный systemd-сервис с проверкой mount и UUID. Сам этот шаг такой защиты ещё не создаёт. Контейнеры не запускать до настройки сервиса.

По [документации Docker](https://docs.docker.com/engine/containers/start-containers-automatically/) при использовании менеджера процессов следует избегать одновременного применения Docker restart policies. В выбранной схеме рестарты будет выполнять systemd.

Выполнить на NICK-SERV под пользователем nick:

```bash
(
set -eu
umask 077
cd /home/nick/immich-app
test ! -e .env || { echo 'STOP: .env уже существует'; exit 1; }
test ! -e docker-compose.yml.original || { echo 'STOP: резервная копия уже существует'; exit 1; }
command -v openssl >/dev/null
test "$(grep -c 'restart: always' docker-compose.yml)" -eq 4
grep -q "'2283:2283'" docker-compose.yml
cp -p docker-compose.yml docker-compose.yml.original
DB_SECRET=$(openssl rand -hex 32)
cat > .env <<EOF
UPLOAD_LOCATION=/mnt/photos/immich
DB_DATA_LOCATION=/home/nick/immich-app/postgres
TZ=Europe/Moscow
IMMICH_VERSION=v3.3.5
DB_PASSWORD=$DB_SECRET
DB_USERNAME=postgres
DB_DATABASE_NAME=immich
EOF
unset DB_SECRET
chmod 600 .env
sed -i "s/'2283:2283'/'127.0.0.1:2283:2283'/; s/restart: always/restart: \"no\"/" docker-compose.yml
sudo docker compose config --quiet
echo 'Конфигурация проверена; контейнеры не запускались'
grep -E '^(UPLOAD_LOCATION|DB_DATA_LOCATION|TZ|IMMICH_VERSION)=' .env
grep -E 'restart:|127\.0\.0\.1:2283' docker-compose.yml
stat -c '%a %U:%G %n' .env
)
```

Ожидается: проверка Compose без ошибок; четыре `restart: "no"`; порт `127.0.0.1:2283:2283`; `.env` с правами 600, владелец nick. Пароль создаётся на сервере и не выводится. Не отправлять `.env`, полный `docker compose config` или выполнять этот блок с `set -x`.

При ошибке остановиться и сообщить её текст: блок специально не перезаписывает существующую `.env` при повторном запуске. В этот момент база ещё не создана, но пароль необходимо сохранить для дальнейшей работы. Доступ с компьютера первоначально будет через SSH-туннель, далее можно настроить Tailscale. Порт локальный, прямое открытие по IP сервера пока не работает.
