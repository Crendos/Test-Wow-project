// ---------------------------------------------------------------------------
// wow_pe_selftest.cpp — автономная проверка движка src/wow_pe.cpp.
//
// Собирается БЕЗ Qt, обычным компилятором:
//   g++ -std=c++20 -O2 -I../src wow_pe_selftest.cpp ../src/wow_pe.cpp -o wow_pe_selftest
//   ./wow_pe_selftest
// (цель wow_pe_selftest есть и в корневом CMakeLists.txt — опция
//  WOWDBSTUDIO_BUILD_TOOLS=ON по умолчанию, — и в автономном tools/CMakeLists.txt,
//  который собирает движок и тесты вообще без Qt:
//    cmake -S tools -B build-tools && cmake --build build-tools && ctest --test-dir build-tools)
//
// Смысл: логика смещений/секций/рецептов — ровно та часть патча Wow.exe,
// где ошибка стоит пользователю нерабочего клиента на 70 МБ. Поэтому она
// вынесена из Qt и покрыта тестами на синтетическом PE-образе.
// ---------------------------------------------------------------------------

#include "wow_pe.h"
#include "wow_test_fixture.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

int g_failures = 0;
int g_checks   = 0;

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) { ++g_failures; std::printf("  [FAIL] %s\n", what.c_str()); }
    else     { std::printf("  [ ok ] %s\n", what.c_str()); }
}

template <typename A, typename B>
void checkEq(const A& got, const B& want, const std::string& what) {
    ++g_checks;
    if (!(got == want)) {
        ++g_failures;
        std::printf("  [FAIL] %s (got %s, want %s)\n", what.c_str(),
                    std::to_string(static_cast<long long>(got)).c_str(),
                    std::to_string(static_cast<long long>(want)).c_str());
    } else {
        std::printf("  [ ok ] %s\n", what.c_str());
    }
}

} // namespace

int main() {
    using namespace wowpe;
    using namespace wowtest;

    std::printf("== SHA-256 / hex ==\n");
    {
        const std::string s1 = "abc";
        check(sha256Hex(reinterpret_cast<const std::uint8_t*>(s1.data()), s1.size()) ==
                  "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
              "sha256(\"abc\")");
        check(sha256Hex(reinterpret_cast<const std::uint8_t*>(""), 0) ==
                  "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
              "sha256(пустая строка)");
        // Длинный ввод: 1 МБ нулей, чтобы проверить многоблочный путь.
        const Bytes big(1u << 20, 0x61);
        check(sha256Hex(big).size() == 64, "sha256(1 МБ) даёт 64 hex-символа");

        Bytes out;
        check(hexToBytes("5FD6 800b:A7-FF,0140", &out).empty() && out.size() == 8 &&
                  out[0] == 0x5F && out[7] == 0x40, "hexToBytes: пробелы и разделители");
        check(!hexToBytes("ABC", &out).empty(), "hexToBytes: нечётная длина -> ошибка");
        check(!hexToBytes("ZZ", &out).empty(), "hexToBytes: не-hex -> ошибка");
        check(toHex(out, 4) == "5FD6800B...", "toHex: ограничение + многоточие");
        check(toHex(out) == "5FD6800BA7FF0140", "toHex: полный вывод");
    }

    std::printf("\n== Разбор PE ==\n");
    const Fixture fx = makePe();
    const Image img = parse(fx.bytes);
    check(img.valid, "PE разобран");
    check(img.plus, "PE32+");
    checkEq(img.machine, 0x8664, "Machine = AMD64");
    checkEq(img.numberOfSections, 4u, "4 секции");
    checkEq(img.imageBase, 0x140000000ull, "ImageBase");
    checkEq(img.sizeOfImage, 0x5000u, "SizeOfImage");
    checkEq(img.sizeOfHeaders, 0x200u, "SizeOfHeaders");
    check(img.checksumFileOffset != 0, "найдено поле CheckSum");
    checkEq(img.checksum, 0xDEADBEEFu, "прочитан CheckSum");
    checkEq(img.certTablePointer, std::uint32_t(fx.certOff), "каталог сертификатов: смещение");
    checkEq(img.certTableSize, 32u, "каталог сертификатов: размер");
    check(img.sections.size() == 4 && img.sections[0].name == ".text" &&
          img.sections[1].name == ".rdata" && img.sections[2].name == ".data" &&
          img.sections[3].name == ".reloc", "имена секций");
    check(img.sections[0].hasCode() && img.sections[0].isExecutable() &&
          !img.sections[0].isDiskPatchable(), ".text непатчабельна на диске");
    check(!img.sections[1].isExecutable() && img.sections[1].isDiskPatchable(),
          ".rdata патчабельна");
    check(img.sections[2].isWritable() && img.sections[2].isDiskPatchable(), ".data патчабельна");
    check(img.sections[3].isDiscardable() && !img.sections[3].isDiskPatchable(),
          ".reloc (discardable) непатчабельна");
    check(img.locationName(fx.rdataOff + 0x40) == ".rdata", "locationName(.rdata)");
    check(img.locationName(0x10) == "<header>", "locationName(<header>)");
    check(img.locationName(fx.certOff) == "<overlay>", "locationName(<overlay>)");
    checkEq(img.fileOffsetToRva(fx.rdataOff + 0x40), 0x2040u, "fileOffset -> RVA");
    checkEq(img.rvaToFileOffset(0x2040u), fx.rdataOff + 0x40, "RVA -> fileOffset");

    std::printf("\n== Поиск по маске и выбор сайта ==\n");
    {
        const Bytes anchor = { 0x91, 0xD5, 0x9B, 0xB7, 0xD4, 0xE1, 0x83, 0xA5 };
        const Pattern p = patternFromBytes(anchor.data(), anchor.size());
        const std::vector<std::size_t> all = findAll(fx.bytes, p);
        checkEq(all.size(), std::size_t(2), "якорь RSA встречается дважды (.text и .rdata)");

        const Site disk = chooseSite(fx.bytes, img, p, /*requirePatchable=*/true);
        check(disk.found, "сайт для диска найден");
        checkEq(disk.offset, fx.modulusOff, "выбран сайт в .rdata, а не в .text");
        check(disk.section == ".rdata", "секция сайта = .rdata");
        check(disk.patchable, "сайт помечен патчабельным");
        check(disk.note.find("первое") != std::string::npos || disk.note.find("секция") != std::string::npos,
              "в note объяснён выбор");

        const Site any = chooseSite(fx.bytes, img, p, /*requirePatchable=*/false);
        checkEq(any.offset, fx.textPatOff, "без требования патчабельности берётся первое (.text)");
        check(!any.patchable, ".text помечена непатчабельной");

        const Pattern wild = patternFromString(".actual.battle.net");
        checkEq(wild.size(), std::size_t(18), "строковый паттерн");
        const Site portal = chooseSite(fx.bytes, img, wild, true);
        checkEq(portal.offset, fx.portalOff, "портал найден");

        const Pattern withWc = { 0x91, kWildcard, 0x9B, kWildcard, 0xD4 };
        checkEq(findAll(fx.bytes, withWc).size(), std::size_t(2), "поиск с масками");
        const Pattern allWc(4, kWildcard);
        checkEq(findAll(fx.bytes, allWc).size(), std::size_t(0), "паттерн из одних масок отклонён");
    }

    std::printf("\n== Запись, checksum, Authenticode ==\n");
    {
        Bytes data = fx.bytes;
        const Bytes repl(256, 0x5F);
        check(writeBytes(data, fx.modulusOff, repl), "запись 256 байт в .rdata");
        check(data[fx.modulusOff] == 0x5F && data[fx.modulusOff + 255] == 0x5F, "байты записаны");
        check(!writeBytes(data, data.size() - 4, Bytes(8, 0)), "запись за EOF отклонена");

        std::uint32_t newSum = 0;
        check(updateChecksum(data, img, &newSum), "пересчёт CheckSum");
        check(newSum != 0xDEADBEEFu, "CheckSum изменился");
        const Image img2 = parse(data);
        checkEq(img2.checksum, newSum, "CheckSum записан в заголовок");
        // Повторный пересчёт идемпотентен.
        std::uint32_t again = 0;
        Bytes data2 = data;
        updateChecksum(data2, img2, &again);
        checkEq(again, newSum, "пересчёт идемпотентен");

        Bytes stripped = data;
        const std::size_t sizeBefore = stripped.size();
        const std::uint32_t left = stripAuthenticode(stripped, img2, /*truncateOverlay=*/true);
        checkEq(left, 0u, "каталог сертификатов обнулён");
        checkEq(stripped.size(), sizeBefore - 32, "overlay с подписью обрезан");
        const Image img3 = parse(stripped);
        check(img3.valid && img3.certTablePointer == 0 && img3.certTableSize == 0,
              "после strip подпись отсутствует");

        Bytes noTrunc = data;
        stripAuthenticode(noTrunc, img2, false);
        checkEq(noTrunc.size(), sizeBefore, "без truncateOverlay размер не меняется");
    }

    std::printf("\n== Рецепт: diff двух образов ==\n");
    {
        Bytes a = fx.bytes;
        Bytes b = fx.bytes;
        const Image imgA = parse(a);

        // «Firestorm-патч»: модуль RSA, ключ Ed25519 и хвост портала.
        for (int i = 0; i < 256; ++i) b[fx.modulusOff + i] = std::uint8_t(0x5F + (i & 0x0F));
        for (int i = 0; i < 32;  ++i) b[fx.edOff + i]      = std::uint8_t(0x02 + (i & 0x07));
        std::memcpy(b.data() + fx.portalOff + 8, "wowemu.dev", 10);

        const std::vector<Hunk> hunks = diffHunks(a, b, imgA, 24, 96);
        checkEq(hunks.size(), std::size_t(3), "три hunks (RSA, Ed25519, портал)");
        if (hunks.size() == 3) {
            checkEq(hunks[0].offset, fx.modulusOff, "hunk 0: смещение RSA");
            checkEq(hunks[0].before.size(), std::size_t(256), "hunk 0: 256 байт");
            checkEq(hunks[0].after.size(),  std::size_t(256), "hunk 0: after 256 байт");
            checkEq(hunks[0].ctxBefore.size(), std::size_t(24), "hunk 0: контекст слева 24");
            checkEq(hunks[0].ctxAfter.size(),  std::size_t(24), "hunk 0: контекст справа 24");
            check(hunks[0].section == ".rdata", "hunk 0: секция .rdata");
            checkEq(hunks[1].offset, fx.edOff, "hunk 1: Ed25519");
            checkEq(hunks[2].offset, fx.portalOff + 8, "hunk 2: хвост портала");
            checkEq(hunks[2].before.size(), std::size_t(10), "hunk 2: 10 байт (battle.net)");
        }
        // Разные размеры — рецепт не строится.
        Bytes bShort = b; bShort.resize(bShort.size() - 64);
        checkEq(diffHunks(a, bShort, imgA).size(), std::size_t(0), "разные размеры -> нет hunks");
        // Идентичные файлы.
        checkEq(diffHunks(a, a, imgA).size(), std::size_t(0), "идентичные файлы -> нет hunks");

        // --- перенос рецепта ---
        const Hunk& h0 = hunks[0];
        {
            Locate l = locateHunk(a, h0);
            check(l.kind == Locate::Kind::Match, "перенос на тот же образ: Match");
            checkEq(l.offset, fx.modulusOff, "перенос: смещение совпало");
        }
        {
            Locate l = locateHunk(b, h0);
            check(l.kind == Locate::Kind::AlreadyApplied, "целевой файл уже пропатчен");
        }
        {
            // «Другой билд»: сайт переехал, контекст рядом изменился.
            Bytes t = a;
            const std::size_t newOff = fx.dataOff + 0x80;
            std::memcpy(t.data() + newOff, a.data() + fx.modulusOff, 256);
            for (int i = 0; i < 256; ++i) t[fx.modulusOff + i] = std::uint8_t(i * 3 + 1);
            Locate l = locateHunk(t, h0);
            check(l.kind == Locate::Kind::Relocated || l.kind == Locate::Kind::Match,
                  "переехавший сайт найден");
            checkEq(l.offset, newOff, "перенос: новое смещение");
        }
        {
            // Контекст полностью изменён, но сам сайт уникален -> ищем по байтам.
            Bytes t = a;
            for (std::size_t i = fx.modulusOff - 24; i < fx.modulusOff; ++i) t[i] = 0xEE;
            for (std::size_t i = fx.modulusOff + 256; i < fx.modulusOff + 280; ++i) t[i] = 0xEE;
            Locate l = locateHunk(t, h0);
            check(l.kind == Locate::Kind::Match || l.kind == Locate::Kind::Relocated,
                  "найден по байтам сайта, когда контекст не совпал");
            checkEq(l.offset, fx.modulusOff, "смещение сайта сохранено");
            check(!l.note.empty(), "note объясняет способ поиска");
        }
        {
            // Неоднозначность: два одинаковых сайта, контекст не помогает.
            // .data предварительно заполняем мусором, чтобы контекст не совпал
            // «случайно» (в .rdata вокруг сайта нули — как в настоящем exe).
            Bytes t = a;
            for (std::size_t i = fx.dataOff; i < fx.dataOff + 0x400; ++i) t[i] = 0x5A;
            std::memcpy(t.data() + fx.dataOff + 0x40, a.data() + fx.modulusOff, 256);
            for (std::size_t i = fx.modulusOff - 24; i < fx.modulusOff + 256 + 24; ++i) t[i] = 0x77;
            std::memcpy(t.data() + fx.modulusOff, a.data() + fx.modulusOff, 256);
            const Locate l = locateHunk(t, h0);
            check(l.kind == Locate::Kind::Ambiguous,
                  "два одинаковых сайта без контекста -> Ambiguous");
            check(l.candidates.size() >= 2, "Ambiguous перечисляет кандидатов");
        }
        {
            Hunk bogus;
            bogus.offset = 10;
            Locate l = locateHunk(a, bogus);
            check(l.kind == Locate::Kind::NotFound, "пустой hunk -> NotFound");
        }
    }

    std::printf("\n== contains ==\n");
    {
        const Bytes hay = { 1, 2, 3, 4, 5 };
        check(contains(hay, 1, { 2, 3 }), "contains: вхождение");
        check(!contains(hay, 4, { 5, 6 }), "contains: выход за границу");
        check(contains(hay, 0, {}), "contains: пустая иголка");
    }

    std::printf("\n-----------------------------------------\n");
    std::printf("Проверок: %d, провалов: %d\n", g_checks, g_failures);
    if (g_failures) { std::printf("SELFTEST FAILED\n"); return 1; }
    std::printf("SELFTEST OK\n");
    return 0;
}
