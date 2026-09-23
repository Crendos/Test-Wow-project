// ---------------------------------------------------------------------------
// wow_mem_patcher.cpp — патч клиента WoW В ПАМЯТИ, без изменения файла на диске.
//
// Зачем: на некоторых билдах (12.x, защита Arxan/Digital.ai) пропатченный
// на диске Wow.exe тихо завершается при старте — файл проверяется рантаймом.
// Классическое решение (стиль Arctium/Firestorm launcher): клиент запускается
// ИСХОДНЫМ, а ключи подменяются в его адресном пространстве ПОСЛЕ полной
// распаковки образа, до момента коннекта:
//
//   * ConnectTo RSA-модуль (256 байт) — подпись SMSG_CONNECT_TO.
//     Ищется и в LE, и в BE (порядок байт модуля зависит от билда);
//   * GameCrypto Ed25519 public key (32 байта) — подпись SMSG_ENTER_ENCRYPTED_MODE.
//
// Портал ходит через WTF/Config.wtf (SET portal "host:port") — файловых
// изменений кроме этой текстовой строки не нужно вообще.
//
// Использование (Windows):
//   wow_mem_patcher.exe --portal 127.0.0.1:1119
//       [--process Wow.exe]            имя процесса клиента
//       [--start "C:\...\Wow.exe"]     запустить клиент, если не запущен
//       [--timeout 300]                секунд на дождаться/пропатчить
//
// Сборка (так же просто, как wow_patch_cli, Qt не нужен):
//   cl /std:c++20 /O2 /EHsc /utf-8 /Isrc /Itools tools\wow_mem_patcher.cpp ^
//      src\wow_patch_core.cpp src\wow_pe.cpp version.lib /Fe:wow_mem_patcher.exe
//
// На не-Windows платформах собирается заглушка: инструмент имеет смысл
// только на машине, где запускается сам клиент Wow.exe (Windows).
// ---------------------------------------------------------------------------

#include "wow_patch_core.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <string>

#ifdef _WIN32

#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;

// ---------------------------------------------------------------- утилиты

static void enableUtf8Console()
{
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
}

static std::string toUtf8(const std::wstring &w)
{
    if (w.empty())
        return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n - 1 : 0, '\0');
    if (n > 0)
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), n, nullptr, nullptr);
    return s;
}

// ----------------------------------------------------------- процессы

static DWORD findProcessByName(const std::wstring &exeName)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return 0;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    DWORD pid = 0;
    if (Process32FirstW(snap, &pe))
    {
        do
        {
            if (_wcsicmp(pe.szExeFile, exeName.c_str()) == 0)
            {
                pid = pe.th32ProcessID;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return pid;
}

static bool startProcess(const std::wstring &exePath)
{
    std::wstring cmd = L"\"" + exePath + L"\"";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr,
                             FALSE, 0, nullptr, nullptr, &si, &pi);
    if (!ok)
        return false;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

// --------------------------------------------------------- цели патча

struct Target
{
    std::string     name;
    wowpatch::Bytes signature;    // что ищем: первые 8 байт родного ключа (LE)
    wowpatch::Bytes value;        // что пишем поверх: полный ключ TrinityCore
    wowpatch::Bytes tcSig;        // первые 8 байт value (детект «уже стоит»)
    // Вариант big-endian: если модуль в билде хранится разворотом — ищем его
    // и пишем соответственно. Пусто = цель без альтернативного порядка.
    wowpatch::Bytes altSignature;
    wowpatch::Bytes altValue;
    wowpatch::Bytes altTcSig;
};

struct TargetStatus
{
    bool blizzard = false;   // родная сигнатура где-то видна
    bool tc       = false;   // ключ TrinityCore уже стоит
    int  patched  = 0;       // заменено участков на этом проходе (всего)
    bool viaBe    = false;   // последняя замена была по BE-сигнатуре
};

template <typename Fn>
static void forEachOccurrence(const wowpatch::Bytes &hay, const wowpatch::Bytes &pattern, Fn &&fn)
{
    if (pattern.empty() || hay.size() < pattern.size())
        return;
    const size_t n = hay.size() - pattern.size();
    for (size_t i = 0; i <= n; ++i)
        if (std::memcmp(hay.data() + i, pattern.data(), pattern.size()) == 0)
            fn(i);
}

// Запись в чужую память: обычный WriteProcessMemory, при отказе (страница
// read-only) — временное VirtualProtectEx(PAGE_EXECUTE_READWRITE) и повтор.
static bool writeRemote(HANDLE process, uintptr_t address,
                        const wowpatch::Bytes &value, bool *usedProtect)
{
    if (usedProtect) *usedProtect = false;
    SIZE_T w = 0;
    if (WriteProcessMemory(process, reinterpret_cast<LPVOID>(address),
                           value.data(), value.size(), &w)
        && w == value.size())
        return true;

    DWORD oldProtect = 0;
    if (!VirtualProtectEx(process, reinterpret_cast<LPVOID>(address),
                          value.size(), PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;
    if (usedProtect) *usedProtect = true;
    w = 0;
    const bool ok = WriteProcessMemory(process, reinterpret_cast<LPVOID>(address),
                                       value.data(), value.size(), &w)
                    && w == value.size();
    DWORD tmp = 0;
    VirtualProtectEx(process, reinterpret_cast<LPVOID>(address),
                     value.size(), oldProtect, &tmp);
    return ok;
}

// ---------------------------------------------------------------------------
// Один проход по памяти процесса:
//  * ищет ключи Blizzard (LE и BE) -> перезаписывает ключами TrinityCore
//    того же порядка + побайтовая верификация;
//  * фиксирует per-target: виден ли родной ключ, стоит ли TrinityCore.
// Буфер читается ЦЕЛИКОМ по региону — сигнатура не может «порваться» на стыке.
// ---------------------------------------------------------------------------
static void memoryPass(HANDLE process, const std::vector<Target> &targets,
                       std::vector<TargetStatus> *st)
{
    for (TargetStatus &s : *st)
        s = TargetStatus{};

    const size_t maxRegion = 512ull * 1024 * 1024;

    uintptr_t address = 0;
    while (address < 0x00007FFE00000000ull)
    {
        MEMORY_BASIC_INFORMATION mbi{};
        if (VirtualQueryEx(process, reinterpret_cast<LPCVOID>(address), &mbi, sizeof(mbi)) != sizeof(mbi))
            break;
        uintptr_t next = address + mbi.RegionSize;
        if (next < address)
            break;

        const bool scan = (mbi.State == MEM_COMMIT)
            && !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))
            && (mbi.Type == MEM_IMAGE || mbi.Type == MEM_PRIVATE)
            && mbi.RegionSize > 0 && mbi.RegionSize <= maxRegion;

        if (scan)
        {
            wowpatch::Bytes buf(mbi.RegionSize);
            SIZE_T got = 0;
            if (ReadProcessMemory(process, mbi.BaseAddress, buf.data(),
                                  buf.size(), &got) && got > 0)
            {
                buf.resize(got);
                const uintptr_t base = reinterpret_cast<uintptr_t>(mbi.BaseAddress);

                for (size_t ti = 0; ti < targets.size(); ++ti)
                {
                    const Target &t = targets[ti];
                    TargetStatus &s = (*st)[ti];

                    auto replaceAt = [&](size_t off, const wowpatch::Bytes &val, bool be)
                    {
                        const uintptr_t abs = base + static_cast<uintptr_t>(off);

                        wowpatch::Bytes current(val.size());
                        SIZE_T r = 0;
                        if (ReadProcessMemory(process, reinterpret_cast<LPCVOID>(abs),
                                current.data(), current.size(), &r)
                            && r == current.size() && current == val)
                            return; // уже наше значение

                        bool usedProtect = false;
                        if (!writeRemote(process, abs, val, &usedProtect))
                            return;

                        wowpatch::Bytes back(val.size());
                        SIZE_T r2 = 0;
                        if (ReadProcessMemory(process, reinterpret_cast<LPCVOID>(abs),
                                back.data(), back.size(), &r2) && back == val)
                        {
                            ++s.patched;
                            s.viaBe = be;
                        }
                    };

                    // 1) Родный ключ Blizzard (LE) — заменить LE-значением.
                    forEachOccurrence(buf, t.signature, [&](size_t off)
                    {
                        s.blizzard = true;
                        replaceAt(off, t.value, /*be=*/false);
                    });
                    // 1b) Родный ключ в BE-порядке — заменить BE-значением.
                    if (!t.altSignature.empty())
                        forEachOccurrence(buf, t.altSignature, [&](size_t off)
                        {
                            s.blizzard = true;
                            replaceAt(off, t.altValue.empty() ? t.value : t.altValue, /*be=*/true);
                        });

                    // 2) Уже стоит ключ TrinityCore (любым порядком)?
                    bool seen = false;
                    forEachOccurrence(buf, t.tcSig, [&](size_t) { seen = true; });
                    if (seen) s.tc = true;
                    if (!t.altTcSig.empty())
                        forEachOccurrence(buf, t.altTcSig, [&](size_t) { seen = true; });
                    if (seen) s.tc = true;
                }
            }
        }
        address = next;
    }
}

// ----------------------------------------------------------- Config.wtf

static bool writePortalIntoConfig(const std::wstring &clientExePath,
                                  const std::string &portal,
                                  std::string *note)
{
    fs::path exe(clientExePath);
    fs::path wtf = exe.parent_path() / "WTF";
    std::error_code ec;
    fs::create_directories(wtf, ec);
    fs::path cfg = wtf / "Config.wtf";

    const std::string line = "SET portal \"" + portal + "\"";
    std::string contents;
    {
        std::ifstream in(cfg, std::ios::binary);
        if (in)
            contents.assign(std::istreambuf_iterator<char>(in),
                            std::istreambuf_iterator<char>());
    }

    const std::string key = "SET portal ";
    bool replaced = false;
    std::string out;
    out.reserve(contents.size() + line.size() + 8);

    size_t pos = 0;
    while (pos < contents.size())
    {
        size_t eol = contents.find('\n', pos);
        if (eol == std::string::npos)
            eol = contents.size();
        std::string l = contents.substr(pos, eol - pos);
        std::string trimmed = l;
        while (!trimmed.empty() && trimmed.back() == '\r')
            trimmed.pop_back();
        if (!replaced && trimmed.compare(0, key.size(), key) == 0)
        {
            out += line;
            replaced = true;
        }
        else
            out += l;
        if (eol < contents.size())
            out += '\n';
        pos = eol + 1;
    }
    if (!replaced)
    {
        if (!out.empty() && out.back() != '\n')
            out += '\n';
        out += line;
        out += '\n';
    }

    std::ofstream fout(cfg, std::ios::binary | std::ios::trunc);
    if (!fout)
    {
        if (note) *note = "не удалось записать " + toUtf8(cfg.wstring());
        return false;
    }
    fout.write(out.data(), static_cast<std::streamsize>(out.size()));
    if (note) *note = std::string(replaced ? "обновлена" : "добавлена") + " строка: " + line
                    + "  (" + toUtf8(cfg.wstring()) + ")";
    return true;
}

// -------------------------------------------------------------- главная

static void printUsage()
{
    std::puts(
        "Патч клиента WoW в памяти (без изменения файла на диске).\n"
        "\n"
        "  wow_mem_patcher.exe --portal 127.0.0.1:1119 [опции]\n"
        "\n"
        "    --process Wow.exe          имя процесса клиента\n"
        "    --start \"C:\\...\\Wow.exe\"  запустить клиент, если не запущен\n"
        "    --portal host:port         SET portal в WTF/Config.wtf\n"
        "    --timeout 300              секунд ожидания (по умолчанию 300)\n"
        "\n"
        "Работает с ИСХОДНЫМ Wow.exe: файл не меняется, защита клиента\n"
        "не срабатывает. Ключи заменяются в памяти после полной загрузки.\n");
}

static void printStatuses(const std::vector<Target> &targets,
                          const std::vector<TargetStatus> &st, int pass)
{
    std::printf("Проход %d:", pass);
    for (size_t i = 0; i < targets.size(); ++i)
    {
        const TargetStatus &s = st[i];
        std::printf("  %s=%s", targets[i].name.c_str(),
                    s.tc && !s.blizzard ? "TC" :
                    s.patched           ? "patched" :
                    s.blizzard          ? "Blizzard" : "not-seen");
        if (s.tc && !s.blizzard && s.patched > 0)
            std::printf("(%d%s)", s.patched, s.viaBe ? ",BE" : ",LE");
    }
    std::printf("\n");
}

int wmain(int argc, wchar_t **argv)
{
    enableUtf8Console();

    std::wstring processName = L"Wow.exe";
    std::wstring startPath;
    std::string  portal;
    int          timeoutSec = 300;

    auto next = [&](int *i, const wchar_t *optName) -> std::wstring {
        if (*i + 1 >= argc)
        {
            std::fprintf(stderr, "Опция %ls требует значение\n", optName);
            std::exit(64);
        }
        ++(*i);
        return argv[*i];
    };

    for (int i = 1; i < argc; ++i)
    {
        std::wstring a = argv[i];
        if (a == L"--process")
            processName = next(&i, L"--process");
        else if (a == L"--start")
            startPath = next(&i, L"--start");
        else if (a == L"--portal")
            portal = toUtf8(next(&i, L"--portal"));
        else if (a == L"--timeout")
            timeoutSec = std::wcstol(next(&i, L"--timeout").c_str(), nullptr, 10);
        else if (a == L"--help" || a == L"-h" || a == L"/?")
        {
            printUsage();
            return 0;
        }
        else
        {
            std::fprintf(stderr, "Неизвестная опция: %ls\n", a.c_str());
            printUsage();
            return 64;
        }
    }

    std::puts("== wow_mem_patcher: живая замена ключей в памяти клиента ==");

    // 1) Найти процесс клиента (при желании — запустить).
    DWORD pid = findProcessByName(processName);
    if (!pid && !startPath.empty())
    {
        std::printf("Клиент не запущен — стартую: %s\n", toUtf8(startPath).c_str());
        if (!startProcess(startPath))
            std::fprintf(stderr, "! не удалось запустить %s (код %lu)\n",
                         toUtf8(startPath).c_str(), static_cast<unsigned long>(GetLastError()));
    }

    auto t0 = std::chrono::steady_clock::now();
    while (!pid)
    {
        if (std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - t0).count() > timeoutSec)
        {
            std::fprintf(stderr, "Процесс %ls так и не появился за %d с\n",
                         processName.c_str(), timeoutSec);
            return 2;
        }
        Sleep(1000);
        pid = findProcessByName(processName);
    }
    std::printf("Процесс %ls найден, pid=%lu\n", processName.c_str(),
                static_cast<unsigned long>(pid));

    // 2) Открыть.
    HANDLE proc = OpenProcess(PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION
                              | PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!proc)
    {
        std::fprintf(stderr,
            "OpenProcess failed: %lu. Запустите патчер от администратора.\n",
            static_cast<unsigned long>(GetLastError()));
        return 3;
    }

    // 3) Путь клиента — для WTF/Config.wtf.
    std::wstring exePath;
    {
        wchar_t buf[MAX_PATH * 2];
        DWORD n = sizeof(buf) / sizeof(*buf);
        if (QueryFullProcessImageNameW(proc, 0, buf, &n))
            exePath.assign(buf, n);
    }

    if (!portal.empty() && !exePath.empty())
    {
        std::string note;
        if (writePortalIntoConfig(exePath, portal, &note))
            std::printf("Config.wtf: %s\n", note.c_str());
        else
            std::fprintf(stderr, "! Config.wtf: %s\n", note.c_str());
    }

    // 4) Цели: два ключа Blizzard -> ключи TrinityCore (LE и BE на выбор билда).
    std::vector<Target> targets;
    {
        Target t;
        t.name         = "ConnectTo-RSA";
        t.signature    = wowpatch::blizzardRsaSignature();
        t.value        = wowpatch::trinityRsaModulusLe();
        t.tcSig.assign(t.value.begin(), t.value.begin() + std::min(size_t(8), t.value.size()));
        t.altSignature = wowpatch::blizzardRsaSignatureBe();
        t.altValue     = wowpatch::trinityRsaModulusBe();
        t.altTcSig.assign(t.altValue.begin(), t.altValue.begin() + std::min(size_t(8), t.altValue.size()));
        targets.push_back(std::move(t));

        Target t2;
        t2.name      = "GameCrypto-Ed25519";
        t2.signature = wowpatch::blizzardEd25519Signature();
        t2.value     = wowpatch::trinityEd25519PublicKey();
        t2.tcSig.assign(t2.value.begin(), t2.value.begin() + std::min(size_t(8), t2.value.size()));
        targets.push_back(std::move(t2));
    }
    std::vector<TargetStatus> st(targets.size());

    std::puts("Жду полной загрузки клиента и ищу ключи Blizzard в памяти (LE+BE)...");
    int pass = 0;
    int stablePasses = 0;
    int lastPatchedTotal = -1;
    for (;;)
    {
        ++pass;
        memoryPass(proc, targets, &st);

        int patchedTotal = 0;
        int pending = 0;       // Blizzard виден, TrinityCore ещё нет
        int hidden = 0;        // ни того, ни другого — ключ, возможно, обфусцирован
        for (size_t i = 0; i < targets.size(); ++i)
        {
            patchedTotal += st[i].patched;
            if (st[i].blizzard && !st[i].tc) ++pending;
            if (!st[i].blizzard && !st[i].tc) ++hidden;
        }

        if (patchedTotal != lastPatchedTotal)
        {
            printStatuses(targets, st, pass);
            lastPatchedTotal = patchedTotal;
        }
        else if (pass % 15 == 1)
            printStatuses(targets, st, pass);

        // Успех: ни одного «висящего» ключа (Blizzard без TrinityCore).
        if (pending == 0)
            ++stablePasses;
        else
            stablePasses = 0;

        if (stablePasses >= 2)
        {
            std::puts("\nГотово: в памяти не осталось родных ключей Blizzard.");
            for (size_t i = 0; i < targets.size(); ++i)
            {
                if (st[i].tc)
                    std::printf("  [%s] ключ TrinityCore на месте%s\n",
                                targets[i].name.c_str(),
                                st[i].viaBe ? " (BE-порядок)" : "");
                else if (!st[i].blizzard && !st[i].tc)
                    std::printf("  [%s] ВНИМАНИЕ: ни родного, ни нашего ключа не видно — "
                                "возможно, он обфусцирован и на этом этапе. Если логин "
                                "падает на ConnectTo — сообщите, нужен отдельный разбор.\n",
                                targets[i].name.c_str());
            }
            break;
        }

        const auto spent = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - t0).count();
        if (spent > timeoutSec)
        {
            std::fprintf(stderr, "Таймаут %d с. Состояние ключей:\n", timeoutSec);
            for (size_t i = 0; i < targets.size(); ++i)
                std::fprintf(stderr, "  [%s] blizzard=%d trinity=%d patched=%d\n",
                             targets[i].name.c_str(), st[i].blizzard, st[i].tc, st[i].patched);
            std::fprintf(stderr, "Повторите с большим --timeout или пришлите этот вывод.\n");
            CloseHandle(proc);
            return 5;
        }
        Sleep(2000);
    }

    CloseHandle(proc);
    std::puts("Клиент пропатчен в памяти — можно логиниться на ваш сервер.");
    std::puts("(При полном перезапуске клиента запустите патчер снова.)");
    return 0;
}

#else // !_WIN32 — только синтаксическая проверка переносимой обвязки

int main()
{
    std::puts("wow_mem_patcher собирается и работает только под Windows "
              "(правит память запущенного Wow.exe). См. docs/PATCH_FIRESTORM_12.1.0.md.");
    return 0;
}

#endif // _WIN32
