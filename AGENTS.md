<!-- autopilot:start -->
# MINI Messenger

Мессенджер «для своих»: вход по логину, паролю и файлу `sign.in`, без телефона и почты.
Клиент — C++20 / Qt 6 Widgets, сервер — Python 3.12 / FastAPI / SQLite,
HTTP и WebSocket поверх TLS 1.3 с пиннингом отпечатка. Учебный проект,
текущая сборка — ядро (фазы 0–2).

## Команды

Все — из корня проекта, в PowerShell.

| Команда | Что делает |
|---|---|
| `py -3.12 -m venv server\.venv` | один раз: окружение сервера |
| `server\.venv\Scripts\python.exe -m pip install -r server\requirements.txt` | зависимости сервера в это окружение |
| `.\scripts\build.ps1 -Configuration Release` | CMake + Ninja + сборка + `windeployqt` → `dist\MINI Messenger.exe` |
| `.\scripts\check.ps1` | Release-сборка + `ctest` (41) + `pytest` (98) одним прогоном |
| `.\scripts\run_server.ps1` | сервер на `127.0.0.1:8765` по TLS; сам создаёт `server\.venv`, `server\configs\server.toml` и пишет отпечаток в `client\config\server_fingerprint.txt` |
| `.\scripts\run_bore.ps1 -LocalPort 8765` | публичный туннель; пишет `client\config\connect.ip` и строку `CURRENT TUNNEL` в README |
| `& "dist\MINI Messenger.exe"` | клиент |
| `& "D:\Visual Studio Community 2026\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe" --test-dir build\client -R test_theme --output-on-failure` | один Qt-тест (`ctest` нет в PATH — путь полный) |
| `server\.venv\Scripts\python.exe -m pytest server\tests\auth\test_login.py -q` | один файл pytest |

Отладка сервера без TLS: `.\scripts\run_server.ps1 -NoTls`.

## Где что

| Путь | Что внутри |
|---|---|
| `client/src/services/` | `MessageService` — единственный владелец конвейера отправки; `SearchService` (Ctrl+K), `AuthService`, `CaptchaService` |
| `client/src/storage/` | SQLite через `StorageWorker` (один поток), репозитории, миграции; поисковый FTS-индекс — in-memory |
| `client/src/net/` | `ApiClient`, `WsClient`, `TlsPin` (сверка отпечатка), `TunnelResolver` |
| `client/src/ui/` | три колонки, темы и акценты, диалоги |
| `server/run.py`, `server/app/routes/`, `server/app/services/` | вход сервера, HTTP/WS-эндпоинты, бизнес-логика |
| `scripts/` | `build.ps1`, `check.ps1`, `run_server.ps1`, `run_bore.ps1` |
| `docs/architecture.md` | слои клиента и сервера; рядом `crypto.md`, `protocol.md`, `testing.md`, `threat-model.md`, `adr/` |
| `.autopilot/2026-11-02-mini-messenger--wip/interfaces.md` | границы модулей и решённые контракты |

## Подводные камни

- Qt 6.8.3 ставится вручную (aqtinstall) в `D:\Qt\6.8.3\msvc2022_64`. Без флага `build.ps1`
  ищет `D:\Qt` и `%USERPROFILE%\Qt`, иначе нужен `-QtPrefix`.
- vcpkg — отдельный клон `D:\vcpkg` (встроенный в VS не имеет `ports/`). Порядок поиска:
  `-VcpkgRoot`, `VCPKG_ROOT`, `D:\vcpkg`, `VC\vcpkg`.
- Qt WebEngine тянет ~250 МБ (вся установка 250–300 МБ). Он необязателен: без него
  hCaptcha падает на встроенную проверку, но dist меньше.
- TLS-пиннинг строгий: клиент пускает только сертификат с SHA-256 из
  `client\config\server_fingerprint.txt`. Файл пишет `run_server.ps1`; удалил
  `server\configs\*.pem` и `*.key` — сертификат и отпечаток новые, файл обновить.
- Поиск по сообщениям — FTS5 во временной (in-memory) таблице, пересобирается из
  расшифрованных тел при каждом старте; на диск plaintext-токены не попадают.
- `server\configs\server.toml`, `captcha.toml` и `master.key` — вне git (`.gitignore`);
  `server.toml` создаётся из `.example` при первом запуске.
- Переменные окружения (только имена, значения не читать и не коммитить):
  `MINI_QT_PREFIX`, `VCPKG_ROOT`, `CMAKE_PREFIX_PATH`, `MINI_SERVER_SECRET`,
  `MINI_CAPTCHA_SECRET`, `MINI_CAPTCHA_PROVIDER`, `MINI_CAPTCHA_KEY`, `MINI_LOG_LEVEL`,
  `MINI_ALLOW_REGISTRATION`.
- Одно устройство на аккаунт. Личные чаты шифруются X3DH + Double Ratchet; группы,
  медиа, боты, звонки, премиум — следующие сборки (фазы 3–6).

## Как здесь работает Autopilot

Сборка ведётся навыком `/autopilot`. Требования, спецификация и таски — в `.autopilot/`.
Прогресс — `.autopilot/dashboard.html`. Требование из `manifest.md` может снять
только пользователь. Прерванную сборку продолжает «продолжи автопилот».

### Правка без автопилота

Мелкий баг или доводка после сборки — обычная сессия, без прогона.

Сначала прочитай:
1. `.autopilot/PROGRESS.md` — что не доделано, известные баги, о чём договорились с пользователем.
2. Что правили после последней сборки — первая строка таблицы в «История git».
3. Только если правка их касается: почему сделано так — `docs/adr/`; что значат слова — `CONTEXT.md`;
   что именно просили — `manifest.md` в папке сборки (указана в начале `PROGRESS.md`).

После правки:
- коммит — по правилам из «История git»;
- строка в «Правки без автопилота» в `PROGRESS.md`: дата — что — почему;
- починенный пункт из «Не доделано» или «Известные баги» — удали;
- договорились с пользователем о том, что следующая сборка не должна пересматривать, —
  строка в «Договорились», его словами.

### История git

Целиком не читай — бери срез под вопрос:

| Вопрос | Команда |
|---|---|
| что правили после последней сборки | `git log --oneline "$(git log -1 --format=%h --grep='^Autopilot: сборка сдана')"..HEAD -- . ':(exclude).autopilot'` |
| всё, что делали ради требования | `git log --oneline --grep="R02"` |
| история файла | `git log --oneline -10 -- <файл>` |
| когда появился или исчез код | `git log --oneline -S "<фрагмент>"` |
| почему строка такая | `git blame -L <с>,<по> <файл>`, затем `git show <sha>` |
| что внутри коммита | `git show --stat <sha>` |

Срез длиннее ~30 коммитов — отдай его субагенту и возьми выжимку в десять строк.

Коммит: заголовок — что (`fix: даты в одном формате`), тело — почему, трейлер
`--trailer "Requirements: R02"`, если правка касается требования из `manifest.md`.
Заголовок `Autopilot: сборка сдана` пишет только автопилот — по нему находят последнюю сборку.
<!-- autopilot:end -->
