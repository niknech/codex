#include "bot.hpp"
#include <algorithm>
#include <cmath>
#include <sstream>

namespace nb {
Json buttons(const std::vector<std::pair<std::string, std::string>> &values) {
    Json rows = Json::array();
    for (const auto &[label, action] : values)
        rows.push_back(Json::array({{{"text", label}, {"callback_data", action}}}));
    return {{"inline_keyboard", rows}};
}
Json menu() {
    // Nested pairs of strings are interpreted as an object by nlohmann/json.
    // Telegram requires an array of rows, each containing KeyboardButton objects.
    Json keyboard = Json::array();
    for (const auto &[left, right] : std::vector<std::pair<std::string, std::string>>{
             {"🍽 Добавить еду", "💧 Добавить воду"},
             {"⚖️ Записать вес", "📊 Итоги дня"},
             {"📅 Статистика", "📈 График веса"},
             {"🎯 Мои нормы", "⚙️ Настройки"}}) {
        keyboard.push_back(Json::array({Json{{"text", left}}, Json{{"text", right}}}));
    }
    return {{"keyboard", keyboard}, {"resize_keyboard", true}};
}
void Bot::say(Id user, const std::string &text, const Json &markup) {
    auto rows = db_.rows("SELECT chat FROM users WHERE id=?", {user});
    Json payload = {{"chat_id", rows.empty() ? user : rows[0]["chat"].get<Id>()}, {"text", text}};
    if (!markup.is_null())
        payload["reply_markup"] = markup;
    db_.write("INSERT INTO outbox(user,method,payload) VALUES(?,'sendMessage',?)",
              {user, payload.dump()});
}
void Bot::session(Id user, const std::string &state, const Json &payload, Id time) {
    db_.write("INSERT INTO sessions VALUES(?,?,?,?) ON CONFLICT(user) DO UPDATE SET "
              "state=excluded.state,payload=excluded.payload,expires=excluded.expires",
              {user, state, payload.dump(), time + 3600});
}
Id Bot::draft(Id user, const std::string &kind, const std::string &day,
              const std::string &description, const Json &estimate, Id time) {
    auto tz = db_.rows("SELECT tz FROM users WHERE id=?", {user})[0]["tz"];
    return db_.write(
        "INSERT INTO drafts(user,kind,day,tz,description,estimate,expires) VALUES(?,?,?,?,?,?,?)",
        {user, kind, day, tz, description, estimate.dump(), time + 3600});
}
void Bot::offer(Id user, Id id) {
    auto d = db_.rows("SELECT * FROM drafts WHERE id=? AND user=?", {id, user})[0];
    auto e = Json::parse(d["estimate"].get<std::string>());
    auto suffix = std::to_string(id) + ":" + std::to_string(d["version"].get<Id>());
    std::string text = d["day"].get<std::string>() + "\n";
    if (d["kind"] == "weight")
        text += "Вес: " + std::to_string(e["amount"].get<double>() / 1000) +
                " кг. Записать (заменить вес за эту дату)?";
    else
        text += estimate_text(e);
    std::vector<std::pair<std::string, std::string>> actions;
    if (d["kind"] == "weight" || e.value("status", "") == "estimate")
        actions.emplace_back("Записать", "save:" + suffix);
    if (d["kind"] == "meal") {
        actions.emplace_back("Уточнить порцию", "clarify:" + suffix);
        actions.emplace_back("Исправить БЖУ", "edit:" + suffix);
        actions.emplace_back("Повторить AI", "retry:" + suffix);
    }
    actions.emplace_back("Отмена", "cancel:" + suffix);
    say(user, text, buttons(actions));
}
void Bot::ingest(const Json &updates) {
    Transaction tx(db_);
    for (const auto &update : updates) {
        Id id = update.at("update_id");
        db_.write("INSERT OR IGNORE INTO inbox(id,payload) VALUES(?,?)", {id, update.dump()});
        db_.write("UPDATE state SET value=MAX(value,?) WHERE key='offset'", {id + 1});
    }
    tx.commit();
}
bool Bot::process_one(Id time) {
    Transaction tx(db_);
    auto rows = db_.rows("SELECT * FROM inbox WHERE done=0 ORDER BY id LIMIT 1");
    if (rows.empty()) {
        tx.commit();
        return false;
    }
    auto update = Json::parse(rows[0]["payload"].get<std::string>());
    Id user = 0;
    try {
        const bool cb = update.contains("callback_query");
        if (cb || update.contains("message")) {
            auto envelope = cb ? update.at("callback_query") : update.at("message");
            user = envelope.at("from").at("id");
            auto msg = cb ? envelope.value("message", Json::object()) : envelope;
            if (cb)
                db_.write(
                    "INSERT INTO outbox(user,method,payload) VALUES(?,'answerCallbackQuery',?)",
                    {user, Json({{"callback_query_id", envelope.at("id")}}).dump()});
            if (msg.contains("chat") && msg["chat"].value("type", "") == "private") {
                auto text = msg.value("text", "");
                if (!cb && text == "/id")
                    say(user, "Ваш Telegram ID: " + std::to_string(user));
                else if (cfg_.public_registration || cfg_.allowed.count(user)) {
                    db_.write("INSERT INTO users(id,chat,tz) VALUES(?,?,?) ON CONFLICT(id) DO "
                              "UPDATE SET chat=excluded.chat",
                              {user, msg["chat"]["id"], cfg_.timezone});
                    db_.exec("SAVEPOINT action");
                    try {
                        if (cb)
                            callback(user, envelope.value("data", ""), time);
                        else
                            message(user, msg["chat"]["id"], text, time);
                        db_.exec("RELEASE action");
                    } catch (const InputError &e) {
                        db_.exec("ROLLBACK TO action; RELEASE action");
                        say(user, e.what());
                    } catch (const Json::exception &) {
                        db_.exec("ROLLBACK TO action; RELEASE action");
                        say(user, "Диалог устарел. Выберите действие в меню заново.");
                    }
                } else if (!cb)
                    say(user, "Доступ закрыт. Передайте владельцу ваш ID: " + std::to_string(user));
            }
        }
    } catch (const Json::exception &) {
        if (user)
            say(user, "Не удалось прочитать сообщение.");
    }
    db_.write("UPDATE inbox SET done=1,payload='{}' WHERE id=?", {rows[0]["id"]});
    tx.commit();
    return true;
}
static std::vector<std::string> words(const std::string &text) {
    std::istringstream in(text);
    std::vector<std::string> out;
    std::string s;
    while (in >> s)
        out.push_back(s);
    return out;
}
void Bot::message(Id u, Id, const std::string &text, Id t) {
    if (text.size() > 3500)
        throw InputError("Сообщение слишком длинное (максимум 3500 байт).");
    auto user = db_.rows("SELECT * FROM users WHERE id=?", {u})[0];
    auto today = local_day(t, user["tz"]);
    if (text == "/cancel" || text == "/start") {
        db_.write("DELETE FROM sessions WHERE user=?", {u});
        db_.write("UPDATE drafts SET status='cancelled' WHERE user=? AND status='pending'", {u});
        say(u,
            text == "/start"
                ? "Дневник питания. Начните с настроек часового пояса и личных норм. Сейчас: " +
                      user["tz"].get<std::string>() +
                      ". Нормы можно заполнить позже. /help — команды, /cancel — отмена."
                : "Незавершённые операции отменены.",
            menu());
        return;
    }
    if (text == "/help") {
        say(u, "/water 250 мл\n/weight 82,4 [ДД.ММ.ГГГГ]\n/manual 420 20 18 45 ; обед\n/food "
               "описание еды\n/fooddate ДД.ММ.ГГГГ описание\n/today\n/stats 7 (или 14)\n/period "
               "ДД.ММ.ГГГГ ДД.ММ.ГГГГ\n/chart 14\n/chartperiod ДД.ММ.ГГГГ ДД.ММ.ГГГГ\n/goals "
               "[дата] ккал Б Ж У вода_мл\n/timezone Europe/Moscow\n/reminders on|off\n/rules час1 "
               "процент1 час2 процент2 процент_ккал\n/entries — последние записи и отмена\n/ai off "
               "— отозвать согласие\n/cancel");
        return;
    }
    if (text == "📊 Итоги дня" || text == "/today") {
        say(u, summary(db_, u, today));
        return;
    }
    if (text == "💧 Добавить воду") {
        session(u, "water", {}, t);
        say(u, "Сколько воды?",
            buttons(
                {{"+150 мл", "water:150"}, {"+250 мл", "water:250"}, {"+500 мл", "water:500"}}));
        return;
    }
    if (text == "⚖️ Записать вес") {
        session(u, "weight", {}, t);
        say(u, "Вес в кг, при желании дата: 82,4 09.10.2026");
        return;
    }
    if (text == "🍽 Добавить еду") {
        session(u, "food", {}, t);
        say(u, "Опишите еду. Без AI: /manual 420 20 18 45 ; обед");
        return;
    }
    if (text == "🎯 Мои нормы") {
        session(u, "goals", {}, t);
        say(u, "Введите ккал Б Ж У вода_мл, например: 2000 100 70 240 2000. Можно начать с даты. "
               "Новые нормы по умолчанию действуют с завтра.");
        return;
    }
    if (text == "⚙️ Настройки") {
        say(u, "Часовой пояс: " + user["tz"].get<std::string>() +
                   "\n/timezone Europe/Moscow\n/reminders on или off\n/rules 13 30 17 60 80\n/ai "
                   "off — запретить отправку описаний AI");
        return;
    }
    if (text == "📅 Статистика" || text == "📈 График веса") {
        bool chart = text == "📈 График веса";
        std::string p = chart ? "chart:" : "stats:";
        say(u, "Выберите период",
            buttons({{"Сегодня", p + "1"},
                     {"Вчера", p + "0"},
                     {"7 дней", p + "7"},
                     {"14 дней", p + "14"},
                     {"Выбрать период", chart ? "custom:chart" : "custom:stats"}}));
        return;
    }
    if (text == "/entries") {
        auto entries = db_.rows(
            "SELECT id,day,kind FROM entries WHERE user=? AND deleted=0 ORDER BY id DESC LIMIT 10",
            {u});
        if (entries.empty())
            say(u, "Записей нет.");
        for (auto &e : entries)
            say(u, e["day"].get<std::string>() + " " + e["kind"].get<std::string>(),
                buttons({{"Удалить эту запись", "undo:" + std::to_string(e["id"].get<Id>())}}));
        return;
    }
    if (text == "/ai off") {
        db_.write("UPDATE users SET ai_consent=0 WHERE id=?", {u});
        db_.write("UPDATE drafts SET status='cancelled' WHERE user=? AND status='pending' AND "
                  "kind='meal'",
                  {u});
        say(u, "Согласие отозвано. Уже отправленный запрос отозвать нельзя; результат не будет "
               "записан.");
        return;
    }
    if (text.rfind("/timezone ", 0) == 0) {
        auto tz = text.substr(10);
        validate_timezone(tz);
        db_.write("UPDATE users SET tz=? WHERE id=?", {tz, u});
        say(u, "Часовой пояс изменён. Прошлые записи сохранили свои даты.");
        return;
    }
    if (text.rfind("/reminders ", 0) == 0) {
        auto v = text.substr(11);
        if (v != "on" && v != "off")
            throw InputError("/reminders on или off");
        db_.write("UPDATE users SET reminders=? WHERE id=?", {v == "on" ? 1 : 0, u});
        if (v == "off")
            db_.write("UPDATE outbox SET done=2 WHERE user=? AND done=0 AND expires>0", {u});
        say(u, "Настройка напоминаний сохранена.");
        return;
    }
    if (text.rfind("/rules ", 0) == 0) {
        auto w = words(text.substr(7));
        if (w.size() != 5)
            throw InputError("/rules 13 30 17 60 80");
        auto a = number(w[0], 0, 23), b = number(w[2], 0, 23);
        if (std::floor(a) != a || std::floor(b) != b)
            throw InputError("Часы должны быть целыми.");
        db_.write("UPDATE users SET "
                  "water_hour1=?,water_pct1=?,water_hour2=?,water_pct2=?,kcal_pct=? WHERE id=?",
                  {a, number(w[1], 1, 100), b, number(w[3], 1, 100), number(w[4], 1, 200), u});
        say(u, "Пороги сохранены; окно воды — час после указанного времени.");
        return;
    }
    auto split = text.find(' ');
    auto cmd = text.substr(0, split);
    auto arg = split == std::string::npos ? "" : text.substr(split + 1);
    auto ss = db_.rows("SELECT * FROM sessions WHERE user=?", {u});
    Json payload;
    std::string state;
    if (!ss.empty()) {
        if (ss[0]["expires"].get<Id>() < t) {
            db_.write("DELETE FROM sessions WHERE user=?", {u});
            say(u, "Предыдущий диалог истёк. Выберите действие заново.");
            return;
        }
        state = ss[0]["state"];
        payload = Json::parse(ss[0]["payload"].get<std::string>());
    }
    if (text.empty() || text[0] != '/') {
        cmd = "/" + state;
        arg = text;
    }
    if (cmd == "/water") {
        int amount = water_ml(arg);
        Id id =
            db_.write("INSERT INTO entries(user,kind,day,utc,tz,amount) VALUES(?,'water',?,?,?,?)",
                      {u, today, t, user["tz"], amount});
        say(u, summary(db_, u, today),
            buttons({{"Отменить эту воду", "undo:" + std::to_string(id)}}));
    } else if (cmd == "/weight") {
        auto w = words(arg);
        if (w.empty() || w.size() > 2)
            throw InputError("Вес: 82,4 [ДД.ММ.ГГГГ]");
        auto day = w.size() == 2 ? parse_day(w[1]) : today;
        if (day > today)
            throw InputError("Будущую дату веса записать нельзя.");
        Id id = draft(u, "weight", day, "",
                      {{"amount", static_cast<int>(std::round(number(w[0], 1, 500) * 1000))}}, t);
        offer(u, id);
    } else if (cmd == "/manual" || cmd == "/edit") {
        auto e = manual_meal(arg);
        Id id;
        if (cmd == "/edit") {
            id = payload.at("id");
            db_.write("UPDATE drafts SET estimate=?,version=version+1 WHERE id=? AND user=? AND "
                      "status='pending' AND expires>=? AND version=?",
                      {e.dump(), id, u, t, payload.at("version")});
            if (!db_.changes())
                throw InputError("Черновик устарел.");
        } else
            id = draft(u, "meal", today, arg, e, t);
        offer(u, id);
    } else if (cmd == "/food" || cmd == "/fooddate" || cmd == "/clarify") {
        if (!cfg_.ai_enabled)
            throw InputError("AI отключён. Используйте /manual ккал Б Ж У ; название");
        Id id;
        if (cmd == "/clarify") {
            id = payload.at("id");
            auto d = db_.rows("SELECT description FROM drafts WHERE id=? AND user=? AND "
                              "status='pending' AND expires>=? AND version=?",
                              {id, u, t, payload.at("version")});
            if (d.empty())
                throw InputError("Черновик устарел.");
            std::string desc = d[0]["description"].get<std::string>() + "\nУточнение: " + arg;
            if (desc.size() > 7000)
                throw InputError("Слишком много уточнений. Начните новое описание.");
            db_.write("UPDATE drafts SET description=?,version=version+1,estimate='{}' WHERE id=?",
                      {desc, id});
        } else {
            auto day = today;
            if (cmd == "/fooddate") {
                auto p = arg.find(' ');
                if (p == std::string::npos)
                    throw InputError("/fooddate дата описание");
                day = parse_day(arg.substr(0, p));
                arg = arg.substr(p + 1);
            }
            if (arg.empty() || day > today)
                throw InputError("Нужно описание и дата не в будущем.");
            id = draft(u, "meal", day, arg, Json::object(), t);
        }
        auto revision = std::to_string(
            db_.rows("SELECT version FROM drafts WHERE id=?", {id})[0]["version"].get<Id>());
        if (user["ai_consent"] == 0)
            say(u,
                "Описание еды будет отправлено настроенному AI-провайдеру. Вес, Telegram ID и "
                "история не передаются. Разрешить?",
                buttons({{"Разрешить и оценить", "consent:" + std::to_string(id) + ":" + revision},
                         {"Отмена", "cancel:" + std::to_string(id) + ":" + revision}}));
        else
            queue_ai(u, id, t);
    } else if (cmd == "/goals") {
        auto w = words(arg);
        bool explicit_date = w.size() == 6;
        auto day = shift_day(today, 1);
        if (w.size() == 6) {
            day = parse_day(w.front());
            w.erase(w.begin());
        }
        if (w.size() != 5)
            throw InputError("[дата] ккал Б Ж У вода_мл");
        auto old = goal(db_, u, today);
        if (day < today || (!old.is_null() && day == today))
            throw InputError(
                "Изменённые нормы вводятся с завтра или будущей даты; история сохраняется.");
        if (old.is_null() && !explicit_date)
            day = today;
        auto water = number(w[4], 1, 10000);
        if (std::floor(water) != water)
            throw InputError("Вода в целых мл.");
        db_.write("INSERT INTO goals VALUES(?,?,?,?,?,?,?) ON CONFLICT(user,day) DO UPDATE SET "
                  "kcal=excluded.kcal,protein=excluded.protein,fat=excluded.fat,carbs=excluded."
                  "carbs,water=excluded.water",
                  {u, day, number(w[0], 1, 20000), number(w[1], 0, 2000), number(w[2], 0, 2000),
                   number(w[3], 0, 2000), water});
        say(u, "Нормы сохранены с " + day + ". Напоминания: /reminders on");
    } else if (cmd == "/stats" || cmd == "/chart") {
        int n = static_cast<int>(number(arg.empty() ? "7" : arg, 1, 366));
        period(u, shift_day(today, 1 - n), today, 0, cmd == "/chart");
    } else if (cmd == "/period" || cmd == "/chartperiod") {
        auto w = words(arg);
        if (w.size() != 2)
            throw InputError("Две даты через пробел: 01.10.2026 10.10.2026");
        auto a = parse_day(w[0]), b = parse_day(w[1]);
        days(a, b);
        session(u, "period_confirm", {{"start", a}, {"end", b}, {"chart", cmd == "/chartperiod"}},
                t);
        say(u, "Период: " + a + " — " + b, buttons({{"Показать", "period:confirm"}}));
        return;
    } else {
        say(u, "Выберите действие в меню или /help.", menu());
        return;
    }
    db_.write("DELETE FROM sessions WHERE user=?", {u});
}
void Bot::callback(Id u, const std::string &action, Id t) {
    auto sep = action.find(':');
    auto verb = action.substr(0, sep);
    auto value = sep == std::string::npos ? "" : action.substr(sep + 1);
    auto today = local_day(t, db_.rows("SELECT tz FROM users WHERE id=?", {u})[0]["tz"]);
    if (verb == "water") {
        message(u, u, "/water " + value, t);
        return;
    }
    if (verb == "stats" || verb == "chart") {
        int n = static_cast<int>(number(value, 0, 366));
        period(u, n ? shift_day(today, 1 - n) : shift_day(today, -1),
               n ? today : shift_day(today, -1), 0, verb == "chart");
        return;
    }
    if (verb == "custom") {
        session(u, value == "chart" ? "chartperiod" : "period", {}, t);
        say(u, "Введите две даты через пробел: 01.10.2026 10.10.2026");
        return;
    }
    if (verb == "period") {
        auto s = db_.rows(
            "SELECT payload FROM sessions WHERE user=? AND state='period_confirm' AND expires>=?",
            {u, t});
        if (s.empty())
            throw InputError("Период устарел. Выберите заново.");
        auto p = Json::parse(s[0]["payload"].get<std::string>());
        period(u, p["start"], p["end"], 0, p["chart"]);
        return;
    }
    if (verb == "page") {
        auto w = words(value);
        if (w.size() != 3)
            throw InputError("Неверная страница.");
        period(u, parse_day(w[0]), parse_day(w[1]), static_cast<int>(number(w[2], 0, 100)), false);
        return;
    }
    auto version_sep = value.find(':');
    auto id_text = value.substr(0, version_sep);
    Id id;
    try {
        size_t n;
        id = std::stoll(id_text, &n);
        if (n != id_text.size() || id < 1)
            throw InputError("Неверная кнопка.");
    } catch (const std::logic_error &) {
        throw InputError("Неверная кнопка.");
    }
    if (verb == "undo") {
        db_.write("UPDATE entries SET deleted=1 WHERE id=? AND user=? AND deleted=0", {id, u});
        say(u, db_.changes() ? "Запись отменена." : "Запись недоступна или уже отменена.");
        return;
    }
    auto rows = db_.rows("SELECT * FROM drafts WHERE id=? AND user=?", {id, u});
    if (rows.empty())
        throw InputError("Черновик недоступен.");
    auto d = rows[0];
    if (d["status"] != "pending")
        throw InputError(d["status"] == "saved" ? "Уже записано." : "Черновик отменён.");
    if (d["expires"].get<Id>() < t)
        throw InputError("Черновик истёк. Начните заново.");
    if (version_sep == std::string::npos ||
        value.substr(version_sep + 1) != std::to_string(d["version"].get<Id>()))
        throw InputError("Оценка изменилась. Используйте кнопки под последней оценкой.");
    if (verb == "cancel") {
        db_.write("UPDATE drafts SET status='cancelled' WHERE id=?", {id});
        say(u, "Отменено.");
        return;
    }
    if (verb == "consent" || verb == "retry") {
        if (verb == "consent")
            db_.write("UPDATE users SET ai_consent=1 WHERE id=?", {u});
        queue_ai(u, id, t);
        return;
    }
    if (verb == "edit" || verb == "clarify") {
        session(u, verb, {{"id", id}, {"version", d["version"]}}, t);
        say(u, verb == "edit" ? "Введите ккал Б Ж У ; название" : "Уточните порцию или состав.");
        return;
    }
    if (verb != "save")
        throw InputError("Неизвестная кнопка.");
    auto e = Json::parse(d["estimate"].get<std::string>());
    Id entry;
    if (d["kind"] == "weight") {
        db_.write(
            "UPDATE entries SET deleted=1 WHERE user=? AND day=? AND kind='weight' AND deleted=0",
            {u, d["day"]});
        entry = db_.write(
            "INSERT INTO entries(user,kind,day,utc,tz,amount,draft) VALUES(?,'weight',?,?,?,?,?)",
            {u, d["day"], t, d["tz"], e.at("amount"), id});
    } else {
        if (e.value("status", "") != "estimate")
            throw InputError("Сначала получите или введите оценку.");
        validate_estimate(e);
        double k = 0, p = 0, f = 0, c = 0;
        for (auto &i : e["items"]) {
            k += i["kcal"].get<double>();
            p += i["protein_g"].get<double>();
            f += i["fat_g"].get<double>();
            c += i["carbs_g"].get<double>();
        }
        entry = db_.write(
            "INSERT INTO "
            "entries(user,kind,day,utc,tz,kcal,protein,fat,carbs,description,items,draft) "
            "VALUES(?,'meal',?,?,?,?,?,?,?,?,?,?)",
            {u, d["day"], t, d["tz"], k, p, f, c, d["description"], e["items"].dump(), id});
    }
    db_.write("UPDATE drafts SET status='saved' WHERE id=?", {id});
    say(u, summary(db_, u, d["day"]),
        buttons({{"Отменить запись", "undo:" + std::to_string(entry)}}));
}
void Bot::period(Id u, const std::string &a, const std::string &b, int page, bool chart) {
    auto dates = days(a, b);
    if (chart) {
        auto n = db_.rows("SELECT COUNT(*) AS n FROM entries WHERE user=? AND kind='weight' AND "
                          "deleted=0 AND day BETWEEN ? AND ?",
                          {u, a, b})[0]["n"]
                     .get<int>();
        if (!n) {
            say(u, "За период нет измерений веса.");
            return;
        }
        db_.write("INSERT INTO outbox(user,method,payload) VALUES(?,'chart',?)",
                  {u, Json({{"start", a}, {"end", b}}).dump()});
        return;
    }
    int first = page * 7;
    if (first >= static_cast<int>(dates.size()))
        throw InputError("Нет такой страницы.");
    std::string out = "Период " + a + " — " + b + "\n";
    for (int i = first; i < std::min(first + 7, static_cast<int>(dates.size())); ++i)
        out += summary(db_, u, dates[i]) + "\n\n";
    auto total = db_.rows(
        "SELECT COALESCE(SUM(kcal),0) AS kcal,COALESCE(SUM(protein),0) AS "
        "protein,COALESCE(SUM(fat),0) AS fat,COALESCE(SUM(carbs),0) AS carbs,COALESCE(SUM(CASE "
        "WHEN kind='water' THEN amount ELSE "
        "0 END),0) AS water FROM entries WHERE user=? AND deleted=0 AND day BETWEEN ? AND ?",
        {u, a, b})[0];
    out += "Всего за период записано: " + total["kcal"].dump() + " ккал; Б/Ж/У " +
           total["protein"].dump() + "/" + total["fat"].dump() + "/" + total["carbs"].dump() +
           " г, " + total["water"].dump() + " мл. Пустые дни — отсутствие записей.";
    std::vector<std::pair<std::string, std::string>> nav;
    if (page)
        nav.emplace_back("Назад", "page:" + a + " " + b + " " + std::to_string(page - 1));
    if (first + 7 < static_cast<int>(dates.size()))
        nav.emplace_back("Далее", "page:" + a + " " + b + " " + std::to_string(page + 1));
    say(u, out, buttons(nav));
}
} // namespace nb
