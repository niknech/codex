#include "bot.hpp"
#include <algorithm>
#include <cmath>
#include <sstream>
namespace nb {
namespace {
const char *rule_keys[] = {"water_hour1", "water_pct1", "water_hour2", "water_pct2", "kcal_pct"};
const char *rule_names[] = {"🕐 Первое напоминание о воде", "💧 Первый порог воды",
                            "🕑 Второе напоминание о воде", "💧 Второй порог воды",
                            "🔥 Порог калорий"};
const char *goal_keys[] = {"kcal", "protein", "fat", "carbs", "water"};
const char *goal_names[] = {"🔥 Калории, ккал", "🥚 Белки, г", "🥑 Жиры, г", "🍞 Углеводы, г",
                            "💧 Вода, мл"};
const char *zones[] = {"Europe/Kaliningrad", "Europe/Moscow",  "Europe/Samara",
                       "Asia/Yekaterinburg", "Asia/Omsk",      "Asia/Krasnoyarsk",
                       "Asia/Irkutsk",       "Asia/Yakutsk",   "Asia/Vladivostok",
                       "Asia/Magadan",       "Asia/Kamchatka", "Etc/UTC"};
const char *cities[] = {
    "Калининград", "Москва / Петербург", "Самара",  "Екатеринбург", "Омск", "Красноярск", "Иркутск",
    "Якутск",      "Владивосток",        "Магадан", "Камчатка",     "UTC"};
std::string hour(double value) {
    auto h = static_cast<int>(value);
    return (h < 10 ? "0" : "") + std::to_string(h) + ":00";
}
} // namespace
void Bot::preferences(Id u, const std::string &state, Json p, Id t) {
    db_.write(
        "INSERT INTO state VALUES('ui_sequence',1) ON CONFLICT(key) DO UPDATE SET value=value+1");
    p["flow"] = db_.rows("SELECT value FROM state WHERE key='ui_sequence'")[0]["value"];
    auto user = db_.rows("SELECT * FROM users WHERE id=?", {u})[0];
    std::string text;
    std::vector<std::pair<std::string, std::string>> actions;
    if (state == "pref_home") {
        text = "⚙️ Всё под тебя\n\n🕒 Часовой пояс: " + user["tz"].get<std::string>() +
               "\n🔔 Напоминания: " + (user["reminders"] == 1 ? "включены" : "выключены") +
               "\n🤖 Оценка еды: " +
               (!cfg_.ai_enabled          ? "пока недоступна"
                : user["ai_consent"] == 1 ? "разрешена"
                                          : "с твоего согласия при вводе еды");
        actions = {{"🕒 Часовой пояс", "zones"},
                   {"🔔 Напоминания", "reminders"},
                   {"🤖 Не отправлять еду в AI", "ai_off"},
                   {"📝 Последние записи", "entries"},
                   {"🪪 Мой Telegram ID", "id"},
                   {"❓ Как пользоваться", "help"}};
    } else if (state == "pref_zones") {
        text = "🕒 По какому времени живём?\n\nВыбери свой город или ближайший с тем же временем. "
               "Прошлые записи сохранят свои даты.";
        for (int i = 0; i < 12; ++i)
            actions.emplace_back(cities[i], "zone_" + std::to_string(i));
        actions.emplace_back("🌍 Другой часовой пояс", "zone_custom");
    } else if (state == "pref_zone_custom") {
        text = "🌍 Твой часовой пояс\n\nНапиши название, например Europe/Berlin. Без команды и "
               "дополнительных символов.";
    } else if (state == "pref_reminders") {
        text = "🔔 Ненавязчивые напоминания\n\n" +
               std::string(user["reminders"] == 1 ? "Сейчас включены" : "Сейчас выключены") +
               "\n\n💧 " + hour(user["water_hour1"]) + " — если записано меньше " +
               quantity(user["water_pct1"]) + "% нормы воды" + "\n💧 " + hour(user["water_hour2"]) +
               " — если записано меньше " + quantity(user["water_pct2"]) + "% нормы воды" +
               "\n🔥 Калории — при достижении " + quantity(user["kcal_pct"]) + "% нормы" +
               "\n\nВремя — по твоему часовому поясу. Напоминание о воде может прийти в течение "
               "часа. Нормы задаются в главном меню.";
        actions.emplace_back(user["reminders"] == 1 ? "🔕 Выключить" : "🔔 Включить",
                             user["reminders"] == 1 ? "rem_off" : "rem_on");
        for (int i = 0; i < 5; ++i)
            actions.emplace_back(rule_names[i], "rule_" + std::to_string(i));
    } else if (state == "pref_rule") {
        int field = p["field"];
        bool is_hour = field == 0 || field == 2;
        text = std::string(rule_names[field]) + "\n\n" +
               (is_hour ? hour(p["value"]) : quantity(p["value"]) + "%") +
               "\n\nМеняй кнопками или введи своё число. Сохраню после подтверждения.";
        actions = {{is_hour ? "−1 час" : "−5%", "minus"},
                   {is_hour ? "+1 час" : "+5%", "plus"},
                   {"✅ Сохранить", "rule_save"},
                   {"↩️ К напоминаниям", "reminders"}};
    } else if (state == "pref_goal") {
        int step = p.value("step", 0);
        p["step"] = step;
        text = "🎯 Личные нормы · " + std::to_string(step + 1) + " из 5\n\n" + goal_names[step] +
               "\nВыбери значение или введи своё число. Это твой ориентир, не рекомендация бота.";
        for (int n : step == 0   ? std::vector<int>{1600, 2000, 2400}
                     : step == 4 ? std::vector<int>{1500, 2000, 2500}
                                 : std::vector<int>{50, 100, 150})
            actions.emplace_back(std::to_string(n), "goal_" + std::to_string(n));
        if (step > 0)
            actions.emplace_back("↩️ Назад", "goal_back");
    } else if (state == "pref_goal_confirm") {
        text = "🎯 Проверим твои ориентиры\n";
        for (int i = 0; i < 5; ++i)
            text += "\n" + std::string(goal_names[i]) + ": " + quantity(p[goal_keys[i]]);
        auto today = local_day(t, user["tz"]);
        text += "\n\nНачнут действовать " +
                display_day(goal(db_, u, today).is_null() ? today : shift_day(today, 1)) +
                ". История останется прежней.";
        actions = {{"✅ Сохранить нормы", "goal_save"}, {"↩️ Изменить", "goal_back"}};
    }
    session(u, state, p, t);
    auto prefix = "ui:" + std::to_string(p["flow"].get<Id>()) + ":";
    for (auto &[label, action] : actions)
        action = prefix + action;
    if (state != "pref_home" && state.rfind("pref_goal", 0) != 0)
        actions.emplace_back("↩️ К настройкам", prefix + "home");
    actions.emplace_back("✖️ Отмена", prefix + "cancel");
    say(u, text, buttons(actions));
}
bool Bot::preference_message(Id u, const std::string &text, Id t) {
    auto rows = db_.rows("SELECT * FROM sessions WHERE user=? AND expires>=?", {u, t});
    if (rows.empty())
        return false;
    auto state = rows[0]["state"].get<std::string>();
    if (state.rfind("pref_", 0) != 0)
        return false;
    auto p = Json::parse(rows[0]["payload"].get<std::string>());
    if (state == "pref_zone_custom") {
        validate_timezone(text);
        db_.write("UPDATE users SET tz=? WHERE id=?", {text, u});
        preferences(u, "pref_home", Json::object(), t);
    } else if (state == "pref_rule") {
        int field = p["field"];
        bool h = field == 0 || field == 2;
        auto value = number(text, h ? 0 : 1, h ? 23 : field == 4 ? 200 : 100);
        if (h && value != std::floor(value))
            throw InputError("🕒 Введи целый час от 0 до 23.");
        p["value"] = value;
        preferences(u, state, p, t);
    } else if (state == "pref_goal") {
        int step = p["step"];
        auto n = number(text, step == 0 || step == 4 ? 1 : 0,
                        step == 0   ? 20000
                        : step == 4 ? 10000
                                    : 2000);
        if (step == 4 && n != std::floor(n))
            throw InputError("💧 Введи количество воды в целых миллилитрах.");
        p[goal_keys[step]] = n;
        p["step"] = step + 1;
        preferences(u, step == 4 ? "pref_goal_confirm" : state, p, t);
    } else
        say(u, "👆 Выбери подходящую кнопку под последним сообщением.");
    return true;
}
bool Bot::preference_callback(Id u, const std::string &state, const std::string &value, Json p,
                              Id t) {
    if (state.rfind("pref_", 0) != 0)
        return false;
    if (value == "home")
        preferences(u, "pref_home", Json::object(), t);
    else if (state == "pref_home" && value == "zones")
        preferences(u, "pref_zones", Json::object(), t);
    else if (value == "reminders" && (state == "pref_home" || state == "pref_rule"))
        preferences(u, "pref_reminders", Json::object(), t);
    else if (state == "pref_home" &&
             (value == "help" || value == "entries" || value == "id" || value == "ai_off")) {
        if (value == "id")
            say(u, "🪪 Твой Telegram ID\n\n" + std::to_string(u));
        else
            message(u, u, value == "ai_off" ? "/ai off" : "/" + value, t);
    } else if (state == "pref_zones" && value == "zone_custom")
        preferences(u, "pref_zone_custom", Json::object(), t);
    else if (state == "pref_zones" && value.rfind("zone_", 0) == 0) {
        auto i = number(value.substr(5), 0, 11);
        if (i != std::floor(i))
            throw InputError("Выбери город кнопкой.");
        db_.write("UPDATE users SET tz=? WHERE id=?", {zones[static_cast<int>(i)], u});
        preferences(u, "pref_home", Json::object(), t);
    } else if (state == "pref_reminders" && (value == "rem_on" || value == "rem_off")) {
        db_.write("UPDATE users SET reminders=? WHERE id=?", {value == "rem_on" ? 1 : 0, u});
        if (value == "rem_off")
            db_.write("UPDATE outbox SET done=2 WHERE user=? AND done=0 AND expires>0", {u});
        preferences(u, state, Json::object(), t);
    } else if (state == "pref_reminders" && value.rfind("rule_", 0) == 0) {
        auto i = number(value.substr(5), 0, 4);
        if (i != std::floor(i))
            throw InputError("Выбери настройку кнопкой.");
        int field = static_cast<int>(i);
        auto user = db_.rows("SELECT * FROM users WHERE id=?", {u})[0];
        preferences(u, "pref_rule", {{"field", field}, {"value", user[rule_keys[field]]}}, t);
    } else if (state == "pref_rule" &&
               (value == "plus" || value == "minus" || value == "rule_save")) {
        int field = p["field"];
        bool h = field == 0 || field == 2;
        if (value == "rule_save") {
            db_.write("UPDATE users SET " + std::string(rule_keys[field]) + "=? WHERE id=?",
                      {p["value"], u});
            preferences(u, "pref_reminders", Json::object(), t);
        } else {
            p["value"] = std::clamp(
                p["value"].get<double>() + (value == "plus" ? 1 : -1) * (h ? 1 : 5), h ? 0.0 : 1.0,
                h            ? 23.0
                : field == 4 ? 200.0
                             : 100.0);
            preferences(u, state, p, t);
        }
    } else if (state == "pref_goal" && value.rfind("goal_", 0) == 0 && value != "goal_back")
        preference_message(u, value.substr(5), t);
    else if ((state == "pref_goal" || state == "pref_goal_confirm") && value == "goal_back") {
        p["step"] = std::max(0, p["step"].get<int>() - 1);
        preferences(u, "pref_goal", p, t);
    } else if (state == "pref_goal_confirm" && value == "goal_save") {
        std::ostringstream args;
        args << "/goals";
        for (auto key : goal_keys)
            args << ' ' << p[key].get<double>();
        message(u, u, args.str(), t);
    } else
        throw InputError("👆 Эта кнопка уже не подходит. Открой настройки ещё раз.");
    return true;
}
} // namespace nb
