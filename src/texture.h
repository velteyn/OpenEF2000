#pragma once

// 8-bit texture maps for DID shapes.
//
// The `.3` header carries a hard-coded 8-character texture name; the game
// loads `tm\<NAME>.tm` -- 256 x 192, 8-bit indexed, exactly 49,152 bytes --
// and colours it with `colours\day.col`, a 768-byte 6-bit VGA palette.
// Masked textures (opcode 0x8E) use palette index 0 as transparency.

#include "did.h"

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

    std::uint32_t at(int x, int y) const {
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        if (x >= width) x = width - 1;
        if (y >= height) y = height - 1;
        return pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                      static_cast<std::size_t>(x)];
    }
};

class TextureBank {
public:
    explicit TextureBank(const DidArchive& archive);

    bool ready() const { return ready_; }
    const std::string& palette_name() const { return palette_name_; }
    std::size_t loaded() const { return cache_.size(); }

    // Loads and caches `tm\<name>.tm`; nullptr when the texture is absent.
    const Texture* get(const std::string& name) const;

    // `[globaltextures]` of `3\init3d.txt`: slot 1..N -> texture name.
    // Slot 0 is always the shape's hard-coded texture.
    const std::vector<std::string>& global_slots() const { return globals_; }

private:
    const DidArchive& archive_;
    std::array<std::uint8_t, 768> rgb_{};
    std::string palette_name_;
    bool ready_ = false;
    std::vector<std::string> globals_;
    mutable std::unordered_map<std::string, std::unique_ptr<Texture>> cache_;
};

}  // namespace ef
