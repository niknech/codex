#include "config.hpp"
#include "domain.hpp"
#include <cstdlib>
#include <fstream>
#include <regex>
#include <sstream>
namespace nb {
static std::string env(const char *key, const std::string &fallback = "") {
    auto p = std::getenv(key);
    return p ? p : fallback;
}
static bool flag(const char *key) {
    auto v = env(key, "false");
    if (v != "true" && v != "false")
        throw std::runtime_error("Invalid boolean configuration");
    return v == "true";
}
static std::string secret(const char *key, const char *name) {
    auto path = env(key);
    if (path.empty() && !env("CREDENTIALS_DIRECTORY").empty())
        path = env("CREDENTIALS_DIRECTORY") + "/" + name;
    std::ifstream in(path);
    std::string s;
    if (!std::getline(in, s))
        throw std::runtime_error("Cannot read credential file");
    if (!s.empty() && s.back() == '\r')
        s.pop_back();
    if (s.empty() || s.find_first_of(" \t\r\n") != std::string::npos)
        throw std::runtime_error("Invalid credential file");
    return s;
}
Config Config::load() {
    Config c;
    c.db = env("BOT_DB_PATH", c.db);
    c.timezone = env("BOT_DEFAULT_TIMEZONE", c.timezone);
    validate_timezone(c.timezone);
    c.public_registration = flag("BOT_PUBLIC_REGISTRATION");
    std::istringstream ids(env("BOT_ALLOWED_USER_IDS"));
    std::string id;
    while (std::getline(ids, id, ',')) {
        if (!std::regex_match(id, std::regex("[1-9][0-9]{0,18}")))
            throw std::runtime_error("Invalid allowed user ID");
        c.allowed.insert(std::stoll(id));
    }
    if (c.allowed.empty() && !c.public_registration)
        throw std::runtime_error("Set BOT_ALLOWED_USER_IDS or BOT_PUBLIC_REGISTRATION=true");
    c.token = secret("BOT_TOKEN_FILE", "telegram_token");
    if (!std::regex_match(c.token, std::regex("[0-9]+:[A-Za-z0-9_-]+")))
        throw std::runtime_error("Invalid Telegram token format");
    c.ai_enabled = flag("AI_ENABLED");
    if (c.ai_enabled) {
        if (env("AI_PROVIDER", "openai") != "openai")
            throw std::runtime_error("Supported AI provider: openai (Responses compatible)");
        c.key = secret("AI_API_KEY_FILE", "ai_api_key");
        c.model = env("AI_MODEL");
        if (c.model.empty())
            throw std::runtime_error("AI_MODEL is required");
        c.ai_url = env("AI_RESPONSES_URL", c.ai_url);
        if (c.ai_url.rfind("https://", 0) != 0)
            throw std::runtime_error("AI URL must use HTTPS");
        c.ai_daily_user = static_cast<int>(number(env("AI_DAILY_USER_LIMIT", "10"), 1, 1000));
        c.ai_daily_global = static_cast<int>(number(env("AI_DAILY_GLOBAL_LIMIT", "50"), 1, 10000));
    }
    return c;
}
} // namespace nb
