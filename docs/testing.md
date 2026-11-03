# Тестирование

## Одна команда

```powershell
.\scripts\check.ps1
```

Собирает Release, гоняет `ctest` (клиент) и `pytest` (сервер).

## Сервер (`server/tests/`)

`pytest` на FastAPI `TestClient` с временной БД, мастер-ключом и быстрым Argon2.

- `auth/` — регистрация, вход, nonce, устройства, пароли, блокировки, капча,
  удаление аккаунта, отсутствие plaintext.
- `mail/` — доставка через `pull`, дедуп `client_msg_id`, удаление после `ack`,
  TTL, привязка `device`, WS-тикет и кадры, ping/pong, приватность БД и логов.

Запуск отдельно:

```powershell
cd server
.\.venv\Scripts\python.exe -m pytest tests/mail -q
```

## Клиент (`client/tests/`)

Qt Test, `QT_QPA_PLATFORM=offscreen`.

- `auth/` — `sign.in`, KDF-векторы, AuthService.
- `crypto/` — sodium, identity keys, X3DH, Double Ratchet, Safety Number.
- `net/` — ApiClient против stub-сервера, WS-клиент, пиннинг, туннель.
- `storage/` — миграции, репозитории, outbox, бэкап.
- `services/` — MessageService (очередь, идемпотентность, статусы),
  ChatService, ContactService, SearchService, SessionCrypto.
- `ui/` — MainWindow с сервисами, профиль, диалоги.
- `e2e/` — **сквозной**: запускает реальный `server/run.py`, регистрирует два
  аккаунта через `AuthService`, поднимает два `ClientRuntime`, обменивается
  текстом и проверяет офлайн-доставку после переподключения.

Один тест:

```powershell
ctest --test-dir build/client -R test_e2e --output-on-failure
```

## Что считается доказательством

- Новый тест сначала красный: падает по проверяемой причине, а не из-за
  импорта или опечатки.
- Ожидаемые значения берутся из спецификации, ручного расчёта или примера,
  а не из кода под тестом.
- E2E проверяет реальную сеть, реальный сервер и реальную БД, а не заглушки.

## Ограничения тестов

- E2E требует `server/.venv` и свободный порт; в CI ставится Python 3.12.
- Сетевые тесты используют локальный stub или `127.0.0.1`; внешних сервисов нет.
- UI-тесты не проверяют пиксели; дизайн проверяется скриншотами вручную.
