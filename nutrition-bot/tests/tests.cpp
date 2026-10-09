#include "bot.hpp"
#include <filesystem>
#include <iostream>
#include <limits>
#include <random>
using namespace nb;
static int checks = 0;
static void check(bool condition, const char *label) {
    ++checks;
    if (!condition)
        throw std::runtime_error(label);
}
template <class F> void rejects(F f, const char *label) {
    bool threw = false;
    try {
        f();
    } catch (const InputError &) {
        threw = true;
    }
    check(threw, label);
}
int main() {
    try {
        init_timezones();
        check(water_ml("0,5 л") == 500, "litres");
        check(water_ml("250 мл") == 250, "ml");
        for (auto s : {"-1", "nan", "inf", "1x", "0", "10001", "0.5 мл"})
            rejects([&] { water_ml(s); }, "invalid water");
        check(parse_day("29.02.2024") == "2024-02-29", "leap day");
        rejects([] { parse_day("29.02.2025"); }, "bad leap day");
        check(days("2026-09-28", "2026-10-04").size() == 7, "seven days");
        check(shift_day("2026-01-01", -1) == "2025-12-31", "year boundary");
        check(local_day(0, "America/New_York") == "1969-12-31", "timezone west");
        check(local_day(0, "Europe/Moscow") == "1970-01-01", "timezone east");
        auto estimate = manual_meal("420 20 18 45 ; lunch");
        check(validate_estimate(estimate)["items"].size() == 1, "manual estimate");
        auto bad = estimate;
        bad["items"][0]["kcal"] = -1;
        rejects([&] { validate_estimate(bad); }, "negative AI");
        bad["items"][0]["kcal"] = "42";
        rejects([&] { validate_estimate(bad); }, "string AI number");
        rejects([] { ai_response({{"status", "incomplete"}}); }, "incomplete AI");
        rejects(
            [] {
                ai_response(Json::parse(
                    R"({"status":"completed","output":[{"type":"message","content":[{"type":"refusal"}]}]})"));
            },
            "AI refusal");
        auto root = std::filesystem::temp_directory_path() /
                    ("nutrition-test-" + std::to_string(std::random_device{}()));
        std::filesystem::create_directory(root);
        auto path = (root / "test.sqlite3").string();
        {
            Database db(path);
            db.migrate();
            db.migrate();
            Config cfg;
            cfg.allowed = {11, 22};
            cfg.ai_enabled = true;
            cfg.model = "test";
            Bot bot(db, cfg);
            Id sequence = 0, t = now();
            auto day = local_day(t, cfg.timezone);
            auto send = [&](Id user, const std::string &text) {
                Json update = {{"update_id", ++sequence},
                               {"message",
                                {{"from", {{"id", user}}},
                                 {"chat", {{"id", user}, {"type", "private"}}},
                                 {"text", text}}}};
                bot.ingest(Json::array({update, update}));
                while (bot.process_one(t)) {
                };
            };
            auto click = [&](Id user, std::string data) {
                if (data.rfind("save:", 0) == 0 && data.find(':', 5) == std::string::npos)
                    data += ":1";
                bot.ingest(
                    Json::array({{{"update_id", ++sequence},
                                  {"callback_query",
                                   {{"id", std::to_string(sequence)},
                                    {"from", {{"id", user}}},
                                    {"message", {{"chat", {{"id", user}, {"type", "private"}}}}},
                                    {"data", data}}}}}));
                while (bot.process_one(t)) {
                };
            };
            send(11, "/start");
            send(22, "/start");
            send(33, "/water 500");
            check(db.rows("SELECT id FROM users WHERE id=33").empty(), "allow list");
            send(11, "/water 0,5 л");
            send(22, "/water 150");
            check(totals(db, 11, day)["water"] == 500, "dedup updates");
            check(totals(db, 22, day)["water"] == 150, "isolation");
            auto entry = db.rows("SELECT id FROM entries WHERE user=11")[0]["id"].get<Id>();
            click(22, "undo:" + std::to_string(entry));
            check(totals(db, 11, day)["water"] == 500, "foreign undo denied");
            click(11, "undo:" + std::to_string(entry));
            check(totals(db, 11, day)["water"] == 0, "undo exact entry");
            send(11, "/weight 82.4");
            auto id = db.rows("SELECT MAX(id) AS id FROM drafts")[0]["id"].get<Id>();
            check(totals(db, 11, day)["weight"].is_null(), "weight draft excluded");
            click(11, "save:" + std::to_string(id));
            check(totals(db, 11, day)["weight"] == 82400, "weight saved");
            send(11, "/weight 83");
            id = db.rows("SELECT MAX(id) AS id FROM drafts")[0]["id"].get<Id>();
            check(totals(db, 11, day)["weight"] == 82400, "replacement requires confirmation");
            click(11, "save:" + std::to_string(id));
            check(totals(db, 11, day)["weight"] == 83000, "weight replaced");
            send(11, "/manual 420 20 18 45 ; lunch");
            id = db.rows("SELECT MAX(id) AS id FROM drafts")[0]["id"].get<Id>();
            check(totals(db, 11, day)["kcal"] == 0, "meal draft excluded");
            click(22, "save:" + std::to_string(id));
            check(totals(db, 22, day)["kcal"] == 0, "foreign save denied");
            click(11, "save:" + std::to_string(id));
            click(11, "save:" + std::to_string(id));
            check(totals(db, 11, day)["kcal"] == 420, "confirm idempotent");
            send(11, "/goals 2000 100 70 240 2000");
            send(11, "/goals 1800 100 60 220 1800");
            check(goal(db, 11, day)["kcal"] == 2000, "historical goals");
            check(goal(db, 11, shift_day(day, 1))["kcal"] == 1800, "future goals");
            check(totals(db, 11, shift_day(day, -1))["weight"].is_null(),
                  "missing weight not zero");
            // Set reminder window to current local hour, so test has no dependency on wall-clock
            // hour.
            int hour = local_minute(t, cfg.timezone) / 60;
            db.write(
                "UPDATE users SET reminders=1,water_hour1=?,water_hour2=?,kcal_pct=21 WHERE id=11",
                {hour, (hour + 2) % 24});
            bot.reminders(t);
            bot.reminders(t);
            check(db.rows("SELECT * FROM notices WHERE user=11").size() == 2,
                  "once per date and rule at kcal threshold");
            send(22, "/goals " + shift_day(day, 1) + " 2000 100 70 240 500");
            check(goal(db, 22, day).is_null(), "explicit first future goal respected");
            send(22, "/goals 2000 100 70 240 500");
            db.write("UPDATE users SET reminders=1,water_hour1=?,water_hour2=? WHERE id=22",
                     {hour, (hour + 2) % 24});
            bot.reminders(t);
            check(db.rows("SELECT * FROM notices WHERE user=22").empty(),
                  "water at 30 percent no reminder");
            send(11, "/food 200 g rice");
            id = db.rows("SELECT MAX(id) AS id FROM drafts")[0]["id"].get<Id>();
            check(db.rows("SELECT * FROM jobs").empty(), "AI consent required");
            click(11, "consent:" + std::to_string(id) + ":1");
            bot.ai_one(t, [](const Json &) -> Json { throw ApiError(429, 30); });
            check(db.rows("SELECT * FROM jobs WHERE status='failed'").size() == 1,
                  "AI 429 no automatic charged retry");
            check(totals(db, 11, day)["kcal"] == 420, "AI failure no meal");
            click(11, "retry:" + std::to_string(id) + ":1");
            bot.ai_one(t, [&](const Json &) {
                auto response = Json::parse(
                    R"({"status":"completed","output":[{"type":"message","content":[{"type":"output_text","text":""}]}]})");
                response["output"][0]["content"][0]["text"] = estimate.dump();
                return response;
            });
            check(totals(db, 11, day)["kcal"] == 420, "AI estimate awaits confirmation");
            click(11, "save:" + std::to_string(id) + ":1");
            check(totals(db, 11, day)["kcal"] == 420, "old estimate button rejected");
            click(11, "save:" + std::to_string(id) + ":2");
            check(totals(db, 11, day)["kcal"] == 840, "AI meal confirmed");
            send(11, "/manual 100 1 1 1 ; expired");
            id = db.rows("SELECT MAX(id) AS id FROM drafts")[0]["id"].get<Id>();
            db.write("UPDATE drafts SET expires=? WHERE id=?", {t - 1, id});
            click(11, "save:" + std::to_string(id));
            check(totals(db, 11, day)["kcal"] == 840, "expired draft rejected");
            send(11, "/food 100 g bread");
            send(11, "/cancel");
            bool called = false;
            bot.ai_one(t, [&](const Json &) -> Json {
                called = true;
                return Json();
            });
            check(!called, "cancelled AI job never sent");
            send(11, "/reminders off");
            db.write("DELETE FROM notices WHERE user=11");
            bot.reminders(t);
            check(db.rows("SELECT * FROM notices WHERE user=11").empty(), "disabled reminders");
#ifndef _WIN32
            for (Id chart_user : {Id{0}, Id{11}}) {
                auto png = make_chart(db, chart_user, shift_day(day, -6), day);
                check(std::filesystem::file_size(png) > 100,
                      "valid PNG chart (empty/single point)");
                std::filesystem::remove_all(std::filesystem::path(png).parent_path());
            }
#endif
            auto before = totals(db, 11, day);
            db.backup((root / "backup.sqlite3").string());
            Database restored((root / "backup.sqlite3").string());
            check(totals(restored, 11, day) == before, "backup restore totals");
            check(restored.rows("PRAGMA integrity_check")[0]["integrity_check"] == "ok",
                  "backup integrity");
            bot.ingest(Json::array({{{"update_id", ++sequence},
                                     {"message",
                                      {{"from", {{"id", 11}}},
                                       {"chat", {{"id", 11}, {"type", "private"}}},
                                       {"text", "/water 250"}}}}}));
        }
        {
            Database reopened(path);
            Config cfg;
            cfg.allowed = {11};
            Bot bot(reopened, cfg);
            check(bot.process_one(now()), "persisted inbox after restart");
            check(!bot.process_one(now()), "inbox consumed once");
            check(totals(reopened, 11, local_day(now(), cfg.timezone))["water"] == 250,
                  "restart preserved input");
        }
        std::filesystem::remove_all(root);
        std::cout << checks << " checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << "\n";
        return 1;
    }
}
