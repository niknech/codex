#include "http.hpp"
#include "products.hpp"
#include <curl/curl.h>
#include <memory>
namespace nb {
CurlRuntime::CurlRuntime() {
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
        throw std::runtime_error("Curl init failed");
}
CurlRuntime::~CurlRuntime() {
    curl_global_cleanup();
}
static size_t receive(char *p, size_t s, size_t n, void *ctx) noexcept {
    try {
        auto &b = *static_cast<std::string *>(ctx);
        if (n > 4 * 1024 * 1024 / (s ? s : 1) || s * n > 4 * 1024 * 1024 - b.size())
            return 0;
        b.append(p, s * n);
        return s * n;
    } catch (...) {
        return 0;
    }
}
using Handle = std::unique_ptr<CURL, decltype(&curl_easy_cleanup)>;
static Handle handle(const std::string &url, std::string &body, int timeout) {
    Handle h(curl_easy_init(), curl_easy_cleanup);
    if (!h)
        throw std::runtime_error("Curl handle failed");
    curl_easy_setopt(h.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(h.get(), CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(h.get(), CURLOPT_TIMEOUT, static_cast<long>(timeout));
    curl_easy_setopt(h.get(), CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(h.get(), CURLOPT_WRITEFUNCTION, receive);
    curl_easy_setopt(h.get(), CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(h.get(), CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(h.get(), CURLOPT_SSL_VERIFYHOST, 2L);
    return h;
}
static Json finish(CURL *h, std::string &body) {
    auto rc = curl_easy_perform(h);
    if (rc != CURLE_OK)
        throw ApiError(0);
    long status = 0;
    curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &status);
    Json j = Json::parse(body, nullptr, false);
    if (status < 200 || status >= 300) {
        int retry = 0;
        if (j.is_object() && j.contains("parameters"))
            retry = j["parameters"].value("retry_after", 0);
        throw ApiError(static_cast<int>(status), retry);
    }
    if (j.is_discarded())
        throw ApiError(502);
    return j;
}
Json post(const std::string &url, const Json &data, const std::string &key, int timeout) {
    std::string body, payload = data.dump();
    auto h = handle(url, body, timeout);
    curl_slist *raw = curl_slist_append(nullptr, "Content-Type: application/json");
    if (!key.empty())
        raw = curl_slist_append(raw, ("Authorization: Bearer " + key).c_str());
    std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> headers(raw, curl_slist_free_all);
    curl_easy_setopt(h.get(), CURLOPT_HTTPHEADER, raw);
    curl_easy_setopt(h.get(), CURLOPT_POSTFIELDS, payload.c_str());
    curl_easy_setopt(h.get(), CURLOPT_POSTFIELDSIZE, static_cast<long>(payload.size()));
    return finish(h.get(), body);
}
Json Telegram::call(const std::string &method, const Json &payload) {
    auto j = post(base_ + method, payload);
    if (!j.value("ok", false))
        throw ApiError(j.value("error_code", 502));
    return j.at("result");
}
Json lookup_product(const std::string &raw) {
    auto code=barcode(raw);
    std::string body;
    auto h=handle("https://world.openfoodfacts.org/api/v3/product/"+code+
        "?fields=code,product_name,product_name_ru,nutriments,product_quantity,product_quantity_unit,obsolete&lc=ru",body,20);
    curl_easy_setopt(h.get(),CURLOPT_USERAGENT,"NutritionBot/0.2 (https://github.com/niknech/codex)");
    try { return finish(h.get(),body); }
    catch (const ApiError &e) { if (e.status==404) return Json::object(); throw; }
}
std::string Telegram::photo_data(const std::string &file_id) {
    auto file=call("getFile",{{"file_id",file_id}});
    auto path=file.at("file_path").get<std::string>();
    if (path.find("..")!=std::string::npos || path.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-/.")!=std::string::npos)
        throw ApiError(400);
    std::string body;
    auto url=base_;
    url.insert(std::string("https://api.telegram.org/").size(),"file/");
    auto h=handle(url+path,body,20);
    if (curl_easy_perform(h.get())!=CURLE_OK) throw ApiError(0);
    long status=0; curl_easy_getinfo(h.get(),CURLINFO_RESPONSE_CODE,&status);
    if (status!=200 || body.size()<3 || static_cast<unsigned char>(body[0])!=0xff || static_cast<unsigned char>(body[1])!=0xd8)
        throw ApiError(400);
    static const char chars[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string encoded;
    unsigned int buffer=0; int bits=0;
    for (unsigned char c:body) { buffer=(buffer<<8)|c; bits+=8; while(bits>=6) { bits-=6; encoded+=chars[(buffer>>bits)&63]; } }
    if (bits) encoded+=chars[(buffer<<(6-bits))&63];
    while(encoded.size()%4) encoded+='=';
    return "data:image/jpeg;base64,"+encoded;
}
void Telegram::photo(Id chat, const std::string &path) {
    std::string body;
    auto h = handle(base_ + "sendPhoto", body, 30);
    std::unique_ptr<curl_mime, decltype(&curl_mime_free)> mime(curl_mime_init(h.get()),
                                                               curl_mime_free);
    auto part = curl_mime_addpart(mime.get());
    curl_mime_name(part, "chat_id");
    auto id = std::to_string(chat);
    curl_mime_data(part, id.c_str(), CURL_ZERO_TERMINATED);
    part = curl_mime_addpart(mime.get());
    curl_mime_name(part, "photo");
    curl_mime_filedata(part, path.c_str());
    curl_easy_setopt(h.get(), CURLOPT_MIMEPOST, mime.get());
    auto j = finish(h.get(), body);
    if (!j.value("ok", false))
        throw ApiError(j.value("error_code", 502));
}
} // namespace nb
