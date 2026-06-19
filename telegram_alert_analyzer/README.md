# Telegram Alert Analyzer

Локальное MVP-приложение для выгрузки, хранения, классификации и анализа сообщений публичного Telegram-канала `@lpr1_treugolnik` за последние 365 дней. Приложение не делает прогнозов, не выдает военных рекомендаций и не занимается real-time alerting — только ретроспективная аналитика публичных сообщений.

## Что умеет

1. Выгружает историю через Telethon / MTProto, а не через Telegram Bot API.
2. Сохраняет сырые сообщения в SQLite без изменений.
3. Повторный запуск безопасен: дубликаты не создаются.
4. Парсит сообщения rule-based алгоритмом.
5. Определяет дату, время, регион, город, тип тревоги, тип вооружения, направление и статус.
6. Показывает Streamlit-дашборд с фильтрами, графиками и таблицами ручной проверки.
7. Экспортирует очищенные данные в CSV и XLSX.

## Как получить TELEGRAM_API_ID и TELEGRAM_API_HASH

1. Откройте сайт <https://my.telegram.org>.
2. Войдите по номеру телефона Telegram.
3. Откройте раздел **API development tools**.
4. Создайте приложение. Название можно выбрать любое, например `alert-analyzer`.
5. Скопируйте `api_id` и `api_hash`.
6. Никому их не отправляйте и не публикуйте.

## Установка

```bash
python -m venv .venv
source .venv/bin/activate  # для Linux/macOS
.venv\Scripts\activate     # для Windows
pip install -r requirements.txt
```

## Настройка .env

Скопируйте пример:

```bash
cp .env.example .env
```

Откройте `.env` и заполните:

```env
TELEGRAM_API_ID=ваш_api_id
TELEGRAM_API_HASH=ваш_api_hash
TELEGRAM_PHONE=ваш_номер_телефона
TELEGRAM_CHANNEL_USERNAME=@lpr1_treugolnik
DATABASE_URL=sqlite:///alerts.db
```

Все приватные данные должны быть только в `.env`. Session-файлы Telethon не коммитятся.

## Инициализация базы

```bash
python scripts/init_db.py
```

Будут созданы таблицы `raw_messages`, `parsed_alerts`, `message_parse_errors`.

## Выгрузка сообщений

```bash
python scripts/export_telegram.py
```

При первом запуске Telegram может попросить код входа. Скрипт выгружает сообщения за последние 365 дней, пропускает пустые сообщения и сохраняет ссылки вида `https://t.me/<channel>/<message_id>`.

## Парсинг сообщений

```bash
python scripts/parse_messages.py
```

Сырые сообщения не удаляются. Результаты сохраняются с `parser_version`, чтобы позже можно было перепарсить данные новой версией алгоритма.

## Запуск дашборда

```bash
streamlit run app.py
```

В дашборде есть разделы: обзор, динамика, время, география, типы угроз, качество парсинга и исходные сообщения.

## Экспорт данных

Откройте раздел **Исходные сообщения** в Streamlit и нажмите кнопку скачивания CSV или XLSX.

## Как дополнять geo_reference.csv

Файл находится здесь: `data/geo_reference.csv`.

Формат строк:

```csv
city,region,aliases,latitude,longitude
Луганск,ЛНР,"луганск;луганске;луганска",48.5740,39.3078
```

- `city` — основное название города.
- `region` — регион.
- `aliases` — варианты написания через точку с запятой.
- `latitude` и `longitude` — координаты, можно оставить пустыми.

Если найдено несколько городов, приложение не угадывает город и снижает confidence.

## Редактирование правил классификации

Правила лежат в `data/classifier_rules.json`. Можно добавлять слова для БПЛА, ракет, артиллерии, отбоя и направлений.

## Команды полного цикла

```bash
python scripts/init_db.py
python scripts/export_telegram.py
python scripts/parse_messages.py
streamlit run app.py
```
