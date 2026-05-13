from __future__ import annotations

from pathlib import Path
from typing import Any

import pandas as pd
from flask import Flask, jsonify, render_template

BASE_DIR = Path(__file__).resolve().parent
DATA_FILE = BASE_DIR / "data.xlsx"

app = Flask(__name__)


def load_data() -> list[dict[str, Any]]:
    if not DATA_FILE.exists():
        return []

    df = pd.read_excel(DATA_FILE)
    required_columns = [
        "Date",
        "Q_IL",
        "Q_IL_pred",
        "mape",
        "mae",
        "mean_mae",
        "mean_mape",
        "mean_Q_IL_pred",
    ]

    missing = [col for col in required_columns if col not in df.columns]
    if missing:
        raise ValueError(f"В файле не хватает колонок: {', '.join(missing)}")

    df = df[required_columns].copy()
    df["Date"] = pd.to_datetime(df["Date"], errors="coerce").dt.strftime("%Y-%m-%d %H:%M:%S")
    return df.fillna(0).to_dict(orient="records")


@app.route("/")
def index() -> str:
    return render_template("index.html")


@app.route("/api/data")
def api_data():
    rows = load_data()
    return jsonify(rows)


if __name__ == "__main__":
    app.run(debug=True)
