# MINI Messenger

MINI Messenger — учебный кроссплатформенный мессенджер для «своих»: вход по логину,
паролю и файлу `sign.in`, без телефона и почты, с шифрованием переписки на устройстве
и сервером, который не читает сообщения. Клиент — C++20 / Qt 6 Widgets, сервер —
Python 3.12 / FastAPI / SQLite.

> Это учебный проект. Он не связан с MINI/BMW, Telegram или другими продуктами.

CURRENT TUNNEL: (not running)

Строку выше обновляет `scripts/run_bore.ps1`; клиент ищет её в README при запуске.

## Скриншот

В этой сборке скриншот не приложен; он появится в релизе после приёмки фазы 2.
Внешний вид: три колонки — рейка, список чатов, переписка; темы dark/light/AMOLED,
восемь акцентов.

## Быстрый старт

1. Скачайте архив релиза и распакуйте в любую папку (портативный режим).
2. В `config/connect.ip` укажите адрес сервера: `bore.pub:54321` или `127.0.0.1:8765`.
3. Если сервер использует самоподписанный сертификат, положите его SHA-256 отпечаток
   в `config/server_fingerprint.txt` (формат `AA:BB:...`).
4. Запустите `MINI Messenger.exe`.

Без `connect.ip` клиент пробует `127.0.0.1:8765`.

## Регистрация и вход

- «Создать аккаунт»: логин (4–24 символа, начинается с буквы), имя, пароль.
- После регистрации в «Загрузках» появляется файл `sign.in`. Это второй фактор:
  без него войти нельзя. Сохраните файл и не редактируйте его.
- «Войти»: логин, пароль и путь к `sign.in`.
- Смена пароля не меняет `sign.in`; перегенерация `sign.in` перешифровывает локальные
  данные. Удаление аккаунта необратимо.

## Сборка из исходников

Требования: Windows 10/11 x64, Visual Studio 2022/2026 с C++ workload, CMake 3.24+,
Ninja, Qt 6.8 (`msvc2022_64`), vcpkg, Python 3.12.

```powershell
git clone <repo-url>
cd "MINI Messenger"
# Qt: D:\Qt\6.8.3\msvc2022_64 (или передайте -QtPrefix)
# vcpkg: D:\vcpkg (или передайте -VcpkgRoot)
.\scripts\build.ps1 -Configuration Release
```

Сборка кладёт готовый комплект в `dist/`: `MINI Messenger.exe`, Qt-плагины,
`libsodium.dll`, `sqlite3.dll`, `translations/`, пустые `config/`, `data/`, `media/`,
`backups/`.

Проверка одной командой:

```powershell
.\scripts\check.ps1
```

Она собирает Release, запускает `ctest` и `pytest`.

## Запуск сервера и bore

```powershell
# сервер (создаст server/.venv и configs/server.toml при первом запуске)
.\scripts\run_server.ps1 -Port 8765

# второй терминал: публичный туннель
.\scripts\run_bore.ps1 -LocalPort 8765
```

`run_bore.ps1` пишет `client/config/connect.ip` и строку `CURRENT TUNNEL` в README.
`run_server.ps1` печатает и сохраняет отпечаток сертификата в
`client/config/server_fingerprint.txt`.

Для локальной отладки без TLS: `.\scripts\run_server.ps1 -NoTls`.

## Структура репозитория

```
client/            Qt-клиент (src/app, core, crypto, net, storage, services, ui)
client/tests/      unit, integration и e2e тесты (Qt Test)
client/resources/  иконки, шрифты, переводы
server/            FastAPI-сервер (app/routes, app/services, app/ws, app/sql)
server/tests/      pytest: auth, mail, WS
scripts/           build.ps1, check.ps1, run_server.ps1, run_bore.ps1
docs/              архитектура, крипто, модель угроз, протокол, тестирование
packaging/         Inno Setup и описание dist-комплекта
```

## Документация

- [docs/architecture.md](docs/architecture.md) — слои клиента и сервера.
- [docs/crypto.md](docs/crypto.md) — ключи, `sign.in`, шифрование сообщений.
- [docs/threat-model.md](docs/threat-model.md) — что защищено, а что нет.
- [docs/protocol.md](docs/protocol.md) — HTTP API, кадры WS, конверты.
- [docs/testing.md](docs/testing.md) — как и чем проверяется сборка.

## Ограничения и честные предупреждения

- Одно устройство на аккаунт (G02). Мультиустройства и синхронизации нет.
- Шифрование тела сообщений в этой сборке — статический X25519 между устройствами
  (без X3DH/Double Ratchet и без forward secrecy). Подробности и риски —
  `docs/crypto.md` и `docs/threat-model.md`.
- Метаданные (кто, когда, размер) сервер видит; тела сообщений — нет.
- Сервер не гарантирует 24/7; клиент рассчитан на периодическую недоступность и
  офлайн-очередь.
- Группы, каналы, медиа, звонки, реакции и боты в эту сборку не входят.
- Установка занимает 250–300 МБ из-за Qt WebEngine (hCaptcha).
- Минимальное окно 1000×640; целевое железо — слабый ПК, 1280×1024.

## Лицензия

MIT — см. [LICENSE](LICENSE).
