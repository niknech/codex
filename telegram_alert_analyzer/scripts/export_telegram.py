from pathlib import Path
import asyncio, sys
sys.path.append(str(Path(__file__).resolve().parents[1]))
from src.telegram_exporter import export_last_year

if __name__ == '__main__':
    asyncio.run(export_last_year())
