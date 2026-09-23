#pragma once

// ---------------------------------------------------------------------------
// wow_patch_core.h — вся ЛОГИКА патча Wow.exe, чистый C++20, БЕЗ Qt.
//
// Зачем отдельным слоем:
//   * Qt-слой (client_patch_service.cpp) превращается в тонкую обёртку: чтение
//     файлов, строки, диалоги. Арифметика смещений, ключи, рецепты и проверки
//     живут здесь и проверяются тестами без Qt (tools/wow_patch_selftest.cpp).
//   * тот же код собирается в консольный патчер (tools/wow_patch_cli.cpp)
//     обычным g++/cl — Qt для патча не нужен вообще.
//
// Ядро работает ТОЛЬКО с буфером байт: прочитать/записать файл — задача вызывающей
// стороны. Это делает его полностью тестируемым.
//
// Смысл патча («Firestorm-стиль»): на диске появляется самостоятельный Wow.exe,
// который запускается без лаунчера и ходит на ваш TrinityCore-сервер. Для этого
// в СЕКЦИИ ДАННЫХ точечно заменяются:
//   1) ConnectTo RSA-модуль (256 байт, little-endian) — им клиент проверяет
//      подпись SMSG_CONNECT_TO. Ключ TrinityCore: ConnectToRSA из
//      src/server/game/Server/Packets/AuthenticationPackets.cpp.
//   2) Ed25519-ключ (32 байта) — им клиент проверяет подпись
//      SMSG_ENTER_ENCRYPTED_MODE (публичная половина
//      EnterEncryptedModePrivateKey). Без него вход в мир обрывается.
//   3) суффикс портала `.actual.battle.net` -> `.actual.<домен>` (окно 10 байт).
//      Именно СУФФИКС: клиент склеивает «eu» + суффикс, поэтому писать сюда
//      «127.0.0.1:1119» целиком нельзя.
// Адрес сервера при этом задаётся строкой `SET portal "host:port"` в
// WTF/Config.wtf — см. configPortal()/writePortalIntoConfig().
//
// .text на диске не правим: у ретейла код упакован (Arxan/Digital.ai) и
// восстанавливается при запуске.
// ---------------------------------------------------------------------------

#include "wow_pe.h"

#include <cstdint>
#include <string>
#include <vector>

namespace wowpatch {

using wowpe::Bytes;
using wowpe::Pattern;

// ---------------------------------------------------------------------------
// Ключи и сигнатуры (константы, сверенные с TrinityCore master)
// ---------------------------------------------------------------------------
// ConnectToRSA, модуль в little-endian (256 байт) — как лежит в клиенте.
const Bytes &trinityRsaModulusLe();
// Публичный ключ Ed25519 (32 байта), соответствующий EnterEncryptedModePrivateKey.
const Bytes &trinityEd25519PublicKey();
// Приватный сид Ed25519 из TrinityCore — только для самопроверки.
const Bytes &trinityEd25519Seed();
// Тот же RSA-модуль в big-endian (как печатает `openssl rsa -modulus`).
const Bytes &trinityRsaModulusBe();

// Родные ключи Blizzard. Первые 8 байт каждого — это и есть сигнатура поиска:
// клиент хранит ключ целиком, и начало ключа уникально в образе.
const Bytes &blizzardRsaSignature();        // ConnectTo (8 байт, little-endian — LSB модуля)
const Bytes &blizzardRsaSignatureBe();      // те же 8 байт в обратном порядке: слот, если модуль
                                            // хранится big-endian (MSB первым)
const Bytes &blizzardSignatureModulusSig(); // Signature/GameCrypto (8 байт)
const Bytes &blizzardEd25519Signature();    // GameCrypto Ed25519 (8 байт)
const Bytes &portalSuffixPattern();         // ".actual.battle.net"
const Bytes &launcherLoginPattern();        // ключ реестра Launch Options
const Bytes &certBundleUrlPattern();        // nydus.battle.net fingerprint

// Самопроверка: встроенные LE-константы — это действительно разворот BE-модуля
// и действительно публичная половина сида TrinityCore (по структуре ключей).
bool keysSelfTest(std::vector<std::string> *log);

// ---------------------------------------------------------------------------
// Профиль клиента
// ---------------------------------------------------------------------------
struct Detection {
    std::string version;      // "12.1.0.69497" или пусто
    int         build = 0;    // 69497
    std::string branch;       // "Retail" / "Classic" / ... / "Unknown"
    std::string profile;      // "modern12" / "modern" / "legacy" / "classic13" / "legion"
    std::string note;
    bool legacyCert = false;              // профиль с Signature/Crypto RSA + runtime-обходом
    bool usesEd25519 = true;              // 11.x/12.x — да
    bool standaloneDiskPatchOk = true;    // статический патч для профиля рабочий
};

// version может быть пустым — тогда профиль определяется по пути установки,
// по размеру файла и по тому, какие ключи в образе уже стоят.
Detection detectProfile(const Bytes &data, const wowpe::Image &img,
                        const std::string &version, const std::string &pathHint);

// "12.1.0.69497" -> 69497 (0, если не разобрано).
int  buildFromVersion(const std::string &version);
bool parseVersion(const std::string &text, int *a, int *b, int *c, int *build);

// ---------------------------------------------------------------------------
// Опции патча
// ---------------------------------------------------------------------------
struct Options {
    // --- портал ---
    std::string portal;                 // "host[:port]"
    int         port = 1119;            // порт по умолчанию
    bool patchPortalSuffix = true;      // .actual.battle.net -> .actual.<домен>
    std::string portalDomain;           // пусто = вывести из portal
    bool patchPortalWholeSlot = false;  // legacy: host:port во всё окно суффикса
    // Если домен не выводится (портал — IP) или не влезает в окно суффикса,
    // подставляем этот запасной домен. Смысл: клиент даже с пустым Config.wtf
    // не уйдёт на Blizzard, а реальный адрес всё равно берётся из SET portal.
    // Пустая строка = не трогать суффикс вообще.
    std::string portalFallbackDomain = "localhost";
    bool expandPortalBuffer = false;    // окно под строкой портала (до 128 Б)

    // --- профиль/ключи ---
    bool autoDetect = true;
    bool applySignaturePatches = true;  // false = только hunks из рецепта
    bool patchLegacyGameCryptoRsa = true;
    bool requireEd25519 = true;
    Bytes rsaModulus;                   // пусто = trinityRsaModulusLe()
    Bytes ed25519Key;                   // пусто = trinityEd25519PublicKey()
    bool keysAssumeBigEndian = false;   // ключ задан как в openssl -> развернуть

    // --- свой CDN / cert bundle / реестр лаунчера ---
    bool patchVersionUrls = false;
    std::string versionUrl;
    std::string cdnsUrl;
    std::string certBundleUrl;
    Bytes certBundle;                   // подписанный bundle в слот 32761 байт
    bool patchLauncherRegistry = false;

    // --- постобработка образа ---
    bool fixChecksum = false;           // пересчитать PE CheckSum
    bool stripSignature = false;        // снять недействительную Authenticode-подпись
    bool verify = true;                 // перепроверить результат по буферу

    // --- рецепты (перенос чужого патча) ---
    std::vector<std::string> recipePaths;
};

// ---------------------------------------------------------------------------
// Отчёт
// ---------------------------------------------------------------------------
struct Record {
    std::string id;
    std::string label;
    std::int64_t offset = -1;           // -1 = не применён
    std::string section;
    std::size_t size = 0;
    bool applied = false;
    bool mandatory = false;
    std::string note;
    Bytes expected;                     // что именно записали — для перепроверки
};

struct Report {
    bool ok = false;
    std::string error;
    std::int64_t originalSize = 0;
    std::int64_t patchedSize = 0;
    bool signatureStripped = false;     // размер вправе уменьшиться
    std::vector<Record> records;
    std::vector<std::string> verification;
    bool verifiedClean = false;
    int applied = 0;
    int skipped = 0;
    int missedMandatory = 0;
    std::uint32_t checksumOld = 0;
    std::uint32_t checksumNew = 0;
    std::string sha256;                 // итогового образа (если verify)
    std::string portalWritten;          // строка SET portal, которую надо записать
    bool alreadyPatched = false;        // ключи TrinityCore стояли ещё до патча
};

// ---------------------------------------------------------------------------
// Шаг патча: найти сигнатуру -> записать значение
// ---------------------------------------------------------------------------
struct Step {
    std::string id;
    std::string label;
    Pattern pattern;
    Bytes value;
    int  slotSize = 0;      // 0 = писать значение целиком (ключи), >0 = окно фикс. длины
    bool mandatory = false;
    std::string note;
};

// Собирает план по опциям и образу (включая hunks из рецептов, если заданы).
std::vector<Step> buildPlan(const Options &opts, const Detection &det,
                            const wowpe::Image &img, const Bytes &data,
                            std::vector<std::string> *log);

// Полный цикл в памяти: разобрать PE -> план -> записать -> checksum/подпись ->
// проверить. Файл не трогает.
bool patchImage(Bytes &data, const Options &opts, const Detection &det,
                Report *report, std::vector<std::string> *log);

// Занятый диапазон: два шага (сигнатура и hunk рецепта) могут указать на одно
// место — тогда второй пропускается, а не перезаписывает первый.
struct UsedRange {
    std::int64_t from = 0;
    std::int64_t to = 0;
    std::string what;
};

// AlreadyApplied — нужное значение УЖЕ стоит в секции данных (файл пропатчен
// раньше или его закрыл hunk рецепта). Это успех, а не ошибка: иначе повторный
// запуск патча и комбинация «рецепт + сигнатуры» всегда падали бы.
enum class StepResult { Applied, NotFound, Occupied, WriteFailed, AlreadyApplied };

// Применить один шаг: найти сигнатуру в секции данных и записать значение.
StepResult applyStep(Bytes &data, const wowpe::Image &img, const Step &st,
                     Record *rec, std::vector<UsedRange> *used,
                     std::vector<std::string> *log);

// Перепроверка уже записанного образа: каждый обязательный патч на месте,
// родных ключей Blizzard не осталось, PE-заголовок цел.
bool verifyImage(const Bytes &data, const wowpe::Image &img, const Report &rep,
                 std::vector<std::string> *out);

// ---------------------------------------------------------------------------
// Портал
// ---------------------------------------------------------------------------
// "127.0.0.1:1119" -> "127.0.0.1"; "eu.actual.battle.net" -> "eu.actual.battle.net".
std::string portalHostOnly(const std::string &portal);
// Домен для суффиксного патча: из "logon.myserver.com" -> "myserver.com" (<=10 байт).
std::string derivePortalDomain(const std::string &portal, std::string *note);
// Нормализация введённого пользователем хоста.
std::string normalizePortalHost(const std::string &portal);
// Значение для слота суффикса: ".actual." + домен, добитое NUL до длины окна.
Bytes portalSuffixValue(const std::string &portal, std::size_t windowSize);
// Значение для legacy-режима: host:port целиком, добитое NUL.
Bytes portalWholeSlotValue(const std::string &portal, int port, std::size_t windowSize);

// ---------------------------------------------------------------------------
// Config.wtf (работа с текстом, без файлового ввода)
// ---------------------------------------------------------------------------
std::string configPortal(const std::string &configText);       // "" если нет
// Записать/обновить `SET portal "..."`. Возвращает true, если текст изменился.
bool writePortalIntoConfig(std::string &configText, const std::string &portal);

// ---------------------------------------------------------------------------
// Ключи из PEM (мини-DER парсер: PKCS#1, PKCS#8, SPKI, сертификат X.509)
// ---------------------------------------------------------------------------
// Модуль RSA в little-endian (как лежит в клиенте) из PEM-текста.
Bytes rsaModulusLeFromPem(const std::string &pemText, std::string *error);
// Публичный ключ Ed25519 (32 байта) из PEM-текста.
Bytes ed25519PublicFromPem(const std::string &pemText, std::string *error);
// Развернуть порядок байт, если ключ вставлен в формате openssl (BE).
Bytes normalizeKeyOrder(const Bytes &key, bool assumeBigEndian, std::string *note);

// ---------------------------------------------------------------------------
// Рецепт: точечная разница «оригинал билда» vs «уже пропатченный клиент»
// ---------------------------------------------------------------------------
struct RecipeMeta {
    std::string name;
    std::string sourceOriginal;
    std::string sourcePatched;
    std::string originalSha256;
    std::string patchedSha256;
    std::string createdUtc;
    std::string originalVersion;
    std::string patchedVersion;
    int formatVersion = 1;
};

struct Recipe {
    RecipeMeta meta;
    std::vector<wowpe::Hunk> hunks;
};

// Снять разницу двух образов ОДНОГО билда.
bool makeRecipe(const Bytes &original, const Bytes &patched, const RecipeMeta &meta,
                Recipe *out, std::vector<std::string> *log, std::string *error);

// JSON (свой мини-сериализатор, чтобы не тянуть Qt в ядро).
std::string recipeToJson(const Recipe &r);
bool recipeFromJson(const std::string &text, Recipe *out, std::string *error);

// Применить рецепт к образу: каждый hunk ищется по контексту, а не по смещению.
// Возвращает число перенесённых hunks; подробности — в log и в Report.
int applyRecipeToImage(Bytes &data, const wowpe::Image &img, const Recipe &r,
                       Report *rep, std::vector<std::string> *log);

// ---------------------------------------------------------------------------
// Диагностика образа (для отчёта в UI/CLI)
// ---------------------------------------------------------------------------
struct SectionInfo {
    std::string name;
    std::uint32_t virtualAddress = 0;
    std::uint32_t virtualSize = 0;
    std::uint32_t rawPointer = 0;
    std::uint32_t rawSize = 0;
    std::uint32_t characteristics = 0;
    bool diskPatchable = false;
};

struct Probe {
    std::string name;
    Bytes pattern;
    std::size_t expect = 0;   // ожидаемая длина значения (для справки)
};

// Все сайты, которые патчер знает.
std::vector<Probe> knownProbes();

struct Inspect {
    bool ok = false;
    std::string error;
    std::int64_t size = 0;
    std::string sha256;
    Detection detection;

    bool pe64 = false;
    std::uint64_t imageBase = 0;
    std::uint32_t entryRva = 0;
    std::uint32_t sizeOfImage = 0;
    std::uint32_t machine = 0;
    std::uint32_t checksum = 0;
    std::uint32_t numberOfSections = 0;
    bool hasSignature = false;
    std::uint32_t certPointer = 0;
    std::uint32_t certSize = 0;
    std::vector<SectionInfo> sections;

    bool blizzardRsaFound = false;
    bool trinityRsaFound = false;
    bool blizzardEdFound = false;
    bool trinityEdFound = false;
    bool portalFound = false;
    bool launcherFound = false;
    bool certBundleSlotFound = false;

    std::vector<std::string> lines;     // готовый человекочитаемый отчёт
};

Inspect inspect(const Bytes &data, const std::string &version, const std::string &pathHint);

} // namespace wowpatch
