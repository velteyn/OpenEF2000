#pragma once

// 8-bit texture maps for DID shapes.
//
// A shape carries a hard-coded 8-character texture name, and opcode 0x47
// selects `[globaltextures]` slots from `3\init3d.txt`.  Every slot resolves to
// `tm\<NAME>.tm` -- 256 x 192, 8-bit indexed, exactly 49,152 bytes.
//
// The colour palette is *not* `colours\day.col`: that one maps the grey skin
// ramp (indices 176..223) to brown/olive.  EF2000 paints its models with
// `colours\2000.col`, whose indices 176..223 are a clean 48-step grey ramp,
// which is why the aircraft in the external view is flat grey.
//
// Shading is done by rotating the palette index rather than by scaling RGB:
// `lev\coltab.dat` holds sixteen 256-byte tables that map a texture index to
// the "same material at a different light level".  See `coltab()`.

#include "did.h"
#include "mesh.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace ef {

struct Texture {
    int width = 0;
    int height = 0;
    //! 0xAARRGGBB, ready to blit.
    std::vector<std::uint32_t> pixels;
    //! Raw palette indices, kept for shading through `coltab`.
    std::vector<std::uint8_t> indices;

    std::uint32_t at(int x, int y) const {
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        if (x >= width) x = width - 1;
        if (y >= height) y = height - 1;
        return pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                      static_cast<std::size_t>(x)];
    }

    std::uint8_t index_at(int x, int y) const {
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        if (x >= width) x = width - 1;
        if (y >= height) y = height - 1;
        return indices[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                       static_cast<std::size_t>(x)];
    }
};

//! Palette entries as 0x00RRGGBB.
using Palette = std::array<std::uint32_t, 256>;

class TextureBank {
public:
    static constexpr int kShadeLevels = 16;

    explicit TextureBank(const DidArchive& archive, const std::string& palette = "2000");

    bool ready() const { return ready_; }
    const std::string& palette_name() const { return palette_name_; }
    std::size_t loaded() const { return cache_.size(); }
    bool has_coltab() const { return coltab_ready_; }

    const Palette& palette() const { return palette_; }

    // Loads and caches `tm\<name>.tm`; nullptr when the texture is absent.
    const Texture* get(const std::string& name) const;

    //! `colours\<name>.col` as 0x00RRGGBB; returns false when absent.
    bool load_palette(const std::string& name);

    //! Applies shade table `level` (0..15) to a palette index.
    std::uint8_t shade(std::uint8_t index, int level) const {
        if (!coltab_ready_ || level <= 0 || level >= kShadeLevels) return index;
        return coltab_[static_cast<std::size_t>(level)][index];
    }

    //! Shaded colour for a texture index.
    std::uint32_t colour(std::uint8_t index, int level) const {
        return palette_[shade(index, level)];
    }

    // `[globaltextures]` of `3\init3d.txt`: slot 1..N -> texture name.
    // Slot 0 is always the shape's hard-coded texture.
    const std::vector<std::string>& global_slots() const { return globals_; }

private:
    const DidArchive& archive_;
    Palette palette_{};
    std::string palette_name_;
    bool ready_ = false;
    bool coltab_ready_ = false;
    std::array<std::array<std::uint8_t, 256>, kShadeLevels> coltab_{};
    std::vector<std::string> globals_;
    mutable std::unordered_map<std::string, std::unique_ptr<Texture>> cache_;
};

}  // namespace ef
