#pragma once

// ---------------------------------------------------------------------------
// wow_test_fixture.h — синтетический PE32+ образ для автономных тестов.
//
// Общий для tools/wow_pe_selftest.cpp и tools/wow_patch_selftest.cpp, чтобы
// движок (src/wow_pe.*) и логика патча (src/wow_patch_core.*) проверялись на
// ОДНОМ И ТОМ ЖЕ файле. Собирается без Qt.
//
// Образнамеренно похож на настоящий Wow.exe:
//   .text  — код (в файле НЕ правим: у ретейла упакован);
//   .rdata — данные: «модуль RSA» (256 Б), «ключ Ed25519» (32 Б), строка
//            портала ".actual.battle.net";
//   .data  — записываемые данные;
//   .reloc — discardable (тоже не правим);
//   overlay — «таблица сертификатов» (data directory 4 хранит ФИЛЕОВОЕ смещение).
// Тот же 8-байтный якорь RSA продублирован в .text — это ловушка для наивного
// «заменить все вхождения»: правильный патчер обязан выбрать сайт в .rdata.
// ---------------------------------------------------------------------------

#include "wow_pe.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace wowtest {

inline void put16(std::vector<std::uint8_t>& v, std::size_t off, std::uint16_t x) {
    v[off] = std::uint8_t(x & 0xFF); v[off + 1] = std::uint8_t(x >> 8);
}
inline void put32(std::vector<std::uint8_t>& v, std::size_t off, std::uint32_t x) {
    for (int i = 0; i < 4; ++i) v[off + i] = std::uint8_t((x >> (8 * i)) & 0xFF);
}
inline void put64(std::vector<std::uint8_t>& v, std::size_t off, std::uint64_t x) {
    for (int i = 0; i < 8; ++i) v[off + i] = std::uint8_t((x >> (8 * i)) & 0xFF);
}


// ---------------------------------------------------------------------------
// Синтетический PE32+: .text (код), .rdata (данные), .data (данные), .reloc
// (discardable) + overlay с «таблицей сертификатов».
// ---------------------------------------------------------------------------
struct Fixture {
    std::vector<std::uint8_t> bytes;
    std::size_t textOff = 0, rdataOff = 0, dataOff = 0, relocOff = 0, certOff = 0;
    std::size_t modulusOff = 0, edOff = 0, portalOff = 0, textPatOff = 0;
};

inline Fixture makePe() {
    Fixture f;
    const std::uint32_t fileAlign = 0x200, sectAlign = 0x1000;
    const std::uint32_t hdrSize   = fileAlign;             // 0x200
    const std::uint32_t rawSize   = 0x400;                 // по 1 КБ на секцию
    const std::uint16_t nsec = 4;
    const std::uint32_t sizeOfOptional = 112 + 16 * 8;     // PE32+
    // PE\0\0 (4) + IMAGE_FILE_HEADER (20) + optional header.
    const std::size_t sectionsOff = 0x40 + 4 + 20 + sizeOfOptional;

    f.bytes.assign(hdrSize + rawSize * nsec, 0);

    // DOS header
    put16(f.bytes, 0, 0x5A4D);
    put32(f.bytes, 0x3C, 0x40);
    // PE signature + file header
    put32(f.bytes, 0x40, 0x00004550);
    put16(f.bytes, 0x44, 0x8664);          // Machine = AMD64
    put16(f.bytes, 0x46, nsec);
    put16(f.bytes, 0x54, sizeOfOptional);
    put16(f.bytes, 0x56, 0x0022);          // Characteristics

    const std::size_t oh = 0x58;
    put16(f.bytes, oh + 0, 0x020B);        // PE32+
    put32(f.bytes, oh + 16, sectAlign);    // EntryPoint RVA
    put64(f.bytes, oh + 24, 0x140000000ull);
    put32(f.bytes, oh + 32, sectAlign);
    put32(f.bytes, oh + 36, fileAlign);
    put32(f.bytes, oh + 56, sectAlign * (nsec + 1));   // SizeOfImage
    put32(f.bytes, oh + 60, hdrSize);                  // SizeOfHeaders
    put32(f.bytes, oh + 64, 0xDEADBEEF);               // CheckSum (заведомо неверный)
    put16(f.bytes, oh + 68, 2);                        // Subsystem = GUI
    put32(f.bytes, oh + 108, 16);                      // NumberOfRvaAndSizes

    const char* names[4] = { ".text", ".rdata", ".data", ".reloc" };
    const std::uint32_t chars[4] = {
        0x60000020,   // CODE | EXECUTE | READ
        0x40000040,   // INITIALIZED_DATA | READ
        0xC0000040,   // INITIALIZED_DATA | READ | WRITE
        0x42000040,   // INITIALIZED_DATA | DISCARDABLE | READ
    };
    for (int i = 0; i < 4; ++i) {
        const std::size_t sh = sectionsOff + std::size_t(i) * 40;
        std::memcpy(f.bytes.data() + sh, names[i], std::strlen(names[i]));
        put32(f.bytes, sh + 8,  rawSize);                       // VirtualSize
        put32(f.bytes, sh + 12, sectAlign * (i + 1));           // VirtualAddress
        put32(f.bytes, sh + 16, rawSize);                       // SizeOfRawData
        put32(f.bytes, sh + 20, hdrSize + rawSize * i);         // PointerToRawData
        put32(f.bytes, sh + 36, chars[i]);
    }
    f.textOff  = hdrSize + rawSize * 0;
    f.rdataOff = hdrSize + rawSize * 1;
    f.dataOff  = hdrSize + rawSize * 2;
    f.relocOff = hdrSize + rawSize * 3;

    // «Таблица сертификатов» в overlay + запись в data directory 4.
    f.certOff = f.bytes.size();
    const std::uint32_t certSize = 32;
    f.bytes.resize(f.certOff + certSize, 0xAB);
    put32(f.bytes, oh + 112 + 4 * 8 + 0, std::uint32_t(f.certOff));
    put32(f.bytes, oh + 112 + 4 * 8 + 4, certSize);

    // Полезные данные: 256-байтный «модуль RSA» и 32-байтный «ключ Ed25519»
    // в .rdata, а тот же 8-байтный якорь — ещё и в .text (ловушка для наивного
    // «заменить все вхождения»).
    const std::uint8_t anchor[8] = { 0x91, 0xD5, 0x9B, 0xB7, 0xD4, 0xE1, 0x83, 0xA5 };
    f.modulusOff = f.rdataOff + 0x40;
    std::memcpy(f.bytes.data() + f.modulusOff, anchor, 8);
    for (int i = 8; i < 256; ++i)
        f.bytes[f.modulusOff + i] = std::uint8_t(0x10 + i);

    f.textPatOff = f.textOff + 0x40;
    std::memcpy(f.bytes.data() + f.textPatOff, anchor, 8);

    const std::uint8_t edAnchor[8] = { 0x15, 0xD6, 0x18, 0xBD, 0x7D, 0xB5, 0x77, 0xBD };
    f.edOff = f.rdataOff + 0x200;
    std::memcpy(f.bytes.data() + f.edOff, edAnchor, 8);
    for (int i = 8; i < 32; ++i) f.bytes[f.edOff + i] = std::uint8_t(0xA0 + i);

    const char* portal = ".actual.battle.net";
    f.portalOff = f.rdataOff + 0x300;
    std::memcpy(f.bytes.data() + f.portalOff, portal, std::strlen(portal) + 1);
    return f;
}


} // namespace wowtest
