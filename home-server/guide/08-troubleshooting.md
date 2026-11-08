# 8. Если что-то не работает

[Оглавление](../../README.md) · [Назад](07-backups.md) · [Далее](09-reference.md)

## Выбрать симптом

| Симптом | Первое действие | Куда дальше |
|---|---|---|
| SSH: timeout | Проверить питание, обычную LAN, адрес роутера или Tailscale | [Подключение](01-connect.md) |
| SSH: connection refused | Через монитор проверить `systemctl status ssh` и адрес | Не менять VPN вслепую |
| SSH: permission denied | Проверить пользователя nick, пароль/ключ | Не переустанавливать сервер |
| Бот не отвечает | Статус бота, журнал, запрос через прокси | [Бот](03-nutrition-bot.md), [AdGuard](04-adguard.md) |
| Ручной ввод работает, AI нет | AI_ENABLED, ключ, лимиты, модель, журнал | Проверять AI отдельно от Telegram |
| Telegram 409 | Найти второй экземпляр с тем же токеном | Не запускать бинарник вручную рядом со службой |
| NICK-VPN отсутствует | Служба AP, `nmcli device status`, `nmcli radio` | [Wi-Fi](05-wifi.md) |
| Wi-Fi подключён, адрес не выдан | addresses, журнал dnsmasq, DHCP | Не создавать второй DHCP |
| Адрес есть, сайтов нет | AdGuard → nvpn0 → таблица 177 → nftables | Проверять по этой цепочке |
| Wi-Fi без VPN-туннеля всё равно даёт интернет | Остановить AP, исключить мобильную сеть телефона | Повторить проверку защиты |
| Immich нигде не открывается | Проверка HDD, immich, Compose, localhost | [Immich](06-immich.md) |
| Localhost работает, HTTPS нет | Tailscale клиента, tailscaled, Serve | Не менять БД |
| HTTPS работает, LAN нет | Текущий IP enp2s0, привязка Compose, гостевая сеть | LAN уже настроена; не запускать установку заново |
| Storage check failed | `findmnt --mountpoint /mnt/photos`, UUID и журнал mount | Не форматировать диск |
| Start request repeated too quickly | Найти первую ошибку в журнале | Исправить, затем reset-failed и start |
| Нет места | `df -h`, `df -i`, `sudo docker system df` | Не удалять postgres, фото или volumes |
| Фото телефона не видны | Разрешения iOS, локальные альбомы, очередь загрузки | Не путать с серверной лентой |

## Собрать диагностику без изменения настроек

**СЕРВЕР:**

```bash
date -Is
uptime
systemctl --failed --no-pager
systemctl is-active nutrition-bot adguardvpn-bot tailscaled docker immich
systemctl is-active nick-wifi-guard nick-wifi-vpn nick-wifi-ap nick-wifi-addresses
findmnt --mountpoint /mnt/photos
df -hT / /mnt/photos
free -h
```

Добавь журнал только проблемной службы, например:

```bash
sudo journalctl -u nick-wifi-vpn -b -n 80 --no-pager
```

Для Immich:

```bash
cd /home/nick/immich-app
sudo docker compose ps -a
sudo docker compose logs --tail 60 immich-server
```

Напиши: что именно не работает, с какого устройства и сети, когда началось, что менялось перед этим. Перед отправкой просмотри вывод: убери токены, пароли, личные адреса, имена и сведения пользователей. Не отправляй `.env`, базы и весь `docker inspect`.

## Чего не делать в качестве «быстрого ремонта»

- Не форматировать диск командами mkfs/parted/wipefs.
- Не выполнять `docker compose down -v` и `docker system prune --volumes`.
- Не очищать все правила `nft flush ruleset` и не отключать UFW: это ломает изоляцию и другие сети.
- Не менять глобальный default route сервера ради Wi-Fi: для неё есть таблица 177.
- Не перезапускать весь NetworkManager, Docker или сервер, если достаточно одной службы.
- Не считать `active` доказательством исправной загрузки, VPN или наличия копий.

Если диск отсутствует или есть ошибки чтения, оставь Immich остановленным и сначала обеспечь сохранность данных.
