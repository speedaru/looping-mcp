// Symmetric reader/writer for MFC CArchive streams (little endian).
//
// Every serializable type describes its fields once with `ar.u32(x)` etc.; the same code
// reads when the archive is a reader and writes when it is a writer, as MFC's Serialize does.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace loo {

struct Error : std::runtime_error {
    using std::runtime_error::runtime_error;
};

template <std::size_t N>
using Bytes = std::array<std::uint8_t, N>;

class Archive {
public:
    static Archive reader(std::span<const std::uint8_t> data, std::size_t pos = 0);
    static Archive writer();

    bool reading() const { return reading_; }
    std::size_t pos() const { return pos_; }
    bool atEnd() const { return pos_ == in_.size(); }

    void u8(std::uint8_t& v);
    void u16(std::uint16_t& v);
    void u32(std::uint32_t& v);
    void u64(std::uint64_t& v);
    void i32(std::int32_t& v);

    void bytes(std::uint8_t* p, std::size_t n);
    template <std::size_t N>
    void raw(Bytes<N>& b) {
        bytes(b.data(), N);
    }

    // MFC CString (unicode): ff fe ff <length> <UTF-16LE chars>.
    void str(std::u16string& s);
    // CArchive::WriteCount / ReadCount: u16, or 0xFFFF followed by u32.
    void count(std::uint32_t& n);

    // Writer only: the bytes produced so far.
    std::vector<std::uint8_t> take() { return std::move(out_); }

private:
    Archive() = default;
    void need(std::size_t n) const;

    bool reading_ = true;
    std::span<const std::uint8_t> in_;
    std::size_t pos_ = 0;
    std::vector<std::uint8_t> out_;
};

std::string toUtf8(const std::u16string& s);
std::u16string fromUtf8(const std::string& s);

}  // namespace loo
