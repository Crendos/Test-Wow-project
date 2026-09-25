#pragma once

#include <QString>
#include <QStringList>
#include <QByteArray>
#include <QVector>

// ---------------------------------------------------------------------------
// Патч клиента WoW.
//
// Целевой сценарий — «как Firestorm»: на диске появляется САМОСТОЯТЕЛЬНЫЙ
// пропатченный Wow.exe, который запускается двойным щелчком (без лаунчера),
// берёт портал из `SET portal` в WTF/Config.wtf и ходит на ваш bnetserver.
// Именно так устроен «WoW 11.2.5 - Firestorm.exe»: это обычная копия Wow.exe
// того же размера, в которой точечно заменены байты в секции данных.
//
// Метод изучен по Arctium Game Launcher 1.5.3 и сверен с
// wowemulation-dev/wow-patcher (MIT/Apache-2.0) и с исходниками TrinityCore
// master (src/server/game/Server/Packets/AuthenticationPackets.cpp).
//
// Что реально нужно клиенту 11.x/12.x (в т.ч. 12.1.0.69497), чтобы зайти на
// TrinityCore-сервер БЕЗ лаунчера:
//   1) ConnectTo RSA-модуль (256 байт, хранится в .rdata в little-endian) —
//      клиент проверяет им подпись SMSG_CONNECT_TO от worldserver.
//      В TrinityCore это ключ `ConnectToRSA` из AuthenticationPackets.cpp;
//      его модуль в LE = 5FD6800B...DCB3EE (проверено openssl'ем).
//   2) Ed25519-ключ (32 байта) — клиент проверяет им подпись
//      SMSG_ENTER_ENCRYPTED_MODE. В TrinityCore это
//      `EnterEncryptedModePrivateKey` (seed 08BDC7A3...), публичный ключ
//      = 02596F0D...BAFC69. БЕЗ него вход в мир обрывается на шифровании.
//   3) `SET portal "host[:port]"` в _retail_\WTF\Config.wtf — единственный
//      надёжный способ указать адрес; строковый слот `.actual.battle.net` в
//      exe — это СУФФИКС (клиент склеивает «eu» + «.actual.battle.net»),
//      поэтому в него нельзя писать «127.0.0.1:1119» целиком.
//   4) На сервере — стандартная dev-цепочка TrinityCore (CN=`*.*`), которая
//      лежит в TrinityCoreWin64vs/bnetserver.cert.pem.
//
// Правки в .text на диске бессмысленны: у ретейла код упакован (Arxan) и
// восстанавливается при запуске. Поэтому движок (src/wow_pe.*) принимает только
// сайты в секциях данных и честно сообщает, если сайг найден в коде.
// ---------------------------------------------------------------------------

struct WowPatchOptions {
    QString portal;                  // "host[:port]" из Config.wtf / вручную
    int port = 1119;                 // порт по умолчанию (как у battle.net)

    // --- портал ---
    // Правильный статический патч портала: заменить доменную часть суффикса
    // `.actual.battle.net` -> `.actual.<portalDomain>` (окно 10 байт).
    bool patchPortalSuffix = true;
    QString portalDomain;            // пусто = подобрать автоматически из portal
    // Legacy-режим: писать host:port во ВСЁ окно `.actual.battle.net\0`.
    // Ломает склейку с префиксом региона (получится «us127.0.0.1:1119»),
    // поэтому по умолчанию выключен и включается только осознанно.
    bool patchPortalWholeSlot = false;
    bool expandPortalBuffer = false; // окно под строкой портала (до 128 Б) — только если проверен билд

    // --- профиль/ключи ---
    bool autoDetect = true;          // автоподбор профиля/сигнатур по билду
    // Применять ли сигнатурные патчи (RSA/Ed25519/портал/URL). Выключается в
    // режиме «только рецепт», когда нужно перенести ровно чужие hunks.
    bool applySignaturePatches = true;
    bool bypassCertValidation = true;// фаза B (память): обход проверки сертификата
    bool writeConfigWtf = true;      // записать SET portal в WTF/Config.wtf
    bool patchLegacyGameCryptoRsa = true; // старые клиенты: RSA Crypto/Signature (профиль решает)
    bool requireEd25519 = true;      // 11.x/12.x: Ed25519 обязателен (иначе обрыв в мире)
    QString rsaModulusHex;           // пусто = TrinityCore dev-модуль (256 байт, LE)
    QString ed25519KeyHex;           // пусто = ключ TrinityCore (32 байта)
    QString rsaPrivatePemPath;       // PEM приватного ключа RSA -> модуль (LE)
    QString ed25519PemPath;          // PEM (PKCS8/SPKI/сертификат) -> публичный Ed25519
    bool keysAssumeBigEndian = false;// hex-ключ задан как в openssl (BE) -> развернуть

    // --- свой CDN / cert bundle ---
    bool patchVersionUrls = false;
    QString versionUrl;
    QString cdnsUrl;
    QString certBundleUrl;
    QString certBundlePath;          // подписанный bundle ({"Created":...) в слот 32761 байт
    bool patchLauncherRegistry = false;

    // --- запись результата ---
    bool makeBackup = true;          // .orig-бэкап при записи поверх существующего
    bool verifyAfterWrite = true;    // перечитать файл и проверить патчи
    bool fixChecksum = false;        // пересчитать PE CheckSum
    bool stripSignature = false;     // снять недействительную Authenticode-подпись
    bool launchAfterPatch = false;   // запустить результат

    // --- рецепты (перенос чужого патча, например Firestorm 11.2.5 -> 12.1.0) ---
    QStringList recipePaths;         // JSON-рецепты, применить до сигнатур

    // --- режим «память» ---
    int waitUnpackMs = 6000;
    bool checkTlsBeforeLaunch = false;
    QString extraArgs;
};

// Результат автоопределения (для UI и лога).
struct WowDetection {
    QString version;        // "12.1.0.69497" или пусто
    int build = 0;          // 69497
    QString branch;         // "Retail" / "Classic" / ... / "Unknown"
    QString profile;        // "modern12" / "modern" / "legacy" / "classic13" / "legion"
    bool legacyCert = false;// профиль с Signature/Crypto RSA + runtime-обходом сертификата
    bool usesEd25519 = true;
    bool standaloneDiskPatchOk = true; // статический патч на диске для этого профиля рабочий
    QString note;
};

// Сводка по одной секции PE.
struct WowSectionInfo {
    QString name;
    quint32 virtualAddress = 0;
    quint32 virtualSize = 0;
    quint32 rawPointer = 0;
    quint32 rawSize = 0;
    quint32 characteristics = 0;
    bool diskPatchable = false;
};

// Отчёт «Диагностика» — всё, что нужно знать о конкретном Wow.exe.
struct WowInspect {
    bool ok = false;
    QString error;
    QString path;
    qint64 size = 0;
    QString sha256;
    QString version;
    QString buildInfoVersion;   // из .build.info в корне установки, если есть
    WowDetection detection;

    bool pe64 = false;
    quint64 imageBase = 0;
    quint32 entryRva = 0;
    quint32 sizeOfImage = 0;
    quint32 checksum = 0;
    bool hasSignature = false;
    quint32 certPointer = 0;
    quint32 certSize = 0;
    QVector<WowSectionInfo> sections;

    QString dataDir;            // найденная папка Data
    bool dataDirFound = false;
    QString configWtfPath;      // найденный Config.wtf
    QString configPortal;       // текущий SET portal (если есть)

    bool blizzardRsaFound = false;   // стоит ли ещё родной ключ Blizzard
    bool trinityRsaFound = false;    // стоит ли уже ключ TrinityCore (= уже пропатчен)
    bool blizzardEdFound = false;
    bool trinityEdFound = false;
    bool portalFound = false;
    bool certBundleSlotFound = false;

    QStringList lines;          // готовый человекочитаемый отчёт
};

// Один применённый/найденный патч — для отчёта и верификации.
struct WowPatchRecord {
    QString id;
    QString label;
    qint64 offset = -1;         // -1 = не применён
    QString section;
    int size = 0;
    bool applied = false;
    QString note;
};

// Результат статического патча.
struct WowPatchReport {
    bool ok = false;
    QString error;
    QString destination;
    QString sha256;
    qint64 size = 0;
    QVector<WowPatchRecord> records;
    QStringList verification;   // строки проверок после записи
    bool verifiedClean = false; // все обязательные проверки прошли
    QString configWtfPath;
    QString portalWritten;
};

class ClientPatchService final {
public:
    // -----------------------------------------------------------------------
    // ОСНОВНОЙ РЕЖИМ: самостоятельный пропатченный exe на диске
    // («Firestorm-стиль»). Оригинал не изменяется, если destination != source;
    // при записи поверх делается бэкап (opts.makeBackup).
    // -----------------------------------------------------------------------
    static WowPatchReport patchStandalone(const QString &source, const QString &destination,
                                          const WowPatchOptions &opts, QStringList *log);

    // Куда по умолчанию сохранять результат: рядом с исходником, с тем же
    // именем (клиент ищет Data относительно себя), суффикс не добавляем.
    static QString defaultOutputPath(const QString &exePath);
    // Копия рядом с оригиналом: Wow.exe -> Wow.patched.exe (иначе не найдёт
    // свои Data/WTF). Для режима «патч в копию, оригинал не трогать».
    static QString copyOutputPath(const QString &exePath);

    // Диагностика: PE, секции, все известные сайты, Data, Config.wtf, версия.
    static WowInspect inspectExecutable(const QString &exePath);

    // -----------------------------------------------------------------------
    // Рецепт: снять точечную разницу «оригинал того же билда» vs «уже
    // пропатченный клиент» (например оригинальный Wow.exe 11.2.5 и
    // «WoW 11.2.5 - Firestorm.exe») и сохранить в JSON. Рецепт хранит контекст,
    // поэтому переносится на другой билд (12.1.0.69497) поиском по контексту.
    // -----------------------------------------------------------------------
    static bool buildRecipe(const QString &originalExe, const QString &patchedExe,
                            const QString &outJsonPath, const WowPatchOptions &opts,
                            QStringList *log, QString *error);

    // Применить рецепт(ы) к exe. Используется и внутри patchStandalone
    // (opts.recipePaths), и отдельной кнопкой.
    static bool applyRecipe(const QString &source, const QString &destination,
                            const QStringList &recipePaths, QStringList *log, QString *error);

    // -----------------------------------------------------------------------
    // Старые/вспомогательные режимы (сохранены для совместимости)
    // -----------------------------------------------------------------------
    // Консервативный режим: ASCII-замена в копии файла.
    static bool replaceAsciiInCopy(const QString &source, const QString &destination,
                                   const QString &find, const QString &replacement, QString *error);

    // Прежняя «копия на диск» — теперь это тонкая обёртка над patchStandalone.
    static bool patchFileCopy(const QString &source, const QString &destination,
                              const WowPatchOptions &opts, QStringList *log, QString *error);

    // Патч в памяти: CreateProcess(CREATE_SUSPENDED) -> PEB -> скан памяти ->
    // фаза A (данные) -> фаза B (обход сертификата). Только Windows.
    static bool patchAndLaunch(const QString &exePath, const WowPatchOptions &opts,
                               QStringList *log, QString *error);

    // Запустить готовый exe (без патчей) — для проверки результата.
    static bool launchClient(const QString &exePath, const QString &extraArgs,
                             QStringList *log, QString *error);

    // -----------------------------------------------------------------------
    // Определение клиента
    // -----------------------------------------------------------------------
    static WowDetection detectProfile(const QString &exePath);
    static QString clientDataDir(const QString &exePath, bool *existsOut = nullptr);
    static QString clientVersion(const QString &exePath);
    // Версия из .build.info в корне установки (на два уровня выше _retail_).
    static QString buildInfoVersion(const QString &exePath);

    // -----------------------------------------------------------------------
    // Ключи
    // -----------------------------------------------------------------------
    static bool checkPortalTls(const QString &host, int port, QStringList *errors);
    static QString readPortalFromConfigWtf(const QString &exePath, QString *error);
    static bool writePortalToConfigWtf(const QString &exePath, const QString &portal, QString *error);
    static QString ed25519KeyFromPem(const QString &pemPath, QString *error);
    static QByteArray hexKey(const QString &hex, int expectedBytes, QString *error);
    // Модуль RSA (little-endian, как лежит в клиенте) из PEM: годится и
    // приватный ключ (PKCS#1/PKCS#8), и сертификат, и публичный ключ.
    static QByteArray rsaModulusLeFromPem(const QString &pemPath, QString *error);
    // Развернуть порядок байт, если ключ вставили в формате openssl (BE).
    static QByteArray normalizeKeyOrder(const QByteArray &key, bool assumeBigEndian, QString *note);
    // Самопроверка: совпадают ли встроенные ключи с константами TrinityCore master.
    static bool keysSelfTest(QStringList *log);
    // Ключи TrinityCore, которыми прошит бинарник (LE, 256 и 32 байта).
    static QByteArray trinityRsaModulusLe();
    static QByteArray trinityEd25519PublicKey();
    // Родные ключи Blizzard (первые 8 байт — это и есть сигнатуры поиска).
    static QByteArray blizzardRsaSignature();
    static QByteArray blizzardEd25519Signature();

    // Краткое описание метода для вкладки.
    static QString methodSummary();
};
