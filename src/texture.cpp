#include "texture.h"

#include <algorithm>
#include <cctype>

namespace ef {
namespace {

constexpr int kTextureWidth = 256;
constexpr int kTextureHeight = 192;
constexpr std::size_t kTextureBytes = kTextureWidth * kTextureHeight;

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

}  // namespace

TextureBank::TextureBank(const DidArchive& archive) : archive_(archive) {
    // `colours\day.col` is a 6-bit VGA palette; fall back to the GUI palette.
    for (const char* candidate : {"day", "gui", "dusk", "night", "dawn"}) {
        auto entry = archive_.resolve(candidate, "colours\\", ".col");
        if (!entry) continue;
        auto bytes = archive_.read(*entry);
        if (!bytes || bytes->size() != 768) continue;
        for (std::size_t i = 0; i < 768; ++i) {
            // 0..63 -> 0..255
            rgb_[i] = static_cast<std::uint8_t>(((*bytes)[i] * 255) / 63);
        }
        palette_name_ = std::string(candidate) + ".col";
        ready_ = true;
        break;
    }

    // Global texture slots, indexed 1..N (slot 0 is the per-shape default).
    auto ini = archive_.resolve("init3d", "3\\", ".txt");
    if (!ini) return;
    auto bytes = archive_.read(*ini);
    if (!bytes) return;
    globals_.resize(1);
    std::string section;
    std::string text(bytes->begin(), bytes->end());
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
            for (char& c : section) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            continue;
        }
        if (section != "globaltextures") continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const int index = std::atoi(line.substr(0, eq).c_str());
        std::string value = line.substr(eq + 1);
        while (!value.empty() && (value.back() == ' ' || value.back() == '\r')) value.pop_back();
        if (index <= 0) continue;
        if (static_cast<std::size_t>(index) >= globals_.size()) globals_.resize(std::size_t(index) + 1);
        globals_[static_cast<std::size_t>(index)] = value;
    }
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
    texture->pixels.resize(kTextureBytes);
    for (std::size_t i = 0; i < kTextureBytes; ++i) {
        const std::uint8_t index = (*bytes)[i];
        const std::uint32_t r = rgb_[std::size_t(index) * 3 + 0];
        const std::uint32_t g = rgb_[std::size_t(index) * 3 + 1];
        const std::uint32_t b = rgb_[std::size_t(index) * 3 + 2];
        // Index 0 is the transparency key for masked textures.
        const std::uint32_t a = index == 0 ? 0u : 0xFFu;
        texture->pixels[i] = (a << 24) | (r << 16) | (g << 8) | b;
    }

    const Texture* result = texture.get();
    cache_.emplace(name, std::move(texture));
    return result;
}

}  // namespace ef
