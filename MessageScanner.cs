using System;
using System.Collections.Generic;
using System.Linq;

namespace TelegramKeywordCleaner;

public sealed class MessageScanner
{
    private readonly IReadOnlyList<string> _normalizedKeywords;

    public MessageScanner(IEnumerable<string> keywords)
    {
        _normalizedKeywords = keywords
            .Select(Normalize)
            .Where(x => !string.IsNullOrWhiteSpace(x))
            .Distinct()
            .ToArray();
    }

    public string? FindMatch(string sourceText)
    {
        var normalizedText = Normalize(sourceText);
        foreach (var keyword in _normalizedKeywords)
        {
            if (normalizedText.Contains(keyword, StringComparison.Ordinal))
            {
                return keyword;
            }
        }

        return null;
    }

    public static string Normalize(string value)
    {
        return (value ?? string.Empty)
            .ToLowerInvariant()
            .Replace('ё', 'е');
    }
}
