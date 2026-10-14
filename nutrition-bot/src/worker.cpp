#include "bot.hpp"
#include <algorithm>
#include <filesystem>
#include <iostream>
namespace nb {
void Bot::check_ai_limit(Id u, Id t) {
    auto day = local_day(t, "Etc/UTC");
    auto counts = db_.rows("SELECT COUNT(*) AS total,COALESCE(SUM(user=?),0) AS personal FROM "
        "(SELECT user FROM jobs WHERE day=? UNION ALL SELECT user FROM product_jobs WHERE day=? AND paid=1)",
        {u, day, day})[0];
    auto bonus = db_.rows("SELECT COALESCE(SUM(amount),0) AS n FROM ai_bonus WHERE user=? AND day=?", {u,day})[0]["n"].get<int>();
    if (counts["total"].get<int>() >= cfg_.ai_daily_global)
        throw InputError("Общий дневной лимит AI исчерпан. Ручной ввод доступен.");
    if (counts["personal"].get<int>() >= cfg_.ai_daily_user + bonus)
        throw InputError("Твой дневной лимит AI исчерпан. Попроси владельца добавить запросы.");
}
void Bot::queue_ai(Id u, Id id, Id t) {
    if (!cfg_.ai_enabled)
        throw InputError("AI отключён. Используйте ручной ввод.");
    if (db_.rows("SELECT ai_consent FROM users WHERE id=?", {u})[0]["ai_consent"] == 0)
        throw InputError("Сначала разрешите отправку описания AI через новое добавление еды.");
    auto d =
        db_.rows("SELECT * FROM drafts WHERE id=? AND user=? AND status='pending' AND expires>=?",
                 {id, u, t});
    if (d.empty())
        throw InputError("⌛ Эта карточка уже устарела. Добавь еду заново через меню.");
    if (!db_.rows("SELECT id FROM jobs WHERE draft=? AND version=? AND status IN "
                  "('pending','running')",
                  {id, d[0]["version"]})
             .empty()) {
        say(u, "Оценка уже в очереди.");
        return;
    }
    // Limits count reservations, including failed requests, in a common UTC day.
    auto day = local_day(t, "Etc/UTC");
    check_ai_limit(u, t);
    db_.write("INSERT INTO jobs(draft,version,user,day,due) VALUES(?,?,?,?,?)",
              {id, d[0]["version"], u, day, t});
    auto suffix = std::to_string(id) + ":" + std::to_string(d[0]["version"].get<Id>());
    say(u, "🤖 Считаю КБЖУ…\nПокажу, что получилось. Размер порции можно будет уточнить.",
        buttons({{"Повторить после сбоя", "retry:" + suffix},
                 {"✍️ Ввести вручную", "guided:" + suffix},
                 {"Отмена", "cancel:" + suffix}}));
}
bool Bot::ai_one(Id t, const AiTransport &transport) {
    Json job, d;
    {
        Transaction tx(db_);
        auto rows = db_.rows(
            "SELECT * FROM jobs WHERE status='pending' AND due<=? ORDER BY id LIMIT 1", {t});
        if (rows.empty()) {
            tx.commit();
            return false;
        }
        job = rows[0];
        auto drafts = db_.rows(
            "SELECT * FROM drafts WHERE id=? AND version=? AND status='pending' AND expires>=?",
            {job["draft"], job["version"], t});
        if (!cfg_.ai_enabled || drafts.empty() ||
            db_.rows("SELECT ai_consent FROM users WHERE id=?", {job["user"]})[0]["ai_consent"] ==
                0) {
            db_.write("UPDATE jobs SET status='cancelled' WHERE id=?", {job["id"]});
            tx.commit();
            return true;
        }
        d = drafts[0];
        db_.write("UPDATE jobs SET status='running',attempts=attempts+1 WHERE id=?", {job["id"]});
        tx.commit();
    }
    Json estimate;
    std::string failure;
    try {
        estimate =
            ai_response(transport(ai_request(cfg_.model, d["description"], cfg_.ai_web_search)));
    } catch (const ApiError &e) {
        std::cerr << "AI request failed: API status " << e.status << '\n';
        failure = "🤖 Сейчас не получилось оценить еду.\n\nМожно повторить кнопкой или ввести "
                  "КБЖУ вручную. Сам повторно запрос не отправляю.";
        if (e.status == 401)
            failure = "🤖 Не удалось подключиться к AI.\n\nАдминистратору нужно проверить ключ "
                      "OpenAI. Пока можно добавить еду вручную.";
        else if (e.status == 403)
            failure = "🤖 Доступ к AI пока закрыт.\n\nАдминистратору нужно проверить доступ к "
                      "OpenAI. Пока можно добавить еду вручную.";
        else if (e.status == 429)
            failure = "⏳ AI пока не принимает запросы: достигнут лимит или закончился баланс "
                      "OpenAI.\n\nМожно попробовать позже или добавить еду вручную.";
    } catch (const InputError &e) {
        failure = e.what();
    } catch (const std::exception &) {
        failure = "🤖 Сейчас не получилось оценить еду. Попробуй ещё раз или выбери ручной ввод.";
    }
    Transaction tx(db_);
    db_.write("UPDATE jobs SET status=? WHERE id=?",
              {failure.empty() ? "done" : "failed", job["id"]});
    auto current = db_.rows(
        "SELECT id FROM drafts WHERE id=? AND version=? AND status='pending' AND expires>=?",
        {d["id"], d["version"], now()});
    if (!current.empty()) {
        if (failure.empty()) {
            db_.write("UPDATE drafts SET estimate=?,version=version+1 WHERE id=?",
                      {estimate.dump(), d["id"]});
            offer(job["user"], d["id"]);
        } else
            say(job["user"], failure,
                buttons({{"Повторить", "retry:" + std::to_string(d["id"].get<Id>()) + ":" +
                                           std::to_string(d["version"].get<Id>())},
                         {"Вручную", "guided:" + std::to_string(d["id"].get<Id>()) + ":" +
                                         std::to_string(d["version"].get<Id>())}}));
    }
    tx.commit();
    return true;
}
void Bot::reminders(Id t) {
    Transaction tx(db_);
    for (auto &u : db_.rows("SELECT * FROM users WHERE reminders=1")) {
        Id id = u["id"];
        auto day = local_day(t, u["tz"]);
        int minute = local_minute(t, u["tz"]);
        auto g = goal(db_, id, day);
        if (g.is_null())
            continue;
        auto total = totals(db_, id, day);
        auto notify = [&](const std::string &rule, const std::string &message, Id expires) {
            db_.write("INSERT OR IGNORE INTO notices VALUES(?,?,?)", {id, day, rule});
            if (db_.changes()) {
                say(id, message);
                db_.write("UPDATE outbox SET expires=? WHERE id=last_insert_rowid()", {expires});
            }
        };
        for (int i = 1; i <= 2; ++i) {
            auto suffix = std::to_string(i);
            int start = u["water_hour" + suffix].get<int>() * 60;
            if (minute >= start && minute < start + 60 && g["water"].get<double>() > 0 &&
                total["water"].get<double>() <
                    g["water"].get<double>() * u["water_pct" + suffix].get<double>() / 100)
                notify("water" + suffix,
                       "💧 Небольшое напоминание\n\nСегодня записано " + total["water"].dump() + " из " + g["water"].dump() +
                           " мл. Если уже пил ещё — добавь воду через меню.",
                       t + (start + 60 - minute) * 60 - t % 60);
        }
        if (g["kcal"].get<double>() > 0 &&
            total["kcal"].get<double>() >=
                g["kcal"].get<double>() * u["kcal_pct"].get<double>() / 100)
            notify("kcal",
                   "🔥 Твой ориентир на сегодня\n\nЗаписано " + quantity(total["kcal"]) + " из " + quantity(g["kcal"]) +
                       " ккал. Это напоминание по выбранному тобой порогу.",
                   t + std::min(60, 1440 - minute) * 60 - t % 60);
    }
    tx.commit();
}
void Bot::deliver_one(Telegram &telegram, Id t) {
    db_.write("UPDATE outbox SET done=2 WHERE done=0 AND expires>0 AND expires<=?", {t});
    auto rows = db_.rows("SELECT * FROM outbox WHERE done=0 AND due<=? ORDER BY "
                         "(method='answerCallbackQuery') DESC,id LIMIT 1",
                         {t});
    if (rows.empty())
        return;
    auto row = rows[0];
    auto p = Json::parse(row["payload"].get<std::string>());
    std::string path;
    try {
        if (row["method"] == "chart") {
            path = make_chart(db_, row["user"], p["start"], p["end"]);
            auto chat =
                db_.rows("SELECT chat FROM users WHERE id=?", {row["user"]})[0]["chat"].get<Id>();
            telegram.photo(chat, path);
        } else
            telegram.call(row["method"], p);
        db_.write("UPDATE outbox SET done=1,payload='{}' WHERE id=?", {row["id"]});
        std::clog << "Delivered outbox " << row["id"] << '\n';
    } catch (const ApiError &e) {
        std::cerr << "Delivery failed: outbox " << row["id"] << ", API status " << e.status << '\n';
        int attempts = row["attempts"].get<int>() + 1;
        bool terminal = (e.status >= 400 && e.status < 500 && e.status != 429) || attempts >= 8;
        Transaction tx(db_);
        if (e.status == 403) {
            db_.write("UPDATE users SET reminders=0 WHERE id=?", {row["user"]});
            db_.write("UPDATE outbox SET done=2 WHERE user=? AND done=0", {row["user"]});
        }
        db_.write("UPDATE outbox SET done=?,attempts=?,due=? WHERE id=?",
                  {terminal ? 2 : 0, attempts,
                   t + std::max(e.retry_after, std::min(300, 1 << std::min(attempts, 8))),
                   row["id"]});
        tx.commit();
    } catch (const std::exception &) {
        Transaction tx(db_);
        db_.write("UPDATE outbox SET done=2 WHERE id=?", {row["id"]});
        say(row["user"], "📈 Не получилось построить график. Попробуй позже — записи в дневнике на месте.");
        tx.commit();
    }
    if (!path.empty())
        std::filesystem::remove_all(std::filesystem::path(path).parent_path());
}
} // namespace nb
