#pragma once
#include "config.hpp"
#include "domain.hpp"
#include "http.hpp"
namespace nb {
class Bot {
    Database &db_;
    const Config &cfg_;
    void message(Id user, Id chat, const std::string &text, Id time);
    void callback(Id user, const std::string &action, Id time);
    void session(Id user, const std::string &state, const Json &payload, Id time);
    void offer(Id user, Id draft);
    Id draft(Id user, const std::string &kind, const std::string &day,
             const std::string &description, const Json &estimate, Id time);
    void queue_ai(Id user, Id draft, Id time);
    void check_ai_limit(Id user, Id time);
    void period(Id user, const std::string &start, const std::string &end, int page, bool chart);
    void screen(Id user, const std::string &state, Json payload, Id time);
    void preferences(Id user, const std::string &state, Json payload, Id time);
    bool preference_message(Id user, const std::string &text, Id time);
    bool preference_callback(Id user, const std::string &state, const std::string &action, Json payload, Id time);
    bool ui_message(Id user, const std::string &text, Id time);
    void ui_callback(Id user, const std::string &action, Id time);
    void finish_meal(Id user, Json payload, Id time);
    void product_screen(Id user, const std::string &state, Json payload, Id time);
    bool product_message(Id user, const std::string &text, Id time);
    bool product_callback(Id user, const std::string &state, const std::string &action, Json payload, Id time);
    void product_queue(Id user, const std::string &kind, Json payload, Id time);

  public:
    Bot(Database &db, const Config &cfg) : db_(db), cfg_(cfg) {}
    void say(Id user, const std::string &text, const Json &markup = Json());
    void ingest(const Json &updates);
    bool process_one(Id time);
    void reminders(Id time);
    bool ai_one(Id time, const AiTransport &transport);
    bool product_one(Id time, const AiTransport &ai, const std::function<Json(const std::string &)> &lookup);
    void deliver_one(Telegram &telegram, Id time);
};
Json menu();
Json buttons(const std::vector<std::pair<std::string, std::string>> &values);
std::string make_chart(Database &db, Id user, const std::string &start, const std::string &end);
} // namespace nb
