#include "bot.hpp"
#include <algorithm>
#include <atomic>
#include <csignal>
#include <filesystem>
#include <iostream>
#include <thread>
#ifndef _WIN32
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif
namespace {
static_assert(std::atomic<bool>::is_always_lock_free);
std::atomic<bool> stopping = false;
void stop(int) {
    stopping = 1;
}
void pause(int seconds) {
    for (int i = 0; i < seconds * 10 && !stopping; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
}
} // namespace
int main(int argc, char **argv) {
    using namespace nb;
    try {
        std::string mode = argc == 2 ? argv[1] : "";
        if (mode == "--version") {
            std::cout << "nutrition-bot 0.1.0\n";
            return 0;
        }
        if (mode != "--check-config" && mode != "--migrate" && mode != "--self-test" &&
            mode != "--run")
            throw std::runtime_error(
                "Usage: nutrition-bot --version|--check-config|--migrate|--self-test|--run");
        init_timezones();
        auto cfg = Config::load();
        if (mode == "--check-config") {
            std::cout << "Configuration OK\n";
            return 0;
        }
        auto parent = std::filesystem::path(cfg.db).parent_path();
        if (!parent.empty())
            std::filesystem::create_directories(parent);
#ifndef _WIN32
        int lock = open((cfg.db + ".lock").c_str(), O_CREAT | O_RDWR, 0600);
        if (lock < 0 || flock(lock, LOCK_EX | LOCK_NB) != 0)
            throw std::runtime_error("Another process is using this database");
#endif
        Database db(cfg.db);
        db.migrate();
        if (mode == "--migrate") {
            std::cout << "Schema OK\n";
            return 0;
        }
        CurlRuntime runtime;
        Telegram telegram(cfg.token);
        if (mode == "--self-test") {
            telegram.call("getMe", Json::object());
            db.rows("SELECT 1");
            auto day = local_day(now(), cfg.timezone);
            auto png = make_chart(db, 0, shift_day(day, -6), day);
            std::filesystem::remove_all(std::filesystem::path(png).parent_path());
            std::cout << "Telegram, SQLite, PNG OK\n";
            return 0;
        }
        std::signal(SIGINT, stop);
        std::signal(SIGTERM, stop);
        // A crash during a paid request has an ambiguous outcome. Do not retry automatically.
        db.exec("UPDATE jobs SET status='failed' WHERE status='running'");
        std::atomic<bool> worker_failed = false;
        auto worker = [&](bool ai) {
            try {
                Database connection(cfg.db);
                Bot bot(connection, cfg);
                Telegram tg(cfg.token);
                while (!stopping) {
                    if (ai) {
                        if (!bot.ai_one(now(), [&](const Json &request) {
                                return post(cfg.ai_url, request, cfg.key, 25);
                            }))
                            pause(1);
                    } else {
                        bot.deliver_one(tg, now());
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    }
                }
            } catch (const std::exception &) {
                std::cerr << "Background worker failed\n";
                worker_failed = true;
                stopping = 1;
            }
        };
        std::jthread delivery([&] { worker(false); });
        std::jthread ai;
        try {
            if (cfg.ai_enabled)
                ai = std::jthread([&] { worker(true); });
        } catch (...) {
            stopping = true;
            throw;
        }
        Bot bot(db, cfg);
        Id last_reminders = 0;
        int failures = 0;
        try {
            while (!stopping) {
                for (int n = 0; n < 100 && !stopping && bot.process_one(now()); ++n) {
                }
                if (now() - last_reminders >= 30) {
                    bot.reminders(now());
                    last_reminders = now();
                }
                try {
                    auto offset = db.rows("SELECT value FROM state WHERE key='offset'")[0]["value"];
                    auto updates = telegram.call(
                        "getUpdates", {{"offset", offset},
                                       {"timeout", 10},
                                       {"limit", 100},
                                       {"allowed_updates", {"message", "callback_query"}}});
                    bot.ingest(updates);
                    failures = 0;
                } catch (const ApiError &e) {
                    if (e.status == 401 || e.status == 409)
                        throw std::runtime_error(
                            "Telegram credentials invalid or another poller/webhook is active");
                    std::cerr << "Polling unavailable, status " << e.status << "\n";
                    pause(std::max(e.retry_after, std::min(30, 1 << std::min(++failures, 5))));
                }
            }
        } catch (...) {
            stopping = 1;
            throw;
        }
        return worker_failed ? 1 : 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
