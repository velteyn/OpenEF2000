#include "ra.h"

#include <array>
#include <cstddef>

namespace ef {
namespace {

// MSB-first bit buffer plus an independent raw-byte cursor, exactly like the
// original: raw reads take the byte *after* the currently buffered byte even
// when the buffer still holds unread bits.
class BitReader {
public:
    BitReader(std::span<const uint8_t> data, std::size_t pos)
        : data_(data), pos_(pos) {}

    int bit() {
        if (nbits_ == 0) {
            if (pos_ >= data_.size()) throw Invalid{};
            cur_ = data_[pos_++];
            nbits_ = 8;
        }
        const int b = (cur_ >> 7) & 1;
        cur_ = static_cast<uint8_t>(cur_ << 1);
        --nbits_;
        return b;
    }

    uint32_t bits(int n) {
        uint32_t v = 0;
        for (int i = 0; i < n; ++i) v = (v << 1) | static_cast<uint32_t>(bit());
        return v;
    }

    uint8_t raw_byte() {
        if (pos_ >= data_.size()) throw Invalid{};
        return data_[pos_++];
    }

    uint16_t raw_word() {
        const uint16_t lo = raw_byte();
        const uint16_t hi = raw_byte();
        return static_cast<uint16_t>(lo | (hi << 8));
    }

    std::size_t pos() const { return pos_; }

    struct Invalid {};

private:
    std::span<const uint8_t> data_;
    std::size_t pos_ = 0;
    uint8_t cur_ = 0;
    int nbits_ = 0;
};

// Serialised binary trie. Entry index = node * 2 + bit. A non-zero `child`
// descends; a zero child means the symbol is `symbol`.
struct Tree {
    std::array<uint16_t, 512> child{};
    std::array<uint16_t, 512> symbol{};

    // Returns (symbol, final node index). The node index matters for the
    // distance tree: a symbol below 2 uses ~node instead of extra bits.
    std::pair<uint16_t, int> decode(BitReader& br) const {
        int node = 0;
        for (;;) {
            const int entry = node * 2 + br.bit();
            if (entry >= 512) return {0, node};
            const uint16_t c = child[static_cast<std::size_t>(entry)];
            if (c != 0) {
                node = c;
            } else {
                return {symbol[static_cast<std::size_t>(entry)], node};
            }
        }
    }
};

Tree build_tree(BitReader& br) {
    Tree t;
    const int count = br.raw_byte();
    const int entries = count * 2;
    const int min_len = static_cast<int>(br.bits(3)) + 1;
    const int max_len = static_cast<int>(br.bits(3)) + 1;
    for (int i = 0; i < entries && i < 512; ++i) {
        if (br.bit()) {
            t.symbol[static_cast<std::size_t>(i)] =
                static_cast<uint16_t>(br.bits(max_len));
        } else {
            t.child[static_cast<std::size_t>(i)] =
                static_cast<uint16_t>(br.bits(min_len));
        }
    }
    return t;
}

uint16_t checksum(std::span<const uint8_t> buf) {
    uint8_t lo = 0, hi = 0;
    for (uint8_t b : buf) {
        lo = static_cast<uint8_t>(lo + b);
        hi = static_cast<uint8_t>(hi + lo);
    }
    return static_cast<uint16_t>((hi << 8) | lo);
}

uint32_t read_u32(std::span<const uint8_t> d, std::size_t off) {
    return static_cast<uint32_t>(d[off]) | (static_cast<uint32_t>(d[off + 1]) << 8) |
           (static_cast<uint32_t>(d[off + 2]) << 16) |
           (static_cast<uint32_t>(d[off + 3]) << 24);
}

uint16_t read_u16(std::span<const uint8_t> d, std::size_t off) {
    return static_cast<uint16_t>(d[off] | (d[off + 1] << 8));
}

}  // namespace

bool ra_looks_valid(std::span<const uint8_t> data) {
    if (data.size() < 16) return false;
    if (data[0] != 'R' || data[1] != 'A') return false;
    const uint32_t size = read_u32(data, 4);
    return size > 0 && size < (64u << 20);
}

std::optional<std::vector<uint8_t>> ra_decode(std::span<const uint8_t> data) {
    if (!ra_looks_valid(data)) return std::nullopt;

    const uint32_t size = read_u32(data, 4);
    const uint16_t want_sum = read_u16(data, 10);

    BitReader br(data, 12);
    std::vector<uint8_t> out;
    out.reserve(size);

    try {
        if (br.bit() != 1) return std::nullopt;

        for (;;) {
            const uint16_t pair_count = br.raw_word();
            const Tree match_tree = build_tree(br);
            const Tree lit_tree = build_tree(br);
            const Tree dist_tree = build_tree(br);

            for (uint16_t i = 0; i < pair_count; ++i) {
                // Literal run.
                const auto [lit, _ignored] = lit_tree.decode(br);
                for (uint16_t k = 0; k < lit; ++k) out.push_back(br.raw_byte());

                // Match.
                auto [m, mnode] = match_tree.decode(br);
                (void)mnode;
                if (m == 0xFF) {
                    const uint32_t hi = br.bits(2);
                    const uint32_t lo = br.raw_byte();
                    m = static_cast<uint16_t>(0xFF + ((hi << 8) | lo));
                }
                if (m != 0) {
                    auto [d, dnode] = dist_tree.decode(br);
                    std::size_t dist;
                    if (d < 2) {
                        dist = static_cast<std::size_t>(d) + 1;
                    } else {
                        uint32_t value = 1;
                        for (uint16_t k = 0; k + 1 < d; ++k) {
                            value = (value << 1) | static_cast<uint32_t>(br.bit());
                        }
                        dist = static_cast<std::size_t>(value) + 1;
                    }
                    if (dist > out.size()) return std::nullopt;
                    for (uint16_t k = 0; k < m; ++k) out.push_back(out[out.size() - dist]);
                }
                if (out.size() > size) return std::nullopt;
            }

            if (br.bit() == 0) break;
        }
    } catch (const BitReader::Invalid&) {
        return std::nullopt;
    }

    if (out.size() != size) return std::nullopt;
    if (checksum(out) != want_sum) return std::nullopt;
    return out;
}

}  // namespace ef
