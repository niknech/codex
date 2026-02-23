using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;

namespace TelegramKeywordCleaner;

public sealed class MainWindow : Window
{
    private readonly TelegramClientService _telegramClient = new();
    private readonly CsvLogger _csvLogger = new("deleted_log.csv");

    private readonly TextBox _apiIdBox = new();
    private readonly PasswordBox _apiHashBox = new();
    private readonly TextBox _phoneBox = new();
    private readonly PasswordBox _codeBox = new();
    private readonly PasswordBox _passwordBox = new();

    private readonly ComboBox _chatSelector = new();
    private readonly TextBox _usernameBox = new();
    private readonly DatePicker _fromDatePicker = new();
    private readonly DatePicker _toDatePicker = new();
    private readonly CheckBox _anyChatCheckBox = new() { Content = "Любой чат" };
    private readonly CheckBox _dryRunCheckBox = new() { Content = "Dry-run (не удалять)", IsChecked = true };
    private readonly CheckBox _deleteForAllCheckBox = new() { Content = "Delete for all" };
    private readonly CheckBox _verboseLogsCheckBox = new() { Content = "Показывать логи", IsChecked = true };
    private readonly TextBox _logBox = new() { IsReadOnly = true, AcceptsReturn = true, VerticalScrollBarVisibility = ScrollBarVisibility.Auto };

    private readonly Button _authButton = new() { Content = "Авторизоваться", Margin = new Thickness(0, 6, 0, 0) };
    private readonly Button _scanButton = new() { Content = "Сканировать и удалить", Margin = new Thickness(0, 6, 0, 0), IsEnabled = false };

    public MainWindow()
    {
        Title = "Telegram Keyword Cleaner (TDLib)";
        Width = 900;
        Height = 760;

        InitializeLayout();
        LoadConfig();
    }

    private void InitializeLayout()
    {
        var root = new ScrollViewer();
        var panel = new StackPanel { Margin = new Thickness(12) };
        root.Content = panel;
        Content = root;

        panel.Children.Add(new TextBlock { Text = "API ID" });
        panel.Children.Add(_apiIdBox);
        panel.Children.Add(new TextBlock { Text = "API Hash" });
        panel.Children.Add(_apiHashBox);
        panel.Children.Add(new TextBlock { Text = "Phone (+7999...)" });
        panel.Children.Add(_phoneBox);
        panel.Children.Add(new TextBlock { Text = "Code (после запроса)" });
        panel.Children.Add(_codeBox);
        panel.Children.Add(new TextBlock { Text = "2FA Password (если нужно)" });
        panel.Children.Add(_passwordBox);
        panel.Children.Add(_authButton);

        panel.Children.Add(new Separator { Margin = new Thickness(0, 8, 0, 8) });

        panel.Children.Add(_anyChatCheckBox);
        panel.Children.Add(new TextBlock { Text = "Выбор чата из списка" });
        panel.Children.Add(_chatSelector);
        panel.Children.Add(new TextBlock { Text = "...или username вручную (без @)" });
        panel.Children.Add(_usernameBox);

        panel.Children.Add(new TextBlock { Text = "Дата от" });
        panel.Children.Add(_fromDatePicker);
        panel.Children.Add(new TextBlock { Text = "Дата до" });
        panel.Children.Add(_toDatePicker);

        panel.Children.Add(_dryRunCheckBox);
        panel.Children.Add(_deleteForAllCheckBox);
        panel.Children.Add(_verboseLogsCheckBox);
        panel.Children.Add(_scanButton);

        panel.Children.Add(new Separator { Margin = new Thickness(0, 8, 0, 8) });
        panel.Children.Add(new TextBlock { Text = "Логи" });
        _logBox.Height = 280;
        panel.Children.Add(_logBox);

        _authButton.Click += async (_, _) => await AuthenticateAsync();
        _scanButton.Click += async (_, _) => await StartScanAsync();
    }

    private void LoadConfig()
    {
        if (!File.Exists("config.json"))
        {
            return;
        }

        var cfg = AppConfig.Load("config.json");
        _apiIdBox.Text = cfg.ApiId.ToString(CultureInfo.InvariantCulture);
        _apiHashBox.Password = cfg.ApiHash;
        _phoneBox.Text = cfg.PhoneNumber;
    }

    private async Task AuthenticateAsync()
    {
        try
        {
            var config = new AppConfig
            {
                ApiId = int.TryParse(_apiIdBox.Text, out var id) ? id : 0,
                ApiHash = _apiHashBox.Password,
                PhoneNumber = _phoneBox.Text,
            };

            AppConfig.Save("config.json", config);

            await _telegramClient.InitializeAsync(config, Log, CancellationToken.None);
            await _telegramClient.AuthenticateAsync(
                phoneProvider: () => _phoneBox.Text,
                codeProvider: () => _codeBox.Password,
                passwordProvider: () => _passwordBox.Password,
                log: Log,
                ct: CancellationToken.None);

            var chats = await _telegramClient.GetMainChatsAsync(100, CancellationToken.None);
            _chatSelector.ItemsSource = chats;
            _chatSelector.DisplayMemberPath = nameof(TelegramChatView.DisplayName);
            _chatSelector.SelectedIndex = chats.Count > 0 ? 0 : -1;

            _scanButton.IsEnabled = true;
            Log("Авторизация завершена.");
        }
        catch (Exception ex)
        {
            Log($"Ошибка авторизации: {ex.Message}");
        }
    }

    private async Task StartScanAsync()
    {
        _scanButton.IsEnabled = false;
        try
        {
            var keywords = File.Exists("keywords.txt")
                ? File.ReadAllLines("keywords.txt", Encoding.UTF8).Where(x => !string.IsNullOrWhiteSpace(x)).ToArray()
                : Array.Empty<string>();

            if (keywords.Length == 0)
            {
                Log("keywords.txt пустой или отсутствует.");
                return;
            }

            var scanOptions = new ScanOptions
            {
                AnyChat = _anyChatCheckBox.IsChecked == true,
                Username = _usernameBox.Text.Trim(),
                DryRun = _dryRunCheckBox.IsChecked == true,
                DeleteForAll = _deleteForAllCheckBox.IsChecked == true,
                VerboseLogs = _verboseLogsCheckBox.IsChecked == true,
                DateFromUtc = _fromDatePicker.SelectedDate?.Date.ToUniversalTime(),
                DateToUtc = _toDatePicker.SelectedDate?.Date.AddDays(1).AddSeconds(-1).ToUniversalTime(),
            };

            var chatIds = await ResolveChatsAsync(scanOptions);
            var scanner = new MessageScanner(keywords);
            var deleter = new Deleter(_telegramClient, _csvLogger, Log);

            foreach (var chatId in chatIds)
            {
                var chat = await _telegramClient.GetChatAsync(chatId, CancellationToken.None);
                Log($"Обработка чата: {chat.Title} ({chat.Id})");

                await foreach (var page in _telegramClient.GetChatHistoryPagedAsync(chatId, scanOptions, CancellationToken.None))
                {
                    foreach (var message in page)
                    {
                        var match = scanner.FindMatch(message.TextForMatch);
                        if (match is null)
                        {
                            continue;
                        }

                        await deleter.HandleMatchAsync(chat, message, match, scanOptions, CancellationToken.None);
                    }
                }
            }

            Log("Сканирование завершено.");
        }
        catch (Exception ex)
        {
            Log($"Ошибка сканирования: {ex.Message}");
        }
        finally
        {
            _scanButton.IsEnabled = true;
        }
    }

    private async Task<List<long>> ResolveChatsAsync(ScanOptions options)
    {
        if (options.AnyChat)
        {
            var chats = await _telegramClient.GetMainChatsAsync(200, CancellationToken.None);
            return chats.Select(c => c.Id).ToList();
        }

        if (!string.IsNullOrWhiteSpace(options.Username))
        {
            var byUsername = await _telegramClient.ResolveChatByUsernameAsync(options.Username, CancellationToken.None);
            return [byUsername];
        }

        if (_chatSelector.SelectedItem is TelegramChatView selected)
        {
            return [selected.Id];
        }

        return [];
    }

    private void Log(string message)
    {
        if (_verboseLogsCheckBox.IsChecked != true)
        {
            return;
        }

        Dispatcher.Invoke(() =>
        {
            _logBox.AppendText($"[{DateTime.Now:HH:mm:ss}] {message}{Environment.NewLine}");
            _logBox.ScrollToEnd();
        });
    }
}
