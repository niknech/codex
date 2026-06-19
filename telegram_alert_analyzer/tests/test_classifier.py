from src.classifier import AlertClassifier

def test_uav_classification():
    c = AlertClassifier().classify('Замечен БПЛА в районе Луганска')
    assert c.alert_type == 'uav_alert'; assert c.weapon_type == 'uav'

def test_missile_classification():
    c = AlertClassifier().classify('Ракетная опасность, возможна баллистика')
    assert c.alert_type == 'missile_alert'; assert c.weapon_type == 'ballistic_missile'

def test_all_clear_classification():
    c = AlertClassifier().classify('Отбой ракетной опасности')
    assert c.alert_type == 'all_clear'; assert c.status == 'all_clear'

def test_combined_classification():
    c = AlertClassifier().classify('БПЛА и ракета в небе')
    assert c.alert_type == 'combined_alert'

def test_unknown_classification():
    c = AlertClassifier().classify('Доброе утро, хорошего дня')
    assert c.alert_type == 'unknown'; assert c.confidence_hint <= 0.3
