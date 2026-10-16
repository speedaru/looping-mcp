#include "loo/archive.hpp"

#include <cstring>

namespace loo {

Archive Archive::reader(std::span<const std::uint8_t> data, std::size_t pos) {
    Archive a;
    a.reading_ = true;
    a.in_ = data;
    a.pos_ = pos;
    return a;
}

Archive Archive::writer() {
    Archive a;
    a.reading_ = false;
    return a;
}

void Archive::need(std::size_t n) const {
    if (in_.size() - pos_ < n) {
        throw Error("unexpected end of file at offset " + std::to_string(pos_) + " (need " + std::to_string(n) +
                    " bytes)");
    }
}

void Archive::bytes(std::uint8_t* p, std::size_t n) {
    if (reading_) {
        need(n);
        std::memcpy(p, in_.data() + pos_, n);
        pos_ += n;
    } else {
        out_.insert(out_.end(), p, p + n);
    }
}

void Archive::u8(std::uint8_t& v) { bytes(&v, 1); }

void Archive::u16(std::uint16_t& v) {
    std::uint8_t b[2] = {static_cast<std::uint8_t>(v), static_cast<std::uint8_t>(v >> 8)};
    bytes(b, 2);
    if (reading_) v = static_cast<std::uint16_t>(b[0] | (b[1] << 8));
}

void Archive::u32(std::uint32_t& v) {
    std::uint8_t b[4];
    for (int i = 0; i < 4; ++i) b[i] = static_cast<std::uint8_t>(v >> (8 * i));
    bytes(b, 4);
    if (reading_) {
        v = 0;
        for (int i = 0; i < 4; ++i) v |= std::uint32_t(b[i]) << (8 * i);
    }
}

void Archive::u64(std::uint64_t& v) {
    std::uint8_t b[8];
    for (int i = 0; i < 8; ++i) b[i] = static_cast<std::uint8_t>(v >> (8 * i));
    bytes(b, 8);
    if (reading_) {
        v = 0;
        for (int i = 0; i < 8; ++i) v |= std::uint64_t(b[i]) << (8 * i);
    }
}

void Archive::i32(std::int32_t& v) {
    auto u = static_cast<std::uint32_t>(v);
    u32(u);
    v = static_cast<std::int32_t>(u);
}

void Archive::count(std::uint32_t& n) {
    if (reading_) {
        std::uint16_t w = 0;
        u16(w);
        if (w == 0xFFFF) {
            u32(n);
        } else {
            n = w;
        }
    } else if (n < 0xFFFF) {
        auto w = static_cast<std::uint16_t>(n);
        u16(w);
    } else {
        std::uint16_t w = 0xFFFF;
        u16(w);
        u32(n);
    }
}

void Archive::str(std::u16string& s) {
    std::uint8_t mark0 = 0xFF;      // BYTE 0xFF ...
    std::uint16_t mark1 = 0xFFFE;   // ... then WORD 0xFFFE = "unicode string"
    if (reading_) {
        std::uint8_t a = 0;
        std::uint16_t b = 0;
        u8(a);
        u16(b);
        if (a != mark0 || b != mark1) {
            throw Error("bad CString marker at offset " + std::to_string(pos_ - 3));
        }
        std::uint8_t l8 = 0;
        u8(l8);
        std::uint32_t len = l8;
        if (l8 == 0xFF) {
            std::uint16_t l16 = 0;
            u16(l16);
            len = l16;
            if (l16 == 0xFFFF) u32(len);
        }
        need(std::size_t(len) * 2);
        s.resize(len);
        for (auto& ch : s) {
            std::uint16_t c = 0;
            u16(c);
            ch = static_cast<char16_t>(c);
        }
    } else {
        u8(mark0);
        u16(mark1);
        auto len = static_cast<std::uint32_t>(s.size());
        if (len < 255) {
            auto l8 = static_cast<std::uint8_t>(len);
            u8(l8);
        } else if (len < 0xFFFE) {
            std::uint8_t l8 = 0xFF;
            u8(l8);
            auto l16 = static_cast<std::uint16_t>(len);
            u16(l16);
        } else {
            std::uint8_t l8 = 0xFF;
            u8(l8);
            std::uint16_t l16 = 0xFFFF;
            u16(l16);
            u32(len);
        }
        for (char16_t ch : s) {
            auto c = static_cast<std::uint16_t>(ch);
            u16(c);
        }
    }
}

std::string toUtf8(const std::u16string& s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        std::uint32_t cp = s[i];
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < s.size() && s[i + 1] >= 0xDC00 && s[i + 1] <= 0xDFFF) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (s[i + 1] - 0xDC00);
            ++i;
        }
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    return out;
}

std::u16string fromUtf8(const std::string& s) {
    std::u16string out;
    for (std::size_t i = 0; i < s.size();) {
        auto c = static_cast<unsigned char>(s[i]);
        std::uint32_t cp = 0;
        std::size_t extra = 0;
        if (c < 0x80) {
            cp = c;
        } else if ((c >> 5) == 0x6) {
            cp = c & 0x1Fu;
            extra = 1;
        } else if ((c >> 4) == 0xE) {
            cp = c & 0x0Fu;
            extra = 2;
        } else if ((c >> 3) == 0x1E) {
            cp = c & 0x07u;
            extra = 3;
        } else {
            throw Error("invalid UTF-8");
        }
        if (i + extra >= s.size() && extra != 0) throw Error("truncated UTF-8");
        for (std::size_t k = 1; k <= extra; ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3Fu);
        i += 1 + extra;
        if (cp >= 0x10000) {
            cp -= 0x10000;
            out += static_cast<char16_t>(0xD800 + (cp >> 10));
            out += static_cast<char16_t>(0xDC00 + (cp & 0x3FF));
        } else {
            out += static_cast<char16_t>(cp);
        }
    }
    return out;
}

}  // namespace loo
