#pragma once

// ---------------------------------------------------------------------------
// wow_pe.h — низкоуровневый движок разбора PE и бинарного патча Wow.exe.
//
// Намеренно БЕЗ Qt: только стандартный C++20. Это даёт две вещи:
//   1) логику можно собрать и прогнать в тестах где угодно
//      (см. tools/wow_pe_selftest.cpp — собирается обычным g++/cl без Qt);
//   2) Qt-слой (client_patch_service.cpp) остаётся тонкой обёрткой и не
//      содержит арифметики смещений, в которой легко ошибиться.
//
// Зачем это нужно для «Firestorm-стиля»: статический патч Wow.exe на диске
// работает только тогда, когда байты попадают в СЕКЦИЮ ДАННЫХ (.rdata/.data).
// Правка .text в файле бессмысленна — у ретейла 11.x/12.x код упакован
// (Arxan/Digital.ai) и восстанавливается при запуске, а заодно ломается
// контроль целостности. Поэтому движок обязан уметь читать таблицу секций и
// отказываться от сомнительных сайтов, а не «заменять все вхождения подряд».
// ---------------------------------------------------------------------------

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace wowpe {

using Bytes   = std::vector<std::uint8_t>;
using Pattern = std::vector<std::int16_t>;   // 0..255 — литерал, -1 — маска

constexpr std::int16_t kWildcard = -1;

// ---------------------------------------------------------------------------
// Секция PE
// ---------------------------------------------------------------------------
struct Section {
    std::string  name;
    std::uint32_t index          = 0;
    std::uint32_t virtualAddress = 0;
    std::uint32_t virtualSize    = 0;
    std::uint32_t rawPointer     = 0;
    std::uint32_t rawSize        = 0;
    std::uint32_t characteristics= 0;

    bool hasCode()      const;   // IMAGE_SCN_CNT_CODE
    bool isExecutable() const;   // IMAGE_SCN_MEM_EXECUTE
    bool isWritable()   const;   // IMAGE_SCN_MEM_WRITE
    bool isDiscardable()const;   // IMAGE_SCN_MEM_DISCARDABLE

    // Можно ли править эти байты в файле и ждать, что клиент их увидит:
    // не код, не отбрасываемая секция, есть инициализированные данные.
    bool isDiskPatchable() const;
};

// ---------------------------------------------------------------------------
// Разобранный образ
// ---------------------------------------------------------------------------
struct Image {
    bool          valid = false;
    std::string   error;

    bool          plus = false;          // PE32+ (64 бита)
    std::uint16_t machine   = 0;
    std::uint16_t subsystem = 0;
    std::uint32_t numberOfSections = 0;

    std::uint64_t imageBase       = 0;
    std::uint32_t entryPointRva   = 0;
    std::uint32_t sizeOfImage     = 0;
    std::uint32_t sizeOfHeaders   = 0;
    std::uint32_t sectionAlignment= 0;
    std::uint32_t fileAlignment   = 0;

    std::uint32_t checksum          = 0;
    std::size_t   checksumFileOffset= 0;
    std::uint32_t numberOfRvaAndSizes = 0;
    std::size_t   dataDirectoryFileOffset = 0;

    // Data directory 4 (Security). Важно: это ФИЛЕОВОЕ смещение, а не RVA.
    std::uint32_t certTablePointer = 0;
    std::uint32_t certTableSize    = 0;

    std::size_t sectionsFileOffset = 0;
    std::size_t fileSize           = 0;

    std::vector<Section> sections;

    const Section* sectionByFileOffset(std::size_t off) const;
    const Section* sectionByRva(std::uint32_t rva) const;
    std::uint32_t  fileOffsetToRva(std::size_t off) const;      // 0 = не попало
    std::size_t    rvaToFileOffset(std::uint32_t rva) const;     // SIZE_MAX = не попало
    // "<header>", "<overlay>" или имя секции — для логов.
    std::string    locationName(std::size_t off) const;
    bool           isDiskPatchableOffset(std::size_t off) const;
};

Image parse(const std::uint8_t* data, std::size_t size);
inline Image parse(const Bytes& b) { return parse(b.data(), b.size()); }

// ---------------------------------------------------------------------------
// Поиск по маске
// ---------------------------------------------------------------------------
Pattern patternFromBytes(const std::uint8_t* p, std::size_t n);
Pattern patternFromString(const std::string& s);
// limit = 0 — без ограничения. Возвращает ВСЕ вхождения (для диагностики).
std::vector<std::size_t> findAll(const std::uint8_t* data, std::size_t size,
                                 const Pattern& pat, std::size_t limit = 0);
inline std::vector<std::size_t> findAll(const Bytes& d, const Pattern& p, std::size_t limit = 0) {
    return findAll(d.data(), d.size(), p, limit);
}
bool contains(const Bytes& hay, std::size_t from, const Bytes& needle);

// ---------------------------------------------------------------------------
// Выбор сайта под патч
// ---------------------------------------------------------------------------
struct Site {
    bool          found      = false;
    std::size_t   offset     = 0;
    std::string   section;
    bool          patchable  = false;
    std::string   note;                 // почему выбрано / почему отказ
    std::vector<std::size_t> allHits;   // все вхождения — для отчёта
};

// requirePatchable = true (режим «диск»): берём первое вхождение в .rdata/.data.
// requirePatchable = false (режим «память»/диагностика): берём первое вхождение.
Site chooseSite(const std::uint8_t* data, std::size_t size, const Image& img,
                const Pattern& pat, bool requirePatchable);
inline Site chooseSite(const Bytes& d, const Image& img, const Pattern& p, bool req) {
    return chooseSite(d.data(), d.size(), img, p, req);
}

bool writeBytes(Bytes& data, std::size_t offset, const std::uint8_t* bytes, std::size_t n);
inline bool writeBytes(Bytes& data, std::size_t offset, const Bytes& b) {
    return writeBytes(data, offset, b.data(), b.size());
}

// ---------------------------------------------------------------------------
// PE checksum / Authenticode
// ---------------------------------------------------------------------------
std::uint32_t computeChecksum(const std::uint8_t* data, std::size_t size,
                              std::size_t checksumFieldOffset);
bool updateChecksum(Bytes& data, const Image& img, std::uint32_t* newValue);

// Обнуляет каталог сертификатов (data dir 4). После правки байт подпись всё
// равно невалидна, а «подписанный, но битый» файл смущает SmartScreen и
// некоторые лаунчеры сильнее, чем честный «без подписи».
// truncateOverlay = обрезать хвост файла, если таблица лежит в самом конце.
// Возвращает новое значение certTablePointer (0 = подписи больше нет).
std::uint32_t stripAuthenticode(Bytes& data, const Image& img, bool truncateOverlay);

// ---------------------------------------------------------------------------
// «Рецепт» — разница двух образов ОДНОГО билда (оригинал + уже пропатченный
// клиент, например оригинальный Wow.exe 11.2.5 и «WoW 11.2.5 - Firestorm.exe»).
// Рецепт хранит не только байты, но и контекст, поэтому его можно перенести на
// ДРУГОЙ билд (12.1.0.69497): сайт ищется по контексту, а не по смещению.
// ---------------------------------------------------------------------------
struct Hunk {
    std::size_t offset = 0;      // смещение в оригинале
    Bytes before;                // байты оригинала
    Bytes after;                 // байты пропатченного файла
    Bytes ctxBefore;             // неизменный контекст слева
    Bytes ctxAfter;              // неизменный контекст справа
    std::string section;
};

std::vector<Hunk> diffHunks(const std::uint8_t* a, std::size_t aSize,
                            const std::uint8_t* b, std::size_t bSize,
                            const Image& imgA,
                            std::size_t context  = 24,
                            std::size_t mergeGap = 96,
                            std::size_t maxHunks = 8192);
inline std::vector<Hunk> diffHunks(const Bytes& a, const Bytes& b, const Image& imgA,
                                   std::size_t context = 24, std::size_t mergeGap = 96,
                                   std::size_t maxHunks = 8192) {
    return diffHunks(a.data(), a.size(), b.data(), b.size(), imgA, context, mergeGap, maxHunks);
}

struct Locate {
    enum class Kind { NotFound, Match, AlreadyApplied, Relocated, Ambiguous };
    Kind kind = Kind::NotFound;
    std::size_t offset = 0;                 // куда писать (для Match/Relocated)
    std::vector<std::size_t> candidates;    // для Ambiguous
    std::string note;
};

// Ищет hunk в target по контексту; если контекст не найден — постепенно его
// укорачивает, а в конце пробует сами байты `before` (только если они уникальны).
Locate locateHunk(const std::uint8_t* target, std::size_t targetSize, const Hunk& h);
inline Locate locateHunk(const Bytes& t, const Hunk& h) {
    return locateHunk(t.data(), t.size(), h);
}

// ---------------------------------------------------------------------------
// Мелочи
// ---------------------------------------------------------------------------
std::string sha256Hex(const std::uint8_t* data, std::size_t size);
inline std::string sha256Hex(const Bytes& b) { return sha256Hex(b.data(), b.size()); }
std::string toHex(const std::uint8_t* data, std::size_t n, std::size_t maxBytes = 0);
inline std::string toHex(const Bytes& b, std::size_t maxBytes = 0) {
    return toHex(b.data(), b.size(), maxBytes);
}
std::string hexToBytes(const std::string& hex, Bytes* out);  // возвращает ошибку или ""

} // namespace wowpe
