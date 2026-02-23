import argparse
import csv
import ctypes
import getpass
import json
import os
import queue
import threading
import time
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Dict, List, Optional, Tuple


class TdLibError(Exception):
    def __init__(self, code: int, message: str):
        super().__init__(f"TDLib error {code}: {message}")
        self.code = code
        self.message = message


def normalize_text(value: str) -> str:
    return value.lower().replace("ё", "е")


def load_keywords(path: Path) -> List[str]:
    if not path.exists():
        raise FileNotFoundError(f"Не найден файл ключевых слов: {path}")
    keywords: List[str] = []
    for line in path.read_text(encoding="utf-8").splitlines():
        token = normalize_text(line.strip())
        if token:
            keywords.append(token)
    if not keywords:
        raise ValueError("Файл keywords.txt пустой")
    return keywords


def extract_message_text(message: dict) -> str:
    content = message.get("content", {})
    ctype = content.get("@type", "")

    if ctype == "messageText":
        return content.get("text", {}).get("text", "")
    if ctype in {
        "messagePhoto",
        "messageVideo",
        "messageAnimation",
        "messageAudio",
        "messageDocument",
        "messageVoiceNote",
        "messageVideoNote",
        "messagePaidMedia",
    }:
        return content.get("caption", {}).get("text", "")
    if ctype == "messagePoll":
        return content.get("poll", {}).get("question", "")
    if ctype == "messageContact":
        contact = content.get("contact", {})
        return " ".join(filter(None, [contact.get("first_name", ""), contact.get("last_name", ""), contact.get("phone_number", "")]))
    return ""


@dataclass
class AppConfig:
    api_id: int
    api_hash: str
    tdlib_library_path: str
    database_directory: str = "./tdlib_data"
    files_directory: str = "./tdlib_data/files"
    system_language_code: str = "ru"
    device_model: str = "Windows PC"
    system_version: str = "Windows 10"
    application_version: str = "1.0"
    use_test_dc: bool = False

    @classmethod
    def from_file(cls, path: Path) -> "AppConfig":
        data = json.loads(path.read_text(encoding="utf-8"))
        return cls(**data)


class TdLibClient:
    def __init__(self, config: AppConfig, log_verbose: bool = False):
        self.config = config
        self.log_verbose = log_verbose
        self._extra = 0
        self._pending: Dict[int, queue.Queue] = {}
        self._updates: "queue.Queue[dict]" = queue.Queue()
        self._closed = False

        self._lib = ctypes.CDLL(config.tdlib_library_path)
        self._lib.td_json_client_create.restype = ctypes.c_void_p
        self._lib.td_json_client_send.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        self._lib.td_json_client_receive.argtypes = [ctypes.c_void_p, ctypes.c_double]
        self._lib.td_json_client_receive.restype = ctypes.c_char_p
        self._lib.td_json_client_execute.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        self._lib.td_json_client_execute.restype = ctypes.c_char_p
        self._lib.td_json_client_destroy.argtypes = [ctypes.c_void_p]

        self._client = self._lib.td_json_client_create()
        if not self._client:
            raise RuntimeError("Не удалось создать TDLib client")

        self._recv_thread = threading.Thread(target=self._recv_loop, daemon=True)
        self._recv_thread.start()

        self._send({"@type": "setLogVerbosityLevel", "new_verbosity_level": 1 if log_verbose else 0})

    def close(self):
        if self._closed:
            return
        self._closed = True
        try:
            self._send({"@type": "close"})
        except Exception:
            pass
        time.sleep(0.2)
        self._lib.td_json_client_destroy(self._client)

    def _recv_loop(self):
        while not self._closed:
            raw = self._lib.td_json_client_receive(self._client, 1.0)
            if not raw:
                continue
            payload = json.loads(raw.decode("utf-8"))
            extra = payload.get("@extra")
            if isinstance(extra, int) and extra in self._pending:
                self._pending[extra].put(payload)
            else:
                self._updates.put(payload)

    def _send(self, data: dict):
        encoded = json.dumps(data, ensure_ascii=False).encode("utf-8")
        self._lib.td_json_client_send(self._client, encoded)

    def call(self, data: dict, timeout: float = 30.0) -> dict:
        self._extra += 1
        extra = self._extra
        waiter: "queue.Queue[dict]" = queue.Queue(maxsize=1)
        self._pending[extra] = waiter
        data = dict(data)
        data["@extra"] = extra
        self._send(data)
        try:
            response = waiter.get(timeout=timeout)
        except queue.Empty:
            raise TimeoutError(f"Таймаут вызова TDLib: {data.get('@type')}")
        finally:
            self._pending.pop(extra, None)

        if response.get("@type") == "error":
            raise TdLibError(response.get("code", -1), response.get("message", "unknown"))
        return response

    def get_update(self, timeout: float = 0.1) -> Optional[dict]:
        try:
            return self._updates.get(timeout=timeout)
        except queue.Empty:
            return None

    def authorize(self):
        while True:
            state = self.call({"@type": "getAuthorizationState"}, timeout=10)
            stype = state.get("@type")
            if stype == "authorizationStateReady":
                print("Авторизация завершена")
                return
            if stype == "authorizationStateClosed":
                raise RuntimeError("TDLib client закрыт во время авторизации")
            if stype == "authorizationStateWaitTdlibParameters":
                Path(self.config.database_directory).mkdir(parents=True, exist_ok=True)
                Path(self.config.files_directory).mkdir(parents=True, exist_ok=True)
                self.call(
                    {
                        "@type": "setTdlibParameters",
                        "parameters": {
                            "@type": "tdlibParameters",
                            "database_directory": self.config.database_directory,
                            "files_directory": self.config.files_directory,
                            "use_message_database": True,
                            "use_secret_chats": False,
                            "api_id": self.config.api_id,
                            "api_hash": self.config.api_hash,
                            "system_language_code": self.config.system_language_code,
                            "device_model": self.config.device_model,
                            "system_version": self.config.system_version,
                            "application_version": self.config.application_version,
                            "enable_storage_optimizer": True,
                            "use_test_dc": self.config.use_test_dc,
                        },
                    }
                )
                continue
            if stype == "authorizationStateWaitPhoneNumber":
                phone = input("Введите номер телефона (с кодом страны, например +7999...): ").strip()
                self.call({"@type": "setAuthenticationPhoneNumber", "phone_number": phone})
                continue
            if stype == "authorizationStateWaitCode":
                code = input("Введите код из Telegram/SMS: ").strip()
                self.call({"@type": "checkAuthenticationCode", "code": code})
                continue
            if stype == "authorizationStateWaitPassword":
                password = getpass.getpass("Введите пароль 2FA: ")
                self.call({"@type": "checkAuthenticationPassword", "password": password})
                continue
            if stype == "authorizationStateWaitRegistration":
                raise RuntimeError("Аккаунт не зарегистрирован")
            time.sleep(0.2)


def choose_chats(client: TdLibClient) -> List[dict]:
    print("\nЗагружаю список чатов...")
    chats: List[dict] = []
    offset_order = "9223372036854775807"
    offset_chat_id = 0

    while len(chats) < 50:
        result = client.call(
            {
                "@type": "getChats",
                "chat_list": {"@type": "chatListMain"},
                "limit": 50,
                "offset_order": offset_order,
                "offset_chat_id": offset_chat_id,
            }
        )
        chat_ids = result.get("chat_ids", [])
        if not chat_ids:
            break
        for cid in chat_ids:
            chat = client.call({"@type": "getChat", "chat_id": cid})
            chats.append(chat)
        last = chats[-1]
        order = str(last.get("positions", [{}])[0].get("order", "0")) if last.get("positions") else "0"
        offset_order = order
        offset_chat_id = last.get("id", 0)
        if len(chat_ids) < 50:
            break

    print("Выбор чатов:")
    print("1) Конкретный чат из списка")
    print("2) Все чаты")
    print("3) По username вручную")
    mode = input("Режим (1/2/3): ").strip() or "1"

    if mode == "2":
        return chats

    if mode == "3":
        username = input("Введите username без @: ").strip().lstrip("@")
        chat = client.call({"@type": "searchPublicChat", "username": username})
        return [chat]

    for i, chat in enumerate(chats, start=1):
        title = chat.get("title", "(без названия)")
        print(f"{i:>2}) {title} [id={chat.get('id')}]")
    idx = int(input("Введите номер чата: ").strip())
    return [chats[idx - 1]]


def parse_date(value: str, end_of_day: bool = False) -> Optional[int]:
    value = value.strip()
    if not value:
        return None
    dt = datetime.strptime(value, "%Y-%m-%d")
    if end_of_day:
        dt = dt.replace(hour=23, minute=59, second=59)
    return int(dt.timestamp())


def ask_options() -> Tuple[Optional[int], Optional[int], bool, bool, bool]:
    print("\nДиапазон дат (формат YYYY-MM-DD, оставьте пусто чтобы пропустить)")
    date_from = parse_date(input("От даты: "))
    date_to = parse_date(input("До даты: "), end_of_day=True)
    dry_run = input("Dry-run (y/n): ").strip().lower().startswith("y")
    delete_for_all = input("Delete for all (y/n): ").strip().lower().startswith("y")
    verbose = input("Показывать логи удаления (y/n): ").strip().lower().startswith("y")
    return date_from, date_to, dry_run, delete_for_all, verbose


def message_matches(message: dict, keywords: List[str]) -> Tuple[bool, Optional[str], str]:
    raw_text = extract_message_text(message)
    normalized = normalize_text(raw_text)
    for kw in keywords:
        if kw in normalized:
            snippet = raw_text.replace("\n", " ").strip()[:120]
            return True, kw, snippet
    return False, None, ""


def append_csv_row(path: Path, row: List[str]):
    write_header = not path.exists()
    with path.open("a", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        if write_header:
            writer.writerow(["chat_id", "chat_title", "message_id", "date", "matched_keyword", "text_snippet"])
        writer.writerow(row)


def process_chat(
    client: TdLibClient,
    chat: dict,
    keywords: List[str],
    date_from: Optional[int],
    date_to: Optional[int],
    dry_run: bool,
    delete_for_all: bool,
    verbose: bool,
    csv_path: Path,
):
    chat_id = chat["id"]
    chat_title = chat.get("title", "")
    print(f"\nОбработка чата: {chat_title} ({chat_id})")
    from_message_id = 0
    total_found = 0
    total_deleted = 0

    while True:
        history = client.call(
            {
                "@type": "getChatHistory",
                "chat_id": chat_id,
                "from_message_id": from_message_id,
                "offset": 0,
                "limit": 100,
                "only_local": False,
            },
            timeout=60,
        )
        messages = history.get("messages", [])
        if not messages:
            break

        for msg in messages:
            msg_date = msg.get("date", 0)
            if date_from and msg_date < date_from:
                continue
            if date_to and msg_date > date_to:
                continue

            matched, kw, snippet = message_matches(msg, keywords)
            if not matched:
                continue

            total_found += 1
            stamp = datetime.fromtimestamp(msg_date).strftime("%Y-%m-%d %H:%M:%S")
            append_csv_row(
                csv_path,
                [str(chat_id), chat_title, str(msg.get("id")), stamp, kw or "", snippet],
            )

            if dry_run:
                print(f"[DRY-RUN] match chat={chat_title} msg={msg.get('id')} kw='{kw}' text='{snippet}'")
                continue

            if delete_for_all and not msg.get("can_be_deleted_for_all", False):
                if verbose:
                    print(f"[SKIP] msg={msg.get('id')} нельзя удалить для всех")
                continue

            try:
                client.call(
                    {
                        "@type": "deleteMessages",
                        "chat_id": chat_id,
                        "message_ids": [msg["id"]],
                        "revoke": bool(delete_for_all),
                    }
                )
                total_deleted += 1
                if verbose:
                    print(f"[DELETED] msg={msg.get('id')} kw='{kw}'")
            except TdLibError as exc:
                if verbose:
                    print(f"[ERROR] msg={msg.get('id')} {exc}")
            time.sleep(0.08)

        from_message_id = messages[-1].get("id", 0)
        if from_message_id == 0:
            break
        time.sleep(0.12)

    print(f"Итого: найдено={total_found}, удалено={total_deleted}")


def main():
    parser = argparse.ArgumentParser(description="Telegram keyword deleter via TDLib")
    parser.add_argument("--config", default="config.json", help="Путь к config.json")
    parser.add_argument("--keywords", default="keywords.txt", help="Путь к keywords.txt")
    parser.add_argument("--csv", default="deleted_log.csv", help="Путь к CSV-логу")
    args = parser.parse_args()

    config_path = Path(args.config)
    if not config_path.exists():
        raise FileNotFoundError("config.json не найден. Создайте его на основе config.json.example")

    config = AppConfig.from_file(config_path)
    keywords = load_keywords(Path(args.keywords))

    client = TdLibClient(config)
    try:
        client.authorize()
        chats = choose_chats(client)
        date_from, date_to, dry_run, delete_for_all, verbose = ask_options()

        for chat in chats:
            try:
                process_chat(
                    client=client,
                    chat=chat,
                    keywords=keywords,
                    date_from=date_from,
                    date_to=date_to,
                    dry_run=dry_run,
                    delete_for_all=delete_for_all,
                    verbose=verbose,
                    csv_path=Path(args.csv),
                )
            except TdLibError as exc:
                print(f"Ошибка TDLib при обработке чата {chat.get('title')}: {exc}")
                time.sleep(0.5)
            except Exception as exc:
                print(f"Непредвиденная ошибка в чате {chat.get('title')}: {exc}")
                time.sleep(0.5)
    finally:
        client.close()


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\nОстановка по Ctrl+C")
