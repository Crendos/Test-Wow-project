# Инструкция: Автономная сборка и патч клиента WoW (12.1.0.69497 и 11.x) в стиле Firestorm

Данное руководство объясняет:
1. Как скомпилировать движок патчера **без Qt** (консольная утилита `wow_patch_cli` и тесты).
2. Как работает патч на диске (Firestorm-стиль) для версии клиента **12.1.0.69497** и почему код `.text` не трогается.
3. Как настроить клиент и TrinityCore сервер, чтобы успешно зайти в мир.

---

## 1. Сборка без Qt

Ядро патчера (`src/wow_pe.*` и `src/wow_patch_core.*`), а также консольный интерфейс (`tools/wow_patch_cli.cpp`) написаны на чистом **C++20** и не имеют внешних зависимостей (даже OpenSSL не требуется — реализованы собственные парсеры DER/PEM, SHA-256 и расчет PE CheckSum).

### Вариант А: Прямая компиляция через g++ / clang++ (Linux / macOS / MinGW)

В корне папки `wow-db-studio`:

```bash
# Сборка консольного патчера wow_patch_cli:
g++ -std=c++20 -O2 -Isrc -Itools tools/wow_patch_cli.cpp src/wow_patch_core.cpp src/wow_pe.cpp -o wow_patch_cli

# Сборка и запуск тестов ядра (159 проверок):
g++ -std=c++20 -O2 -Isrc -Itools tools/wow_patch_selftest.cpp src/wow_patch_core.cpp src/wow_pe.cpp -o wow_patch_selftest
./wow_patch_selftest
```

### Вариант Б: Прямая компиляция через MSVC (Windows `cmd` / Developer Command Prompt)

```cmd
cl /std:c++20 /O2 /EHsc /utf-8 /Isrc /Itools tools\wow_patch_cli.cpp src\wow_patch_core.cpp src\wow_pe.cpp version.lib /Fe:wow_patch_cli.exe
```

### Вариант В: Сборка через CMake (автономная подпапка `tools/`)

В каталоге `tools/` подготовлен отдельный `CMakeLists.txt`, не требующий Qt:

```bash
cd tools
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build --output-on-failure
```

---

## 2. Как устроен патч «Firestorm-стиль» для клиента 12.1.0.69497

В клиентах ретейла (Dragonflight 10.x, The War Within 11.x, Midnight 12.x) секция `.text` защищена упаковщиком/рантайм-протекцией (Arxan/Digital.ai). Если модифицировать машинный код `.text` на диске:
* Срабатывает самоконтроль целостности при распаковке;
* Клиент мгновенно падает или перезаписывает распакованный код заново.

Поэтому **Firestorm-стиль** делает патч исключительно **в секциях инициализированных данных (`.rdata` / `.data`)**:
1. **ConnectTo RSA Modulus** (256 байт, Little-Endian): заменяется публичный модуль Blizzard на модуль открытого ключа TrinityCore (`ConnectToRSA`). Этим ключом клиент проверяет цифровую подпись пакета `SMSG_CONNECT_TO` при перенаправлении с auth/bnet на realm-сервер.
2. **GameCrypto Ed25519 Public Key** (32 байта): заменяется публичный ключ Blizzard на открытый ключ TrinityCore (`EnterEncryptedModePrivateKey`). Клиент проверяет подпись пакета `SMSG_ENTER_ENCRYPTED_MODE`. Без этого шага соединение разрывается сразу после выбора персонажа.
3. **Login Portal Suffix**: окно строки `.actual.battle.net` безопасно перезаписывается на `.actual.<домен>` (до 10 символов) или `.actual.localhost`. Клиент не сможет случайно соединиться с серверами авторизации Blizzard.
4. **WTF/Config.wtf**: создается строка:
   ```text
   SET portal "127.0.0.1:1119"
   ```
   (или адрес вашего сервера).

---

## 3. Инструкция по патчу через консоль (`wow_patch_cli`)

### Шаг 1. Диагностика исходного клиента
Запустите диагностику, чтобы убедиться, что PE-заголовок корректен, папка `Data` на месте и ключи Blizzard найдены в `.rdata`:

```bash
./wow_patch_cli diagnose "C:/Games/World of Warcraft/_retail_/Wow.exe" --version 12.1.0.69497
```

### Шаг 2. Патч на диске (in-place)
Примените патч с указанием адреса вашего логин-сервера:

```bash
./wow_patch_cli patch "C:/Games/World of Warcraft/_retail_/Wow.exe" --portal 127.0.0.1:1119 --version 12.1.0.69497
```

* Что произойдет:
  * Создастся резервная копия `Wow.exe.orig`.
  * `Wow.exe` будет пропатчен на месте.
  * В `WTF/Config.wtf` автоматически добавится/обновится параметр `SET portal "127.0.0.1:1119"`.
  * Движок перечитает записанный файл с диска и выполнит побайтовую верификацию каждого изменения.

---

## 4. Перенос патча через «Рецепты» (разница оригинального и чужого клиента)

Если у вас есть готовый рабочий клиент (например `WoW 11.2.5 - Firestorm.exe`) и чистый оригинальный `Wow.exe` той же версии:

1. **Снять рецепт различий:**
   ```bash
   ./wow_patch_cli recipe "C:/Clean/Wow.exe" "C:/Firestorm/WoW 11.2.5 - Firestorm.exe" firestorm_recipe.json
   ```
   Патчер найдет только различия в секциях данных (`.rdata`), запишет неизменный контекст до и после каждого блока и сохранит в JSON.

2. **Применить рецепт к вашему клиенту 12.1.0.69497:**
   ```bash
   ./wow_patch_cli apply-recipe "C:/Games/WoW 12.1.0/_retail_/Wow.exe" "C:/Games/WoW 12.1.0/_retail_/Wow_Patched.exe" firestorm_recipe.json
   ```
   Каждое изменение будет спозиционировано по контексту даже при смещении адресов в новом билде.

---

## 5. Настройка TrinityCore (серверная часть)

Для подключения клиента 12.1.0.69497 на сервере TrinityCore должны быть выставлены:

1. **bnetserver.conf**:
   ```ini
   LoginREST.ExternalAddress = 127.0.0.1
   LoginREST.LocalAddress = 127.0.0.1
   LoginREST.Port = 8081
   ```
2. **База данных `auth`, таблица `realmlist`**:
   * Поле `address`: `127.0.0.1` (или внешний IP).
   * Поле `port`: `8085` (worldserver).
   * Поле `gamebuild`: `69497`.
3. **Создание учетной записи**:
   В консоли `bnetserver` создавайте аккаунт через команду bnet:
   ```text
   bnetaccount create admin@local adminpassword
   account set gmlevel 1#1 3 -1
   ```
4. **Запуск**:
   Запускайте `Wow.exe` напрямую (без лаунчера Battle.net). Клиент прочитает `SET portal` из `WTF/Config.wtf`, использует ключи TrinityCore из секции данных и соединится с вашим сервером.
