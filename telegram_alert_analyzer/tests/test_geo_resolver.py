from pathlib import Path
from src.geo_resolver import GeoResolver

def test_city_region_from_reference():
    g = GeoResolver().resolve('Тревога в Луганске')
    assert g.city == 'Луганск'; assert g.region == 'ЛНР'

def test_ambiguous_city(tmp_path: Path):
    p = tmp_path / 'geo.csv'
    p.write_text('city,region,aliases,latitude,longitude\nМирный,Регион1,мирный,,\nМирный,Регион2,мирный,,\n', encoding='utf-8')
    g = GeoResolver(p).resolve('Опасность в Мирный')
    assert g.ambiguous is True
    assert g.city is None
