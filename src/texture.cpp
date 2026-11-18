#include "texture.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <utility>

namespace ef {
namespace {

constexpr int kTextureWidth = 256;
constexpr int kTextureHeight = 192;
constexpr std::size_t kTextureBytes = kTextureWidth * kTextureHeight;

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

//! Reads a `.col` file: 256 RGB triplets, either 6-bit VGA or 8-bit.
bool read_palette(const std::vector<std::uint8_t>& bytes, Palette& out) {
    if (bytes.size() != 768) return false;
    std::uint8_t peak = 0;
    for (std::uint8_t v : bytes) peak = std::max(peak, v);
    const int scale = peak <= 63 ? 255 : 63;  // divide by 63, or 1
    for (std::size_t i = 0; i < 256; ++i) {
        const std::uint32_t r = (bytes[i * 3 + 0] * scale) / 63;
        const std::uint32_t g = (bytes[i * 3 + 1] * scale) / 63;
        const std::uint32_t b = (bytes[i * 3 + 2] * scale) / 63;
        out[i] = (r << 16) | (g << 8) | b;
    }
    return true;
}

}  // namespace

TextureBank::TextureBank(const DidArchive& archive, const std::string& palette)
    : archive_(archive) {
    // `colours\2000.col` is the model palette: its grey ramp at 176..223 is what
    // makes the aircraft grey.  `day.col` maps that ramp to brown/olive.
    if (!load_palette(palette)) {
        for (const char* fallback : {"2000", "day", "gui"}) {
            if (load_palette(fallback)) break;
        }
    }

    // Global texture slots, indexed 1..N (slot 0 is the per-shape default).
    if (auto ini = archive_.resolve("init3d", "3\\", ".txt")) {
        if (auto bytes = archive_.read(*ini)) {
            globals_.resize(1);
            std::string section;
            const std::string text(bytes->begin(), bytes->end());
            std::size_t pos = 0;
            while (pos < text.size()) {
                auto end = text.find('\n', pos);
                if (end == std::string::npos) end = text.size();
                std::string line = text.substr(pos, end - pos);
                pos = end + 1;
                while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
                if (line.empty()) continue;
                if (line.front() == '[') {
                    section = line.substr(1, line.find(']') - 1);
                    for (char& c : section) {
                        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    }
                    continue;
                }
                if (section != "globaltextures") continue;
                const auto eq = line.find('=');
                if (eq == std::string::npos) continue;
                const int index = std::atoi(line.substr(0, eq).c_str());
                std::string value = line.substr(eq + 1);
                while (!value.empty() && (value.back() == ' ' || value.back() == '\r')) {
                    value.pop_back();
                }
                if (index <= 0) continue;
                if (static_cast<std::size_t>(index) >= globals_.size()) {
                    globals_.resize(std::size_t(index) + 1);
                }
                globals_[static_cast<std::size_t>(index)] = value;
            }
        }
    }

    // Sixteen 256-byte shade tables: `coltab[level][index]`.
    if (auto entry = archive_.resolve("coltab", "lev\\", ".dat")) {
        if (auto bytes = archive_.read(*entry)) {
            if (bytes->size() >= kShadeLevels * 256) {
                for (int level = 0; level < kShadeLevels; ++level) {
                    std::copy(bytes->begin() + level * 256, bytes->begin() + (level + 1) * 256,
                              coltab_[static_cast<std::size_t>(level)].begin());
                }
                coltab_ready_ = true;
            }
        }
    }
}

bool TextureBank::load_palette(const std::string& name) {
    auto entry = archive_.resolve(name, "colours\\", ".col");
    if (!entry) return false;
    auto bytes = archive_.read(*entry);
    if (!bytes) return false;
    Palette loaded{};
    if (!read_palette(*bytes, loaded)) return false;
    palette_ = loaded;
    palette_name_ = name + ".col";
    ready_ = true;
    return true;
}

const Texture* TextureBank::get(const std::string& name) const {
    if (!ready_ || name.empty()) return nullptr;
    if (const auto found = cache_.find(name); found != cache_.end()) {
        return found->second.get();
    }

    auto entry = archive_.resolve(name, "tm\\", ".tm");
    if (!entry) entry = archive_.resolve(lower(name), "tm\\", ".tm");
    if (!entry) {
        cache_.emplace(name, nullptr);
        return nullptr;
    }
    auto bytes = archive_.read(*entry);
    if (!bytes || bytes->size() != kTextureBytes) {
        cache_.emplace(name, nullptr);
        return nullptr;
    }

    auto texture = std::make_unique<Texture>();
    texture->width = kTextureWidth;
    texture->height = kTextureHeight;
    texture->indices.assign(bytes->begin(), bytes->end());
    texture->pixels.resize(kTextureBytes);
    for (std::size_t i = 0; i < kTextureBytes; ++i) {
        const std::uint8_t index = (*bytes)[i];
        // Index 0 is the transparency key for masked textures.
        const std::uint32_t a = index == 0 ? 0u : 0xFFu;
        texture->pixels[i] = (a << 24) | palette_[index];
    }

    const Texture* result = texture.get();
    cache_.emplace(name, std::move(texture));
    return result;
}

}  // namespace ef
