#include "bot.hpp"
#include <cmath>
#include <date/date.h>
#include <sstream>

namespace nb {
namespace {
const char *fields[] = {"kcal", "protein", "fat", "carbs"};
const char *prompts[] = {"🔥 Сколько калорий?", "🥚 Сколько белков?", "🥑 Сколько жиров?",
                         "🍞 Сколько углеводов?"};
std::string month_name(const std::string &first) {
    static const char *names[] = {"Январь", "Февраль", "Март",     "Апрель",  "Май",    "Июнь",
                                  "Июль",   "Август",  "Сентябрь", "Октябрь", "Ноябрь", "Декабрь"};
    return std::string(names[std::stoi(first.substr(5, 2)) - 1]) + " " + first.substr(0, 4);
}
std::string adjacent_month(const std::string &first, int delta) {
    date::year_month_day ym;
    std::istringstream input(first);
    input >> date::parse("%F", ym);
    return date::format("%F", date::sys_days{ym + date::months{delta}});
}
} // namespace
Json calendar_buttons(const std::string &month, const std::string &today,
                      const std::string &prefix) {
    auto first = parse_day(month.substr(0, 7) + "-01");
    auto last = shift_day(adjacent_month(first, 1), -1);
    date::year_month_day ym;
    std::istringstream input(first);
    input >> date::parse("%F", ym);
    int pad = date::weekday{date::sys_days{ym}}.iso_encoding() - 1;
    Json rows = Json::array(), row = Json::array();
    auto button = [&](const std::string &label, const std::string &action) {
        return Json{{"text", label}, {"callback_data", prefix + action}};
    };
    for (auto name : {"Пн", "Вт", "Ср", "Чт", "Пт", "Сб", "Вс"})
        row.push_back(button(name, "noop"));
    rows.push_back(row);
    row = Json::array();
    for (int i = 0; i < pad; ++i)
        row.push_back(button("·", "noop"));
    for (auto &day : days(first, last)) {
        auto label = std::to_string(std::stoi(day.substr(8, 2)));
        row.push_back(button(day > today ? "·" : label, day > today ? "noop" : "date_" + day));
        if (row.size() == 7) {
            rows.push_back(row);
            row = Json::array();
        }
    }
    if (!row.empty()) {
        while (row.size() < 7)
            row.push_back(button("·", "noop"));
        rows.push_back(row);
    }
    row = Json::array();
    if (first > "1900-01-01")
        row.push_back(button("‹", "month_" + adjacent_month(first, -1)));
    row.push_back(button(month_name(first), "noop"));
    if (adjacent_month(first, 1) <= today)
        row.push_back(button("›", "month_" + adjacent_month(first, 1)));
    rows.push_back(row);
    rows.push_back(Json::array({button("📍 Сегодня", "date_" + today), button("↩️ Назад", "back")}));
    rows.push_back(Json::array({button("✖️ Отмена", "cancel")}));
    return {{"inline_keyboard", rows}};
}

// Each screen gets a persistent unique revision. Buttons from another user or
// a previous screen cannot act on a new dialog, even after a process restart.
void Bot::screen(Id u, const std::string &state, Json p, Id t) {
    db_.write("INSERT INTO state(key,value) VALUES('ui_sequence',1) ON CONFLICT(key) DO UPDATE SET "
              "value=value+1");
    auto seq = db_.rows("SELECT value FROM state WHERE key='ui_sequence'")[0]["value"].get<Id>();
    if (!p.is_object())
        p = Json::object();
    p["flow"] = seq;
    session(u, state, p, t);
    auto prefix = "ui:" + std::to_string(seq) + ":";
    auto choices = [&](std::vector<std::pair<std::string, std::string>> values) {
        for (auto &[label, action] : values)
            action = prefix + action;
        values.emplace_back("✖️ Отмена", prefix + "cancel");
        return buttons(values);
    };
    if (state == "meal_category") {
        say(u, "🍽 Добавим еду\n\nК какому приёму пищи её отнести?",
            choices({{"🌅 Завтрак", "cat_breakfast"},
                     {"☀️ Обед", "cat_lunch"},
                     {"🌙 Ужин", "cat_dinner"}}));
    } else if (state == "meal_method") {
        // Exactly two choices, as requested. /cancel remains available everywhere.
        say(u, meal_label(p["category"]) + "\n\nКак добавим еду?",
            buttons({{cfg_.ai_enabled ? "🤖 С помощью AI" : "🤖 AI недоступен", prefix + "ai"},
                     {"✍️ Вручную", prefix + "manual"}}));
    } else if (state == "meal_ai") {
        say(u,
            meal_label(p["category"]) +
                " · оценка AI\n\n🍽 Опиши еду и размер порции.\nНапример: 2 яйца, 100 г варёного "
                "риса и чай без сахара.\n\nAI оценит калории, белки, жиры и углеводы. Результат "
                "можно исправить перед записью.\nОписание отправится в OpenAI; /ai off — "
                "отключить.",
            choices({{"↩️ Назад", "back"}}));
    } else if (state == "meal_name") {
        say(u,
            meal_label(p["category"]) +
                " · ручной ввод\n\n🍽 Что ты ел?\nНапример: омлет и тост\n\nДальше введём КБЖУ по "
                "одному — порядок запоминать не нужно.",
            choices({{"Пропустить название", "skip_name"}, {"↩️ Назад", "back"}}));
    } else if (state == "meal_values") {
        int step = p["step"];
        std::string text = meal_label(p["category"]) + " · " + p["name"].get<std::string>() +
                           "\n\n" + prompts[step] + "\n";
        text += step == 0 ? "Введи ккал для всей съеденной порции.\nНапример: 420"
                          : "Введи граммы для всей съеденной порции.\nНапример: 20 или 12,5";
        text += "\n\nШаг " + std::to_string(step + 1) + " из 4";
        const char *labels[] = {"Калории", "Белки", "Жиры", "Углеводы"};
        for (int i = 0; i < step; ++i)
            text += "\n✓ " + std::string(labels[i]) + ": " + quantity(p[fields[i]].get<double>());
        std::vector<std::pair<std::string, std::string>> actions;
        for (int n : step == 0 ? std::vector<int>{100, 250, 500} : std::vector<int>{0, 10, 20})
            actions.emplace_back(std::to_string(n) + (step == 0 ? " ккал" : " г"),
                                 "pick_" + std::to_string(n));
        if (step == 0)
            actions.emplace_back("⌨️ Ввести все 4 числа сразу", "compact");
        actions.emplace_back("↩️ Назад", "back");
        say(u, text, choices(actions));
    } else if (state == "meal_compact") {
        say(u,
            meal_label(p["category"]) + " · " + p["name"].get<std::string>() +
                "\n\n⌨️ Введи четыре числа через пробел:\nккал  белки  жиры  углеводы\n\nПример: "
                "420 20 18 45\nЗначения для всей порции, БЖУ — в граммах.\nКоманды и точка с "
                "запятой не нужны.",
            choices({{"↩️ По одному числу", "back"}}));
    } else if (state == "weight_value") {
        say(u,
            "⚖️ Запишем вес\n\nВведи только вес в килограммах.\nНапример: 82,4\n\n📅 Дату выберем "
            "следующим шагом.",
            choices({}));
    } else if (state == "weight_date") {
        auto today = local_day(t, db_.rows("SELECT tz FROM users WHERE id=?", {u})[0]["tz"]);
        say(u,
            "⚖️ " + quantity(p["amount"].get<double>() / 1000) + " кг\n\n📅 Когда было измерение?",
            choices(
                {{"Сегодня · " + display_day(today), "date_" + today},
                 {"Вчера · " + display_day(shift_day(today, -1)), "date_" + shift_day(today, -1)},
                 {"🗓 Открыть календарь", "calendar"},
                 {"↩️ Изменить вес", "back"}}));
    } else if (state == "weight_calendar") {
        auto today = local_day(t, db_.rows("SELECT tz FROM users WHERE id=?", {u})[0]["tz"]);
        say(u,
            "🗓 " + month_name(p["month"]) +
                "\n\nВыбери день измерения. Стрелки переключают месяцы.\n⚖️ " +
                quantity(p["amount"].get<double>() / 1000) + " кг",
            calendar_buttons(p["month"], today, prefix));
    }
}
void Bot::finish_meal(Id u, Json p, Id t) {
    std::ostringstream line;
    line << p["kcal"].get<double>() << ' ' << p["protein"].get<double>() << ' '
         << p["fat"].get<double>() << ' ' << p["carbs"].get<double>() << " ;"
         << p["name"].get<std::string>();
    auto estimate = manual_meal(line.str());
    Id id;
    if (p.contains("edit_id")) {
        id = p["edit_id"];
        db_.write("UPDATE drafts SET estimate=?,description=?,version=version+1 WHERE id=? AND "
                  "user=? AND version=? AND status='pending' AND expires>=?",
                  {estimate.dump(), p["name"], id, u, p["edit_version"], t});
        if (!db_.changes())
            throw InputError("Эта запись уже изменилась. Открой последнюю карточку.");
    } else {
        id = draft(u, "meal", p["day"], p["name"], estimate, t);
        db_.write("UPDATE drafts SET category=? WHERE id=?", {p["category"], id});
    }
    db_.write("DELETE FROM sessions WHERE user=?", {u});
    offer(u, id);
}
bool Bot::ui_message(Id u, const std::string &text, Id t) {
    auto rows = db_.rows("SELECT * FROM sessions WHERE user=?", {u});
    if (rows.empty())
        return false;
    std::string state = rows[0]["state"];
    if (state.rfind("meal_", 0) != 0 && state.rfind("weight_", 0) != 0)
        return false;
    if (rows[0]["expires"].get<Id>() < t) {
        db_.write("DELETE FROM sessions WHERE user=?", {u});
        say(u, "⌛ Диалог истёк. Начни заново через меню.", menu());
        return true;
    }
    auto p = Json::parse(rows[0]["payload"].get<std::string>());
    if (state == "meal_ai") {
        if (!cfg_.ai_enabled)
            throw InputError("🤖 AI отключён. Выбери ручной ввод через меню.");
        if (text.find_first_not_of(" \t\r\n") == std::string::npos || text.size() > 4000)
            throw InputError("🍽 Напиши описание еды покороче, вместе с размером порции.");
        auto id = draft(u, "meal", p["day"], text, Json::object(), t);
        db_.write("UPDATE drafts SET category=? WHERE id=?", {p["category"], id});
        db_.write("DELETE FROM sessions WHERE user=?", {u});
        db_.write("UPDATE users SET ai_consent=1 WHERE id=?", {u});
        queue_ai(u, id, t);
    } else if (state == "meal_name") {
        if (text.find_first_not_of(" \t\r\n") == std::string::npos || text.size() > 200)
            throw InputError("🍽 Напиши короткое название блюда.\nНапример: омлет и тост.");
        p["name"] = text;
        p["step"] = 0;
        screen(u, "meal_values", p, t);
    } else if (state == "meal_values") {
        int step = p["step"];
        p[fields[step]] = number(text, 0, step == 0 ? 20000 : 2000);
        if (step == 3)
            finish_meal(u, p, t);
        else {
            p["step"] = step + 1;
            screen(u, state, p, t);
        }
    } else if (state == "meal_compact") {
        std::istringstream in(text);
        std::string values[4], extra;
        if (!(in >> values[0] >> values[1] >> values[2] >> values[3]) || (in >> extra))
            throw InputError("⌨️ Нужны ровно четыре числа:\nккал  белки  жиры  "
                             "углеводы\n\nНапример: 420 20 18 45");
        for (int i = 0; i < 4; ++i)
            p[fields[i]] = number(values[i], 0, i == 0 ? 20000 : 2000);
        finish_meal(u, p, t);
    } else if (state == "weight_value") {
        p["amount"] = static_cast<int>(std::round(number(text, 1, 500) * 1000));
        screen(u, "weight_date", p, t);
    } else
        say(u, "👆 Выбери вариант кнопкой под последним сообщением.\n/cancel — отмена.");
    return true;
}
void Bot::ui_callback(Id u, const std::string &action, Id t) {
    auto split = action.find(':');
    if (split == std::string::npos)
        throw InputError("Кнопка устарела.");
    auto rows = db_.rows("SELECT * FROM sessions WHERE user=? AND expires>=?", {u, t});
    if (rows.empty())
        throw InputError("⌛ Эта кнопка устарела. Начни заново через меню.");
    auto p = Json::parse(rows[0]["payload"].get<std::string>());
    std::string state = rows[0]["state"];
    if (std::to_string(p.value("flow", Id{0})) != action.substr(0, split))
        throw InputError("👆 Используй кнопки под последним сообщением.");
    auto value = action.substr(split + 1);
    if (value == "noop")
        return;
    if (value == "cancel") {
        db_.write("DELETE FROM sessions WHERE user=?", {u});
        say(u, "✖️ Ввод отменён. Ничего не добавлено.", menu());
        return;
    }
    if (state == "meal_category" &&
        (value == "cat_breakfast" || value == "cat_lunch" || value == "cat_dinner")) {
        p["category"] = value.substr(4);
        screen(u, "meal_method", p, t);
        return;
    }
    if (state == "meal_method") {
        if (value == "ai") {
            if (cfg_.ai_enabled) {
                screen(u, "meal_ai", p, t);
                return;
            }
            say(u, "🤖 AI временно недоступен.\n\nНажми «Вручную» — я помогу заполнить значения по "
                   "шагам.");
            return;
        }
        if (value == "manual") {
            screen(u, "meal_name", p, t);
            return;
        }
    }
    if (value == "back") {
        if (state == "meal_name" || state == "meal_ai")
            screen(u, "meal_method", p, t);
        else if (state == "meal_values") {
            int step = p["step"];
            if (step == 0)
                screen(u, "meal_name", p, t);
            else {
                p["step"] = step - 1;
                screen(u, state, p, t);
            }
        } else if (state == "meal_compact") {
            p["step"] = 0;
            screen(u, "meal_values", p, t);
        } else if (state == "weight_date")
            screen(u, "weight_value", p, t);
        else if (state == "weight_calendar")
            screen(u, "weight_date", p, t);
        else
            throw InputError("Кнопка недоступна.");
        return;
    }
    if (state == "meal_name" && value == "skip_name") {
        ui_message(u, meal_label(p["category"]), t);
        return;
    }
    if (state == "meal_values") {
        if (value == "compact") {
            screen(u, "meal_compact", p, t);
            return;
        }
        if (value.rfind("pick_", 0) == 0) {
            ui_message(u, value.substr(5), t);
            return;
        }
    }
    auto today = local_day(t, db_.rows("SELECT tz FROM users WHERE id=?", {u})[0]["tz"]);
    if (state == "weight_date" && value == "calendar") {
        p["month"] = today.substr(0, 7) + "-01";
        screen(u, "weight_calendar", p, t);
        return;
    }
    if (state == "weight_calendar" && value.rfind("month_", 0) == 0) {
        auto first = parse_day(value.substr(6));
        if (first < "1900-01-01" || first > today || first.substr(8) != "01")
            throw InputError("Этот месяц недоступен.");
        p["month"] = first;
        screen(u, state, p, t);
        return;
    }
    if ((state == "weight_date" || state == "weight_calendar") && value.rfind("date_", 0) == 0) {
        auto day = parse_day(value.substr(5));
        if (day > today || day < "1900-01-01")
            throw InputError("Выбери прошедшую дату или сегодня.");
        Id id = draft(u, "weight", day, "", {{"amount", p["amount"]}}, t);
        db_.write("DELETE FROM sessions WHERE user=?", {u});
        offer(u, id);
        return;
    }
    throw InputError("👆 Эта кнопка не подходит к текущему шагу.");
}
} // namespace nb
