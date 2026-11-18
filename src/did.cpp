#include "did.h"

#include "ra.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace ef {
namespace {

constexpr std::uint32_t kHeaderKey = 0x35812B85u;
constexpr std::uint16_t kDirKey = 0x2B85u;

std::uint32_t read_u32(const std::vector<std::uint8_t>& d, std::size_t off) {
    return static_cast<std::uint32_t>(d[off]) |
           (static_cast<std::uint32_t>(d[off + 1]) << 8) |
           (static_cast<std::uint32_t>(d[off + 2]) << 16) |
           (static_cast<std::uint32_t>(d[off + 3]) << 24);
}

std::uint16_t read_u16(const std::vector<std::uint8_t>& d, std::size_t off) {
    return static_cast<std::uint16_t>(d[off] | (d[off + 1] << 8));
}

bool plain_header_at(const std::vector<std::uint8_t>& d, std::size_t off,
                     std::uint16_t& ec, std::uint16_t& pc, std::uint16_t& xc,
                     std::uint16_t& ss) {
    if (off + 8 > d.size()) return false;
    ec = read_u16(d, off);
    pc = read_u16(d, off + 2);
    xc = read_u16(d, off + 4);
    ss = read_u16(d, off + 6);
    if (pc < 1 || pc > 256 || xc < 1 || xc > 256) return false;
    if (ec == 0) return false;
    const std::size_t need = static_cast<std::size_t>(8) + ss + std::size_t(ec) * 12;
    return off + need == d.size();
}

std::vector<std::string> read_strings(const std::vector<std::uint8_t>& d,
                                      std::size_t& p, int count,
                                      std::uint8_t xor_key) {
    std::vector<std::string> out;
    out.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        if (p >= d.size()) throw std::runtime_error("DID directory truncated");
        const int len = d[p++];
        if (p + static_cast<std::size_t>(len) > d.size())
            throw std::runtime_error("DID directory truncated");
        std::string s;
        s.reserve(static_cast<std::size_t>(len));
        for (int k = 0; k < len; ++k) {
            s.push_back(static_cast<char>(d[p + static_cast<std::size_t>(k)] ^ xor_key));
        }
        p += static_cast<std::size_t>(len);
        out.push_back(std::move(s));
    }
    return out;
}

}  // namespace

std::uint32_t name_hash_alt(const std::string& name) {
    const std::size_t n = std::min<std::size_t>(name.size(), 15);
    std::uint8_t order = 0, acc = 0, hi = 0, lo = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const auto c = static_cast<std::uint8_t>(name[i]);
        if (i != 0 && c != '_') {
            const auto prev = static_cast<std::uint8_t>(name[i - 1]);
            order = static_cast<std::uint8_t>((order << 1) | (c > prev ? 1 : 0));
        }
        if (c == '_') {
            lo = static_cast<std::uint8_t>(lo - i);
        } else {
            acc = static_cast<std::uint8_t>(acc + c);
            hi = static_cast<std::uint8_t>(hi + acc);
            lo = static_cast<std::uint8_t>((lo << 1) ^ c);
        }
    }
    return (static_cast<std::uint32_t>(n & 0xF) << 28) |
           (static_cast<std::uint32_t>(acc & 0xF) << 24) |
           (static_cast<std::uint32_t>(hi) << 16) |
           (static_cast<std::uint32_t>(lo) << 8) |
           static_cast<std::uint32_t>(order);
}

std::uint32_t name_hash_v2(const std::string& name) {
    const std::size_t n = std::min<std::size_t>(name.size(), 15);
    std::uint32_t h = 0, order = 0;
    std::uint8_t prev = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const auto c = static_cast<std::uint8_t>(name[i]);
        if (c == '_') {
            h -= static_cast<std::uint32_t>(i);
        } else {
            if (i != 0) order = ((order << 1) | (prev < c ? 1u : 0u)) & 0xFF;
            h = (static_cast<std::uint32_t>(c) + h * 0x24u) % 0xFFFFBu;
        }
        prev = c;
    }
    return order ^ ((h << 8) & 0xFFFFFFFFu) ^ ((static_cast<std::uint32_t>(n) << 28) & 0xFFFFFFFFu);
}

DidArchive::DidArchive(const std::filesystem::path& path) : path_(path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open " + path.string());
    f.seekg(0, std::ios::end);
    const auto size = static_cast<std::size_t>(f.tellg());
    f.seekg(0, std::ios::beg);
    data_.resize(size);
    if (size > 0 && !f.read(reinterpret_cast<char*>(data_.data()),
                           static_cast<std::streamsize>(size))) {
        throw std::runtime_error("cannot read " + path.string());
    }
    detect_and_parse();
}

void DidArchive::detect_and_parse() {
    if (data_.size() < 12) throw std::runtime_error("archive too small");

    std::uint16_t ec = 0, pc = 0, xc = 0, ss = 0;

    // v2.0: XORed directory offset and header.
    const std::size_t xor_off = read_u32(data_, 0) ^ kHeaderKey;
    if (xor_off + 8 < data_.size()) {
        const std::uint16_t a = read_u16(data_, xor_off) ^ kDirKey;
        const std::uint16_t b = read_u16(data_, xor_off + 2) ^ kDirKey;
        const std::uint16_t c = read_u16(data_, xor_off + 4) ^ kDirKey;
        const std::uint16_t d = read_u16(data_, xor_off + 6) ^ kDirKey;
        if (b >= 1 && b <= 256 && c >= 1 && c <= 256 && a > 0 &&
            xor_off + 8 + d + std::size_t(a) * 12 <= data_.size()) {
            variant_ = "xorv2";
            parse_xorv2();
            return;
        }
    }

    const std::size_t plain_off = read_u32(data_, 0);
    if (plain_off < data_.size() && plain_header_at(data_, plain_off, ec, pc, xc, ss)) {
        variant_ = "plain8";
        parse_plain8(plain_off);
        return;
    }

    for (std::size_t p = 8; p + 8 < data_.size(); ++p) {
        if (plain_header_at(data_, p, ec, pc, xc, ss)) {
            variant_ = "plain8";
            parse_plain8(p);
            return;
        }
    }
    throw std::runtime_error("no known DID directory in " + path_.string());
}

void DidArchive::parse_plain8(std::size_t offset) {
    const std::uint16_t ec = read_u16(data_, offset);
    const std::uint16_t pc = read_u16(data_, offset + 2);
    const std::uint16_t xc = read_u16(data_, offset + 4);
    const std::uint16_t ss = read_u16(data_, offset + 6);

    std::size_t p = offset + 8;
    prefixes_ = read_strings(data_, p, pc, 0);
    extensions_ = read_strings(data_, p, xc, 0);

    const std::size_t start = offset + 8 + ss;
    entries_.reserve(ec);
    for (std::uint16_t i = 0; i < ec; ++i) {
        const std::size_t at = start + std::size_t(i) * 12;
        entries_.push_back(DidEntry{i, read_u32(data_, at), read_u32(data_, at + 4),
                                    read_u32(data_, at + 8)});
    }
}

void DidArchive::parse_xorv2() {
    const std::size_t offset = read_u32(data_, 0) ^ kHeaderKey;
    const std::uint16_t ec = read_u16(data_, offset) ^ kDirKey;
    const std::uint16_t pc = read_u16(data_, offset + 2) ^ kDirKey;
    const std::uint16_t xc = read_u16(data_, offset + 4) ^ kDirKey;
    const std::uint16_t ss = read_u16(data_, offset + 6) ^ kDirKey;

    std::size_t p = offset + 8;
    prefixes_ = read_strings(data_, p, pc, 0x85);
    extensions_ = read_strings(data_, p, xc, 0x85);

    const std::size_t start = offset + 8 + ss;
    entries_.reserve(ec);
    for (std::uint16_t i = 0; i < ec; ++i) {
        const std::size_t at = start + std::size_t(i) * 12;
        entries_.push_back(DidEntry{i, read_u32(data_, at), read_u32(data_, at + 4),
                                    read_u32(data_, at + 8)});
    }
}

std::string DidArchive::canonical(const std::string& name) const {
    std::string s;
    s.reserve(name.size());
    bool leading = true;
    for (char ch : name) {
        if (ch == '/') {
            s.push_back('\\');
            leading = false;
            continue;
        }
        if (leading && ch == '\\') continue;
        leading = false;
        s.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }

    int prefix_index = -1;
    for (std::size_t i = 0; i < prefixes_.size(); ++i) {
        std::string lower = prefixes_[i];
        for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (s.rfind(lower, 0) == 0) {
            prefix_index = static_cast<int>(i);
            s.erase(0, lower.size());
            break;
        }
    }

    const auto dot = s.rfind('.');
    if (dot != std::string::npos) {
        const std::string ext = s.substr(dot);
        for (std::size_t i = 0; i < extensions_.size(); ++i) {
            if (ext == extensions_[i]) {
                s = s.substr(0, dot) + static_cast<char>('A' + i);
                break;
            }
        }
    }

    if (prefix_index >= 0) s.insert(s.begin(), static_cast<char>('0' + prefix_index));
    return s;
}

std::uint32_t DidArchive::hash_name(const std::string& canonical_name) const {
    return variant_ == "xorv2" ? name_hash_v2(canonical_name)
                               : name_hash_alt(canonical_name);
}

std::optional<DidEntry> DidArchive::lookup(const std::string& name) const {
    const std::uint32_t key = hash_name(canonical(name));
    const auto skey = static_cast<std::int32_t>(key);

    // Entries are sorted by signed hash.
    std::size_t lo = 0, hi = entries_.size();
    while (lo < hi) {
        const std::size_t mid = (lo + hi) / 2;
        const auto sh = static_cast<std::int32_t>(entries_[mid].hash);
        if (sh == skey) return entries_[mid];
        if (sh < skey) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return std::nullopt;
}

std::optional<DidEntry> DidArchive::resolve(const std::string& stem,
                                            const std::string& prefix,
                                            const std::string& extension) const {
    return lookup("\\" + prefix + stem + extension);
}

std::vector<std::uint8_t> DidArchive::read_raw(const DidEntry& entry) const {
    if (entry.offset > data_.size() || entry.size > data_.size() - entry.offset) {
        throw std::runtime_error("DID entry out of bounds");
    }
    return std::vector<std::uint8_t>(data_.begin() + entry.offset,
                                     data_.begin() + entry.offset + entry.size);
}

std::string DidArchive::storage(const DidEntry& entry) const {
    if (entry.size >= 2 && entry.offset + 2 <= data_.size() && data_[entry.offset] == 'R' &&
        data_[entry.offset + 1] == 'A') {
        return "ra";
    }
    return "raw";
}

std::optional<std::vector<std::uint8_t>> DidArchive::read(const DidEntry& entry) const {
    const auto raw = read_raw(entry);
    if (raw.size() >= 2 && raw[0] == 'R' && raw[1] == 'A') {
        return ra_decode(raw);
    }
    return raw;
}

}  // namespace ef
