# Протокол

## Общие правила

- JSON, ключи `snake_case`.
- Ошибка: `{"error":{"code","message","retry_after_ms?","request_id"}}`.
- Авторизация: `Authorization: Bearer <access_token>`.
- `protocol_version = 1`; несовпадение мажора — «Обновите приложение».
- Время — миллисекунды Unix.

## HTTP API

| Метод | Путь | Назначение |
|---|---|---|
| GET | `/health` | живость, версия, статус БД |
| GET | `/api/v1/meta` | версия протокола, капча, TTL |
| GET | `/api/v1/auth/check_login?login=` | доступность логина |
| POST | `/api/v1/auth/prepare` | соли + nonce (`purpose`: login/register/reissue) |
| POST | `/api/v1/auth/register` | `login`, `display_name`, `auth_key`, `password_key`, `nonce`, `captcha_ticket`, `device` |
| POST | `/api/v1/auth/login` | `login`, `signin_key`, `password_key`, `nonce`, `device` |
| POST | `/api/v1/auth/refresh` | `refresh_token` |
| POST | `/api/v1/auth/change_password` | `old_password_key`, `new_password_key` |
| POST | `/api/v1/auth/reissue` | `auth_key`, `nonce` |
| POST | `/api/v1/auth/logout` | отзыв сессий устройства |
| POST | `/api/v1/auth/delete_account` | `login`, `password_key` |
| GET/POST/DELETE | `/api/v1/devices[...]` | список, регистрация, отзыв |
| GET | `/api/v1/users/{login}` | публичный профиль |
| POST | `/api/v1/captcha/challenge`, `/verify` | встроенная капча |
| POST | `/api/v1/mail/send` | `{"envelopes":[...]}` |
| GET | `/api/v1/mail/pull?device=&cursor=` | очередь писем |
| POST | `/api/v1/mail/ack` | `{"ids":[...]}` |
| PUT | `/api/v1/mail/keys` | публикация prekey bundle |
| GET | `/api/v1/mail/keys/{login}` | prekey bundle собеседника |
| POST | `/api/v1/ws/ticket` | одноразовый ticket на 60 с |
| WS | `/ws?ticket=` | кадры реального времени |

## Конверт

```json
{
  "kind": "text",
  "client_msg_id": "uuid-v4",
  "chat_id": "direct_<16 hex>",
  "ciphertext_b64": "<base64>",
  "header": {"to": "bob", "ts": 1790000000000}
}
```

- `ciphertext_b64` — непрозрачный BLOB, ≤1 МиБ; сервер его не читает.
- `header.to` — точный логин получателя (маршрутизация).
- При выдаче (`pull` и WS) сервер сам добавляет `header.sender_login` по
  аутентифицированному токену; клиентский `sender_login` перезаписывается.
- Получатель обязан проверить
  `chat_id == directChatId(self_login, sender_login)` и строить чат/ключ только
  по серверному `sender_login`. Иначе сообщение отбрасывается
  (`chat_id_mismatch` / `sender_unverified`).
- Дедуп: `(recipient_device, client_msg_id)`; повтор не создаёт дубль.
- TTL 14 дней; после `ack` запись удаляется сразу.

`mail/send` → `{"accepted":["<client_msg_id>"],"rejected":[{"client_msg_id","code"}]}`.
`mail/pull` → `{"items":[{"mail_id","received_at","envelope":{...}}],"cursor","has_more"}`.
`mail/ack` → `{"acked":["<mail_id>"]}`.

## WebSocket

Кадр: `{"v":1,"type","ts","data"}`.

| type | направление | data |
|---|---|---|
| `hello` | сервер → клиент | `protocol_version`, `server_time` |
| `message` | сервер → клиент | поля конверта |
| `ack` | сервер → отправитель | `client_msg_id`, `chat_id` |
| `delivery` | сервер → отправитель | `client_msg_id`, `chat_id`, `at` |
| `ping` | обе стороны | произвольные данные |
| `pong` | обе стороны | эхо `data` |
| `error` | сервер → клиент | `code`, `message` |

Правила:

- Тикет одноразовый, 60 с; используется в query `/ws?ticket=`.
- Кадр ≤1 МиБ; при превышении — close 4003.
- >100 кадров/с — close 4008.
- Сервер шлёт `ping` каждые 20 с; клиент отвечает `pong`.
- Неизвестные типы игнорируются (v1 расширяем).

## Офлайн-доставка

1. Сервер хранит письмо до `ack`.
2. Клиент при `connected` вызывает `pull`, получает все письма, отдаёт их
   `MessageService`, затем `ack`.
3. `MessageService` пишет сообщение в локальную БД; повторный `pull` не создаёт
   дубль (уникальный `client_msg_id`).
