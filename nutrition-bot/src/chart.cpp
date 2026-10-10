#include "bot.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
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
        gp << "set terminal pngcairo size 1000,500 font 'DejaVu Sans,12'\nset output '" << output
           << "'\nset xdata time\nset timefmt '%Y-%m-%d'\nset format x '%d.%m'\nset xlabel "
              "'Date'\nset ylabel 'kg'\nset grid\nset key off\nset xrange ['"
           << shift_day(start, -1) << "':'" << shift_day(end, 1) << "']\n";
        if (weights.empty())
            gp << "set yrange [0:1]\nplot 0 with lines\n";
        else
            gp << "plot '" << data << "' using 1:2 with linespoints lw 2 pt 7\n";
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
