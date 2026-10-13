#pragma once
#include "database.hpp"
#include <functional>
namespace nb {
struct ApiError : std::runtime_error {
    int status, retry_after;
    ApiError(int code, int retry = 0)
        : std::runtime_error("Remote API error " + std::to_string(code)), status(code),
          retry_after(retry) {}
};
class CurlRuntime {
  public:
    CurlRuntime();
    ~CurlRuntime();
    CurlRuntime(const CurlRuntime &) = delete;
};
Json post(const std::string &url, const Json &body, const std::string &key = "", int timeout = 35);
Json lookup_product(const std::string &code);
class Telegram {
    std::string base_;

  public:
    explicit Telegram(const std::string &token)
        : base_("https://api.telegram.org/bot" + token + "/") {}
    Json call(const std::string &method, const Json &payload);
    void photo(Id chat, const std::string &path);
    std::string photo_data(const std::string &file_id);
};
using AiTransport = std::function<Json(const Json &)>;
Json ai_request(const std::string &model, const std::string &description, bool web_search = false);
Json ai_response(const Json &response);
} // namespace nb
