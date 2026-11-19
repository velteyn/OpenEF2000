// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 velteyn

#include "render.h"

#include <set>
#include <tuple>

#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

namespace ef {
namespace {

constexpr const char* kFontPath =
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";
constexpr double kPi = 3.14159265358979323846;

struct Canvas {
    SDL_Surface* s = nullptr;
    int w = 0, h = 0;
    bool locked = false;

    ~Canvas() {
        if (s) SDL_FreeSurface(s);
    }

    void lock() {
        if (!locked) {
            SDL_LockSurface(s);
            locked = true;
        }
    }
    void unlock() {
        if (locked) {
            SDL_UnlockSurface(s);
            locked = false;
        }
    }

    Uint32 rgb(int r, int g, int b, int a = 255) const {
        return SDL_MapRGBA(s->format, static_cast<Uint8>(r),
                           static_cast<Uint8>(g), static_cast<Uint8>(b),
                           static_cast<Uint8>(a));
    }

    void fill(int x, int y, int ww, int hh, Uint32 c) {
        SDL_Rect r{x, y, ww, hh};
        SDL_FillRect(s, &r, c);
    }

    void pixel(int x, int y, Uint32 c) {
        if (!locked || x < 0 || y < 0 || x >= w || y >= h) return;
        auto* row = reinterpret_cast<Uint32*>(
            static_cast<Uint8*>(s->pixels) + y * s->pitch);
        row[x] = c;
    }

    void line(int x0, int y0, int x1, int y1, Uint32 c) {
        int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
        int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
        int err = dx + dy;
        while (true) {
            pixel(x0, y0, c);
            if (x0 == x1 && y0 == y1) break;
            int e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
    }

    //! Outline circle with a segment count that keeps large radii smooth.
    void ring(int cx, int cy, int r, Uint32 c) {
        if (r < 1) return;
        const int segs = std::clamp(r, 16, 180);
        for (int i = 0; i < segs; ++i) {
            const double a0 = (2.0 * kPi * i) / segs;
            const double a1 = (2.0 * kPi * (i + 1)) / segs;
            line(cx + static_cast<int>(std::lround(r * std::cos(a0))),
                 cy + static_cast<int>(std::lround(r * std::sin(a0))),
                 cx + static_cast<int>(std::lround(r * std::cos(a1))),
                 cy + static_cast<int>(std::lround(r * std::sin(a1))), c);
        }
    }

    void circle(int cx, int cy, int r, Uint32 c) {
        for (int i = 0; i < 16; ++i) {
            double a0 = (2.0 * kPi * i) / 16.0;
            double a1 = (2.0 * kPi * (i + 1)) / 16.0;
            line(cx + static_cast<int>(std::lround(r * std::cos(a0))),
                 cy + static_cast<int>(std::lround(r * std::sin(a0))),
                 cx + static_cast<int>(std::lround(r * std::cos(a1))),
                 cy + static_cast<int>(std::lround(r * std::sin(a1))), c);
        }
    }
};

//! Ground threats that the planner's coverage overlays draw rings around.
//! Classification is by type name; the ring radii are *assumed* because the
//! shipped files carry no range per unit, and are scaled by
//! RenderOptions::coverage_scale so the assumption is visible and adjustable.
enum class Threat { none, aaa, sam, ewr };

Threat classify_threat(const std::string& type) {
    const auto has = [&](const char* needle) {
        return type.find(needle) != std::string::npos;
    };
    if (has("MARCONI") || has("EWR")) return Threat::ewr;
    if (has("PATRIOT") || has("SA_6") || has("SA_11") || has("SA_13") ||
        has("RAPIER") || has("ROLAND") || has("BUK") || has("KUB")) {
        return Threat::sam;
    }
    if (has("SHILKA") || has("ZSU") || has("VULCAN")) return Threat::aaa;
    return Threat::none;
}

constexpr int kAaaRangeCells = 8;
constexpr int kSamRangeCells = 25;
constexpr int kEwrRangeCells = 60;

SDL_Color side_color(const std::string& side) {
    if (side == "NATO") return SDL_Color{90, 155, 255, 255};
    if (side == "RUSSIAN") return SDL_Color{240, 95, 95, 255};
    if (side == "SWEDISH") return SDL_Color{245, 210, 95, 255};
    if (side == "FINNISH") return SDL_Color{110, 220, 160, 255};
    if (side == "BALTIC") return SDL_Color{120, 220, 230, 255};
    return SDL_Color{190, 190, 190, 255};
}

void draw_text(Canvas& c, TTF_Font* font, int x, int y, const std::string& text,
               SDL_Color color) {
    if (!font || text.empty()) return;
    SDL_Surface* ts = TTF_RenderUTF8_Blended(font, text.c_str(), color);
    if (!ts) return;
    SDL_Surface* conv = SDL_ConvertSurface(ts, c.s->format, 0);
    SDL_FreeSurface(ts);
    if (!conv) return;
    SDL_Rect dst{x, y, 0, 0};
    SDL_BlitSurface(conv, nullptr, c.s, &dst);
    SDL_FreeSurface(conv);
}

std::string mission_label(const Mission& m, int idx) {
    std::ostringstream os;
    os << "#" << idx << "  " << m.type << " " << m.side << " " << m.role;
    return os.str();
}

}  // namespace

unsigned map_side_bits(const std::string& side) {
    if (side == "NATO") return MapSideNato;
    if (side == "RUSSIAN") return MapSideRussian;
    if (side == "SWEDISH" || side == "FINNISH" || side == "BALTIC" ||
        side == "NEUTRAL") {
        return MapSideNeutral;
    }
    // An unknown actor must not disappear when a filter is applied.
    return MapSideAll;
}

unsigned map_layer_bits(const std::string& type, const std::string& role) {
    const auto has = [&](const char* needle) {
        return type.find(needle) != std::string::npos;
    };
    if (role == "AIR_SUPPORT") return MapLayerAirSupport;
    if (has("MARCONI") || has("EWR") || has("RADAR") || has("WARNING"))
        return MapLayerComms;
    if (has("TANKER") || has("SHIP") || has("TYPE42") || has("NEUTRASHIMY") ||
        has("NAVAL") || has("PORT") || has("VESSEL")) {
        return MapLayerNavy;
    }
    if (role == "SUPPLY" || has("SUPPLY") || has("DEPOT") || has("LOGISTIC"))
        return MapLayerSupply;
    if (has("POL_") || has("POLITICAL") || has("INDUSTRIAL") || has("OIL") ||
        has("REFINERY") || has("POWER")) {
        return MapLayerPolitical;
    }
    if (has("AIRFIELD") || has("AIR_BASE") || has("AIRBASE"))
        return MapLayerAirSupport;
    if (has("PATRIOT") || has("SA_") || has("RAPIER") || has("ROLAND") ||
        has("BUK") || has("KUB") || has("VULCAN") || has("SHILKA") ||
        has("ZSU") || has("SAM") || has("AAA")) {
        return MapLayerAirDefence;
    }
    // Choke points and unknown site kinds stay visible under every layer.
    return MapLayerAll;
}

int MapProjection::world_x(int sx) const {
    const double span = std::max(1, right - left);
    return static_cast<int>(std::lround(
        world_min_x + (sx - left) * static_cast<double>(world_max_x - world_min_x) / span));
}

int MapProjection::world_y(int sy) const {
    const double span = std::max(1, bottom - top);
    return static_cast<int>(std::lround(
        world_max_y - (sy - top) * static_cast<double>(world_max_y - world_min_y) / span));
}

int MapProjection::screen_x(int x) const {
    const double span = std::max(1, world_max_x - world_min_x);
    return static_cast<int>(std::lround(
        left + (x - world_min_x) * static_cast<double>(right - left) / span));
}

int MapProjection::screen_y(int y) const {
    const double span = std::max(1, world_max_y - world_min_y);
    return static_cast<int>(std::lround(
        bottom - (y - world_min_y) * static_cast<double>(bottom - top) / span));
}

bool MapProjection::contains(int sx, int sy) const {
    return sx >= left && sx < right && sy >= top && sy < bottom;
}

namespace {

struct MapLayout {
    int w = 0, h = 0, left = 0, top = 0, right = 0, bottom = 0;
};

MapLayout map_layout(const RenderOptions& opt) {
    MapLayout l;
    l.w = std::max(opt.bare ? 16 : 640, opt.width);
    l.h = std::max(opt.bare ? 16 : 480, opt.height);
    const int panel_w = opt.bare ? 0 : 380;
    l.left = opt.bare ? 0 : 55;
    l.top = opt.bare ? 0 : 55;
    l.right = opt.bare ? l.w : l.w - panel_w - 20;
    l.bottom = opt.bare ? l.h : l.h - 55;
    return l;
}

struct MapExtent {
    int minx = 0, miny = 0, maxx = 100, maxy = 100;
};

MapExtent map_extent(const Dataset& ds, const RenderOptions& opt) {
    const int sel = std::clamp(opt.selected, 0,
                               std::max(0, static_cast<int>(ds.missions.size()) - 1));

    // Collect every coordinate, then take a robust extent: a handful of
    // records carry wildly out-of-range values, and a raw min/max would shrink
    // the whole theatre to a dot.
    std::vector<int> xs, ys;
    auto add = [&](const Vec3& p) {
        xs.push_back(p.x);
        ys.push_back(p.y);
    };
    auto add_mission = [&](const Mission& m) {
        if (m.pos) add(*m.pos);
        for (const auto& g : m.groups) if (g.pos) add(*g.pos);
        for (const auto& f : m.flights) if (f.pos) add(*f.pos);
        for (const auto& t : m.targets) {
            if (t.pos) add(*t.pos);
            for (const auto& a : t.areas) if (a.pos) add(*a.pos);
            // Attack-pattern nodes are local target geometry (thousands of
            // units around the aim point), not map positions. Including them
            // would collapse the theatre to a dot.
        }
        for (const auto& r : m.routes)
            for (const auto& w : r.waypoints) add(w.p);
    };
    if (opt.focus && !ds.missions.empty()) {
        add_mission(ds.missions[sel]);
    } else {
        for (const auto& m : ds.missions) add_mission(m);
    }

    MapExtent e;
    // Bases are deliberately excluded from the extent. They come from a
    // different data set with its own coordinate space, and including them
    // collapses the theatre to a dot.
    if (opt.bounds) {
        e.minx = (*opt.bounds)[0]; e.miny = (*opt.bounds)[1];
        e.maxx = (*opt.bounds)[2]; e.maxy = (*opt.bounds)[3];
    } else if (!xs.empty()) {
        e.minx = *std::min_element(xs.begin(), xs.end());
        e.maxx = *std::max_element(xs.begin(), xs.end());
        e.miny = *std::min_element(ys.begin(), ys.end());
        e.maxy = *std::max_element(ys.begin(), ys.end());
    }
    if (e.maxx - e.minx < 1) e.maxx = e.minx + 1;
    if (e.maxy - e.miny < 1) e.maxy = e.miny + 1;

    // Coverage rings extend beyond their unit, so widen the view to contain
    // the largest radius when an overlay is on.
    if (opt.sam_coverage || opt.ewr_coverage) {
        const int ring = static_cast<int>(std::lround(
            (opt.ewr_coverage ? kEwrRangeCells : kSamRangeCells) * opt.coverage_scale));
        e.minx -= ring; e.maxx += ring; e.miny -= ring; e.maxy += ring;
    }

    const int padx = std::max(1, (e.maxx - e.minx) / 20);
    const int pady = std::max(1, (e.maxy - e.miny) / 20);
    e.minx -= padx; e.maxx += padx; e.miny -= pady; e.maxy += pady;

    // A focused mission with only a waypoint or two would otherwise zoom to a
    // dot; keep enough surrounding theatre in view to give context.
    if (opt.focus) {
        const int minimum = 160;
        if (e.maxx - e.minx < minimum) {
            const int cx = (e.minx + e.maxx) / 2;
            e.minx = cx - minimum / 2;
            e.maxx = cx + minimum / 2;
        }
        if (e.maxy - e.miny < minimum) {
            const int cy = (e.miny + e.maxy) / 2;
            e.miny = cy - minimum / 2;
            e.maxy = cy + minimum / 2;
        }
    }

    // Zoom keeps the centre of the current extent and shrinks the half-size,
    // which is what the planner's z-In/z-Out controls expect.
    if (opt.zoom > 1.0f) {
        const int cx = (e.minx + e.maxx) / 2, cy = (e.miny + e.maxy) / 2;
        const int hw = std::max(1, static_cast<int>((e.maxx - e.minx) / (2.0f * opt.zoom)));
        const int hh = std::max(1, static_cast<int>((e.maxy - e.miny) / (2.0f * opt.zoom)));
        e.minx = cx - hw; e.maxx = cx + hw; e.miny = cy - hh; e.maxy = cy + hh;
    }

    // Drag-to-pan shifts the visible window over the theatre.
    e.minx += opt.pan_x; e.maxx += opt.pan_x;
    e.miny += opt.pan_y; e.maxy += opt.pan_y;
    return e;
}

}  // namespace

MapProjection map_projection(const Dataset& ds, const RenderOptions& opt) {
    const MapLayout l = map_layout(opt);
    const MapExtent e = map_extent(ds, opt);
    MapProjection p;
    p.world_min_x = e.minx; p.world_min_y = e.miny;
    p.world_max_x = e.maxx; p.world_max_y = e.maxy;
    p.left = l.left; p.top = l.top; p.right = l.right; p.bottom = l.bottom;
    return p;
}

SDL_Surface* render_map(const Dataset& ds, const RenderOptions& opt) {
    const MapLayout layout = map_layout(opt);
    const int W = layout.w, H = layout.h;
    const int left = layout.left, top = layout.top;
    const int right = layout.right, bottom = layout.bottom;

    Canvas c;
    c.s = SDL_CreateRGBSurfaceWithFormat(0, W, H, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!c.s) return nullptr;
    c.w = W; c.h = H;
    // The original's `m` cycle alternates a dark relief map (modes 0/3) with
    // a light political map that labels places (modes 1/2).
    const bool political = (opt.map_mode == 1 || opt.map_mode == 2);
    c.fill(0, 0, W, H,
           political ? c.rgb(150, 156, 164) : c.rgb(10, 16, 24));
    if (!opt.bare) {
        c.fill(right + 10, 0, 380, H,
               political ? c.rgb(132, 138, 148) : c.rgb(18, 24, 34));
        c.fill(0, H - 36, W, 36,
               political ? c.rgb(120, 126, 136) : c.rgb(14, 20, 30));
    }

    const int sel = std::clamp(opt.selected, 0,
                               std::max(0, static_cast<int>(ds.missions.size()) - 1));
    const MapExtent extent = map_extent(ds, opt);
    const int minx = extent.minx, miny = extent.miny;
    const int maxx = extent.maxx, maxy = extent.maxy;
    // Empty checkboxes mean "no filter", which is how the original campaign
    // map opens: it shows every side's flags until a box restricts it.
    const unsigned side_mask = opt.side_mask ? opt.side_mask : MapSideAll;
    const unsigned layer_mask = opt.layer_mask ? opt.layer_mask : MapLayerAll;


    auto px = [&](int x) {
        return left + (x - minx) * (right - left) / (maxx - minx);
    };
    auto py = [&](int y) {
        return bottom - (y - miny) * (bottom - top) / (maxy - miny);
    };

    // Side colours are darkened a little on the light political map so the
    // markers keep contrast.
    auto side_rgb = [&](const std::string& side) {
        SDL_Color sc = side_color(side);
        if (political) {
            sc.r = static_cast<Uint8>(sc.r * 3 / 4);
            sc.g = static_cast<Uint8>(sc.g * 3 / 4);
            sc.b = static_cast<Uint8>(sc.b * 3 / 4);
        }
        return c.rgb(sc.r, sc.g, sc.b);
    };

    c.lock();
    const Uint32 grid = political ? c.rgb(108, 114, 122) : c.rgb(30, 42, 58);
    int step = 50;
    int span = std::max(maxx - minx, maxy - miny);
    if (span > 2000) step = 500;
    else if (span > 800) step = 200;
    else if (span > 300) step = 100;
    for (int x = (minx / step) * step; x <= maxx; x += step) {
        int sx = px(x);
        c.line(sx, top, sx, bottom, grid);
    }
    for (int y = (miny / step) * step; y <= maxy; y += step) {
        int sy = py(y);
        c.line(left, sy, right, sy, grid);
    }

    // all target areas (muted), filtered by the Map Options side and layer
    for (const auto& m : ds.missions) {
        if ((map_side_bits(m.side) & side_mask) == 0) continue;
        const Uint32 col = side_rgb(m.side);
        for (const auto& t : m.targets) {
            for (const auto& a : t.areas) {
                if (!a.pos) continue;
                if ((map_layer_bits(a.type) & layer_mask) == 0) continue;
                c.circle(px(a.pos->x), py(a.pos->y), 2, col);
            }
        }
    }

    // bases / navigation points
    for (const auto& b : ds.bnas) {
        if (!b.pos) continue;
        if ((map_layer_bits(b.type) & layer_mask) == 0) continue;
        int x = px(b.pos->x), y = py(b.pos->y);
        Uint32 col = (b.type == "PORT") ? c.rgb(110, 220, 150) : c.rgb(100, 180, 255);
        for (int dy = -3; dy <= 3; ++dy)
            for (int dx = -3; dx <= 3; ++dx) c.pixel(x + dx, y + dy, col);
    }

    // Ground units and flights are the flags the campaign map filters by side
    // and layer. Groups are squares, flights are diamonds, airbases are open
    // boxes. The `f` toggle hides the markers but keeps target areas and the
    // coverage rings.
    if (opt.flags) {
        for (const auto& m : ds.missions) {
            if ((map_side_bits(m.side) & side_mask) == 0) continue;
            const Uint32 col = side_rgb(m.side);
            for (const auto& g : m.groups) {
                if (!g.pos) continue;
                if ((map_layer_bits(g.type, g.role) & layer_mask) == 0) continue;
                const int x = px(g.pos->x), y = py(g.pos->y);
                c.fill(x - 1, y - 1, 3, 3, col);
            }
            for (const auto& f : m.flights) {
                if (!f.pos) continue;
                if ((MapLayerAirSupport & layer_mask) == 0) continue;
                const int x = px(f.pos->x), y = py(f.pos->y);
                c.pixel(x, y - 2, col); c.pixel(x - 1, y - 1, col);
                c.pixel(x + 1, y - 1, col); c.pixel(x - 2, y, col);
                c.pixel(x + 2, y, col); c.pixel(x - 1, y + 1, col);
                c.pixel(x + 1, y + 1, col); c.pixel(x, y + 2, col);
            }
        }

        for (const auto& b : ds.bases) {
            if (!b.pos) continue;
            if ((map_side_bits(b.side) & side_mask) == 0) continue;
            if ((MapLayerAirSupport & layer_mask) == 0) continue;
            const Uint32 col = side_rgb(b.side);
            const int x = px(b.pos->x), y = py(b.pos->y);
            for (int d = -3; d <= 3; ++d) {
                c.pixel(x + d, y - 3, col); c.pixel(x + d, y + 3, col);
                c.pixel(x - 3, y + d, col); c.pixel(x + 3, y + d, col);
            }
        }
    }

    // Coverage overlays. Rings are drawn for every ground unit in the theatre,
    // coloured by the side that owns it, matching the planner's two toggles.
    // The side filter applies; the layer mask does not, because the two rings
    // already have their own switches.
    if (opt.sam_coverage || opt.ewr_coverage) {
        const double scale = static_cast<double>(right - left) /
                             static_cast<double>(std::max(maxx - minx, 1));
        auto draw_ring = [&](const Vec3& p, Threat kind, const std::string& side) {
            int cells = 0;
            if (kind == Threat::ewr) cells = kEwrRangeCells;
            else if (kind == Threat::sam) cells = kSamRangeCells;
            else if (kind == Threat::aaa) cells = kAaaRangeCells;
            if (cells == 0) return;
            if (kind == Threat::ewr && !opt.ewr_coverage) return;
            if (kind != Threat::ewr && !opt.sam_coverage) return;
            const int radius = static_cast<int>(std::lround(
                cells * opt.coverage_scale * scale));
            c.ring(px(p.x), py(p.y), radius, side_rgb(side));
        };

        // The same site is referenced by many mission records, so draw each
        // (position, side, class) once or the map turns into a ball of rings.
        std::set<std::tuple<int, int, int, std::string>> seen;
        auto draw_once = [&](const Vec3& p, Threat kind, const std::string& side) {
            const int cls = static_cast<int>(kind);
            if (!seen.emplace(p.x, p.y, cls, side).second) return;
            draw_ring(p, kind, side);
        };

        for (const auto& m : ds.missions) {
            if ((map_side_bits(m.side) & side_mask) == 0) continue;
            // `MISSION.CFG` carries ground units as GROUP records; the archive
            // mission dialect uses FLIGHT. Accept both.
            for (const auto& g : m.groups) {
                if (!g.pos) continue;
                draw_once(*g.pos, classify_threat(g.type), m.side);
            }
            for (const auto& f : m.flights) {
                if (!f.pos) continue;
                draw_once(*f.pos, classify_threat(f.aircraft), m.side);
            }
            // Early-warning sites also appear as target areas.
            for (const auto& t : m.targets) {
                for (const auto& a : t.areas) {
                    if (!a.pos) continue;
                    if (a.type.find("EWR") == std::string::npos &&
                        a.name.find("WARNING") == std::string::npos) {
                        continue;
                    }
                    draw_once(*a.pos, Threat::ewr, m.side);
                }
            }
        }
    }

    // Planner Fuel toggle: a dashed range ring around the mission's first
    // waypoint. The mission files carry no fuel-capacity table, so the radius
    // is the same kind of documented assumption as the SAM/EWR rings and uses
    // the same scale knob.
    if (opt.fuel && !ds.missions.empty()) {
        const Mission& m = ds.missions[sel];
        std::optional<Vec3> centre = m.pos;
        if (!m.routes.empty() && !m.routes.front().waypoints.empty()) {
            centre = m.routes.front().waypoints.front().p;
        }
        if (centre) {
            const double scale = static_cast<double>(right - left) /
                                 static_cast<double>(std::max(maxx - minx, 1));
            const int radius = static_cast<int>(std::lround(
                45.0 * opt.coverage_scale * scale));
            const Uint32 col = c.rgb(120, 220, 255);
            for (int a = 0; a < 360; a += 6) {
                if ((a / 6) % 3 != 0) continue;
                const double r = a * kPi / 180.0;
                c.fill(px(centre->x) + static_cast<int>(std::lround(radius * std::cos(r))),
                       py(centre->y) + static_cast<int>(std::lround(radius * std::sin(r))),
                       2, 2, col);
            }
        }
    }

    // The mission being planned always draws, even when its side or layer is
    // filtered out of the background: it is the thing the user is editing.
    if (!ds.missions.empty()) {
        const Mission& m = ds.missions[sel];
        Uint32 route_col = c.rgb(255, 220, 90);
        Uint32 wp_col = c.rgb(255, 255, 255);
        Uint32 tgt_col = c.rgb(255, 90, 90);

        for (const auto& r : m.routes) {
            for (std::size_t i = 1; i < r.waypoints.size(); ++i) {
                const auto& a = r.waypoints[i - 1];
                const auto& b = r.waypoints[i];
                c.line(px(a.p.x), py(a.p.y), px(b.p.x), py(b.p.y), route_col);
            }
            for (const auto& w : r.waypoints) {
                int x = px(w.p.x), y = py(w.p.y);
                c.circle(x, y, 3, wp_col);
            }
        }
        for (const auto& t : m.targets) {
            for (const auto& a : t.areas) {
                if (!a.pos) continue;
                int x = px(a.pos->x), y = py(a.pos->y);
                c.circle(x, y, 7, tgt_col);
                c.circle(x, y, 8, tgt_col);
            }
            // Attack patterns belong to the target view: their nodes are local
            // to the aim point, not theatre coordinates.
        }
    }
    c.unlock();

    // Political map modes label the airbases and navigation points, as the
    // original does. Relief modes stay unlabelled.
    if (opt.labels && political) {
        if (TTF_WasInit() == 0) TTF_Init();
        TTF_Font* label_font = TTF_OpenFont(kFontPath, 10);
        if (label_font != nullptr) {
            const auto unquote = [](std::string t) {
                if (!t.empty() && t.front() == '"') t.erase(t.begin());
                if (!t.empty() && t.back() == '"') t.pop_back();
                return t;
            };
            const SDL_Color ink{30, 34, 40, 255};
            // The same airbase appears in every mission pack; label each name
            // once or the political map turns into a wall of text.
            std::set<std::string> labelled;
            std::vector<std::pair<int, int>> placed;
            for (const auto& b : ds.bases) {
                if (!b.pos) continue;
                const std::string name = unquote(b.name);
                if (!labelled.insert(name).second) continue;
                const int lx = px(b.pos->x) + 5, ly = py(b.pos->y) + 2;
                // Drop labels that would overprint an earlier one; the
                // original map only names the larger places for this reason.
                bool crowded = false;
                for (const auto& p : placed) {
                    if (std::abs(lx - p.first) < 55 && std::abs(ly - p.second) < 12) {
                        crowded = true;
                        break;
                    }
                }
                if (crowded) continue;
                placed.emplace_back(lx, ly);
                draw_text(c, label_font, lx, ly, name, ink);
            }
            for (const auto& b : ds.bnas) {
                if (!b.pos) continue;
                if (b.type != "PORT") continue;
                draw_text(c, label_font, px(b.pos->x) + 5, py(b.pos->y) + 2,
                          b.type, ink);
            }
            TTF_CloseFont(label_font);
        }
    }

    // Bare mode is embedded inside a GUI window: the caller supplies the
    // surrounding chrome, so do not draw the side panel or any text.
    if (opt.bare) {
        SDL_Surface* out = c.s;
        c.s = nullptr;  // Canvas owns the surface; detach before returning
        return out;
    }

    if (TTF_WasInit() == 0) TTF_Init();
    TTF_Font* f_big = TTF_OpenFont(kFontPath, 22);
    TTF_Font* f_small = TTF_OpenFont(kFontPath, 14);
    SDL_Color white{235, 240, 248, 255};
    SDL_Color dim{150, 165, 185, 255};
    SDL_Color gold{255, 220, 90, 255};

    draw_text(c, f_big, 20, 14, "OpenEF2000", white);
    {
        std::ostringstream os;
        os << "missions " << ds.missions.size() << "   bases "
           << ds.bnas.size() << "   target areas " << ds.areas.size()
           << "   world [" << minx << ".." << maxx << "] x ["
           << miny << ".." << maxy << "]";
        draw_text(c, f_small, 20, H - 27, os.str(), dim);
    }

    if (!ds.missions.empty()) {
        const Mission& m = ds.missions[sel];
        int x = right + 25, y = 18;
        draw_text(c, f_big, x, y, "Mission", gold); y += 32;
        draw_text(c, f_small, x, y, mission_label(m, sel), white); y += 22;
        {
            std::ostringstream os;
            os << "status " << m.status << "   groups " << m.groups.size()
               << "   routes " << m.routes.size() << "   flights " << m.flights.size();
            draw_text(c, f_small, x, y, os.str(), dim); y += 20;
        }
        int wps = 0, pylons = 0, nodes = 0;
        for (const auto& r : m.routes) wps += static_cast<int>(r.waypoints.size());
        for (const auto& f : m.flights) pylons += static_cast<int>(f.pylons.size());
        for (const auto& t : m.targets) {
            for (const auto& p : t.patterns)
                for (const auto& r : p.routes) nodes += static_cast<int>(r.nodes.size());
            for (const auto& a : t.areas)
                for (const auto& p : a.patterns)
                    for (const auto& r : p.routes) nodes += static_cast<int>(r.nodes.size());
        }
        {
            std::ostringstream os;
            os << "waypoints " << wps << "   pylons " << pylons
               << "   attack nodes " << nodes;
            draw_text(c, f_small, x, y, os.str(), dim); y += 26;
        }
        draw_text(c, f_small, x, y, "Routes", gold); y += 18;
        for (std::size_t i = 0; i < m.routes.size() && i < 7; ++i) {
            std::ostringstream os;
            os << "  route " << i << ": " << m.routes[i].waypoints.size()
               << " wp";
            draw_text(c, f_small, x, y, os.str(), white); y += 16;
        }
        y += 8;
        draw_text(c, f_small, x, y, "Targets", gold); y += 18;
        int shown = 0;
        for (const auto& t : m.targets) {
            for (const auto& a : t.areas) {
                if (shown++ >= 8) break;
                draw_text(c, f_small, x, y, "  " + a.type + " " + a.name, white);
                y += 16;
            }
        }
        y += 8;
        draw_text(c, f_small, x, y, "Controls", gold); y += 18;
        draw_text(c, f_small, x, y, "  N/P or Up/Down  mission", dim); y += 16;
        draw_text(c, f_small, x, y, "  S  save PNG      Esc quit", dim); y += 22;

        draw_text(c, f_small, x, y, "Missions", gold); y += 18;
        int n = static_cast<int>(ds.missions.size());
        int start = std::max(0, sel - 7);
        int end = std::min(n, start + 15);
        start = std::max(0, end - 15);
        for (int i = start; i < end; ++i) {
            const auto& mm = ds.missions[i];
            std::ostringstream os;
            os << "#" << i << "  " << mm.type << " " << mm.side << " " << mm.role;
            draw_text(c, f_small, x, y, os.str(), i == sel ? gold : dim);
            y += 15;
        }
    }

    // selected mission target labels and waypoint indices
    if (!ds.missions.empty()) {
        const Mission& m = ds.missions[sel];
        for (const auto& t : m.targets) {
            for (const auto& a : t.areas) {
                if (!a.pos) continue;
                draw_text(c, f_small, px(a.pos->x) + 10, py(a.pos->y) - 8,
                          a.type + " " + a.name, SDL_Color{255, 150, 150, 255});
            }
        }
        for (const auto& r : m.routes) {
            for (std::size_t i = 0; i < r.waypoints.size(); ++i) {
                if (i % 5 != 0) continue;
                const auto& w = r.waypoints[i];
                draw_text(c, f_small, px(w.p.x) + 5, py(w.p.y) + 4,
                          std::to_string(w.index), SDL_Color{200, 220, 255, 255});
            }
        }
    }

    if (f_big) TTF_CloseFont(f_big);
    if (f_small) TTF_CloseFont(f_small);
    SDL_Surface* out = c.s;
    c.s = nullptr;
    return out;
}

bool save_png(SDL_Surface* surface, const std::string& path) {
    if (!surface) return false;
    if (!(IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG)) return false;
    int ok = IMG_SavePNG(surface, path.c_str());
    IMG_Quit();
    return ok == 0;
}

void print_mission(const Dataset& ds, int index) {
    if (ds.missions.empty()) {
        std::printf("no missions\n");
        return;
    }
    int sel = std::clamp(index, 0, static_cast<int>(ds.missions.size()) - 1);
    const Mission& m = ds.missions[sel];
    std::printf("mission #%d  %s %s %s %s\n", sel, m.type.c_str(),
                m.side.c_str(), m.role.c_str(), m.status.c_str());
    std::printf("  groups %zu  routes %zu  targets %zu  flights %zu\n",
                m.groups.size(), m.routes.size(), m.targets.size(),
                m.flights.size());
    for (std::size_t i = 0; i < m.routes.size(); ++i) {
        std::printf("  route %zu: %zu waypoints\n", i, m.routes[i].waypoints.size());
        for (const auto& w : m.routes[i].waypoints) {
            std::printf("    wp %2d  (%d,%d,%d)  %s\n", w.index, w.p.x, w.p.y,
                        w.p.z, w.action.c_str());
        }
    }
    for (const auto& t : m.targets) {
        std::printf("  target mission=%d group=%d\n", t.mission, t.group);
        for (const auto& a : t.areas)
            std::printf("    area %s %s\n", a.type.c_str(), a.name.c_str());
    }
}

void print_stats(const Dataset& ds) {
    int routes = 0, wps = 0, flights = 0, pylons = 0, targets = 0, areas = 0;
    int aps = 0, ars = 0, nodes = 0;
    for (const auto& m : ds.missions) {
        routes += static_cast<int>(m.routes.size());
        flights += static_cast<int>(m.flights.size());
        targets += static_cast<int>(m.targets.size());
        for (const auto& r : m.routes) wps += static_cast<int>(r.waypoints.size());
        for (const auto& f : m.flights) pylons += static_cast<int>(f.pylons.size());
        for (const auto& t : m.targets) {
            for (const auto& a : t.areas) {
                ++areas;
                aps += static_cast<int>(a.patterns.size());
                for (const auto& p : a.patterns)
                    for (const auto& r : p.routes) {
                        ++ars;
                        nodes += static_cast<int>(r.nodes.size());
                    }
            }
            aps += static_cast<int>(t.patterns.size());
            for (const auto& p : t.patterns)
                for (const auto& r : p.routes) {
                    ++ars;
                    nodes += static_cast<int>(r.nodes.size());
                }
        }
    }
    int bna_wps = 0;
    for (const auto& b : ds.bnas)
        for (const auto& r : b.routes) bna_wps += static_cast<int>(r.waypoints.size());

    std::printf("data: %s\n", ds.dir.c_str());
    std::printf("missions        %zu (declared %d)\n", ds.missions.size(),
                ds.num_missions_declared);
    std::printf("routes          %d\n", routes);
    std::printf("waypoints       %d\n", wps);
    std::printf("flights         %d\n", flights);
    std::printf("pylons          %d\n", pylons);
    std::printf("targets         %d\n", targets);
    std::printf("target areas    %d\n", areas);
    std::printf("attack patterns %d\n", aps);
    std::printf("attack routes   %d\n", ars);
    std::printf("attack nodes    %d\n", nodes);
    std::printf("bases           %zu\n", ds.bases.size());
    std::printf("base nav (BNA)  %zu (declared %d)\n", ds.bnas.size(),
                ds.num_bna_declared);
    std::printf("BNA waypoints   %d\n", bna_wps);
    std::printf("conflict areas  %zu (declared %d)\n", ds.areas.size(),
                ds.num_areas_declared);
}

}  // namespace ef
