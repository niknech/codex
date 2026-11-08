# 4. AdGuard VPN

[Оглавление](../../README.md) · [Назад](03-nutrition-bot.md) · [Далее](05-wifi.md)

## Как работает

AdGuard в режиме SOCKS слушает `127.0.0.1:1080`. Служба `adguardvpn-bot` запускается от `nick`; настройки и авторизация — `/home/nick/.local/share/adguardvpn-cli`. Бот использует прокси напрямую, Wi-Fi — через tun2socks. Остальные приложения без настройки прокси выходят напрямую. Tailscale нужен для удалённого входа и не заменяет AdGuard.

**Перезапуск AdGuard прерывает интернет и у бота, и у NICK-VPN.** Используй обычную LAN для администрирования.

## Проверить

**СЕРВЕР, под nick:**

```bash
systemctl status adguardvpn-bot --no-pager
sudo ss -lntp 'sport = :1080'
adguardvpn-cli status
```

В переписке статус CLI расходился с работающим прокси. Проверяй запрос:

```bash
curl -4 --noproxy '' --proxy socks5h://127.0.0.1:1080 \
  --connect-timeout 10 --max-time 30 -w '\n' https://api.ipify.org
```

Сравни с прямым выходом:

```bash
curl -4 --noproxy '*' --connect-timeout 10 --max-time 30 -w '\n' https://api.ipify.org
```

Разные IP подтверждают разные пути только для этого запроса, не всех сайтов или протоколов. Если одинаковые, проверь исключения:

```bash
adguardvpn-cli site-exclusions mode
adguardvpn-cli site-exclusions show
```

## Восстановить соединение

Сначала журнал:

```bash
sudo journalctl -u adguardvpn-bot -n 80 --no-pager
```

При необходимости:

```bash
sudo systemctl restart adguardvpn-bot
```

Через несколько секунд повтори запрос, проверь Telegram и телефон. Если прокси работает, а Wi-Fi нет, смотри раздел 5.

## Сменить страну

Сначала узнай доступные локации и синтаксис установленной версии:

```bash
adguardvpn-cli list-locations
adguardvpn-cli connect --help
```

Для версии с `--location`:

1. `sudo systemctl stop adguardvpn-bot`.
2. Под **nick, без sudo**: `adguardvpn-cli connect --location LOCATION`, заменив LOCATION выбранным кодом из списка.
3. Убедись, что подключение успешно, проверь `adguardvpn-cli status`.
4. `adguardvpn-cli disconnect`, затем `sudo systemctl start adguardvpn-bot`.
5. Повтори проверку прокси и приложений.

Если выбор не удался, верни запуск службы и проверь журнал. Не оставляй ручной процесс одновременно со службой. Не используй `sudo adguardvpn-cli`: root имеет другое хранилище настроек. Для повторной авторизации останови службу, выполни `adguardvpn-cli login` под nick, затем запусти службу. Пароль и код вводи локально.

## Обновление и настройки

Сохрани закрытую копию каталога авторизации и unit по разделу 7. Сверь `adguardvpn-cli --help` с [официальной документацией CLI](https://github.com/AdguardTeam/AdGuardVPNCLI), обновляй в окно обслуживания. После обновления проверь SOCKS, бот и Wi-Fi. Не переключай режим на TUN: эта схема зависит от SOCKS на 1080. Пароли и каталог авторизации в GitHub не загружать.
