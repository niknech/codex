#include "bot.hpp"
#include "schema.hpp"
#include "products.hpp"
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
        check(old.rows("SELECT MAX(version) AS v FROM schema_migrations")[0]["v"] == 3,
              "migration version 3");
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
        check(f.last()["reply_markup"]["inline_keyboard"].size() == 3, "three input methods");
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
        check(f.state() == "meal_edit_fields", "edit skips full manual wizard");
        f.click(f.action("field_0"));
        f.send("-5");
        check(f.state() == "meal_edit_value", "invalid edit stays on field");
        f.send("150");
        auto edited =
            Json::parse(f.db.rows("SELECT estimate FROM drafts WHERE id=?", {id})[0]["estimate"]
                            .get<std::string>());
        check(edited["items"][0]["name"] == "Суп" && edited["items"][0]["protein_g"] == 10 &&
                  edited["items"][0]["carbs_g"] == 30,
              "edit preserves name and other nutrients");
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
        check(a.db.rows("SELECT * FROM jobs").size() == 1,
              "AI selection starts estimate immediately");
        a.click("consent:" + suffix, 22);
        check(a.db.rows("SELECT * FROM jobs").size() == 1, "foreign AI consent rejected");
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
            check(request["text"]["format"]["schema"]["properties"]["status"]["enum"] ==
                      Json::array({"estimate", "not_food"}),
                  "AI cannot return a clarification question");
            auto estimated = manual_meal("260 6 2 56 ; рис 200 г");
            estimated["assumptions"] = Json::array({"Порция риса 200 г"});
            return response(estimated);
        });
        check(totals(a.db, 11, a.day)["kcal"] == 0, "clarification never saved");
        a.click("clarify:" + std::to_string(aid) + ":2");
        a.send("100 г варёного риса");
        ab.ai_one(a.t, [&](const Json &request) {
            check(request["input"].get<std::string>().find("100 г") != std::string::npos,
                  "clarification sent with original description");
            check(request["input"].get<std::string>().find("Порция риса 200 г") !=
                      std::string::npos,
                  "previous assumptions available for correction");
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
        Fixture e;
        e.send("/manual 200 10 5 20 ; стейк");
        auto eid = e.draft_id();
        auto meal = manual_meal("200 10 5 20 ; стейк");
        meal["items"].push_back(manual_meal("100 2 3 15 ; сок")["items"][0]);
        meal["assumptions"] = Json::array({"Считаю половину стейка за 150 г"});
        e.db.write("UPDATE drafts SET estimate=? WHERE id=?", {meal.dump(), eid});
        e.click("guided:" + std::to_string(eid) + ":1", 22);
        check(e.db.rows("SELECT * FROM sessions WHERE user=22").empty(),
              "foreign nutrient edit denied");
        e.click("guided:" + std::to_string(eid) + ":1");
        check(e.state() == "meal_edit_items", "multi-item estimate selects product");
        e.click(e.action("item_1"));
        e.click(e.action("field_1"));
        e.send("4,5");
        auto changed =
            Json::parse(e.db.rows("SELECT estimate FROM drafts WHERE id=?", {eid})[0]["estimate"]
                            .get<std::string>());
        check(changed["items"][0] == meal["items"][0], "unselected food unchanged");
        check(changed["items"][1]["protein_g"] == 4.5 && changed["items"][1]["kcal"] == 100,
              "selected nutrient changed only");
        check(changed["assumptions"] == meal["assumptions"], "portion explanation retained");
        auto explanation = estimate_text(changed);
        check(explanation.find("💡 Как я оценил порцию") != std::string::npos &&
                  explanation.find("Принято:") == std::string::npos,
              "friendly assumption heading");
        e.click("guided:" + std::to_string(eid) + ":2");
        e.click(e.action("item_0"));
        e.click(e.action("field_0"));
        e.db.write("UPDATE drafts SET version=version+1 WHERE id=?", {eid});
        e.send("1");
        check(Json::parse(e.db.rows("SELECT estimate FROM drafts WHERE id=?", {eid})[0]["estimate"]
                              .get<std::string>()) == changed,
              "stale editor cannot overwrite newer estimate");
        Fixture b;
        b.cfg.admins={11};
        b.send("/ai_bonus 22",22);
        check(b.db.rows("SELECT * FROM ai_bonus").empty(),"non-admin bonus denied");
        b.send("/ai_bonus 22");
        check(b.db.rows("SELECT amount FROM ai_bonus")[0]["amount"]==5,"admin bonus default and update dedup");
        b.send("/ai_bonus 22 3");
        check(b.db.rows("SELECT amount FROM ai_bonus")[0]["amount"]==8,"bonus increments");
        b.send("/ai_bonus 22 1.5");
        check(b.db.rows("SELECT amount FROM ai_bonus")[0]["amount"]==8,"fractional bonus rejected");
        b.send("🍽 Добавить еду"); b.click(b.action("cat_breakfast")); b.click(b.action("barcode"));
        b.send("3017620422003");
        Bot bb(b.db,b.cfg);
        Json product={{"barcode","3017620422003"},{"name","Тест"},{"unit","g"},{"package",200},
            {"kcal",500},{"protein",10},{"fat",20},{"carbs",60},{"source","test"},{"url",""}};
        auto lookup=[&](const std::string &) { return Json{{"product",{{"code","3017620422003"},
            {"product_name","Тест"},{"product_quantity_unit","g"},{"product_quantity",200},
            {"nutriments",{{"energy-kcal_100g",500},{"proteins_100g",10},{"fat_100g",20},{"carbohydrates_100g",60}}}}}}; };
        auto no_ai=[](const Json &) -> Json { throw std::runtime_error("unexpected AI"); };
        bb.product_one(b.t,no_ai,lookup);
        check(b.state()=="product_review","OFF lookup without AI");
        auto accept=b.action("accept"); b.click(accept,22);
        check(b.db.rows("SELECT * FROM products").empty(),"foreign product accept rejected");
        b.click(accept);
        check(b.state()=="product_amount","confirmed product portion");
        b.click(b.action("package"));
        check(totals(b.db,11,b.day)["kcal"]==0,"product awaits meal save");
        b.click("save:"+std::to_string(b.draft_id())+":1");
        check(totals(b.db,11,b.day)["kcal"]==1000,"package nutrition scaled");
        b.send("🍽 Добавить еду"); b.click(b.action("cat_lunch")); b.click(b.action("barcode"));
        b.send("3017620422003");
        check(b.state()=="product_review" && b.db.rows("SELECT * FROM product_jobs").size()==1,"confirmed cache avoids network");
        check(product_estimate(product,50)["items"][0]["kcal"]==250,"50 gram scaling");
        auto incomplete=lookup(""); incomplete["product"]["nutriments"].erase("fat_100g");
        check(off_product(incomplete,"3017620422003").is_null(),"missing nutrients not zero");
        Fixture w;
        w.cfg.ai_enabled=true; w.cfg.ai_web_search=true; w.cfg.model="gpt-6-luna";
        w.cfg.ai_daily_user=1; w.cfg.admins={11};
        w.send("🍽 Добавить еду"); w.click(w.action("cat_dinner")); w.click(w.action("barcode"));
        w.send("3017620422003");
        Bot wb(w.db,w.cfg);
        wb.product_one(w.t,no_ai,[](const std::string &){return Json::object();});
        check(w.db.rows("SELECT * FROM product_jobs WHERE paid=1").size()==1,"OFF miss reserves AI fallback");
        auto wrap=[](const Json &p) { return Json{{"status","completed"},{"output",Json::array({
            {{"type","message"},{"content",Json::array({{{"type","output_text"},{"text",p.dump()}}})}}})}}; };
        wb.product_one(w.t,[&](const Json &request){
            check(request["tools"][0]["type"]=="web_search","barcode web request");
            return wrap({{"found",false}});
        },lookup);
        check(w.state()=="product_missing","unmatched barcode offers manual input");
        auto photo=[&] { w.ingest({{"update_id",++w.seq},{"message",{{"from",{{"id",11}}},
            {"chat",{{"id",11},{"type","private"}}},{"photo",Json::array({{{"file_id","fixture-photo"}}})}}}}); };
        auto count=w.db.rows("SELECT * FROM product_jobs").size();
        photo();
        check(w.state()=="product_missing" && w.db.rows("SELECT * FROM product_jobs").size()==count,
              "photo creates no jobs and preserves flow");
        w.click(w.flow("web"));
        check(w.db.rows("SELECT * FROM product_jobs").size()==count,"quota enforced for retry");
        w.send("/ai_bonus 11"); w.click(w.flow("web"));
        check(w.state()=="product_wait","bonus enables web retry");
        w.send("/ai off");
        bool sent=false;
        wb.product_one(w.t,[&](const Json &){sent=true; return wrap(product);},lookup);
        check(!sent && w.state().empty(),"revoked consent cancels web job");
        w.cfg.ai_daily_global=2;
        w.send("🍽 Добавить еду"); w.click(w.action("cat_lunch")); w.click(w.action("ai")); w.send("рис");
        check(w.db.rows("SELECT * FROM jobs").empty(),"bonus cannot bypass global quota");
        auto unverified=product; unverified["found"]=true; unverified["url"]="https://invented.invalid";
        check(product_response(wrap(unverified),"web","3017620422003").is_null(),"web product needs actual source");
        bool checksum=false; try { barcode("3017620422004"); } catch(const InputError &) {checksum=true;}
        check(checksum,"invalid barcode checksum rejected");
        Fixture legacy;
        legacy.cfg.ai_enabled=true;
        legacy.db.write("INSERT INTO product_jobs(user,flow,kind,payload,day,paid) VALUES(11,1,'label','{}',?,1)",
            {local_day(legacy.t,"Etc/UTC")});
        Bot lb(legacy.db,legacy.cfg);
        lb.product_one(legacy.t,no_ai,lookup);
        check(legacy.db.rows("SELECT status,payload FROM product_jobs")[0]==Json({{"status","cancelled"},{"payload","{}"}}),
              "legacy photo jobs cancelled without API access");
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
