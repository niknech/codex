using System.IO;
using System.Text.Json;

namespace TelegramKeywordCleaner;

public sealed class AppConfig
{
    public int ApiId { get; init; }
    public string ApiHash { get; init; } = string.Empty;
    public string PhoneNumber { get; init; } = string.Empty;

    public static AppConfig Load(string path)
    {
        var text = File.ReadAllText(path);
        return JsonSerializer.Deserialize<AppConfig>(text) ?? new AppConfig();
    }

    public static void Save(string path, AppConfig config)
    {
        var json = JsonSerializer.Serialize(config, new JsonSerializerOptions { WriteIndented = true });
        File.WriteAllText(path, json);
    }
}
