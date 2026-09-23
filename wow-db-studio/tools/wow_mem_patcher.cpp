// ---------------------------------------------------------------------------
// wow_mem_patcher.cpp — патч клиента WoW В ПАМЯТИ, без изменения файла на диске.
//
// Зачем: на некоторых билдах (12.x, защита Arxan/Digital.ai) пропатченный
// на диске Wow.exe тихо завершается при старте — файл проверяется рантаймом.
// Классическое решение (стиль Arctium/Firestorm launcher): клиент запускается
// ИСХОДНЫМ, а ключи подменяются в его адресном пространстве ПОСЛЕ полной
// распаковки образа, до момента коннекта:
//
//   * ConnectTo RSA-модуль (256 байт, LE) — подпись SMSG_CONNECT_TO;
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
    // Кириллица в выводе приводится в UTF-8 — аккуратно в любом терминале.
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
}

static std::wstring toWide(const std::string &s)
{
    if (s.empty())
        return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n > 0 ? n - 1 : 0, L'\0');
    if (n > 0)
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
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
    // Командная строка целиком в кавычках — путь с пробелами обязателен к цитированию.
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
    std::string           name;
    wowpatch::Bytes       signature;   // что ищем: первые 8 байт родного ключа Blizzard
    wowpatch::Bytes       value;       // что пишем поверх: полный ключ TrinityCore
    wowpatch::Bytes       tcSig;       // первые 8 байт значения (детект «уже стоит»)
};

// Находит в буфере hay все вхождения pattern, вызывая fn(offset).
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

// ---------------------------------------------------------------------------
// Один проход по памяти процесса:
//  * ищет ключи Blizzard -> перезаписывает ключами TrinityCore + верифицирует;
//  * отмечает наличие уже стоящих ключей TrinityCore;
//  * /patchedCount — сколько участков заменено на этом проходе.
// Буфер читается ЦЕЛИКОМ по региону — сигнатура не может «порваться» на стыке.
// ---------------------------------------------------------------------------
static bool memoryPass(HANDLE process, const std::vector<Target> &targets,
                       bool *blizzardSeen, bool *trinitySeen, int *patchedCount)
{
    *blizzardSeen = false;
    *trinitySeen  = false;
    *patchedCount = 0;

    const size_t maxRegion = 512ull * 1024 * 1024; // огромные кэши-регионы пропускаем

    uintptr_t address = 0;
    while (address < 0x00007FFE00000000ull)
    {
        MEMORY_BASIC_INFORMATION mbi{};
        if (VirtualQueryEx(process, reinterpret_cast<LPCVOID>(address), &mbi, sizeof(mbi)) != sizeof(mbi))
            break; // конец адресного пространства

        uintptr_t next = address + mbi.RegionSize;
        if (next < address)
            break; // переполнение — стоп

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

                for (const Target &t : targets)
                {
                    // 1) Все родные ключи Blizzard — заменить.
                    forEachOccurrence(buf, t.signature, [&](size_t off)
                    {
                        *blizzardSeen = true;
                        const uintptr_t abs = base + static_cast<uintptr_t>(off);

                        // Уже стоит наше значение (напр. дубль сигнатуры)?
                        wowpatch::Bytes current(t.value.size());
                        SIZE_T r = 0;
                        if (ReadProcessMemory(process, reinterpret_cast<LPCVOID>(abs),
                                current.data(), current.size(), &r)
                            && r == current.size() && current == t.value)
                            return;

                        SIZE_T w = 0;
                        if (!WriteProcessMemory(process, reinterpret_cast<LPVOID>(abs),
                                t.value.data(), t.value.size(), &w)
                            || w != t.value.size())
                            return;

                        // Верификация записи.
                        wowpatch::Bytes back(t.value.size());
                        SIZE_T r2 = 0;
                        if (ReadProcessMemory(process, reinterpret_cast<LPCVOID>(abs),
                                back.data(), back.size(), &r2)
                            && back == t.value)
                            ++(*patchedCount);
                    });

                    // 2) Точка уже держит ключ TrinityCore?
                    bool seen = false;
                    forEachOccurrence(buf, t.tcSig, [&](size_t) { seen = true; });
                    if (seen)
                        *trinitySeen = true;
                }
            }
        }
        address = next;
    }
    return true;
}

// ----------------------------------------------------------- Config.wtf

static bool writePortalIntoConfig(const std::wstring &clientExePath,
                                  const std::string &portal,
                                  std::string *note)
{
    fs::path exe(clientExePath);
    fs::path wtf = exe.parent_path() / "WTF";   // .\_retail_\WTF
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

    // toWide нужен только клиентам API по строкам — процесс ищем wide-именем.
    (void)toWide;

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

    // 4) Цели: два ключа Blizzard -> ключи TrinityCore.
    std::vector<Target> targets;
    {
        Target t;
        t.name      = "ConnectTo-RSA";
        t.signature = wowpatch::blizzardRsaSignature();
        t.value     = wowpatch::trinityRsaModulusLe();
        t.tcSig.assign(t.value.begin(), t.value.begin() + std::min(size_t(8), t.value.size()));
        targets.push_back(std::move(t));

        Target t2;
        t2.name      = "GameCrypto-Ed25519";
        t2.signature = wowpatch::blizzardEd25519Signature();
        t2.value     = wowpatch::trinityEd25519PublicKey();
        t2.tcSig.assign(t2.value.begin(), t2.value.begin() + std::min(size_t(8), t2.value.size()));
        targets.push_back(std::move(t2));
    }

    std::puts("Жду полной загрузки клиента и ищу ключи Blizzard в памяти...");
    int passNumber = 0;
    for (;;)
    {
        ++passNumber;
        bool blizzardSeen = false, trinitySeen = false;
        int patched = 0;
        memoryPass(proc, targets, &blizzardSeen, &trinitySeen, &patched);

        if (patched > 0)
            std::printf("Проход %d: заменено участков: %d\n", passNumber, patched);
        else if (passNumber % 10 == 1)
            std::printf("Проход %d: ключей ещё нет (клиент грузится)...\n", passNumber);

        if (trinitySeen && !blizzardSeen && passNumber > 1)
        {
            std::puts("\nГотово: в памяти остались только ключи TrinityCore.");
            break;
        }

        const auto spent = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - t0).count();
        if (spent > timeoutSec)
        {
            std::fprintf(stderr,
                "Таймаут %d с: blizzard=%d trinity=%d. Повторите с большим --timeout.\n",
                timeoutSec, blizzardSeen ? 1 : 0, trinitySeen ? 1 : 0);
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
