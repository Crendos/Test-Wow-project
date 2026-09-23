#include "wow_patch_core.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// ===========================================================================
// Все строки логов — на русском, потому что их читает пользователь вкладки
// «Патч Wow.exe» и консольного патчера. Ядро не знает ни про Qt, ни про файлы:
// оно работает с буфером байт и возвращает текст отчёта.
// ===========================================================================

namespace wowpatch {
namespace {

// ---------------------------------------------------------------------------
// Мелкие утилиты
// ---------------------------------------------------------------------------
std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

std::string upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return char(std::toupper(c)); });
    return s;
}

bool endsWith(const std::string &s, const std::string &suffix) {
    return s.size() >= suffix.size() &&
           std::memcmp(s.data() + (s.size() - suffix.size()), suffix.data(), suffix.size()) == 0;
}

bool contains(const std::string &hay, const std::string &needle) {
    return needle.empty() || hay.find(needle) != std::string::npos;
}

std::string trim(const std::string &s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::string hexs(const Bytes &b, std::size_t maxBytes = 0) {
    return upper(wowpe::toHex(b, maxBytes));
}

Bytes fromHex(const std::string &hex) {
    Bytes out;
    wowpe::hexToBytes(hex, &out);
    return out;
}

Bytes bytesOf(const std::string &s) {
    return Bytes(s.begin(), s.end());
}

Bytes bytesOf(const char *s) {
    const std::size_t n = std::strlen(s);
    return Bytes(s, s + n);
}

std::string hex16(std::uint64_t v) {
    static const char *d = "0123456789ABCDEF";
    std::string out;
    for (int i = 60; i >= 0; i -= 4) out.push_back(d[(v >> i) & 0xF]);
    // убираем ведущие нули, но оставляем хотя бы один разряд
    std::size_t first = out.find_first_not_of('0');
    if (first == std::string::npos) return "0";
    return out.substr(first);
}

template <typename T>
std::string str(T v) { return std::to_string(v); }

std::string mb(std::size_t bytes) {
    const double v = double(bytes) / (1024.0 * 1024.0);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f", v);
    return buf;
}

// ---------------------------------------------------------------------------
// Мини-JSON (свой, чтобы ядро не зависело от Qt). Поддерживает ровно то, что
// нужно рецепту: объект/массив/строка/число/bool/null.
// ---------------------------------------------------------------------------
struct JVal {
    enum class T { Null, Bool, Num, Str, Arr, Obj } type = T::Null;
    bool boolean = false;
    double num = 0.0;
    std::string str;
    std::vector<JVal> arr;
    std::vector<std::pair<std::string, JVal>> obj;

    const JVal *find(const std::string &key) const {
        for (const auto &kv : obj) if (kv.first == key) return &kv.second;
        return nullptr;
    }
    std::string asString(const std::string &def = std::string()) const {
        return type == T::Str ? str : def;
    }
    double asDouble(double def = 0.0) const {
        if (type == T::Num) return num;
        if (type == T::Str && !str.empty()) {
            char *end = nullptr;
            const double v = std::strtod(str.c_str(), &end);
            if (end && end != str.c_str()) return v;
        }
        return def;
    }
    long long asInt(long long def = 0) const { return static_cast<long long>(asDouble(double(def))); }
    bool asBool(bool def = false) const { return type == T::Bool ? boolean : def; }
};

class JParser {
public:
    JParser(const std::string &text) : s_(text) {}

    bool parse(JVal *out, std::string *err) {
        skipWs();
        if (!value(out)) { if (err) *err = err_; return false; }
        skipWs();
        if (p_ != s_.size()) { err_ = "после JSON-значения остались символы"; if (err) *err = err_; return false; }
        return true;
    }

private:
    void skipWs() {
        while (p_ < s_.size() && std::isspace(static_cast<unsigned char>(s_[p_]))) ++p_;
    }
    bool fail(const std::string &m) { err_ = m + " (позиция " + str(p_) + ")"; return false; }

    bool value(JVal *out) {
        skipWs();
        if (p_ >= s_.size()) return fail("неожиданный конец текста");
        const char c = s_[p_];
        if (c == '{') return object(out);
        if (c == '[') return array(out);
        if (c == '"') { out->type = JVal::T::Str; return string(&out->str); }
        if (c == 't' || c == 'f') return boolean(out);
        if (c == 'n') return nullv(out);
        return number(out);
    }

    bool object(JVal *out) {
        out->type = JVal::T::Obj;
        ++p_; // '{'
        skipWs();
        if (p_ < s_.size() && s_[p_] == '}') { ++p_; return true; }
        for (;;) {
            skipWs();
            std::string key;
            if (p_ >= s_.size() || s_[p_] != '"') return fail("в объекте ожидался ключ-строка");
            if (!string(&key)) return false;
            skipWs();
            if (p_ >= s_.size() || s_[p_] != ':') return fail("в объекте ожидалось ':'");
            ++p_;
            JVal v;
            if (!value(&v)) return false;
            out->obj.emplace_back(std::move(key), std::move(v));
            skipWs();
            if (p_ < s_.size() && s_[p_] == ',') { ++p_; continue; }
            if (p_ < s_.size() && s_[p_] == '}') { ++p_; return true; }
            return fail("в объекте ожидалась ',' или '}'");
        }
    }

    bool array(JVal *out) {
        out->type = JVal::T::Arr;
        ++p_; // '['
        skipWs();
        if (p_ < s_.size() && s_[p_] == ']') { ++p_; return true; }
        for (;;) {
            JVal v;
            if (!value(&v)) return false;
            out->arr.push_back(std::move(v));
            skipWs();
            if (p_ < s_.size() && s_[p_] == ',') { ++p_; continue; }
            if (p_ < s_.size() && s_[p_] == ']') { ++p_; return true; }
            return fail("в массиве ожидалась ',' или ']'");
        }
    }

    bool string(std::string *out) {
        ++p_; // открывающая кавычка
        std::string r;
        while (p_ < s_.size()) {
            const char c = s_[p_++];
            if (c == '"') { *out = r; return true; }
            if (c != '\\') { r.push_back(c); continue; }
            if (p_ >= s_.size()) return fail("обрыв строки после '\\'");
            const char e = s_[p_++];
            switch (e) {
            case '"': r.push_back('"'); break;
            case '\\': r.push_back('\\'); break;
            case '/': r.push_back('/'); break;
            case 'b': r.push_back('\b'); break;
            case 'f': r.push_back('\f'); break;
            case 'n': r.push_back('\n'); break;
            case 'r': r.push_back('\r'); break;
            case 't': r.push_back('\t'); break;
            case 'u': {
                if (p_ + 4 > s_.size()) return fail("обрыв \\uXXXX");
                unsigned cp = 0;
                for (int i = 0; i < 4; ++i) {
                    const char h = s_[p_++];
                    cp <<= 4;
                    if (h >= '0' && h <= '9') cp |= unsigned(h - '0');
                    else if (h >= 'a' && h <= 'f') cp |= unsigned(h - 'a' + 10);
                    else if (h >= 'A' && h <= 'F') cp |= unsigned(h - 'A' + 10);
                    else return fail("неверный \\uXXXX");
                }
                // UTF-8 (суррогатные пары не поддерживаем — в рецепте их не бывает)
                if (cp < 0x80) r.push_back(char(cp));
                else if (cp < 0x800) { r.push_back(char(0xC0 | (cp >> 6))); r.push_back(char(0x80 | (cp & 0x3F))); }
                else { r.push_back(char(0xE0 | (cp >> 12))); r.push_back(char(0x80 | ((cp >> 6) & 0x3F)));
                       r.push_back(char(0x80 | (cp & 0x3F))); }
                break;
            }
            default: return fail("неверный экранирующий символ");
            }
        }
        return fail("незакрытая строка");
    }

    bool boolean(JVal *out) {
        if (s_.compare(p_, 4, "true") == 0) { p_ += 4; out->type = JVal::T::Bool; out->boolean = true; return true; }
        if (s_.compare(p_, 5, "false") == 0) { p_ += 5; out->type = JVal::T::Bool; out->boolean = false; return true; }
        return fail("ожидалось true/false");
    }
    bool nullv(JVal *out) {
        if (s_.compare(p_, 4, "null") == 0) { p_ += 4; out->type = JVal::T::Null; return true; }
        return fail("ожидалось null");
    }
    bool number(JVal *out) {
        const std::size_t start = p_;
        if (p_ < s_.size() && (s_[p_] == '-' || s_[p_] == '+')) ++p_;
        while (p_ < s_.size() && (std::isdigit(static_cast<unsigned char>(s_[p_])) || s_[p_] == '.' ||
                                  s_[p_] == 'e' || s_[p_] == 'E' || s_[p_] == '-' || s_[p_] == '+'))
            ++p_;
        if (p_ == start) return fail("ожидалось число");
        const std::string tok = s_.substr(start, p_ - start);
        char *end = nullptr;
        out->num = std::strtod(tok.c_str(), &end);
        if (!end || end == tok.c_str()) return fail("неверное число: " + tok);
        out->type = JVal::T::Num;
        return true;
    }

    const std::string &s_;
    std::size_t p_ = 0;
    std::string err_;
};

std::string jsonEscape(const std::string &s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                static const char *d = "0123456789abcdef";
                out += "\\u00";
                const unsigned char u = static_cast<unsigned char>(c);
                out.push_back(d[(u >> 4) & 0xF]);
                out.push_back(d[u & 0xF]);
            } else out.push_back(c);
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Мини-DER парсер (PKCS#1 / PKCS#8 / SPKI / сертификат X.509)
// ---------------------------------------------------------------------------
struct Der {
    const Bytes &d;
    explicit Der(const Bytes &data) : d(data) {}

    bool readTLV(std::size_t &pos, std::uint8_t &tag, std::size_t &len, std::size_t &content) const {
        if (pos >= d.size()) return false;
        tag = d[pos++];
        if (pos >= d.size()) return false;
        std::uint8_t first = d[pos++];
        if (first < 0x80) {
            len = first;
        } else {
            const int n = first & 0x7F;
            if (n == 0 || n > 4 || pos + std::size_t(n) > d.size()) return false;
            len = 0;
            for (int i = 0; i < n; ++i) len = (len << 8) | d[pos++];
        }
        content = pos;
        return pos + len <= d.size();
    }

    // 06 09 2A 86 48 86 F7 0D 01 01 01 — rsaEncryption
    static bool isRsaOid(const Bytes &b, std::size_t at) {
        static const std::uint8_t oid[] = { 0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x01, 0x01 };
        if (at + 2 + sizeof(oid) > b.size()) return false;
        if (b[at] != 0x06 || b[at + 1] != sizeof(oid)) return false;
        return std::memcmp(b.data() + at + 2, oid, sizeof(oid)) == 0;
    }
    // 06 0A 2B 06 01 04 01 97 55 01 05 01 — id-X25519 (RFC 8410)
    static bool isX25519Oid(const Bytes &b, std::size_t at) {
        static const std::uint8_t oid[] = { 0x2B, 0x06, 0x01, 0x04, 0x01, 0x97, 0x55, 0x01, 0x05, 0x01 };
        if (at + 2 + sizeof(oid) > b.size()) return false;
        if (b[at] != 0x06 || b[at + 1] != sizeof(oid)) return false;
        return std::memcmp(b.data() + at + 2, oid, sizeof(oid)) == 0;
    }
    // 06 03 2B 65 70 — id-Ed25519 (RFC 8410)
    static bool isEd25519Oid(const Bytes &b, std::size_t at) {
        static const std::uint8_t oid[] = { 0x2B, 0x65, 0x70 };
        if (at + 2 + sizeof(oid) > b.size()) return false;
        if (b[at] != 0x06 || b[at + 1] != sizeof(oid)) return false;
        return std::memcmp(b.data() + at + 2, oid, sizeof(oid)) == 0;
    }
};

// Из SPKI/сертификата:
//   SEQUENCE { AlgorithmIdentifier, BIT STRING { SEQUENCE { INTEGER modulus, INTEGER exp } } }
// Важно: readTLV оставляет pos НА НАЧАЛЕ содержимого, поэтому «следующий
// элемент» — это content + len, а не pos.
bool modulusFromSpki(const Bytes &der, std::size_t algStart, Bytes *out) {
    Der d(der);
    std::uint8_t tag = 0;
    std::size_t len = 0, cp = 0, p = algStart;
    if (!d.readTLV(p, tag, len, cp) || tag != 0x30) return false;   // AlgorithmIdentifier
    std::size_t next = cp + len;                                    // BIT STRING
    if (!d.readTLV(next, tag, len, cp) || tag != 0x03 || len < 2) return false;
    std::size_t inner = cp + 1;                 // пропускаем байт «unused bits»
    const std::size_t innerEnd = cp + len;
    if (inner >= innerEnd) return false;
    if (!d.readTLV(inner, tag, len, cp) || tag != 0x30) return false;   // RSAPublicKey
    if (cp + len > innerEnd) return false;
    std::size_t q = cp;
    if (!d.readTLV(q, tag, len, cp) || tag != 0x02 || len == 0) return false; // modulus
    if (cp + len > der.size()) return false;
    out->assign(der.begin() + std::ptrdiff_t(cp), der.begin() + std::ptrdiff_t(cp + len));
    // Модуль в DER кодируется без знака: ведущий 0x00 убираем.
    if (out->size() > 1 && out->front() == 0x00) out->erase(out->begin());
    return !out->empty();
}

// Ищет RSA-модуль в DER-структуре любого поддерживаемого вида.
bool rsaModulusFromDer(const Bytes &der, Bytes *out) {
    Der d(der);
    std::uint8_t tag = 0;
    std::size_t len = 0, cp = 0, p = 0;
    if (!d.readTLV(p, tag, len, cp) || tag != 0x30) return false;   // внешняя SEQUENCE
    const std::size_t seqContent = cp;
    const std::size_t seqEnd = cp + len;
    if (seqEnd > der.size()) return false;

    auto takeInteger = [&](std::size_t content, std::size_t length) -> bool {
        if (content + length > der.size()) return false;
        out->assign(der.begin() + std::ptrdiff_t(content),
                    der.begin() + std::ptrdiff_t(content + length));
        // Модуль в DER кодируется как беззнаковое целое: ведущий 0x00 убираем.
        if (out->size() > 1 && out->front() == 0x00) out->erase(out->begin());
        return !out->empty();
    };

    // 1) PKCS#1. Два вида:
    //    RSAPublicKey  = SEQUENCE { INTEGER modulus, INTEGER exponent }
    //    RSAPrivateKey = SEQUENCE { INTEGER version, INTEGER modulus, INTEGER exponent, ... }
    //    Именно второй — это то, что лежит в TrinityCore
    //    (-----BEGIN RSA PRIVATE KEY----- в AuthenticationPackets.cpp).
    //    Модуль RSA-2048 кодируется длиной 257 (0x82 0x01 0x01), то есть >= 0x80,
    //    а version — одним байтом, поэтому ветки не путаются.
    p = seqContent;
    if (d.readTLV(p, tag, len, cp) && tag == 0x02) {
        if (len >= 0x80) {
            if (takeInteger(cp, len)) return true;            // RSAPublicKey
        } else {
            std::size_t q = cp + len;                         // второй элемент
            std::uint8_t t2 = 0;
            std::size_t l2 = 0, c2 = 0;
            if (d.readTLV(q, t2, l2, c2) && t2 == 0x02 && l2 >= 0x80) {
                if (takeInteger(c2, l2)) return true;         // RSAPrivateKey
            }
        }
    }

    // 2) SPKI / сертификат: ищем OID rsaEncryption и разбираем AlgorithmIdentifier.
    for (std::size_t i = seqContent + 2; i + 11 < der.size() && i < seqEnd; ++i) {
        if (der[i] != 0x06 || !Der::isRsaOid(der, i)) continue;
        if (i < 2) continue;
        Bytes m;
        if (modulusFromSpki(der, i - 2, &m) && !m.empty()) { *out = std::move(m); return true; }
    }

    // 3) PKCS#8 PrivateKeyInfo:
    //    SEQUENCE { INTEGER version, SEQUENCE algId, OCTET STRING { PKCS#1 } }
    p = seqContent;
    if (!d.readTLV(p, tag, len, cp) || tag != 0x02 || len > 4) return false;
    std::size_t q = cp + len;                                   // AlgorithmIdentifier
    if (!d.readTLV(q, tag, len, cp) || tag != 0x30) return false;
    std::size_t r = cp + len;                                   // OCTET STRING
    if (!d.readTLV(r, tag, len, cp) || tag != 0x04) return false;
    if (cp + len > der.size()) return false;
    const Bytes inner(der.begin() + std::ptrdiff_t(cp), der.begin() + std::ptrdiff_t(cp + len));
    Bytes m;
    if (rsaModulusFromDer(inner, &m) && !m.empty()) { *out = std::move(m); return true; }
    return false;
}

bool ed25519PublicFromDer(const Bytes &der, Bytes *out) {
    Der d(der);
    std::uint8_t tag = 0;
    std::size_t len = 0, cp = 0, p = 0;
    if (!d.readTLV(p, tag, len, cp) || tag != 0x30) return false;
    const std::size_t seqContent = cp;
    const std::size_t seqEnd = std::min(cp + len, der.size());

    // SPKI: SEQUENCE { SEQUENCE { OID Ed25519 }, BIT STRING (0x00 + 32 байта) }
    for (std::size_t i = seqContent + 2; i + 5 < der.size() && i < seqEnd; ++i) {
        if (der[i] != 0x06) continue;
        if (!Der::isEd25519Oid(der, i) && !Der::isX25519Oid(der, i)) continue;
        if (i < 2) continue;
        std::size_t q = i - 2;                                  // AlgorithmIdentifier
        std::size_t l2 = 0, c2 = 0;
        if (!d.readTLV(q, tag, l2, c2) || tag != 0x30) continue;
        std::size_t r = c2 + l2;                                // BIT STRING
        if (!d.readTLV(r, tag, l2, c2) || tag != 0x03 || l2 != 33) continue;
        if (c2 + 33 > der.size() || der[c2] != 0x00) continue;
        out->assign(der.begin() + std::ptrdiff_t(c2 + 1), der.begin() + std::ptrdiff_t(c2 + 33));
        return true;
    }
    // PKCS#8 приватный ключ Ed25519 содержит только сид: публичную половину из
    // него без Ed25519-арифметики не вывести, поэтому честно возвращаем false.
    return false;
}

int base64Value(char ch) {
    const unsigned char c = static_cast<unsigned char>(ch);
    if (c >= 'A' && c <= 'Z') return int(c - 'A');
    if (c >= 'a' && c <= 'z') return int(c - 'a') + 26;
    if (c >= '0' && c <= '9') return int(c - '0') + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

// Вырезать PEM-блок и снять base64.
bool pemToDer(const std::string &pemText, Bytes *der, std::string *what) {
    const std::size_t b = pemText.find("-----BEGIN");
    if (b == std::string::npos) return false;
    const std::size_t nl = pemText.find("-----", b + 10);
    if (nl == std::string::npos) return false;
    if (what) *what = trim(pemText.substr(b + 10, nl - (b + 10)));
    const std::size_t bodyStart = pemText.find('\n', nl);
    if (bodyStart == std::string::npos) return false;
    const std::size_t e = pemText.find("-----END", bodyStart);
    const std::string body = pemText.substr(bodyStart, (e == std::string::npos ? pemText.size() : e) - bodyStart);

    der->clear();
    unsigned acc = 0;
    int bits = 0;
    for (char ch : body) {
        const int v = base64Value(ch);
        if (v < 0) continue;
        acc = (acc << 6) | unsigned(v);
        bits += 6;
        if (bits >= 8) { bits -= 8; der->push_back(static_cast<std::uint8_t>((acc >> bits) & 0xFF)); }
    }
    return !der->empty();
}

// ---------------------------------------------------------------------------
// Константы: сигнатуры поиска и значения
// ---------------------------------------------------------------------------
const Bytes &constBytes(const std::string &hex) {
    static std::vector<Bytes> cache;
    static std::vector<std::string> keys;
    for (std::size_t i = 0; i < keys.size(); ++i)
        if (keys[i] == hex) return cache[i];
    keys.push_back(hex);
    cache.push_back(fromHex(hex));
    return cache.back();
}

// Сигнатуры (первые 8 байт родных ключей Blizzard — сверено с wow-patcher).
const Bytes &patConnectTo()      { return constBytes("91D59BB7D4E183A5"); }
const Bytes &patSignatureMod()   { return constBytes("35FF17E733C4D3D4"); }
const Bytes &patGameCryptoRsa()  { return constBytes("71FDFA60140DF205"); }
const Bytes &patGameCryptoEd()   { return constBytes("15D618BD7DB577BD"); }

// Слот встроенного cert-bundle в .rdata (как в wow-patcher: 32761 из 32768).
constexpr std::size_t kCertBundleSlot = 32761;
// Окно доменной части суффикса: длина "battle.net".
constexpr std::size_t kPortalDomainWindow = 10;

const std::string kUrlV1    = "http://%s.patch.battle.net:1119/%s/versions";
const std::string kUrlV2    = "https://%s.version.battle.net/v2/products/%s/versions";
const std::string kUrlV2New = "https://%s.version.battle.net/v2/products/%s/%s";
const std::string kCdnsUrl  = "http://%s.patch.battle.net:1119/%s/cdns";

// Диапазоны, уже занятые применёнными патчами (чтобы два шага не перезаписали
// друг друга — например, ключи и рецепт могут указать на одно место).
bool overlaps(const UsedRange &r, std::int64_t from, std::int64_t to) { return from < r.to && r.from < to; }

std::string kindName(wowpe::Locate::Kind k) {
    using K = wowpe::Locate::Kind;
    switch (k) {
    case K::Match: return "совпадение";
    case K::AlreadyApplied: return "уже применён";
    case K::Relocated: return "перенесён по контексту";
    case K::Ambiguous: return "неоднозначно";
    default: return "не найдено";
    }
}

} // namespace

// ===========================================================================
// Ключи TrinityCore
// ===========================================================================
const Bytes &trinityRsaModulusLe() {
    static const Bytes v = fromHex(
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
    return v;
}

const Bytes &trinityRsaModulusBe() {
    static const Bytes v = fromHex(
        "EEB3DCD4D3C3B45451CE665BCB32B8F0"
        "F79253C619F20C852F8A26A97A459F60"
        "C4EBCDEA7F8D59D857B2607B094C9B68"
        "B8C7EDEF1E800DE66B375B5390EB1813"
        "0D7F436483DA98E6ACC230A282A5C6CB"
        "C7FB869F9FA9026A0349C538FBC0C855"
        "CCC0CE2591BE85CFD1D137CECC83D2EA"
        "3080077B809F9D44542229BE86DADB48"
        "C5A9F91336952376F10EDC840D940212"
        "A897F33B14EEAA6F9805274E1FA360A5"
        "A9DAD817DF33CBE213548B18B0CAB9BB"
        "886406DF75A6D76100BBB05A0E7AD477"
        "084D15E21083B004AA9E8B77A906895D"
        "085D0FB82E6BC1CB64CF6E5CDB4F5865"
        "0851FB0D481A6FB63D1F0BDDFE1B1DF0"
        "BFB0276BF58EBCC74001FFA70B80D65F");
    return v;
}

const Bytes &trinityEd25519PublicKey() {
    static const Bytes v = fromHex(
        "02596F0D0C061A8B30745988FD72C59E"
        "29EC367FB0F341F28E0F08D037BAFC69");
    return v;
}

const Bytes &trinityEd25519Seed() {
    static const Bytes v = fromHex(
        "08BDC7A3CCC34F3F6A0BFFCF31C1B697"
        "691E729A0AAB2C77C36F8AE75A9AA7C9");
    return v;
}

const Bytes &blizzardRsaSignature()        { return patConnectTo(); }
const Bytes &blizzardRsaSignatureBe() {
    static const Bytes v = [] {
        Bytes b = patConnectTo();
        std::reverse(b.begin(), b.end());
        return b;
    }();
    return v;
}
const Bytes &blizzardSignatureModulusSig() { return patSignatureMod(); }
const Bytes &blizzardEd25519Signature()    { return patGameCryptoEd(); }

const Bytes &portalSuffixPattern() {
    static const Bytes v = bytesOf(".actual.battle.net");
    return v;
}
const Bytes &launcherLoginPattern() {
    static const Bytes v = bytesOf("Software\\Blizzard Entertainment\\Battle.net\\Launch Options\\");
    return v;
}
const Bytes &certBundleUrlPattern() {
    static const Bytes v = bytesOf("http://nydus.battle.net/Bnet/zxx/client/bgs-key-fingerprint");
    return v;
}

namespace {
const Bytes &launcherLoginReplacement() {
    static const Bytes v = bytesOf("Software\\Arctium WoW Launcher\\Battle.net\\Launch Options\\");
    return v;
}
const Bytes &certBundleEnvelope() {
    static const Bytes v = bytesOf("{\"Created\":");
    return v;
}
} // namespace

bool keysSelfTest(std::vector<std::string> *log) {
    auto L = [&](const std::string &s) { if (log) log->push_back(s); };
    bool ok = true;

    const Bytes &le = trinityRsaModulusLe();
    const Bytes &be = trinityRsaModulusBe();
    L("=== САМОПРОВЕРКА КЛЮЧЕЙ ===");
    if (le.size() != 256) { L("[!!] RSA-модуль LE: длина " + str(le.size()) + ", ожидалось 256"); ok = false; }
    else L("[OK] RSA-модуль LE: 256 байт");

    Bytes rev(be.begin(), be.end());
    std::reverse(rev.begin(), rev.end());
    if (rev == le) L("[OK] LE-модуль = разворот BE-модуля ключа TrinityCore ConnectToRSA");
    else { L("[!!] LE-модуль НЕ равен развороту BE-модуля — константы рассинхронизированы"); ok = false; }

    if (be.size() == 256 && be.front() >= 0x80)
        L("[OK] BE-модуль начинается с 0x" + hexs(Bytes(1, be.front())) + " — старший бит установлен (нормально для RSA-2048)");
    else { L("[!!] BE-модуль подозрительный: размер " + str(be.size())); ok = false; }

    const Bytes &ed = trinityEd25519PublicKey();
    if (ed.size() == 32) L("[OK] Ed25519 публичный ключ: 32 байта");
    else { L("[!!] Ed25519 публичный ключ: длина " + str(ed.size()) + ", ожидалось 32"); ok = false; }

    const Bytes &seed = trinityEd25519Seed();
    if (seed.size() == 32) L("[OK] Ed25519 сид TrinityCore: 32 байта (EnterEncryptedModePrivateKey)");
    else { L("[!!] Ed25519 сид: длина " + str(seed.size())); ok = false; }

    L("Первые 8 байт родного ключа Blizzard (сигнатура поиска ConnectTo): " + hexs(patConnectTo()));
    L("Первые 8 байт ключа TrinityCore (что ищем, чтобы понять «уже пропатчен»): " + hexs(le, 8));
    if (patConnectTo() == Bytes(le.begin(), le.begin() + 8)) {
        L("[!!] Сигнатура Blizzard совпала с началом ключа TrinityCore — поиск будет находить уже пропатченные файлы");
        ok = false;
    } else {
        L("[OK] Сигнатуры Blizzard и TrinityCore различаются — поиск корректно отличает родной ключ от подставленного");
    }
    L(ok ? "ИТОГ: встроенные ключи корректны." : "ИТОГ: ЕСТЬ ПРОБЛЕМЫ — см. строки [!!].");
    return ok;
}

// ===========================================================================
// Версия / профиль
// ===========================================================================
bool parseVersion(const std::string &text, int *a, int *b, int *c, int *build) {
    int parts[4] = { 0, 0, 0, 0 };
    int n = 0;
    std::string cur;
    for (char ch : text) {
        if (std::isdigit(static_cast<unsigned char>(ch))) { cur.push_back(ch); continue; }
        if (!cur.empty()) { if (n < 4) parts[n++] = std::atoi(cur.c_str()); cur.clear(); }
    }
    if (!cur.empty() && n < 4) parts[n++] = std::atoi(cur.c_str());
    if (n < 2) return false;
    if (a) *a = parts[0];
    if (b) *b = parts[1];
    if (c) *c = parts[2];
    if (build) *build = parts[3];
    return true;
}

int buildFromVersion(const std::string &version) {
    int a = 0, b = 0, c = 0, build = 0;
    if (!parseVersion(version, &a, &b, &c, &build)) return 0;
    return build;
}

Detection detectProfile(const Bytes &data, const wowpe::Image &img,
                        const std::string &version, const std::string &pathHint) {
    Detection d;
    d.branch = "Unknown";
    d.profile = "modern";
    d.version = version;
    d.build = buildFromVersion(version);

    const std::string lp = lower(pathHint);
    if (contains(lp, "_retail_"))                       d.branch = "Retail";
    else if (contains(lp, "_classic_titan_"))           d.branch = "Titan";
    else if (contains(lp, "_anniversary_"))             d.branch = "Anniversary";
    else if (contains(lp, "_classic_era_"))             d.branch = "Classic Era";
    else if (contains(lp, "_classic_") || contains(lp, "wowclassic")) d.branch = "Classic";
    else if (endsWith(lp, "wow.exe") || endsWith(lp, "wow-64.exe"))   d.branch = "Client";

    int major = 0, minor = 0;
    parseVersion(d.version, &major, &minor, nullptr, nullptr);

    const bool classicFamily = d.branch == "Classic" || d.branch == "Classic Era" ||
                               d.branch == "Anniversary" || d.branch == "Titan";
    const bool retail = d.branch == "Retail" || d.branch == "Client" || d.branch == "Unknown";

    if (classicFamily) {
        if (major == 1 && minor <= 13) {
            d.profile = "classic13";
            d.legacyCert = false;
            d.usesEd25519 = false;
            d.note = "Classic 1.13.x: только ConnectTo RSA + portal из Config.wtf. Обход сертификата не применяется.";
        } else if (major == 1) {
            // Classic Era 1.14+/1.15.x собраны на современном движке: там те же
            // ConnectTo RSA и Ed25519, что в ретейле 9.x+.
            d.profile = "classic_modern";
            d.legacyCert = false;
            d.usesEd25519 = true;
            d.note = "Classic Era 1.14+/1.15: современный движок — ConnectTo RSA + Ed25519 + SET portal.";
        } else {
            d.profile = "legacy";
            d.legacyCert = true;
            d.usesEd25519 = major >= 3;
            d.note = "Legacy-профиль (2.5.x / 3.4.x / 4.4.x): Signature/Crypto RSA + runtime-обход проверки сертификата.";
        }
    } else if (retail) {
        if (major >= 6 && major <= 8) {
            d.profile = "legion";
            d.legacyCert = true;
            d.usesEd25519 = false;
            d.note = "Legion/BfA 6.x-8.x: ConnectTo RSA + SET portal в Config.wtf.";
        } else if (major == 9 || major == 10) {
            d.profile = "legacy";
            d.legacyCert = true;
            d.usesEd25519 = true;
            d.note = "Retail 9.x-10.x: legacy-профиль (Signature/Crypto RSA + обход сертификата).";
        } else if (major >= 11) {
            // 11.x и 12.x (включая 12.1.0.69497): статического патча данных в
            // .rdata достаточно — именно так сделан Firestorm.exe.
            d.profile = major >= 12 ? "modern12" : "modern";
            d.legacyCert = false;
            d.usesEd25519 = true;
            d.note = major >= 12
                ? "Retail 12.x: статический патч на диске достаточен (ConnectTo RSA + Ed25519 + SET portal)."
                : "Retail 11.x: статический патч на диске достаточен (ConnectTo RSA + Ed25519 + SET portal).";
        } else {
            d.profile = "modern";
            d.legacyCert = false;
            d.usesEd25519 = true;
            d.note = "Версия не определена — применяем современный набор (RSA + Ed25519 + портал).";
        }
    } else {
        d.profile = "modern";
        d.note = "Неизвестная ветка — применяем современный набор сигнатур.";
    }

    // Уточнение по содержимому: если в файле уже стоит ключ TrinityCore, значит
    // клиент уже пропатчен (это важно для режима «рецепт» и для диагностики).
    if (img.valid && !data.empty()) {
        bool trinityThere = wowpe::chooseSite(
            data, img, wowpe::patternFromBytes(trinityRsaModulusLe().data(), 8), /*requirePatchable=*/true).found;
        trinityThere = trinityThere || wowpe::chooseSite(
            data, img, wowpe::patternFromBytes(trinityRsaModulusBe().data(), 8), /*requirePatchable=*/true).found;
        bool blizzardThere = wowpe::chooseSite(
            data, img, wowpe::patternFromBytes(patConnectTo().data(), patConnectTo().size()),
            /*requirePatchable=*/true).found;
        blizzardThere = blizzardThere || wowpe::chooseSite(
            data, img, wowpe::patternFromBytes(blizzardRsaSignatureBe().data(), blizzardRsaSignatureBe().size()),
            /*requirePatchable=*/true).found;
        const bool blizzardEdThere = wowpe::chooseSite(
            data, img, wowpe::patternFromBytes(patGameCryptoEd().data(), patGameCryptoEd().size()),
            /*requirePatchable=*/true).found;
        if (trinityThere && !blizzardThere)
            d.note += " Файл УЖЕ пропатчен (стоит ключ TrinityCore).";
        else if (!trinityThere && !blizzardThere)
            d.note += blizzardEdThere
                ? " ConnectTo RSA на диске не видна (ни LE, ни BE — вероятна обфускация ключа), "
                  "но Ed25519 Blizzard найдена: образ подлинный."
                : " Родной ключ Blizzard не найден — возможно, это не Wow.exe или билд неизвестен.";
    }
    d.standaloneDiskPatchOk = true;
    return d;
}

// ===========================================================================
// Портал
// ===========================================================================
std::string portalHostOnly(const std::string &portal) {
    std::string p = trim(portal);
    // "host:port" -> "host"; IPv6 в скобках не трогаем
    if (!p.empty() && p.front() == '[') {
        const std::size_t close = p.find(']');
        if (close != std::string::npos) return p.substr(0, close + 1);
    }
    const std::size_t colon = p.rfind(':');
    if (colon != std::string::npos && p.find(':') == colon) return p.substr(0, colon);
    return p;
}

bool looksLikeIp(const std::string &host) {
    if (host.empty()) return false;
    if (host.front() == '[' || host.find(':') != std::string::npos) return true; // IPv6
    int dots = 0;
    for (char c : host) {
        if (c == '.') { ++dots; continue; }
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    }
    return dots == 3;
}

std::string derivePortalDomain(const std::string &portal, std::string *note) {
    const std::string host = portalHostOnly(portal);
    if (host.empty()) {
        if (note) *note = "портал пустой — суффикс не правим, адрес возьмётся из SET portal";
        return std::string();
    }
    if (looksLikeIp(host)) {
        if (note) *note = "адрес «" + host + "» — это IP, а слот суффикса рассчитан на домен "
                          "(клиент склеит «eu» + суффикс). Суффикс не правим: адрес задаётся "
                          "строкой SET portal в Config.wtf.";
        return std::string();
    }
    // Берём регистрируемый домен: последние два ярлыка ("logon.example.com" -> "example.com").
    std::vector<std::string> labels;
    std::string cur;
    for (char c : host) {
        if (c == '.') { labels.push_back(cur); cur.clear(); }
        else cur.push_back(c);
    }
    if (!cur.empty()) labels.push_back(cur);
    std::string dom;
    if (labels.size() >= 2) dom = labels[labels.size() - 2] + "." + labels.back();
    else dom = host;
    dom = lower(dom);
    if (dom.size() > kPortalDomainWindow) {
        if (note) *note = "домен «" + dom + "» длиннее " + str(kPortalDomainWindow) +
                          " байт (столько занимает «battle.net») — в слот не влезает, "
                          "суффикс не правим. Адрес остаётся в SET portal.";
        return std::string();
    }
    if (note) *note = "домен «" + dom + "» из портала «" + portal + "» (" + str(dom.size()) +
                      " байт, влезает в окно суффикса)";
    return dom;
}

std::string normalizePortalHost(const std::string &portal) {
    std::string p = trim(portal);
    // Убираем scheme и путь, если пользователь вставил URL целиком.
    const std::size_t scheme = p.find("://");
    if (scheme != std::string::npos) p = p.substr(scheme + 3);
    const std::size_t slash = p.find('/');
    if (slash != std::string::npos) p = p.substr(0, slash);
    while (!p.empty() && (p.back() == '.' || p.back() == ':')) p.pop_back();
    return p;
}

Bytes portalSuffixValue(const std::string &portal, std::size_t windowSize) {
    std::string note;
    std::string domain = derivePortalDomain(portal, &note);
    if (domain.empty()) return Bytes();
    Bytes v = bytesOf(".actual.");
    const Bytes d = bytesOf(domain);
    v.insert(v.end(), d.begin(), d.end());
    if (v.size() > windowSize) return Bytes();
    v.resize(windowSize, 0x00);
    return v;
}

Bytes portalWholeSlotValue(const std::string &portal, int port, std::size_t windowSize) {
    std::string p = normalizePortalHost(portal);
    if (p.empty()) return Bytes();
    if (p.find(':') == std::string::npos && port > 0) p += ":" + str(port);
    const Bytes v = bytesOf(p);
    if (v.size() + 1 > windowSize) return Bytes();   // нужен место под NUL
    Bytes out = v;
    out.resize(windowSize, 0x00);
    return out;
}

// ===========================================================================
// Config.wtf
// ===========================================================================
std::string configPortal(const std::string &text) {
    // SET   portal   "value"  — регистр не важен
    std::string low = lower(text);
    std::size_t pos = 0;
    while ((pos = low.find("portal", pos)) != std::string::npos) {
        std::size_t i = pos + 6;
        while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
        if (i < text.size() && text[i] == '=') ++i;      // вариант "SET portal = ..."
        while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
        if (i < text.size() && text[i] == '"') {
            const std::size_t end = text.find('"', i + 1);
            if (end != std::string::npos) return trim(text.substr(i + 1, end - i - 1));
        }
        pos += 6;
    }
    return std::string();
}

bool writePortalIntoConfig(std::string &text, const std::string &portal) {
    const std::string line = "SET portal \"" + portal + "\"";
    std::string low = lower(text);
    std::size_t pos = 0;
    while ((pos = low.find("portal", pos)) != std::string::npos) {
        // проверяем, что перед «portal» стоит SET (или начало строки)
        std::size_t s = pos;
        while (s > 0 && (text[s - 1] == ' ' || text[s - 1] == '\t')) --s;
        const bool hasSet = s >= 3 && lower(text.substr(s - 3, 3)) == "set";
        std::size_t i = pos + 6;
        while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
        if (i < text.size() && text[i] == '=') ++i;
        while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
        if (i < text.size() && text[i] == '"') {
            const std::size_t q1 = i;
            const std::size_t q2 = text.find('"', q1 + 1);
            if (q2 != std::string::npos && hasSet) {
                const std::string old = text.substr(q1 + 1, q2 - q1 - 1);
                if (old == portal) return false;
                text.replace(q1, q2 - q1 + 1, "\"" + portal + "\"");
                return true;
            }
        }
        pos += 6;
    }
    if (!text.empty() && text.back() != '\n') text.push_back('\n');
    text += line + "\n";
    return true;
}

// ===========================================================================
// Ключи из PEM
// ===========================================================================
Bytes rsaModulusLeFromPem(const std::string &pemText, std::string *error) {
    Bytes der;
    std::string what;
    if (!pemToDer(pemText, &der, &what)) {
        if (error) *error = "в файле нет PEM-блока -----BEGIN ... -----";
        return Bytes();
    }
    Bytes modulus;
    if (!rsaModulusFromDer(der, &modulus) || modulus.empty()) {
        if (error) *error = "в PEM (" + what + ") не найден RSA-модуль "
                            "(поддерживаются PKCS#1, PKCS#8, SPKI и сертификат X.509)";
        return Bytes();
    }
    // Клиент хранит модуль в little-endian — разворачиваем.
    std::reverse(modulus.begin(), modulus.end());
    if (modulus.size() < 256) modulus.resize(256, 0x00);
    else if (modulus.size() > 256) modulus.resize(256);
    return modulus;
}

Bytes ed25519PublicFromPem(const std::string &pemText, std::string *error) {
    Bytes der;
    std::string what;
    if (!pemToDer(pemText, &der, &what)) {
        if (error) *error = "в файле нет PEM-блока -----BEGIN ... -----";
        return Bytes();
    }
    Bytes pub;
    if (!ed25519PublicFromDer(der, &pub) || pub.size() != 32) {
        if (error) *error = "в PEM (" + what + ") не найден публичный ключ Ed25519 (32 байта). "
                            "Нужен PUBLIC KEY или сертификат; из приватного ключа публичная "
                            "половина без Ed25519-арифметики не выводится.";
        return Bytes();
    }
    return pub;
}

Bytes normalizeKeyOrder(const Bytes &key, bool assumeBigEndian, std::string *note) {
    if (!assumeBigEndian) {
        if (note) *note = "порядок байт оставлен как введён (little-endian, как хранит клиент)";
        return key;
    }
    Bytes out(key.begin(), key.end());
    std::reverse(out.begin(), out.end());
    if (note) *note = "введённый ключ развёрнут из big-endian (формат openssl) в little-endian";
    return out;
}

// ===========================================================================
// План патча
// ===========================================================================
std::vector<Probe> knownProbes() {
    return {
        { "ConnectTo RsaModulus (Blizzard)", patConnectTo(), 256 },
        { "ConnectTo RsaModulus (Blizzard, BE-порядок)", blizzardRsaSignatureBe(), 256 },
        { "Ключ TrinityCore (уже пропатчен)", Bytes(trinityRsaModulusLe().begin(), trinityRsaModulusLe().begin() + 8), 256 },
        { "Ключ TrinityCore (уже пропатчен, BE)", Bytes(trinityRsaModulusBe().begin(), trinityRsaModulusBe().begin() + 8), 256 },
        { "Signature RsaModulus (legacy)", patSignatureMod(), 256 },
        { "GameCrypto RsaModulus (legacy)", patGameCryptoRsa(), 256 },
        { "GameCrypto Ed25519 (Blizzard)", patGameCryptoEd(), 32 },
        { "Ed25519 TrinityCore (уже пропатчен)", Bytes(trinityEd25519PublicKey().begin(), trinityEd25519PublicKey().begin() + 8), 32 },
        { "Портал-суффикс .actual.battle.net", portalSuffixPattern(), 18 },
        { "Ключ реестра лаунчера", launcherLoginPattern(), 0 },
        { "Cert bundle URL", certBundleUrlPattern(), 0 },
        { "Cert bundle envelope", certBundleEnvelope(), kCertBundleSlot },
    };
}

std::vector<Step> buildPlan(const Options &opts, const Detection &det,
                            const wowpe::Image &img, const Bytes &data,
                            std::vector<std::string> *log) {
    auto L = [&](const std::string &s) { if (log) log->push_back(s); };
    std::vector<Step> steps;
    if (!opts.applySignaturePatches) {
        L("Сигнатурные патчи отключены — применяем только hunks из рецепта.");
        return steps;
    }

    // --- ключи ---
    Bytes rsa = opts.rsaModulus.empty() ? trinityRsaModulusLe() : opts.rsaModulus;
    Bytes ed  = opts.ed25519Key.empty() ? trinityEd25519PublicKey() : opts.ed25519Key;
    std::string note;
    rsa = normalizeKeyOrder(rsa, opts.keysAssumeBigEndian, &note);
    if (opts.keysAssumeBigEndian) L("RSA-модуль: " + note + ". Первые байты: " + hexs(rsa, 8));
    if (rsa.size() != 256) {
        L("!! RSA-модуль " + str(rsa.size()) + " байт вместо 256 — патч RSA пропущен.");
        rsa.clear();
    }
    if (ed.size() != 32) {
        L("!! Ed25519-ключ " + str(ed.size()) + " байт вместо 32 — патч Ed25519 пропущен.");
        ed.clear();
    }

    if (!rsa.empty()) {
        // Как лежит модуль в ЭТОМ билде: обычный порядок (LE, LSB первым) или
        // разворот (BE, MSB первым — «openssl-порядок»). Патчим в том же виде,
        // в каком нашли родной ключ.
        const bool leThere = wowpe::chooseSite(
            data, img, wowpe::patternFromBytes(patConnectTo().data(), patConnectTo().size()),
            /*requirePatchable=*/true).found;
        const bool beThere = wowpe::chooseSite(
            data, img, wowpe::patternFromBytes(blizzardRsaSignatureBe().data(), blizzardRsaSignatureBe().size()),
            /*requirePatchable=*/true).found;
        if (beThere && !leThere) {
            const Bytes rsaBe(rsa.rbegin(), rsa.rend());
            steps.push_back({ "rsa.connectto", "ConnectTo RsaModulus (BE)",
                              wowpe::patternFromBytes(blizzardRsaSignatureBe().data(), blizzardRsaSignatureBe().size()),
                              rsaBe, 0, true,
                              "ключ TrinityCore ConnectToRSA (big-endian, 256 байт)" });
            L("RSA-модуль: родной ключ найден в BE-порядке — патч будет записан разворотом (BE).");
        } else {
            // Особый случай: родного ключа нет, но BE-вариант TrinityCore уже
            // стоит (повторный запуск патча на BE-клиенте) — план берём BE,
            // чтобы applyStep увидел «значение уже стоит».
            const bool tcBeThere = !leThere && wowpe::chooseSite(
                data, img, wowpe::patternFromBytes(trinityRsaModulusBe().data(), 8),
                /*requirePatchable=*/true).found;
            if (tcBeThere) {
                const Bytes rsaBe(rsa.rbegin(), rsa.rend());
                steps.push_back({ "rsa.connectto", "ConnectTo RsaModulus (BE)",
                                  wowpe::patternFromBytes(blizzardRsaSignatureBe().data(), blizzardRsaSignatureBe().size()),
                                  rsaBe, 0, true,
                                  "ключ TrinityCore ConnectToRSA (big-endian, 256 байт)" });
            } else {
                steps.push_back({ "rsa.connectto", "ConnectTo RsaModulus",
                                  wowpe::patternFromBytes(patConnectTo().data(), patConnectTo().size()),
                                  rsa, 0, true, "ключ TrinityCore ConnectToRSA (LE, 256 байт)" });
            }
        }
    }

    const bool needEd = det.usesEd25519 || !opts.autoDetect;
    if (!ed.empty() && needEd)
        steps.push_back({ "ed25519", "GameCrypto Ed25519",
                          wowpe::patternFromBytes(patGameCryptoEd().data(), patGameCryptoEd().size()),
                          ed, 0, opts.requireEd25519 && needEd,
                          "публичная половина EnterEncryptedModePrivateKey" });

    const bool legacy = opts.autoDetect ? det.legacyCert : opts.patchLegacyGameCryptoRsa;
    if (!rsa.empty() && legacy) {
        steps.push_back({ "rsa.signature", "Signature RsaModulus (legacy)",
                          wowpe::patternFromBytes(patSignatureMod().data(), patSignatureMod().size()),
                          rsa, 0, false, "" });
        steps.push_back({ "rsa.gamecrypto", "GameCrypto RsaModulus (legacy)",
                          wowpe::patternFromBytes(patGameCryptoRsa().data(), patGameCryptoRsa().size()),
                          rsa, 0, false, "" });
    }

    // --- портал ---
    // Правильный статический патч — суффикс `.actual.<домен>`: клиент склеивает
    // префикс региона ("eu") с этим суффиксом, поэтому писать сюда
    // "127.0.0.1:1119" целиком нельзя (получится "eu127.0.0.1:1119").
    if (opts.patchPortalSuffix) {
        std::string dnote;
        std::string domain = trim(opts.portalDomain);
        if (domain.empty()) domain = derivePortalDomain(opts.portal, &dnote);
        else dnote = "домен задан вручную";
        if (!domain.empty() && domain.front() == '.') domain.erase(domain.begin());
        if (domain.size() > kPortalDomainWindow) domain.clear();
        if (domain.empty() && !opts.portalFallbackDomain.empty() &&
            opts.portalFallbackDomain.size() <= kPortalDomainWindow) {
            domain = lower(opts.portalFallbackDomain);
            if (!dnote.empty()) L("  [i] Портал-суффикс: " + dnote);
            L("  [i] Ставим запасной домен «" + domain + "»: клиент даже с пустым Config.wtf не уйдёт "
              "на Blizzard, а реальный адрес возьмёт из SET portal.");
            dnote.clear();
        }
        if (domain.empty()) {
            if (!dnote.empty()) L("  [i] Портал-суффикс: " + dnote);
        } else if (domain.size() > kPortalDomainWindow) {
            L("  ! Домен «" + domain + "» длиннее " + str(kPortalDomainWindow) +
              " байт — суффикс портала не правим, адрес останется в SET portal.");
        } else {
            Bytes value = bytesOf(".actual.");
            const Bytes db = bytesOf(domain);
            value.insert(value.end(), db.begin(), db.end());
            value.resize(portalSuffixPattern().size(), 0x00);
            steps.push_back({ "portal.suffix", "Портал-суффикс -> .actual." + domain,
                              wowpe::patternFromBytes(portalSuffixPattern().data(), portalSuffixPattern().size()),
                              value, int(portalSuffixPattern().size()), false, "" });
            if (!dnote.empty()) L("  [i] Портал-суффикс: " + dnote);
        }
    }
    if (opts.patchPortalWholeSlot) {
        const std::size_t window = portalSuffixPattern().size() + 1;   // + NUL
        Bytes pattern = portalSuffixPattern();
        pattern.push_back(0x00);
        const Bytes value = portalWholeSlotValue(opts.portal, opts.port, window);
        if (value.empty()) {
            L("  ! Портал «" + opts.portal + "» не влезает в окно " + str(window) + " байт — пропуск.");
        } else {
            L("  [!] Включён legacy-режим «портал во всё окно»: клиент склеивает префикс региона "
              "с этой строкой, результат может быть нерабочим. Надёжный способ — SET portal в Config.wtf.");
            steps.push_back({ "portal.whole", "Login Portal (всё окно)",
                              wowpe::patternFromBytes(pattern.data(), pattern.size()),
                              value, int(window), false, "" });
        }
    }
    if (opts.patchLauncherRegistry)
        steps.push_back({ "registry.launcher", "Launcher Login Registry",
                          wowpe::patternFromBytes(launcherLoginPattern().data(), launcherLoginPattern().size()),
                          launcherLoginReplacement(), int(launcherLoginPattern().size()), false, "" });

    if (opts.patchVersionUrls) {
        const std::string v1  = opts.versionUrl.empty() ? kUrlV1    : opts.versionUrl;
        const std::string v2  = opts.versionUrl.empty() ? kUrlV2    : opts.versionUrl;
        const std::string v2n = opts.versionUrl.empty() ? kUrlV2New : opts.versionUrl;
        const std::string cd  = opts.cdnsUrl.empty()    ? kCdnsUrl  : opts.cdnsUrl;
        auto mk = [](const std::string &id, const std::string &label,
                     const std::string &find, const std::string &repl) {
            Bytes p = bytesOf(find); p.push_back(0x00);
            Bytes v = bytesOf(repl); v.resize(p.size(), 0x00);
            return Step{ id, label, wowpe::patternFromBytes(p.data(), p.size()), v, int(p.size()), false, "" };
        };
        steps.push_back(mk("url.version1", "Version URL v1", kUrlV1, v1));
        steps.push_back(mk("url.version2", "Version URL v2", kUrlV2, v2));
        steps.push_back(mk("url.version3", "Version URL v3 (unified)", kUrlV2New, v2n));
        steps.push_back(mk("url.cdns", "CDNs URL", kCdnsUrl, cd));
    }
    if (!opts.certBundleUrl.empty()) {
        const Bytes u = bytesOf(opts.certBundleUrl);
        if (u.size() > certBundleUrlPattern().size())
            L("  ! Cert bundle URL длиннее " + str(certBundleUrlPattern().size()) + " байт — пропуск");
        else {
            Bytes v = u;
            v.resize(certBundleUrlPattern().size(), 0x00);
            steps.push_back({ "url.certbundle", "Cert bundle URL",
                              wowpe::patternFromBytes(certBundleUrlPattern().data(), certBundleUrlPattern().size()),
                              v, int(certBundleUrlPattern().size()), false, "" });
        }
    }
    if (!opts.certBundle.empty()) {
        const Bytes &env = certBundleEnvelope();
        if (opts.certBundle.size() < env.size() ||
            std::memcmp(opts.certBundle.data(), env.data(), env.size()) != 0) {
            L("  ! Cert bundle: файл должен начинаться с {\"Created\": — это не подписанный bundle");
        } else if (opts.certBundle.size() > kCertBundleSlot) {
            L("  ! Cert bundle: " + str(opts.certBundle.size()) + " байт, слот всего " + str(kCertBundleSlot));
        } else {
            steps.push_back({ "certbundle.inject",
                              "Cert bundle (" + str(opts.certBundle.size()) + " байт в слот " + str(kCertBundleSlot) + ")",
                              wowpe::patternFromBytes(env.data(), env.size()),
                              opts.certBundle, int(kCertBundleSlot), false, "" });
        }
    }

    // Диапазон data не используется напрямую (план строится по сигнатурам), но
    // оставлен в интерфейсе: рецепты ищутся по образу на этапе применения.
    (void)data;
    return steps;
}

// ===========================================================================
// Применение
// ===========================================================================
StepResult applyStep(Bytes &data, const wowpe::Image &img, const Step &st,
                     Record *rec, std::vector<UsedRange> *used,
                     std::vector<std::string> *log) {
    auto L = [&](const std::string &s) { if (log) log->push_back(s); };
    if (rec) { rec->id = st.id; rec->label = st.label; rec->mandatory = st.mandatory; }

    const wowpe::Site site = wowpe::chooseSite(data, img, st.pattern, /*requirePatchable=*/true);
    if (!site.found) {
        // Сигнатуры (родного ключа Blizzard) уже нет. Прежде чем объявлять
        // провал, проверим: может быть, нужное значение УЖЕ стоит в секции
        // данных — тогда патч просто уже применён (повторный запуск, или его
        // закрыл hunk рецепта).
        if (!st.value.empty()) {
            const std::size_t probe = std::min<std::size_t>(st.value.size(), 16);
            const wowpe::Site vs = wowpe::chooseSite(
                data, img, wowpe::patternFromBytes(st.value.data(), probe), /*requirePatchable=*/true);
            if (vs.found && vs.offset + st.value.size() <= data.size() &&
                std::memcmp(data.data() + vs.offset, st.value.data(), st.value.size()) == 0) {
                if (used) used->push_back({ std::int64_t(vs.offset),
                                            std::int64_t(vs.offset + st.value.size()), st.label });
                if (rec) {
                    rec->offset = std::int64_t(vs.offset);
                    rec->section = vs.section;
                    rec->size = st.value.size();
                    rec->applied = true;
                    rec->note = "уже применён";
                    rec->expected = st.value;
                }
                L("  [=] " + st.label + " @ 0x" + hex16(vs.offset) + " (" + vs.section +
                  "): значение уже стоит — патч применять не нужно");
                return StepResult::AlreadyApplied;
            }
        }
        if (rec) { rec->applied = false; rec->note = site.note; }
        L("  [-] " + st.label + ": " + site.note);
        if (st.mandatory)
            L("      ^ ОБЯЗАТЕЛЬНЫЙ патч не найден. Без него клиент не пустит на сервер.");
        return StepResult::NotFound;
    }

    Bytes payload = st.value;
    const std::size_t slot = std::max<std::size_t>(std::size_t(st.slotSize), st.pattern.size());
    if (payload.size() < slot) payload.resize(slot, 0x00);

    // Место могло быть занято предыдущим шагом (например, hunk из рецепта уже
    // закрыл тот же ключ). Перезаписывать нельзя: второй патч может быть короче.
    if (used) {
        for (const UsedRange &g : *used) {
            if (overlaps(g, std::int64_t(site.offset), std::int64_t(site.offset + payload.size()))) {
                if (rec) {
                    rec->offset = std::int64_t(site.offset);
                    rec->section = site.section;
                    rec->size = payload.size();
                    rec->applied = false;
                    rec->note = "место уже занято: " + g.what;
                }
                L("  [=] " + st.label + " @ 0x" + hex16(site.offset) + ": уже закрыт (" + g.what + ")");
                return StepResult::Occupied;
            }
        }
    }

    if (!wowpe::writeBytes(data, site.offset, payload)) {
        if (rec) {
            rec->offset = std::int64_t(site.offset);
            rec->section = site.section;
            rec->applied = false;
            rec->note = "запись не влезла в файл";
        }
        L("  ! " + st.label + " @ 0x" + hex16(site.offset) + ": запись " + str(payload.size()) + " байт не влезла в файл");
        return StepResult::WriteFailed;
    }
    if (used) used->push_back({ std::int64_t(site.offset), std::int64_t(site.offset + payload.size()), st.label });
    if (rec) {
        rec->offset = std::int64_t(site.offset);
        rec->section = site.section;
        rec->size = payload.size();
        rec->applied = true;
        rec->note = site.note;
        rec->expected = payload;
    }
    L("  [+] " + st.label + " @ 0x" + hex16(site.offset) + " (" + site.section + ", RVA 0x" +
      hex16(img.fileOffsetToRva(site.offset)) + ", " + str(payload.size()) + " байт)" +
      (site.allHits.size() > 1 ? "  (вхождений в файле: " + str(site.allHits.size()) + " — правим только это)" : ""));
    return StepResult::Applied;
}

int applyRecipeToImage(Bytes &data, const wowpe::Image &img, const Recipe &r,
                       Report *rep, std::vector<std::string> *log) {
    auto L = [&](const std::string &s) { if (log) log->push_back(s); };
    L("Рецепт «" + (r.meta.name.empty() ? std::string("без имени") : r.meta.name) +
      "»: hunks=" + str(r.hunks.size()));
    int done = 0, skipped = 0, miss = 0;

    std::vector<UsedRange> used;
    if (rep) for (const Record &rc : rep->records)
        if (rc.applied && rc.offset >= 0)
            used.push_back({ rc.offset, rc.offset + std::int64_t(rc.size), rc.label });

    for (const wowpe::Hunk &hk : r.hunks) {
        if (hk.before.empty() || hk.after.empty()) { ++skipped; continue; }
        const wowpe::Locate loc = wowpe::locateHunk(data, hk);
        const std::string tag = "рецепт:" + (hk.section.empty() ? std::string("hunk") : hk.section);

        Record rec;
        rec.id = "recipe";
        rec.label = tag;
        rec.mandatory = false;

        if (loc.kind == wowpe::Locate::Kind::Match || loc.kind == wowpe::Locate::Kind::Relocated) {
            bool busy = false;
            for (const UsedRange &g : used)
                if (overlaps(g, std::int64_t(loc.offset), std::int64_t(loc.offset + hk.after.size()))) {
                    L("  [~] " + tag + " @ 0x" + hex16(loc.offset) + ": место уже занято (" + g.what + ") — пропуск");
                    busy = true;
                    break;
                }
            if (busy) { ++skipped; continue; }
            if (!img.isDiskPatchableOffset(loc.offset)) {
                L("  [!] " + tag + " @ 0x" + hex16(loc.offset) + " попал в " + img.locationName(loc.offset) +
                  " — на диске не правим (код упакован и восстановится при запуске)");
                ++skipped;
                continue;
            }
            if (!wowpe::writeBytes(data, loc.offset, hk.after)) {
                L("  ! " + tag + ": запись " + str(hk.after.size()) + " байт не влезла в файл");
                ++miss;
                continue;
            }
            used.push_back({ std::int64_t(loc.offset), std::int64_t(loc.offset + hk.after.size()), tag });
            rec.offset = std::int64_t(loc.offset);
            rec.section = img.locationName(loc.offset);
            rec.size = hk.after.size();
            rec.applied = true;
            rec.expected = hk.after;
            rec.note = (loc.kind == wowpe::Locate::Kind::Relocated ? "перенесён по контексту: " : "") + loc.note;
            if (rep) rep->records.push_back(rec);
            ++done;
            L("  [+] " + tag + " @ 0x" + hex16(loc.offset) + " (" + str(hk.after.size()) + " байт, " +
              kindName(loc.kind) + ")");
        } else if (loc.kind == wowpe::Locate::Kind::AlreadyApplied) {
            rec.offset = std::int64_t(loc.offset);
            rec.section = img.locationName(loc.offset);
            rec.size = hk.after.size();
            rec.applied = true;
            rec.expected = hk.after;
            rec.note = "уже применён";
            if (rep) rep->records.push_back(rec);
            ++done;
            L("  [=] " + tag + " @ 0x" + hex16(loc.offset) + ": уже применён");
        } else if (loc.kind == wowpe::Locate::Kind::Ambiguous) {
            std::string cands;
            for (std::size_t c : loc.candidates) {
                if (!cands.empty()) cands += ", ";
                cands += "0x" + hex16(c);
            }
            L("  [?] " + tag + ": неоднозначно (" + str(loc.candidates.size()) +
              " кандидатов: " + cands + ") — пропуск");
            ++skipped;
        } else {
            L("  [-] " + tag + ": не найден в этом билде (контекст не совпал) — пропуск");
            ++miss;
        }
    }
    L("Рецепт: перенесено " + str(done) + ", пропущено " + str(skipped) + ", не найдено " + str(miss));
    if (rep) { rep->applied += done; rep->skipped += skipped; }
    return done;
}

bool verifyImage(const Bytes &data, const wowpe::Image &img, const Report &rep,
                 std::vector<std::string> *out) {
    auto L = [&](const std::string &s) { if (out) out->push_back(s); };
    bool clean = true;

    const bool sizeOk = rep.signatureStripped
                            ? (std::int64_t(data.size()) <= rep.originalSize)
                            : (std::int64_t(data.size()) == rep.originalSize);
    if (rep.originalSize > 0 && !sizeOk) {
        L("[!!] размер изменился: было " + str(rep.originalSize) + ", стало " +
          str(data.size()) + " — клиент так не запустится");
        clean = false;
    } else if (rep.originalSize > 0) {
        L("[OK] размер " + str(data.size()) + " байт — PE не разъехался (все правки in-place)");
    }

    const wowpe::Image again = wowpe::parse(data);
    if (!again.valid) {
        L("[!!] PE перестал разбираться: " + again.error);
        clean = false;
    } else {
        L("[OK] PE разбирается: " + str(again.numberOfSections) + " секций, SizeOfImage 0x" +
          hex16(again.sizeOfImage));
        if (again.numberOfSections != img.numberOfSections || again.sizeOfImage != img.sizeOfImage) {
            L("[!!] заголовок изменился (секций/SizeOfImage не те)");
            clean = false;
        } else {
            L("[OK] заголовок и таблица секций не изменились");
        }
    }

    int verified = 0;
    for (const Record &rec : rep.records) {
        if (!rec.applied || rec.offset < 0 || rec.expected.empty()) continue;
        const std::size_t off = std::size_t(rec.offset);
        if (off + rec.expected.size() > data.size()) {
            L("[!!] " + rec.label + ": смещение за пределами файла");
            clean = false;
            continue;
        }
        if (std::memcmp(data.data() + off, rec.expected.data(), rec.expected.size()) == 0) {
            ++verified;
            L("[OK] " + rec.label + " @ 0x" + hex16(off) + " — " + str(rec.expected.size()) + " байт на месте");
        } else {
            L("[!!] " + rec.label + " @ 0x" + hex16(off) + " — байты НЕ те, что записали");
            clean = false;
        }
    }
    if (verified == 0 && !rep.records.empty()) {
        L("[!!] ни один патч не подтверждён");
        clean = false;
    }

    // Родных ключей Blizzard остаться не должно — но смотрим только в секции
    // данных: в .text такие же 8 байт могут встретиться случайно (код мы всё
    // равно не правим, и на работу клиента они не влияют).
    bool rsaPlanned = false, edPlanned = false;
    for (const Record &rc : rep.records) {
        if (rc.id == "rsa.connectto") rsaPlanned = true;
        if (rc.id == "ed25519") edPlanned = true;
    }
    const wowpe::Site bliz = wowpe::chooseSite(
        data, img, wowpe::patternFromBytes(patConnectTo().data(), patConnectTo().size()),
        /*requirePatchable=*/true);
    const wowpe::Site blizBe = wowpe::chooseSite(
        data, img, wowpe::patternFromBytes(blizzardRsaSignatureBe().data(), blizzardRsaSignatureBe().size()),
        /*requirePatchable=*/true);
    if (!bliz.found && !blizBe.found) L("[OK] родной ключ Blizzard ConnectTo в секциях данных не найден");
    else if (rsaPlanned) {
        const wowpe::Site &b = bliz.found ? bliz : blizBe;
        L("[!!] родной ключ Blizzard ConnectTo всё ещё на месте @ 0x" + hex16(b.offset) +
          " (" + b.section + ") — RSA-патч не применился");
        clean = false;
    } else {
        const wowpe::Site &b = bliz.found ? bliz : blizBe;
        L("[i] родной ключ Blizzard ConnectTo на месте @ 0x" + hex16(b.offset) +
          " — RSA-патч не планировался (режим «только рецепт»)");
    }
    const wowpe::Site blizEd = wowpe::chooseSite(
        data, img, wowpe::patternFromBytes(patGameCryptoEd().data(), patGameCryptoEd().size()),
        /*requirePatchable=*/true);
    if (!blizEd.found) L("[OK] родной ключ Blizzard Ed25519 в секциях данных не найден");
    else L("[i] родной ключ Blizzard Ed25519 ещё на месте @ 0x" + hex16(blizEd.offset) +
           " — для 11.x/12.x это означает обрыв при входе в мир");

    const wowpe::Site tr = wowpe::chooseSite(
        data, img, wowpe::patternFromBytes(trinityRsaModulusLe().data(), 8), /*requirePatchable=*/true);
    const wowpe::Site trBe = wowpe::chooseSite(
        data, img, wowpe::patternFromBytes(trinityRsaModulusBe().data(), 8), /*requirePatchable=*/true);
    if (tr.found) L("[OK] ключ TrinityCore ConnectTo присутствует @ 0x" + hex16(tr.offset));
    else if (trBe.found) L("[OK] ключ TrinityCore ConnectTo присутствует (BE-порядок) @ 0x" + hex16(trBe.offset));
    else if (rsaPlanned) { L("[!!] ключ TrinityCore ConnectTo НЕ найден в результате"); clean = false; }
    else L("[i] ключа TrinityCore ConnectTo нет — в этом режиме он и не подставлялся");

    const wowpe::Site tre = wowpe::chooseSite(
        data, img, wowpe::patternFromBytes(trinityEd25519PublicKey().data(), 8), /*requirePatchable=*/true);
    if (tre.found) L("[OK] ключ TrinityCore Ed25519 присутствует @ 0x" + hex16(tre.offset));
    else if (edPlanned) { L("[!!] ключ TrinityCore Ed25519 НЕ найден в результате"); clean = false; }
    else L("[i] ключ TrinityCore Ed25519 не найден (для legacy-профилей это нормально)");

    return clean;
}

bool patchImage(Bytes &data, const Options &opts, const Detection &det,
                Report *rep, std::vector<std::string> *log) {
    auto L = [&](const std::string &s) { if (log) log->push_back(s); };
    Report local;
    Report &r = rep ? *rep : local;
    // Отчёт может уже содержать записи рецептов, применённых ДО сигнатурных
    // патчей (см. applyRecipeToImage): их нельзя терять — по ним считается,
    // какие диапазоны уже заняты.
    const std::vector<Record> preset = r.records;
    const int presetApplied = r.applied;
    r = Report();
    r.records = preset;
    r.applied = presetApplied;

    const std::size_t originalSize = data.size();
    r.originalSize = std::int64_t(originalSize);

    const wowpe::Image img = wowpe::parse(data);
    if (!img.valid) {
        r.error = "PE не разобран: " + img.error + " — это не похоже на Wow.exe";
        L("[!!] " + r.error);
        return false;
    }
    L("PE: " + std::string(img.plus ? "PE32+ (x64)" : "PE32") + ", секций " + str(img.numberOfSections) +
      ", ImageBase 0x" + hex16(img.imageBase) + ", SizeOfImage 0x" + hex16(img.sizeOfImage) +
      ", Authenticode " + (img.certTablePointer ? "есть" : "нет"));

    // Уже пропатчен?
    r.alreadyPatched =
        !wowpe::findAll(data, wowpe::patternFromBytes(trinityRsaModulusLe().data(), 8), 1).empty();
    if (r.alreadyPatched)
        L("[i] В файле уже стоит ключ TrinityCore — возможно, он уже пропатчен.");

    // Ключи берём из opts: PEM/hex разбирает вызывающая сторона (Qt или CLI),
    // потому что ядро не работает с файлами. Пусто = встроенные TrinityCore.
    // Рецепты тоже применяются снаружи ДО сигнатур (см. applyRecipeToImage) —
    // затем их диапазоны передаются сюда через `used`, чтобы не перезаписать.
    std::vector<UsedRange> used;
    for (const Record &rc : r.records)
        if (rc.applied && rc.offset >= 0)
            used.push_back({ rc.offset, rc.offset + std::int64_t(rc.size), rc.label });

    const std::vector<Step> steps = buildPlan(opts, det, img, data, log);
    L("Применяем патчи (только секции данных, первое вхождение):");

    int mandatoryDone = 0, mandatoryTotal = 0;
    for (const Step &st : steps) {
        if (st.mandatory) ++mandatoryTotal;
        Record rec;
        const StepResult res = applyStep(data, img, st, &rec, &used, log);
        switch (res) {
        case StepResult::Applied:
            ++r.applied;
            if (st.mandatory) ++mandatoryDone;
            break;
        case StepResult::Occupied:
        case StepResult::AlreadyApplied:
            // Место уже закрыто тем же значением (рецепт) или значение уже
            // стоит в секции данных — считаем обязательный патч выполненным.
            if (st.mandatory) ++mandatoryDone;
            ++r.applied;
            break;
        case StepResult::NotFound:
            if (st.mandatory) ++r.missedMandatory; else ++r.skipped;
            break;
        case StepResult::WriteFailed:
            if (st.mandatory) ++r.missedMandatory; else ++r.skipped;
            break;
        }
        r.records.push_back(rec);
    }

    if (mandatoryTotal && mandatoryDone < mandatoryTotal) {
        r.error = "Не применены обязательные патчи (" + str(mandatoryDone) + " из " + str(mandatoryTotal) +
                  "). Проверьте «Диагностику»: возможно, билд другой или файл уже модифицирован.";
        return false;
    }
    if (r.applied == 0) {
        r.error = "Ни один патч не применён — нечего сохранять.";
        return false;
    }

    // --- checksum / подпись ---
    if (opts.fixChecksum) {
        std::uint32_t v = 0;
        r.checksumOld = img.checksum;
        if (wowpe::updateChecksum(data, img, &v)) {
            r.checksumNew = v;
            L("PE CheckSum пересчитан: 0x" + hex16(img.checksum) + " -> 0x" + hex16(v));
        } else {
            L("  ! PE CheckSum пересчитать не удалось");
        }
    } else if (img.checksum != 0) {
        L("PE CheckSum оставлен как есть (0x" + hex16(img.checksum) +
          "). Windows для пользовательских exe его не проверяет; включите опцию, если хотите «чистый» заголовок.");
    }
    if (opts.stripSignature && img.certTablePointer) {
        const std::uint32_t left = wowpe::stripAuthenticode(data, img, /*truncateOverlay=*/true);
        r.signatureStripped = (left == 0);
        L(left == 0 ? "Authenticode: каталог сертификатов обнулён, хвост обрезан (файл теперь без подписи)"
                    : "Authenticode: снять подпись не удалось");
    } else if (img.certTablePointer) {
        L("Authenticode: подпись оставлена. После правки байт она станет невалидной — это нормально "
          "и запуску не мешает (Windows её не проверяет у пользовательских exe).");
    }

    r.patchedSize = std::int64_t(data.size());
    r.sha256 = wowpe::sha256Hex(data);
    r.portalWritten = opts.portal;

    // --- проверка результата по буферу ---
    if (opts.verify) {
        L("Проверка результата:");
        r.verifiedClean = verifyImage(data, img, r, &r.verification);
        for (const std::string &s : r.verification) L("  " + s);
        if (!r.verifiedClean) {
            r.error = "Проверка результата не прошла — файл не сохранён. Подробности выше.";
            return false;
        }
    }

    r.ok = true;
    L("Готово: применено " + str(r.applied) + " патчей, SHA-256 " + r.sha256);
    return true;
}

// ===========================================================================
// Рецепты
// ===========================================================================
bool makeRecipe(const Bytes &original, const Bytes &patched, const RecipeMeta &meta,
                Recipe *out, std::vector<std::string> *log, std::string *error) {
    auto L = [&](const std::string &s) { if (log) log->push_back(s); };
    auto fail = [&](const std::string &m) { if (error) *error = m; L("[!!] " + m); return false; };

    if (original.empty()) return fail("оригинал пуст");
    if (patched.empty()) return fail("пропатченный файл пуст");
    if (original.size() != patched.size())
        L("[i] Размеры различаются: оригинал " + str(original.size()) + ", пропатченный " +
          str(patched.size()) + " — hunks всё равно снимаем по совпадающей части.");

    const wowpe::Image img = wowpe::parse(original);
    if (!img.valid) return fail("оригинал — не PE: " + img.error);

    out->meta = meta;
    out->hunks = wowpe::diffHunks(original, patched, img, /*context=*/24, /*mergeGap=*/96, /*maxHunks=*/8192);
    if (out->hunks.empty())
        return fail("различий не найдено — файлы идентичны (или пропатченный файл не того билда)");

    int patchable = 0;
    for (const wowpe::Hunk &h : out->hunks) {
        const std::string label = h.section.empty() ? std::string("?") : h.section;
        const bool ok = img.isDiskPatchableOffset(h.offset);
        if (ok) ++patchable;
        L(std::string(ok ? "  [+] " : "  [!] ") + label + " @ 0x" + hex16(h.offset) + ": " +
          str(h.before.size()) + " -> " + str(h.after.size()) + " байт" +
          (ok ? "" : " — СЕКЦИЯ КОДА, на диске не перенесётся"));
    }
    L("Всего hunks: " + str(out->hunks.size()) + ", из них в секциях данных: " + str(patchable));
    if (patchable == 0)
        L("[!] Все различия — в секциях кода. Такой рецепт на диске не работает: у ретейла код "
          "упакован и восстанавливается при запуске.");
    return true;
}

std::string recipeToJson(const Recipe &r) {
    std::string out;
    out += "{\n";
    out += "  \"format\": " + str(r.meta.formatVersion) + ",\n";
    out += "  \"name\": \"" + jsonEscape(r.meta.name) + "\",\n";
    out += "  \"createdUtc\": \"" + jsonEscape(r.meta.createdUtc) + "\",\n";
    out += "  \"source\": {\n";
    out += "    \"original\": \"" + jsonEscape(r.meta.sourceOriginal) + "\",\n";
    out += "    \"patched\": \"" + jsonEscape(r.meta.sourcePatched) + "\",\n";
    out += "    \"originalSha256\": \"" + jsonEscape(r.meta.originalSha256) + "\",\n";
    out += "    \"patchedSha256\": \"" + jsonEscape(r.meta.patchedSha256) + "\",\n";
    out += "    \"originalVersion\": \"" + jsonEscape(r.meta.originalVersion) + "\",\n";
    out += "    \"patchedVersion\": \"" + jsonEscape(r.meta.patchedVersion) + "\"\n";
    out += "  },\n";
    out += "  \"hunks\": [\n";
    for (std::size_t i = 0; i < r.hunks.size(); ++i) {
        const wowpe::Hunk &h = r.hunks[i];
        out += "    {\n";
        out += "      \"offset\": " + str(h.offset) + ",\n";
        out += "      \"section\": \"" + jsonEscape(h.section) + "\",\n";
        out += "      \"before\": \"" + wowpe::toHex(h.before) + "\",\n";
        out += "      \"after\": \"" + wowpe::toHex(h.after) + "\",\n";
        out += "      \"ctxBefore\": \"" + wowpe::toHex(h.ctxBefore) + "\",\n";
        out += "      \"ctxAfter\": \"" + wowpe::toHex(h.ctxAfter) + "\"\n";
        out += "    }";
        out += (i + 1 < r.hunks.size()) ? ",\n" : "\n";
    }
    out += "  ]\n";
    out += "}\n";
    return out;
}

bool recipeFromJson(const std::string &text, Recipe *out, std::string *error) {
    JVal root;
    JParser p(text);
    if (!p.parse(&root, error)) return false;
    if (root.type != JVal::T::Obj) { if (error) *error = "рецепт — не JSON-объект"; return false; }

    out->meta = RecipeMeta();
    if (const JVal *v = root.find("format")) out->meta.formatVersion = int(v->asInt(1));
    if (const JVal *v = root.find("name")) out->meta.name = v->asString();
    if (const JVal *v = root.find("createdUtc")) out->meta.createdUtc = v->asString();
    if (const JVal *src = root.find("source")) {
        if (const JVal *v = src->find("original")) out->meta.sourceOriginal = v->asString();
        if (const JVal *v = src->find("patched")) out->meta.sourcePatched = v->asString();
        if (const JVal *v = src->find("originalSha256")) out->meta.originalSha256 = v->asString();
        if (const JVal *v = src->find("patchedSha256")) out->meta.patchedSha256 = v->asString();
        if (const JVal *v = src->find("originalVersion")) out->meta.originalVersion = v->asString();
        if (const JVal *v = src->find("patchedVersion")) out->meta.patchedVersion = v->asString();
    }
    const JVal *hunks = root.find("hunks");
    if (!hunks || hunks->type != JVal::T::Arr) {
        if (error) *error = "в рецепте нет массива \"hunks\"";
        return false;
    }
    out->hunks.clear();
    for (const JVal &h : hunks->arr) {
        if (h.type != JVal::T::Obj) continue;
        wowpe::Hunk hk;
        if (const JVal *v = h.find("offset")) hk.offset = std::size_t(v->asInt(0));
        if (const JVal *v = h.find("section")) hk.section = v->asString();
        if (const JVal *v = h.find("before")) hk.before = fromHex(v->asString());
        if (const JVal *v = h.find("after")) hk.after = fromHex(v->asString());
        if (const JVal *v = h.find("ctxBefore")) hk.ctxBefore = fromHex(v->asString());
        if (const JVal *v = h.find("ctxAfter")) hk.ctxAfter = fromHex(v->asString());
        if (hk.before.empty() || hk.after.empty()) continue;
        out->hunks.push_back(std::move(hk));
    }
    if (out->hunks.empty()) { if (error) *error = "в рецепте нет ни одного пригодного hunk"; return false; }
    return true;
}

// ===========================================================================
// Диагностика
// ===========================================================================
Inspect inspect(const Bytes &data, const std::string &version, const std::string &pathHint) {
    Inspect r;
    auto L = [&](const std::string &s) { r.lines.push_back(s); };

    r.size = std::int64_t(data.size());
    r.sha256 = wowpe::sha256Hex(data);
    const wowpe::Image img = wowpe::parse(data);
    if (!img.valid) {
        r.error = "PE не разобран: " + img.error;
        L("[!!] " + r.error);
        return r;
    }
    r.detection = detectProfile(data, img, version, pathHint);
    r.pe64 = img.plus;
    r.imageBase = img.imageBase;
    r.entryRva = img.entryPointRva;
    r.sizeOfImage = img.sizeOfImage;
    r.machine = img.machine;
    r.checksum = img.checksum;
    r.numberOfSections = img.numberOfSections;
    r.hasSignature = img.certTablePointer != 0;
    r.certPointer = img.certTablePointer;
    r.certSize = img.certTableSize;

    L("=== ДИАГНОСТИКА ===");
    L("Размер: " + str(data.size()) + " байт (" + mb(data.size()) + " МБ)   SHA-256: " + r.sha256);
    L("Версия: " + (r.detection.version.empty() ? std::string("?") : r.detection.version));
    L("Профиль: [" + r.detection.profile + "] ветка " + r.detection.branch +
      (r.detection.note.empty() ? "" : " — " + r.detection.note));
    L(std::string("PE: ") + (img.plus ? "PE32+ (x64)" : "PE32 (x86)") + ", Machine 0x" + hex16(img.machine) +
      ", ImageBase 0x" + hex16(img.imageBase) + ", EntryPoint RVA 0x" + hex16(img.entryPointRva) +
      ", SizeOfImage 0x" + hex16(img.sizeOfImage) + ", CheckSum 0x" + hex16(img.checksum));
    L(r.hasSignature
        ? "Authenticode: есть (смещение " + str(img.certTablePointer) + ", " + str(img.certTableSize) +
          " байт) — после правки байт подпись станет невалидной"
        : "Authenticode: нет");

    L("Секции (" + str(img.sections.size()) + "):");
    for (const wowpe::Section &s : img.sections) {
        SectionInfo si;
        si.name = s.name;
        si.virtualAddress = s.virtualAddress;
        si.virtualSize = s.virtualSize;
        si.rawPointer = s.rawPointer;
        si.rawSize = s.rawSize;
        si.characteristics = s.characteristics;
        si.diskPatchable = s.isDiskPatchable();
        r.sections.push_back(si);

        std::string line = "  ";
        line += (si.name + "        ").substr(0, 8);
        line += " VA 0x" + hex16(si.virtualAddress) +
                " vsize 0x" + hex16(si.virtualSize) +
                " raw 0x" + hex16(si.rawPointer) + "+0x" + hex16(si.rawSize) +
                " chars 0x" + hex16(si.characteristics) + " ";
        line += si.diskPatchable ? "[можно править на диске]" : "[КОД/служебная — на диске не правим]";
        L(line);
    }

    L("Сайты патча:");
    for (const Probe &p : knownProbes()) {
        const Pattern pat = wowpe::patternFromBytes(p.pattern.data(), p.pattern.size());
        // siteAll — все вхождения (для отчёта), siteData — сайт в секции данных,
        // то есть тот, который патчер реально сможет править на диске.
        const wowpe::Site siteAll = wowpe::chooseSite(data, img, pat, /*requirePatchable=*/false);
        const wowpe::Site siteData = wowpe::chooseSite(data, img, pat, /*requirePatchable=*/true);
        if (siteAll.allHits.empty()) {
            L("  [-] " + p.name + ": не найдено");
        } else {
            const bool patchable = siteData.found;
            std::string line = std::string("  [") + (patchable ? "+" : "!") + "] " + p.name + ": " +
                str(siteAll.allHits.size()) + " вхожд., первое @ 0x" + hex16(siteAll.allHits.front()) +
                " (" + img.locationName(siteAll.allHits.front()) + "), RVA 0x" +
                hex16(img.fileOffsetToRva(siteAll.allHits.front())) +
                (p.expect ? ", нужно места " + str(p.expect) + " байт" : "");
            if (patchable)
                line += "  -> правим в " + siteData.section + " @ 0x" + hex16(siteData.offset);
            else
                line += "  — В СЕКЦИИ КОДА: на диске не правим";
            L(line);
        }
        const bool there = siteData.found;
        if (p.pattern == patConnectTo()) r.blizzardRsaFound = there;
        else if (p.pattern == blizzardRsaSignatureBe())
            r.blizzardRsaFound = r.blizzardRsaFound || there;   // LE не нашли — проверили BE
        else if (p.pattern == patSignatureMod()) { /* legacy: только для отчёта */ }
        else if (p.pattern == patGameCryptoEd()) r.blizzardEdFound = there;
        else if (p.pattern == portalSuffixPattern()) r.portalFound = there;
        else if (p.pattern == launcherLoginPattern()) r.launcherFound = there;
        else if (p.pattern == certBundleEnvelope()) r.certBundleSlotFound = there;
        else if (p.pattern.size() == 8 &&
                 std::memcmp(p.pattern.data(), trinityRsaModulusLe().data(), 8) == 0)
            r.trinityRsaFound = there;
        else if (p.pattern.size() == 8 &&
                 std::memcmp(p.pattern.data(), trinityRsaModulusBe().data(), 8) == 0)
            r.trinityRsaFound = r.trinityRsaFound || there;     // LE не нашли — проверили BE
        else if (p.pattern.size() == 8 &&
                 std::memcmp(p.pattern.data(), trinityEd25519PublicKey().data(), 8) == 0)
            r.trinityEdFound = there;
    }

    L("Итог: " + std::string(r.blizzardRsaFound ? "родной ключ Blizzard на месте" : "родного ключа Blizzard нет") +
      ", " + (r.trinityRsaFound ? "ключ TrinityCore УЖЕ стоит" : "ключа TrinityCore нет") + ".");
    if (!r.blizzardRsaFound && r.blizzardEdFound)
        L("  Примечание: Ed25519 Blizzard найдена — образ подлинный, а ConnectTo RSA не видна\n"
          "  ни в LE, ни в BE. Для билдов 12.x такой ключ обычно хранится обфусцированным:\n"
          "  замените его в памяти запущенного клиента (wow_mem_patcher или кнопка\n"
          "  «Пропатчить и запустить (память, Arctium)» в Studio).");
    r.ok = true;
    return r;
}

} // namespace wowpatch
