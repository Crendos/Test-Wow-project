# Инструкция: сборка WowDbStudio на Windows с нуля (Qt 6 + MSVC 2022)

Полный цикл «чистая машина → готовый WowDbStudio.exe»: установка Visual Studio 2022,
CMake, Qt 6, затем конфигурация, сборка, тесты и деплой.

> Если Qt ставить не хочется вообще — консольный патчер собирается без Qt
> (см. раздел 7 и `docs/PATCH_FIRESTORM_12.1.0.md`, раздел 1).

---

## 0. Что понадобится

| Компонент | Минимум | Зачем |
|---|---|---|
| Windows 10/11 x64 | — | Целевая платформа приложения |
| Visual Studio 2022 (Community достаточно) | 17.x | C++20-компилятор MSVC v143 |
| Workload «Desktop development with C++» | — | cl.exe, Windows SDK, CMake |
| CMake | 3.21+ | Генерация проекта (есть в составе VS) |
| Qt 6 (MSVC 2022 64-bit) | 6.4+ | GUI: Core, Gui, Widgets, Sql, Network |

---

## 1. Установка Visual Studio 2022 + CMake

### Вариант А: через winget (всё одной командой)

Откройте **PowerShell от администратора**:

```powershell
winget install Microsoft.VisualStudio.2022.Community --override "--add Microsoft.VisualStudio.Workload.NativeDesktop --includeRecommended --passive --norestart"
```

Workload `NativeDesktop` («Разработка классических приложений на C++») уже включает:
MSVC v143, Windows 11 SDK, **CMake** и инструменты тестирования.

### Вариант Б: вручную

1. Скачайте «Visual Studio 2022 Community» с https://visualstudio.microsoft.com/
2. В установщике отметьте workload **«Desktop development with C++»**
   (в деталях справа должны стоять: MSVC v143, Windows 10/11 SDK, C++ CMake tools for Windows).
3. Установите.

Проверка (в обычной cmd/PowerShell после установки):

```cmd
cmake --version
```

Должно быть **3.21 или новее**. Если `cmake` не находится — либо откройте
«Developer PowerShell for VS 2022», либо поставьте CMake отдельно:
`winget install Kitware.CMake`.

---

## 2. Установка Qt 6

Проекту нужны модули **Core, Gui, Widgets, Sql, Network** — все они входят в базовый
пакет Qt, ничего дополнительного выбирать не надо. Важно одно: **именно сборка
для MSVC 2022 64-bit** (не MinGW — preset в репозитории рассчитан на MSVC).

### Вариант А: Qt Online Installer (официальный)

1. Скачайте установщик: https://www.qt.io/download-qt-installer-oss
2. Запустите, войдите в (или создайте бесплатный) Qt Account.
3. На шаге выбора компонентов разверните **Qt → Qt 6.x.x** и отметьте:
   - **MSVC 2022 64-bit**
   - (опционально) **Qt Creator** — если хотите IDE от Qt.
4. Путь установки по умолчанию: `C:\Qt`. После установки получится
   каталог вида `C:\Qt\6.11.2\msvc2022_64` — запомните его, это ваш **QTDIR**.

### Вариант Б: без аккаунта, через aqtinstall (консоль)

```powershell
pip install aqtinstall
# посмотреть доступные версии:
aqt list-qt windows desktop
# поставить (подставьте версию из списка, любую >= 6.4):
aqt install-qt windows desktop 6.11.2 win64_msvc2022_64 -O C:\Qt
```

Получится тот же `C:\Qt\6.11.2\msvc2022_64`.

### Проверка

```cmd
dir C:\Qt\6.11.2\msvc2022_64\bin\qmake.exe
```

Если файл на месте — Qt установлен.

---

## 3. Получение исходников

```cmd
git clone https://github.com/Crendos/Test-Wow-project.git
cd Test-Wow-project\wow-db-studio
```

Если репозиторий уже есть — просто `git pull` внутри него.

---

## 4. Переменная окружения QTDIR

Пресет в `CMakePresets.json` берёт путь к Qt из переменной `QTDIR`.

```cmd
setx QTDIR "C:\Qt\6.11.2\msvc2022_64"
```

> Подставьте СВОЮ версию из шага 2. После `setx` **закройте и заново откройте**
> терминал, иначе переменная не подхватится.

---

## 5. Конфигурация и сборка

Все команды выполняются в корне папки `wow-db-studio`.

### Способ 1: через готовые пресеты (рекомендуется)

```cmd
cmake --preset vs2022-x64
cmake --build --preset vs2022-x64-release
```

Готовый exe: `out\build\vs2022-x64\Release\WowDbStudio.exe`.

### Способ 2: вручную, без пресетов

```cmd
cmake -S . -B out/build/vs -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=C:/Qt/6.11.2/msvc2022_64
cmake --build out/build/vs --config Release
```

### Способ 3: через GUI Visual Studio

1. `cmake -S . -B out/build/vs -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=...`
2. Откройте `out\build\vs\WowDbStudio.sln`.
3. Сверху выберите **Release | x64** → Build → Build Solution.

### Способ 4: Qt Creator

File → Open File or Project → выберите `wow-db-studio\CMakeLists.txt` →
кит «Qt 6.x MSVC2022 64bit» определится автоматически → Configure Project → Build.

---

## 6. Проверка: автотесты движка патча

Вместе с приложением собираются консольные тесты (`wow_pe_selftest`, `wow_patch_selftest`):

```cmd
ctest --test-dir out/build/vs2022-x64 -C Release --output-on-failure
```

Оба теста должны завершиться `Passed`. Если падают — пришлите вывод, там будет
конкретная строка проверки.

---

## 7. (Опционально) Консольный патчер без Qt

Движок патча (`src/wow_pe.*`, `src/wow_patch_core.*`) — чистый C++20, ему Qt не нужен.
Отдельный CMake-проект лежит в `tools/`:

```cmd
cd tools
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Результат: `tools\build\Release\wow_patch_cli.exe` — консольный патчер Wow.exe
(команды `diagnose`, `patch`, `recipe`, `apply-recipe`; синтаксис см. в
`docs/PATCH_FIRESTORM_12.1.0.md`, раздел 3).

---

## 8. Деплой: чтобы exe запускался на любом ПК

Собранный `WowDbStudio.exe` требует Qt-DLL рядом. Их автоматически копирует
утилита `windeployqt`:

```cmd
C:\Qt\6.11.2\msvc2022_64\bin\windeployqt.exe --release --compiler-runtime out\build\vs2022-x64\Release\WowDbStudio.exe
```

После этого всю папку `Release` можно переносить на другую машину.

### Про MySQL (вкладка «База данных»)

QMYSQL — это плагин Qt, загружаемый в рантайме, и ему нужна клиентская
библиотека MySQL/MariaDB:

- `windeployqt` скопирует `sqldrivers\qsqlmysql.dll`, если она есть в вашей установке Qt;
- дополнительно рядом с exe (или в PATH) нужна **libmysql.dll** из
  «MySQL Connector/C» той же разрядности (x64). Без неё при подключении будет
  ошибка `QMYSQL driver not loaded`.
- Если в вашей сборке Qt вообще нет `qsqlmysql.dll` (бывает у некоторых
  дистрибутивов) — плагин собирается из исходников Qt (qtbase → sqldrivers)
  с ключом `-DMySQL_ROOT=...`, либо используйте MariaDB Connector/C.

Если подключение к БД не нужно — этот пункт можно пропустить, приложение
работает и без него.

---

## 9. Частые ошибки

| Ошибка | Причина и лечение |
|---|---|
| `Could not find a package configuration file provided by "Qt6"` | CMake не видит Qt: не задан `QTDIR` (см. шаг 4) или не передан `-DCMAKE_PREFIX_PATH`. Проверьте путь — он должен указывать на каталог `...\msvc2022_64`. |
| `Found unsuitable Qt version "5.x"` | В PATH/QTDIR попал Qt5. Укажите путь к Qt6 явно в `-DCMAKE_PREFIX_PATH`. |
| `CMAKE_CXX_COMPILER could not be found` / нет генератора VS | Visual Studio без workload C++ (шаг 1) или вы в терминале, где не виден `cl`. Откройте «Developer PowerShell for VS 2022». |
| Ошибки компиляции про `std::format` / C++20 | Студия старой ревизии: обновите VS 2022 до актуальной (нужен полный C++20). |
| При запуске: `qt6widgets.dll is missing` | Запускаете exe вне Qt-окружения — выполните шаг 8 (`windeployqt`). |
| `QMYSQL driver not loaded` | Нет `libmysql.dll` рядом с exe — см. конец шага 8. |
| `cmake --preset` ругается на версию | CMake старше 3.21 — обновите (`winget install Kitware.CMake`). |

---

## 10. Кратко весь цикл одним блоком

```cmd
:: 1) VS 2022 C++ workload + CMake — см. шаг 1
:: 2) Qt 6 MSVC2022 64-bit — см. шаг 2
setx QTDIR "C:\Qt\6.11.2\msvc2022_64"
:: (переоткрыть терминал)
git clone https://github.com/Crendos/Test-Wow-project.git
cd Test-Wow-project\wow-db-studio
cmake --preset vs2022-x64
cmake --build --preset vs2022-x64-release
ctest --test-dir out/build/vs2022-x64 -C Release --output-on-failure
%QTDIR%\bin\windeployqt.exe --release --compiler-runtime out\build\vs2022-x64\Release\WowDbStudio.exe
start out\build\vs2022-x64\Release\WowDbStudio.exe
```
