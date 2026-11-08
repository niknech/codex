# 5. Wi-Fi NICK-VPN

[Оглавление](../../README.md) · [Назад](04-adguard.md) · [Далее](06-immich.md)

## Как устроено

```text
Телефон → NICK-VPN (wlp3s0, 10.77.0.1/24)
        → таблица 177 → nvpn0 → tun2socks
        → AdGuard SOCKS 127.0.0.1:1080 → интернет
```

Интернет приходит через `enp2s0`. Wi-Fi управляет NetworkManager; адреса выдаёт отдельный dnsmasq. Не переключай профиль на `ipv4.method shared`: это другая схема маршрутизации.

| Служба | Назначение |
|---|---|
| `nick-wifi-guard` | nftables и политика маршрутов |
| `nick-wifi-vpn` | tun2socks, интерфейс nvpn0 |
| `nick-wifi-ap` | Wi-Fi-профиль NICK-VPN |
| `nick-wifi-addresses` | DHCP и основная точка автозапуска |

Правила пропускают TCP/UDP клиентов через туннель; IPv6 и остальные протоколы блокируют. Ping с телефона поэтому не подходит для проверки. Доступ клиентов к самому серверу ограничен DHCP: для SSH/Immich используй обычную LAN или отдельно работающий Tailscale, не удаляй защиту.

## Проверить

**СЕРВЕР:**

```bash
systemctl is-active nick-wifi-guard nick-wifi-vpn nick-wifi-ap nick-wifi-addresses
systemctl is-enabled nick-wifi-addresses
nmcli -f NAME,UUID,TYPE,DEVICE connection show --active
ip -brief address show wlp3s0
ip -brief address show nvpn0
ip -4 rule show
ip -4 route show table 177
sudo nft list table inet nick_wifi
```

Ожидаем четыре `active`, затем `enabled`, адреса `10.77.0.1/24` и `198.18.77.1/30`, маршрут `default dev nvpn0` в таблице 177. Правила 1100/1101 выбирают таблицу и запрещают обход для входящего трафика wlp3s0.

На телефоне отключи мобильный интернет, подключись к NICK-VPN, открой новый сайт. DHCP выдаёт `10.77.0.50–10.77.0.150`, шлюз `10.77.0.1`. Подключённый Wi-Fi не равнозначен работающему интернету.

## Включить и выключить

Работай с ноутбука в **обычной домашней сети**, иначе потеряешь доступ.

Временно выключить раздачу:

```bash
sudo systemctl stop nick-wifi-addresses nick-wifi-ap
```

Включить обратно:

```bash
sudo systemctl start nick-wifi-addresses
```

Служба подтянет AP и зависимости. Проверь четыре службы. При failed сначала разбери журнал; затем можно `sudo systemctl reset-failed nick-wifi-ap nick-wifi-addresses` и повторить запуск.

Убрать автозапуск раздачи:

```bash
sudo systemctl disable --now nick-wifi-addresses
sudo systemctl stop nick-wifi-ap
```

Вернуть:

```bash
sudo systemctl enable --now nick-wifi-addresses
```

Остановка одного DHCP не отключает клиентов с выданным адресом, поэтому выключаем и AP. Guard оставляй работающим. AdGuard нужен боту независимо от Wi-Fi.

## Поменять пароль

Сначала проверь профиль:

```bash
nmcli -f NAME,UUID,TYPE connection show
```

Исторический UUID NICK-VPN — `b61bf30e-b068-4551-a718-416d50ddaa18`. При пересоздании профиля сверяй `systemctl cat nick-wifi-ap`: служба должна ссылаться на действующий UUID. Не создавай дубликат.

Сохрани закрытую копию NetworkManager по разделу 7. Открой:

```bash
sudo nmtui
```

Выбери **Изменить соединение → NICK-VPN → пароль → OK → Выход**. Если nmtui не установлен, сначала установи соответствующий пакет для Debian или используй доступный редактор NetworkManager; не пересоздавай сеть. Меняй только пароль: режим AP, адреса, отключённый IPv6 и отключённый autoconnect профиля сохрани. Пароль WPA2 — 8–63 символа.

Применить (отключит клиентов):

```bash
sudo systemctl restart nick-wifi-ap
sudo systemctl start nick-wifi-addresses
```

На телефоне при необходимости забудь сеть и подключись заново. Проверь интернет. Не передавай пароль в аргументах команды, сохраняющихся в истории.

## Проверить блокировку выхода без VPN

Только при независимом LAN-доступе к серверу. На телефоне выключи мобильный интернет и другие VPN.

1. Убедись, что сайт через NICK-VPN открывается.
2. На сервере: `sudo systemctl stop nick-wifi-vpn`.
3. Новый сайт на телефоне открываться не должен. Прямой запрос с сервера из раздела 4 должен работать.
4. `sudo systemctl start nick-wifi-vpn`.
5. `ip -4 route show table 177` снова показывает `default dev nvpn0`, интернет на телефоне возвращается.

Этот тест и восстановление маршрута после restart подтверждены в переписке. Проверка всей цепочки после reboot не подтверждена. Если телефон выходит напрямую без туннеля, останови AP и разбери правила.

## Файлы и обслуживание

DHCP: `/etc/nick-wifi/dhcp.conf`. Правила: `/etc/nick-wifi/guard.nft`. Загрузка защиты: `/etc/nick-wifi/guard.sh`. Возврат маршрута: `/etc/systemd/system/nick-wifi-vpn.service.d/20-routing.conf`. DHCP выдаёт DNS `1.1.1.1` и `9.9.9.9`; dnsmasq с `port=0` сам DNS не обслуживает.

Перед изменениями сохрани эти файлы, units и профиль NetworkManager. Для обновления tun2socks (исторически 2.7.0) сохрани прежний бинарник, проверь официальный выпуск и повтори тест блокировки. Установочные блоки из старого чата поверх текущих настроек не запускай.

Перед применением восстановленных настроек проверь:

```bash
sudo nft -c -f /etc/nick-wifi/guard.nft
sudo dnsmasq --test --conf-file=/etc/nick-wifi/dhcp.conf
```

Применение выполняй в окно обслуживания с LAN-доступом. Источники: [NetworkManager](https://networkmanager.pages.freedesktop.org/NetworkManager/NetworkManager/nmcli.html), [tun2socks](https://github.com/xjasonlyu/tun2socks/wiki/Examples).
