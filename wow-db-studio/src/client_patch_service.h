#pragma once

#include <QString>
#include <QStringList>
#include <QByteArray>

// Патч клиента WoW по методу Arctium Game Launcher (изучен и задокументирован
// в docs/ANALYSIS_RU.md корня репозитория; сверен с wowemulation-dev/wow-patcher,
// MIT/Apache-2.0):
//  1) автоопределение ветки/билда клиента и выбор набора сигнатур (профиля);
//  2) патч данных в копии файла (диск) или в памяти запущенного процесса;
//  3) для локального запуска / своего IP можно патчить portal на 127.0.0.1,
//     localhost или IP:порт (в пределах слота 19 байт), а также включить
//     обход проверки сертификата (runtime-патчи фаз B) под самоподписанный
//     сертификат вашего bnet-сервера.

struct WowPatchOptions {
    QString portal;                  // "host[:port]" из Config.wtf / вручную
    int port = 1119;                 // порт по умолчанию (как у battle.net)
    bool expandPortalBuffer = false; // окно под строкой портала (до 128 Б) — только если проверен билд
    bool autoDetect = true;          // автоподбор профиля/сигнатур по билду
    bool bypassCertValidation = true;// фаза B: обход проверки сертификата (локально/свой IP)
    bool writeConfigWtf = true;      // записать SET portal в WTF/Config.wtf перед запуском
    bool patchLegacyGameCryptoRsa = true; // старые клиенты: RSA Crypto/Signature (профиль решает)
    bool patchVersionUrls = false;   // подмена version/CDN URL (свой CDN / старый патч)
    QString versionUrl;              // например http://ngdp.arctium.io/...
    QString cdnsUrl;                 // например http://ngdp.arctium.io/customs/wow/cdns
    QString certBundleUrl;           // опционально: URL бандла сертификатов (nydus)
    QString rsaModulusHex;           // пусто = TrinityCore dev-модуль (256 байт)
    QString ed25519KeyHex;           // пусто = ключ из сертификата 7725530 (32 байта)
    int waitUnpackMs = 6000;         // ожидание расшифровки .text (фаза B), мс
    bool checkTlsBeforeLaunch = false;
    QString extraArgs;
};

// Результат автоопределения (для UI и лога).
struct WowDetection {
    QString version;        // "10.2.7.53134" или пусто
    QString branch;         // "Retail" / "Classic" / "Classic Era" / "Anniversary" / "Titan" / "Unknown"
    QString profile;        // "modern" / "legacy" / "classic13" / "manual"
    bool legacyCert = false;// профиль с Signature/Crypto RSA + runtime-обходом сертификата
    bool usesEd25519 = true;
    QString note;
};

class ClientPatchService final {
public:
    // Существующий консервативный режим: ASCII-замена в копии файла.
    static bool replaceAsciiInCopy(const QString &source, const QString &destination,
                                   const QString &find, const QString &replacement, QString *error);

    // Патч по сигнатурам в КОПИИ файла (диск). Оригинал не изменяется.
    static bool patchFileCopy(const QString &source, const QString &destination,
                              const WowPatchOptions &opts, QStringList *log, QString *error);

    // Патч в памяти: CreateProcess(CREATE_SUSPENDED) -> PEB -> скан памяти ->
    // фаза A (данные) -> фаза B (обход сертификата, если профиль позволяет).
    // Файл на диске не изменяется. Только Windows.
    static bool patchAndLaunch(const QString &exePath, const WowPatchOptions &opts,
                               QStringList *log, QString *error);

    // Автоподбор: ветка по пути, версия по VersionInfo, профиль по диапазону билдов.
    static WowDetection detectProfile(const QString &exePath);

    // Папка Data рядом с exe (та же директория, напр. new/wow/Data).
    // existsOut (опц.) = существует ли каталог; возвращается путь без проверки.
    static QString clientDataDir(const QString &exePath, bool *existsOut = nullptr);

    // TLS-проверка сертификата сервера перед запуском.
    static bool checkPortalTls(const QString &host, int port, QStringList *errors);

    // Читает SET portal "host:port" из WTF/Config.wtf рядом с exe.
    static QString readPortalFromConfigWtf(const QString &exePath, QString *error);

    // Записывает/перезаписывает SET portal в WTF/Config.wtf (резервная копия .bak).
    static bool writePortalToConfigWtf(const QString &exePath, const QString &portal, QString *error);

    // Версия клиента (FileVersionInfo, с учётом билдов > 65535).
    static QString clientVersion(const QString &exePath);

    // Извлечение 32-байтного Ed25519-ключа из PEM-сертификата (SPKI, BIT STRING 0x20).
    static QString ed25519KeyFromPem(const QString &pemPath, QString *error);

    // Шестнадцатеричный ключ (512/64 hex) -> QByteArray; ошибка если длина неверна.
    static QByteArray hexKey(const QString &hex, int expectedBytes, QString *error);

    // Краткое описание метода для вкладки.
    static QString methodSummary();
};
