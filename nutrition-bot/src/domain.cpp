#include "domain.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <date/tz.h>
#include <iomanip>
#include <regex>
#include <sstream>
namespace nb {
Id now() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}
void init_timezones() {
#if !USE_OS_TZDB
    if (auto path = std::getenv("BOT_TZDATA_DIR"))
        date::set_install(path);
#endif
}
void validate_timezone(const std::string &z) {
    try {
        date::locate_zone(z);
    } catch (...) {
        throw InputError("Неизвестный часовой пояс. Пример: Europe/Moscow");
    }
}
static auto local(Id t, const std::string &z) {
    return date::locate_zone(z)->to_local(date::sys_seconds{std::chrono::seconds{t}});
}
std::string local_day(Id t, const std::string &z) {
    return date::format("%F", date::floor<date::days>(local(t, z)));
}
int local_minute(Id t, const std::string &z) {
    auto l = local(t, z);
    return static_cast<int>(
        std::chrono::duration_cast<std::chrono::minutes>(l - date::floor<date::days>(l)).count());
}
static date::sys_days day_value(const std::string &text) {
    date::year_month_day d;
    std::istringstream in(text);
    in >> date::parse("%F", d);
    if (in.fail() || !d.ok() || in.peek() != EOF)
        throw InputError("Дата: ДД.ММ.ГГГГ или ГГГГ-ММ-ДД");
    return date::sys_days{d};
}
std::string parse_day(const std::string &s) {
    std::string v = s;
    if (std::regex_match(s, std::regex(R"(\d{2}\.\d{2}\.\d{4})")))
        v = s.substr(6, 4) + "-" + s.substr(3, 2) + "-" + s.substr(0, 2);
    if (!std::regex_match(v, std::regex(R"(\d{4}-\d{2}-\d{2})")))
        throw InputError("Дата: ДД.ММ.ГГГГ или ГГГГ-ММ-ДД");
    return date::format("%F", day_value(v));
}
std::string shift_day(const std::string &d, int offset) {
    return date::format("%F", day_value(d) + date::days{offset});
}
std::vector<std::string> days(const std::string &a, const std::string &b) {
    auto start = day_value(a), end = day_value(b);
    auto n = (end - start).count();
    if (n < 0 || n > 365)
        throw InputError("Период: от 1 до 366 дней, начало не позже конца.");
    std::vector<std::string> r;
    for (auto d = start; d <= end; d += date::days{1})
        r.push_back(date::format("%F", d));
    return r;
}
double number(std::string text, double low, double high) {
    std::replace(text.begin(), text.end(), ',', '.');
    if (!std::regex_match(text, std::regex(R"(\s*\d+(\.\d+)?\s*)")))
        throw InputError("Введите положительное число без лишнего текста.");
    double v;
    try {
        v = std::stod(text);
    } catch (...) {
        throw InputError("Число слишком велико.");
    }
    if (!std::isfinite(v) || v < low || v > high)
        throw InputError("Число вне допустимого диапазона.");
    return v;
}
int water_ml(std::string text) {
    std::smatch m;
    std::regex re(R"(^\s*(\d+(?:[.,]\d+)?)\s*(мл|ml|л|l)?\s*$)");
    if (!std::regex_match(text, m, re))
        throw InputError("Вода: 250, 250 мл или 0,5 л.");
    double v = number(m[1], 0.001, 10000);
    if (m[2] == "л" || m[2] == "l")
        v *= 1000;
    if (v < 1 || v > 10000 || std::abs(v - std::round(v)) > 0.00001)
        throw InputError("Вода: 1–10000 целых мл.");
    return static_cast<int>(std::round(v));
}
Json manual_meal(const std::string &text) {
    auto split = text.find(';');
    if (split == std::string::npos)
        throw InputError(
            "Формат: ккал белки жиры углеводы ; название. Например: 420 20 18 45 ; обед");
    std::istringstream in(text.substr(0, split));
    std::string k, p, f, c, extra;
    if (!(in >> k >> p >> f >> c) || (in >> extra))
        throw InputError("Нужны четыре числа: ккал Б Ж У ; название");
    auto desc = text.substr(split + 1);
    if (desc.find_first_not_of(" \t") == std::string::npos || desc.size() > 200)
        throw InputError("Название: 1–200 байт.");
    return {{"status", "estimate"},
            {"question", nullptr},
            {"assumptions", Json::array()},
            {"items", Json::array({{{"name", desc},
                                    {"portion_g", nullptr},
                                    {"kcal", number(k, 0, 20000)},
                                    {"protein_g", number(p, 0, 2000)},
                                    {"fat_g", number(f, 0, 2000)},
                                    {"carbs_g", number(c, 0, 2000)},
                                    {"basis", "manual"}}})}};
}
Json validate_estimate(const Json &d) {
    try {
        auto status = d.at("status").get<std::string>();
        if (status != "estimate" && status != "needs_clarification" && status != "not_food")
            throw InputError("AI: неизвестный статус.");
        if (!d.at("question").is_null() &&
            (!d["question"].is_string() || d["question"].get<std::string>().size() > 1000))
            throw InputError("AI: неверный вопрос.");
        if (status == "needs_clarification" &&
            (!d["question"].is_string() || d["question"].get<std::string>().empty()))
            throw InputError("AI: нет вопроса.");
        const auto &items = d.at("items");
        const auto &assumptions = d.at("assumptions");
        if (!items.is_array() || items.size() > 20 || (status == "estimate" && items.empty()) ||
            !assumptions.is_array() || assumptions.size() > 20)
            throw InputError("AI: неверный список.");
        for (auto &a : assumptions)
            if (!a.is_string() || a.get<std::string>().size() > 500)
                throw InputError("AI: неверное допущение.");
        for (auto &i : items) {
            if (!i.at("name").is_string() || i["name"].get<std::string>().empty() ||
                i["name"].get<std::string>().size() > 200)
                throw InputError("AI: неверное название.");
            for (auto field : {"kcal", "protein_g", "fat_g", "carbs_g"}) {
                if (!i.at(field).is_number())
                    throw InputError("AI: неверное число.");
                auto v = i[field].get<double>();
                if (!std::isfinite(v) || v < 0 || v > (std::string(field) == "kcal" ? 20000 : 2000))
                    throw InputError("AI: неверное число.");
            }
            if (!i.at("portion_g").is_null()) {
                if (!i["portion_g"].is_number())
                    throw InputError("AI: неверная порция.");
                auto v = i["portion_g"].get<double>();
                if (!std::isfinite(v) || v <= 0 || v > 20000)
                    throw InputError("AI: неверная порция.");
            }
            auto basis = i.at("basis").get<std::string>();
            if (basis != "estimate" && basis != "manual")
                throw InputError("AI: неверный источник.");
        }
        return d;
    } catch (const Json::exception &) {
        throw InputError("AI вернул неверную структуру.");
    }
}
Json totals(Database &db, Id u, const std::string &day) {
    return db.rows(
        "SELECT COUNT(*) AS count, COALESCE(SUM(CASE WHEN kind='water' THEN amount ELSE 0 END),0) "
        "AS water, MAX(CASE WHEN kind='weight' THEN amount END) AS weight, COALESCE(SUM(kcal),0) "
        "AS kcal, COALESCE(SUM(protein),0) AS protein, COALESCE(SUM(fat),0) AS fat, "
        "COALESCE(SUM(carbs),0) AS carbs FROM entries WHERE user=? AND day=? AND deleted=0",
        {u, day})[0];
}
Json goal(Database &db, Id u, const std::string &d) {
    auto r =
        db.rows("SELECT * FROM goals WHERE user=? AND day<=? ORDER BY day DESC LIMIT 1", {u, d});
    return r.empty() ? Json() : r[0];
}
std::string quantity(double value) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(1) << value;
    auto text = out.str();
    if (text.size() > 2 && text.substr(text.size() - 2) == ".0")
        text.resize(text.size() - 2);
    std::replace(text.begin(), text.end(), '.', ',');
    return text;
}
std::string display_day(const std::string &day) {
    return day.substr(8, 2) + "." + day.substr(5, 2) + "." + day.substr(0, 4);
}
std::string meal_label(const std::string &category) {
    if (category == "breakfast")
        return "🌅 Завтрак";
    if (category == "lunch")
        return "☀️ Обед";
    if (category == "dinner")
        return "🌙 Ужин";
    return "🍽 Без рубрики";
}
std::string summary(Database &db, Id u, const std::string &d) {
    auto t = totals(db, u, d), g = goal(db, u, d);
    std::string out = "📅 " + display_day(d) + "\n";
    if (t["count"] == 0)
        return out + "\n📝 Пока нет записей за этот день.";
    auto nutrient = [&](const std::string &icon, const std::string &title, const std::string &key,
                        const std::string &unit) {
        out += "\n" + icon + " " + title + ": " + quantity(t[key]);
        if (!g.is_null())
            out += " / " + quantity(g[key]);
        out += " " + unit;
    };
    if (!g.is_null())
        out += "Записано / личная норма\n";
    nutrient("🔥", "Калории", "kcal", "ккал");
    nutrient("🥚", "Белки", "protein", "г");
    nutrient("🥑", "Жиры", "fat", "г");
    nutrient("🍞", "Углеводы", "carbs", "г");
    nutrient("💧", "Вода", "water", "мл");
    out +=
        "\n⚖️ Вес: " + (t["weight"].is_null() ? std::string("нет измерения")
                                             : quantity(t["weight"].get<double>() / 1000) + " кг");
    out += "\n\n🍽 Приёмы пищи";
    for (auto category : {"breakfast", "lunch", "dinner", "unspecified"}) {
        auto r = db.rows("SELECT COUNT(*) AS n,COALESCE(SUM(kcal),0) AS k FROM entries WHERE "
                         "user=? AND day=? AND kind='meal' AND category=? AND deleted=0",
                         {u, d, category})[0];
        if (std::string(category) == "unspecified" && r["n"] == 0)
            continue;
        out += "\n" + meal_label(category) + ": " +
               (r["n"] == 0 ? std::string("нет записей") : quantity(r["k"]) + " ккал");
    }
    return out;
}
std::string estimate_text(const Json &d) {
    if (d["status"] == "needs_clarification")
        return d["question"].get<std::string>();
    if (d["status"] == "not_food")
        return "Не удалось распознать еду. Уточните описание или внесите БЖУ вручную.";
    double k = 0, p = 0, f = 0, c = 0;
    for (auto &i : d["items"]) {
        k += i["kcal"].get<double>();
        p += i["protein_g"].get<double>();
        f += i["fat_g"].get<double>();
        c += i["carbs_g"].get<double>();
    }
    std::ostringstream out;
    out << std::fixed << std::setprecision(1) << "📝 Проверь перед сохранением\n\n"
        << "🔥 Калории: " << quantity(k) << " ккал\n🥚 Белки: " << quantity(p)
        << " г\n🥑 Жиры: " << quantity(f) << " г\n🍞 Углеводы: " << quantity(c) << " г\n\n";
    for (auto &i : d["items"]) {
        auto line = i["name"].get<std::string>() + "\n";
        if (out.str().size() + line.size() < 2800)
            out << line;
    }
    for (auto &a : d["assumptions"]) {
        auto line = "Принято: " + a.get<std::string>() + "\n";
        if (out.str().size() + line.size() < 3200)
            out << line;
    }
    return out.str();
}
} // namespace nb
