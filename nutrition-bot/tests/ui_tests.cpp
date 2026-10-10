#include "bot.hpp"
#include "schema.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
using namespace nb;
int checks = 0;
void check(bool ok, const char *name) {
    ++checks;
    if (!ok)
        throw std::runtime_error(name);
}
struct Fixture {
    Database db{":memory:"};
    Config cfg;
    Id seq = 0, t = now();
    std::string day;
    Fixture() {
        cfg.allowed = {11, 22};
        db.migrate();
        day = local_day(t, cfg.timezone);
        send("/start");
        send("/start", 22);
    }
    void ingest(Json update) {
        Bot bot(db, cfg);
        bot.ingest(Json::array({update, update}));
        while (bot.process_one(t)) {
        };
    }
    void send(const std::string &text, Id u = 11) {
        ingest({{"update_id", ++seq},
                {"message",
                 {{"from", {{"id", u}}},
                  {"chat", {{"id", u}, {"type", "private"}}},
                  {"text", text}}}});
    }
    void click(const std::string &action, Id u = 11) {
        ingest({{"update_id", ++seq},
                {"callback_query",
                 {{"id", std::to_string(seq)},
                  {"from", {{"id", u}}},
                  {"message", {{"chat", {{"id", u}, {"type", "private"}}}}},
                  {"data", action}}}});
    }
    Json last(Id u = 11) {
        return Json::parse(db.rows("SELECT payload FROM outbox WHERE user=? AND "
                                   "method='sendMessage' ORDER BY id DESC LIMIT 1",
                                   {u})[0]["payload"]
                               .get<std::string>());
    }
    std::string state() {
        auto r = db.rows("SELECT state FROM sessions WHERE user=11");
        return r.empty() ? "" : r[0]["state"].get<std::string>();
    }
    std::string action(const std::string &ending) {
        auto p = last();
        for (auto &row : p["reply_markup"]["inline_keyboard"])
            for (auto &b : row) {
                std::string s = b["callback_data"];
                if (s.ends_with(ending))
                    return s;
            }
        throw std::runtime_error("button not found: " + ending);
    }
    std::string flow(const std::string &action) {
        auto p = Json::parse(
            db.rows("SELECT payload FROM sessions WHERE user=11")[0]["payload"].get<std::string>());
        return "ui:" + std::to_string(p["flow"].get<Id>()) + ":" + action;
    }
    Id draft_id() {
        return db.rows("SELECT MAX(id) AS id FROM drafts WHERE user=11")[0]["id"].get<Id>();
    }
};
int main() {
    try {
        init_timezones();
        // The released v1 schema must migrate without losing existing nutrition data.
        Database old(":memory:");
        old.exec(schema_sql);
        old.exec("INSERT INTO users(id,chat,tz) VALUES(1,1,'Europe/Moscow'); INSERT INTO "
                 "entries(user,kind,day,utc,tz,amount) "
                 "VALUES(1,'weight','2026-10-01',0,'Europe/Moscow',82400)");
        old.migrate();
        old.migrate();
        check(old.rows("SELECT amount,category FROM entries")[0] ==
                  Json({{"amount", 82400}, {"category", "unspecified"}}),
              "migration preserves old weight");
        check(old.rows("SELECT MAX(version) AS v FROM schema_migrations")[0]["v"] == 2,
              "migration version 2");
        auto cal = calendar_buttons("2024-02-01", "2024-03-01", "ui:1:");
        bool leap = false;
        for (auto &row : cal["inline_keyboard"])
            for (auto &b : row) {
                if (b["callback_data"] == "ui:1:date_2024-02-29")
                    leap = true;
                check(b["callback_data"].get<std::string>().size() <= 64, "callback length");
            }
        check(leap, "leap date available");
        check(cal["inline_keyboard"][1][3]["text"] == "1", "calendar Monday alignment");
        cal = calendar_buttons("2026-01-01", "2026-01-10", "ui:1:");
        for (auto &row : cal["inline_keyboard"])
            for (auto &b : row) {
                auto s = b["callback_data"].get<std::string>();
                check(s != "ui:1:date_2026-01-11", "future dates disabled");
            }
        Fixture f;
        f.send("🍽 Добавить еду");
        auto stale = f.action("cat_breakfast");
        f.click(stale, 22);
        check(f.db.rows("SELECT * FROM sessions WHERE user=22").empty(),
              "foreign wizard button rejected");
        f.click(stale);
        check(f.state() == "meal_method", "category step");
        check(f.last()["reply_markup"]["inline_keyboard"].size() == 2, "two input methods");
        auto manual = f.action("manual"), ai = f.action("ai");
        f.click(ai);
        check(f.db.rows("SELECT * FROM jobs").empty(), "disabled AI no request");
        f.click(manual);
        f.send("Омлет и тост");
        check(f.state() == "meal_values", "name entered");
        f.send("-1");
        check(f.state() == "meal_values" && f.db.rows("SELECT * FROM drafts").empty(),
              "invalid kcal stays on step");
        auto preset = f.flow("pick_250");
        f.click(preset);
        f.click(preset);
        auto p = Json::parse(f.db.rows("SELECT payload FROM sessions WHERE user=11")[0]["payload"]
                                 .get<std::string>());
        check(p["step"] == 1, "replayed button cannot fill next field");
        f.send("20");
        f.click(f.flow("back"));
        f.send("25");
        f.send("10,5");
        f.send("30");
        Id id = f.draft_id();
        check(totals(f.db, 11, f.day)["kcal"] == 0, "wizard draft excluded");
        check(f.db.rows("SELECT category FROM drafts WHERE id=?", {id})[0]["category"] ==
                  "breakfast",
              "draft breakfast category");
        f.click("save:" + std::to_string(id) + ":1");
        f.click("save:" + std::to_string(id) + ":1");
        auto total = totals(f.db, 11, f.day);
        check(total["kcal"] == 250 && total["protein"] == 25 && total["fat"] == 10.5,
              "wizard nutrients correct once");
        check(f.db.rows("SELECT category FROM entries WHERE draft=?", {id})[0]["category"] ==
                  "breakfast",
              "saved category");
        for (auto category : {"lunch", "dinner"}) {
            f.send("🍽 Добавить еду");
            f.click(f.action(std::string("cat_") + category));
            f.click(f.action("manual"));
            f.click(f.action("skip_name"));
            f.click(f.action("compact"));
            f.send("100 1 2");
            check(f.state() == "meal_compact", "compact requires four values");
            f.send("100 1 2 3");
            id = f.draft_id();
            f.click("save:" + std::to_string(id) + ":1");
            check(f.db.rows("SELECT category FROM entries WHERE draft=?", {id})[0]["category"] ==
                      category,
                  "lunch/dinner persistence");
        }
        f.send("🍽 Добавить еду");
        f.click(f.action("cat_lunch"));
        f.click(f.action("manual"));
        f.send("Суп");
        f.click(f.action("compact"));
        f.send("200 10 5 30");
        id = f.draft_id();
        f.click("guided:" + std::to_string(id) + ":1");
        f.send("Суп, исправлено");
        f.click(f.action("compact"));
        f.send("150 10 5 20");
        f.click("save:" + std::to_string(id) + ":1");
        check(totals(f.db, 11, f.day)["kcal"] == 450, "old confirmation invalid after editing");
        f.click("save:" + std::to_string(id) + ":2");
        check(totals(f.db, 11, f.day)["kcal"] == 600, "edited meal saved");
        f.send("⚖️ Записать вес");
        f.send("82.4 01.01.2026");
        check(f.state() == "weight_value", "weight asks single number");
        f.send("82,4");
        check(f.state() == "weight_date", "weight then date");
        f.click(f.action("calendar"));
        check(f.state() == "weight_calendar", "calendar displayed");
        f.click(f.flow("date_" + shift_day(f.day, 1)));
        check(f.state() == "weight_calendar", "forged future date rejected");
        f.click(f.flow("month_2024-02-01"));
        f.click(f.action("date_2024-02-29"));
        id = f.draft_id();
        f.click("save:" + std::to_string(id) + ":1");
        check(totals(f.db, 11, "2024-02-29")["weight"] == 82400, "calendar selected date saved");
        f.send("⚖️ Записать вес");
        f.send("83");
        auto date = f.action("date_" + f.day);
        f.click(date);
        id = f.draft_id();
        f.click(date);
        check(f.draft_id() == id, "date double click no second draft");
        f.click("save:" + std::to_string(id) + ":1");
        f.send("⚖️ Записать вес");
        f.send("84");
        f.click(f.action("date_" + f.day));
        check(f.last()["text"].get<std::string>().find("83") != std::string::npos,
              "replacement shows prior weight");
        check(totals(f.db, 11, f.day)["weight"] == 83000,
              "replacement still requires confirmation");
        f.send("🍽 Добавить еду");
        stale = f.action("cat_dinner");
        f.send("/cancel");
        f.click(stale);
        check(f.state().empty(), "cancel invalidates flow");
        f.send("🍽 Добавить еду");
        stale = f.action("cat_dinner");
        f.db.write("UPDATE sessions SET expires=? WHERE user=11", {f.t - 1});
        f.click(stale);
        check(f.state() == "meal_category", "expired button cannot advance");
        auto report = summary(f.db, 11, f.day);
        check(report.find("🌅 Завтрак: 250") != std::string::npos &&
                  report.find("☀️ Обед: 250") != std::string::npos &&
                  report.find("🌙 Ужин: 100") != std::string::npos,
              "daily category sums");
        f.send("/stats 14");
        check(f.last()["text"].get<std::string>().find("Страница 1 из 5") != std::string::npos,
              "report pagination");
        auto next = f.action(" 1");
        f.click(next);
        check(f.last()["text"].get<std::string>().find("Страница 2 из 5") != std::string::npos,
              "report next page");
        for (auto &row : f.db.rows("SELECT payload FROM outbox WHERE method='sendMessage'")) {
            auto data = Json::parse(row["payload"].get<std::string>());
            auto text = data["text"].get<std::string>();
            int utf16 = 0;
            for (unsigned char c : text)
                if ((c & 0xc0) != 0x80)
                    utf16 += c >= 0xf0 ? 2 : 1;
            check(utf16 <= 4096, "message fits Telegram length");
        }
        // Guided AI: consent, category, clarification, confirmation and cost limits.
        Fixture a;
        a.cfg.ai_enabled = true;
        a.cfg.model = "gpt-5-nano";
        a.cfg.ai_daily_user = 2;
        a.send("🍽 Добавить еду");
        a.click(a.action("cat_lunch"));
        a.click(a.action("ai"));
        check(a.state() == "meal_ai", "AI description screen");
        a.send("   ");
        check(a.db.rows("SELECT * FROM drafts").empty(), "empty AI description rejected");
        a.send("Рис");
        auto aid = a.draft_id();
        auto suffix = std::to_string(aid) + ":1";
        check(a.db.rows("SELECT * FROM jobs").empty(), "guided AI requires consent");
        a.click("consent:" + suffix, 22);
        check(a.db.rows("SELECT * FROM jobs").empty(), "foreign AI consent rejected");
        a.click("consent:" + suffix);
        a.click("consent:" + suffix);
        check(a.db.rows("SELECT * FROM jobs").size() == 1, "duplicate consent costs once");
        Bot ab(a.db, a.cfg);
        auto response = [](const Json &estimate) {
            return Json{{"status", "completed"},
                        {"output",
                         Json::array({{{"type", "message"},
                                       {"content", Json::array({{{"type", "output_text"},
                                                                 {"text", estimate.dump()}}})}}})}};
        };
        ab.ai_one(a.t, [&](const Json &request) {
            check(request["input"] == "Рис" && request["store"] == false,
                  "only description sent without storage");
            check(request["model"] == "gpt-5-nano" && request["reasoning"]["effort"] == "low",
                  "cheap model bounded reasoning");
            return response({{"status", "needs_clarification"},
                             {"question", "Сколько граммов?"},
                             {"items", Json::array()},
                             {"assumptions", Json::array()}});
        });
        check(totals(a.db, 11, a.day)["kcal"] == 0, "clarification never saved");
        a.click("clarify:" + std::to_string(aid) + ":2");
        a.send("100 г варёного риса");
        ab.ai_one(a.t, [&](const Json &request) {
            check(request["input"].get<std::string>().find("100 г") != std::string::npos,
                  "clarification sent with original description");
            return response(manual_meal("130 3 1 28 ; рис"));
        });
        check(totals(a.db, 11, a.day)["kcal"] == 0, "AI result needs confirmation");
        a.click("save:" + suffix);
        check(totals(a.db, 11, a.day)["kcal"] == 0, "stale AI save denied");
        a.click("save:" + std::to_string(aid) + ":4");
        check(a.db.rows("SELECT category,kcal FROM entries")[0] ==
                  Json({{"category", "lunch"}, {"kcal", 130}}),
              "AI retains category on save");
        a.send("🍽 Добавить еду");
        a.click(a.action("cat_dinner"));
        a.click(a.action("ai"));
        a.send("100 г риса");
        check(a.db.rows("SELECT * FROM jobs").size() == 2, "AI daily limit enforced");
        check(totals(a.db, 11, a.day)["kcal"] == 130, "limit does not add food");
        // Save one synthetic preview, without user data, when requested by the developer.
        if (auto path = std::getenv("BOT_UI_PREVIEW")) {
            std::ofstream out(path);
            out << report;
        }
        std::cout << checks << " UI/migration checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
