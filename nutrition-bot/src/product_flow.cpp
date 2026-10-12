#include "bot.hpp"
#include "products.hpp"
namespace nb {
void Bot::product_screen(Id u, const std::string &state, Json p, Id t) {
    db_.write(
        "INSERT INTO state VALUES('ui_sequence',1) ON CONFLICT(key) DO UPDATE SET value=value+1");
    p["flow"] = db_.rows("SELECT value FROM state WHERE key='ui_sequence'")[0]["value"];
    session(u, state, p, t);
    auto prefix = "ui:" + std::to_string(p["flow"].get<Id>()) + ":";
    std::vector<std::pair<std::string, std::string>> actions;
    std::string text;
    if (state == "product_code")
        text = "🔢 Найдём продукт\n\nВведи цифры штрихкода. Сначала проверю свою базу и Open Food Facts. Если данных нет и поиск включён — отправлю код в OpenAI (один AI-запрос). /ai off — отозвать согласие.";
    else if (state == "product_missing") {
        text = "🔎 Полной карточки нет\n\nВнеси КБЖУ вручную с упаковки или повтори поиск по точному штрихкоду.";
        if (cfg_.ai_enabled && cfg_.ai_web_search)
            actions.emplace_back("🌐 Найти через AI", prefix + "web");
    } else if (state == "product_review") {
        auto product = p["product"];
        text = "📦 " + product["name"].get<std::string>() + "\n\nНа 100 " +
               (product["unit"] == "ml" ? "мл" : "г") + "\n🔥 " + quantity(product["kcal"]) +
               " ккал\n🥚 " + quantity(product["protein"]) + " г белка\n🥑 " +
               quantity(product["fat"]) + " г жира\n🍞 " + quantity(product["carbs"]) +
               " г углеводов\n\nИсточник: " + product.value("source", "этикетка") + "\n" +
               product.value("url", "") +
               "\n\nСверь данные с упаковкой.";
        actions.emplace_back("✅ Верно, выбрать порцию", prefix + "accept");
    } else if (state == "product_amount") {
        text = "⚖️ Сколько съедено?\nВведи количество в " +
               std::string(p["product"]["unit"] == "ml" ? "мл" : "г") + ".";
        actions.emplace_back("100", prefix + "amount100");
        if (!p["product"]["package"].is_null())
            actions.emplace_back("Вся упаковка (" + quantity(p["product"]["package"]) + ")",
                                 prefix + "package");
    } else
        text = "⏳ Проверяю продукт…\n/cancel — отменить.";
    if (state == "product_missing") {
        if (!cfg_.ai_enabled) text = "🔎 Полной карточки в базе нет\nAI отключён. Внеси КБЖУ вручную с упаковки.";
        actions.emplace_back("✍️ Ввести вручную", prefix + "manual");
    }
    actions.emplace_back("✖️ Отмена", prefix + "cancel");
    say(u, text, buttons(actions));
}
void Bot::product_queue(Id u, const std::string &kind, Json p, Id t) {
    bool paid = kind != "lookup";
    if (paid) {
        if (!cfg_.ai_enabled || (kind == "web" && !cfg_.ai_web_search))
            throw InputError("Этот режим AI отключён.");
        check_ai_limit(u, t);
        db_.write("UPDATE users SET ai_consent=1 WHERE id=?", {u});
    }
    product_screen(u, "product_wait", p, t);
    auto session = db_.rows("SELECT payload FROM sessions WHERE user=?", {u})[0];
    p = Json::parse(session["payload"].get<std::string>());
    db_.write("INSERT INTO product_jobs(user,flow,kind,payload,day,paid) VALUES(?,?,?,?,?,?)",
              {u, p["flow"], kind, p.dump(), local_day(t, "Etc/UTC"), paid ? 1 : 0});
}
bool Bot::product_message(Id u, const std::string &text, Id t) {
    auto rows = db_.rows("SELECT * FROM sessions WHERE user=? AND expires>=?", {u, t});
    if (rows.empty())
        return false;
    std::string state = rows[0]["state"];
    if (state.rfind("product_", 0) != 0)
        return false;
    auto p = Json::parse(rows[0]["payload"].get<std::string>());
    if (state == "product_code") {
        p["code"] = barcode(text);
        auto cache =
            db_.rows("SELECT data FROM products WHERE user=? AND barcode=?", {u, p["code"]});
        if (!cache.empty()) {
            p["product"] = validate_product(Json::parse(cache[0]["data"].get<std::string>()));
            product_screen(u, "product_review", p, t);
        } else
            product_queue(u, "lookup", p, t);
    } else if (state == "product_amount") {
        auto amount = number(text, 0.1, 20000);
        auto estimate = product_estimate(p["product"], amount);
        auto id = draft(u, "meal", p["day"], p["product"]["name"], estimate, t);
        db_.write("UPDATE drafts SET category=? WHERE id=?", {p["category"], id});
        db_.write("DELETE FROM sessions WHERE user=?", {u});
        offer(u, id);
    } else
        throw InputError("Используй кнопки последней карточки или /cancel.");
    return true;
}
bool Bot::product_callback(Id u, const std::string &state, const std::string &value, Json p, Id t) {
    if (state == "product_missing" && value == "manual") {
        screen(u, "meal_name", p, t);
        return true;
    }
    if (state.rfind("product_", 0) != 0)
        return false;
    if (state == "product_missing" && value == "web") {
        product_queue(u, "web", p, t);
        return true;
    }
    if (state == "product_review" && value == "accept") {
        auto product = validate_product(p["product"]);
        db_.write("INSERT INTO products VALUES(?,?,?,?) ON CONFLICT(user,barcode) DO UPDATE SET "
                  "data=excluded.data,updated=excluded.updated",
                  {u, product["barcode"], product.dump(), t});
        product_screen(u, "product_amount", p, t);
        return true;
    }
    if (state == "product_amount" && (value == "amount100" || value == "package")) {
        auto n = value == "amount100" ? Json(100) : p["product"]["package"];
        product_message(u, n.dump(), t);
        return true;
    }
    throw InputError("Используй кнопки последней карточки.");
}
bool Bot::product_one(Id t, const AiTransport &ai,
                      const std::function<Json(const std::string &)> &lookup) {
    Json job, p;
    {
        Transaction tx(db_);
        auto rows =
            db_.rows("SELECT * FROM product_jobs WHERE status='pending' ORDER BY id LIMIT 1");
        if (rows.empty()) {
            tx.commit();
            return false;
        }
        job = rows[0];
        p = Json::parse(job["payload"].get<std::string>());
        auto s = db_.rows(
            "SELECT payload FROM sessions WHERE user=? AND state='product_wait' AND expires>=?",
            {job["user"], t});
        bool valid =
            (job["kind"] == "lookup" || job["kind"] == "web") && !s.empty() && Json::parse(s[0]["payload"].get<std::string>())["flow"] == job["flow"];
        if (job["paid"] == 1)
            valid = valid && cfg_.ai_enabled && (job["kind"] != "web" || cfg_.ai_web_search) &&
                    db_.rows("SELECT ai_consent FROM users WHERE id=?",
                             {job["user"]})[0]["ai_consent"] == 1;
        db_.write("UPDATE product_jobs SET status=?,payload=CASE WHEN ? THEN payload ELSE '{}' END "
                  "WHERE id=?",
                  {valid ? "running" : "cancelled", valid ? 1 : 0, job["id"]});
        tx.commit();
        if (!valid)
            return true;
    }
    Json result;
    std::string failure;
    auto kind = job["kind"].get<std::string>();
    auto code = p.value("code", "");
    try {
        if (kind == "lookup")
            result = off_product(lookup(code), code);
        else
            result = product_response(
                ai(product_request(cfg_.model, kind, code)),
                kind, code);
    } catch (const std::exception &) {
        failure = "Не удалось проверить продукт. Автоповтора платного запроса нет.";
    }
    Transaction tx(db_);
    db_.write("UPDATE product_jobs SET status=?,payload='{}' WHERE id=?",
              {failure.empty() ? "done" : "failed", job["id"]});
    auto rows = db_.rows(
        "SELECT payload FROM sessions WHERE user=? AND state='product_wait' AND expires>=?",
        {job["user"], now()});
    if (!rows.empty() &&
        Json::parse(rows[0]["payload"].get<std::string>())["flow"] == job["flow"]) {
        p.erase("file");
        if (!failure.empty())
            say(job["user"], failure);
        if (result.is_null()) {
            if (kind == "lookup" && failure.empty() && cfg_.ai_enabled && cfg_.ai_web_search) {
                try {
                    product_queue(job["user"], "web", p, t);
                } catch (const InputError &e) {
                    say(job["user"], e.what());
                    product_screen(job["user"], "product_missing", p, t);
                }
            } else
                product_screen(job["user"], "product_missing", p, t);
        } else {
            p["product"] = result;
            product_screen(job["user"], "product_review", p, t);
        }
    }
    tx.commit();
    return true;
}
} // namespace nb
