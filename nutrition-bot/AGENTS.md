# Working on nutrition-bot

The owner understands C and wants to direct development in natural language, review behavior,
and learn the C++ implementation without writing it personally.

- Explain the changed behavior, a short C-to-C++ connection, and the test outcome in Russian.
- Keep the bot's user-facing text Russian. Use C++20, SQLite, libcurl, date/tz and gnuplot.
- Never commit tokens, personal database contents, logs, or local configuration.
- Do not deploy to the home server without an explicit deployment request.
- Keep database effects and inbox acknowledgement in one transaction; never do HTTP inside it.
- Check owner, expiry, status and version for draft actions. Never let AI confirm meals.
- Add meaningful regression coverage for changes to quantities, ownership, dates, or persistence.
- Run CMake build and CTest; do not call a change tested if only source inspection was possible.
- For migrations, keep compatibility or document backup and restore requirements.
- Work on a feature branch and open a PR. Do not merge your own PR unless explicitly asked.
