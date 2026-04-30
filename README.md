# Красивый live-дашборд из Excel

## Что делает проект
- Читает `data.xlsx` (должен лежать рядом с `app.py`).
- Каждые **1.5 секунды** добавляет по одной строке в дашборд.
- Обновляет:
  - график `Q_IL` и `Q_IL_pred` от `Date`;
  - график `mape` от `Date`;
  - график `mae` от `Date`;
  - таблицу справа: `mae`, `mean_mae`, `mean_mape`, `mean_Q_IL_pred`.

## Как запустить (очень просто)
1. Установи Python 3.10+.
2. В папке проекта выполни:
   ```bash
   python -m venv .venv
   source .venv/bin/activate  # Windows: .venv\\Scripts\\activate
   pip install -r requirements.txt
   ```
3. Положи свой Excel-файл в корень проекта и назови его `data.xlsx`.
4. Запусти:
   ```bash
   python app.py
   ```
5. Открой в браузере: http://127.0.0.1:5000

## Важный формат колонок в Excel
`Date, Q_IL, Q_IL_pred, mape, mae, mean_mae, mean_mape, mean_Q_IL_pred`
