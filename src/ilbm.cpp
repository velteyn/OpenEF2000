#include "ilbm.h"

#include <cstring>

namespace ef {
namespace {

std::uint32_t be32(const std::uint8_t* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) | static_cast<std::uint32_t>(p[3]);
}

std::uint16_t be16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>((p[0] << 8) | p[1]);
}

}  // namespace

std::optional<IlbmImage> decode_ilbm(const std::vector<std::uint8_t>& data) {
    if (data.size() < 12) return std::nullopt;
    if (std::memcmp(data.data(), "FORM", 4) != 0) return std::nullopt;
    if (std::memcmp(data.data() + 8, "ILBM", 4) != 0 &&
        std::memcmp(data.data() + 8, "PBM ", 4) != 0) {
        return std::nullopt;
    }

    IlbmImage image;
    int compression = 0;
    std::size_t body_offset = 0;
    std::size_t body_size = 0;

    const std::size_t end = std::min<std::size_t>(data.size(), 8 + be32(data.data() + 4));
    std::size_t pos = 12;
    while (pos + 8 <= end) {
        const char* id = reinterpret_cast<const char*>(data.data() + pos);
        const std::size_t size = be32(data.data() + pos + 4);
        const std::size_t payload = pos + 8;
        if (payload + size > data.size()) break;

        if (std::memcmp(id, "BMHD", 4) == 0 && size >= 20) {
            const std::uint8_t* p = data.data() + payload;
            image.width = be16(p + 0);
            image.height = be16(p + 2);
            image.planes = p[8];
            compression = p[10];
        } else if (std::memcmp(id, "CMAP", 4) == 0) {
            const std::size_t count = std::min<std::size_t>(size / 3, 256);
            image.palette.resize(count);
            for (std::size_t i = 0; i < count; ++i) {
                const std::uint8_t* p = data.data() + payload + i * 3;
                image.palette[i] = (static_cast<std::uint32_t>(p[0]) << 16) |
                                   (static_cast<std::uint32_t>(p[1]) << 8) |
                                   static_cast<std::uint32_t>(p[2]);
            }
        } else if (std::memcmp(id, "BODY", 4) == 0) {
            body_offset = payload;
            body_size = size;
        }
        pos = payload + size + (size & 1);
    }

    if (image.width <= 0 || image.height <= 0 || image.planes <= 0 || image.planes > 8) {
        return std::nullopt;
    }
    if (body_offset == 0) return std::nullopt;

    const std::size_t row_bytes = static_cast<std::size_t>((image.width + 7) / 8);
    const std::size_t plane_bytes = row_bytes * static_cast<std::size_t>(image.height) *
                                    static_cast<std::size_t>(image.planes);
    image.pixels.assign(static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height),
                        0);

    //! Sequential ByteRun1/raw reader over the BODY chunk.
    std::size_t src = body_offset;
    const std::size_t src_end = body_offset + body_size;

    auto decode_plane = [&](std::vector<std::uint8_t>& out) {
        out.assign(plane_bytes, 0);
        std::size_t written = 0;
        while (written < plane_bytes && src < src_end) {
            if (compression == 0) {
                out[written++] = data[src++];
                continue;
            }
            const auto n = static_cast<std::int8_t>(data[src++]);
            if (n >= 0) {
                const std::size_t count = static_cast<std::size_t>(n) + 1;
                for (std::size_t i = 0; i < count && written < plane_bytes; ++i) {
                    if (src < src_end) out[written++] = data[src++];
                }
            } else if (n != -128) {
                const std::size_t count = static_cast<std::size_t>(-n) + 1;
                if (src >= src_end) break;
                const std::uint8_t value = data[src++];
                for (std::size_t i = 0; i < count && written < plane_bytes; ++i) {
                    out[written++] = value;
                }
            }
        }
        return written == plane_bytes;
    };

    std::vector<std::uint8_t> planes;
    if (!decode_plane(planes)) return std::nullopt;

    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            std::uint8_t value = 0;
            const std::size_t byte = static_cast<std::size_t>(x) / 8;
            const auto bit = static_cast<std::uint8_t>(0x80u >> (x & 7));
            for (int plane = 0; plane < image.planes; ++plane) {
                // ILBM stores each scanline as all planes in sequence, so the
                // buffer is row-major with planes interleaved inside a row.
                const std::size_t index =
                    (static_cast<std::size_t>(y) * static_cast<std::size_t>(image.planes) +
                     static_cast<std::size_t>(plane)) *
                        row_bytes +
                    byte;
                if (planes[index] & bit) value |= static_cast<std::uint8_t>(1u << plane);
            }
            image.pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) +
                         static_cast<std::size_t>(x)] = value;
        }
    }
    return image;
}

}  // namespace ef
