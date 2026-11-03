# Архитектура MINI Messenger

## Клиент

Слои идут сверху вниз, зависимости — только вниз:

```
ui/        Qt Widgets, никакого SQL и никакой сети напрямую
services/  AuthService, ChatService, MessageService, ContactService, SearchService
net/       ApiClient (HTTP), WsClient + WebSocketConnection (RFC 6455)
storage/   StorageWorker (один поток), Database, репозитории, шифрование тел
crypto/    libsodium: KDF, sign.in, X25519/Ed25519, XChaCha20-Poly1305
domain/    Chat, Message, Contact, OutboxItem — копируемые value-типы
app/       Application, ClientRuntime, Config, Paths, SingleInstance
core/      Log, Result, Settings, Clock, Ids
```

Правила:

- UI не вызывает репозитории; только сервисы.
- Никаких блокирующих вызовов в GUI-потоке: сеть — Qt async, БД — `StorageWorker`.
- Все ошибки — `Result<T>` или сигнал; исключения не используются в горячем пути.
- Строки интерфейса — через `tr()` и `.ts`.

### `ClientRuntime`

Одна точка сборки сессии. `Application` (и E2E-тест) создают `ClientRuntime`
после успешного входа и передают:

- `Endpoint` сервера и `TlsPin`;
- `dataDir`, `deviceId`, логин, access/refresh токены, `K_enc`.

`ClientRuntime` открывает `StorageWorker`, загружает или создаёт `keys.bin`,
поднимает `ApiClient` и `WsClient`, создаёт `SessionCrypto`, `MessageService`,
`ChatService`, `ContactService`, `SearchService`, публикует публичный prekey
bundle и запускает WS. `MainWindow` получает указатели через `set*Service()`.

Поток сообщения:

1. `Composer` → `ChatView` → `MainWindow::sendMessage` → `MessageService::sendText`.
2. Запись в `messages` (queued) и `outbox` в `StorageWorker`.
3. Через окно отмены (5 с) `pump()` берёт по одному сообщению на чат.
4. `SessionCrypto::encrypt` (XChaCha20-Poly1305).
5. `POST /api/v1/mail/send`; сервер дедупит и кладёт в `mail`.
6. WS-кадр `ack`/`delivery` переводит статус в sent/delivered.
7. Входящее: WS-кадр `message` или `pull` → `handleIncoming` → `messages`.

Offline: пока WS не подключён, `pump()` не отправляет; очередь остаётся в
`outbox` и переживает перезапуск. При `connected` `ApiMessageTransport` делает
`pull` и `MessageService::flushQueue()`.

## Сервер

```
app/main.py        create_app: lifespan, middleware, error contract
app/routes/        auth, devices, users, captcha, mail, ws, health
app/services/      auth, device, mail, captcha, rate limit, token
app/ws/            Hub (соединения), Gateway (ticket, кадры, ping/pong)
app/sql/           001_initial.sql, 002_mail_ws.sql
```

- SQLite WAL, пул соединений, `BEGIN IMMEDIATE` для транзакций.
- `MailService` не разбирает `ciphertext`; читает только `header.to` для
  маршрутизации.
- `Hub` держит одно соединение на устройство; `MailService` пушит кадр
  `message` при вставке и `delivery` при ack.
- TTL: письма живут 14 дней; фоновый цикл раз в минуту удаляет просроченные
  и просроченные WS-тикеты.
- WS-тикет одноразовый, живёт 60 с, хранится хешем под мастер-ключом.

## Хранилище

- SQLite-файл рядом с exe (`data/mini.db`).
- Тело сообщения и превью черновика шифруются XChaCha20-Poly1305 ключом
  `DB_KEY = HKDF(K_enc, "MINI/db/v1")`.
- Метаданные (chat_id, время, отправитель, состояние) открыты — это осознанное
  ограничение первой сборки.
- `keys.bin` — AEAD-контейнер с identity/signed prekey/one-time prekeys.
