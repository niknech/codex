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
    for (const auto &[left, right] :
         std::vector<std::pair<std::string, std::string>>{{"🍽 Добавить еду", "💧 Добавить воду"},
                                                          {"⚖️ Записать вес", "📊 Итоги дня"},
                                                          {"📅 Статистика", "📈 График веса"},
                                                          {"🎯 Мои нормы", "⚙️ Настройки"},
                                                          {"↩️ Главное меню", "✖️ Отмена"}}) {
        keyboard.push_back(Json::array({Json{{"text", left}}, Json{{"text", right}}}));
    }
    return {{"keyboard", keyboard}, {"resize_keyboard", true}};
}
void Bot::say(Id user, const std::string &text, const Json &markup) {
    auto rows = db_.rows("SELECT chat FROM users WHERE id=?", {user});
    Json payload = {{"chat_id", rows.empty() ? user : rows[0]["chat"].get<Id>()}, {"text", text}};
    // Telegram entity offsets count UTF-16 code units, including two for emoji.
    // Entities format our heading without interpreting user text as HTML/Markdown.
    auto newline = text.find('\n');
    if (newline != std::string::npos) {
        int length = 0;
        for (unsigned char c : text.substr(0, newline))
            if ((c & 0xc0) != 0x80)
                length += c >= 0xf0 ? 2 : 1;
        if (length > 0 && length <= 100)
            payload["entities"] =
                Json::array({{{"type", "bold"}, {"offset", 0}, {"length", length}}});
    }
    payload["reply_markup"] = markup.is_null() ? menu() : markup;
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
    std::string text = "📅 " + display_day(d["day"]) + "\n\n";
    if (d["kind"] == "weight") {
        text = "⚖️ Новое измерение\n\n" + quantity(e["amount"].get<double>() / 1000) + " кг\n📅 " + display_day(d["day"]) + "\n";
        auto last = db_.rows("SELECT day,amount FROM entries WHERE user=? AND kind='weight' AND deleted=0 AND day<? ORDER BY day DESC LIMIT 1",{user,d["day"]});
        if (!last.empty()) {
            auto delta=(e["amount"].get<double>()-last[0]["amount"].get<double>())/1000;
            text += "\nРанее: " + quantity(last[0]["amount"].get<double>()/1000) + " кг · " + display_day(last[0]["day"]) +
                "\nИзменение: " + (delta>0?"+":"") + quantity(delta) + " кг\n";
        }
        text += "\n";
        auto previous = totals(db_, user, d["day"])["weight"];
        text += previous.is_null()
                    ? "Записать измерение?"
                    : "За этот день уже записано " + quantity(previous.get<double>() / 1000) +
                          " кг.\nЗаменить предыдущий вес?";
    } else
        text += meal_label(d["category"]) + "\n\n" + estimate_text(e);
    std::vector<std::pair<std::string, std::string>> actions;
    if (d["kind"] == "weight" || e.value("status", "") == "estimate")
        actions.emplace_back("✅ Записать", "save:" + suffix);
    if (d["kind"] == "meal") {
        actions.emplace_back("✏️ Исправить значения", "guided:" + suffix);
        if (cfg_.ai_enabled) {
            actions.emplace_back("💬 Уточнить", "clarify:" + suffix);
        }
    }
    actions.emplace_back("✖️ Отмена", "cancel:" + suffix);
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
                        else if (msg.contains("photo"))
                            say(user, "Фото не распознаются. Введи цифры штрихкода или добавь еду вручную.");
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
    if (text == "✖️ Отмена" || text == "↩️ Главное меню") {
        message(u,u,text == "✖️ Отмена" ? "/cancel" : "/start",t);
        return;
    }
    if (text.rfind("/ai_bonus", 0) == 0) {
        if (!cfg_.admins.count(u)) throw InputError("Команда доступна только администратору.");
        std::istringstream input(text);
        std::string command, target, amount = "5", extra;
        input >> command >> target;
        if (command != "/ai_bonus" || target.empty()) throw InputError("/ai_bonus ID [количество], по умолчанию +5");
        if (input >> amount) { if (input >> extra) throw InputError("/ai_bonus ID [количество]"); }
        Id id;
        try { size_t end; id = std::stoll(target, &end); if (end != target.size() || id <= 0) throw std::runtime_error("id"); }
        catch (...) { throw InputError("Некорректный ID."); }
        auto n = number(amount, 1, 100);
        if (n != std::floor(n)) throw InputError("Количество должно быть целым.");
        if (db_.rows("SELECT id FROM users WHERE id=?", {id}).empty()) throw InputError("Пользователь ещё не зарегистрирован.");
        auto day = local_day(t, "Etc/UTC");
        db_.write("INSERT INTO ai_bonus VALUES(?,?,?) ON CONFLICT(user,day) DO UPDATE SET amount=MIN(1000,amount+excluded.amount)", {id,day,static_cast<int>(n)});
        auto bonus = db_.rows("SELECT amount FROM ai_bonus WHERE user=? AND day=?",{id,day})[0]["amount"].get<int>();
        say(u, "✅ Лимит пользователя " + target + " на сегодня: " + std::to_string(cfg_.ai_daily_user + bonus) +
            " запросов.\nБонус действует до 00:00 UTC (03:00 МСК). Общий лимит бота сохраняется.");
        return;
    }
    if (text.size() > 3500)
        throw InputError("Сообщение слишком длинное (максимум 3500 байт).");
    auto user = db_.rows("SELECT * FROM users WHERE id=?", {u})[0];
    auto today = local_day(t, user["tz"]);
    if (text == "/cancel" || text == "/start") {
        db_.write("DELETE FROM sessions WHERE user=?", {u});
        db_.write("UPDATE drafts SET status='cancelled' WHERE user=? AND status='pending'", {u});
        say(u,
            text == "/start" ? "🍽 Твой дневник питания\n\nЕда, вода и вес — всё в одном "
                               "месте.\nВыбери действие в меню ниже.\n\n🕒 Часовой пояс: " +
                                   user["tz"].get<std::string>() +
                                   "\n🎯 Личные нормы можно заполнить позже.\n\nПодсказки и напоминания — в настройках."
                             : "👌 Ввод отменён\n\nМожно начать заново — выбери действие в меню.",
            menu());
        return;
    }
    if (text == "/help") {
        say(u, "📖 Как пользоваться дневником\n\n🍽 Еда → Завтрак / Обед / Ужин → AI или "
               "Вручную.\nAI оценит описание еды и порции, если подключён.\nВручную введи "
               "название и значения по шагам или четыре числа сразу.\n\n⚖️ Вес → число в кг → дата "
               "кнопкой или в календаре.\n\n💧 Вода → готовая порция или своё количество.\n📊 "
               "Итоги дня и 📅 Статистика — в меню.\n\n⚙️ В настройках — напоминания, часовой пояс и последние записи.\n✖️ Отмена — в нижнем меню, если хочешь начать заново.");
        return;
    }
    if (text == "📊 Итоги дня" || text == "/today") {
        say(u, summary(db_, u, today));
        return;
    }
    if (text == "💧 Добавить воду") {
        session(u, "water", {}, t);
        say(u, "💧 Добавим воду\n\nВыбери порцию или напиши своё количество в мл.",
            buttons(
                {{"+150 мл", "water:150"}, {"+250 мл", "water:250"}, {"+500 мл", "water:500"}, {"✖️ Отмена","nav:cancel"}}));
        return;
    }
    if (text == "⚖️ Записать вес") {
        screen(u, "weight_value", Json::object(), t);
        return;
    }
    if (text == "🍽 Добавить еду") {
        screen(u, "meal_category", {{"day", today}}, t);
        return;
    }
    if (text == "🎯 Мои нормы") {
        preferences(u,"pref_goal",Json::object(),t);
        return;
    }
    if (text == "⚙️ Настройки") {
        preferences(u,"pref_home",Json::object(),t);
        return;
    }
    if (text == "📅 Статистика" || text == "📈 График веса") {
        bool chart = text == "📈 График веса";
        std::string p = chart ? "chart:" : "stats:";
        say(u, "📅 За какой период посмотрим?",
            buttons({{"Сегодня", p + "1"},
                     {"Вчера", p + "0"},
                     {"7 дней", p + "7"},
                     {"14 дней", p + "14"},
                     {"Выбрать период", chart ? "custom:chart" : "custom:stats"}}));
        return;
    }
    if (text == "/entries") {
        auto entries = db_.rows(
            "SELECT id,day,kind,amount,kcal,description FROM entries WHERE user=? AND deleted=0 ORDER BY id DESC LIMIT 10",
            {u});
        if (entries.empty())
            say(u, "📝 Здесь появятся твои записи\n\nДобавь еду, воду или вес через меню.");
        for (auto &e : entries)
            say(u, "📅 " + display_day(e["day"]) + "\n\n" +
                (e["kind"]=="weight" ? "⚖️ Вес: "+quantity(e["amount"].get<double>()/1000)+" кг" :
                 e["kind"]=="water" ? "💧 Вода: "+quantity(e["amount"])+" мл" :
                 "🍽 "+e["description"].get<std::string>()+"\n🔥 "+quantity(e["kcal"])+" ккал"),
                buttons({{"Удалить эту запись", "undo:" + std::to_string(e["id"].get<Id>())}}));
        return;
    }
    if (text == "/ai off") {
        db_.write("UPDATE users SET ai_consent=0 WHERE id=?", {u});
        db_.write("DELETE FROM sessions WHERE user=? AND state LIKE 'product_%'", {u});
        db_.write("UPDATE product_jobs SET status='cancelled',payload='{}' WHERE user=? AND status='pending'", {u});
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
        say(u, "🕒 Готово! Теперь ориентируюсь на твоё время. Даты прошлых записей не изменились.");
        return;
    }
    if (text.rfind("/reminders ", 0) == 0) {
        auto v = text.substr(11);
        if (v != "on" && v != "off")
            throw InputError("/reminders on или off");
        db_.write("UPDATE users SET reminders=? WHERE id=?", {v == "on" ? 1 : 0, u});
        if (v == "off")
            db_.write("UPDATE outbox SET done=2 WHERE user=? AND done=0 AND expires>0", {u});
        say(u, "✅ Готово! Настройки напоминаний сохранены.");
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
    if ((text.empty() || text[0] != '/') && preference_message(u, text, t)) return;
    if ((text.empty() || text[0] != '/') && product_message(u, text, t)) return;
    if ((text.empty() || text[0] != '/') && ui_message(u, text, t))
        return;
    auto split = text.find(' ');
    auto cmd = text.substr(0, split);
    auto arg = split == std::string::npos ? "" : text.substr(split + 1);
    auto ss = db_.rows("SELECT * FROM sessions WHERE user=?", {u});
    Json payload;
    std::string state;
    if (!ss.empty()) {
        if (ss[0]["expires"].get<Id>() < t) {
            db_.write("DELETE FROM sessions WHERE user=?", {u});
            if (text.empty() || text[0] != '/') {
                say(u, "⌛ Давай начнём заново — выбери действие в меню.");
                return;
            }
        } else {
            state = ss[0]["state"];
            payload = Json::parse(ss[0]["payload"].get<std::string>());
        }
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
                throw InputError("Эта карточка уже изменилась. Открой последнюю версию.");
        } else
            id = draft(u, "meal", today, arg, e, t);
        offer(u, id);
    } else if (cmd == "/food" || cmd == "/fooddate" || cmd == "/clarify") {
        if (!cfg_.ai_enabled)
            throw InputError("🤖 AI пока недоступен. Выбери «Вручную» при добавлении еды — помогу заполнить значения.");
        Id id;
        if (cmd == "/clarify") {
            id = payload.at("id");
            auto d = db_.rows("SELECT description,estimate FROM drafts WHERE id=? AND user=? AND "
                              "status='pending' AND expires>=? AND version=?",
                              {id, u, t, payload.at("version")});
            if (d.empty())
                throw InputError("Эта карточка уже изменилась. Открой последнюю версию.");
            auto previous = Json::parse(d[0]["estimate"].get<std::string>());
            std::string context;
            if (previous.value("status", "") == "needs_clarification")
                context = "\nПредыдущий вопрос: " + previous["question"].get<std::string>();
            else if (previous.value("status", "") == "estimate")
                context = "\nПредыдущая оценка: " + estimate_text(previous);
            std::string desc =
                d[0]["description"].get<std::string>() + context + "\nУточнение: " + arg;
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
        say(u, "✅ Ориентиры сохранены\n\nНачнут действовать " + display_day(day) + ".\nНапоминания можно включить в настройках.");
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
        say(u, "👋 Чем займёмся?\nВыбери действие в меню. Подсказки найдёшь в настройках.", menu());
        return;
    }
    db_.write("DELETE FROM sessions WHERE user=?", {u});
}
void Bot::callback(Id u, const std::string &action, Id t) {
    auto sep = action.find(':');
    auto verb = action.substr(0, sep);
    auto value = sep == std::string::npos ? "" : action.substr(sep + 1);
    auto today = local_day(t, db_.rows("SELECT tz FROM users WHERE id=?", {u})[0]["tz"]);
    if (action=="nav:cancel") { message(u,u,"/cancel",t); return; }
    if (verb == "ui") {
        ui_callback(u, value, t);
        return;
    }
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
        say(u, "📅 Выбери свой период\n\nНапиши начало и конец через пробел. Например: 01.10.2026 10.10.2026");
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
            throw InputError("Эта кнопка уже не подходит. Выбери действие в меню.");
    } catch (const std::logic_error &) {
        throw InputError("Эта кнопка уже не подходит. Выбери действие в меню.");
    }
    if (verb == "undo") {
        db_.write("UPDATE entries SET deleted=1 WHERE id=? AND user=? AND deleted=0", {id, u});
        say(u, db_.changes() ? "Запись отменена." : "Запись недоступна или уже отменена.");
        return;
    }
    auto rows = db_.rows("SELECT * FROM drafts WHERE id=? AND user=?", {id, u});
    if (rows.empty())
        throw InputError("Эта карточка уже недоступна. Начни заново через меню.");
    auto d = rows[0];
    if (d["status"] != "pending")
        throw InputError(d["status"] == "saved" ? "✅ Это уже сохранено в дневнике." : "Ты уже отменил эту запись. Можно добавить новую через меню.");
    if (d["expires"].get<Id>() < t)
        throw InputError("Мы давно не возвращались к этой записи. Давай начнём заново через меню.");
    if (version_sep == std::string::npos ||
        value.substr(version_sep + 1) != std::to_string(d["version"].get<Id>()))
        throw InputError("Оценка изменилась. Используйте кнопки под последней оценкой.");
    if (verb == "cancel") {
        db_.write("UPDATE drafts SET status='cancelled' WHERE id=?", {id});
        say(u, "Отменено.");
        return;
    }
    if (verb == "guided") {
        if (d["kind"] != "meal")
            throw InputError("Это не запись еды.");
        auto estimate = Json::parse(d["estimate"].get<std::string>());
        if (estimate.value("status", "") == "estimate") {
            validate_estimate(estimate);
            screen(u, estimate["items"].size() == 1 ? "meal_edit_fields" : "meal_edit_items",
                   {{"edit_id", id},
                    {"edit_version", d["version"]},
                    {"estimate", estimate},
                    {"item", 0}},
                   t);
            return;
        }
        screen(u, "meal_name",
               {{"day", d["day"]},
                {"category", d["category"]},
                {"edit_id", id},
                {"edit_version", d["version"]}},
               t);
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
        say(u, verb == "edit" ? "Введите ккал Б Ж У ; название"
                              : "💬 Напиши, что изменить.\nНапример: сок 100 мл, яйца жареные. "
                                "Пересчитаю всю порцию.");
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
    db_.write("UPDATE entries SET category=? WHERE id=?", {d["category"], entry});
    db_.write("UPDATE drafts SET status='saved' WHERE id=?", {id});
    say(u, "✅ Записано\n\n" + summary(db_, u, d["day"]),
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
    constexpr int page_size = 3;
    int first = page * page_size;
    if (first >= static_cast<int>(dates.size()))
        throw InputError("Нет такой страницы.");
    std::string out = "📊 Статистика\n📅 " + display_day(a) + " — " + display_day(b) + "\n\n";
    for (int i = first; i < std::min(first + page_size, static_cast<int>(dates.size())); ++i)
        out += summary(db_, u, dates[i]) + "\n\n────────────\n\n";
    auto total = db_.rows(
        "SELECT COALESCE(SUM(kcal),0) AS kcal,COALESCE(SUM(protein),0) AS "
        "protein,COALESCE(SUM(fat),0) AS fat,COALESCE(SUM(carbs),0) AS carbs,COALESCE(SUM(CASE "
        "WHEN kind='water' THEN amount ELSE "
        "0 END),0) AS water FROM entries WHERE user=? AND deleted=0 AND day BETWEEN ? AND ?",
        {u, a, b})[0];
    out +=
        "📌 Всего за период\n🔥 " + quantity(total["kcal"]) +
        " ккал\n🥚 Белки: " + quantity(total["protein"]) +
        " г\n🥑 Жиры: " + quantity(total["fat"]) + " г\n🍞 Углеводы: " + quantity(total["carbs"]) +
        " г\n💧 Вода: " + quantity(total["water"]) + " мл\n\nСтраница " + std::to_string(page + 1) +
        " из " + std::to_string((dates.size() + page_size - 1) / page_size) +
        "\nПустой день означает отсутствие записей.";
    std::vector<std::pair<std::string, std::string>> nav;
    if (page)
        nav.emplace_back("Назад", "page:" + a + " " + b + " " + std::to_string(page - 1));
    if (first + page_size < static_cast<int>(dates.size()))
        nav.emplace_back("Далее", "page:" + a + " " + b + " " + std::to_string(page + 1));
    say(u, out, buttons(nav));
}
} // namespace nb
