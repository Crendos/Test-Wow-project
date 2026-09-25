// ---------------------------------------------------------------------------
// wow_patch_cli.cpp — консольный патчер Wow.exe. БЕЗ Qt: только C++20.
//
// Сборка (Linux/macOS):
//   g++ -std=c++20 -O2 -I../src wow_patch_cli.cpp ../src/wow_patch_core.cpp ../src/wow_pe.cpp -o wow_patch_cli
// Сборка (Windows, MSVC):
//   cl /std:c++20 /O2 /EHsc /I..\src wow_patch_cli.cpp ..\src\wow_patch_core.cpp ..\src\wow_pe.cpp /Fe:wow_patch_cli.exe
// В CMake есть цель wow_patch_cli (см. CMakeLists.txt).
//
// Команды:
//   keys                                 самопроверка ключей TrinityCore
//   diagnose <Wow.exe> [--version V]     полный отчёт: PE, секции, все сайты
//   findslot <patched.exe> <target.exe>  слот RSA-модуля в другом билде по контексту
//   patch <src> [dst] --portal H[:P] …   патч «как Firestorm»: самостоятельный exe
//   recipe <orig> <patched> <out.json>   снять рецепт с чужого пропатченного клиента
//   apply-recipe <src> <dst> <r.json>…   перенести рецепт на свой билд
//
// Пример (клиент 12.1.0.69497, сервер на этом же ПК):
//   wow_patch_cli patch "C:\Games\WoW\_retail_\Wow.exe" --portal 127.0.0.1:1119
//   (dst не указан — пишем поверх, оригинал сохраняется в Wow.exe.orig)
// ---------------------------------------------------------------------------

#include "wow_patch_core.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#ifdef _WIN32
#  include <windows.h>
#endif

namespace fs = std::filesystem;
using wowpatch::Bytes;

namespace {

void printAll(const std::vector<std::string> &lines) {
    for (const std::string &s : lines) std::printf("%s\n", s.c_str());
}

bool readFile(const fs::path &p, Bytes *out, std::string *err) {
    std::ifstream f(p, std::ios::binary);
    if (!f) { if (err) *err = "не удалось открыть " + p.string(); return false; }
    f.seekg(0, std::ios::end);
    const std::streamoff n = f.tellg();
    f.seekg(0, std::ios::beg);
    if (n < 0) { if (err) *err = "не удалось определить размер " + p.string(); return false; }
    out->resize(static_cast<std::size_t>(n));
    if (n > 0 && !f.read(reinterpret_cast<char *>(out->data()), n)) {
        if (err) *err = "не удалось прочитать " + p.string();
        return false;
    }
    return true;
}

bool writeFile(const fs::path &p, const Bytes &data, std::string *err) {
    std::error_code ec;
    if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) { if (err) *err = "не удалось открыть для записи " + p.string(); return false; }
    if (!data.empty()) f.write(reinterpret_cast<const char *>(data.data()), std::streamsize(data.size()));
    f.flush();
    if (!f) { if (err) *err = "не удалось записать " + p.string(); return false; }
    return true;
}

bool readText(const fs::path &p, std::string *out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    out->assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

bool writeText(const fs::path &p, const std::string &text, std::string *err) {
    std::error_code ec;
    if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) { if (err) *err = "не удалось записать " + p.string(); return false; }
    f.write(text.data(), std::streamsize(text.size()));
    return bool(f);
}

std::string upper(std::string s) {
    for (char &c : s) c = char(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

Bytes hexBytes(const std::string &hex, std::string *err) {
    Bytes out;
    const std::string e = wowpe::hexToBytes(hex, &out);
    if (!e.empty()) { if (err) *err = e; return Bytes(); }
    return out;
}

// --- Config.wtf ------------------------------------------------------------
fs::path findConfigWtf(const fs::path &exe) {
    const fs::path dir = exe.parent_path();
    for (const fs::path &c : { dir / "WTF" / "Config.wtf", dir / "Config.wtf",
                               dir.parent_path() / "WTF" / "Config.wtf" }) {
        std::error_code ec;
        if (fs::exists(c, ec)) return c;
    }
    return fs::path();
}

// wow.exe -> wow.patched.exe (та же папка: копия должна найти свои Data/WTF).
fs::path copyOutputPath(const fs::path &exe) {
    const std::string ext = exe.extension().empty() ? ".exe" : exe.extension().string();
    return exe.parent_path() / (exe.stem().string() + ".patched" + ext);
}

// .build.info лежит в корне установки (на два уровня выше _retail_).
std::string buildInfoVersion(const fs::path &exe) {
    const fs::path dir = exe.parent_path();
    for (const fs::path &c : { dir.parent_path() / ".build.info", dir / ".build.info" }) {
        std::string text;
        if (!readText(c, &text)) continue;
        // Формат: TSV, первая строка — заголовки, ищем колонку Version.
        const std::size_t nl = text.find('\n');
        if (nl == std::string::npos) continue;
        const std::string head = text.substr(0, nl);
        std::vector<std::string> cols;
        std::string cur;
        for (char ch : head) {
            if (ch == '\t' || ch == '\r' || ch == '\n') { cols.push_back(cur); cur.clear(); }
            else cur.push_back(ch);
        }
        cols.push_back(cur);
        int idx = -1;
        for (std::size_t i = 0; i < cols.size(); ++i)
            if (upper(cols[i]) == "VERSION") idx = int(i);
        if (idx < 0) continue;
        std::size_t pos = nl + 1;
        while (pos < text.size()) {
            const std::size_t eol = text.find('\n', pos);
            const std::string line = text.substr(pos, (eol == std::string::npos ? text.size() : eol) - pos);
            pos = (eol == std::string::npos) ? text.size() : eol + 1;
            if (line.empty() || line[0] == '#') continue;
            std::vector<std::string> f;
            std::string c2;
            for (char ch : line) {
                if (ch == '\t' || ch == '\r') { f.push_back(c2); c2.clear(); }
                else c2.push_back(ch);
            }
            f.push_back(c2);
            if (std::size_t(idx) < f.size() && !f[std::size_t(idx)].empty()) return f[std::size_t(idx)];
            break;
        }
    }
    return std::string();
}

std::string detectVersion(const fs::path &exe, const std::string &forced) {
    if (!forced.empty()) return forced;
#ifdef _WIN32
    DWORD handle = 0;
    const std::string path = exe.string();
    const DWORD size = GetFileVersionInfoSizeA(path.c_str(), &handle);
    if (size > 0) {
        std::vector<char> buf(size);
        if (GetFileVersionInfoA(path.c_str(), 0, size, buf.data())) {
            VS_FIXEDFILEINFO *info = nullptr;
            UINT infoLen = 0;
            if (VerQueryValueA(buf.data(), "\\", reinterpret_cast<void **>(&info), &infoLen) && info) {
                const int hi = int((info->dwFileVersionMS >> 16) & 0xFFFF);
                const int lo = int(info->dwFileVersionMS & 0xFFFF);
                const int b1 = int((info->dwFileVersionLS >> 16) & 0xFFFF);
                const int b2 = int(info->dwFileVersionLS & 0xFFFF);
                if (hi || lo) return std::to_string(hi) + "." + std::to_string(lo) + "." +
                                 std::to_string(b1) + "." + std::to_string(b2);
            }
        }
    }
#endif
    return buildInfoVersion(exe);
}

// --- разбор аргументов -----------------------------------------------------
struct Args {
    std::vector<std::string> positional;
    std::vector<std::string> recipes;
    std::string portal, domain, version, rsaPem, edPem, rsaHex, edHex, certBundleUrl, certBundleFile;
    std::string versionUrl, cdnsUrl, configPath, name, rsaSlot;
    int port = 0;
    int window = 0;  // --window для findslot (0 = дефолт ядра)
    bool noSuffix = false, wholeSlot = false, be = false, checksum = false, stripSig = false;
    bool noVerify = false, noConfig = false, legacyRsa = true, noEd = false, launcherReg = false;
    bool urls = false, help = false, copy = false;
};

bool parseArgs(int argc, char **argv, Args *a, std::string *err) {
    auto need = [&](int &i, const char *name) -> const char * {
        if (i + 1 >= argc) { *err = std::string("после ") + name + " нужно значение"; return nullptr; }
        return argv[++i];
    };
    for (int i = 2; i < argc; ++i) {
        const std::string s = argv[i];
        const char *v = nullptr;
        if (s == "--portal")            { if (!(v = need(i, "--portal"))) return false; a->portal = v; }
        else if (s == "--port")         { if (!(v = need(i, "--port"))) return false; a->port = std::atoi(v); }
        else if (s == "--domain")       { if (!(v = need(i, "--domain"))) return false; a->domain = v; }
        else if (s == "--version")      { if (!(v = need(i, "--version"))) return false; a->version = v; }
        else if (s == "--rsa-pem")      { if (!(v = need(i, "--rsa-pem"))) return false; a->rsaPem = v; }
        else if (s == "--ed-pem")       { if (!(v = need(i, "--ed-pem"))) return false; a->edPem = v; }
        else if (s == "--rsa-hex")      { if (!(v = need(i, "--rsa-hex"))) return false; a->rsaHex = v; }
        else if (s == "--ed-hex")       { if (!(v = need(i, "--ed-hex"))) return false; a->edHex = v; }
        else if (s == "--cert-url")     { if (!(v = need(i, "--cert-url"))) return false; a->certBundleUrl = v; }
        else if (s == "--cert-file")    { if (!(v = need(i, "--cert-file"))) return false; a->certBundleFile = v; }
        else if (s == "--version-url")  { if (!(v = need(i, "--version-url"))) return false; a->versionUrl = v; a->urls = true; }
        else if (s == "--cdns-url")     { if (!(v = need(i, "--cdns-url"))) return false; a->cdnsUrl = v; a->urls = true; }
        else if (s == "--config")       { if (!(v = need(i, "--config"))) return false; a->configPath = v; }
        else if (s == "--name")         { if (!(v = need(i, "--name"))) return false; a->name = v; }
        else if (s == "--recipe")       { if (!(v = need(i, "--recipe"))) return false; a->recipes.push_back(v); }
        else if (s == "--rsa-slot")     { if (!(v = need(i, "--rsa-slot"))) return false; a->rsaSlot = v; }
        else if (s == "--window")       { if (!(v = need(i, "--window"))) return false; a->window = std::atoi(v); }
        else if (s == "--no-suffix")    a->noSuffix = true;
        else if (s == "--copy")         a->copy = true;
        else if (s == "--whole-slot")   a->wholeSlot = true;
        else if (s == "--be")           a->be = true;
        else if (s == "--checksum")     a->checksum = true;
        else if (s == "--strip-sig")    a->stripSig = true;
        else if (s == "--no-verify")    a->noVerify = true;
        else if (s == "--no-config")    a->noConfig = true;
        else if (s == "--no-legacy-rsa")a->legacyRsa = false;
        else if (s == "--no-ed25519")   a->noEd = true;
        else if (s == "--launcher-reg") a->launcherReg = true;
        else if (s == "--help" || s == "-h") a->help = true;
        else if (!s.empty() && s[0] == '-') { *err = "неизвестный параметр: " + s; return false; }
        else a->positional.push_back(s);
    }
    return true;
}

void usage() {
    std::printf(
        "Патчер Wow.exe (Firestorm-стиль) — без Qt, чистый C++.\n"
        "\n"
        "  wow_patch_cli keys\n"
        "      Самопроверка встроенных ключей TrinityCore.\n"
        "\n"
        "  wow_patch_cli diagnose <Wow.exe> [--version 12.1.0.69497]\n"
        "      Отчёт: PE, секции, все известные сайты, Config.wtf, версия.\n"
        "\n"
        "  wow_patch_cli findslot <patched-reference.exe> <target.exe> [--window N]\n"
        "      Найти слот ConnectTo-модуля в target по контексту соседних байтов\n"
        "      из уже пропатченного эталона (работает между билдами, где сигнатуры\n"
        "      Blizzard сменились). Печатает текущее содержимое слота (новый\n"
        "      модуль) и готовую команду patch с --rsa-slot. --window: окно\n"
        "      контекста с каждой стороны (по умолчанию 64, минимум 16).\n"
        "\n"
        "  wow_patch_cli patch <src.exe> [dst.exe] --portal <host[:port]> [параметры]\n"
        "      Основной режим: самостоятельный пропатченный exe на диске.\n"
        "      dst не указан -> пишем поверх src, оригинал сохраняется в <src>.orig.\n"
        "      Параметры:\n"
        "        --port N           порт, если не указан в --portal (по умолчанию 1119)\n"
        "        --domain D         домен для суффикса .actual.<D> (иначе выводится из портала)\n"
        "        --no-suffix        не править суффикс .actual.battle.net\n"
        "        --whole-slot       legacy: писать host:port во всё окно суффикса (не рекомендуется)\n"
        "        --rsa-pem F        RSA-ключ/сертификат в PEM -> модуль (LE)\n"
        "        --ed-pem F         публичный ключ Ed25519 в PEM\n"
        "        --rsa-hex H        модуль RSA в hex (256 байт)\n"
        "        --ed-hex H         ключ Ed25519 в hex (32 байта)\n"
        "        --be               hex-ключи заданы в big-endian (формат openssl) -> развернуть\n"
        "        --recipe F.json    применить рецепт (можно несколько раз)\n"
        "        --copy             писать в КОПИЮ <имя>.patched.exe рядом с исходником\n"
        "                         (например Wow.patched.exe) — оригинал не трогается\n"
        "        --rsa-slot OFF    явный файловый слот RSA (hex 0x… или десятичный\n"
        "                         из findslot) — пишем модуль точно по адресу\n"
        "        --checksum         пересчитать PE CheckSum\n"
        "        --strip-sig        снять Authenticode-подпись (станет невалидной в любом случае)\n"
        "        --no-verify        не перепроверять результат\n"
        "        --no-config        не трогать WTF/Config.wtf\n"
        "        --config F         путь к Config.wtf (иначе ищем рядом с exe)\n"
        "        --version V        версия клиента (иначе берём из ресурса/.build.info)\n"
        "        --no-legacy-rsa    не править Signature/GameCrypto RSA (legacy-клиенты)\n"
        "        --no-ed25519       не требовать Ed25519 (для 1.13.x)\n"
        "        --launcher-reg     подменить ключ реестра лаунчера\n"
        "        --version-url U    свой URL versions (включает патч URL)\n"
        "        --cdns-url U       свой URL cdns\n"
        "        --cert-url U       свой URL cert-bundle\n"
        "        --cert-file F      подписанный cert-bundle ({\"Created\":...) в слот 32761 байт\n"
        "                         (готовый от Arctium: data/arctium_cert_bundle.bin)\n"
        "\n"
        "  wow_patch_cli recipe <original.exe> <patched.exe> <out.json> [--name N] [--version V]\n"
        "      Снять точечную разницу с чужого пропатченного клиента (например\n"
        "      «WoW 11.2.5 - Firestorm.exe») и сохранить как рецепт.\n"
        "\n"
        "  wow_patch_cli apply-recipe <src.exe> <dst.exe> <recipe.json> [recipe2.json …]\n"
        "      Перенести рецепт(ы) на свой билд: hunks ищутся по контексту.\n"
        "\n"
        "Что делает патч (11.x/12.x, в т.ч. 12.1.0.69497):\n"
        "  1) ConnectTo RSA-модуль (256 байт, .rdata, LE) -> ключ TrinityCore;\n"
        "  2) Ed25519 (32 байта) -> публичная половина EnterEncryptedModePrivateKey;\n"
        "  3) SET portal \"host:port\" в WTF/Config.wtf;\n"
        "  4) (опция) суффикс .actual.battle.net -> .actual.<домен <=10 байт>.\n"
        "  .text не правим: у ретейла код упакован и восстанавливается при запуске.\n");
}

// --- команды ---------------------------------------------------------------
int cmdKeys() {
    std::vector<std::string> log;
    const bool ok = wowpatch::keysSelfTest(&log);
    printAll(log);
    return ok ? 0 : 1;
}

int cmdDiagnose(const fs::path &exe, const Args &a) {
    Bytes data;
    std::string err;
    if (!readFile(exe, &data, &err)) { std::printf("[!!] %s\n", err.c_str()); return 1; }
    const std::string version = detectVersion(exe, a.version);
    const wowpatch::Inspect r = wowpatch::inspect(data, version, exe.string());
    printAll(r.lines);
    if (!r.ok) { std::printf("[!!] %s\n", r.error.c_str()); return 1; }

    std::error_code ec;
    const fs::path dataDir1 = exe.parent_path() / "Data";
    const fs::path dataDir2 = exe.parent_path().parent_path() / "Data";
    const bool hasData = fs::is_directory(dataDir1, ec) || fs::is_directory(dataDir2, ec);
    std::printf("Папка Data: %s — %s\n",
                (fs::is_directory(dataDir1, ec) ? dataDir1 : dataDir2).string().c_str(),
                hasData ? "найдена" : "НЕ НАЙДЕНА (клиент не запустится)");
    const fs::path cfg = findConfigWtf(exe);
    if (cfg.empty()) std::printf("Config.wtf: не найден (будет создан при патче)\n");
    else {
        std::string text;
        readText(cfg, &text);
        const std::string portal = wowpatch::configPortal(text);
        std::printf("Config.wtf: %s   SET portal: %s\n", cfg.string().c_str(),
                    portal.empty() ? "нет" : portal.c_str());
    }
    return 0;
}

// --rsa-slot: hex (0x…) либо десятичный файловый offset.
std::int64_t parseOffsetArg(const std::string &s, std::string *err) {
    if (s.empty()) { if (err) *err = "пустое значение"; return -1; }
    char *end = nullptr;
    const long long v = std::strtoll(s.c_str(), &end, 0);
    if (end == s.c_str() || (end && *end != '\0') || v < 0) {
        if (err) *err = "не число: " + s + " (нужен 0x… или десятичный offset)";
        return -1;
    }
    return static_cast<std::int64_t>(v);
}

int cmdFindSlot(const fs::path &refPath, const fs::path &tgtPath, const Args &a) {
    Bytes ref, tgt;
    std::string err;
    if (!readFile(refPath, &ref, &err)) { std::printf("[!!] %s\n", err.c_str()); return 1; }
    if (!readFile(tgtPath, &tgt, &err)) { std::printf("[!!] %s\n", err.c_str()); return 1; }
    std::printf("Эталон (патченый): %s — %zu байт\n", refPath.string().c_str(), ref.size());
    std::printf("Цель:               %s — %zu байт\n", tgtPath.string().c_str(), tgt.size());
    const int w = a.window > 0 ? a.window : 64;
    const wowpatch::SlotHunt h = wowpatch::huntRsaSlot(ref, tgt, w);
    printAll(h.log);
    if (!h.found) { std::printf("[!!] %s\n", h.note.c_str()); return 1; }

    std::printf("\n[+] СЛОТ НАЙДЕН\n");
    std::printf("    файловый offset: 0x%llX (%lld)\n",
                static_cast<long long>(h.offset), static_cast<long long>(h.offset));
    const wowpe::Image img = wowpe::parse(tgt);
    if (img.valid) {
        std::printf("    секция: %s, RVA: 0x%X, на диске правится: %s\n",
                    img.locationName(static_cast<std::size_t>(h.offset)).c_str(),
                    img.fileOffsetToRva(static_cast<std::size_t>(h.offset)),
                    img.isDiskPatchableOffset(static_cast<std::size_t>(h.offset)) ? "ДА" : "НЕТ");
    }
    std::printf("    эталон: %s\n", h.refNote.c_str());
    std::printf("    содержимое: %s\n", h.note.c_str());
    std::printf("    модуль сейчас (256 байт, hex):\n");
    for (std::size_t i = 0; i + 32 <= h.content.size(); i += 32)
        std::printf("      %s\n", wowpe::toHex(h.content.data() + i, 32).c_str());
    std::printf("\nСледующий шаг (печатаем как одну строку):\n");
    std::printf("  wow_patch_cli patch \"%s\" --portal 127.0.0.1:1119 "
                "--rsa-slot 0x%llX --version <версия>\n",
                tgtPath.string().c_str(), static_cast<long long>(h.offset));
    return 0;
}

int cmdPatch(const fs::path &src, fs::path dstIn, const Args &a) {
    if (a.copy) {
        if (!dstIn.empty()) {
            std::printf("[i] dst указан явно — --copy игнорируется\n");
        } else {
            dstIn = copyOutputPath(src);
            std::printf("--copy: исходный файл не трогаем, результат -> %s\n",
                        dstIn.string().c_str());
        }
    }
    std::string err;
    Bytes data;
    if (!readFile(src, &data, &err)) { std::printf("[!!] %s\n", err.c_str()); return 1; }
    const std::size_t originalSize = data.size();
    const wowpe::Image img = wowpe::parse(data);
    if (!img.valid) { std::printf("[!!] PE не разобран: %s\n", img.error.c_str()); return 1; }

    const std::string version = detectVersion(src, a.version);
    const wowpatch::Detection det =
        wowpatch::detectProfile(data, img, version, src.string());
    std::printf("Профиль: [%s] версия %s, ветка %s\n", det.profile.c_str(),
                det.version.empty() ? "?" : det.version.c_str(), det.branch.c_str());
    if (!det.note.empty()) std::printf("         %s\n", det.note.c_str());

    wowpatch::Options opts;
    opts.portal = wowpatch::normalizePortalHost(a.portal);
    opts.port = a.port > 0 ? a.port : 1119;
    opts.patchPortalSuffix = !a.noSuffix;
    opts.patchPortalWholeSlot = a.wholeSlot;
    opts.portalDomain = a.domain;
    opts.autoDetect = true;
    opts.patchLegacyGameCryptoRsa = a.legacyRsa;
    opts.requireEd25519 = !a.noEd;
    opts.keysAssumeBigEndian = a.be;
    opts.fixChecksum = a.checksum;
    opts.stripSignature = a.stripSig;
    opts.verify = !a.noVerify;
    opts.patchVersionUrls = a.urls;
    opts.versionUrl = a.versionUrl;
    opts.cdnsUrl = a.cdnsUrl;
    opts.certBundleUrl = a.certBundleUrl;
    opts.patchLauncherRegistry = a.launcherReg;
    if (!a.rsaSlot.empty()) {
        std::string perr;
        const std::int64_t off = parseOffsetArg(a.rsaSlot, &perr);
        if (off < 0) { std::printf("[!!] --rsa-slot: %s\n", perr.c_str()); return 1; }
        opts.rsaSlotOffset = off;
        std::printf("RSA-слот задан явно: файловый offset 0x%llX (сигнатурный поиск отключён)\n",
                    static_cast<long long>(off));
    }

    if (!a.rsaPem.empty()) {
        std::string pem;
        if (!readText(a.rsaPem, &pem)) { std::printf("[!!] не прочитался %s\n", a.rsaPem.c_str()); return 1; }
        opts.rsaModulus = wowpatch::rsaModulusLeFromPem(pem, &err);
        if (opts.rsaModulus.empty()) { std::printf("[!!] RSA из PEM: %s\n", err.c_str()); return 1; }
        std::printf("RSA-модуль из PEM %s (переведён в little-endian): %s…\n", a.rsaPem.c_str(),
                    wowpe::toHex(opts.rsaModulus, 8).c_str());
    } else if (!a.rsaHex.empty()) {
        opts.rsaModulus = hexBytes(a.rsaHex, &err);
        if (opts.rsaModulus.empty()) { std::printf("[!!] RSA-ключ: %s\n", err.c_str()); return 1; }
        if (opts.rsaModulus.size() != 256) {
            std::printf("[!!] RSA-модуль %zu байт, нужно 256\n", opts.rsaModulus.size());
            return 1;
        }
    }
    if (!a.edPem.empty()) {
        std::string pem;
        if (!readText(a.edPem, &pem)) { std::printf("[!!] не прочитался %s\n", a.edPem.c_str()); return 1; }
        opts.ed25519Key = wowpatch::ed25519PublicFromPem(pem, &err);
        if (opts.ed25519Key.empty()) { std::printf("[!!] Ed25519 из PEM: %s\n", err.c_str()); return 1; }
        std::printf("Ed25519 из PEM %s: %s\n", a.edPem.c_str(), wowpe::toHex(opts.ed25519Key).c_str());
    } else if (!a.edHex.empty()) {
        opts.ed25519Key = hexBytes(a.edHex, &err);
        if (opts.ed25519Key.size() != 32) {
            std::printf("[!!] Ed25519-ключ: %s (нужно 32 байта)\n", err.empty() ? "неверная длина" : err.c_str());
            return 1;
        }
    }
    if (!a.certBundleFile.empty()) {
        if (!readFile(a.certBundleFile, &opts.certBundle, &err)) {
            std::printf("[!!] cert bundle: %s\n", err.c_str());
            return 1;
        }
    }

    std::vector<std::string> log;

    // --- рецепты: применяются ДО сигнатурных патчей ---
    wowpatch::Report rep;
    for (const std::string &rp : a.recipes) {
        std::string text;
        if (!readText(rp, &text)) { std::printf("  ! рецепт %s не прочитан\n", rp.c_str()); continue; }
        wowpatch::Recipe recipe;
        if (!wowpatch::recipeFromJson(text, &recipe, &err)) {
            std::printf("  ! рецепт %s: %s\n", rp.c_str(), err.c_str());
            continue;
        }
        std::printf("Применяем рецепт %s (%s)\n", rp.c_str(), recipe.meta.name.c_str());
        wowpatch::applyRecipeToImage(data, img, recipe, &rep, &log);
    }

    if (!wowpatch::patchImage(data, opts, det, &rep, &log)) {
        printAll(log);
        for (const std::string &s : rep.verification) std::printf("  %s\n", s.c_str());
        std::printf("[!!] %s\n", rep.error.c_str());
        return 1;
    }
    printAll(log);

    // --- куда писать ---
    std::error_code eqEc;
    const bool sameFile = !dstIn.empty() && fs::exists(dstIn, eqEc) && fs::equivalent(src, dstIn, eqEc);
    const bool inPlace = dstIn.empty() || sameFile || fs::absolute(src) == fs::absolute(dstIn);
    const fs::path dst = inPlace ? src : dstIn;
    if (inPlace) {
        const fs::path bak = src.string() + ".orig";
        std::error_code ec;
        if (!fs::exists(bak, ec)) {
            if (fs::copy_file(src, bak, ec))
                std::printf("Резервная копия оригинала: %s\n", bak.string().c_str());
            else
                std::printf("  ! не удалось создать %s — продолжаю без бэкапа\n", bak.string().c_str());
        } else {
            std::printf("Резервная копия уже есть: %s (не перезаписываю)\n", bak.string().c_str());
        }
    } else {
        std::error_code ec;
        if (fs::exists(dst, ec)) {
            const fs::path bak = dst.string() + ".bak";
            fs::remove(bak, ec);
            if (fs::copy_file(dst, bak, ec))
                std::printf("Резервная копия: %s\n", bak.string().c_str());
        }
    }
    if (!writeFile(dst, data, &err)) { std::printf("[!!] %s\n", err.c_str()); return 1; }
    std::printf("Записано: %s (%zu байт, было %zu)\n", dst.string().c_str(), data.size(), originalSize);

    // --- перепроверка того, что реально легло на диск ---
    if (opts.verify) {
        Bytes written;
        if (!readFile(dst, &written, &err)) {
            std::printf("[!!] не удалось перечитать результат: %s\n", err.c_str());
            return 1;
        }
        const wowpe::Image wimg = wowpe::parse(written);
        std::vector<std::string> vout;
        std::printf("Проверка файла на диске:\n");
        const bool clean = wowpatch::verifyImage(written, wimg.valid ? wimg : img, rep, &vout);
        for (const std::string &s : vout) std::printf("  %s\n", s.c_str());
        if (!clean) { std::printf("[!!] файл на диске не прошёл проверку\n"); return 1; }
    }

    // --- Config.wtf ---
    if (!a.noConfig && !opts.portal.empty()) {
        fs::path cfg = a.configPath.empty() ? findConfigWtf(dst) : fs::path(a.configPath);
        std::string text;
        if (cfg.empty() || !readText(cfg, &text)) {
            cfg = dst.parent_path() / "WTF" / "Config.wtf";
            text.clear();
            std::printf("Config.wtf не найден — создаю %s\n", cfg.string().c_str());
        }
        if (wowpatch::writePortalIntoConfig(text, opts.portal)) {
            std::string werr;
            if (writeText(cfg, text, &werr))
                std::printf("Config.wtf: SET portal \"%s\" -> %s\n", opts.portal.c_str(), cfg.string().c_str());
            else
                std::printf("[!!] Config.wtf: %s\n", werr.c_str());
        } else {
            std::printf("Config.wtf: SET portal \"%s\" уже стоит, ничего не меняю\n", opts.portal.c_str());
        }
    }

    std::printf("\nГотово. %s — самостоятельный exe: запускается двойным щелчком, лаунчер не нужен.\n",
                dst.filename().string().c_str());
    std::printf("Портал берётся из SET portal в Config.wtf рядом с этим exe.\n");
    return 0;
}

int cmdRecipe(const fs::path &orig, const fs::path &patched, const fs::path &outJson, const Args &a) {
    Bytes A, B;
    std::string err;
    if (!readFile(orig, &A, &err)) { std::printf("[!!] %s\n", err.c_str()); return 1; }
    if (!readFile(patched, &B, &err)) { std::printf("[!!] %s\n", err.c_str()); return 1; }

    wowpatch::RecipeMeta meta;
    meta.name = a.name.empty() ? patched.filename().string() : a.name;
    meta.sourceOriginal = orig.string();
    meta.sourcePatched = patched.string();
    meta.originalSha256 = wowpe::sha256Hex(A);
    meta.patchedSha256 = wowpe::sha256Hex(B);
    meta.originalVersion = detectVersion(orig, a.version);
    meta.patchedVersion = detectVersion(patched, a.version);
    meta.createdUtc = "cli";

    wowpatch::Recipe rec;
    std::vector<std::string> log;
    if (!wowpatch::makeRecipe(A, B, meta, &rec, &log, &err)) {
        printAll(log);
        std::printf("[!!] %s\n", err.c_str());
        return 1;
    }
    printAll(log);
    const std::string json = wowpatch::recipeToJson(rec);
    if (!writeText(outJson, json, &err)) { std::printf("[!!] %s\n", err.c_str()); return 1; }
    std::printf("Рецепт сохранён: %s (%zu байт, hunks=%zu)\n", outJson.string().c_str(), json.size(), rec.hunks.size());

    // Контроль: читаем обратно и переносим на оригинал — должно совпасть с patched.
    wowpatch::Recipe back;
    if (wowpatch::recipeFromJson(json, &back, &err)) {
        Bytes target = A;
        const wowpe::Image img = wowpe::parse(target);
        wowpatch::Report rep;
        std::vector<std::string> rlog;
        const int moved = wowpatch::applyRecipeToImage(target, img, back, &rep, &rlog);
        const bool same = target == B;
        std::printf("Самопроверка рецепта: перенесено %d hunks, результат %s пропатченному файлу\n",
                    moved, same ? "побайтово РАВЕН" : "НЕ равен");
        if (!same) return 1;
    } else {
        std::printf("[!!] сохранённый JSON не читается обратно: %s\n", err.c_str());
        return 1;
    }
    return 0;
}

int cmdApplyRecipe(const fs::path &src, const fs::path &dstIn, const std::vector<std::string> &recipes) {
    if (recipes.empty()) { std::printf("[!!] не указано ни одного рецепта\n"); return 1; }
    Bytes data;
    std::string err;
    if (!readFile(src, &data, &err)) { std::printf("[!!] %s\n", err.c_str()); return 1; }
    const wowpe::Image img = wowpe::parse(data);
    if (!img.valid) { std::printf("[!!] PE не разобран: %s\n", img.error.c_str()); return 1; }

    wowpatch::Report rep;
    std::vector<std::string> log;
    int total = 0;
    for (const std::string &rp : recipes) {
        std::string text;
        if (!readText(rp, &text)) { std::printf("  ! рецепт %s не прочитан\n", rp.c_str()); continue; }
        wowpatch::Recipe r;
        if (!wowpatch::recipeFromJson(text, &r, &err)) {
            std::printf("  ! рецепт %s: %s\n", rp.c_str(), err.c_str());
            continue;
        }
        total += wowpatch::applyRecipeToImage(data, img, r, &rep, &log);
    }
    printAll(log);
    if (total == 0) { std::printf("[!!] ни один hunk не перенёсся — нечего сохранять\n"); return 1; }

    const bool inPlace = dstIn.empty() || fs::absolute(src) == fs::absolute(dstIn);
    const fs::path dst = inPlace ? src : dstIn;
    if (inPlace) {
        const fs::path bak = src.string() + ".orig";
        std::error_code ec;
        if (!fs::exists(bak, ec)) fs::copy_file(src, bak, ec);
    }
    if (!writeFile(dst, data, &err)) { std::printf("[!!] %s\n", err.c_str()); return 1; }
    std::printf("Записано: %s (перенесено hunks: %d)\n", dst.string().c_str(), total);

    std::vector<std::string> vout;
    const bool clean = wowpatch::verifyImage(data, img, rep, &vout);
    for (const std::string &s : vout) std::printf("  %s\n", s.c_str());
    std::printf("Проверка: %s\n", clean ? "чисто" : "есть замечания (см. строки [!!] и [i])");
    return 0;
}

} // namespace

int main(int argc, char **argv) {
#ifdef _WIN32
    // Консольный вывод в UTF-8, чтобы русские строки не превращались в кашу.
    SetConsoleOutputCP(65001);
#endif
    if (argc < 2) { usage(); return 2; }
    const std::string cmd = argv[1];
    Args a;
    std::string err;
    if (!parseArgs(argc, argv, &a, &err)) { std::printf("[!!] %s\n", err.c_str()); return 2; }
    if (a.help) { usage(); return 0; }

    if (cmd == "keys") return cmdKeys();
    if (cmd == "help" || cmd == "--help" || cmd == "-h") { usage(); return 0; }

    if (cmd == "diagnose") {
        if (a.positional.empty()) { std::printf("[!!] нужен путь к Wow.exe\n"); return 2; }
        return cmdDiagnose(fs::path(a.positional[0]), a);
    }
    if (cmd == "findslot") {
        if (a.positional.size() != 2) {
            std::printf("[!!] нужно: findslot <patched-reference.exe> <target.exe> [--window N]\n");
            return 2;
        }
        return cmdFindSlot(fs::path(a.positional[0]), fs::path(a.positional[1]), a);
    }
    if (cmd == "patch") {
        if (a.positional.empty()) { std::printf("[!!] нужен путь к исходному Wow.exe\n"); return 2; }
        if (a.positional.size() > 2) { std::printf("[!!] слишком много путей\n"); return 2; }
        if (a.portal.empty() && a.recipes.empty()) {
            std::printf("[!!] укажите --portal host[:port] (или --recipe, если переносите чужой патч)\n");
            return 2;
        }
        return cmdPatch(fs::path(a.positional[0]),
                        a.positional.size() > 1 ? fs::path(a.positional[1]) : fs::path(), a);
    }
    if (cmd == "recipe") {
        if (a.positional.size() != 3) {
            std::printf("[!!] нужно: recipe <original.exe> <patched.exe> <out.json>\n");
            return 2;
        }
        return cmdRecipe(fs::path(a.positional[0]), fs::path(a.positional[1]), fs::path(a.positional[2]), a);
    }
    if (cmd == "apply-recipe") {
        if (a.positional.size() < 2) {
            std::printf("[!!] нужно: apply-recipe <src.exe> <dst.exe> <recipe.json>…\n");
            return 2;
        }
        std::vector<std::string> recipes(a.positional.begin() + 2, a.positional.end());
        for (const std::string &r : a.recipes) recipes.push_back(r);
        return cmdApplyRecipe(fs::path(a.positional[0]), fs::path(a.positional[1]), recipes);
    }

    std::printf("[!!] неизвестная команда: %s\n\n", cmd.c_str());
    usage();
    return 2;
}
