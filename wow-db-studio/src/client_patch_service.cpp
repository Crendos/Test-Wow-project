#include "client_patch_service.h"

#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QSaveFile>
#include <QSslSocket>
#include <QSslCertificate>
#include <QRegularExpression>
#include <QStringList>
#include <QThread>
#include <QVector>

#include <cstring>
#include <functional>
#include <string>
#include <vector>

#ifdef Q_OS_WIN
#  include <windows.h>
#  include <psapi.h>
#  include <winver.h>
#endif

namespace {

// ===========================================================================
// Сигнатуры данных (сверено с Arctium/WoW-Launcher и wowemulation-dev/wow-patcher)
// ===========================================================================
const QByteArray kPatternConnectToModulus  = QByteArray::fromHex("91D59BB7D4E183A5");
const QByteArray kPatternSignatureModulus  = QByteArray::fromHex("35FF17E733C4D3D4");
const QByteArray kPatternGameCryptoRsa     = QByteArray::fromHex("71FDFA60140DF205");
const QByteArray kPatternGameCryptoEd25519 = QByteArray::fromHex("15D618BD7DB577BD");
const QByteArray kPatternPortal            = QByteArray(".actual.battle.net") + '\0';
const QByteArray kPatternLauncherLogin =
    QByteArray("Software\\Blizzard Entertainment\\Battle.net\\Launch Options\\");
const QByteArray kPatternCertBundleUrl =
    QByteArray("http://nydus.battle.net/Bnet/zxx/client/bgs-key-fingerprint");

// ---------------------------------------------------------------------------
// Значения (dev-ключи TrinityCore; Ed25519 = ключ из сертификата 7725530)
// ---------------------------------------------------------------------------
const QByteArray kPatchRsaModulus = QByteArray::fromHex(
    "5FD6800BA7FF0140C7BC8EF56B27B0BF"
    "F01D1BFEDD0B1F3DB66F1A480DFB5108"
    "65584FDB5C6ECF64CBC16B2EB80F5D08"
    "5D8906A9778B9EAA04B08310E2154D08"
    "77D47A0E5AB0BB0061D7A675DF066488"
    "BBB9CAB0188B5413E2CB33DF17D8DAA9"
    "A560A31F4E2705986FAAEE143BF397A8"
    "1202940D84DC0EF17623953613F9A9C5"
    "48DBDA86BE292254449D9F807B078030"
    "EAD283CCCE37D1D1CF85BE9125CEC0CC"
    "55C8C0FB38C549036A02A99F9F86FBC7"
    "CBC6A582A230C2ACE698DA8364437F0D"
    "1318EB90535B376BE60D801EEFEDC7B8"
    "689B4C097B60B257D8598D7FEACDEBC4"
    "609F457AA9268A2F850CF219C65392F7"
    "F0B832CB5B66CE5154B4C3D3D4DCB3EE");

const QByteArray kPatchEd25519Key = QByteArray::fromHex(
    "02596F0D0C061A8B30745988FD72C59E"
    "29EC367FB0F341F28E0F08D037BAFC69");

const QByteArray kPatchLauncherLogin =
    QByteArray("Software\\Arctium WoW Launcher\\Battle.net\\Launch Options\\");

const QByteArray kUrlV1    = "http://%s.patch.battle.net:1119/%s/versions";
const QByteArray kUrlV2    = "https://%s.version.battle.net/v2/products/%s/versions";
const QByteArray kUrlV2New = "https://%s.version.battle.net/v2/products/%s/%s";
const QByteArray kCdnsUrl  = "http://%s.patch.battle.net:1119/%s/cdns";

// ---------------------------------------------------------------------------
// Runtime-паттерны (.text, появляются после расшифровки Arxan; wildcard = -1)
// ---------------------------------------------------------------------------
using Pattern = std::vector<int16_t>;

Pattern pat(std::initializer_list<int16_t> v) { return Pattern(v); }

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

// ===========================================================================
// Вспомогательные функции
// ===========================================================================
// Замена ВСЕХ вхождений find. Если значение короче находки — добивается NUL до
// её длины (чтобы не оставался хвост старой строки); если длиннее (ключи:
// сигнатура 8 байт, значение 256/32 байта) — пишется полное значение, как в
// Arctium/wow-patcher: в файле за сигнатурой идёт остальное поле ключа.
// Возвращает число замен; missingNote пишется в лог, когда вхождений нет.
int replaceAllPattern(QByteArray &data, const QByteArray &find, QByteArray repl,
                      QStringList *log, const QString &label,
                      const QString &missingNote = QString()) {
    if (find.isEmpty()) return 0;
    const bool expand = repl.size() > find.size();
    if (!expand && repl.size() < find.size())
        repl.append(find.size() - repl.size(), '\0');
    int count = 0;
    qsizetype pos = 0;
    while (pos + find.size() <= data.size()) {
        const qsizetype off = data.indexOf(find, pos);
        if (off < 0) break;
        if (expand && off + repl.size() > data.size()) {
            if (log) log->append(QStringLiteral("  ! %1 @ 0x%2: не хватает места (%3 байт), вхождение пропущено")
                                     .arg(label).arg(off, 0, 16).arg(qulonglong(repl.size())));
            pos = off + 1;
            continue;
        }
        // In-place: RSA/Ed25519 are longer than the 8-byte signature but occupy
        // the following key field. Growing the QByteArray would shift the PE and
        // the copy would not start.
        memcpy(data.data() + off, repl.constData(), size_t(repl.size()));
        ++count;
        if (log) log->append(QStringLiteral("[+] %1 @ 0x%2 (%3 байт)")
                                 .arg(label).arg(off, 0, 16).arg(qulonglong(repl.size())));
        pos = off + qsizetype(repl.size());
    }
    if (!count && !missingNote.isEmpty() && log)
        log->append(missingNote);
    return count;
}

QString normalizePortalHost(QString text) {
    text = text.trimmed();
    if (text.startsWith(QLatin1String("http://"), Qt::CaseInsensitive)) text = text.mid(7);
    if (text.startsWith(QLatin1String("https://"), Qt::CaseInsensitive)) text = text.mid(8);
    while (text.endsWith(QLatin1Char('/'))) text.chop(1);
    const int slash = text.indexOf(QLatin1Char('/'));
    if (slash >= 0) text = text.left(slash);
    return text.trimmed();
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
    return QStringLiteral("Портал «%1» не влезает в слот %2 байт (с NUL). "
                          "Бинарный патч портала будет пропущен — клиент возьмёт SET portal из WTF/Config.wtf. "
                          "Короткий IP:порт влезает в слот; длинный хост оставляйте в Config.wtf.")
        .arg(text).arg(maxLen);
}

// ===========================================================================
// Профиль: подбор набора сигнатур по билду
// ===========================================================================
struct ParsedVersion { int major = 0, minor = 0, patch = 0, build = 0; };
ParsedVersion parseVersion(const QString &v) {
    ParsedVersion r;
    if (v.isEmpty()) return r;
    const QStringList parts = v.split('.');
    if (parts.size() >= 1) r.major = parts[0].toInt();
    if (parts.size() >= 2) r.minor = parts[1].toInt();
    if (parts.size() >= 3) r.patch = parts[2].toInt();
    if (parts.size() >= 4) r.build = parts[3].toInt();
    return r;
}

} // namespace

// ===========================================================================
// Автоопределение профиля (ветка по пути, диапазон по версии)
// ===========================================================================
WowDetection ClientPatchService::detectProfile(const QString &exePath) {
    WowDetection d;
    d.branch = QStringLiteral("Unknown");
    d.profile = QStringLiteral("modern");
    d.legacyCert = false;
    d.usesEd25519 = true;

    const QString lower = exePath.toLower();
    const QString exeName = QFileInfo(exePath).fileName();
    if (lower.contains(QStringLiteral("_retail_"))) {
        d.branch = QStringLiteral("Retail");
    } else if (exeName.compare(QStringLiteral("Wow.exe"), Qt::CaseInsensitive) == 0
            || exeName.compare(QStringLiteral("Wow-64.exe"), Qt::CaseInsensitive) == 0) {
        d.branch = QStringLiteral("Client");
    } else if (lower.contains(QStringLiteral("_classic_titan_"))) {
        d.branch = QStringLiteral("Titan");
    } else if (lower.contains(QStringLiteral("_anniversary_"))) {
        d.branch = QStringLiteral("Anniversary");
    } else if (lower.contains(QStringLiteral("_classic_era_"))) {
        d.branch = QStringLiteral("Classic Era");
    } else if (lower.contains(QStringLiteral("_classic_")) || lower.contains(QStringLiteral("wowclassic"))) {
        d.branch = QStringLiteral("Classic");
    }

    d.version = clientVersion(exePath);
    const ParsedVersion v = parseVersion(d.version);

    const bool classicFamily = d.branch == QStringLiteral("Classic")
                            || d.branch == QStringLiteral("Classic Era")
                            || d.branch == QStringLiteral("Anniversary")
                            || d.branch == QStringLiteral("Titan");
    const bool retail = d.branch == QStringLiteral("Retail")
                     || d.branch == QStringLiteral("Client")
                     || d.branch == QStringLiteral("Unknown");

    if (classicFamily) {
        // Classic (1.13.x) НЕ содержит Ed25519 и НЕ переживает патч Signature/Crypto RSA.
        if (v.major == 1 && v.minor <= 13) {
            d.profile = QStringLiteral("classic13");
            d.legacyCert = false;
            d.usesEd25519 = false;
            d.note = QStringLiteral("Classic 1.13.x: только ConnectTo RSA + portal из Config.wtf (SET portal). Обход сертификата не применяется.");
        } else {
            // 1.14.x / 2.5.x / 3.4.x–5.5.x / 4.4.x / Titan: legacy-режим с обходом сертификата.
            d.profile = QStringLiteral("legacy");
            d.legacyCert = true;
            d.usesEd25519 = v.major >= 3; // 3.4.x+ содержит Ed25519; 1.14/2.5 — нет (проверено, отсутствие безопасно)
            d.note = QStringLiteral("Legacy-профиль: Signature/Crypto RSA + runtime-обход проверки сертификата (нужно для локального/своего IP).");
        }
    } else if (retail) {
        if (v.major >= 6 && v.major <= 8) {
            d.profile = QStringLiteral("legion");
            d.legacyCert = true;
            d.usesEd25519 = false;
            d.note = QStringLiteral("Legion/BfA 6.x–8.x: ConnectTo RSA + SET portal в Config.wtf. "
                                    "Длинный хост в exe не патчится (слот ~19 байт) — портал берётся из WTF.");
        } else if (v.major == 9 || v.major == 10) {
            d.profile = QStringLiteral("legacy");
            d.legacyCert = true;
            d.usesEd25519 = true;
            d.note = QStringLiteral("Retail 9.x–10.x: legacy-профиль (Signature/Crypto RSA + обход сертификата).");
        } else if (v.major >= 11) {
            d.profile = QStringLiteral("modern");
            d.legacyCert = false;
            d.usesEd25519 = true;
            d.note = QStringLiteral("Retail 11.x/12.x (в т.ч. 12.1.0): патч как Arctium — в ПАМЯТИ (CreateProcess + RSA/Ed25519). "
                                    "Копия exe на диск часто не стартует (упаковка/подпись). "
                                    "Длинный хост (auth.eracolgar.su) не влезает в слот .actual.battle.net — "
                                    "клиент берёт SET portal из WTF/Config.wtf. Для самоподписанного bnet включите обход сертификата.");
        } else {
            d.profile = QStringLiteral("modern");
            d.note = QStringLiteral("Ветка/версия не распознаны — используется современный профиль с попыткой всех сигнатур.");
        }
    }
    return d;
}

// ===========================================================================
// 1. Консервативный режим: ASCII-замена в копии файла (как было)
// ===========================================================================
bool ClientPatchService::replaceAsciiInCopy(const QString &src, const QString &dst,
                                            const QString &find, const QString &replacement,
                                            QString *error) {
    if (find.isEmpty() || replacement.toUtf8().size() > find.toUtf8().size()) {
        if (error) *error = "Replacement must fit the original ASCII field.";
        return false;
    }
    QFile in(src);
    if (!in.open(QIODevice::ReadOnly)) { if (error) *error = in.errorString(); return false; }
    QByteArray b = in.readAll();
    const auto f = find.toUtf8();
    const qsizetype pos = b.indexOf(f);
    if (pos < 0) { if (error) *error = "ASCII value was not found in this file."; return false; }
    b.replace(pos, f.size(), replacement.toUtf8().leftJustified(f.size(), '\0'));
    QFile out(dst);
    if (!out.open(QIODevice::WriteOnly)) { if (error) *error = out.errorString(); return false; }
    if (out.write(b) != b.size()) { if (error) *error = out.errorString(); return false; }
    return true;
}

// ===========================================================================
// 2. Патч по сигнатурам в КОПИИ файла (диск). Оригинал не изменяется.
// ===========================================================================
bool ClientPatchService::patchFileCopy(const QString &source, const QString &destination,
                                       const WowPatchOptions &opts, QStringList *log, QString *error) {
    if (!QFileInfo::exists(source)) {
        if (error) *error = QStringLiteral("Файл не найден: %1").arg(source);
        return false;
    }
    if (log) log->append(QStringLiteral("Патч копии файла (на диск): %1 -> %2").arg(source, destination));

    QFile in(source);
    if (!in.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("Не удалось открыть: %1").arg(in.errorString());
        return false;
    }
    QByteArray data = in.readAll();
    in.close();
    if (data.size() < 0x1000) {
        if (error) *error = "Файл слишком мал — это не похоже на Wow.exe.";
        return false;
    }

    // --- определённые ключи ---
    QByteArray rsa = kPatchRsaModulus, ed = kPatchEd25519Key;
    if (!opts.rsaModulusHex.trimmed().isEmpty()) {
        QString e;
        rsa = hexKey(opts.rsaModulusHex.trimmed(), 256, &e);
        if (rsa.isEmpty()) { if (error) *error = "RSA-ключ: " + e; return false; }
    }
    if (!opts.ed25519KeyHex.trimmed().isEmpty()) {
        QString e;
        ed = hexKey(opts.ed25519KeyHex.trimmed(), 32, &e);
        if (ed.isEmpty()) { if (error) *error = "Ed25519-ключ: " + e; return false; }
    }

    // --- профиль ---
    const WowDetection det = opts.autoDetect ? detectProfile(source) : WowDetection{};
    const bool legacy = opts.autoDetect ? det.legacyCert : opts.patchLegacyGameCryptoRsa;
    if (opts.autoDetect) {
        if (log) log->append(QStringLiteral("Профиль: [%1] версия %2, ветка %3%4")
                             .arg(det.profile, det.version.isEmpty() ? QStringLiteral("?") : det.version,
                                  det.branch, det.note.isEmpty() ? QString() : QStringLiteral(" — ") + det.note));
        if (log && det.profile == QLatin1String("modern"))
            log->append(QStringLiteral("  ! 12.x: копия на диск — RSA/Ed25519/portal на месте (размер PE не меняется). "
                                       "Сохраните как Wow.exe рядом с Data. Integrity/античит не патчатся."));
    }

    // --- папка Data берётся из каталога самого Wow.exe ---
    bool hasData = false;
    const QString dataDir = clientDataDir(source, &hasData);
    if (log) log->append(QStringLiteral("Папка Data рядом с клиентом: %1%2")
                             .arg(QDir::toNativeSeparators(dataDir),
                                  hasData ? QString() : QStringLiteral(" — НЕ НАЙДЕНА!")));
    const QString srcDir = QFileInfo(source).absolutePath();
    const QString dstDir = QFileInfo(destination).absolutePath();
    if (srcDir != dstDir && log)
        log->append(QStringLiteral("  ! Копия сохраняется НЕ рядом с исходным Wow.exe (%1). Клиент из этой копии будет искать Data рядом с ней (%2\\Data), а не рядом с оригиналом. Либо сохраните копию рядом с Wow.exe, либо положите папку Data рядом с копией.")
                        .arg(QDir::toNativeSeparators(dstDir), QDir::toNativeSeparators(srcDir)));

    // --- RSA ConnectTo (обязательный паттерн данных) ---
    replaceAllPattern(data, kPatternConnectToModulus, rsa, log,
                      QStringLiteral("ConnectTo RsaModulus"),
                      QStringLiteral("[-] ConnectTo RsaModulus: сигнатура не найдена (другой билд?)"));

    // --- Ed25519 (присутствует не во всех ветках; отсутствие — предупреждение) ---
    replaceAllPattern(data, kPatternGameCryptoEd25519, ed, log,
                      QStringLiteral("GameCrypto Ed25519"),
                      det.usesEd25519
                          ? QStringLiteral("[-] GameCrypto Ed25519: не найдено (пропуск)")
                          : QStringLiteral("[-] GameCrypto Ed25519: не ожидается в этой ветке (пропуск)"));

    // --- Signature / Crypto RSA: только legacy-профиль (1.13.x — нельзя!) ---
    if (legacy) {
        replaceAllPattern(data, kPatternSignatureModulus, rsa, log,
                          QStringLiteral("Signature RsaModulus (legacy)"),
                          QStringLiteral("[-] Signature RsaModulus (legacy): не найдено"));
        replaceAllPattern(data, kPatternGameCryptoRsa, rsa, log,
                          QStringLiteral("GameCrypto RsaModulus (legacy)"),
                          QStringLiteral("[-] GameCrypto RsaModulus (legacy): не найдено"));
    }

    // --- Portal (optional): long host does not fit .actual.battle.net; 7.x uses Config.wtf ---
    {
        QByteArray value;
        const QString portalError = paddedPortal(opts.portal, opts.port, &value, kPatternPortal.size());
        if (!portalError.isEmpty()) {
            if (log) log->append(QStringLiteral("  ! %1").arg(portalError));
        } else if (replaceAllPattern(data, kPatternPortal, value, log,
                                     QStringLiteral("Login Portal -> %1").arg(QString::fromUtf8(value.constData()))) == 0) {
            if (log) log->append(QStringLiteral("  [-] Portal .actual.battle.net не найден — SET portal в Config.wtf (так работает 7.3.5)."));
        }
        const QString dstName = QFileInfo(destination).fileName();
        const QString srcName = QFileInfo(source).fileName();
        if (log && dstName.compare(srcName, Qt::CaseInsensitive) != 0)
            log->append(QStringLiteral("  ! Копия названа «%1», а не «%2». Часть клиентов запускается только как Wow.exe / Wow-64.exe — сохраните копию с тем же именем в другой папке рядом с Data.")
                            .arg(dstName, srcName));
    }

    // --- Launcher Login Registry (опционально) ---
    replaceAllPattern(data, kPatternLauncherLogin, kPatchLauncherLogin, log,
                      QStringLiteral("Launcher Login Registry"));

    // --- Version / CDN URL (опционально) ---
    if (opts.patchVersionUrls) {
        const QByteArray v1 = (opts.versionUrl.isEmpty() ? kUrlV1 : opts.versionUrl.toUtf8()) + '\0';
        const QByteArray v2 = (opts.versionUrl.isEmpty() ? kUrlV2 : opts.versionUrl.toUtf8()) + '\0';
        const QByteArray v2new = (opts.versionUrl.isEmpty() ? kUrlV2New : opts.versionUrl.toUtf8()) + '\0';
        const QByteArray cdns = (opts.cdnsUrl.isEmpty() ? kCdnsUrl : opts.cdnsUrl.toUtf8()) + '\0';
        struct UrlPatch { QByteArray find; QByteArray repl; const char *name; };
        const UrlPatch urls[] = {
            { kUrlV1 + '\0', v1, "Version URL v1" },
            { kUrlV2 + '\0', v2, "Version URL v2" },
            { kUrlV2New + '\0', v2new, "Version URL v2 new" },
            { kCdnsUrl + '\0', cdns, "CDNs URL" },
        };
        for (const auto &u : urls) {
            replaceAllPattern(data, u.find, u.repl, log, QString::fromLatin1(u.name));
        }
    }

    // --- Cert bundle URL (опционально) ---
    if (!opts.certBundleUrl.isEmpty() && opts.certBundleUrl.size() <= kPatternCertBundleUrl.size()) {
        replaceAllPattern(data, kPatternCertBundleUrl, opts.certBundleUrl.toUtf8(), log,
                          QStringLiteral("Cert bundle URL"));
    }

    QSaveFile out(destination);
    if (!out.open(QIODevice::WriteOnly)) {
        if (error) *error = QStringLiteral("Не удалось создать выходную копию: %1").arg(out.errorString());
        return false;
    }
    out.write(data);
    if (!out.commit()) {
        if (error) *error = QStringLiteral("Не удалось сохранить копию: %1").arg(out.errorString());
        return false;
    }
    if (log) log->append(QStringLiteral("Готово. Копия сохранена: %1").arg(destination));
    return true;
}

#ifdef Q_OS_WIN
namespace {

// ===========================================================================
// Фаза A/B: скан памяти по регионам (VirtualQueryEx) и запись патчей
// ===========================================================================
struct PatchOp {
    const char *label;      // статическая строка
    quintptr address = 0;
    QByteArray bytes;
};

constexpr int SCAN_MAX_REGIONS = 4096;
constexpr qsizetype MAX_REGION_READ = 32 * 1024 * 1024; // 32 MiB на регион

bool matchAt(const QByteArray &buf, qsizetype off, const Pattern &pat) {
    if (off + qsizetype(pat.size()) > buf.size()) return false;
    for (qsizetype i = 0; i < qsizetype(pat.size()); ++i) {
        if (pat[size_t(i)] >= 0 && quint8(buf.at(off + i)) != quint8(pat[size_t(i)]))
            return false;
    }
    return true;
}

quintptr moduleEnd(HANDLE h, quintptr base) {
    if (!base) return 0;
    quintptr end = base;
    quintptr addr = base;
    for (int i = 0; i < SCAN_MAX_REGIONS; ++i) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQueryEx(h, reinterpret_cast<LPCVOID>(addr), &mbi, sizeof(mbi)) || !mbi.RegionSize)
            break;
        if (quintptr(mbi.AllocationBase) != base)
            break;
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
    bool exact = true;
    for (int16_t v : pat) { if (v < 0) { exact = false; break; } }
    QByteArray needle;
    int leadOff = 0;
    char leadByte = 0;
    if (exact) {
        needle.resize(int(pat.size()));
        for (int i = 0; i < needle.size(); ++i) needle[i] = char(quint8(pat[size_t(i)]));
    } else {
        for (size_t i = 0; i < pat.size(); ++i) {
            if (pat[i] >= 0) { leadOff = int(i); leadByte = char(pat[i]); break; }
        }
    }
    quintptr addr = start;
    int regions = 0;
    while (addr < end && regions < SCAN_MAX_REGIONS) {
        ++regions;
        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQueryEx(h, reinterpret_cast<LPCVOID>(addr), &mbi, sizeof(mbi)) || !mbi.RegionSize)
            break;
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
                            if (!bytes.isEmpty())
                                out.push_back({ nullptr, chunk + quintptr(hit), bytes });
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
                                const QByteArray bytes = make(buf.mid(off, qsizetype(pat.size())));
                                if (!bytes.isEmpty())
                                    out.push_back({ nullptr, chunk + quintptr(off), bytes });
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

// Замена "первых N байт" паттерна (для runtime-сайтов).
QByteArray replaceFirst(const QByteArray &matched, QByteArray prefix) {
    QByteArray out = matched;
    for (int i = 0; i < prefix.size() && i < out.size(); ++i)
        out[i] = prefix.at(i);
    return out;
}

// C2 00 00 = ret 0 (пролог функции целостности).
QByteArray ret0Stub(const QByteArray &matched) { return replaceFirst(matched, QByteArray("\xC2\x00\x00", 3)); }
// 90 90 — успешная ветка всегда.
QByteArray nopPair(const QByteArray &matched) { return replaceFirst(matched, QByteArray("\x90\x90", 2)); }
// B0 01 = mov al, 1 (CN всегда совпадает).
QByteArray movAl1(const QByteArray &matched) { return replaceFirst(matched, QByteArray("\xB0\x01", 2)); }
// B3 01 EB 02 = mov bl,1; jmp +2 (цепочка всегда ок).
QByteArray movBl1(const QByteArray &matched) { return replaceFirst(matched, QByteArray("\xB3\x01\xEB\x02", 4)); }

// Ожидание расшифровки .text (сторож init после TLS-колбэка Arxan).
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
// 3. Патч в памяти: CreateProcess(CREATE_SUSPENDED) → PEB → фаза A → фаза B
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

    // --- ключи ---
    QByteArray rsa = kPatchRsaModulus, ed = kPatchEd25519Key;
    if (!opts.rsaModulusHex.trimmed().isEmpty()) {
        QString e;
        rsa = hexKey(opts.rsaModulusHex.trimmed(), 256, &e);
        if (rsa.isEmpty()) { if (error) *error = "RSA-ключ: " + e; return false; }
    }
    if (!opts.ed25519KeyHex.trimmed().isEmpty()) {
        QString e;
        ed = hexKey(opts.ed25519KeyHex.trimmed(), 32, &e);
        if (ed.isEmpty()) { if (error) *error = "Ed25519-ключ: " + e; return false; }
    }

    // --- профиль ---
    const WowDetection det = opts.autoDetect ? detectProfile(exePath) : WowDetection{};
    bool legacy = opts.autoDetect ? det.legacyCert : opts.patchLegacyGameCryptoRsa;
    const bool modern = (det.profile == QLatin1String("modern"));
    if (!legacy && opts.bypassCertValidation)
        legacy = true; // пользователь явно просит обход — пробуем legacy-паттерны (отсутствие безопасно)
    if (opts.autoDetect && log) log->append(QStringLiteral("Профиль: [%1] версия %2, ветка %3%4")
                                            .arg(det.profile, det.version.isEmpty() ? QStringLiteral("?") : det.version,
                                                 det.branch, det.note.isEmpty() ? QString() : QStringLiteral(" — ") + det.note));

    // --- папка Data всегда берётся из каталога самого Wow.exe ---
    {
        bool hasData = false;
        const QString dataDir = clientDataDir(exePath, &hasData);
        if (log) log->append(QStringLiteral("Папка Data рядом с клиентом: %1%2")
                                 .arg(QDir::toNativeSeparators(dataDir),
                                      hasData ? QString() : QStringLiteral(" — НЕ НАЙДЕНА! Клиент не запустится без неё.")));
    }

    // --- Config.wtf: пишем portal КАК ВВЕЛИ (без принудительного :1119) ---
    if (opts.writeConfigWtf) {
        QString cfgErr;
        const QString existing = readPortalFromConfigWtf(exePath, &cfgErr);
        const QString want = normalizePortalHost(opts.portal);
        if (!want.isEmpty() && existing != want) {
            if (!writePortalToConfigWtf(exePath, want, &cfgErr)) {
                if (log) log->append(QStringLiteral("  ! Config.wtf: %1").arg(cfgErr));
            } else if (log) log->append(QStringLiteral("  [OK] SET portal \"%1\" записан в WTF/Config.wtf (как есть, порт не дописывается)").arg(want));
        } else if (log && existing == want && !want.isEmpty()) {
            log->append(QStringLiteral("  [OK] SET portal \"%1\" уже стоит в Config.wtf — не трогаем").arg(existing));
        }
    }

    // --- 1) CreateProcessW с CREATE_SUSPENDED (Unicode-пути; A+utf8 ломает кириллицу) ---
    const QString dir = QFileInfo(exePath).absolutePath();
    const QString cmdLine = QStringLiteral("\"%1\" -config Config.wtf%2").arg(
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

    // --- 2) NtQueryInformationProcess → PEB → ImageBase ---
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
            if (pNtQuery(pi.hProcess, 26, &peb32, sizeof(peb32), &n) != 0 || !peb32)
                return 0;
            quint32 base32 = 0;
            if (!ReadProcessMemory(pi.hProcess, reinterpret_cast<LPCVOID>(peb32 + 0x08),
                                   &base32, sizeof(base32), &nread) || nread != sizeof(base32))
                return 0;
            return quintptr(base32);
        }
#endif
        if (!ReadProcessMemory(pi.hProcess, reinterpret_cast<LPCVOID>(quintptr(pbi.PebBaseAddress) + 0x10),
                               &base, sizeof(base), &nread) || nread != sizeof(base))
            return 0;
        return base;
    };

    auto regionMapped = [&](quintptr addr) -> bool {
        if (!addr) return false;
        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQueryEx(pi.hProcess, reinterpret_cast<LPCVOID>(addr), &mbi, sizeof(mbi)))
            return false;
        return mbi.State == MEM_COMMIT
            && (quintptr(mbi.AllocationBase) == addr || quintptr(mbi.BaseAddress) == addr);
    };

    quintptr imageBase = readImageBase();
    if (log) log->append(QStringLiteral("  [2] ImageBase = 0x%1 (%2)")
                         .arg(imageBase, 0, 16)
                         .arg(wow64 ? QStringLiteral("WOW64 32-bit") : QStringLiteral("native")));

    // CREATE_SUSPENDED holds the main thread; NtResumeProcess does not release it.
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
            "Образ процесса не замапился (VirtualQueryEx). ImageBase=0x%1 wow64=%2 vq=%3 state=0x%4 size=0x%5 err=0x%6. "
            "Для 7.3.5 достаточно SET portal в Config.wtf и запуска Wow.exe напрямую.")
            .arg(imageBase, 0, 16).arg(int(wow64)).arg(qulonglong(vq))
            .arg(mbi.State, 0, 16).arg(qulonglong(mbi.RegionSize), 0, 16)
            .arg(GetLastError(), 0, 16));
    }
    if (log) log->append(QStringLiteral("  [3] Образ готов (ImageBase 0x%1); процесс приостановлен.").arg(imageBase, 0, 16));

    // --- 4) ФАЗА A: патчи данных (вся память, с wildcard-паттернами) ---
    const quintptr scanEnd = moduleEnd(pi.hProcess, imageBase);
    if (log) log->append(QStringLiteral("  [4] Скан модуля 0x%1 … 0x%2 (%3 МБ)")
                         .arg(imageBase, 0, 16).arg(scanEnd, 0, 16)
                         .arg((scanEnd > imageBase ? (scanEnd - imageBase) : 0) / (1024 * 1024)));
    QVector<PatchOp> ops;
    int okCount = 0;

    const auto queueSimple = [&](const QByteArray &pattern, QByteArray repl, const char *label) {
        // Ключи длиннее сигнатуры (8 байт -> 256/32) — пишем полное значение;
        // короче — добиваем NUL до размера сайта, иначе останется хвост старой строки.
        if (repl.size() < pattern.size())
            repl.append(pattern.size() - repl.size(), '\0');
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
    // Portal
    {
        QVector<PatchOp> hits;
        scanMemory(pi.hProcess, imageBase, scanEnd, Pattern(kPatternPortal.begin(), kPatternPortal.end()),
                   [&](const QByteArray &) -> QByteArray {
                       qsizetype buffer = kPatternPortal.size();
                       if (opts.expandPortalBuffer) buffer = 128; // расширение — только по явному флагу
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

    // --- 5) ResumeThread: CREATE_SUSPENDED снимается только так ---
    ResumeThread(pi.hThread);
    pNtResume(pi.hProcess);
    if (log) log->append(QStringLiteral("  [5] Процесс запущен (ResumeThread), ожидание расшифровки .text..."));

    // --- 6) ФАЗА B только для legacy. 12.1.0: не сканируем Arxan (висит), не трогаем integrity. ---
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
        log->append(QStringLiteral("  [6] 12.x: фаза B (Arxan/integrity) пропущена — иначе зависание и «клиент сломан»."));
    }

    if (log) log->append(QStringLiteral("Готово: клиент запущен с патчами. Файл на диске не изменён."));
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
#endif
}

// ===========================================================================
// 3b. Папка Data рядом с exe (клиент ищет её в каталоге самого Wow.exe)
// ===========================================================================
QString ClientPatchService::clientDataDir(const QString &exePath, bool *existsOut) {
    const QString dir = QFileInfo(exePath).absolutePath();
    const QString data = dir + QStringLiteral("/Data");
    if (existsOut) {
        const QFileInfo fi(data);
        *existsOut = fi.exists() && fi.isDir();
    }
    return data;
}

// ===========================================================================
// 4. TLS-проверка сертификата сервера (QSslSocket, Qt Network)
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
        return false;
    }
    const QSslCertificate cert = socket.peerCertificate();
    if (cert.isNull()) {
        if (errors) errors->append("  ! Сервер не прислал сертификат.");
        return false;
    }
    if (errors) {
        errors->append(QStringLiteral("  [OK] Сертификат: %1. Для локального запуска/своего IP лучше включить «Обход проверки сертификата» — тогда подойдёт и самоподписанный сертификат вашего bnet-сервера.")
                       .arg(cert.subjectInfo(QSslCertificate::CommonName).join(QLatin1String(", "))));
    }
    socket.disconnectFromHost();
    return true;
}

// ===========================================================================
// 5. Чтение SET portal из WTF/Config.wtf
// ===========================================================================
QString ClientPatchService::readPortalFromConfigWtf(const QString &exePath, QString *error) {
    const QDir dir = QFileInfo(exePath).absoluteDir();
    QString config;
    for (const QString &candidate : { dir.filePath(QStringLiteral("WTF/Config.wtf")),
                                      dir.filePath(QStringLiteral("Config.wtf")) }) {
        if (QFileInfo::exists(candidate)) { config = candidate; break; }
    }
    if (config.isEmpty()) {
        if (error) *error = "Config.wtf не найден рядом с клиентом (папки WTF нет).";
        return QString();
    }
    QFile f(config);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) *error = QStringLiteral("Не удалось открыть Config.wtf: %1").arg(f.errorString());
        return QString();
    }
    const QString text = QString::fromUtf8(f.readAll());
    const QRegularExpression re(QStringLiteral("(?i)SET\\s+portal\\s+\"([^\"]+)\""));
    const QRegularExpressionMatch m = re.match(text);
    if (!m.hasMatch()) {
        if (error) *error = "В Config.wtf нет строки SET portal \"host:port\".";
        return QString();
    }
    return m.captured(1);
}

// ===========================================================================
// 6. Запись SET portal в WTF/Config.wtf (с бэкапом .bak)
// ===========================================================================
bool ClientPatchService::writePortalToConfigWtf(const QString &exePath, const QString &portal, QString *error) {
    const QDir dir = QFileInfo(exePath).absoluteDir();
    QString config;
    for (const QString &candidate : { dir.filePath(QStringLiteral("WTF/Config.wtf")),
                                      dir.filePath(QStringLiteral("Config.wtf")) }) {
        if (QFileInfo::exists(candidate)) { config = candidate; break; }
    }
    if (config.isEmpty()) {
        // создаём WTF/Config.wtf
        QDir wtf(dir);
        if (!wtf.mkpath(QStringLiteral("WTF"))) {
            if (error) *error = QStringLiteral("Не удалось создать папку WTF рядом с клиентом: %1").arg(dir.absolutePath());
            return false;
        }
        config = dir.filePath(QStringLiteral("WTF/Config.wtf"));
    }
    QString text;
    if (QFileInfo::exists(config)) {
        QFile f(config);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) text = QString::fromUtf8(f.readAll());
        // бэкап
        QFile::copy(config, config + QStringLiteral(".bak"));
    }
    const QRegularExpression re(QStringLiteral("(?im)^\\s*SET\\s+portal\\s+\"[^\"]*\"\\s*"));
    if (re.match(text).hasMatch())
        text.replace(re, QStringLiteral("SET portal \"%1\"").arg(portal));
    else
        text += QStringLiteral("SET portal \"%1\"\n").arg(portal);

    QSaveFile out(config);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) *error = QStringLiteral("Не удалось открыть Config.wtf для записи: %1").arg(out.errorString());
        return false;
    }
    out.write(text.toUtf8());
    if (!out.commit()) {
        if (error) *error = QStringLiteral("Не удалось сохранить Config.wtf: %1").arg(out.errorString());
        return false;
    }
    return true;
}

// ===========================================================================
// 7. Версия клиента (FileVersionInfo, билды >65535 — как в Arctium)
// ===========================================================================
QString ClientPatchService::clientVersion(const QString &exePath) {
#ifdef Q_OS_WIN
    DWORD handle = 0;
    const QByteArray path = exePath.toUtf8();
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
// 8. Извлечение Ed25519-ключа из PEM-сертификата (SPKI BIT STRING 0x20)
// ===========================================================================
QString ClientPatchService::ed25519KeyFromPem(const QString &pemPath, QString *error) {
    QFile f(pemPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) *error = QStringLiteral("Не удалось открыть PEM: %1").arg(f.errorString());
        return QString();
    }
    QString pem = QString::fromUtf8(f.readAll());
    pem.remove(QRegularExpression(QStringLiteral("-----BEGIN[^-]+-----|-----END[^-]+-----|\\s")));
    const QByteArray der = QByteArray::fromBase64(pem.toUtf8());
    // SPKI: SEQUENCE { SEQUENCE { OID Ed25519 (1.3.101.112) }, BIT STRING 0x00 <32 байта> }
    const int pos = der.indexOf(QByteArray("\x03\x20", 2));
    if (pos < 0 || pos + 2 + 32 > der.size()) {
        if (error) *error = "В сертификате не найден ключ Ed25519 (BIT STRING 32 байта).";
        return QString();
    }
    const QByteArray key = der.mid(pos + 2, 32);
    return QString::fromLatin1(key.toHex().toUpper());
}

// ===========================================================================
// 9. Hex -> QByteArray с проверкой длины
// ===========================================================================
QByteArray ClientPatchService::hexKey(const QString &hex, int expectedBytes, QString *error) {
    QByteArray b = QByteArray::fromHex(hex.toLatin1().trimmed());
    if (b.size() != expectedBytes) {
        if (error) *error = QStringLiteral("ожидается %1 байт (%2 hex-символов), получено %3.")
                                .arg(expectedBytes).arg(expectedBytes * 2).arg(b.size());
        return QByteArray();
    }
    return b;
}

// ===========================================================================
// 10. Описание метода для вкладки
// ===========================================================================
QString ClientPatchService::methodSummary() {
    return QStringLiteral(
        "МЕТОД (изучен по Arctium Game Launcher 1.5.3.195; сверен с wowemulation-dev/wow-patcher "
        "и исходником Arctium/WoW-Launcher, MIT).\n"
        "\n"
        "  1) CreateProcessA(Wow.exe, CREATE_SUSPENDED) — процесс создан, но не запущен;\n"
        "  2) NtQueryInformationProcess → PEB → ImageBase (PEB+0x10);\n"
        "  3) короткий resume (загрузка образа) → NtSuspendProcess;\n"
        "  4) ФАЗА A: скан памяти (VirtualQueryEx) + запись патчей данных:\n"
        "     ConnectTo RSA, Signature/Crypto RSA (legacy), Ed25519, portal, реестр,\n"
        "     version/CDN URL, cert-bundle URL — VirtualProtectEx/WriteProcessMemory;\n"
        "  5) resume → TLS-колбэк Arxan расшифровывает .text;\n"
        "  6) ФАЗА B (опция «Обход сертификата»): Integrity → ret, CertBundle JZ → NOP,\n"
        "     CertCommonName → AL=1, CertChain → BL=1 — принимает самоподписанный\n"
        "     сертификат вашего bnet-сервера (нужно для localhost / своего IP).\n"
        "\n"
        "АВТОПОДБОР: ветка по пути (_retail_/_classic_/_anniversary_/…), версия по "
        "VersionInfo, профиль по диапазону (1.13.x — только ConnectTo + portal; "
        "1.14+/2.5.x/3.4.x/4.4.x/9.x-10.x — legacy; 11.x+ — современный).\n"
        "\n"
        "ЛОКАЛЬНО / СВОЙ IP — ДА: portal может быть 127.0.0.1:1119, localhost:1119 "
        "или IP:1119 (влезает в слот 19 байт), плюс запись SET portal в WTF/Config.wtf. "
        "Для самоподписанного сертификата включите «Обход проверки сертификата» — "
        "иначе современный клиент отклонит сертификат. Оригинал файла никогда не изменяется.");
}
