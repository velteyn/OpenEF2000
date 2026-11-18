#include "shape3d.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <stdexcept>

namespace ef {
namespace {

constexpr std::size_t kMaxXyz = 768;
constexpr std::size_t kMaxPolygonVertices = 40;

std::int16_t s16(std::uint16_t v) { return static_cast<std::int16_t>(v); }

struct Mat {
    // Column vectors: basis[0..2] and an offset.
    float b[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    float o[3] = {0, 0, 0};

    Vec3f apply(const float p[3]) const {
        Vec3f r;
        r.x = b[0][0] * p[0] + b[1][0] * p[1] + b[2][0] * p[2] + o[0];
        r.y = b[0][1] * p[0] + b[1][1] * p[1] + b[2][1] * p[2] + o[1];
        r.z = b[0][2] * p[0] + b[1][2] * p[1] + b[2][2] * p[2] + o[2];
        return r;
    }
};

Mat rotation_roll_pitch_yaw(float roll, float pitch, float yaw) {
    const float cr = std::cos(roll), sr = std::sin(roll);
    const float cp = std::cos(pitch), sp = std::sin(pitch);
    const float cy = std::cos(yaw), sy = std::sin(yaw);

    const float rz[3][3] = {{cr, -sr, 0}, {sr, cr, 0}, {0, 0, 1}};
    const float ry[3][3] = {{cp, 0, sp}, {0, 1, 0}, {-sp, 0, cp}};
    const float rx[3][3] = {{1, 0, 0}, {0, cy, -sy}, {0, sy, cy}};

    auto mul = [](const float a[3][3], const float b[3][3], float out[3][3]) {
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j) {
                out[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j];
            }
        }
    };

    float tmp[3][3], out[3][3];
    mul(ry, rx, tmp);
    mul(rz, tmp, out);

    Mat m;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) m.b[i][j] = out[i][j];
    return m;
}

// Number of operand words for the instruction whose opcode is at `ip - 1`.
std::size_t operand_size(const std::vector<std::uint16_t>& w, std::size_t ip) {
    const std::uint16_t op = w[ip - 1];
    switch (op) {
        // No operands.
        case 0x00: case 0x54: case 0x74: case 0x84: case 0x8B: case 0x8C:
        case 0xA8:
            return 0;
        // One operand.
        case 0x07: case 0x08: case 0x30: case 0x37: case 0x48: case 0x64:
        case 0x65: case 0x66: case 0x75: case 0x76: case 0x7B: case 0x7C:
        case 0x85: case 0x87: case 0x93: case 0x98: case 0xA3:
            return 1;
        // Two operands.
        case 0x20: case 0x21: case 0x22: case 0x24: case 0x25: case 0x26:
        case 0x27: case 0x2D: case 0x33: case 0x3F: case 0x40: case 0x41:
        case 0x42: case 0x43: case 0x45: case 0x4B: case 0x4C: case 0x4E:
        case 0x51: case 0x61: case 0x67: case 0x68: case 0x69: case 0x6B:
        case 0x6C: case 0x6D: case 0x86: case 0x94: case 0x97:
            return 2;
        // Three operands.
        case 0x01: case 0x34: case 0x4D: case 0x52: case 0x5C: case 0x63:
        case 0x6E: case 0x6F: case 0x70: case 0x71: case 0x80: case 0x95:
        case 0x96: case 0x9D: case 0xA7:
            return 3;
        // Four operands.
        case 0x02: case 0x15: case 0x39: case 0x4F: case 0x5A: case 0x5D:
        case 0x62: case 0x6A: case 0x72: case 0x81: case 0x83: case 0x9E:
            return 4;
        // Five / six / nine / eleven operands.
        case 0x03: case 0x38: case 0x7D: case 0xA2: return 5;
        case 0x1B: case 0x3E: case 0x7E: return 6;
        case 0x1F: case 0x77: return 9;
        case 0x49: return 11;
        case 0x1A: return 5;
        // "<count> then <count> indices".
        case 0x04: case 0x2F: case 0x31: case 0x32: case 0x73: case 0x88:
        case 0x8D: case 0x8E:
            return 1 + w[ip];
        // "<texture> <count> then <count> * (u, v)".
        case 0x2E: case 0x47:
            return 2 + 2 * std::size_t(w[ip + 1]);
        // "<count> <base> then <count> * (x, y, z)".
        case 0x53:
            return 2 + 3 * std::size_t(w[ip]);
        case 0x5F:
            return 1 + 3 * std::size_t(w[ip]);
        case 0x60:
            return 1 + 4 * std::size_t(w[ip]);
        case 0x7F:
            return 2 + 4 * std::size_t(w[ip + 1]);
        // "<bytes> <surface> <type>" plus type-dependent material data.
        case 0x9F: {
            const std::uint16_t type = w[ip + 2];
            const std::size_t extra = type == 1 ? 3 : type == 2 ? 4 : type == 3 ? 8 : 0;
            return 3 + extra;
        }
        default:
            throw std::runtime_error("unknown .3 opcode");
    }
}

// Axes each delta-write opcode updates, in operand order.
struct DeltaSpec {
    int axes[3];
    int count;
    bool with_index;
};

DeltaSpec delta_spec(std::uint16_t op) {
    switch (op) {
        case 0x63: return {{0, 1, 2}, 3, false};
        case 0x64: return {{0, 0, 0}, 1, false};
        case 0x65: return {{1, 0, 0}, 1, false};
        case 0x66: return {{2, 0, 0}, 1, false};
        case 0x67: return {{0, 1, 0}, 2, false};
        case 0x68: return {{0, 2, 0}, 2, false};
        case 0x69: return {{1, 2, 0}, 2, false};
        case 0x6A: return {{0, 1, 2}, 3, true};
        case 0x6B: return {{0, 0, 0}, 1, true};
        case 0x6C: return {{1, 0, 0}, 1, true};
        case 0x6D: return {{2, 0, 0}, 1, true};
        case 0x6E: return {{0, 1, 0}, 2, true};
        case 0x6F: return {{0, 2, 0}, 2, true};
        case 0x70: return {{1, 2, 0}, 2, true};
        default: return {{0, 0, 0}, 0, false};
    }
}

struct Routine {
    std::optional<std::size_t> return_ip;
    Mat matrix;
    bool sprite = false;
};

}  // namespace

bool Mesh::bounds(Vec3f& lo, Vec3f& hi) const {
    bool any = false;
    for (const auto& t : triangles) {
        if (t.sprite) continue;
        for (const Vec3f* p : {&t.a, &t.b, &t.c}) {
            if (!any) {
                lo = hi = *p;
                any = true;
            } else {
                lo.x = std::min(lo.x, p->x); lo.y = std::min(lo.y, p->y); lo.z = std::min(lo.z, p->z);
                hi.x = std::max(hi.x, p->x); hi.y = std::max(hi.y, p->y); hi.z = std::max(hi.z, p->z);
            }
        }
    }
    return any;
}

std::size_t Mesh::triangle_count(bool include_sprites) const {
    if (include_sprites) return triangles.size();
    return static_cast<std::size_t>(
        std::count_if(triangles.begin(), triangles.end(),
                      [](const MeshTriangle& t) { return !t.sprite; }));
}

Shape Shape::parse(std::span<const std::uint8_t> data) {
    if (data.size() < 80) throw std::runtime_error(".3 file too small");

    Shape shape;
    const std::size_t n = data.size() / 2;
    shape.words_.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        shape.words_[i] = static_cast<std::uint16_t>((data[2 * i] << 8) | data[2 * i + 1]);
    }
    const auto& w = shape.words_;

    // LOD distances, terminated by 0x7FFF.
    int minimal = s16(w[0]);
    int maximal = s16(w[1]);
    shape.lods_.push_back(ShapeLod{minimal, maximal, 0, 0, 0});
    std::size_t i = 2;
    while (i < 32 && w[i] != 0 && w[i] != 0x7FFF && shape.lods_.size() < 4) {
        shape.lods_.push_back(ShapeLod{maximal, s16(w[i]), 0, 0, 0});
        maximal = s16(w[i]);
        ++i;
    }
    while (i < 32 && w[i] != 0x7FFF) ++i;

    shape.rendering_priority_ = s16(w[8]);
    shape.class_id_ = w[16];

    if ((w[28] & 0xFF00) == 0x2100 || (w[28] & 0xFF00) == 0x3100) {
        const char chars[8] = {
            static_cast<char>(w[28] & 0xFF), static_cast<char>(w[29] >> 8),
            static_cast<char>(w[29] & 0xFF), static_cast<char>(w[30] >> 8),
            static_cast<char>(w[30] & 0xFF), static_cast<char>(w[31] >> 8),
            static_cast<char>(w[31] & 0xFF), static_cast<char>(w[32] >> 8)};
        std::string name(chars, 8);
        const auto zero = name.find('\0');
        if (zero != std::string::npos) name.resize(zero);
        shape.texture_name_ = name;
    }

    const bool tfx2 = (shape.class_id_ == 0x0000 || shape.class_id_ == 0x0004 ||
                       shape.class_id_ == 0x0020);
    for (std::size_t k = 0; k < shape.lods_.size(); ++k) {
        std::size_t target = 0;
        if (tfx2) {
            const std::size_t cursor = 17 + k;
            if (cursor >= w.size()) throw std::runtime_error(".3 LOD table truncated");
            target = cursor + w[cursor] / 2;
        } else {
            if (17 + k >= w.size()) throw std::runtime_error(".3 LOD table truncated");
            target = 16 + 2 * std::size_t(w[17 + k]);
        }
        if (target >= w.size()) throw std::runtime_error(".3 LOD out of range");
        shape.lods_[k].xyz_limit = w[target];
        shape.lods_[k].begin = target + 1;
    }

    for (auto& lod : shape.lods_) {
        std::size_t next = w.size();
        for (const auto& other : shape.lods_) {
            if (other.begin > lod.begin) next = std::min(next, other.begin);
        }
        lod.end = next;
    }
    return shape;
}

std::optional<std::size_t> Shape::verify(std::size_t lod) const {
    if (lod >= lods_.size()) return std::nullopt;
    const auto& w = words_;
    std::size_t ip = lods_[lod].begin;
    try {
        while (ip < lods_[lod].end) {
            const std::uint16_t op = w[ip++];
            if (op == 0xFFFF) return ip - 1;
            ip += operand_size(w, ip);
            if (ip > lods_[lod].end) return std::nullopt;
        }
    } catch (const std::exception&) {
        return std::nullopt;
    }
    return std::nullopt;
}

Mesh execute_shape(const Shape& shape, std::size_t lod,
                   const ShapeParameters& parameters, int viewer_distance,
                   int time_of_day) {
    if (lod >= shape.lods().size()) throw std::runtime_error("shape LOD out of range");

    const auto& w = shape.words();
    const auto& program = shape.lods()[lod];
    Mesh mesh;

    std::vector<std::array<float, 3>> raw(kMaxXyz, {0.0f, 0.0f, 0.0f});
    std::size_t last_raw = 0;
    std::array<std::uint8_t, kMaxPolygonVertices> colors{};
    std::vector<std::pair<int, int>> uvs;
    int current_texture = 0;
    bool textured = false;
    float scale = 1.0f;
    int current_parameter = parameters[0];

    std::vector<Routine> stack;
    stack.push_back(Routine{std::nullopt, Mat{}, false});

    auto emit = [&](const std::vector<std::uint16_t>& indices,
                    const std::vector<int>& color_slots, bool decal = false) {
        const Routine& r = stack.back();
        std::vector<Vec3f> pts;
        pts.reserve(indices.size());
        for (std::uint16_t idx : indices) {
            if (idx >= kMaxXyz) return;
            pts.push_back(r.matrix.apply(raw[idx].data()));
        }
        if (pts.size() < 3) return;
        for (std::size_t k = 1; k + 1 < pts.size(); ++k) {
            MeshTriangle t;
            t.a = pts[0];
            t.b = pts[k];
            t.c = pts[k + 1];
            t.textured = textured;
            t.sprite = r.sprite;
            t.decal = decal;
            const auto c0 = static_cast<std::uint8_t>(colors[static_cast<std::size_t>(color_slots[0])]);
            const auto c1 = static_cast<std::uint8_t>(colors[static_cast<std::size_t>(color_slots[k])]);
            const auto c2 = static_cast<std::uint8_t>(colors[static_cast<std::size_t>(color_slots[k + 1])]);
            t.colors[0] = c0;
            t.colors[1] = c1;
            t.colors[2] = c2;
            mesh.triangles.push_back(t);
        }
    };

    auto branch_call = [&](std::size_t& ip, std::size_t offset_index,
                           std::size_t return_index) {
        const auto target = offset_index + static_cast<std::size_t>(s16(w[offset_index]) / 2);
        Routine child = stack.back();
        child.return_ip = return_index;
        stack.push_back(child);
        ip = target;
    };

    const bool tracing = std::getenv("EF_SHAPE_TRACE") != nullptr;
    std::map<std::uint16_t, std::size_t> counts;
    std::map<std::uint16_t, std::size_t>* trace = tracing ? &counts : nullptr;

    std::size_t ip = program.begin;
    std::size_t steps = 0;
    const std::size_t budget = 4'000'000;

    while (true) {
        if (++steps > budget) throw std::runtime_error(".3 instruction budget exceeded");
        if (ip >= program.end) throw std::runtime_error(".3 bytecode overrun");

        const std::uint16_t op = w[ip++];
        if (trace) ++(*trace)[op];

        if (op == 0xFFFF) break;
        if (op == 0x00) {
            const auto return_ip = stack.back().return_ip;
            stack.pop_back();
            if (!return_ip || stack.empty()) break;
            ip = *return_ip;
            continue;
        }
        if (op == 0x07) { ip += static_cast<std::size_t>(s16(w[ip]) / 2); continue; }
        if (op == 0x08) {
            const auto target = ip + static_cast<std::size_t>(s16(w[ip]) / 2);
            Routine child = stack.back();
            child.return_ip = ip + 1;
            stack.push_back(child);
            ip = target;
            continue;
        }
        if (op == 0x15) { ip += 3 + static_cast<std::size_t>(s16(w[ip + 3]) / 2); continue; }

        if (op == 0x20 || op == 0x21 || op == 0x22) {
            const int reference = w[ip];
            const auto offset = static_cast<std::size_t>(s16(w[ip + 1]) / 2);
            const bool take = (op == 0x20) ? current_parameter < reference
                            : (op == 0x21) ? current_parameter == reference
                                           : current_parameter > reference;
            ip += take ? 1 + offset : 2;
            continue;
        }
        if (op == 0x24 || op == 0x25) {
            const int reference = w[ip];
            const bool take = (op == 0x24) ? current_parameter == reference
                                           : current_parameter > reference;
            if (take) {
                branch_call(ip, ip + 1, ip + 2);
            } else {
                ip += 2;
            }
            continue;
        }
        if (op == 0x26 || op == 0x27) {
            const int threshold = w[ip];
            const auto offset = static_cast<std::size_t>(s16(w[ip + 1]) / 2);
            const bool take = (op == 0x26) ? viewer_distance <= threshold
                                           : viewer_distance >= threshold;
            ip += take ? 1 + offset : 2;
            continue;
        }
        if (op == 0x3F || op == 0x40 || op == 0x42) {
            const int time = w[ip];
            const int mask = 1 << time_of_day;
            const bool take = (op == 0x3F) ? time == mask : time > mask;
            if (take) {
                branch_call(ip, ip + 1, ip + 2);
            } else {
                ip += 2;
            }
            continue;
        }
        if (op == 0x43 || op == 0x45) {
            const int time = w[ip];
            const auto offset = static_cast<std::size_t>(s16(w[ip + 1]) / 2);
            ip += (time > (1 << time_of_day)) ? 1 + offset : 2;
            continue;
        }
        if (op == 0x94) {
            branch_call(ip, ip + 1, ip + 2);
            continue;
        }
        if (op == 0x2D) {
            const int position_index = w[ip];
            const auto offset = static_cast<std::size_t>(s16(w[ip + 1]) / 2);
            Routine child = stack.back();
            child.return_ip = ip + 2;
            const auto& t = raw[static_cast<std::size_t>(position_index)];
            child.matrix.o[0] += t[0];
            child.matrix.o[1] += t[1];
            child.matrix.o[2] += t[2];
            stack.push_back(child);
            ip += 1 + offset;
            continue;
        }
        if (op == 0x48) {
            const int index = w[ip++];
            current_parameter = index < static_cast<int>(parameters.size()) ? parameters[index] : 0;
            continue;
        }
        if (op == 0x41 || op == 0x76 || op == 0x7B || op == 0x7C) {
            ip += static_cast<std::size_t>(s16(w[ip]) / 2);
            continue;
        }
        if (op == 0x87 || op == 0x93) {
            const auto offset = static_cast<std::size_t>(s16(w[ip]) / 2);
            Routine child = stack.back();
            child.return_ip = ip + 1;
            stack.push_back(child);
            ip += offset;
            continue;
        }
        if (op == 0x1F || op == 0x77) {
            auto& r = stack.back();
            r.matrix = Mat{};
            r.matrix.b[0][0] = r.matrix.b[1][1] = r.matrix.b[2][2] = scale;
            r.sprite = true;
            ip += 9;
            continue;
        }
        if (op == 0x4B) {
            const auto offset = static_cast<std::size_t>(s16(w[ip + 1]) / 2);
            Routine child = stack.back();
            child.return_ip = ip + 2;
            child.sprite = true;
            child.matrix = Mat{};
            child.matrix.b[0][0] = child.matrix.b[1][1] = child.matrix.b[2][2] = scale;
            stack.push_back(child);
            ip = ip + 1 + offset;
            continue;
        }
        if (op == 0x49) {
            const float k = 6.283185307179586f / 65536.0f;
            const float roll = static_cast<float>(w[ip]) * k;
            const float pitch = static_cast<float>(w[ip + 1]) * k;
            const float yaw = static_cast<float>(w[ip + 2]) * k;
            const int position_index = w[ip + 9];
            const auto offset = static_cast<std::size_t>(s16(w[ip + 10]) / 2);
            const Mat rotation = rotation_roll_pitch_yaw(roll, pitch, yaw);

            Routine child = stack.back();
            child.return_ip = ip + 11;
            Mat combined = child.matrix;
            for (int i = 0; i < 3; ++i) {
                for (int j = 0; j < 3; ++j) {
                    combined.b[i][j] = rotation.b[i][0] * child.matrix.b[0][j] +
                                       rotation.b[i][1] * child.matrix.b[1][j] +
                                       rotation.b[i][2] * child.matrix.b[2][j];
                }
            }
            const auto& t = raw[static_cast<std::size_t>(position_index)];
            combined.o[0] += t[0];
            combined.o[1] += t[1];
            combined.o[2] += t[2];
            child.matrix = combined;
            stack.push_back(child);
            ip += 10 + offset;
            continue;
        }

        // Vertex writers.
        if (op == 0x53) {
            const int count = w[ip];
            const int base = w[ip + 1];
            std::size_t cursor = ip + 2;
            for (int k = 0; k < count; ++k) {
                const auto index = static_cast<std::size_t>(base + k);
                if (index >= kMaxXyz) break;
                raw[index] = {static_cast<float>(s16(w[cursor])),
                              static_cast<float>(s16(w[cursor + 1])),
                              static_cast<float>(s16(w[cursor + 2]))};
                cursor += 3;
            }
            ip = cursor;
            last_raw = static_cast<std::size_t>(base + count - 1);
            continue;
        }
        if (op == 0x62) {
            const auto index = static_cast<std::size_t>(w[ip]);
            if (index < kMaxXyz) {
                raw[index] = {static_cast<float>(s16(w[ip + 1])),
                              static_cast<float>(s16(w[ip + 2])),
                              static_cast<float>(s16(w[ip + 3]))};
            }
            last_raw = index;
            ip += 4;
            continue;
        }
        if (op >= 0x63 && op <= 0x70) {
            const DeltaSpec spec = delta_spec(op);
            const std::size_t size = static_cast<std::size_t>(spec.count) + (spec.with_index ? 1 : 0);
            std::array<float, 3> next = raw[last_raw];
            for (int k = 0; k < spec.count; ++k) {
                const int axis = spec.axes[k];
                next[static_cast<std::size_t>(axis)] += static_cast<float>(s16(w[ip + static_cast<std::size_t>(k)]));
            }
            const auto index = spec.with_index ? static_cast<std::size_t>(w[ip + size - 1])
                                               : last_raw + 1;
            ip += size;
            if (index < kMaxXyz) raw[index] = next;
            last_raw = index;
            continue;
        }
        if (op == 0x7F) {
            const int count = w[ip + 1];
            std::size_t cursor = ip + 2;
            for (int k = 0; k < count; ++k) {
                const auto index = static_cast<std::size_t>(w[cursor]);
                if (index < kMaxXyz) {
                    raw[index] = {static_cast<float>(s16(w[cursor + 1])),
                                  static_cast<float>(s16(w[cursor + 2])),
                                  static_cast<float>(s16(w[cursor + 3]))};
                }
                last_raw = index;
                cursor += 4;
            }
            ip = cursor;
            continue;
        }
        if (op == 0x61) { ip += 2; continue; }

        // State.
        if (op == 0x30) { scale = static_cast<float>(w[ip]); ++ip; continue; }
        if (op == 0x31) {
            const int count = w[ip];
            for (int k = 0; k < count && k < static_cast<int>(colors.size()); ++k) {
                colors[static_cast<std::size_t>(k)] = static_cast<std::uint8_t>(w[ip + 1 + static_cast<std::size_t>(k)]);
            }
            ip += 1 + static_cast<std::size_t>(count);
            continue;
        }
        if (op == 0x2E || op == 0x47) {
            current_texture = w[ip];
            const int count = w[ip + 1];
            uvs.clear();
            for (int k = 0; k < count; ++k) {
                uvs.emplace_back(w[ip + 2 + 2 * static_cast<std::size_t>(k)],
                                 w[ip + 3 + 2 * static_cast<std::size_t>(k)]);
            }
            textured = true;
            ip += 2 + 2 * static_cast<std::size_t>(count);
            continue;
        }
        if (op == 0x83) { ip += 4; continue; }
        if (op == 0x84) { continue; }
        if (op == 0x4C) {
            colors[0] = static_cast<std::uint8_t>(w[ip] & 0xFF);
            colors[1] = static_cast<std::uint8_t>(w[ip] >> 8);
            colors[2] = static_cast<std::uint8_t>(w[ip + 1] & 0xFF);
            ip += 2;
            continue;
        }
        if (op == 0x4E) {
            colors[0] = static_cast<std::uint8_t>(w[ip] & 0xFF);
            colors[1] = static_cast<std::uint8_t>(w[ip] >> 8);
            colors[2] = static_cast<std::uint8_t>(w[ip + 1] & 0xFF);
            colors[3] = static_cast<std::uint8_t>(w[ip + 1] >> 8);
            ip += 2;
            continue;
        }
        if (op == 0x37 || op == 0x98) { ++ip; continue; }
        if (op == 0xA7) { ip += 3; continue; }
        if (op == 0xA8) { continue; }

        // Geometry.
        if (op == 0x02 || op == 0x5A) {
            const auto color = static_cast<std::uint8_t>(w[ip]);
            colors[0] = colors[1] = colors[2] = color;
            emit({w[ip + 1], w[ip + 2], w[ip + 3]}, {0, 1, 2});
            ip += 4;
            continue;
        }
        if (op == 0x03) {
            const auto color = static_cast<std::uint8_t>(w[ip]);
            colors[0] = colors[1] = colors[2] = colors[3] = color;
            emit({w[ip + 1], w[ip + 2], w[ip + 3], w[ip + 4]}, {0, 1, 2, 3});
            ip += 5;
            continue;
        }
        if (op == 0x04) {
            const auto color = static_cast<std::uint8_t>(w[ip]);
            const int count = w[ip + 1];
            std::vector<std::uint16_t> indices;
            std::vector<int> slots;
            for (int k = 0; k < count; ++k) {
                indices.push_back(w[ip + 2 + static_cast<std::size_t>(k)]);
                slots.push_back(k);
                if (k < static_cast<int>(colors.size())) colors[static_cast<std::size_t>(k)] = color;
            }
            emit(indices, slots);
            ip += 2 + static_cast<std::size_t>(count);
            continue;
        }
        if (op == 0x01) { ip += 3; continue; }
        if (op == 0x4D || op == 0x80 || op == 0x71 || op == 0x9D) {
            emit({w[ip], w[ip + 1], w[ip + 2]}, {0, 1, 2});
            ip += 3;
            continue;
        }
        if (op == 0x4F || op == 0x81 || op == 0x72 || op == 0x9E) {
            emit({w[ip], w[ip + 1], w[ip + 2], w[ip + 3]}, {0, 1, 2, 3});
            ip += 4;
            continue;
        }
        if (op == 0x32 || op == 0x73) {
            const int count = w[ip];
            std::vector<std::uint16_t> indices;
            std::vector<int> slots;
            for (int k = 0; k < count; ++k) {
                indices.push_back(w[ip + 1 + static_cast<std::size_t>(k)]);
                slots.push_back(k);
            }
            emit(indices, slots);
            ip += 1 + static_cast<std::size_t>(count);
            continue;
        }
        if (op == 0x2F || op == 0x88 || op == 0x8E || op == 0x8D) {
            const int count = w[ip];
            std::vector<std::uint16_t> indices;
            std::vector<int> slots;
            for (int k = 0; k < count; ++k) {
                indices.push_back(w[ip + 1 + static_cast<std::size_t>(k)]);
                slots.push_back(k);
            }
            emit(indices, slots, op != 0x2F);
            ip += 1 + static_cast<std::size_t>(count);
            continue;
        }

        ip += operand_size(w, ip);
    }

    (void)current_texture;
    if (tracing) {
        std::fprintf(stderr, "shape opcode usage:\n");
        for (const auto& [op, n] : counts) std::fprintf(stderr, "  0x%02X: %zu\n", op, n);
    }
    return mesh;
}

}  // namespace ef
