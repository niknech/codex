#pragma once
#include "http.hpp"
namespace nb {
std::string barcode(const std::string &text);
Json off_product(const Json &response, const std::string &code);
Json validate_product(Json product);
Json product_estimate(const Json &product, double amount);
Json product_request(const std::string &model, const std::string &kind, const std::string &code);
Json product_response(const Json &response, const std::string &kind, const std::string &code);
} // namespace nb
