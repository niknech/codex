using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using System.Threading.Channels;
using System.Threading.Tasks;
using TdLib;

namespace TelegramKeywordCleaner;

public sealed class TelegramClientService : IDisposable
{
    private TdClient? _client;
    private AppConfig? _config;
    private readonly Channel<TdApi.AuthorizationState> _authStateChannel = Channel.CreateUnbounded<TdApi.AuthorizationState>();
    private readonly ConcurrentDictionary<long, TelegramChatView> _knownChats = new();

    public Task InitializeAsync(AppConfig config, Action<string> log, CancellationToken ct)
    {
        ct.ThrowIfCancellationRequested();

        _config = config;
        _client = new TdClient();
        _client.UpdateReceived += OnUpdate;

        _ = _client.ExecuteAsync(new TdApi.SetLogVerbosityLevel
        {
            NewVerbosityLevel = 1,
        });

        log("TDLib инициализирован.");
        return Task.CompletedTask;
    }

    public async Task AuthenticateAsync(
        Func<string> phoneProvider,
        Func<string> codeProvider,
        Func<string> passwordProvider,
        Action<string> log,
        CancellationToken ct)
    {
        EnsureClient();

        while (!ct.IsCancellationRequested)
        {
            var state = await _authStateChannel.Reader.ReadAsync(ct);
            switch (state)
            {
                case TdApi.AuthorizationState.AuthorizationStateWaitTdlibParameters:
                    await SendExpectingAsync<TdApi.Ok>(BuildSetTdlibParameters(), ct);
                    log("Переданы параметры TDLib.");
                    break;

                case TdApi.AuthorizationState.AuthorizationStateWaitEncryptionKey:
                    await SendExpectingAsync<TdApi.Ok>(new TdApi.CheckDatabaseEncryptionKey(), ct);
                    log("Проверен ключ шифрования БД TDLib.");
                    break;

                case TdApi.AuthorizationState.AuthorizationStateWaitPhoneNumber:
                    await SendExpectingAsync<TdApi.Ok>(new TdApi.SetAuthenticationPhoneNumber { PhoneNumber = phoneProvider() }, ct);
                    log("Ожидание кода подтверждения...");
                    break;

                case TdApi.AuthorizationState.AuthorizationStateWaitCode:
                    await SendExpectingAsync<TdApi.Ok>(new TdApi.CheckAuthenticationCode { Code = codeProvider() }, ct);
                    log("Проверка кода...");
                    break;

                case TdApi.AuthorizationState.AuthorizationStateWaitPassword:
                    await SendExpectingAsync<TdApi.Ok>(new TdApi.CheckAuthenticationPassword { Password = passwordProvider() }, ct);
                    log("Проверка 2FA пароля...");
                    break;

                case TdApi.AuthorizationState.AuthorizationStateWaitRegistration:
                    throw new InvalidOperationException("Для аккаунта требуется регистрация. Используйте уже зарегистрированный аккаунт Telegram.");

                case TdApi.AuthorizationState.AuthorizationStateWaitOtherDeviceConfirmation:
                    log("Подтвердите вход на другом устройстве в приложении Telegram.");
                    break;

                case TdApi.AuthorizationState.AuthorizationStateReady:
                    log("Состояние авторизации: Ready.");
                    return;

                case TdApi.AuthorizationState.AuthorizationStateClosed:
                    throw new InvalidOperationException("TDLib клиент закрылся во время авторизации.");
            }
        }
    }

    public async Task<List<TelegramChatView>> GetMainChatsAsync(int limit, CancellationToken ct)
    {
        EnsureClient();

        _ = await SendExpectingAsync<TdApi.Ok>(new TdApi.LoadChats
        {
            ChatList = new TdApi.ChatList.ChatListMain(),
            Limit = limit,
        }, ct);

        return _knownChats.Values
            .OrderBy(c => c.DisplayName)
            .Take(limit)
            .ToList();
    }

    public async Task<TelegramChatView> GetChatAsync(long chatId, CancellationToken ct)
    {
        EnsureClient();
        if (_knownChats.TryGetValue(chatId, out var chat))
        {
            return chat;
        }

        var tdChat = await SendExpectingAsync<TdApi.Chat>(new TdApi.GetChat { ChatId = chatId }, ct);
        chat = TelegramChatView.FromTd(tdChat);
        _knownChats[chat.Id] = chat;
        return chat;
    }

    public async Task<long> ResolveChatByUsernameAsync(string username, CancellationToken ct)
    {
        EnsureClient();

        var found = await SendExpectingAsync<TdApi.Chat>(new TdApi.SearchPublicChat { Username = username }, ct);
        var view = TelegramChatView.FromTd(found);
        _knownChats[view.Id] = view;
        return view.Id;
    }

    public async IAsyncEnumerable<IReadOnlyList<TelegramMessageView>> GetChatHistoryPagedAsync(long chatId, ScanOptions options, [System.Runtime.CompilerServices.EnumeratorCancellation] CancellationToken ct)
    {
        EnsureClient();

        long fromMessageId = 0;
        while (!ct.IsCancellationRequested)
        {
            TdApi.Messages page;
            try
            {
                page = await SendExpectingAsync<TdApi.Messages>(new TdApi.GetChatHistory
                {
                    ChatId = chatId,
                    FromMessageId = fromMessageId,
                    Offset = 0,
                    Limit = 100,
                    OnlyLocal = false,
                }, ct);
            }
            catch (TdException tdEx)
            {
                await Task.Delay(750, ct);
                throw new InvalidOperationException($"Ошибка TDLib при получении истории: {tdEx.Message}", tdEx);
            }

            if (page.Messages_.Length == 0)
            {
                yield break;
            }

            var mapped = page.Messages_
                .Select(TelegramMessageView.FromTd)
                .Where(m => IsDateInRange(m.DateUtc, options))
                .ToList();

            if (mapped.Count > 0)
            {
                yield return mapped;
            }

            fromMessageId = page.Messages_.Last().Id;
            await Task.Delay(200, ct);
        }
    }

    public async Task DeleteMessagesAsync(long chatId, long messageId, bool revoke, CancellationToken ct)
    {
        EnsureClient();

        _ = await SendExpectingAsync<TdApi.Ok>(new TdApi.DeleteMessages
        {
            ChatId = chatId,
            MessageIds = [messageId],
            Revoke = revoke,
        }, ct);
    }

    private TdApi.SetTdlibParameters BuildSetTdlibParameters()
    {
        if (_config is null)
        {
            throw new InvalidOperationException("Конфигурация TDLib не загружена.");
        }

        return new TdApi.SetTdlibParameters
        {
            Parameters = new TdApi.TdlibParameters
            {
                UseTestDc = false,
                DatabaseDirectory = "./tdlib_data",
                FilesDirectory = "./tdlib_data/files",
                UseFileDatabase = true,
                UseChatInfoDatabase = true,
                UseMessageDatabase = true,
                UseSecretChats = false,
                ApiId = _config.ApiId,
                ApiHash = _config.ApiHash,
                SystemLanguageCode = "ru",
                DeviceModel = Environment.MachineName,
                SystemVersion = Environment.OSVersion.VersionString,
                ApplicationVersion = "1.0",
                EnableStorageOptimizer = true,
            },
        };
    }

    private async Task<TExpected> SendExpectingAsync<TExpected>(TdApi.Function<TExpected> function, CancellationToken ct)
        where TExpected : TdApi.Object
    {
        EnsureClient();
        ct.ThrowIfCancellationRequested();

        var response = await _client!.ExecuteAsync(function);

        return response switch
        {
            TExpected expected => expected,
            TdApi.Error err => throw new TdException(err.Message),
            _ => throw new InvalidOperationException($"Неожиданный ответ TDLib: {response.GetType().Name}"),
        };
    }

    private static bool IsDateInRange(DateTime messageDateUtc, ScanOptions options)
    {
        if (options.DateFromUtc.HasValue && messageDateUtc < options.DateFromUtc.Value)
        {
            return false;
        }

        if (options.DateToUtc.HasValue && messageDateUtc > options.DateToUtc.Value)
        {
            return false;
        }

        return true;
    }

    private void OnUpdate(object? sender, TdApi.Update update)
    {
        switch (update)
        {
            case TdApi.Update.UpdateAuthorizationState state:
                _authStateChannel.Writer.TryWrite(state.AuthorizationState);
                break;

            case TdApi.Update.UpdateNewChat newChat:
                var chat = TelegramChatView.FromTd(newChat.Chat);
                _knownChats[chat.Id] = chat;
                break;

            case TdApi.Update.UpdateChatTitle titleUpdate:
                if (_knownChats.TryGetValue(titleUpdate.ChatId, out var existing))
                {
                    _knownChats[titleUpdate.ChatId] = existing with { Title = titleUpdate.Title };
                }
                break;
        }
    }

    private void EnsureClient()
    {
        if (_client is null)
        {
            throw new InvalidOperationException("TDLib client is not initialized.");
        }
    }

    public void Dispose()
    {
        if (_client is not null)
        {
            _client.UpdateReceived -= OnUpdate;
            _client.Dispose();
            _client = null;
        }
    }
}

public sealed record TelegramChatView(long Id, string Title)
{
    public string DisplayName => $"{Title} ({Id})";

    public static TelegramChatView FromTd(TdApi.Chat chat) => new(chat.Id, chat.Title);
}

public sealed record TelegramMessageView(long Id, DateTime DateUtc, string TextForMatch)
{
    public static TelegramMessageView FromTd(TdApi.Message message)
    {
        var text = message.Content switch
        {
            TdApi.MessageContent.MessageText mt => mt.Text.Text,
            TdApi.MessageContent.MessagePhoto mp => mp.Caption.Text,
            TdApi.MessageContent.MessageVideo mv => mv.Caption.Text,
            TdApi.MessageContent.MessageDocument md => md.Caption.Text,
            TdApi.MessageContent.MessageAnimation ma => ma.Caption.Text,
            TdApi.MessageContent.MessageAudio maud => maud.Caption.Text,
            _ => string.Empty,
        };

        return new TelegramMessageView(
            message.Id,
            DateTimeOffset.FromUnixTimeSeconds(message.Date).UtcDateTime,
            text ?? string.Empty);
    }
}

public sealed class ScanOptions
{
    public bool AnyChat { get; init; }
    public string Username { get; init; } = string.Empty;
    public DateTime? DateFromUtc { get; init; }
    public DateTime? DateToUtc { get; init; }
    public bool DryRun { get; init; }
    public bool DeleteForAll { get; init; }
    public bool VerboseLogs { get; init; }
}
