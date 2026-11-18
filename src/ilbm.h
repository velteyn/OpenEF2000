#pragma once

// Minimal IFF/ILBM reader for EF2000's `iff\256\*.lbm` screens and backdrops.
//
// Only the subset the game actually uses is implemented: ILBM/PBM, 8-bitplane
// chunky conversion, ByteRun1 compression, and the CMAP palette.  The GUI needs
// the backdrop and the campaign map; later asset work (ANIM, sprites) can build
// on this.

#include <cstdint>
#include <optional>
#include <vector>

namespace ef {

struct IlbmImage {
    int width = 0;
    int height = 0;
    int planes = 0;
    //! Palette entries as 0x00RRGGBB, empty when the file has no CMAP.
    std::vector<std::uint32_t> palette;
    //! One palette index per pixel, row-major.
    std::vector<std::uint8_t> pixels;
};

//! Returns std::nullopt when the data is not a decodable ILBM.
std::optional<IlbmImage> decode_ilbm(const std::vector<std::uint8_t>& data);

}  // namespace ef
