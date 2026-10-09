#pragma once
#include "database.hpp"
#include <stdexcept>
#include <vector>
namespace nb {
struct InputError : std::runtime_error {
    using std::runtime_error::runtime_error;
};
Id now();
void init_timezones();
void validate_timezone(const std::string &zone);
std::string local_day(Id utc, const std::string &zone);
int local_minute(Id utc, const std::string &zone);
std::string parse_day(const std::string &input);
std::string shift_day(const std::string &day, int offset);
std::vector<std::string> days(const std::string &start, const std::string &end);
double number(std::string text, double low, double high);
int water_ml(std::string text);
Json manual_meal(const std::string &text);
Json validate_estimate(const Json &data);
Json totals(Database &db, Id user, const std::string &day);
Json goal(Database &db, Id user, const std::string &day);
std::string summary(Database &db, Id user, const std::string &day);
std::string estimate_text(const Json &estimate);
} // namespace nb
