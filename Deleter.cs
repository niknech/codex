using System;
using System.Threading;
using System.Threading.Tasks;

namespace TelegramKeywordCleaner;

public sealed class Deleter
{
    private readonly TelegramClientService _client;
    private readonly CsvLogger _csvLogger;
    private readonly Action<string> _log;

    public Deleter(TelegramClientService client, CsvLogger csvLogger, Action<string> log)
    {
        _client = client;
        _csvLogger = csvLogger;
        _log = log;
    }

    public async Task HandleMatchAsync(TelegramChatView chat, TelegramMessageView message, string keyword, ScanOptions options, CancellationToken ct)
    {
        var snippet = message.TextForMatch.Length > 120
            ? message.TextForMatch[..120]
            : message.TextForMatch;

        if (options.DryRun)
        {
            _log($"[DRY-RUN] Совпадение в {chat.Title}: msg={message.Id}, keyword={keyword}, text={snippet}");
            await _csvLogger.LogAsync(chat, message, keyword, snippet, ct);
            return;
        }

        try
        {
            await _client.DeleteMessagesAsync(chat.Id, message.Id, options.DeleteForAll, ct);
            await _csvLogger.LogAsync(chat, message, keyword, snippet, ct);
            _log($"Удалено сообщение {message.Id} в чате {chat.Title}.");
            await Task.Delay(150, ct);
        }
        catch (Exception ex)
        {
            _log($"Пропуск удаления {message.Id}: {ex.Message}");
        }
    }
}
