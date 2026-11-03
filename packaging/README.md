# Упаковка dist

`scripts\build.ps1 -Configuration Release` собирает `dist\`:

```
dist\
  MINI Messenger.exe
  Qt6*.dll, platforms\, styles\, tls\, imageformats\, ...
  libsodium.dll
  sqlite3.dll
  translations\mini.qm
  config\           connect.ip, server_fingerprint.txt, captcha.cfg
  data\             локальная БД, keys.bin, session.bin
  data\logs\
  media\
  backups\
```

Комплект портативный: настройки и данные лежат рядом с exe; при отсутствии прав
на запись `Paths` уходит в `%APPDATA%\MINIMessenger\`.

## Inno Setup

`packaging\mini.iss` собирает установщик из `..\dist`. Требуется Inno Setup 6:

```powershell
iscc packaging\mini.iss
```

Результат — `packaging\out\MINI-Messenger-0.1.0-win64.exe`. Версия в `mini.iss`
должна совпадать с `project(MiniMessenger VERSION ...)` в `client/CMakeLists.txt`.
