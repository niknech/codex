#include "bot.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <locale>
#include <random>
#ifdef _WIN32
#include <process.h>
#else
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#endif
namespace nb {
std::string make_chart(Database &db, Id user, const std::string &start, const std::string &end) {
    auto dates = days(start, end);
    std::filesystem::path dir;
    std::random_device random;
    for (int i = 0; i < 20; ++i) {
        auto p = std::filesystem::temp_directory_path() /
                 ("nutrition-chart-" + std::to_string(random()));
        if (std::filesystem::create_directory(p)) {
            dir = p;
            break;
        }
    }
    if (dir.empty())
        throw std::runtime_error("Cannot create chart directory");
    try {
        std::filesystem::permissions(dir, std::filesystem::perms::owner_all);
        auto output = (dir / "weight.png").generic_string();
        auto data = (dir / "data.txt").generic_string();
        auto script = (dir / "plot.gp").generic_string();
        // Only generated paths and validated dates/numbers enter the plotting language.
        for (auto &p : {output, data, script})
            if (p.find_first_of("'\r\n") != std::string::npos)
                throw std::runtime_error("Unsupported temporary directory");
        auto weights = db.rows("SELECT day,amount FROM entries WHERE user=? AND kind='weight' AND "
                               "deleted=0 AND day BETWEEN ? AND ? ORDER BY day",
                               {user, start, end});
        std::ofstream out(data);
        out.imbue(std::locale::classic());
        for (auto &day : dates) {
            auto it = std::find_if(weights.begin(), weights.end(),
                                   [&](const Json &w) { return w["day"] == day; });
            out << day << " ";
            if (it == weights.end())
                out << "NaN";
            else
                out << (*it)["amount"].get<double>() / 1000;
            out << "\n";
        }
        out.close();
        std::ofstream gp(script);
        gp.imbue(std::locale::classic());
        gp << "set encoding utf8\nset terminal pngcairo size 1200,720 enhanced "
              "font 'DejaVu Sans,14' background rgb '#F3F7FB'\nset output '"
           << output
           << "'\nset xdata time\nset timefmt '%Y-%m-%d'\nset format x '%d.%m'\n"
              "set key off\nset border 0\nset tics nomirror scale 0 textcolor rgb '#64748B'\n"
              "set grid ytics lc rgb '#DBE5EF' lw 1\nset format y '%.1f'\n"
              "set ylabel 'Вес, кг' textcolor rgb '#64748B' offset 1,0\n"
              "set lmargin at screen 0.10\nset rmargin at screen 0.95\n"
              "set bmargin at screen 0.14\nset tmargin at screen 0.65\n"
              "set object 1 rect from graph 0,0 to graph 1,1 behind "
              "fillcolor rgb '#FFFFFF' fillstyle solid 1.0 noborder\n"
              "set label 1 'ДИНАМИКА ВЕСА' at screen 0.10,0.93 front "
              "font 'DejaVu Sans Bold,15' textcolor rgb '#0F766E'\n"
              "set label 2 '"
           << display_day(start) << " — " << display_day(end)
           << "' at screen 0.10,0.88 front textcolor rgb '#64748B'\n"
              "set label 3 'Точки — твои замеры. Пропуски не заполняются.' at screen 0.10,0.05 "
              "front font 'DejaVu Sans,11' textcolor rgb '#64748B'\nset xrange ['"
           << shift_day(start, -1) << "':'" << shift_day(end, 1) << "']\n";
        // Explicit ticks keep long periods and single-day views readable on a phone.
        gp << "set xtics (";
        auto step = std::max<size_t>(1, (dates.size() - 1 + 5) / 6);
        bool first = true;
        for (size_t i = 0; i < dates.size(); ++i) {
            if (i % step != 0 && i + 1 != dates.size())
                continue;
            if (i + 1 != dates.size() && i > 0 && dates.size() - 1 - i < step / 2)
                continue;
            gp << (first ? "" : ",") << "'" << display_day(dates[i]).substr(0, 5) << "' '"
               << dates[i] << "'";
            first = false;
        }
        gp << ")\n";
        if (weights.empty()) {
            gp << "set yrange [0:1]\nunset ytics\nunset ylabel\n"
                  "set label 4 'Здесь появится твой график' at graph 0.5,0.55 center "
                  "font 'DejaVu Sans Bold,20' textcolor rgb '#334155'\n"
                  "set label 5 'Добавь первое измерение веса через меню' at graph 0.5,0.40 "
                  "center textcolor rgb '#64748B'\nplot 2 with points lc rgb '#FFFFFF'\n";
        } else {
            double low = weights[0]["amount"].get<double>() / 1000, high = low;
            for (const auto &w : weights) {
                auto kg = w["amount"].get<double>() / 1000;
                low = std::min(low, kg);
                high = std::max(high, kg);
            }
            auto pad = std::max(0.5, (high - low) * 0.25);
            auto last = weights.back()["amount"].get<double>() / 1000;
            auto delta = last - weights.front()["amount"].get<double>() / 1000;
            gp << "set yrange [" << low - pad << ":" << high + pad
               << "]\n"
                  "set label 4 '"
               << quantity(last)
               << " кг' at screen 0.10,0.77 "
                  "font 'DejaVu Sans Bold,30' textcolor rgb '#0F172A'\n"
                  "set label 5 'Последний замер · "
               << display_day(weights.back()["day"])
               << "' at screen 0.10,0.71 font 'DejaVu Sans,11' textcolor rgb '#64748B'\n"
                  "set label 6 '"
               << (weights.size() == 1 ? "Первый замер за период"
                                       : "Изменение: " + std::string(delta > 0 ? "+" : "") +
                                             quantity(delta) + " кг")
               << "' at screen 0.59,0.78 font 'DejaVu Sans Bold,17' textcolor rgb '#0F766E'\n"
                  "set label 7 'Замеров: "
               << weights.size()
               << "' at screen 0.59,0.72 textcolor rgb '#64748B'\n"
                  "plot '"
               << data
               << "' using 1:2 with lines lw 3 lc rgb '#14B8A6', '' "
                  "using 1:2 with points pt 7 ps 1.2 lc rgb '#0F766E', '-' "
                  "using 1:2 with points pt 7 ps 1.8 lc rgb '#F59E0B'\n"
               << weights.back()["day"].get<std::string>() << " " << last << "\ne\n";
        }
        gp.close();
#ifdef _WIN32
        auto rc = _spawnlp(_P_WAIT, "gnuplot", "gnuplot", script.c_str(), nullptr);
        if (rc != 0)
            throw std::runtime_error("gnuplot failed");
#else
        pid_t pid;
        char program[] = "/usr/bin/gnuplot";
        char *argv[] = {program, script.data(), nullptr};
        if (posix_spawn(&pid, program, nullptr, nullptr, argv, environ) != 0)
            throw std::runtime_error("gnuplot spawn failed");
        int status = 0;
        if (waitpid(pid, &status, 0) < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
            throw std::runtime_error("gnuplot failed");
#endif
        std::ifstream png(output, std::ios::binary);
        char signature[8]{};
        png.read(signature, 8);
        if (png.gcount() != 8 || std::string(signature, 8) != std::string("\x89PNG\r\n\x1a\n", 8))
            throw std::runtime_error("Invalid PNG");
        return output;
    } catch (...) {
        std::filesystem::remove_all(dir);
        throw;
    }
}
} // namespace nb
