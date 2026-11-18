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

SDL_Surface* render_map(const Dataset& ds, const RenderOptions& opt) {
    const int W = std::max(640, opt.width);
    const int H = std::max(480, opt.height);
    const int panel_w = 380;
    const int left = 55, top = 55;
    const int right = W - panel_w - 20;
    const int bottom = H - 55;

    Canvas c;
    c.s = SDL_CreateRGBSurfaceWithFormat(0, W, H, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!c.s) return nullptr;
    c.w = W; c.h = H;
    c.fill(0, 0, W, H, c.rgb(10, 16, 24));
    c.fill(right + 10, 0, panel_w, H, c.rgb(18, 24, 34));
    c.fill(0, H - 36, W, 36, c.rgb(14, 20, 30));

    int sel = std::clamp(opt.selected, 0, std::max(0, static_cast<int>(ds.missions.size()) - 1));

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
            for (const auto& p : t.patterns)
                for (const auto& r : p.routes)
                    for (const auto& n : r.nodes) add(n.p);
            for (const auto& a : t.areas)
                for (const auto& p : a.patterns)
                    for (const auto& r : p.routes)
                        for (const auto& n : r.nodes) add(n.p);
        }
        for (const auto& r : m.routes)
            for (const auto& w : r.waypoints) add(w.p);
    };
    if (opt.focus && !ds.missions.empty()) {
        add_mission(ds.missions[sel]);
    } else {
        for (const auto& m : ds.missions) add_mission(m);
    }

    // Bases are deliberately excluded from the extent. They come from a
    // different data set with its own coordinate space, and including them
    // collapses the theatre to a dot.
    int minx = 0, miny = 0, maxx = 100, maxy = 100;
    if (opt.bounds) {
        minx = (*opt.bounds)[0]; miny = (*opt.bounds)[1];
        maxx = (*opt.bounds)[2]; maxy = (*opt.bounds)[3];
    } else if (!xs.empty()) {
        minx = *std::min_element(xs.begin(), xs.end());
        maxx = *std::max_element(xs.begin(), xs.end());
        miny = *std::min_element(ys.begin(), ys.end());
        maxy = *std::max_element(ys.begin(), ys.end());
    }
    if (maxx - minx < 1) maxx = minx + 1;
    if (maxy - miny < 1) maxy = miny + 1;

    // Coverage rings extend beyond their unit, so widen the view to contain
    // the largest radius when an overlay is on.
    if (opt.sam_coverage || opt.ewr_coverage) {
        const int ring = static_cast<int>(std::lround(
            (opt.ewr_coverage ? kEwrRangeCells : kSamRangeCells) * opt.coverage_scale));
        minx -= ring; maxx += ring; miny -= ring; maxy += ring;
    }

    const int padx = std::max(1, (maxx - minx) / 20);
    const int pady = std::max(1, (maxy - miny) / 20);
    minx -= padx; maxx += padx; miny -= pady; maxy += pady;

    auto px = [&](int x) {
        return left + (x - minx) * (right - left) / (maxx - minx);
    };
    auto py = [&](int y) {
        return bottom - (y - miny) * (bottom - top) / (maxy - miny);
    };

    c.lock();
    const Uint32 grid = c.rgb(30, 42, 58);
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

    // all target areas (muted)
    for (const auto& m : ds.missions) {
        SDL_Color sc = side_color(m.side);
        Uint32 col = c.rgb(sc.r, sc.g, sc.b);
        for (const auto& t : m.targets) {
            for (const auto& a : t.areas) {
                if (!a.pos) continue;
                c.circle(px(a.pos->x), py(a.pos->y), 2, col);
            }
        }
    }

    // bases / navigation points
    for (const auto& b : ds.bnas) {
        if (!b.pos) continue;
        int x = px(b.pos->x), y = py(b.pos->y);
        Uint32 col = (b.type == "PORT") ? c.rgb(110, 220, 150) : c.rgb(100, 180, 255);
        for (int dy = -3; dy <= 3; ++dy)
            for (int dx = -3; dx <= 3; ++dx) c.pixel(x + dx, y + dy, col);
    }

    // Coverage overlays. Rings are drawn for every ground unit in the theatre,
    // coloured by the side that owns it, matching the planner's two toggles.
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
            const SDL_Color sc = side_color(side);
            c.ring(px(p.x), py(p.y), radius, c.rgb(sc.r, sc.g, sc.b));
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

    if (!ds.missions.empty()) {
        const Mission& m = ds.missions[sel];
        SDL_Color sc = side_color(m.side);
        Uint32 route_col = c.rgb(255, 220, 90);
        Uint32 wp_col = c.rgb(255, 255, 255);
        Uint32 tgt_col = c.rgb(255, 90, 90);
        Uint32 atk_col = c.rgb(255, 150, 60);

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
            auto draw_patterns = [&](const std::vector<AttackPattern>& pats) {
                for (const auto& p : pats) {
                    for (const auto& r : p.routes) {
                        for (std::size_t i = 1; i < r.nodes.size(); ++i) {
                            const auto& a = r.nodes[i - 1];
                            const auto& b = r.nodes[i];
                            c.line(px(a.p.x), py(a.p.y), px(b.p.x), py(b.p.y), atk_col);
                        }
                        for (const auto& n : r.nodes)
                            c.circle(px(n.p.x), py(n.p.y), 2, atk_col);
                    }
                }
            };
            draw_patterns(t.patterns);
            for (const auto& a : t.areas) draw_patterns(a.patterns);
        }
        (void)sc;
    }
    c.unlock();

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

    // Coverage overlays. Rings are drawn for every ground unit in the theatre,
    // coloured by the side that owns it, matching the planner's two toggles.
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
            const SDL_Color sc = side_color(side);
            c.ring(px(p.x), py(p.y), radius, c.rgb(sc.r, sc.g, sc.b));
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
    // Coverage overlays. Rings are drawn for every ground unit in the theatre,
    // coloured by the side that owns it, matching the planner's two toggles.
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
            const SDL_Color sc = side_color(side);
            c.ring(px(p.x), py(p.y), radius, c.rgb(sc.r, sc.g, sc.b));
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
