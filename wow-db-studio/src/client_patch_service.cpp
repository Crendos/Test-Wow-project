#include "client_patch_service.h"

#include "wow_patch_core.h"
#include "wow_pe.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSslCertificate>
#include <QSslSocket>
#include <QStringList>
#include <QThread>

#include <algorithm>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <string>
#include <vector>

#ifdef Q_OS_WIN
#  include <windows.h>
#  include <psapi.h>
#  include <winver.h>
#endif

// ---------------------------------------------------------------------------
// Этот файл — ТОНКАЯ ОБЁРТКА над ядром src/wow_patch_core.* (чистый C++20).
//
// Всё, где легко ошибиться и дорого исправить (PE-смещения, секции, ключи,
// портал, рецепты, верификация), живёт в ядре и покрыто тестами
// tools/wow_patch_selftest.cpp, которые собираются БЕЗ Qt. Здесь остаются:
//   * файловый ввод-вывод и пути (QFile/QDir/QSaveFile);
//   * строки и отчёты для UI (QString/QStringList);
//   * то, что умеет только Qt: TLS-проверка сертификата (QSslSocket);
//   * то, что умеет только Windows: версия из ресурса exe и патч в памяти.
//
// Правило: никакой арифметики смещений и никаких hex-констант ключей здесь не
// появляется — только вызовы wowpatch::*.
// ---------------------------------------------------------------------------

namespace {

// ===========================================================================
// Мосты Qt <-> ядро
// ===========================================================================
wowpatch::Bytes toCore(const QByteArray &a) {
    return wowpatch::Bytes(reinterpret_cast<const quint8 *>(a.constData()),
                           reinterpret_cast<const quint8 *>(a.constData()) + a.size());
}
QByteArray fromCore(const wowpatch::Bytes &b) {
    return QByteArray(reinterpret_cast<const char *>(b.data()), int(b.size()));
}
QString qstr(const std::string &s) { return QString::fromStdString(s); }
std::string sstr(const QString &s) { return s.toStdString(); }

QStringList qstrList(const std::vector<std::string> &v) {
    QStringList out;
    out.reserve(int(v.size()));
    for (const std::string &s : v) out.append(QString::fromStdString(s));
    return out;
}

WowDetection toQt(const wowpatch::Detection &d) {
    WowDetection r;
    r.version = qstr(d.version);
    r.build = d.build;
    r.branch = qstr(d.branch);
    r.profile = qstr(d.profile);
    r.note = qstr(d.note);
    r.legacyCert = d.legacyCert;
    r.usesEd25519 = d.usesEd25519;
    r.standaloneDiskPatchOk = d.standaloneDiskPatchOk;
    return r;
}

WowSectionInfo toQt(const wowpatch::SectionInfo &s) {
    WowSectionInfo r;
    r.name = qstr(s.name);
    r.virtualAddress = s.virtualAddress;
    r.virtualSize = s.virtualSize;
    r.rawPointer = s.rawPointer;
    r.rawSize = s.rawSize;
    r.characteristics = s.characteristics;
    r.diskPatchable = s.diskPatchable;
    return r;
}

WowPatchRecord toQt(const wowpatch::Record &r) {
    WowPatchRecord out;
    out.id = qstr(r.id);
    out.label = qstr(r.label);
    out.offset = qint64(r.offset);
    out.section = qstr(r.section);
    out.size = int(r.size);
    out.applied = r.applied;
    out.note = qstr(r.note);
    return out;
}

// ===========================================================================
// Файловый ввод-вывод
// ===========================================================================
QByteArray readFileBytes(const QString &path, QString *error) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("Не удалось открыть %1: %2").arg(path, f.errorString());
        return QByteArray();
    }
    QByteArray b = f.readAll();
    f.close();
    if (b.isEmpty()) {
        if (error) *error = QStringLiteral("Файл пуст: %1").arg(path);
        return QByteArray();
    }
    return b;
}

bool writeFileBytes(const QString &path, const wowpatch::Bytes &data, QString *error) {
    const QDir dir = QFileInfo(path).absoluteDir();
    if (!dir.exists() && !QDir().mkpath(dir.absolutePath())) {
        if (error) *error = QStringLiteral("Не удалось создать папку %1").arg(dir.absolutePath());
        return false;
    }
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly)) {
        if (error) *error = QStringLiteral("Не удалось создать %1: %2").arg(path, out.errorString());
        return false;
    }
    if (!data.empty() &&
        out.write(reinterpret_cast<const char *>(data.data()), qint64(data.size())) != qint64(data.size())) {
        if (error) *error = QStringLiteral("Не удалось записать %1: %2").arg(path, out.errorString());
        out.cancelWriting();
        return false;
    }
    if (!out.commit()) {
        if (error) *error = QStringLiteral("Не удалось сохранить %1: %2").arg(path, out.errorString());
        return false;
    }
    return true;
}

bool writeTextFile(const QString &path, const QString &text, QString *error) {
    const QDir dir = QFileInfo(path).absoluteDir();
    if (!dir.exists()) QDir().mkpath(dir.absolutePath());
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) *error = QStringLiteral("Не удалось создать %1: %2").arg(path, out.errorString());
        return false;
    }
    out.write(text.toUtf8());
    if (!out.commit()) {
        if (error) *error = QStringLiteral("Не удалось сохранить %1: %2").arg(path, out.errorString());
        return false;
    }
    return true;
}

QString hex(const QByteArray &b, int maxBytes = 0) {
    const QByteArray v = (maxBytes > 0 && b.size() > maxBytes) ? b.left(maxBytes) : b;
    return QString::fromLatin1(v.toHex(' ').toUpper()) +
           ((maxBytes > 0 && b.size() > maxBytes) ? QStringLiteral(" …") : QString());
}

// ===========================================================================
// Константы для режима «память» (Windows). Значения берутся из ядра — это
// единственный источник правды, сюда они просто конвертируются в QByteArray.
// ===========================================================================
const QByteArray kPatchRsaModulus          = fromCore(wowpatch::trinityRsaModulusLe());
const QByteArray kPatchEd25519Key          = fromCore(wowpatch::trinityEd25519PublicKey());
const QByteArray kPatternConnectToModulus  = fromCore(wowpatch::blizzardRsaSignature());
const QByteArray kPatternSignatureModulus  = fromCore(wowpatch::blizzardSignatureModulusSig());
const QByteArray kPatternGameCryptoEd25519 = fromCore(wowpatch::blizzardEd25519Signature());
const QByteArray kPatternGameCryptoRsa     = QByteArray::fromHex("71FDFA60140DF205");
const QByteArray kPatternPortalSuffix      = fromCore(wowpatch::portalSuffixPattern());
const QByteArray kPatternPortal            = kPatternPortalSuffix + '\0';
const QByteArray kPatternLauncherLogin     = fromCore(wowpatch::launcherLoginPattern());
const QByteArray kPatternCertBundleUrl     = fromCore(wowpatch::certBundleUrlPattern());
const QByteArray kPatchLauncherLogin =
    QByteArray("Software\\Arctium WoW Launcher\\Battle.net\\Launch Options\\");

const QByteArray kUrlV1    = "http://%s.patch.battle.net:1119/%s/versions";
const QByteArray kUrlV2    = "https://%s.version.battle.net/v2/products/%s/versions";
const QByteArray kUrlV2New = "https://%s.version.battle.net/v2/products/%s/%s";
const QByteArray kCdnsUrl  = "http://%s.patch.battle.net:1119/%s/cdns";

// ===========================================================================
// Runtime-паттерны режима «память»: они ищутся в .text ПОСЛЕ расшифровки Arxan,
// поэтому содержат wildcard'ы (-1). Для статического патча на диске не
// используются — там работают только сигнатуры данных из ядра.
// ===========================================================================
using Pattern = wowpe::Pattern;
Pattern pat(std::initializer_list<std::int16_t> v) { return Pattern(v.begin(), v.end()); }

#ifdef Q_OS_WIN
// Сторож: `mov dword ptr [rip+disp32], 1` после TLS-колбэка Arxan.
const Pattern kPatInit = pat({
    0xC7, 0x05, -1,-1,-1,-1, 0x01,0x00,0x00,0x00, 0x48,0x8D, -1,-1,-1,-1,-1, 0x48,
    0x8D, -1,-1,-1,-1,-1, 0xE8, -1,-1,-1,-1, 0x85 });
const Pattern kPatIntegrity = pat({
    0x44,0x89,-1,0x24,-1, 0x44,0x89,-1,0x24,-1, 0x89,-1,0x24,-1, 0x48,0x89,-1,0x24,-1,
    0x53,0x56,0x57 });
const Pattern kPatIntegrityAlt = pat({
    0x44,0x89,-1,0x24,-1, 0x44,0x89,-1,0x24,-1, 0x89,-1,0x24,-1, 0x48,0x89,-1,0x24,-1,
    0x53,0x57 });
const Pattern kPatCertBundleBranch = pat({ 0x75,0x06, 0x48,-1,-1, 0x60,0x5F,0xC3 });
const Pattern kPatCertCommonName    = pat({ 0x80,-1,0x2A, 0x75,-1, 0x32,0xC0,0x48 });
const Pattern kPatCertChain         = pat({ 0x32,0xDB,0xEB,0x02, 0xB3,0x01, 0x48,0x83,-1,-1, 0x00,0x00,0x00,0x00 });
#endif

// ===========================================================================
// Портал (Qt-версия нужна режиму «память»; логика та же, что в ядре)
// ===========================================================================
QString normalizePortalHost(QString text) {
    return qstr(wowpatch::normalizePortalHost(sstr(text)));
}

QString paddedPortal(const QString &portal, int defaultPort, QByteArray *out, qsizetype maxLen) {
    QString text = normalizePortalHost(portal);
    if (text.isEmpty()) return QString();
    auto tryFit = [&](const QString &candidate) -> bool {
        const QByteArray value = candidate.toUtf8() + '\0';
        if (value.size() > maxLen) return false;
        *out = value;
        return true;
    };
    if (text.contains(QLatin1Char(':'))) {
        if (tryFit(text)) return QString();
        const int colon = text.lastIndexOf(QLatin1Char(':'));
        if (colon > 0 && tryFit(text.left(colon))) return QString();
    } else {
        if (tryFit(text + QStringLiteral(":%1").arg(defaultPort))) return QString();
        if (tryFit(text)) return QString();
    }
    return QStringLiteral("Портал «%1» не влезает в слот %2 байт (с НУЛ). "
                          "Бинарный патч портала будет пропущен — клиент возьмёт SET portal из WTF/Config.wtf.")
        .arg(text).arg(maxLen);
}

// ===========================================================================
// Опции Qt -> опции ядра
// ===========================================================================
wowpatch::Options toCoreOptions(const WowPatchOptions &o) {
    wowpatch::Options c;
    c.portal = sstr(o.portal);
    c.port = o.port;
    c.patchPortalSuffix = o.patchPortalSuffix;
    c.portalDomain = sstr(o.portalDomain);
    c.patchPortalWholeSlot = o.patchPortalWholeSlot;
    c.expandPortalBuffer = o.expandPortalBuffer;
    c.autoDetect = o.autoDetect;
    c.applySignaturePatches = o.applySignaturePatches;
    c.patchLegacyGameCryptoRsa = o.patchLegacyGameCryptoRsa;
    c.requireEd25519 = o.requireEd25519;
    c.keysAssumeBigEndian = o.keysAssumeBigEndian;
    c.patchVersionUrls = o.patchVersionUrls;
    c.versionUrl = sstr(o.versionUrl);
    c.cdnsUrl = sstr(o.cdnsUrl);
    c.certBundleUrl = sstr(o.certBundleUrl);
    c.patchLauncherRegistry = o.patchLauncherRegistry;
    c.fixChecksum = o.fixChecksum;
    c.stripSignature = o.stripSignature;
    c.verify = o.verifyAfterWrite;
    for (const QString &r : o.recipePaths) c.recipePaths.push_back(sstr(r));
    return c;
}

// Ключи из PEM/hex читаются здесь (файлы — это Qt), а разбираются в ядре.
bool resolveKeys(const WowPatchOptions &opts, wowpatch::Options *core,
                 const std::function<void (const QString &)> &L, QString *error) {
    if (!opts.rsaPrivatePemPath.trimmed().isEmpty()) {
        QString err;
        const QByteArray key = ClientPatchService::rsaModulusLeFromPem(opts.rsaPrivatePemPath.trimmed(), &err);
        if (key.isEmpty()) {
            if (error) *error = QStringLiteral("RSA из PEM: %1").arg(err);
            return false;
        }
        core->rsaModulus = toCore(key);
        L(QStringLiteral("RSA-модуль взят из PEM %1 (переведён в little-endian, как хранит клиент): %2")
              .arg(QDir::toNativeSeparators(opts.rsaPrivatePemPath.trimmed()), hex(key, 8)));
    } else if (!opts.rsaModulusHex.trimmed().isEmpty()) {
        QString err;
        QByteArray key = ClientPatchService::hexKey(opts.rsaModulusHex.trimmed(), 256, &err);
        if (key.isEmpty()) {
            if (error) *error = QStringLiteral("RSA-ключ: %1").arg(err);
            return false;
        }
        QString note;
        key = ClientPatchService::normalizeKeyOrder(key, opts.keysAssumeBigEndian, &note);
        core->rsaModulus = toCore(key);
        core->keysAssumeBigEndian = false;   // уже развернули
        L(QStringLiteral("RSA-модуль из hex: %1 (%2)").arg(hex(key, 8), note));
    } else {
        L(QStringLiteral("RSA-модуль: встроенный ключ TrinityCore (ConnectToRSA), LE: %1 …")
              .arg(hex(kPatchRsaModulus, 8)));
    }

    if (!opts.ed25519PemPath.trimmed().isEmpty()) {
        QString err;
        const QString keyHex = ClientPatchService::ed25519KeyFromPem(opts.ed25519PemPath.trimmed(), &err);
        const QByteArray key = QByteArray::fromHex(keyHex.toLatin1());
        if (key.size() != 32) {
            if (error) *error = QStringLiteral("Ed25519 из PEM: %1").arg(err);
            return false;
        }
        core->ed25519Key = toCore(key);
        L(QStringLiteral("Ed25519 из PEM %1: %2")
              .arg(QDir::toNativeSeparators(opts.ed25519PemPath.trimmed()), hex(key)));
    } else if (!opts.ed25519KeyHex.trimmed().isEmpty()) {
        QString err;
        const QByteArray key = ClientPatchService::hexKey(opts.ed25519KeyHex.trimmed(), 32, &err);
        if (key.isEmpty()) {
            if (error) *error = QStringLiteral("Ed25519-ключ: %1").arg(err);
            return false;
        }
        core->ed25519Key = toCore(key);
        L(QStringLiteral("Ed25519 из hex: %1").arg(hex(key)));
    } else {
        L(QStringLiteral("Ed25519: встроенный публичный ключ TrinityCore (EnterEncryptedMode): %1")
              .arg(hex(kPatchEd25519Key)));
    }

    if (!opts.certBundlePath.trimmed().isEmpty()) {
        QString err;
        const QByteArray bundle = readFileBytes(opts.certBundlePath.trimmed(), &err);
        if (bundle.isEmpty()) L(QStringLiteral("  ! Cert bundle: %1").arg(err));
        else core->certBundle = toCore(bundle);
    }
    return true;
}

QString findConfigWtf(const QString &exePath) {
    const QDir dir = QFileInfo(exePath).absoluteDir();
    for (const QString &candidate : { dir.filePath(QStringLiteral("WTF/Config.wtf")),
                                      dir.filePath(QStringLiteral("Config.wtf")),
                                      dir.filePath(QStringLiteral("../WTF/Config.wtf")) }) {
        const QString clean = QDir::cleanPath(candidate);
        if (QFileInfo::exists(clean)) return clean;
    }
    return QString();
}

} // namespace

// ===========================================================================
// 1. Автоопределение профиля
// ===========================================================================
WowDetection ClientPatchService::detectProfile(const QString &exePath) {
    QString version = clientVersion(exePath);
    if (version.isEmpty()) version = buildInfoVersion(exePath);
    // Байты не читаем: профиль определяется по версии и пути установки.
    // Содержимое (уже пропатчен или нет) показывает inspectExecutable().
    return toQt(wowpatch::detectProfile(wowpatch::Bytes(), wowpe::Image(), sstr(version), sstr(exePath)));
}

// ===========================================================================
// 2. Версия из .build.info (корень установки)
// ===========================================================================
QString ClientPatchService::buildInfoVersion(const QString &exePath) {
    const QDir dir = QFileInfo(exePath).absoluteDir();
    for (const QString &candidate : { dir.filePath(QStringLiteral("../.build.info")),
                                      dir.filePath(QStringLiteral(".build.info")) }) {
        const QString path = QDir::cleanPath(candidate);
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
        const QString text = QString::fromUtf8(f.readAll());
        f.close();
        const QStringList rows = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        if (rows.size() < 2) continue;
        const QStringList head = rows.first().split(QLatin1Char('\t'));
        int idx = -1;
        for (int i = 0; i < head.size(); ++i)
            if (head.at(i).trimmed().compare(QLatin1String("Version"), Qt::CaseInsensitive) == 0) idx = i;
        if (idx < 0) continue;
        for (int r = 1; r < rows.size(); ++r) {
            const QString line = rows.at(r).trimmed();
            if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;
            const QStringList cols = rows.at(r).split(QLatin1Char('\t'));
            if (idx < cols.size() && !cols.at(idx).trimmed().isEmpty())
                return cols.at(idx).trimmed();
            break;
        }
    }
    return QString();
}

// ===========================================================================
// 3. Консервативный режим: ASCII-замена в копии файла
// ===========================================================================
bool ClientPatchService::replaceAsciiInCopy(const QString &src, const QString &dst,
                                            const QString &find, const QString &replacement,
                                            QString *error) {
    const QByteArray raw = readFileBytes(src, error);
    if (raw.isEmpty()) return false;
    QByteArray data = raw;
    const QByteArray f = find.toUtf8();
    QByteArray r = replacement.toUtf8();
    if (f.isEmpty()) {
        if (error) *error = QStringLiteral("Пустая строка поиска.");
        return false;
    }
    if (r.size() < f.size()) r.append(f.size() - r.size(), '\0');
    if (r.size() > f.size()) {
        if (error) *error = QStringLiteral("Замена длиннее находки: строки в PE имеют фиксированную длину.");
        return false;
    }
    int count = 0;
    qsizetype pos = 0;
    while ((pos = data.indexOf(f, pos)) >= 0) {
        std::memcpy(data.data() + pos, r.constData(), size_t(r.size()));
        ++count;
        pos += f.size();
    }
    if (!count) {
        if (error) *error = QStringLiteral("Строка «%1» в файле не найдена.").arg(find);
        return false;
    }
    if (!writeFileBytes(dst, toCore(data), error)) return false;
    return true;
}

// ===========================================================================
// 4. Ключи
// ===========================================================================
QByteArray ClientPatchService::trinityRsaModulusLe()    { return kPatchRsaModulus; }
QByteArray ClientPatchService::trinityEd25519PublicKey(){ return kPatchEd25519Key; }
QByteArray ClientPatchService::blizzardRsaSignature()   { return kPatternConnectToModulus; }
QByteArray ClientPatchService::blizzardEd25519Signature(){ return kPatternGameCryptoEd25519; }

QByteArray ClientPatchService::normalizeKeyOrder(const QByteArray &key, bool assumeBigEndian, QString *note) {
    std::string n;
    const wowpatch::Bytes out = wowpatch::normalizeKeyOrder(toCore(key), assumeBigEndian, &n);
    if (note) *note = qstr(n);
    return fromCore(out);
}

QByteArray ClientPatchService::rsaModulusLeFromPem(const QString &pemPath, QString *error) {
    QFile f(pemPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) *error = QStringLiteral("Не удалось открыть PEM: %1").arg(f.errorString());
        return QByteArray();
    }
    const QString text = QString::fromUtf8(f.readAll());
    f.close();
    std::string err;
    const wowpatch::Bytes modulus = wowpatch::rsaModulusLeFromPem(sstr(text), &err);
    if (modulus.empty()) {
        if (error) *error = qstr(err);
        return QByteArray();
    }
    return fromCore(modulus);
}

bool ClientPatchService::keysSelfTest(QStringList *log) {
    std::vector<std::string> l;
    const bool ok = wowpatch::keysSelfTest(&l);
    if (log) *log = qstrList(l);
    return ok;
}

QByteArray ClientPatchService::hexKey(const QString &hexText, int expectedBytes, QString *error) {
    wowpatch::Bytes b;
    const std::string err = wowpe::hexToBytes(sstr(hexText), &b);
    if (!err.empty()) {
        if (error) *error = qstr(err);
        return QByteArray();
    }
    if (int(b.size()) != expectedBytes) {
        if (error) *error = QStringLiteral("Ключ должен быть %1 байт (%2 hex-символов), получено %3.")
                                .arg(expectedBytes).arg(expectedBytes * 2).arg(int(b.size()));
        return QByteArray();
    }
    return fromCore(b);
}

// ===========================================================================
// 5. Config.wtf и папка Data
// ===========================================================================
QString ClientPatchService::clientDataDir(const QString &exePath, bool *existsOut) {
    const QDir dir = QFileInfo(exePath).absoluteDir();
    // Ретейл: exe в _retail_, Data либо рядом с exe, либо в корне установки.
    const QStringList candidates = { dir.filePath(QStringLiteral("Data")),
                                     dir.filePath(QStringLiteral("../Data")) };
    for (const QString &c : candidates) {
        const QString clean = QDir::cleanPath(c);
        if (QFileInfo(clean).isDir()) {
            if (existsOut) *existsOut = true;
            return clean;
        }
    }
    if (existsOut) *existsOut = false;
    return QDir::cleanPath(candidates.first());
}

QString ClientPatchService::readPortalFromConfigWtf(const QString &exePath, QString *error) {
    const QString config = findConfigWtf(exePath);
    if (config.isEmpty()) {
        if (error) *error = QStringLiteral("Config.wtf не найден рядом с клиентом (искали WTF/Config.wtf, Config.wtf и ../WTF/Config.wtf).");
        return QString();
    }
    QFile f(config);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) *error = QStringLiteral("Не удалось открыть Config.wtf: %1").arg(f.errorString());
        return QString();
    }
    const QString text = QString::fromUtf8(f.readAll());
    f.close();
    const QString portal = qstr(wowpatch::configPortal(sstr(text)));
    if (portal.isEmpty() && error)
        *error = QStringLiteral("В %1 нет строки SET portal \"host:port\".").arg(config);
    return portal;
}

bool ClientPatchService::writePortalToConfigWtf(const QString &exePath, const QString &portal, QString *error) {
    QString config = findConfigWtf(exePath);
    QString text;
    if (config.isEmpty()) {
        config = QDir::cleanPath(QFileInfo(exePath).absoluteDir().filePath(QStringLiteral("WTF/Config.wtf")));
        if (error) *error = QStringLiteral("Config.wtf не найден — создаю %1").arg(config);
    } else {
        QFile f(config);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            if (error) *error = QStringLiteral("Не удалось открыть Config.wtf: %1").arg(f.errorString());
            return false;
        }
        text = QString::fromUtf8(f.readAll());
        f.close();
    }
    std::string body = sstr(text);
    if (!wowpatch::writePortalIntoConfig(body, sstr(portal))) return true;   // уже стоит
    return writeTextFile(config, qstr(body), error);
}

// ===========================================================================
// 6. Диагностика
// ===========================================================================
WowInspect ClientPatchService::inspectExecutable(const QString &exePath) {
    WowInspect r;
    r.path = exePath;
    QString err;
    const QByteArray raw = readFileBytes(exePath, &err);
    if (raw.isEmpty()) {
        r.error = err;
        r.lines.append(err);
        return r;
    }
    r.size = raw.size();

    QString version = clientVersion(exePath);
    const QString bi = buildInfoVersion(exePath);
    if (version.isEmpty()) version = bi;
    r.version = version;
    r.buildInfoVersion = bi;

    const wowpatch::Inspect ci = wowpatch::inspect(toCore(raw), sstr(version), sstr(exePath));
    r.ok = ci.ok;
    r.error = qstr(ci.error);
    r.sha256 = qstr(ci.sha256);
    r.detection = toQt(ci.detection);
    r.pe64 = ci.pe64;
    r.imageBase = ci.imageBase;
    r.entryRva = ci.entryRva;
    r.sizeOfImage = ci.sizeOfImage;
    r.checksum = ci.checksum;
    r.hasSignature = ci.hasSignature;
    r.certPointer = ci.certPointer;
    r.certSize = ci.certSize;
    for (const wowpatch::SectionInfo &s : ci.sections) r.sections.append(toQt(s));
    r.blizzardRsaFound = ci.blizzardRsaFound;
    r.trinityRsaFound = ci.trinityRsaFound;
    r.blizzardEdFound = ci.blizzardEdFound;
    r.trinityEdFound = ci.trinityEdFound;
    r.portalFound = ci.portalFound;
    r.certBundleSlotFound = ci.certBundleSlotFound;
    r.lines = qstrList(ci.lines);

    // То, что видно только из файловой системы.
    bool hasData = false;
    r.dataDir = clientDataDir(exePath, &hasData);
    r.dataDirFound = hasData;
    r.lines.append(QStringLiteral("Папка Data: %1%2").arg(QDir::toNativeSeparators(r.dataDir),
                   hasData ? QString() : QStringLiteral("  — НЕ НАЙДЕНА (клиент не запустится)")));

    r.configWtfPath = findConfigWtf(exePath);
    if (r.configWtfPath.isEmpty()) {
        r.lines.append(QStringLiteral("Config.wtf: не найден (будет создан при патче)"));
    } else {
        QString perr;
        r.configPortal = readPortalFromConfigWtf(exePath, &perr);
        r.lines.append(QStringLiteral("Config.wtf: %1   SET portal: %2")
                           .arg(QDir::toNativeSeparators(r.configWtfPath),
                                r.configPortal.isEmpty() ? QStringLiteral("нет") : r.configPortal));
    }
    if (!ci.ok) r.lines.append(QStringLiteral("[!!] %1").arg(r.error));
    return r;
}

// ===========================================================================
// 7. ОСНОВНОЙ РЕЖИМ: самостоятельный пропатченный exe на диске
// ===========================================================================
WowPatchReport ClientPatchService::patchStandalone(const QString &source, const QString &destination,
                                                   const WowPatchOptions &opts, QStringList *log) {
    auto L = [&](const QString &s) { if (log) log->append(s); };
    WowPatchReport rep;

    L(QStringLiteral("=== ПАТЧ НА ДИСК (Firestorm-стиль) ==="));
    L(QStringLiteral("Источник : %1").arg(QDir::toNativeSeparators(source)));
    L(QStringLiteral("Результат: %1").arg(QDir::toNativeSeparators(destination)));

    QString err;
    const QByteArray raw = readFileBytes(source, &err);
    if (raw.isEmpty()) {
        rep.error = err;
        L(QStringLiteral("[!!] %1").arg(err));
        return rep;
    }
    wowpatch::Bytes data = toCore(raw);
    const wowpe::Image img = wowpe::parse(data);
    if (!img.valid) {
        rep.error = QStringLiteral("PE не разобран: %1 — это не похоже на Wow.exe").arg(qstr(img.error));
        L(QStringLiteral("[!!] %1").arg(rep.error));
        return rep;
    }

    QString version = clientVersion(source);
    const QString bi = buildInfoVersion(source);
    if (version.isEmpty()) version = bi;
    const wowpatch::Detection cdet = opts.autoDetect
        ? wowpatch::detectProfile(data, img, sstr(version), sstr(source))
        : wowpatch::Detection{};
    const WowDetection det = toQt(cdet);
    if (opts.autoDetect)
        L(QStringLiteral("Профиль: [%1] версия %2, ветка %3%4")
              .arg(det.profile, det.version.isEmpty() ? QStringLiteral("?") : det.version,
                   det.branch, det.note.isEmpty() ? QString() : QStringLiteral(" — ") + det.note));
    if (!version.isEmpty() && !bi.isEmpty() && version != bi)
        L(QStringLiteral("Версия из ресурса exe: %1, из .build.info: %2").arg(version, bi));

    bool hasData = false;
    const QString dataDir = clientDataDir(source, &hasData);
    L(QStringLiteral("Папка Data: %1%2").arg(QDir::toNativeSeparators(dataDir),
          hasData ? QString() : QStringLiteral("  — НЕ НАЙДЕНА! Клиент без Data не запустится.")));

    wowpatch::Options core = toCoreOptions(opts);
    if (!resolveKeys(opts, &core, L, &err)) {
        rep.error = err;
        L(QStringLiteral("[!!] %1").arg(err));
        return rep;
    }

    std::vector<std::string> clog;
    wowpatch::Report crep;

    // --- рецепты: ДО сигнатурных патчей (диапазоны потом учитываются ядром) ---
    for (const QString &recipePath : opts.recipePaths) {
        const QString path = recipePath.trimmed();
        if (path.isEmpty()) continue;
        QFile rf(path);
        if (!rf.open(QIODevice::ReadOnly)) {
            L(QStringLiteral("  ! Рецепт %1 не открыт: %2").arg(path, rf.errorString()));
            continue;
        }
        const QString text = QString::fromUtf8(rf.readAll());
        rf.close();
        wowpatch::Recipe recipe;
        std::string jerr;
        if (!wowpatch::recipeFromJson(sstr(text), &recipe, &jerr)) {
            L(QStringLiteral("  ! Рецепт %1: %2").arg(path, qstr(jerr)));
            continue;
        }
        L(QStringLiteral("Рецепт %1").arg(QDir::toNativeSeparators(path)));
        wowpatch::applyRecipeToImage(data, img, recipe, &crep, &clog);
    }

    // --- сам патч (план, запись, checksum/подпись, верификация) ---
    const bool ok = wowpatch::patchImage(data, core, cdet, &crep, &clog);
    for (const std::string &s : clog) L(qstr(s));

    rep.size = qint64(data.size());
    rep.sha256 = qstr(crep.sha256);
    rep.verifiedClean = crep.verifiedClean;
    rep.verification = qstrList(crep.verification);
    for (const wowpatch::Record &rc : crep.records) rep.records.append(toQt(rc));

    if (!ok) {
        rep.error = qstr(crep.error);
        L(QStringLiteral("[!!] %1").arg(rep.error));
        return rep;
    }

    // --- бэкап и запись ---
    const QFileInfo srcInfo(source), dstInfo(destination);
    const bool inPlace = srcInfo.absoluteFilePath() == dstInfo.absoluteFilePath();
    if (QFileInfo::exists(destination) && opts.makeBackup) {
        const QString bak = inPlace ? destination + QStringLiteral(".orig")
                                    : destination + QStringLiteral(".bak");
        QFile::remove(bak);
        if (QFile::copy(destination, bak))
            L(QStringLiteral("Резервная копия: %1").arg(QDir::toNativeSeparators(bak)));
        else
            L(QStringLiteral("  ! Не удалось создать резервную копию %1 — продолжаю без неё").arg(bak));
    }
    if (!writeFileBytes(destination, data, &err)) {
        rep.error = err;
        L(QStringLiteral("[!!] %1").arg(err));
        return rep;
    }
    rep.destination = destination;
    rep.size = qint64(data.size());
    L(QStringLiteral("Записано: %1 (%2 байт, SHA-256 %3)")
          .arg(QDir::toNativeSeparators(destination)).arg(data.size()).arg(rep.sha256));

    // --- верификация того, что РЕАЛЬНО легло на диск ---
    if (opts.verifyAfterWrite) {
        L(QStringLiteral("Проверка файла на диске:"));
        QString verr;
        const QByteArray written = readFileBytes(destination, &verr);
        if (written.isEmpty()) {
            rep.verification.append(QStringLiteral("[!!] не удалось перечитать результат: %1").arg(verr));
            rep.verifiedClean = false;
        } else {
            const wowpatch::Bytes wd = toCore(written);
            const wowpe::Image wimg = wowpe::parse(wd);
            std::vector<std::string> vout;
            const bool clean = wowpatch::verifyImage(wd, wimg.valid ? wimg : img, crep, &vout);
            for (const std::string &s : vout) {
                L(QStringLiteral("  %1").arg(qstr(s)));
                rep.verification.append(qstr(s));
            }
            rep.verifiedClean = clean;
            if (!clean) {
                rep.error = QStringLiteral("Файл на диске не прошёл проверку — см. строки [!!] выше.");
                L(QStringLiteral("[!!] %1").arg(rep.error));
                return rep;
            }
        }
    }

    // --- Config.wtf ---
    if (opts.writeConfigWtf && !opts.portal.trimmed().isEmpty()) {
        const QString want = normalizePortalHost(opts.portal);
        QString cfgErr;
        const QString existing = readPortalFromConfigWtf(source, &cfgErr);
        if (existing == want) {
            L(QStringLiteral("Config.wtf: SET portal \"%1\" уже стоит — не трогаю").arg(want));
            rep.configWtfPath = findConfigWtf(destination);
            rep.portalWritten = want;
        } else if (writePortalToConfigWtf(destination, want, &cfgErr)) {
            rep.configWtfPath = findConfigWtf(destination);
            rep.portalWritten = want;
            L(QStringLiteral("[OK] SET portal \"%1\" записан в %2")
                  .arg(want, QDir::toNativeSeparators(rep.configWtfPath)));
            if (!cfgErr.isEmpty()) L(QStringLiteral("  [i] %1").arg(cfgErr));
        } else {
            L(QStringLiteral("  ! Config.wtf: %1").arg(cfgErr));
        }
    }

    rep.ok = true;
    L(QStringLiteral("Готово: %1 — самостоятельный exe (запускается без лаунчера).").arg(QDir::toNativeSeparators(destination)));

    if (opts.launchAfterPatch) {
        QString lerr;
        if (!launchClient(destination, opts.extraArgs, log, &lerr))
            L(QStringLiteral("  ! Запуск: %1").arg(lerr));
    }
    return rep;
}

bool ClientPatchService::patchFileCopy(const QString &source, const QString &destination,
                                       const WowPatchOptions &opts, QStringList *log, QString *error) {
    const WowPatchReport rep = patchStandalone(source, destination, opts, log);
    if (!rep.ok && error) *error = rep.error;
    return rep.ok;
}

QString ClientPatchService::defaultOutputPath(const QString &exePath) {
    // Firestorm-стиль: пропатченный exe лежит там же и называется так же —
    // тогда он находит свою Data и WTF. Оригиналу делаем бэкап .orig.
    return QFileInfo(exePath).absoluteFilePath();
}

QString ClientPatchService::copyOutputPath(const QString &exePath) {
    // Копия лежит РЯДОМ с оригиналом (иначе не найдёт свои Data и WTF):
    // Wow.exe -> Wow.patched.exe.
    const QFileInfo fi(exePath);
    const QString ext = fi.suffix().isEmpty() ? QStringLiteral("exe") : fi.suffix();
    return fi.absolutePath() + QLatin1Char('/') + fi.completeBaseName() +
           QStringLiteral(".patched.") + ext;
}

// ===========================================================================
// 8. Рецепт: снять разницу и сохранить в JSON
// ===========================================================================
bool ClientPatchService::buildRecipe(const QString &originalExe, const QString &patchedExe,
                                     const QString &outJsonPath, const WowPatchOptions &opts,
                                     QStringList *log, QString *error) {
    Q_UNUSED(opts);
    auto L = [&](const QString &s) { if (log) log->append(s); };

    L(QStringLiteral("=== ИЗВЛЕЧЕНИЕ РЕЦЕПТА ==="));
    L(QStringLiteral("Оригинал       : %1").arg(QDir::toNativeSeparators(originalExe)));
    L(QStringLiteral("Пропатченный  : %1").arg(QDir::toNativeSeparators(patchedExe)));

    QString err;
    const QByteArray aRaw = readFileBytes(originalExe, &err);
    if (aRaw.isEmpty()) { if (error) *error = err; L(QStringLiteral("[!!] %1").arg(err)); return false; }
    const QByteArray bRaw = readFileBytes(patchedExe, &err);
    if (bRaw.isEmpty()) { if (error) *error = err; L(QStringLiteral("[!!] %1").arg(err)); return false; }

    wowpatch::RecipeMeta meta;
    meta.name = sstr(QFileInfo(patchedExe).completeBaseName());
    meta.sourceOriginal = sstr(originalExe);
    meta.sourcePatched = sstr(patchedExe);
    meta.originalSha256 = sstr(QString::fromLatin1(
        QCryptographicHash::hash(aRaw, QCryptographicHash::Sha256).toHex()));
    meta.patchedSha256 = sstr(QString::fromLatin1(
        QCryptographicHash::hash(bRaw, QCryptographicHash::Sha256).toHex()));
    meta.originalVersion = sstr(clientVersion(originalExe));
    meta.patchedVersion = sstr(clientVersion(patchedExe));
    meta.createdUtc = sstr(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));

    wowpatch::Recipe recipe;
    std::vector<std::string> clog;
    std::string cerr;
    if (!wowpatch::makeRecipe(toCore(aRaw), toCore(bRaw), meta, &recipe, &clog, &cerr)) {
        for (const std::string &s : clog) L(qstr(s));
        if (error) *error = qstr(cerr);
        L(QStringLiteral("[!!] %1").arg(qstr(cerr)));
        return false;
    }
    for (const std::string &s : clog) L(qstr(s));

    const QString json = qstr(wowpatch::recipeToJson(recipe));
    if (!writeTextFile(outJsonPath, json, &err)) {
        if (error) *error = err;
        L(QStringLiteral("[!!] %1").arg(err));
        return false;
    }
    L(QStringLiteral("Рецепт сохранён: %1 (hunks=%2)")
          .arg(QDir::toNativeSeparators(outJsonPath)).arg(recipe.hunks.size()));

    // Контроль: читаем обратно и переносим на оригинал — должно совпасть.
    wowpatch::Recipe back;
    std::string jerr;
    if (!wowpatch::recipeFromJson(sstr(json), &back, &jerr)) {
        if (error) *error = QStringLiteral("Сохранённый JSON не читается обратно: %1").arg(qstr(jerr));
        L(QStringLiteral("[!!] %1").arg(*error));
        return false;
    }
    wowpatch::Bytes target = toCore(aRaw);
    const wowpe::Image timg = wowpe::parse(target);
    wowpatch::Report rr;
    std::vector<std::string> rlog;
    const int moved = wowpatch::applyRecipeToImage(target, timg, back, &rr, &rlog);
    for (const std::string &s : rlog) L(qstr(s));
    const bool same = target == toCore(bRaw);
    L(QStringLiteral("Самопроверка рецепта: перенесено %1 hunks, результат %2 пропатченному файлу")
          .arg(moved).arg(same ? QStringLiteral("побайтово РАВЕН") : QStringLiteral("НЕ равен")));
    if (!same) {
        if (error) *error = QStringLiteral("Самопроверка рецепта не прошла: перенос не воспроизводит пропатченный файл.");
        return false;
    }
    return true;
}

// ===========================================================================
// 9. Применить рецепт(ы) к exe
// ===========================================================================
bool ClientPatchService::applyRecipe(const QString &source, const QString &destination,
                                     const QStringList &recipePaths, QStringList *log, QString *error) {
    WowPatchOptions opts;
    opts.recipePaths = recipePaths;
    opts.applySignaturePatches = false;   // нужны только hunks из рецепта
    opts.writeConfigWtf = false;
    opts.requireEd25519 = false;
    opts.autoDetect = false;
    const WowPatchReport rep = patchStandalone(source, destination, opts, log);
    if (!rep.ok && error) *error = rep.error;
    return rep.ok;
}

// ===========================================================================
// 10. Запуск готового клиента
// ===========================================================================
bool ClientPatchService::launchClient(const QString &exePath, const QString &extraArgs,
                                      QStringList *log, QString *error) {
    if (!QFileInfo::exists(exePath)) {
        if (error) *error = QStringLiteral("Файл не найден: %1").arg(exePath);
        return false;
    }
    QStringList args;
    args << extraArgs.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    const QString workDir = QFileInfo(exePath).absolutePath();
    if (!QProcess::startDetached(exePath, args, workDir)) {
        if (error) *error = QStringLiteral("Не удалось запустить %1").arg(exePath);
        return false;
    }
    if (log) log->append(QStringLiteral("Запущен %1%2 (рабочая папка %3)")
                             .arg(QDir::toNativeSeparators(exePath),
                                  args.isEmpty() ? QString() : QStringLiteral(" ") + args.join(QLatin1Char(' ')),
                                  QDir::toNativeSeparators(workDir)));
    return true;
}

#ifdef Q_OS_WIN
namespace {

// ===========================================================================
// Фаза A/B: скан памяти по регионам (VirtualQueryEx) и запись патчей
// ===========================================================================
struct PatchOp {
    const char *label;
    quintptr address = 0;
    QByteArray bytes;
};

constexpr int SCAN_MAX_REGIONS = 4096;
constexpr qsizetype MAX_REGION_READ = 32 * 1024 * 1024;

bool matchAt(const QByteArray &buf, qsizetype off, const Pattern &pat) {
    if (off + qsizetype(pat.size()) > buf.size()) return false;
    for (qsizetype i = 0; i < qsizetype(pat.size()); ++i)
        if (pat[size_t(i)] >= 0 && quint8(buf.at(off + i)) != quint8(pat[size_t(i)])) return false;
    return true;
}

quintptr moduleEnd(HANDLE h, quintptr base) {
    if (!base) return 0;
    quintptr end = base, addr = base;
    for (int i = 0; i < SCAN_MAX_REGIONS; ++i) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQueryEx(h, reinterpret_cast<LPCVOID>(addr), &mbi, sizeof(mbi)) || !mbi.RegionSize) break;
        if (quintptr(mbi.AllocationBase) != base) break;
        const quintptr re = quintptr(mbi.BaseAddress) + quintptr(mbi.RegionSize);
        if (re > end) end = re;
        if (re <= addr) break;
        addr = re;
    }
    return end > base ? end : base + 0x400000;
}

void scanMemory(HANDLE h, quintptr start, quintptr end, const Pattern &pat,
                const std::function<QByteArray(const QByteArray &)> &make,
                QVector<PatchOp> &out, QStringList *log) {
    if (pat.empty() || start >= end) return;
    bool exact = std::all_of(pat.begin(), pat.end(), [](int16_t v) { return v >= 0; });
    QByteArray needle;
    int leadOff = 0;
    char leadByte = 0;
    if (exact) {
        needle.resize(int(pat.size()));
        for (int i = 0; i < needle.size(); ++i) needle[i] = char(quint8(pat[size_t(i)]));
    } else {
        for (size_t i = 0; i < pat.size(); ++i)
            if (pat[i] >= 0) { leadOff = int(i); leadByte = char(pat[i]); break; }
    }
    quintptr addr = start;
    int regions = 0;
    while (addr < end && regions < SCAN_MAX_REGIONS) {
        ++regions;
        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQueryEx(h, reinterpret_cast<LPCVOID>(addr), &mbi, sizeof(mbi)) || !mbi.RegionSize) break;
        const quintptr regionBase = quintptr(mbi.BaseAddress);
        const quintptr regionEnd = regionBase + quintptr(mbi.RegionSize);
        if (mbi.State != MEM_COMMIT || mbi.RegionSize < SIZE_T(pat.size())) {
            addr = regionEnd <= addr ? addr + 0x1000 : regionEnd;
            continue;
        }
        quintptr chunk = regionBase;
        while (chunk < regionEnd && chunk < end) {
            const SIZE_T remain = SIZE_T(qMin<quint64>(regionEnd - chunk, quint64(MAX_REGION_READ)));
            QByteArray buf(int(remain), Qt::Uninitialized);
            SIZE_T got = 0;
            if (ReadProcessMemory(h, reinterpret_cast<LPCVOID>(chunk), buf.data(), remain, &got)
                && got >= SIZE_T(pat.size())) {
                buf.truncate(int(got));
                if (exact) {
                    qsizetype off = 0;
                    while (true) {
                        const qsizetype hit = buf.indexOf(needle, off);
                        if (hit < 0) break;
                        if (make) {
                            const QByteArray bytes = make(buf.mid(hit, needle.size()));
                            if (!bytes.isEmpty()) out.push_back({ nullptr, chunk + quintptr(hit), bytes });
                        } else {
                            out.push_back({ nullptr, chunk + quintptr(hit), QByteArray(1, '\x01') });
                        }
                        off = hit + needle.size();
                    }
                } else {
                    qsizetype off = 0;
                    while (off + qsizetype(pat.size()) <= buf.size()) {
                        const qsizetype hit = buf.indexOf(leadByte, off + leadOff);
                        if (hit < 0) break;
                        off = hit - leadOff;
                        if (off < 0) { off = hit + 1; continue; }
                        if (matchAt(buf, off, pat)) {
                            if (make) {
                                const QByteArray bytes = make(buf.mid(off, qsizetype(pat.size()))
                                );
                                if (!bytes.isEmpty()) out.push_back({ nullptr, chunk + quintptr(off), bytes });
                            } else {
                                out.push_back({ nullptr, chunk + quintptr(off), QByteArray(1, '\x01') });
                            }
                            off += qsizetype(pat.size());
                        } else {
                            ++off;
                        }
                    }
                }
            }
            if (remain <= SIZE_T(pat.size())) break;
            chunk += remain - SIZE_T(pat.size()) + 1;
        }
        addr = regionEnd <= addr ? addr + 0x1000 : regionEnd;
    }
    Q_UNUSED(log);
}

bool writeAt(HANDLE h, quintptr address, const QByteArray &data, QStringList *log) {
    if (data.isEmpty() || !h) return false;
    DWORD oldProtect = 0;
    if (!VirtualProtectEx(h, reinterpret_cast<LPVOID>(address), static_cast<SIZE_T>(data.size()),
                          PAGE_EXECUTE_READWRITE, &oldProtect)
        && GetLastError() != ERROR_SUCCESS) {
        // Не фатально — попробуем писать с текущими правами.
    }
    SIZE_T written = 0;
    const BOOL ok = WriteProcessMemory(h, reinterpret_cast<LPVOID>(address), data.constData(),
                                       static_cast<SIZE_T>(data.size()), &written);
    FlushInstructionCache(h, reinterpret_cast<LPVOID>(address), static_cast<SIZE_T>(data.size()));
    if (oldProtect != 0)
        VirtualProtectEx(h, reinterpret_cast<LPVOID>(address), static_cast<SIZE_T>(data.size()), oldProtect, &oldProtect);
    if (!ok || written != static_cast<SIZE_T>(data.size())) {
        if (log) log->append(QStringLiteral("  ! WriteProcessMemory failed: %1 (записано %2/%3)")
                             .arg(GetLastError(), 8, 16, QLatin1Char('0')).arg(qulonglong(written)).arg(data.size()));
        return false;
    }
    return true;
}

QByteArray replaceFirst(const QByteArray &matched, QByteArray prefix) {
    QByteArray out = matched;
    for (int i = 0; i < prefix.size() && i < out.size(); ++i) out[i] = prefix.at(i);
    return out;
}
QByteArray ret0Stub(const QByteArray &m) { return replaceFirst(m, QByteArray("\xC2\x00\x00", 3)); }
QByteArray nopPair(const QByteArray &m)  { return replaceFirst(m, QByteArray("\x90\x90", 2)); }
QByteArray movAl1(const QByteArray &m)   { return replaceFirst(m, QByteArray("\xB0\x01", 2)); }
QByteArray movBl1(const QByteArray &m)   { return replaceFirst(m, QByteArray("\xB3\x01\xEB\x02", 4)); }

bool waitForUnpack(HANDLE h, quintptr base, quintptr end, const QString &logPrefix,
                   int waitMs, QStringList *log, bool *foundOut) {
    const int steps = qMax(1, waitMs / 300);
    const quintptr scanTo = end > base ? end : base + 0x800000;
    for (int i = 0; i < steps; ++i) {
        QVector<PatchOp> hits;
        scanMemory(h, base, scanTo, kPatInit, nullptr, hits, nullptr);
        if (!hits.isEmpty()) {
            if (log) log->append(logPrefix + QStringLiteral("  [OK] Расшифровка .text обнаружена (сторож init) за %1 мс").arg((i + 1) * 300));
            if (foundOut) *foundOut = true;
            return true;
        }
        QThread::msleep(300);
    }
    if (foundOut) *foundOut = false;
    return false;
}

} // namespace
#endif
// ===========================================================================
// 8. Патч в памяти (Arctium): CreateProcess(CREATE_SUSPENDED) → PEB → A → B
// ===========================================================================
bool ClientPatchService::patchAndLaunch(const QString &exePath, const WowPatchOptions &opts,
                                        QStringList *log, QString *error) {
#ifndef Q_OS_WIN
    Q_UNUSED(exePath); Q_UNUSED(opts); Q_UNUSED(log);
    if (error) *error = "Патч в памяти доступен только на Windows.";
    return false;
#else
    if (!QFileInfo::exists(exePath)) {
        if (error) *error = QStringLiteral("Файл не найден: %1").arg(exePath);
        return false;
    }
    if (log) log->append(QStringLiteral("Патч в памяти (метод Arctium):"));

    QByteArray rsa = kPatchRsaModulus, ed = kPatchEd25519Key;
    if (!opts.rsaPrivatePemPath.trimmed().isEmpty()) {
        QString e;
        rsa = rsaModulusLeFromPem(opts.rsaPrivatePemPath.trimmed(), &e);
        if (rsa.isEmpty()) { if (error) *error = "RSA из PEM: " + e; return false; }
    } else if (!opts.rsaModulusHex.trimmed().isEmpty()) {
        QString e;
        rsa = hexKey(opts.rsaModulusHex.trimmed(), 256, &e);
        if (rsa.isEmpty()) { if (error) *error = "RSA-ключ: " + e; return false; }
        QString n; rsa = normalizeKeyOrder(rsa, opts.keysAssumeBigEndian, &n);
    }
    if (!opts.ed25519KeyHex.trimmed().isEmpty()) {
        QString e;
        ed = hexKey(opts.ed25519KeyHex.trimmed(), 32, &e);
        if (ed.isEmpty()) { if (error) *error = "Ed25519-ключ: " + e; return false; }
    }

    const WowDetection det = opts.autoDetect ? detectProfile(exePath) : WowDetection{};
    bool legacy = opts.autoDetect ? det.legacyCert : opts.patchLegacyGameCryptoRsa;
    const bool modern = (det.profile == QLatin1String("modern") || det.profile == QLatin1String("modern12"));
    if (!legacy && opts.bypassCertValidation) legacy = true;
    if (opts.autoDetect && log) log->append(QStringLiteral("Профиль: [%1] версия %2, ветка %3%4")
                                            .arg(det.profile, det.version.isEmpty() ? QStringLiteral("?") : det.version,
                                                 det.branch, det.note.isEmpty() ? QString() : QStringLiteral(" — ") + det.note));

    {
        bool hasData = false;
        const QString dataDir = clientDataDir(exePath, &hasData);
        if (log) log->append(QStringLiteral("Папка Data рядом с клиентом: %1%2")
                                 .arg(QDir::toNativeSeparators(dataDir),
                                      hasData ? QString() : QStringLiteral(" — НЕ НАЙДЕНА! Клиент не запустится без неё.")));
    }

    if (opts.writeConfigWtf) {
        QString cfgErr;
        const QString existing = readPortalFromConfigWtf(exePath, &cfgErr);
        const QString want = normalizePortalHost(opts.portal);
        if (!want.isEmpty() && existing != want) {
            if (!writePortalToConfigWtf(exePath, want, &cfgErr)) {
                if (log) log->append(QStringLiteral("  ! Config.wtf: %1").arg(cfgErr));
            } else if (log) log->append(QStringLiteral("  [OK] SET portal \"%1\" записан в WTF/Config.wtf").arg(want));
        } else if (log && existing == want && !want.isEmpty()) {
            log->append(QStringLiteral("  [OK] SET portal \"%1\" уже стоит в Config.wtf — не трогаем").arg(existing));
        }
    }

    const QString dir = QFileInfo(exePath).absolutePath();
    const QString cmdLine = QStringLiteral("\"%1\"%2").arg(
        QDir::toNativeSeparators(exePath),
        opts.extraArgs.isEmpty() ? QString() : QStringLiteral(" ") + opts.extraArgs);
    std::wstring cmdCopy = cmdLine.toStdWString();
    std::wstring dirBuf = QDir::toNativeSeparators(dir).toStdWString();
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmdCopy.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED,
                        nullptr, dirBuf.c_str(), &si, &pi)) {
        if (error) *error = QStringLiteral("CreateProcess failed (код %1). Запустите Studio от имени пользователя, "
                                          "у которого есть права на Wow.exe; антивирус не должен блокировать CREATE_SUSPENDED.")
                                .arg(GetLastError(), 8, 16, QLatin1Char('0'));
        return false;
    }
    if (log) log->append(QStringLiteral("  [1] Процесс создан, PID=%1 (приостановлен)").arg(pi.dwProcessId));

    auto killAndReturn = [&](const QString &msg) -> bool {
        if (error) *error = msg;
        TerminateProcess(pi.hProcess, 0);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return false;
    };

    struct PROCESS_BASIC_INFORMATION {
        PVOID Reserved1; PVOID PebBaseAddress;
        PVOID Reserved2_0, Reserved2_1; ULONG_PTR UniqueProcessId; PVOID Reserved3;
    };
    using NtQueryInfo_t = LONG(WINAPI *)(HANDLE, ULONG, PVOID, ULONG, PULONG);
    using NtResume_t = LONG(WINAPI *)(HANDLE);
    using NtSuspend_t = LONG(WINAPI *)(HANDLE);
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    auto pNtQuery = reinterpret_cast<NtQueryInfo_t>(GetProcAddress(ntdll, "NtQueryInformationProcess"));
    auto pNtResume = reinterpret_cast<NtResume_t>(GetProcAddress(ntdll, "NtResumeProcess"));
    auto pNtSuspend = reinterpret_cast<NtSuspend_t>(GetProcAddress(ntdll, "NtSuspendProcess"));
    if (!pNtQuery || !pNtResume || !pNtSuspend)
        return killAndReturn("ntdll: NtQueryInformationProcess/NtSuspendProcess/NtResumeProcess недоступны.");

    PROCESS_BASIC_INFORMATION pbi{};
    ULONG len = 0;
    if (pNtQuery(pi.hProcess, 0, &pbi, sizeof(pbi), &len) != 0)
        return killAndReturn("NtQueryInformationProcess failed (не удалось получить PEB).");

    BOOL wow64 = FALSE;
    IsWow64Process(pi.hProcess, &wow64);

    auto readImageBase = [&]() -> quintptr {
        quintptr base = 0;
        SIZE_T nread = 0;
#ifdef _WIN64
        if (wow64) {
            ULONG_PTR peb32 = 0;
            ULONG n = 0;
            if (pNtQuery(pi.hProcess, 26, &peb32, sizeof(peb32), &n) != 0 || !peb32) return 0;
            quint32 base32 = 0;
            if (!ReadProcessMemory(pi.hProcess, reinterpret_cast<LPCVOID>(peb32 + 0x08),
                                   &base32, sizeof(base32), &nread) || nread != sizeof(base32)) return 0;
            return quintptr(base32);
        }
#endif
        if (!ReadProcessMemory(pi.hProcess, reinterpret_cast<LPCVOID>(quintptr(pbi.PebBaseAddress) + 0x10),
                               &base, sizeof(base), &nread) || nread != sizeof(base)) return 0;
        return base;
    };

    auto regionMapped = [&](quintptr addr) -> bool {
        if (!addr) return false;
        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQueryEx(pi.hProcess, reinterpret_cast<LPCVOID>(addr), &mbi, sizeof(mbi))) return false;
        return mbi.State == MEM_COMMIT
            && (quintptr(mbi.AllocationBase) == addr || quintptr(mbi.BaseAddress) == addr);
    };

    quintptr imageBase = readImageBase();
    if (log) log->append(QStringLiteral("  [2] ImageBase = 0x%1 (%2)")
                         .arg(imageBase, 0, 16)
                         .arg(wow64 ? QStringLiteral("WOW64 32-bit") : QStringLiteral("native")));

    bool mapped = regionMapped(imageBase);
    if (!mapped) {
        ResumeThread(pi.hThread);
        pNtResume(pi.hProcess);
        for (int i = 0; i < 80 && !mapped; ++i) {
            QThread::msleep(50);
            if (!imageBase) imageBase = readImageBase();
            mapped = regionMapped(imageBase);
        }
        SuspendThread(pi.hThread);
        pNtSuspend(pi.hProcess);
    }
    if (!mapped || !imageBase) {
        MEMORY_BASIC_INFORMATION mbi{};
        const SIZE_T vq = VirtualQueryEx(pi.hProcess, reinterpret_cast<LPCVOID>(imageBase), &mbi, sizeof(mbi));
        return killAndReturn(QStringLiteral(
            "Образ процесса не замапился (VirtualQueryEx). ImageBase=0x%1 wow64=%2 vq=%3 state=0x%4 size=0x%5 err=0x%6.")
            .arg(imageBase, 0, 16).arg(int(wow64)).arg(qulonglong(vq))
            .arg(mbi.State, 0, 16).arg(qulonglong(mbi.RegionSize), 0, 16)
            .arg(GetLastError(), 0, 16));
    }
    if (log) log->append(QStringLiteral("  [3] Образ готов (ImageBase 0x%1); процесс приостановлен.").arg(imageBase, 0, 16));

    const quintptr scanEnd = moduleEnd(pi.hProcess, imageBase);
    if (log) log->append(QStringLiteral("  [4] Скан модуля 0x%1 … 0x%2 (%3 МБ)")
                         .arg(imageBase, 0, 16).arg(scanEnd, 0, 16)
                         .arg((scanEnd > imageBase ? (scanEnd - imageBase) : 0) / (1024 * 1024)));
    QVector<PatchOp> ops;
    int okCount = 0;

    const auto queueSimple = [&](const QByteArray &pattern, QByteArray repl, const char *label) {
        if (repl.size() < pattern.size()) repl.append(pattern.size() - repl.size(), '\0');
        QVector<PatchOp> hits;
        scanMemory(pi.hProcess, imageBase, scanEnd, Pattern(pattern.begin(), pattern.end()),
                   [repl](const QByteArray &) { return repl; }, hits, log);
        for (auto &h : hits) { h.label = label; ops.append(h); }
        if (hits.isEmpty() && log)
            log->append(QStringLiteral("  [-] %1: паттерн не найден (пропуск)").arg(QLatin1String(label)));
    };

    queueSimple(kPatternConnectToModulus, rsa, "ConnectTo RsaModulus");
    if (det.usesEd25519 || !opts.autoDetect)
        queueSimple(kPatternGameCryptoEd25519, ed, "GameCrypto Ed25519");
    if (legacy) {
        queueSimple(kPatternSignatureModulus, rsa, "Signature RsaModulus (legacy)");
        queueSimple(kPatternGameCryptoRsa, rsa, "GameCrypto RsaModulus (legacy)");
    }
    {
        QVector<PatchOp> hits;
        scanMemory(pi.hProcess, imageBase, scanEnd, Pattern(kPatternPortal.begin(), kPatternPortal.end()),
                   [&](const QByteArray &) -> QByteArray {
                       qsizetype buffer = kPatternPortal.size();
                       if (opts.expandPortalBuffer) buffer = 128;
                       QByteArray value;
                       const QString perr = paddedPortal(opts.portal, opts.port, &value, buffer);
                       if (!perr.isEmpty()) {
                           if (log) log->append(QStringLiteral("  ! %1").arg(perr));
                           return QByteArray();
                       }
                       value.append(buffer - value.size(), '\0');
                       return value;
                   }, hits, log);
        for (auto &h : hits) { h.label = "Login Portal"; ops.append(h); }
        if (hits.isEmpty() && log)
            log->append(QStringLiteral("  [-] Portal .actual.battle.net не найден — оставляем SET portal в Config.wtf."));
    }
    if (opts.patchLauncherRegistry)
        queueSimple(kPatternLauncherLogin, kPatchLauncherLogin, "Launcher Login Registry");
    if (opts.patchVersionUrls) {
        queueSimple(kUrlV1 + '\0', (opts.versionUrl.isEmpty() ? kUrlV1 : opts.versionUrl.toUtf8()) + '\0', "Version URL v1");
        queueSimple(kUrlV2 + '\0', (opts.versionUrl.isEmpty() ? kUrlV2 : opts.versionUrl.toUtf8()) + '\0', "Version URL v2");
        queueSimple(kUrlV2New + '\0', (opts.versionUrl.isEmpty() ? kUrlV2New : opts.versionUrl.toUtf8()) + '\0', "Version URL v2 new");
        queueSimple(kCdnsUrl + '\0', (opts.cdnsUrl.isEmpty() ? kCdnsUrl : opts.cdnsUrl.toUtf8()) + '\0', "CDNs URL");
    }
    if (!opts.certBundleUrl.isEmpty() && opts.certBundleUrl.size() <= kPatternCertBundleUrl.size())
        queueSimple(kPatternCertBundleUrl, opts.certBundleUrl.toUtf8(), "Cert bundle URL");

    if (ops.isEmpty())
        return killAndReturn("Не найдено ни одного паттерна — клиент не поддерживается этим набором сигнатур.");

    for (auto &op : ops) {
        if (writeAt(pi.hProcess, op.address, op.bytes, log)) {
            ++okCount;
            if (log) log->append(QStringLiteral("  [+] %1 @ 0x%2 (%3 байт)")
                                 .arg(QString::fromLatin1(op.label)).arg(op.address, 0, 16).arg(op.bytes.size()));
        }
    }
    if (log) log->append(QStringLiteral("Фаза A: применено %1/%2 патчей данных.").arg(okCount).arg(ops.size()));

    ResumeThread(pi.hThread);
    pNtResume(pi.hProcess);
    if (log) log->append(QStringLiteral("  [5] Процесс запущен (ResumeThread), ожидание расшифровки .text..."));

    if (opts.bypassCertValidation && !modern) {
        bool unpacked = false;
        waitForUnpack(pi.hProcess, imageBase, scanEnd, QStringLiteral("  [6] "), opts.waitUnpackMs, log, &unpacked);
        if (!unpacked && log)
            log->append(QStringLiteral("  [-] Сторож расшифровки .text не найден — пробуем cert-патчи как есть."));
        SuspendThread(pi.hThread);
        pNtSuspend(pi.hProcess);
        QVector<PatchOp> rt;
        const auto queueRt = [&](const Pattern &pat, const std::function<QByteArray(const QByteArray &)> &make,
                                 const char *label) {
            QVector<PatchOp> hits;
            scanMemory(pi.hProcess, imageBase, scanEnd, pat, make, hits, log);
            for (auto &h : hits) { h.label = label; rt.append(h); }
            if (hits.isEmpty() && log)
                log->append(QStringLiteral("  [-] %1: не найдено (в этом билде может отсутствовать)").arg(QLatin1String(label)));
        };
        if (legacy) {
            queueRt(kPatIntegrity, ret0Stub, "Integrity (ret 0)");
            queueRt(kPatIntegrityAlt, ret0Stub, "Integrity alt (ret 0)");
        }
        queueRt(kPatCertBundleBranch, nopPair, "CertBundle branch (NOP)");
        queueRt(kPatCertCommonName, movAl1, "CertCommonName (AL=1)");
        queueRt(kPatCertChain, movBl1, "CertChain (BL=1)");
        int applied = 0;
        for (auto &op : rt) {
            if (writeAt(pi.hProcess, op.address, op.bytes, log)) {
                ++applied;
                if (log) log->append(QStringLiteral("  [+] %1 @ 0x%2").arg(QString::fromLatin1(op.label)).arg(op.address, 0, 16));
            }
        }
        if (log) log->append(QStringLiteral("Фаза B: обход сертификата — применено %1/%2.").arg(applied).arg(rt.size()));
        ResumeThread(pi.hThread);
        pNtResume(pi.hProcess);
    } else if (modern && log) {
        log->append(QStringLiteral("  [6] 11.x/12.x: фаза B (Arxan/integrity) пропущена — для статически "
                                   "пропатченного exe она не нужна, а скан упакованного .text вешает процесс."));
    }

    if (log) log->append(QStringLiteral("Готово: клиент запущен с патчами. Файл на диске не изменён."));
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
#endif
}

// ===========================================================================
// 9. TLS-проверка сертификата сервера
// ===========================================================================
bool ClientPatchService::checkPortalTls(const QString &host, int port, QStringList *errors) {
    if (host.isEmpty() || port <= 0) {
        if (errors) errors->append("  ! Пустой host/port.");
        return false;
    }
    if (errors) errors->append(QStringLiteral("Проверка TLS-сертификата %1:%2 …").arg(host).arg(port));
    QSslSocket socket;
    socket.setPeerVerifyMode(QSslSocket::VerifyPeer);
    socket.connectToHostEncrypted(host, quint16(port));
    if (!socket.waitForConnected(3500) || !socket.waitForEncrypted(3500)) {
        if (errors) errors->append(QStringLiteral("  ! %1").arg(socket.errorString()));
        if (errors) errors->append(QStringLiteral("  [i] Для TrinityCore это ожидаемо: bnetserver отдаёт dev-цепочку "
                                                  "с CN=*.* (TrinityCore Battle.net Aurora CA), которую обычный TLS-клиент "
                                                  "не примет. Пропатченный Wow.exe её принимает — эта проверка только "
                                                  "показывает, что порт 1119 вообще слушается."));
        return false;
    }
    const QSslCertificate cert = socket.peerCertificate();
    if (cert.isNull()) {
        if (errors) errors->append("  ! Сервер не прислал сертификат.");
        return false;
    }
    if (errors)
        errors->append(QStringLiteral("  [OK] Сертификат: %1. Порт слушается, TLS поднимается.").arg(
            cert.subjectInfo(QSslCertificate::CommonName).join(QLatin1String(", "))));
    socket.disconnectFromHost();
    return true;
}

// ===========================================================================
// 10. Версия клиента (FileVersionInfo, билды >65535 — как в Arctium)
// ===========================================================================
QString ClientPatchService::clientVersion(const QString &exePath) {
#ifdef Q_OS_WIN
    DWORD handle = 0;
    const QByteArray path = QDir::toNativeSeparators(exePath).toUtf8();
    const DWORD size = GetFileVersionInfoSizeA(path.constData(), &handle);
    if (!size) return QString();
    QByteArray data(static_cast<int>(size), Qt::Uninitialized);
    if (!GetFileVersionInfoA(path.constData(), 0, size, data.data())) return QString();
    VS_FIXEDFILEINFO *info = nullptr;
    UINT infoLen = 0;
    if (!VerQueryValueA(data.constData(), "\\", reinterpret_cast<void **>(&info), &infoLen) || !info)
        return QString();
    int m = HIWORD(info->dwFileVersionMS);
    int n = LOWORD(info->dwFileVersionMS);
    int b = HIWORD(info->dwFileVersionLS);
    int p = LOWORD(info->dwFileVersionLS);
    if (b >= 6553 && p < 0xFFFF) {          // упаковка билда > 65535
        p = b * 10 + p;
        b = n;
        n = m % 100;
        m = m / 100;
    }
    return QStringLiteral("%1.%2.%3.%4").arg(m).arg(n).arg(b).arg(p);
#else
    Q_UNUSED(exePath);
    return QString();
#endif
}


// ===========================================================================
// 11. Извлечение Ed25519-ключа из PEM (SPKI BIT STRING 0x00 + 32 байта)
// ===========================================================================
QString ClientPatchService::ed25519KeyFromPem(const QString &pemPath, QString *error) {
    QFile f(pemPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) *error = QStringLiteral("Не удалось открыть PEM: %1").arg(f.errorString());
        return QString();
    }
    const QString text = QString::fromUtf8(f.readAll());
    f.close();
    std::string err;
    const wowpatch::Bytes key = wowpatch::ed25519PublicFromPem(sstr(text), &err);
    if (key.size() != 32) {
        if (error) *error = qstr(err);
        return QString();
    }
    return QString::fromLatin1(fromCore(key).toHex()).toUpper();
}


// ===========================================================================
// 13. Описание метода для вкладки
// ===========================================================================
QString ClientPatchService::methodSummary() {
    return QStringLiteral(
        "РЕЖИМ «FIRESTORM» — самостоятельный пропатченный Wow.exe на диске.\n"
        "Так сделан, например, «WoW 11.2.5 - Firestorm.exe»: это копия Wow.exe ТОГО ЖЕ размера,\n"
        "в которой точечно заменены байты в секции данных. Запускается двойным щелчком, лаунчер не нужен,\n"
        "адрес сервера берётся из `SET portal` в WTF/Config.wtf.\n"
        "\n"
        "Что правим (клиент 11.x / 12.x, в т.ч. 12.1.0.69497):\n"
        "  1) ConnectTo RSA-модуль, 256 байт в .rdata (little-endian) — клиент проверяет им подпись\n"
        "     SMSG_CONNECT_TO. Подставляем ключ TrinityCore (ConnectToRSA).\n"
        "  2) Ed25519, 32 байта — клиент проверяет им подпись SMSG_ENTER_ENCRYPTED_MODE.\n"
        "     БЕЗ него вход в мир обрывается на переходе к шифрованию. Ключ TrinityCore —\n"
        "     публичная половина EnterEncryptedModePrivateKey.\n"
        "  3) SET portal \"host[:port]\" в WTF/Config.wtf — единственный надёжный способ задать адрес.\n"
        "  4) (опция) суффикс .actual.battle.net -> .actual.<домен ≤10 байт>. Это именно СУФФИКС:\n"
        "     клиент склеивает «eu» + «.actual.battle.net», поэтому писать сюда «127.0.0.1:1119»\n"
        "     целиком нельзя — получится «eu127.0.0.1:1119».\n"
        "\n"
        "Чего НЕ делаем: не правим .text на диске. У ретейла код упакован (Arxan/Digital.ai) и\n"
        "восстанавливается при запуске, поэтому файловые правки кода либо теряются, либо роняют клиент.\n"
        "Движок (src/wow_pe.*) читает таблицу секций и принимает только сайты в .rdata/.data,\n"
        "правит ПЕРВОЕ подходящее вхождение (а не все подряд) и после записи перечитывает файл,\n"
        "подтверждая каждый патч, размер, заголовок и отсутствие родных ключей Blizzard.\n"
        "\n"
        "Сервер: TrinityCore bnetserver со стандартной dev-цепочкой (CN=`*.*`, TrinityCore Battle.net\n"
        "Aurora CA) — она лежит в TrinityCoreWin64vs/bnetserver.cert.pem. Порты: 1119 (bnet),\n"
        "8081 (REST-логин), 8085 (мир). Аккаунт: `bnetaccount create почта пароль`.\n"
        "\n"
        "РЕЖИМ «РЕЦЕПТ» — если у вас есть готовый пропатченный клиент другого билда (тот самый\n"
        "Firestorm.exe) и его оригинал: снимаем точечную разницу, сохраняем в JSON вместе с контекстом\n"
        "и переносим на ваш 12.1.0.69497 поиском по контексту, а не по смещениям. Что не перенесётся —\n"
        "добирается сигнатурами.\n"
        "\n"
        "РЕЖИМ «ПАМЯТЬ» — как Arctium: CreateProcess(CREATE_SUSPENDED) -> PEB -> патчи в памяти ->\n"
        "запуск. Файл не меняется. Нужен, когда exe трогать нельзя, и для legacy-профилей,\n"
        "где требуется runtime-обход проверки сертификата.");
}
