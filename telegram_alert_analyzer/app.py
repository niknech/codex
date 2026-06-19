from __future__ import annotations

import pandas as pd
import plotly.express as px
import streamlit as st
from src.dashboard_data import load_dashboard_data
from src.export import to_csv_bytes, to_xlsx_bytes

st.set_page_config(page_title='Telegram Alert Analyzer', layout='wide')
st.title('Telegram Alert Analyzer: ретроспективная аналитика тревог')

@st.cache_data(ttl=60)
def data():
    df = load_dashboard_data()
    if not df.empty:
        df['event_date'] = pd.to_datetime(df['event_date'], errors='coerce')
        df['hour'] = pd.to_datetime(df['event_time'], errors='coerce').dt.hour
    return df

df = data()
if df.empty:
    st.info('Данных пока нет. Запустите init_db, export_telegram и parse_messages.')
    st.stop()

with st.sidebar:
    st.header('Фильтры')
    for col, label in [('region','Регион'), ('city','Город'), ('alert_type','Тип тревоги'), ('weapon_type','Оружие')]:
        vals = sorted([x for x in df[col].dropna().unique()]) if col in df else []
        picked = st.multiselect(label, vals)
        if picked:
            df = df[df[col].isin(picked)]

tabs = st.tabs(['Обзор','Динамика','Время','География','Типы угроз','Качество парсинга','Исходные сообщения'])
with tabs[0]:
    c1,c2,c3,c4 = st.columns(4)
    c1.metric('Сообщений', len(df)); c2.metric('Распознано', int((df.alert_type!='unknown').sum()))
    c3.metric('Unknown', int((df.alert_type=='unknown').sum()))
    c4.metric('Период', f"{df.event_date.min().date()} — {df.event_date.max().date()}")
    st.plotly_chart(px.bar(df.region.value_counts().reset_index(), x='region', y='count', title='Топ регионов'), use_container_width=True)
    st.plotly_chart(px.bar(df.city.value_counts().reset_index(), x='city', y='count', title='Топ городов'), use_container_width=True)
with tabs[1]:
    daily = df.groupby(pd.Grouper(key='event_date', freq='D')).size().reset_index(name='count')
    weekly = df.groupby(pd.Grouper(key='event_date', freq='W')).size().reset_index(name='count')
    st.plotly_chart(px.line(daily, x='event_date', y='count', title='Сообщения по дням'), use_container_width=True)
    st.plotly_chart(px.line(weekly, x='event_date', y='count', title='Сообщения по неделям'), use_container_width=True)
with tabs[2]:
    tmp = df.dropna(subset=['event_date']).copy(); tmp['weekday'] = tmp.event_date.dt.day_name()
    heat = tmp.pivot_table(index='weekday', columns='hour', values='raw_message_id', aggfunc='count', fill_value=0)
    st.plotly_chart(px.imshow(heat, title='День недели × час'), use_container_width=True)
    st.plotly_chart(px.histogram(df, x='hour', nbins=24, title='Распределение по часам'), use_container_width=True)
    bins = pd.cut(df['hour'], [-1,5,11,17,23], labels=['ночь','утро','день','вечер'])
    st.bar_chart(bins.value_counts())
with tabs[3]:
    st.subheader('Регионы'); st.dataframe(df.region.value_counts().reset_index())
    st.subheader('Города'); st.dataframe(df.city.value_counts().reset_index())
with tabs[4]:
    st.plotly_chart(px.pie(df, names='alert_type', title='Типы тревог'), use_container_width=True)
    st.plotly_chart(px.bar(df.weapon_type.value_counts().reset_index(), x='weapon_type', y='count', title='Типы вооружения'), use_container_width=True)
    dyn = df.groupby(['event_date','alert_type']).size().reset_index(name='count')
    st.plotly_chart(px.line(dyn, x='event_date', y='count', color='alert_type', title='Динамика типов угроз'), use_container_width=True)
with tabs[5]:
    st.metric('Доля unknown', f"{(df.alert_type=='unknown').mean():.1%}")
    st.dataframe(df[df.confidence.fillna(0) < 0.6].sort_values('confidence'))
with tabs[6]:
    st.dataframe(df)
    st.download_button('Скачать CSV', to_csv_bytes(df), 'alerts_clean.csv', 'text/csv')
    st.download_button('Скачать XLSX', to_xlsx_bytes(df), 'alerts_clean.xlsx')
