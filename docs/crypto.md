# Криптография

Только libsodium. Никаких собственных S-боксов, XOR и «секретных» алгоритмов.

## `sign.in`

- 20 токенов: 5 слов из 1024, 10 букв (латиница/кириллица/греческий), 5 символов.
- `CHECK = base32(BLAKE2b-64(normalized))`, сравнение регистрозависимое.
- Нормализация — 13 шагов (переводы строк, пробелы, BOM, Unicode NFC и т. д.).
- Файл `MINI-SIGNIN-1`; редактировать его нельзя.

## Ключи

```
K_file = Argon2id(normalized, salt_file)      m=64 MiB, t=3, p=1, len=32
K_auth = HKDF(K_file, "MINI/auth/v1")
K_enc  = HKDF(K_file, "MINI/enc/v1")
K_mac  = HKDF(K_file, "MINI/mac/v1")
PW_KEY = Argon2id(password, salt_pw)
DB_KEY = HKDF(K_enc, "MINI/db/v1")
KEY_WRAP = HKDF(K_enc, "MINI/keywrap/v1")
```

На сервер уходят только `K_auth` (в поле `auth_key`/`signin_key`) и `PW_KEY`
внутри TLS. Сервер хранит Argon2id-верификаторы и сравнивает постоянное время.
`K_enc`, `K_mac`, `sign.in` и приватные ключи устройства не покидают устройство.

## `keys.bin`

MAGIC/VER/COUNT + записи. Каждая запись — XChaCha20-Poly1305 под `KEY_WRAP`,
AAD связывает `mini-keys`, `device_id` и id записи, поэтому усечение или
перестановка ломают аутентификацию. Содержит identity Ed25519, signed prekey
X25519 с подписью и 20 one-time prekeys.

## Шифрование сообщений (текущая сборка)

Используется **статический X25519 key agreement**:

1. При старте клиент публикует публичный bundle:
   `identity_key` (Ed25519), `signed_prekey` (X25519), `signed_prekey_signature`.
2. Перед первым сообщением в чат клиент получает bundle собеседника,
   проверяет подпись Ed25519 и считает
   `shared = X25519(my_signed_prekey_private, peer_signed_prekey)`.
3. `session_key = HKDF-SHA256(shared, info="MINI/Session/v1:" + chat_id)`.
4. Тело: XChaCha20-Poly1305, случайный 24-байтный nonce, AAD = `chat_id`.
   Nonce и ciphertext лежат в конверте; сервер их не разбирает.

Свойства:

- Сервер видит только публичные ключи и шифртекст; расшифровать переписку он
  не может.
- Одинаковый ключ на пару устройств до смены signed prekey.
- **Нет forward secrecy**: компрометация signed prekey раскрывает прошлые
  сообщения этой пары.
- Ротация signed prekey (30 дней) требует пересогласования; в этой сборке
  старые чаты после ротации нужно начать заново.

X3DH и Double Ratchet реализованы и покрыты тестами (`client/src/crypto/`),
но в конвейер этой сборки не включены: для них нужен обмен one-time prekeys
и хранение ratchet-состояния между запусками. Это следующая сборка.

## Safety Number

`SHA-256("MINI-SafetyNumber/v1" || min(uid) || max(uid) || IK_A || IK_B)`,
12 групп по 5 цифр. Смена ключа собеседника должна показывать баннер —
в текущей сборке баннер не подключён к экрану чата.
