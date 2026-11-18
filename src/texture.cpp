#include "texture.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <unordered_map>
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

namespace ef {
namespace {

struct EdgeKey {
    long a, b;
    bool operator==(const EdgeKey& other) const { return a == other.a && b == other.b; }
};

struct EdgeKeyHash {
    std::size_t operator()(const EdgeKey& k) const {
        return std::hash<long>{}(k.a) * 31u ^ std::hash<long>{}(k.b);
    }
};

long vertex_key(const Vec3f& p) {
    const auto x = static_cast<long>(p.x * 10.0f);
    const auto y = static_cast<long>(p.y * 10.0f);
    const auto z = static_cast<long>(p.z * 10.0f);
    return (x * 73856093L) ^ (y * 19349663L) ^ (z * 83492791L);
}

}  // namespace

std::string TextureBank::best_atlas_name(const std::string& shape_name,
                                         const Mesh& mesh) const {
    if (!ready_) return {};

    // Cache the decision per shape.
    if (const auto found = atlas_cache_.find(shape_name); found != atlas_cache_.end()) {
        return found->second;
    }

    // Shared edges -> the UV midpoints each side sees. A correct atlas makes
    // the two samples agree.
    std::unordered_map<EdgeKey, std::pair<std::pair<int, int>, std::pair<int, int>>,
                       EdgeKeyHash>
        edges;
    for (const auto& t : mesh.triangles) {
        if (!t.textured || !t.has_uv) continue;
        const Vec3f* v[3] = {&t.a, &t.b, &t.c};
        for (int k = 0; k < 3; ++k) {
            long ka = vertex_key(*v[k]);
            long kb = vertex_key(*v[(k + 1) % 3]);
            if (ka > kb) std::swap(ka, kb);
            const EdgeKey key{ka, kb};
            const int mu = static_cast<int>((t.uv[k][0] + t.uv[(k + 1) % 3][0]) * 0.5f);
            const int mv = static_cast<int>((t.uv[k][1] + t.uv[(k + 1) % 3][1]) * 0.5f);
            if (const auto found = edges.find(key); found != edges.end()) {
                found->second.second = {mu, mv};
            } else {
                edges.emplace(key, std::make_pair(std::make_pair(mu, mv),
                                                  std::make_pair(-1, -1)));
            }
        }
    }
    std::vector<std::pair<std::pair<int, int>, std::pair<int, int>>> pairs;
    for (const auto& [key, uv] : edges) {
        if (uv.second.first >= 0) pairs.push_back(uv);
    }

    // Candidate atlases: the shape's own tm_<name>N family, the generic
    // airframe/panel maps, then everything init3d knows.
    std::vector<std::string> candidates;
    for (int i = 1; i <= 3; ++i) candidates.push_back("tm_" + shape_name + std::to_string(i));
    for (const char* name : {"tm_pans", "tm_lines", "tm_sides", "tm_arms", "middles",
                             "tm_efa3", "tm_efa2", "tm_efa1"}) {
        candidates.emplace_back(name);
    }
    for (std::size_t k = 1; k < globals_.size(); ++k) {
        if (!globals_[k].empty()) candidates.push_back(globals_[k]);
    }

    std::string best;
    double best_score = 1e30;
    for (const auto& name : candidates) {
        const Texture* texture = get(name);
        if (texture == nullptr || pairs.empty()) continue;
        double total = 0.0;
        for (const auto& [left, right] : pairs) {
            const std::uint32_t a = texture->at(left.first, left.second);
            const std::uint32_t b = texture->at(right.first, right.second);
            total += std::abs(int((a >> 16) & 0xFF) - int((b >> 16) & 0xFF)) +
                     std::abs(int((a >> 8) & 0xFF) - int((b >> 8) & 0xFF)) +
                     std::abs(int(a & 0xFF) - int(b & 0xFF));
        }
        const double score = total / static_cast<double>(pairs.size());
        if (score < best_score) {
            best_score = score;
            best = name;
        }
    }

    atlas_cache_[shape_name] = best;
    return best;
}

}  // namespace ef
