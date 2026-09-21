#include "wow_pe.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace wowpe {
namespace {

// ---------------------------------------------------------------------------
// Чтение полей PE (little-endian) с проверкой границ
// ---------------------------------------------------------------------------
template <typename T>
bool readLe(const std::uint8_t* data, std::size_t size, std::size_t off, T* out) {
    if (off + sizeof(T) > size) return false;
    T v = 0;
    for (std::size_t i = 0; i < sizeof(T); ++i)
        v |= static_cast<T>(static_cast<T>(data[off + i]) << (8 * i));
    *out = v;
    return true;
}

template <typename T>
void writeLe(std::uint8_t* data, std::size_t off, T v) {
    for (std::size_t i = 0; i < sizeof(T); ++i)
        data[off + i] = static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF);
}

constexpr std::uint32_t kScnCntCode             = 0x00000020;
constexpr std::uint32_t kScnCntInitializedData  = 0x00000040;
constexpr std::uint32_t kScnMemDiscardable      = 0x02000000;
constexpr std::uint32_t kScnMemExecute          = 0x20000000;
constexpr std::uint32_t kScnMemWrite            = 0x80000000;

// ---------------------------------------------------------------------------
// SHA-256 (чтобы движок не зависел ни от Qt, ни от OpenSSL)
// ---------------------------------------------------------------------------
struct Sha256 {
    std::uint32_t h[8] = { 0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                           0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u };
    std::uint64_t len = 0;
    std::uint8_t  buf[64]{};
    std::size_t   bufLen = 0;

    static std::uint32_t rotr(std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

    void block(const std::uint8_t* p) {
        static const std::uint32_t k[64] = {
            0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
            0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
            0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
            0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
            0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
            0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
            0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
            0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u };
        std::uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = (std::uint32_t(p[i * 4]) << 24) | (std::uint32_t(p[i * 4 + 1]) << 16) |
                   (std::uint32_t(p[i * 4 + 2]) << 8) | std::uint32_t(p[i * 4 + 3]);
        for (int i = 16; i < 64; ++i) {
            const std::uint32_t s0 = rotr(w[i-15],7) ^ rotr(w[i-15],18) ^ (w[i-15] >> 3);
            const std::uint32_t s1 = rotr(w[i-2],17) ^ rotr(w[i-2],19)  ^ (w[i-2] >> 10);
            w[i] = w[i-16] + s0 + w[i-7] + s1;
        }
        std::uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
        for (int i = 0; i < 64; ++i) {
            const std::uint32_t S1 = rotr(e,6) ^ rotr(e,11) ^ rotr(e,25);
            const std::uint32_t ch = (e & f) ^ (~e & g);
            const std::uint32_t t1 = hh + S1 + ch + k[i] + w[i];
            const std::uint32_t S0 = rotr(a,2) ^ rotr(a,13) ^ rotr(a,22);
            const std::uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t t2 = S0 + mj;
            hh=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
        }
        h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=hh;
    }

    void update(const std::uint8_t* p, std::size_t n) {
        len += n;
        while (n) {
            const std::size_t take = std::min(n, std::size_t(64) - bufLen);
            std::memcpy(buf + bufLen, p, take);
            bufLen += take; p += take; n -= take;
            if (bufLen == 64) { block(buf); bufLen = 0; }
        }
    }

    std::string finalHex() {
        const std::uint64_t bits = len * 8;
        std::uint8_t pad = 0x80;
        update(&pad, 1);
        pad = 0x00;
        while (bufLen != 56) update(&pad, 1);
        std::uint8_t tail[8];
        for (int i = 0; i < 8; ++i) tail[i] = std::uint8_t((bits >> (56 - 8 * i)) & 0xFF);
        // update() меняет len, но он больше не используется.
        std::memcpy(buf + 56, tail, 8);
        block(buf);
        static const char* hexd = "0123456789abcdef";
        std::string out;
        out.reserve(64);
        for (int i = 0; i < 8; ++i)
            for (int j = 3; j >= 0; --j) {
                const std::uint8_t byte = std::uint8_t((h[i] >> (8 * j)) & 0xFF);
                out.push_back(hexd[byte >> 4]);
                out.push_back(hexd[byte & 0xF]);
            }
        return out;
    }
};

} // namespace

// ---------------------------------------------------------------------------
// Section
// ---------------------------------------------------------------------------
bool Section::hasCode()       const { return (characteristics & kScnCntCode) != 0; }
bool Section::isExecutable()  const { return (characteristics & kScnMemExecute) != 0; }
bool Section::isWritable()    const { return (characteristics & kScnMemWrite) != 0; }
bool Section::isDiscardable() const { return (characteristics & kScnMemDiscardable) != 0; }

bool Section::isDiskPatchable() const {
    if (isDiscardable()) return false;
    if (rawSize == 0)    return false;   // неинициализированные данные: в файле их нет
    // Код и исполняемые секции на диске править нельзя: у ретейла 11.x/12.x
    // .text упакован и распаковывается в памяти, а заодно проверяется на
    // целостность — правка файла либо потеряется, либо уронит клиент.
    if (hasCode() || isExecutable()) return false;
    return true;
}

// ---------------------------------------------------------------------------
// Image
// ---------------------------------------------------------------------------
const Section* Image::sectionByFileOffset(std::size_t off) const {
    for (const Section& s : sections) {
        if (s.rawSize == 0) continue;
        if (off >= s.rawPointer && off < std::size_t(s.rawPointer) + s.rawSize)
            return &s;
    }
    return nullptr;
}

const Section* Image::sectionByRva(std::uint32_t rva) const {
    for (const Section& s : sections) {
        const std::uint32_t vs = s.virtualSize ? s.virtualSize : s.rawSize;
        if (rva >= s.virtualAddress && rva < s.virtualAddress + vs)
            return &s;
    }
    return nullptr;
}

std::uint32_t Image::fileOffsetToRva(std::size_t off) const {
    if (const Section* s = sectionByFileOffset(off))
        return std::uint32_t(s->virtualAddress + (off - s->rawPointer));
    if (off < sizeOfHeaders) return std::uint32_t(off);
    return 0;
}

std::size_t Image::rvaToFileOffset(std::uint32_t rva) const {
    if (const Section* s = sectionByRva(rva)) {
        const std::size_t delta = rva - s->virtualAddress;
        if (delta < s->rawSize) return s->rawPointer + delta;
        return std::size_t(-1);
    }
    if (rva < sizeOfHeaders) return rva;
    return std::size_t(-1);
}

std::string Image::locationName(std::size_t off) const {
    if (const Section* s = sectionByFileOffset(off)) return s->name;
    if (off < sizeOfHeaders) return "<header>";
    return "<overlay>";
}

bool Image::isDiskPatchableOffset(std::size_t off) const {
    const Section* s = sectionByFileOffset(off);
    return s ? s->isDiskPatchable() : false;
}

// ---------------------------------------------------------------------------
// parse
// ---------------------------------------------------------------------------
Image parse(const std::uint8_t* data, std::size_t size) {
    Image img;
    img.fileSize = size;
    if (!data || size < 0x40) { img.error = "файл меньше заголовка DOS (не PE)"; return img; }
    std::uint16_t mz = 0;
    if (!readLe(data, size, 0, &mz) || mz != 0x5A4D) { img.error = "нет сигнатуры MZ"; return img; }

    std::uint32_t lfanew = 0;
    if (!readLe(data, size, 0x3C, &lfanew) || lfanew == 0 || lfanew + 4 > size) {
        img.error = "битый e_lfanew"; return img;
    }
    std::uint32_t sig = 0;
    if (!readLe(data, size, lfanew, &sig) || sig != 0x00004550) { img.error = "нет сигнатуры PE\\0\\0"; return img; }

    const std::size_t fh = std::size_t(lfanew) + 4;
    // Все поля IMAGE_FILE_HEADER читаем ТОЧНО их ширины: NumberOfSections —
    // 2 байта. Чтение «как uint32» захватит половину TimeDateStamp и даст
    // абсурдное число секций (проверено на настоящих PE из corkami/pocs).
    std::uint16_t machine = 0, numberOfSections = 0, sizeOfOptional = 0, characteristics = 0;
    if (!readLe(data, size, fh + 0,  &machine) ||
        !readLe(data, size, fh + 2,  &numberOfSections) ||
        !readLe(data, size, fh + 16, &sizeOfOptional) ||
        !readLe(data, size, fh + 18, &characteristics)) {
        img.error = "IMAGE_FILE_HEADER не читается"; return img;
    }
    img.machine = machine;
    img.numberOfSections = numberOfSections;

    const std::size_t oh = fh + 20;
    std::uint16_t magic = 0;
    if (!readLe(data, size, oh, &magic)) { img.error = "нет optional header"; return img; }
    img.plus = (magic == 0x20B);
    if (magic != 0x20B && magic != 0x10B) { img.error = "неизвестный optional header magic"; return img; }

    if (!readLe(data, size, oh + 16, &img.entryPointRva) ||
        !readLe(data, size, oh + 56, &img.sizeOfImage) ||
        !readLe(data, size, oh + 60, &img.sizeOfHeaders) ||
        !readLe(data, size, oh + 64, &img.checksum) ||
        !readLe(data, size, oh + 68, &img.subsystem) ||
        !readLe(data, size, oh + 32, &img.sectionAlignment) ||
        !readLe(data, size, oh + 36, &img.fileAlignment) ||
        !readLe(data, size, oh + (img.plus ? 108 : 92), &img.numberOfRvaAndSizes)) {
        img.error = "optional header не читается целиком"; return img;
    }
    img.checksumFileOffset = oh + 64;

    if (img.plus) {
        std::uint64_t base = 0;
        if (!readLe(data, size, oh + 24, &base)) { img.error = "нет ImageBase"; return img; }
        img.imageBase = base;
    } else {
        std::uint32_t base = 0;
        if (!readLe(data, size, oh + 28, &base)) { img.error = "нет ImageBase"; return img; }
        img.imageBase = base;
    }

    img.dataDirectoryFileOffset = oh + (img.plus ? 112 : 96);
    if (img.numberOfRvaAndSizes > 16) img.numberOfRvaAndSizes = 16;
    if (img.numberOfRvaAndSizes > 4) {
        const std::size_t dd = img.dataDirectoryFileOffset + 4 * 8;
        std::uint32_t p = 0, s = 0;
        if (readLe(data, size, dd + 0, &p) && readLe(data, size, dd + 4, &s)) {
            img.certTablePointer = p;
            img.certTableSize    = s;
        }
    }

    img.sectionsFileOffset = oh + sizeOfOptional;
    // Таблица секций читается настолько, насколько её реально хватило в файле.
    // У обрезанных/вырожденных PE заявленное число секций может превышать файл —
    // тогда берём то, что помещается (как pefile), а не отказываемся от разбора.
    std::size_t readable = 0;
    if (img.sectionsFileOffset < size) readable = (size - img.sectionsFileOffset) / 40;
    std::size_t count = std::min<std::size_t>(img.numberOfSections, readable);
    if (img.numberOfSections > 0 && count == 0) {
        img.error = "таблица секций не помещается в файл"; return img;
    }
    if (count != img.numberOfSections) img.numberOfSections = std::uint32_t(count);
    img.sections.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t sh = img.sectionsFileOffset + std::size_t(i) * 40;
        Section s;
        s.index = std::uint32_t(i);
        char nm[9] = {0};
        std::memcpy(nm, data + sh, 8);
        s.name = nm;
        while (!s.name.empty() && s.name.back() == '\0') s.name.pop_back();
        readLe(data, size, sh + 8,  &s.virtualSize);
        readLe(data, size, sh + 12, &s.virtualAddress);
        readLe(data, size, sh + 16, &s.rawSize);
        readLe(data, size, sh + 20, &s.rawPointer);
        readLe(data, size, sh + 36, &s.characteristics);
        img.sections.push_back(s);
    }
    img.valid = true;
    return img;
}

// ---------------------------------------------------------------------------
// Паттерны и поиск
// ---------------------------------------------------------------------------
Pattern patternFromBytes(const std::uint8_t* p, std::size_t n) {
    Pattern out;
    out.reserve(n);
    for (std::size_t i = 0; i < n; ++i) out.push_back(static_cast<std::int16_t>(p[i]));
    return out;
}

Pattern patternFromString(const std::string& s) {
    return patternFromBytes(reinterpret_cast<const std::uint8_t*>(s.data()), s.size());
}

std::vector<std::size_t> findAll(const std::uint8_t* data, std::size_t size,
                                 const Pattern& pat, std::size_t limit) {
    std::vector<std::size_t> hits;
    const std::size_t n = pat.size();
    if (n == 0 || n > size || !data) return hits;

    // Быстрый путь для точного паттерна (без масок): memcmp-подобный поиск
    // по первому байту. Для паттерна с масками — обычный перебор с якорем.
    bool exact = std::all_of(pat.begin(), pat.end(), [](std::int16_t v) { return v >= 0; });
    std::size_t anchor = 0;
    if (!exact) {
        bool has = false;
        for (std::size_t i = 0; i < n; ++i)
            if (pat[i] >= 0) { anchor = i; has = true; break; }
        if (!has) return hits;   // паттерн целиком из масок — бессмысленно
    }
    const std::uint8_t anchorByte = static_cast<std::uint8_t>(pat[anchor]);

    for (std::size_t pos = 0; pos + n <= size; ++pos) {
        if (data[pos + anchor] != anchorByte) continue;
        bool ok = true;
        for (std::size_t i = 0; i < n; ++i) {
            if (pat[i] >= 0 && data[pos + i] != static_cast<std::uint8_t>(pat[i])) { ok = false; break; }
        }
        if (!ok) continue;
        hits.push_back(pos);
        if (limit && hits.size() >= limit) break;
        // Вхождения могут перекрываться (например «AAAA» в «AAAAAA»), шаг = 1.
    }
    return hits;
}

bool contains(const Bytes& hay, std::size_t from, const Bytes& needle) {
    if (needle.empty()) return true;
    if (from + needle.size() > hay.size()) return false;
    return std::memcmp(hay.data() + from, needle.data(), needle.size()) == 0;
}

// ---------------------------------------------------------------------------
// chooseSite
// ---------------------------------------------------------------------------
Site chooseSite(const std::uint8_t* data, std::size_t size, const Image& img,
                const Pattern& pat, bool requirePatchable) {
    Site site;
    site.allHits = findAll(data, size, pat);
    if (site.allHits.empty()) {
        site.note = "паттерн не найден";
        return site;
    }
    for (std::size_t off : site.allHits) {
        const Section* s = img.sectionByFileOffset(off);
        const bool patchable = s ? s->isDiskPatchable() : false;
        if (!requirePatchable || patchable) {
            site.found     = true;
            site.offset    = off;
            site.patchable = patchable;
            site.section   = img.locationName(off);
            if (!patchable && requirePatchable)
                site.note = "в секции кода — для диска не годится";
            else if (!patchable)
                site.note = "секция " + site.section + " (код) — править можно только в памяти";
            else if (site.allHits.size() > 1)
                site.note = "взято первое из " + std::to_string(site.allHits.size()) +
                            " вхождений (секция " + site.section + ")";
            else
                site.note = "секция " + site.section;
            return site;
        }
    }
    site.found   = false;
    site.section = img.locationName(site.allHits.front());
    site.note    = "все " + std::to_string(site.allHits.size()) +
                   " вхождений в непатчабельных секциях (первое — " + site.section + ")";
    return site;
}

// ---------------------------------------------------------------------------
// Запись
// ---------------------------------------------------------------------------
bool writeBytes(Bytes& data, std::size_t offset, const std::uint8_t* bytes, std::size_t n) {
    if (n == 0) return true;
    if (offset + n > data.size()) return false;
    std::memcpy(data.data() + offset, bytes, n);
    return true;
}

// ---------------------------------------------------------------------------
// Checksum
// ---------------------------------------------------------------------------
std::uint32_t computeChecksum(const std::uint8_t* data, std::size_t size,
                              std::size_t checksumFieldOffset) {
    // Канонический PE checksum (как CheckSumMappedFile в imagehlp и как считает
    // pefile.generate_checksum): сумма 16-битных слов со свёрткой переносов в
    // 16 бит, поле CheckSum считается нулевым, в конце прибавляется длина файла.
    std::uint64_t sum = 0;
    std::size_t i = 0;
    for (; i + 1 < size; i += 2) {
        if (i == checksumFieldOffset || i == checksumFieldOffset + 2) continue;
        sum += std::uint32_t(data[i]) | (std::uint32_t(data[i + 1]) << 8);
        sum = (sum & 0xFFFFull) + (sum >> 16);
    }
    if (i < size) sum += data[i];   // хвостовой нечётный байт
    sum = (sum & 0xFFFFull) + (sum >> 16);
    sum = (sum & 0xFFFFull) + (sum >> 16);
    return std::uint32_t(sum + size);
}

bool updateChecksum(Bytes& data, const Image& img, std::uint32_t* newValue) {
    if (!img.valid || img.checksumFileOffset + 4 > data.size()) return false;
    const std::uint32_t v = computeChecksum(data.data(), data.size(), img.checksumFileOffset);
    writeLe<std::uint32_t>(data.data(), img.checksumFileOffset, v);
    if (newValue) *newValue = v;
    return true;
}

// ---------------------------------------------------------------------------
// Authenticode
// ---------------------------------------------------------------------------
std::uint32_t stripAuthenticode(Bytes& data, const Image& img, bool truncateOverlay) {
    if (!img.valid || img.numberOfRvaAndSizes <= 4) return img.certTablePointer;
    const std::size_t dd = img.dataDirectoryFileOffset + 4 * 8;
    if (dd + 8 > data.size()) return img.certTablePointer;
    if (img.certTablePointer == 0 && img.certTableSize == 0) return 0;

    writeLe<std::uint32_t>(data.data(), dd + 0, 0u);
    writeLe<std::uint32_t>(data.data(), dd + 4, 0u);

    if (truncateOverlay && img.certTablePointer != 0) {
        const std::size_t certStart = img.certTablePointer;
        const std::size_t certEnd   = certStart + img.certTableSize;
        // Таблица сертификатов всегда лежит в самом конце файла (overlay).
        // Обрезаем только если за ней нет ничего, кроме выравнивания.
        if (certEnd <= data.size() && certStart < data.size()) {
            bool onlyPadding = true;
            for (std::size_t i = certEnd; i < data.size(); ++i)
                if (data[i] != 0) { onlyPadding = false; break; }
            if (onlyPadding) data.resize(certStart);
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// diffHunks
// ---------------------------------------------------------------------------
std::vector<Hunk> diffHunks(const std::uint8_t* a, std::size_t aSize,
                            const std::uint8_t* b, std::size_t bSize,
                            const Image& imgA,
                            std::size_t context, std::size_t mergeGap, std::size_t maxHunks) {
    std::vector<Hunk> hunks;
    if (!a || !b || aSize != bSize) return hunks;   // разные размеры — рецепт не строится

    std::vector<std::size_t> diffPos;
    diffPos.reserve(1024);
    for (std::size_t i = 0; i < aSize; ++i)
        if (a[i] != b[i]) diffPos.push_back(i);
    if (diffPos.empty()) return hunks;

    std::size_t groupStart = 0;
    for (std::size_t k = 0; k <= diffPos.size(); ++k) {
        const bool last = (k == diffPos.size());
        const bool newGroup = !last && k > groupStart &&
                              (diffPos[k] - diffPos[k - 1] > mergeGap);
        if (!last && !newGroup) continue;

        const std::size_t endIdx = last ? diffPos.size() - 1 : k - 1;
        const std::size_t first  = diffPos[groupStart];
        const std::size_t lastPos= diffPos[endIdx];

        Hunk h;
        h.offset = first;
        const std::size_t len = lastPos - first + 1;
        h.before.assign(a + first, a + first + len);
        h.after .assign(b + first, b + first + len);
        const std::size_t cb = std::min(context, first);
        const std::size_t ca = std::min(context, aSize - (lastPos + 1));
        h.ctxBefore.assign(a + first - cb, a + first);
        h.ctxAfter .assign(a + lastPos + 1, a + lastPos + 1 + ca);
        if (imgA.valid) {
            if (const Section* s = imgA.sectionByFileOffset(first)) h.section = s->name;
            else h.section = imgA.locationName(first);
        }
        hunks.push_back(std::move(h));
        if (hunks.size() >= maxHunks) return hunks;
        groupStart = k;
    }
    return hunks;
}

// ---------------------------------------------------------------------------
// locateHunk
// ---------------------------------------------------------------------------
namespace {

std::vector<std::size_t> findExact(const std::uint8_t* data, std::size_t size,
                                   const std::uint8_t* needle, std::size_t n,
                                   std::size_t limit = 16) {
    std::vector<std::size_t> out;
    if (n == 0 || n > size) return out;
    for (std::size_t i = 0; i + n <= size; ++i) {
        if (data[i] != needle[0]) continue;
        if (std::memcmp(data + i, needle, n) == 0) {
            out.push_back(i);
            if (out.size() >= limit) break;
        }
    }
    return out;
}

} // namespace

Locate locateHunk(const std::uint8_t* target, std::size_t targetSize, const Hunk& h) {
    Locate res;
    if (!target || h.before.empty()) { res.note = "пустой hunk"; return res; }
    if (h.before.size() > targetSize)  { res.note = "hunk больше файла"; return res; }

    // Полный «конвейер» поиска: контекст слева + before + контекст справа.
    Bytes needle;
    needle.reserve(h.ctxBefore.size() + h.before.size() + h.ctxAfter.size());
    needle.insert(needle.end(), h.ctxBefore.begin(), h.ctxBefore.end());
    needle.insert(needle.end(), h.before.begin(),  h.before.end());
    needle.insert(needle.end(), h.ctxAfter.begin(), h.ctxAfter.end());
    const std::size_t shift = h.ctxBefore.size();

    // Пробуем полный контекст, затем укорачиваем его вдвое (по обоим концам),
    // пока не останется только сам `before`.
    std::size_t trimLeft  = 0;
    std::size_t trimRight = 0;
    for (int attempt = 0; attempt < 8; ++attempt) {
        const std::size_t lb = trimLeft;
        const std::size_t rb = needle.size() - trimRight;
        if (lb >= rb) break;
        const std::size_t n = rb - lb;
        const std::vector<std::size_t> hits = findExact(target, targetSize, needle.data() + lb, n);
        if (hits.size() == 1) {
            const std::size_t off = hits[0] + shift - lb;
            if (off + h.before.size() <= targetSize) {
                res.offset = off;
                if (std::memcmp(target + off, h.before.data(), h.before.size()) == 0)
                    res.kind = (off == h.offset) ? Locate::Kind::Match : Locate::Kind::Relocated;
                else if (!h.after.empty() && off + h.after.size() <= targetSize &&
                         std::memcmp(target + off, h.after.data(), h.after.size()) == 0)
                    res.kind = Locate::Kind::AlreadyApplied;
                else {
                    res.kind = Locate::Kind::NotFound;
                    res.note = "контекст совпал, но байты сайта другие";
                }
                if (res.kind != Locate::Kind::NotFound) {
                    if (res.kind == Locate::Kind::Relocated)
                        res.note = "сайт переехал: 0x" + std::to_string(h.offset) + " -> 0x" +
                                   std::to_string(off);
                    return res;
                }
            }
        } else if (hits.size() > 1) {
            // Несколько совпадений: если одно из них даёт исходное смещение — берём его.
            for (std::size_t p : hits) {
                const std::size_t off = p + shift - lb;
                if (off == h.offset && off + h.before.size() <= targetSize &&
                    std::memcmp(target + off, h.before.data(), h.before.size()) == 0) {
                    res.kind = Locate::Kind::Match;
                    res.offset = off;
                    res.note = "контекст неоднозначен, но исходное смещение совпало";
                    return res;
                }
            }
            res.kind = Locate::Kind::Ambiguous;
            for (std::size_t p : hits) res.candidates.push_back(p + shift - lb);
            res.note = "контекст встречается " + std::to_string(hits.size()) + " раз";
            return res;
        }
        // Укорачиваем контекст: сначала правый, потом левый.
        const std::size_t rightCtx = h.ctxAfter.size() - trimRight;
        if (rightCtx > 0) {
            trimRight += std::max<std::size_t>(1, rightCtx / 2);
            if (trimRight > h.ctxAfter.size()) trimRight = h.ctxAfter.size();
        } else {
            const std::size_t leftCtx = h.ctxBefore.size() - trimLeft;
            if (leftCtx == 0) break;
            trimLeft += std::max<std::size_t>(1, leftCtx / 2);
            if (trimLeft > h.ctxBefore.size()) trimLeft = h.ctxBefore.size();
        }
        if (trimLeft >= h.ctxBefore.size() && trimRight >= h.ctxAfter.size()) break;
    }

    // Последняя надежда — сам `before`, если он уникален во всём файле.
    const std::vector<std::size_t> raw = findExact(target, targetSize, h.before.data(),
                                                   h.before.size(), 4);
    if (raw.size() == 1) {
        res.kind   = (raw[0] == h.offset) ? Locate::Kind::Match : Locate::Kind::Relocated;
        res.offset = raw[0];
        res.note   = "найден по самим байтам (контекст не совпал — другой билд)";
        return res;
    }
    if (raw.size() > 1) {
        res.kind = Locate::Kind::Ambiguous;
        res.candidates = raw;
        res.note = "байты сайта встречаются " + std::to_string(raw.size()) + " раз, контекст не совпал";
        return res;
    }

    // Возможно, патч уже применён.
    if (!h.after.empty()) {
        const std::vector<std::size_t> done = findExact(target, targetSize, h.after.data(),
                                                        h.after.size(), 4);
        if (done.size() == 1) {
            res.kind   = Locate::Kind::AlreadyApplied;
            res.offset = done[0];
            res.note   = "в целевом файле уже стоят пропатченные байты";
            return res;
        }
    }
    res.kind = Locate::Kind::NotFound;
    res.note = "сайт не найден ни по контексту, ни по байтам";
    return res;
}

// ---------------------------------------------------------------------------
// Мелочи
// ---------------------------------------------------------------------------
std::string sha256Hex(const std::uint8_t* data, std::size_t size) {
    Sha256 ctx;
    // update() крупными кусками, чтобы не плодить вызовы.
    const std::size_t chunk = std::size_t(1) << 20;
    for (std::size_t off = 0; off < size; off += chunk)
        ctx.update(data + off, std::min(chunk, size - off));
    if (size == 0) ctx.update(data, 0);
    return ctx.finalHex();
}

std::string toHex(const std::uint8_t* data, std::size_t n, std::size_t maxBytes) {
    static const char* hexd = "0123456789ABCDEF";
    const std::size_t lim = (maxBytes && maxBytes < n) ? maxBytes : n;
    std::string out;
    out.reserve(lim * 2 + (lim < n ? 3 : 0));
    for (std::size_t i = 0; i < lim; ++i) {
        out.push_back(hexd[data[i] >> 4]);
        out.push_back(hexd[data[i] & 0xF]);
    }
    if (lim < n) out += "...";
    return out;
}

std::string hexToBytes(const std::string& hex, Bytes* out) {
    std::string clean;
    clean.reserve(hex.size());
    for (char c : hex) {
        if (std::isspace(static_cast<unsigned char>(c))) continue;
        if (c == ':' || c == '-' || c == ',' || c == '_') continue;
        clean.push_back(c);
    }
    if (clean.size() % 2 != 0) return "нечётное число hex-символов";
    Bytes b;
    b.reserve(clean.size() / 2);
    auto nib = [](char c, int* v) -> bool {
        if (c >= '0' && c <= '9') { *v = c - '0'; return true; }
        if (c >= 'a' && c <= 'f') { *v = c - 'a' + 10; return true; }
        if (c >= 'A' && c <= 'F') { *v = c - 'A' + 10; return true; }
        return false;
    };
    for (std::size_t i = 0; i < clean.size(); i += 2) {
        int hi = 0, lo = 0;
        if (!nib(clean[i], &hi) || !nib(clean[i + 1], &lo))
            return "не-hex символ рядом с позицией " + std::to_string(i);
        b.push_back(static_cast<std::uint8_t>((hi << 4) | lo));
    }
    if (out) *out = std::move(b);
    return std::string();
}

} // namespace wowpe
