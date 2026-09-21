// ---------------------------------------------------------------------------
// wow_patch_selftest.cpp — автономная проверка ВСЕЙ логики патча (src/wow_patch_core.cpp).
//
// Собирается БЕЗ Qt, обычным компилятором:
//   g++ -std=c++20 -O2 -I../src -I. wow_patch_selftest.cpp ../src/wow_patch_core.cpp ../src/wow_pe.cpp -o wow_patch_selftest
//   ./wow_patch_selftest
// (в CMake есть цель wow_patch_selftest, см. CMakeLists.txt)
//
// Что проверяется на синтетическом PE (tools/wow_test_fixture.h):
//   * ключи TrinityCore: LE-модуль = разворот BE-модуля, длины, отличие сигнатур;
//   * профиль клиента по версии/пути (12.1.0.69497 -> modern12);
//   * портал: host-only, домен для суффикса, значения слотов, Config.wtf;
//   * PEM -> DER: PKCS#1, PKCS#8, SPKI, сертификат; Ed25519 SPKI;
//   * патч образа: ключи заменены, decoy в .text НЕ тронут, размер тот же,
//     верификация чистая, родных ключей Blizzard не осталось;
//   * рецепт: снятие разницы, JSON туда-обратно, перенос на «другой билд»;
//   * CheckSum и снятие Authenticode-подписи.
// ---------------------------------------------------------------------------

#include "wow_patch_core.h"
#include "wow_test_fixture.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

int g_failures = 0;
int g_checks = 0;

void check(bool ok, const std::string &what) {
    ++g_checks;
    if (!ok) { ++g_failures; std::printf("  [FAIL] %s\n", what.c_str()); }
    else     { std::printf("  [ ok ] %s\n", what.c_str()); }
}

void checkEqStr(const std::string &got, const std::string &want, const std::string &what) {
    ++g_checks;
    if (got != want) {
        ++g_failures;
        std::printf("  [FAIL] %s\n         got : %s\n         want: %s\n", what.c_str(), got.c_str(), want.c_str());
    } else {
        std::printf("  [ ok ] %s\n", what.c_str());
    }
}

using wowpatch::Bytes;
using Bits = std::vector<std::uint8_t>;

void appendTo(Bytes &dst, const Bytes &src) { dst.insert(dst.end(), src.begin(), src.end()); }

Bytes bytesOf(const std::string &s) { return Bytes(s.begin(), s.end()); }

bool sameBytes(const Bytes &a, const Bytes &b) { return a == b; }

// --- мини-DER сборщик (для проверки PEM-парсера на известных структурах) ----
void derLen(Bytes &out, std::size_t len) {
    if (len < 0x80) { out.push_back(static_cast<std::uint8_t>(len)); return; }
    Bytes tmp;
    while (len) { tmp.insert(tmp.begin(), static_cast<std::uint8_t>(len & 0xFF)); len >>= 8; }
    out.push_back(static_cast<std::uint8_t>(0x80 | tmp.size()));
    out.insert(out.end(), tmp.begin(), tmp.end());
}
Bytes derInteger(const Bytes &value) {
    Bytes v = value;
    if (!v.empty() && (v.front() & 0x80)) v.insert(v.begin(), 0x00);   // беззнаковое
    Bytes out;
    out.push_back(0x02);
    derLen(out, v.size());
    out.insert(out.end(), v.begin(), v.end());
    return out;
}
Bytes derSeq(const Bytes &content) {
    Bytes out;
    out.push_back(0x30);
    derLen(out, content.size());
    out.insert(out.end(), content.begin(), content.end());
    return out;
}
Bytes derOctet(const Bytes &content) {
    Bytes out;
    out.push_back(0x04);
    derLen(out, content.size());
    out.insert(out.end(), content.begin(), content.end());
    return out;
}
Bytes derBitString(const Bytes &content) {
    Bytes out;
    out.push_back(0x03);
    derLen(out, content.size() + 1);
    out.push_back(0x00);   // unused bits
    out.insert(out.end(), content.begin(), content.end());
    return out;
}
Bytes derOid(const Bytes &body) {
    Bytes out;
    out.push_back(0x06);
    derLen(out, body.size());
    out.insert(out.end(), body.begin(), body.end());
    return out;
}
Bytes derNull() { return Bytes{ 0x05, 0x00 }; }

std::string base64(const Bytes &in) {
    static const char *tbl =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    std::size_t i = 0;
    while (i + 2 < in.size()) {
        const unsigned v = (unsigned(in[i]) << 16) | (unsigned(in[i + 1]) << 8) | unsigned(in[i + 2]);
        out.push_back(tbl[(v >> 18) & 63]); out.push_back(tbl[(v >> 12) & 63]);
        out.push_back(tbl[(v >> 6) & 63]);  out.push_back(tbl[v & 63]);
        i += 3;
    }
    const std::size_t rest = in.size() - i;
    if (rest == 1) {
        const unsigned v = unsigned(in[i]) << 16;
        out.push_back(tbl[(v >> 18) & 63]); out.push_back(tbl[(v >> 12) & 63]);
        out += "==";
    } else if (rest == 2) {
        const unsigned v = (unsigned(in[i]) << 16) | (unsigned(in[i + 1]) << 8);
        out.push_back(tbl[(v >> 18) & 63]); out.push_back(tbl[(v >> 12) & 63]);
        out.push_back(tbl[(v >> 6) & 63]);  out.push_back('=');
    }
    return out;
}

std::string toPem(const std::string &label, const Bytes &der) {
    const std::string b64 = base64(der);
    std::string out = "-----BEGIN " + label + "-----\n";
    for (std::size_t i = 0; i < b64.size(); i += 64)
        out += b64.substr(i, 64) + "\n";
    out += "-----END " + label + "-----\n";
    return out;
}

// OID rsaEncryption = 1.2.840.113549.1.1.1
Bytes oidRsa() { return Bytes{ 0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x01, 0x01 }; }
// OID id-Ed25519 = 1.3.101.112
Bytes oidEd25519() { return Bytes{ 0x2B, 0x65, 0x70 }; }

// «Модуль» для теста: 256 байт, старший бит установлен (как у настоящего RSA-2048).
Bytes fakeModulusBe() {
    Bytes m(256, 0x00);
    m[0] = 0xC1;
    for (std::size_t i = 1; i < m.size(); ++i) m[i] = static_cast<std::uint8_t>((i * 37 + 11) & 0xFF);
    return m;
}

} // namespace

int main() {
    using namespace wowpatch;
    using namespace wowtest;

    std::printf("== Ключи TrinityCore ==\n");
    {
        std::vector<std::string> log;
        check(keysSelfTest(&log), "keysSelfTest() проходит");
        check(log.size() >= 8, "keysSelfTest() печатает подробный лог");
        check(trinityRsaModulusLe().size() == 256, "RSA-модуль LE: 256 байт");
        check(trinityEd25519PublicKey().size() == 32, "Ed25519 pubkey: 32 байта");
        check(trinityEd25519Seed().size() == 32, "Ed25519 seed: 32 байта");
        Bytes rev(trinityRsaModulusBe());
        std::reverse(rev.begin(), rev.end());
        check(rev == trinityRsaModulusLe(), "LE = разворот BE (константы согласованы)");
        check(trinityRsaModulusLe().front() == 0x5F && trinityRsaModulusLe().back() == 0xEE,
              "RSA-модуль LE начинается с 5F и кончается EE (как в AuthenticationPackets.cpp)");
        check(!sameBytes(Bytes(blizzardRsaSignature()), Bytes(trinityRsaModulusLe().begin(), trinityRsaModulusLe().begin() + 8)),
              "сигнатура Blizzard отличается от начала ключа TrinityCore");
    }

    std::printf("\n== Версия и профиль ==\n");
    {
        int a = 0, b = 0, c = 0, build = 0;
        check(parseVersion("12.1.0.69497", &a, &b, &c, &build), "parseVersion(\"12.1.0.69497\")");
        check(a == 12 && b == 1 && c == 0 && build == 69497, "12.1.0.69497 разобралось по частям");
        check(buildFromVersion("11.2.5.60818") == 60818, "buildFromVersion(11.2.5.60818) = 60818");
        check(buildFromVersion("мусор") == 0, "buildFromVersion(мусор) = 0");
        check(!parseVersion("12", &a, &b, &c, &build), "parseVersion требует минимум две части");

        const Fixture fx = makePe();
        const wowpe::Image img = wowpe::parse(fx.bytes);
        const Detection d = detectProfile(fx.bytes, img, "12.1.0.69497",
                                          "C:/Games/World of Warcraft/_retail_/Wow.exe");
        checkEqStr(d.profile, "modern12", "12.1.0.69497 + _retail_ -> профиль modern12");
        checkEqStr(d.branch, "Retail", "ветка Retail по пути _retail_");
        check(d.build == 69497, "build = 69497");
        check(d.usesEd25519, "12.x: Ed25519 обязателен");
        check(!d.legacyCert, "12.x: legacy-обход сертификата не нужен");
        check(d.standaloneDiskPatchOk, "12.x: статический патч на диске рабочий");

        const Detection d11 = detectProfile(fx.bytes, img, "11.2.5.60818", "x/_retail_/Wow.exe");
        checkEqStr(d11.profile, "modern", "11.2.5 -> профиль modern");
        check(d11.usesEd25519 && !d11.legacyCert, "11.x: Ed25519 да, legacy нет");

        const Detection dc = detectProfile(fx.bytes, img, "1.15.4.56789", "x/_classic_/Wow.exe");
        checkEqStr(dc.branch, "Classic", "путь _classic_ -> ветка Classic");
        checkEqStr(dc.profile, "classic_modern", "Classic Era 1.15 -> профиль classic_modern");
        check(dc.usesEd25519 && !dc.legacyCert, "Classic 1.15: современный движок — Ed25519 да, legacy нет");

        const Detection d34 = detectProfile(fx.bytes, img, "3.4.3.54474", "x/_classic_/Wow.exe");
        checkEqStr(d34.profile, "legacy", "Classic 3.4.3 -> legacy-профиль");
        check(d34.legacyCert && d34.usesEd25519, "3.4.x: Signature/Crypto RSA + Ed25519");

        const Detection d13 = detectProfile(fx.bytes, img, "1.13.7.12345", "x/_classic_era_/Wow.exe");
        checkEqStr(d13.profile, "classic13", "Classic Era 1.13 -> профиль classic13");
        check(!d13.usesEd25519 && !d13.legacyCert, "1.13: ни Ed25519, ни обхода сертификата");

        const Detection dl = detectProfile(fx.bytes, img, "7.3.5.26972", "x/_retail_/Wow.exe");
        checkEqStr(dl.profile, "legion", "7.3.5 -> профиль legion");
    }

    std::printf("\n== Портал ==\n");
    {
        checkEqStr(portalHostOnly("127.0.0.1:1119"), "127.0.0.1", "portalHostOnly(host:port)");
        checkEqStr(portalHostOnly("eu.actual.battle.net"), "eu.actual.battle.net", "portalHostOnly(без порта)");
        checkEqStr(normalizePortalHost("https://logon.wow.ru/portal"), "logon.wow.ru", "normalizePortalHost(URL)");
        checkEqStr(normalizePortalHost("  127.0.0.1:1119  "), "127.0.0.1:1119", "normalizePortalHost(пробелы)");

        std::string note;
        checkEqStr(derivePortalDomain("127.0.0.1:1119", &note), "", "IP-адрес не даёт домен для суффикса");
        check(note.find("IP") != std::string::npos, "для IP объясняем, что адрес возьмётся из SET portal");
        checkEqStr(derivePortalDomain("logon.wow.ru", &note), "wow.ru", "logon.wow.ru -> wow.ru");
        check(derivePortalDomain("logon.verylongdomainname.com", &note).empty(),
              "слишком длинный домен не лезет в окно суффикса");
        check(note.find("длиннее") != std::string::npos, "про длинный домен есть понятная строка");

        const Bytes suffix = portalSuffixValue("logon.wow.ru", 18);
        check(suffix.size() == 18, "значение суффикса = 18 байт (длина .actual.battle.net)");
        check(std::memcmp(suffix.data(), ".actual.wow.ru", 14) == 0, "суффикс = .actual.wow.ru");
        check(suffix[14] == 0 && suffix[17] == 0, "хвост добит NUL");
        check(portalSuffixValue("127.0.0.1:1119", 18).empty(), "для IP суффикс не формируется");

        const Bytes whole = portalWholeSlotValue("127.0.0.1", 1119, 19);
        check(whole.size() == 19, "значение «всё окно» = 19 байт");
        check(std::memcmp(whole.data(), "127.0.0.1:1119", 14) == 0, "в окне host:port");
        check(portalWholeSlotValue("verylonghostname.example.com", 1119, 19).empty(),
              "длинный портал не лезет в окно 19 байт");
    }

    std::printf("\n== Config.wtf ==\n");
    {
        std::string cfg = "SET gxMaximize \"1\"\nSET portal \"us.actual.battle.net\"\nSET textLocale \"ruRU\"\n";
        checkEqStr(configPortal(cfg), "us.actual.battle.net", "configPortal находит SET portal");
        check(writePortalIntoConfig(cfg, "127.0.0.1:1119"), "writePortalIntoConfig меняет текст");
        checkEqStr(configPortal(cfg), "127.0.0.1:1119", "после записи portal = 127.0.0.1:1119");
        check(!writePortalIntoConfig(cfg, "127.0.0.1:1119"), "повторная запись тем же значением -> без изменений");
        check(cfg.find("gxMaximize") != std::string::npos, "остальные строки Config.wtf не тронуты");
        check(std::count(cfg.begin(), cfg.end(), '\n') == 3, "число строк не выросло");

        std::string empty = "SET gxMaximize \"1\"\n";
        check(writePortalIntoConfig(empty, "127.0.0.1:1119"), "в пустой конфиг строка добавляется");
        checkEqStr(configPortal(empty), "127.0.0.1:1119", "добавленная строка читается обратно");
        check(empty.find("SET portal \"127.0.0.1:1119\"") != std::string::npos, "формат строки: SET portal \"...\"");
        checkEqStr(configPortal("SET   portal   =   \"logon.wow.ru\""), "logon.wow.ru", "вариант с '=' и пробелами");
        checkEqStr(configPortal("никакого портала тут нет"), "", "без portal -> пустая строка");
    }

    std::printf("\n== PEM -> ключи (PKCS#1 / PKCS#8 / SPKI / сертификат, Ed25519) ==\n");
    {
        const Bytes be = fakeModulusBe();
        Bytes wantLe(be);
        std::reverse(wantLe.begin(), wantLe.end());

        // PKCS#1 RSAPublicKey
        Bytes pkcs1Body = derInteger(be);
        appendTo(pkcs1Body, Bytes{ 0x02, 0x03, 0x01, 0x00, 0x01 }); // e = 65537
        const Bytes pkcs1 = derSeq(pkcs1Body);
        std::string err;
        Bytes got = rsaModulusLeFromPem(toPem("RSA PUBLIC KEY", pkcs1), &err);
        check(got == wantLe, std::string("PKCS#1 -> модуль LE (") + (err.empty() ? "" : err) + ")");

        // PKCS#1 RSAPrivateKey — ровно тот вид, в котором ключ лежит в TrinityCore
        // (-----BEGIN RSA PRIVATE KEY----- в AuthenticationPackets.cpp).
        Bytes privBody = derInteger(Bytes{ 0x00 });            // version
        appendTo(privBody, derInteger(be));                    // modulus
        appendTo(privBody, derInteger(Bytes{ 0x01, 0x00, 0x01 })); // publicExponent
        appendTo(privBody, derInteger(Bytes(256, 0x11)));      // privateExponent (фиктивный)
        appendTo(privBody, derInteger(Bytes(128, 0x22)));      // prime1
        appendTo(privBody, derInteger(Bytes(128, 0x33)));      // prime2
        err.clear();
        got = rsaModulusLeFromPem(toPem("RSA PRIVATE KEY", derSeq(privBody)), &err);
        check(got == wantLe, std::string("PKCS#1 RSA PRIVATE KEY -> модуль LE") + (err.empty() ? "" : ": " + err));

        // SPKI
        Bytes algBody = derOid(oidRsa());
        appendTo(algBody, derNull());
        Bytes spkiBody = derSeq(algBody);
        appendTo(spkiBody, derBitString(pkcs1));
        got = rsaModulusLeFromPem(toPem("PUBLIC KEY", derSeq(spkiBody)), &err);
        check(got == wantLe, "SPKI -> модуль LE");

        // PKCS#8
        Bytes p8Body = derInteger(Bytes{ 0x00 });
        appendTo(p8Body, derSeq(algBody));
        appendTo(p8Body, derOctet(pkcs1));
        got = rsaModulusLeFromPem(toPem("PRIVATE KEY", derSeq(p8Body)), &err);
        check(got == wantLe, "PKCS#8 -> модуль LE");

        // Сертификат X.509 (упрощённый: SEQUENCE { SEQUENCE{... SPKI ...}, ... })
        Bytes tbs;
        appendTo(tbs, derInteger(Bytes{ 0x02 }));          // version
        appendTo(tbs, derInteger(Bytes{ 0x01, 0x02 }));    // serial
        appendTo(tbs, derSeq(algBody));                    // sig alg
        appendTo(tbs, derSeq(spkiBody));                   // SPKI
        Bytes certBody = derSeq(tbs);
        appendTo(certBody, derSeq(algBody));
        appendTo(certBody, derBitString(Bytes(256, 0x33)));
        err.clear();
        got = rsaModulusLeFromPem(toPem("CERTIFICATE", derSeq(certBody)), &err);
        check(got == wantLe, "сертификат X.509 -> модуль LE");

        // Ошибки
        check(rsaModulusLeFromPem("это не PEM", &err).empty() && !err.empty(), "не-PEM -> ошибка с текстом");
        Bytes edPub(32, 0x7E);
        Bytes edAlg = derSeq(derOid(oidEd25519()));
        Bytes edSpkiBody = edAlg;
        const Bits bits = derBitString(edPub);
        edSpkiBody.insert(edSpkiBody.end(), bits.begin(), bits.end());
        const Bytes edSpki = derSeq(edSpkiBody);
        const Bytes gotEd = ed25519PublicFromPem(toPem("PUBLIC KEY", edSpki), &err);
        check(gotEd == edPub, "Ed25519 SPKI -> 32 байта публичного ключа");
        check(ed25519PublicFromPem(toPem("RSA PUBLIC KEY", pkcs1), &err).empty() && !err.empty(),
              "в RSA-PEM нет Ed25519 -> понятная ошибка");

        // Тот же парсер на НАСТОЯЩЕМ модуле TrinityCore: собираем PKCS#1
        // RSAPrivateKey с реальным BE-модулем ConnectToRSA и проверяем, что
        // на выходе получается встроенная LE-константа.
        {
            Bytes body = derInteger(Bytes{ 0x00 });
            appendTo(body, derInteger(trinityRsaModulusBe()));
            appendTo(body, derInteger(Bytes{ 0x01, 0x00, 0x01 }));
            appendTo(body, derInteger(Bytes(256, 0x77)));
            err.clear();
            const Bytes gotTc = rsaModulusLeFromPem(toPem("RSA PRIVATE KEY", derSeq(body)), &err);
            check(gotTc == trinityRsaModulusLe(),
                  std::string("PEM с реальным модулем TrinityCore -> встроенная LE-константа") +
                  (err.empty() ? "" : ": " + err));
        }

        // normalizeKeyOrder
        std::string note;
        check(normalizeKeyOrder(be, false, &note) == be, "normalizeKeyOrder(false) не меняет байты");
        check(normalizeKeyOrder(be, true, &note) == wantLe, "normalizeKeyOrder(true) разворачивает BE -> LE");
        check(note.find("big-endian") != std::string::npos, "про разворот есть пояснение");
    }

    std::printf("\n== Патч образа: 12.1.0.69497, Firestorm-стиль ==\n");
    Fixture fx = makePe();
    Bytes original = fx.bytes;
    Bytes patched = fx.bytes;
    Report rep;
    {
        const wowpe::Image img = wowpe::parse(patched);
        const Detection det = detectProfile(patched, img, "12.1.0.69497", "C:/Games/WoW/_retail_/Wow.exe");

        Options opts;
        opts.portal = "127.0.0.1:1119";
        opts.port = 1119;
        opts.patchPortalSuffix = true;      // для IP будет честно пропущен
        opts.autoDetect = true;
        opts.requireEd25519 = true;
        opts.verify = true;

        std::vector<std::string> log;
        const bool ok = patchImage(patched, opts, det, &rep, &log);
        check(ok, std::string("patchImage завершился успешно") + (ok ? "" : ": " + rep.error));
        check(rep.ok, "Report.ok");
        check(rep.applied >= 3, "применено минимум 3 патча (RSA + Ed25519 + запасной суффикс)");
        check(rep.missedMandatory == 0, "нет пропущенных обязательных патчей");
        check(rep.verifiedClean, "верификация чистая");
        check(patched.size() == original.size(), "размер файла не изменился (все правки in-place)");
        check(!rep.sha256.empty() && rep.sha256.size() == 64, "посчитан SHA-256 результата");

        // RSA заменён в .rdata, причём на ключ TrinityCore
        check(std::memcmp(patched.data() + fx.modulusOff, trinityRsaModulusLe().data(), 256) == 0,
              "в .rdata на месте родного ключа теперь модуль TrinityCore (256 байт)");
        // Ловушка: тот же якорь в .text НЕ тронут
        check(std::memcmp(patched.data() + fx.textPatOff, original.data() + fx.textPatOff, 256) == 0,
              "decoy-якорь в .text не тронут (код на диске не правим)");
        // Ed25519 заменён
        check(std::memcmp(patched.data() + fx.edOff, trinityEd25519PublicKey().data(), 32) == 0,
              "Ed25519 заменён на публичный ключ TrinityCore (32 байта)");
        // Родных ключей Blizzard не осталось
        const Bytes &sigRsa = blizzardRsaSignature();
        const Bytes &sigEd = blizzardEd25519Signature();
        const wowpe::Image pimg = wowpe::parse(patched);
        check(!wowpe::chooseSite(patched, pimg, wowpe::patternFromBytes(sigRsa.data(), sigRsa.size()), true).found,
              "родной ключ Blizzard ConnectTo больше не находится в секциях данных");
        check(!wowpe::chooseSite(patched, pimg, wowpe::patternFromBytes(sigEd.data(), sigEd.size()), true).found,
              "родной ключ Blizzard Ed25519 больше не находится в секциях данных");
        // Строка портала не испорчена (для IP суффикс честно не правится)
        check(std::memcmp(patched.data() + fx.portalOff, ".actual.localhost", 17) == 0 &&
              patched[fx.portalOff + 17] == 0,
              "при IP-портале суффикс -> .actual.localhost (клиент не уйдёт на Blizzard)");
        // Заголовок и таблица секций целы
        check(std::memcmp(patched.data(), original.data(), fx.rdataOff) == 0,
              "заголовок + таблица секций + .text побайтово те же");
        // Отчёт содержит записи
        bool sawRsa = false, sawEd = false;
        for (const Record &r : rep.records) {
            if (r.id == "rsa.connectto" && r.applied && r.offset == std::int64_t(fx.modulusOff)) sawRsa = true;
            if (r.id == "ed25519" && r.applied && r.offset == std::int64_t(fx.edOff)) sawEd = true;
        }
        check(sawRsa, "в отчёте есть rsa.connectto с верным смещением");
        check(sawEd, "в отчёте есть ed25519 с верным смещением");
        check(!rep.verification.empty(), "верификация напечатала строки");
    }

    std::printf("\n== Суффикс: запасной домен можно выключить ==\n");
    {
        Bytes noFallback = fx.bytes;
        const wowpe::Image img = wowpe::parse(noFallback);
        const Detection det = detectProfile(noFallback, img, "12.1.0.69497", "x/_retail_/Wow.exe");
        Options opts;
        opts.portal = "127.0.0.1:1119";
        opts.portalFallbackDomain.clear();      // не трогать суффикс
        Report r6;
        std::vector<std::string> log;
        check(patchImage(noFallback, opts, det, &r6, &log), "патч без запасного домена прошёл");
        check(std::memcmp(noFallback.data() + fx.portalOff, ".actual.battle.net", 18) == 0,
              "с выключенным запасным доменом суффикс не тронут");
    }

    std::printf("\n== Патч образа: доменный портал -> суффикс .actual.<домен> ==\n");
    {
        Bytes dom = fx.bytes;
        const wowpe::Image img = wowpe::parse(dom);
        const Detection det = detectProfile(dom, img, "12.1.0.69497", "x/_retail_/Wow.exe");
        Options opts;
        opts.portal = "logon.wow.ru";
        opts.patchPortalSuffix = true;
        opts.requireEd25519 = true;
        Report r2;
        std::vector<std::string> log;
        check(patchImage(dom, opts, det, &r2, &log), "патч с доменным порталом прошёл");
        check(std::memcmp(dom.data() + fx.portalOff, ".actual.wow.ru", 14) == 0,
              "суффикс заменён на .actual.wow.ru");
        check(dom[fx.portalOff + 14] == 0 && dom[fx.portalOff + 17] == 0, "хвост суффикса добит NUL");
        check(dom.size() == fx.bytes.size(), "размер не изменился");
        bool sawPortal = false;
        for (const Record &r : r2.records) if (r.id == "portal.suffix" && r.applied) sawPortal = true;
        check(sawPortal, "в отчёте есть portal.suffix");
        checkEqStr(r2.portalWritten, "logon.wow.ru", "в отчёте сохранён портал для Config.wtf");
    }

    std::printf("\n== Опции: CheckSum, снятие подписи, только рецепт ==\n");
    {
        Bytes cs = fx.bytes;
        const wowpe::Image img = wowpe::parse(cs);
        const Detection det = detectProfile(cs, img, "12.1.0.69497", "x/_retail_/Wow.exe");
        Options opts;
        opts.portal = "logon.wow.ru";
        opts.fixChecksum = true;
        opts.verify = true;
        Report r3;
        std::vector<std::string> log;
        check(patchImage(cs, opts, det, &r3, &log), "патч с fixChecksum прошёл");
        check(r3.checksumOld == 0xDEADBEEF, "старый CheckSum прочитан из заголовка");
        check(r3.checksumNew != 0 && r3.checksumNew != r3.checksumOld, "новый CheckSum другой");
        const wowpe::Image img2 = wowpe::parse(cs);
        check(img2.checksum == r3.checksumNew, "CheckSum записан в заголовок");
        check(r3.checksumNew == wowpe::computeChecksum(cs.data(), cs.size(), img2.checksumFileOffset),
              "записанный CheckSum совпадает с пересчитанным");
    }
    {
        Bytes sg = fx.bytes;
        const std::size_t sizeBefore = sg.size();
        const wowpe::Image img = wowpe::parse(sg);
        const Detection det = detectProfile(sg, img, "12.1.0.69497", "x/_retail_/Wow.exe");
        Options opts;
        opts.portal = "logon.wow.ru";
        opts.stripSignature = true;
        opts.verify = true;
        Report r4;
        std::vector<std::string> log;
        check(patchImage(sg, opts, det, &r4, &log), "патч со снятием подписи прошёл");
        check(r4.signatureStripped, "отчёт помечает снятую подпись");
        check(sg.size() < sizeBefore, "хвост-«таблица сертификатов» обрезан");
        const wowpe::Image img2 = wowpe::parse(sg);
        check(img2.valid && img2.certTablePointer == 0, "в data directory 4 больше нет каталога сертификатов");
        check(r4.verifiedClean, "верификация приняла уменьшившийся размер (подпись снята)");
    }
    {
        Bytes none = fx.bytes;
        const wowpe::Image img = wowpe::parse(none);
        const Detection det = detectProfile(none, img, "12.1.0.69497", "x/_retail_/Wow.exe");
        Options opts;
        opts.portal = "logon.wow.ru";
        opts.applySignaturePatches = false;   // режим «только рецепт»
        Report r5;
        std::vector<std::string> log;
        check(!patchImage(none, opts, det, &r5, &log), "без сигнатурных патчей и без рецепта — отказ");
        check(!r5.error.empty(), "отказ объяснён текстом");
        check(none == fx.bytes, "файл не изменён");
    }

    std::printf("\n== Повторный патч и комбинация «рецепт + сигнатуры» ==\n");
    {
        // Повторный запуск патча на уже пропатченном образе обязан проходить,
        // а не падать с «не применены обязательные патчи».
        Bytes again = patched;
        const wowpe::Image img = wowpe::parse(again);
        const Detection det = detectProfile(again, img, "12.1.0.69497", "x/_retail_/Wow.exe");
        Options opts;
        opts.portal = "127.0.0.1:1119";
        opts.requireEd25519 = true;
        Report rr;
        std::vector<std::string> log;
        check(patchImage(again, opts, det, &rr, &log), "повторный патч уже пропатченного образа проходит");
        check(rr.missedMandatory == 0, "повторный патч: нет пропущенных обязательных");
        check(again == patched, "повторный патч не изменил байты");
        bool sawAlready = false;
        for (const Record &r : rr.records)
            if (r.id == "rsa.connectto" && r.applied && r.note == "уже применён") sawAlready = true;
        check(sawAlready, "в отчёте помечено «уже применён»");
    }
    {
        // Комбинация: сначала hunks рецепта, потом сигнатурные патчи.
        Bytes combo = original;
        const wowpe::Image img = wowpe::parse(combo);
        const Detection det = detectProfile(combo, img, "12.1.0.69497", "x/_retail_/Wow.exe");

        RecipeMeta meta;
        meta.name = "combo";
        Recipe rec;
        std::vector<std::string> log;
        std::string err;
        check(makeRecipe(original, patched, meta, &rec, &log, &err), "рецепт для комбо снят");
        Report rr;
        applyRecipeToImage(combo, img, rec, &rr, &log);
        check(combo == patched, "рецепт перенёс байты до сигнатурных патчей");

        Options opts;
        opts.portal = "127.0.0.1:1119";
        opts.requireEd25519 = true;
        const bool ok = patchImage(combo, opts, det, &rr, &log);
        check(ok, std::string("патч после рецепта не падает") + (ok ? "" : ": " + rr.error));
        check(rr.missedMandatory == 0, "комбо: обязательные патчи закрыты рецептом");
        check(combo == patched, "комбо: байты не испорчены двойным применением");
        bool sawRecipe = false, sawKey = false;
        for (const Record &r : rr.records) {
            if (r.id == "recipe" && r.applied) sawRecipe = true;
            if (r.id == "rsa.connectto" && r.applied) sawKey = true;
        }
        check(sawRecipe, "в отчёте остались записи рецепта");
        check(sawKey, "в отчёте есть запись про ключ (применён или уже применён)");
    }

    std::printf("\n== Рецепт: снятие, JSON, перенос ==\n");
    {
        RecipeMeta meta;
        meta.name = "Firestorm 11.2.5 -> 12.1.0";
        meta.sourceOriginal = "Wow.exe";
        meta.sourcePatched = "WoW 11.2.5 - Firestorm.exe";
        meta.originalVersion = "12.1.0.69497";
        meta.patchedVersion = "12.1.0.69497";
        meta.originalSha256 = wowpe::sha256Hex(original);
        meta.patchedSha256 = wowpe::sha256Hex(patched);

        Recipe rec;
        std::vector<std::string> log;
        std::string err;
        check(makeRecipe(original, patched, meta, &rec, &log, &err),
              std::string("makeRecipe снял разницу") + (err.empty() ? "" : ": " + err));
        check(!rec.hunks.empty(), "hunks не пустые");
        checkEqStr(rec.meta.name, "Firestorm 11.2.5 -> 12.1.0", "метаданные рецепта сохранены");

        const wowpe::Image img = wowpe::parse(original);
        int inData = 0;
        for (const wowpe::Hunk &h : rec.hunks)
            if (img.isDiskPatchableOffset(h.offset)) ++inData;
        check(inData == int(rec.hunks.size()), "все hunks — в секциях данных");
        check(inData >= 2, "как минимум RSA и Ed25519 дали hunks");

        // JSON туда-обратно
        const std::string json = recipeToJson(rec);
        check(json.find("\"hunks\"") != std::string::npos, "в JSON есть массив hunks");
        check(json.find("Firestorm") != std::string::npos, "в JSON есть имя рецепта");
        Recipe back;
        check(recipeFromJson(json, &back, &err), std::string("JSON прочитан обратно") + (err.empty() ? "" : ": " + err));
        check(back.hunks.size() == rec.hunks.size(), "число hunks совпало");
        bool same = back.hunks.size() == rec.hunks.size();
        for (std::size_t i = 0; same && i < rec.hunks.size(); ++i) {
            same = rec.hunks[i].offset == back.hunks[i].offset &&
                   rec.hunks[i].before == back.hunks[i].before &&
                   rec.hunks[i].after == back.hunks[i].after &&
                   rec.hunks[i].ctxBefore == back.hunks[i].ctxBefore &&
                   rec.hunks[i].ctxAfter == back.hunks[i].ctxAfter &&
                   rec.hunks[i].section == back.hunks[i].section;
        }
        check(same, "все hunks пережили сериализацию байт-в-байт");
        checkEqStr(back.meta.name, rec.meta.name, "имя рецепта пережило сериализацию");
        checkEqStr(back.meta.originalSha256, rec.meta.originalSha256, "SHA-256 оригинала пережил сериализацию");

        // Перенос на «другой билд»: те же hunks, но применённые к свежему оригиналу
        Bytes target = original;
        const wowpe::Image timg = wowpe::parse(target);
        Report rr;
        std::vector<std::string> rlog;
        const int moved = applyRecipeToImage(target, timg, back, &rr, &rlog);
        check(moved == int(rec.hunks.size()), "рецепт перенёс все hunks");
        check(target == patched, "результат переноса побайтово равен пропатченному образу");

        // Повторное применение — идемпотентно
        Report rr2;
        std::vector<std::string> rlog2;
        const int moved2 = applyRecipeToImage(target, timg, back, &rr2, &rlog2);
        check(moved2 == int(rec.hunks.size()), "повторное применение тоже прошло (already applied)");
        check(target == patched, "повторное применение не испортило байты");

        // Ошибки JSON
        Recipe bad;
        check(!recipeFromJson("{ это не json", &bad, &err) && !err.empty(), "битый JSON -> ошибка");
        check(!recipeFromJson("{\"hunks\": []}", &bad, &err) && !err.empty(), "пустой рецепт -> ошибка");
        check(!recipeFromJson("{\"name\": \"x\"}", &bad, &err) && !err.empty(), "рецепт без hunks -> ошибка");

        // Идентичные файлы
        Recipe none;
        check(!makeRecipe(original, original, meta, &none, &log, &err) && !err.empty(),
              "идентичные файлы -> рецепт не снимается");
    }

    std::printf("\n== Диагностика (inspect) ==\n");
    {
        const Inspect r = inspect(original, "12.1.0.69497", "C:/Games/WoW/_retail_/Wow.exe");
        check(r.ok, "inspect отработал");
        check(r.pe64, "PE32+ распознан");
        check(r.sections.size() == 4, "4 секции в отчёте");
        check(r.imageBase == 0x140000000ull, "ImageBase прочитан");
        check(r.hasSignature && r.certSize == 32, "«таблица сертификатов» видна");
        check(r.blizzardRsaFound, "родной ключ Blizzard ConnectTo найден");
        check(r.blizzardEdFound, "родной ключ Blizzard Ed25519 найден");
        check(r.portalFound, "строка портала найдена");
        check(!r.trinityRsaFound, "ключа TrinityCore в оригинале нет");
        check(!r.lines.empty(), "отчёт содержит строки для вывода");
        bool sawText = false, sawRdata = false;
        for (const SectionInfo &s : r.sections) {
            if (s.name == ".text") sawText = !s.diskPatchable;
            if (s.name == ".rdata") sawRdata = s.diskPatchable;
        }
        check(sawText, ".text помечен как «на диске не правим»");
        check(sawRdata, ".rdata помечен как «можно править на диске»");
        check(r.sha256 == wowpe::sha256Hex(original), "SHA-256 в отчёте совпадает");

        const Inspect r2 = inspect(patched, "12.1.0.69497", "x/_retail_/Wow.exe");
        check(!r2.blizzardRsaFound, "у пропатченного образа родного ключа Blizzard нет");
        check(r2.trinityRsaFound, "у пропатченного образа ключ TrinityCore на месте");
        check(r2.trinityEdFound, "у пропатченного образа Ed25519 TrinityCore на месте");
        check(r2.detection.note.find("УЖЕ пропатчен") != std::string::npos,
              "диагностика говорит, что файл уже пропатчен");

        const Bytes garbage = bytesOf("это точно не PE-файл");
        const Inspect r3 = inspect(garbage, "", "");
        check(!r3.ok && !r3.error.empty(), "не-PE диагностируется с ошибкой");
    }

    std::printf("\n== verifyImage отдельно ==\n");
    {
        const wowpe::Image img = wowpe::parse(patched);
        std::vector<std::string> out;
        check(verifyImage(patched, img, rep, &out), "верификация пропатченного образа проходит");
        check(!out.empty(), "верификация печатает строки");

        Bytes broken = patched;
        broken[fx.modulusOff] ^= 0xFF;   // портим один байт ключа
        out.clear();
        check(!verifyImage(broken, img, rep, &out), "испорченный байт ключа ловится верификацией");

        Bytes truncated(patched.begin(), patched.begin() + std::ptrdiff_t(patched.size() - 64));
        out.clear();
        check(!verifyImage(truncated, img, rep, &out), "обрезанный файл ловится верификацией");
    }

    std::printf("\n-----------------------------------------\n");
    std::printf("Проверок: %d, провалов: %d\n", g_checks, g_failures);
    if (g_failures == 0) { std::printf("SELFTEST OK\n"); return 0; }
    std::printf("SELFTEST FAILED\n");
    return 1;
}
