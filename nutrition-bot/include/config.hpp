#pragma once
#include "database.hpp"
#include <set>
namespace nb {
struct Config {
    std::string db = "data/bot.sqlite3", timezone = "Europe/Moscow", token, key, model;
    std::string ai_url = "https://api.openai.com/v1/responses";
    std::set<Id> allowed;
    bool public_registration = false, ai_enabled = false, ai_web_search = false;
    int ai_daily_user = 10, ai_daily_global = 50;
    static Config load();
};
} // namespace nb
