using System;
using System.Globalization;
using System.IO;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace TelegramKeywordCleaner;

public sealed class CsvLogger
{
    private readonly string _path;
    private readonly SemaphoreSlim _lock = new(1, 1);

    public CsvLogger(string path)
    {
        _path = path;
        EnsureHeader();
    }

    public async Task LogAsync(TelegramChatView chat, TelegramMessageView message, string matchedKeyword, string snippet, CancellationToken ct)
    {
        var line = string.Join(',',
            Escape(chat.Id.ToString(CultureInfo.InvariantCulture)),
            Escape(chat.Title),
            Escape(message.Id.ToString(CultureInfo.InvariantCulture)),
            Escape(message.DateUtc.ToString("O", CultureInfo.InvariantCulture)),
            Escape(matchedKeyword),
            Escape(snippet));

        await _lock.WaitAsync(ct);
        try
        {
            await File.AppendAllTextAsync(_path, line + Environment.NewLine, Encoding.UTF8, ct);
        }
        finally
        {
            _lock.Release();
        }
    }

    private void EnsureHeader()
    {
        if (File.Exists(_path))
        {
            return;
        }

        File.WriteAllText(_path, "chat_id,chat_title,message_id,date,matched_keyword,text_snippet" + Environment.NewLine, Encoding.UTF8);
    }

    private static string Escape(string value)
    {
        var escaped = value.Replace("\"", "\"\"");
        return $"\"{escaped}\"";
    }
}
