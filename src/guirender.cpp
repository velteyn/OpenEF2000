// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 velteyn

#include "guirender.h"

#include "render.h"

#include <cmath>
#include <cstring>

#include "ilbm.h"

#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>

#include <algorithm>
#include <cstdio>
#include <vector>

namespace ef {
namespace {

const char* kFontPath = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";
constexpr int kScreenW = 640;
constexpr int kScreenH = 400;

Uint32 map_rgb(SDL_PixelFormat* fmt, std::uint32_t rgb) {
    return SDL_MapRGB(fmt, static_cast<Uint8>((rgb >> 16) & 0xFF),
                      static_cast<Uint8>((rgb >> 8) & 0xFF), static_cast<Uint8>(rgb & 0xFF));
}

void fill_rect(SDL_Surface* s, int x, int y, int w, int h, Uint32 colour) {
    SDL_Rect r{x, y, w, h};
    SDL_FillRect(s, &r, colour);
}

void hline(SDL_Surface* s, int x, int y, int w, Uint32 colour) { fill_rect(s, x, y, w, 1, colour); }
void vline(SDL_Surface* s, int x, int y, int h, Uint32 colour) { fill_rect(s, x, y, 1, h, colour); }

//! Raised DOS-style frame: dark outline, light top-left, shadow bottom-right.
void frame(SDL_Surface* s, int x, int y, int w, int h, const Palette& pal, bool sunken) {
    if (w <= 0 || h <= 0) return;
    const Uint32 light = map_rgb(s->format, pal[14]);
    const Uint32 mid = map_rgb(s->format, pal[11]);
    const Uint32 dark = map_rgb(s->format, pal[8]);
    const Uint32 shadow = map_rgb(s->format, pal[9]);
    const Uint32 top = sunken ? dark : light;
    const Uint32 bottom = sunken ? light : dark;

    hline(s, x, y, w, top);
    vline(s, x, y, h, top);
    hline(s, x, y + h - 1, w, bottom);
    vline(s, x + w - 1, y, h, bottom);
    if (w > 2 && h > 2) {
        hline(s, x + 1, y + 1, w - 2, sunken ? shadow : mid);
        vline(s, x + 1, y + 1, h - 2, sunken ? shadow : mid);
        hline(s, x + 1, y + h - 2, w - 2, sunken ? mid : shadow);
        vline(s, x + w - 2, y + 1, h - 2, sunken ? mid : shadow);
    }
}

bool visible_colour(int index) { return index > 0; }

}  // namespace

void draw_image(SDL_Surface* surface, const IlbmImage& image) {
    if (surface == nullptr || image.width <= 0 || image.height <= 0) return;
    for (int y = 0; y < surface->h; ++y) {
        const int sy = y * image.height / surface->h;
        auto* row = static_cast<Uint32*>(static_cast<void*>(
            static_cast<Uint8*>(surface->pixels) + y * surface->pitch));
        for (int x = 0; x < surface->w; ++x) {
            const int sx = x * image.width / surface->w;
            const std::uint8_t index =
                image.pixels[static_cast<std::size_t>(sy) *
                                 static_cast<std::size_t>(image.width) +
                             static_cast<std::size_t>(sx)];
            const std::uint32_t rgb =
                index < image.palette.size() ? image.palette[index] : 0;
            row[x] = map_rgb(surface->format, rgb);
        }
    }
}

std::optional<IlbmImage> load_backdrop(const DidArchive& archive, const std::string& name) {
    if (name.empty()) return std::nullopt;
    for (const char* prefix : {"iff\\256\\", "iff\\", "gui\\"}) {
        for (const char* extension : {".lbm", ".bmp"}) {
            auto entry = archive.resolve(name, prefix, extension);
            if (!entry) continue;
            auto bytes = archive.read(*entry);
            if (!bytes) continue;
            if (auto image = decode_ilbm(*bytes)) return image;
        }
    }
    return std::nullopt;
}

namespace {

struct MapCache {
    SDL_Surface* surface = nullptr;
    int w = 0, h = 0, zoom = 0, mission = -1;
    bool sam = false, ewr = false;
    unsigned sides = 0, layers = 0;
    int pan_x = 0, pan_y = 0;
    bool focus = false;
    int mode = 0;
    bool flags = true;
};

MapCache g_map_cache;

//! The map is rendered into the window's declared work area (400x400 for both
//! map windows), then the client shows a scrolled sub-rectangle.
int map_work_w(const GuiWindow& win, int client_w) {
    return win.work_w > 0 ? win.work_w : client_w;
}
int map_work_h(const GuiWindow& win, int client_h) {
    return win.work_h > 0 ? win.work_h : client_h;
}

struct MapScrollbar {
    SDL_Rect track{};
    SDL_Rect thumb{};
    int max_scroll = 0;
};

bool map_scrollbar(const GuiWindow& win, const SDL_Rect& client,
                   MapScrollbar* out) {
    if (!win.scroll_y) return false;
    const int work_h = map_work_h(win, client.h);
    const int max_scroll = work_h - client.h;
    if (max_scroll <= 0 || client.h < 40) return false;
    const int bar_w = 12;
    out->track = SDL_Rect{client.x + client.w - bar_w, client.y, bar_w, client.h};
    const int usable = client.h - 24;
    const int thumb_h = std::max(16, usable * client.h / work_h);
    const int pos = std::clamp(win.scroll_y_pos, 0, max_scroll);
    const int thumb_y = client.y + 12 + (usable - thumb_h) * pos / max_scroll;
    out->thumb = SDL_Rect{out->track.x, thumb_y, bar_w, thumb_h};
    out->max_scroll = max_scroll;
    return true;
}

RenderOptions map_options(const GuiSystem& system, const GuiWindow& win, int w, int h) {
    RenderOptions opt;
    opt.bare = true;
    opt.width = w;
    opt.height = h;
    opt.selected = system.camp_mission();
    opt.labels = true;
    // The campaign map is a theatre overview; the planner frames the mission
    // being edited.
    opt.focus = (win.name == "mp_map");
    opt.sam_coverage = system.camp_sam();
    opt.ewr_coverage = system.camp_ewr();
    opt.zoom = std::pow(1.4f, static_cast<float>(system.camp_zoom()));
    opt.pan_x = system.camp_pan_x();
    opt.pan_y = system.camp_pan_y();
    opt.map_mode = system.camp_map_mode();
    opt.flags = system.camp_flags();
    // Fuel is a planner-bar toggle; the campaign bar has no such control.
    opt.fuel = (win.name == "mp_map") && system.mp_icons();
    // The campaign screen has the Map Options checkboxes; the planner has no
    // side/layer controls, so its map always shows the whole theatre.
    if (win.name == "mp_map") {
        opt.side_mask = MapSideAll;
        opt.layer_mask = MapLayerAll;
    } else {
        opt.side_mask = system.camp_side_mask();
        opt.layer_mask = system.camp_layer_mask();
    }
    return opt;
}

// The map is expensive to draw, and the planner changes it rarely, so render
// it only when one of the inputs changes.
SDL_Surface* map_for(const Dataset& ds, const GuiSystem& system, const GuiWindow& win,
                     int w, int h, SDL_PixelFormat* format) {
    const RenderOptions opt = map_options(system, win, w, h);
    if (g_map_cache.surface != nullptr && g_map_cache.w == w && g_map_cache.h == h &&
        g_map_cache.zoom == system.camp_zoom() &&
        g_map_cache.mission == system.camp_mission() &&
        g_map_cache.sam == opt.sam_coverage && g_map_cache.ewr == opt.ewr_coverage &&
        g_map_cache.sides == opt.side_mask && g_map_cache.layers == opt.layer_mask &&
        g_map_cache.pan_x == opt.pan_x && g_map_cache.pan_y == opt.pan_y &&
        g_map_cache.focus == opt.focus && g_map_cache.mode == opt.map_mode &&
        g_map_cache.flags == opt.flags) {
        return g_map_cache.surface;
    }
    SDL_Surface* raw = render_map(ds, opt);
    if (raw == nullptr) return nullptr;
    SDL_Surface* converted = SDL_ConvertSurface(raw, format, 0);
    SDL_FreeSurface(raw);
    if (converted == nullptr) return nullptr;
    if (g_map_cache.surface != nullptr) SDL_FreeSurface(g_map_cache.surface);
    g_map_cache = MapCache{converted, w, h, system.camp_zoom(),
                           system.camp_mission(), opt.sam_coverage,
                           opt.ewr_coverage, opt.side_mask, opt.layer_mask,
                           opt.pan_x, opt.pan_y, opt.focus, opt.map_mode,
                           opt.flags};
    return converted;
}

}  // namespace

bool gui_map_scrollbar(const GuiWindow& win, SDL_Rect* track, SDL_Rect* thumb,
                       int* max_scroll) {
    const SDL_Rect client = GuiSystem::client_rect(win);
    MapScrollbar sb;
    if (!map_scrollbar(win, client, &sb)) return false;
    if (track) *track = sb.track;
    if (thumb) *thumb = sb.thumb;
    if (max_scroll) *max_scroll = sb.max_scroll;
    return true;
}

bool gui_is_map_window(const std::string& name) {
    return name == "mp_map" || name == "map";
}

bool gui_map_world_at(const GuiSystem& system, const Dataset& ds,
                      const GuiWindow& win, int sx, int sy, int* world_x,
                      int* world_y, int* radius) {
    const SDL_Rect client = GuiSystem::client_rect(win);
    if (sx < client.x || sy < client.y || sx >= client.x + client.w ||
        sy >= client.y + client.h || client.w <= 0 || client.h <= 0) {
        return false;
    }
    const int work_w = map_work_w(win, client.w);
    const int work_h = map_work_h(win, client.h);
    const int sx_off = std::clamp(win.scroll_x_pos, 0,
                                  std::max(0, work_w - client.w));
    const int sy_off = std::clamp(win.scroll_y_pos, 0,
                                  std::max(0, work_h - client.h));
    const RenderOptions opt = map_options(system, win, work_w, work_h);
    const MapProjection p = map_projection(ds, opt);
    const int mx = sx - client.x + sx_off, my = sy - client.y + sy_off;
    if (!p.contains(mx, my)) return false;
    if (world_x) *world_x = p.world_x(mx);
    if (world_y) *world_y = p.world_y(my);
    if (radius) {
        const double per_px = static_cast<double>(p.world_max_x - p.world_min_x) /
                              std::max(1, p.right - p.left);
        *radius = std::max(1, static_cast<int>(std::lround(per_px * 10.0)));
    }
    return true;
}

bool gui_map_pan(GuiSystem& system, const Dataset& ds, const GuiWindow& win,
                 int dx, int dy) {
    const SDL_Rect client = GuiSystem::client_rect(win);
    if (client.w <= 0 || client.h <= 0) return false;
    const int work_w = map_work_w(win, client.w);
    const int work_h = map_work_h(win, client.h);
    const RenderOptions opt = map_options(system, win, work_w, work_h);
    const MapProjection p = map_projection(ds, opt);
    const double per_px_x = static_cast<double>(p.world_max_x - p.world_min_x) /
                            std::max(1, p.right - p.left);
    const double per_px_y = static_cast<double>(p.world_max_y - p.world_min_y) /
                            std::max(1, p.bottom - p.top);
    system.set_camp_pan(system.camp_pan_x() -
                            static_cast<int>(std::lround(dx * per_px_x)),
                        system.camp_pan_y() +
                            static_cast<int>(std::lround(dy * per_px_y)));
    return true;
}

bool gui_map_zoom_at(GuiSystem& system, const Dataset& ds, const GuiWindow& win,
                     int sx, int sy, int steps) {
    const int old = system.camp_zoom();
    const int next = std::clamp(old + steps, 0, 6);
    if (next == old) return false;

    const SDL_Rect client = GuiSystem::client_rect(win);
    if (client.w <= 0 || client.h <= 0) return false;
    const int work_w = map_work_w(win, client.w);
    const int work_h = map_work_h(win, client.h);
    const int sx_off = std::clamp(win.scroll_x_pos, 0,
                                  std::max(0, work_w - client.w));
    const int sy_off = std::clamp(win.scroll_y_pos, 0,
                                  std::max(0, work_h - client.h));
    const RenderOptions cur = map_options(system, win, work_w, work_h);
    const MapProjection before = map_projection(ds, cur);
    const int mx = sx - client.x + sx_off, my = sy - client.y + sy_off;
    if (!before.contains(mx, my)) return false;
    const int wx = before.world_x(mx), wy = before.world_y(my);

    RenderOptions updated = cur;
    updated.zoom = std::pow(1.4f, static_cast<float>(next));
    updated.pan_x = 0;
    updated.pan_y = 0;
    const MapProjection after = map_projection(ds, updated);
    const double spanx = std::max(1, after.world_max_x - after.world_min_x);
    const double spany = std::max(1, after.world_max_y - after.world_min_y);
    const int panx = static_cast<int>(std::lround(
        wx - after.world_min_x -
        (mx - after.left) * spanx / std::max(1, after.right - after.left)));
    const int pany = static_cast<int>(std::lround(
        wy - after.world_max_y +
        (my - after.top) * spany / std::max(1, after.bottom - after.top)));

    system.set_camp_zoom(next);
    system.set_camp_pan(panx, pany);
    return true;
}

bool gui_map_zoom_rect(GuiSystem& system, const Dataset& ds, const GuiWindow& win,
                       int x0, int y0, int x1, int y1) {
    const SDL_Rect client = GuiSystem::client_rect(win);
    if (client.w <= 0 || client.h <= 0) return false;
    const int ax = std::min(x0, x1), bx = std::max(x0, x1);
    const int ay = std::min(y0, y1), by = std::max(y0, y1);
    if (bx - ax < 8 || by - ay < 8) return false;

    const int work_w = map_work_w(win, client.w);
    const int work_h = map_work_h(win, client.h);
    const int sx_off = std::clamp(win.scroll_x_pos, 0,
                                  std::max(0, work_w - client.w));
    const int sy_off = std::clamp(win.scroll_y_pos, 0,
                                  std::max(0, work_h - client.h));
    const RenderOptions cur = map_options(system, win, work_w, work_h);
    const MapProjection before = map_projection(ds, cur);
    const int mx0 = std::clamp(ax - client.x + sx_off, before.left + 1,
                               before.right - 1);
    const int my0 = std::clamp(ay - client.y + sy_off, before.top + 1,
                               before.bottom - 1);
    const int mx1 = std::clamp(bx - client.x + sx_off, before.left + 1,
                               before.right - 1);
    const int my1 = std::clamp(by - client.y + sy_off, before.top + 1,
                               before.bottom - 1);
    if (mx1 - mx0 < 4 || my1 - my0 < 4) return false;

    const int wx0 = before.world_x(mx0), wx1 = before.world_x(mx1);
    const int wy0 = before.world_y(my1), wy1 = before.world_y(my0);  // y is inverted
    const double rect_w = std::max(1, wx1 - wx0);
    const double rect_h = std::max(1, wy1 - wy0);
    const double span_x = std::max(1, before.world_max_x - before.world_min_x);
    const double span_y = std::max(1, before.world_max_y - before.world_min_y);
    const double factor =
        std::min(span_x / rect_w, span_y / rect_h);
    int level = static_cast<int>(std::ceil(std::log(factor) / std::log(1.4)));
    level = std::clamp(level, 0, 6);

    RenderOptions updated = cur;
    updated.zoom = std::pow(1.4f, static_cast<float>(level));
    updated.pan_x = 0;
    updated.pan_y = 0;
    const MapProjection after = map_projection(ds, updated);
    const int cx = (wx0 + wx1) / 2, cy = (wy0 + wy1) / 2;
    const int panx = cx - (after.world_min_x + after.world_max_x) / 2;
    const int pany = cy - (after.world_min_y + after.world_max_y) / 2;
    system.set_camp_zoom(level);
    system.set_camp_pan(panx, pany);
    return true;
}

void draw_gui(SDL_Surface* surface, GuiSystem& system, const Palette& palette,
              const GuiRenderOptions& options, const IlbmImage* backdrop) {
    if (surface == nullptr) return;
    SDL_PixelFormat* fmt = surface->format;

    // ---- backdrop ---------------------------------------------------------
    fill_rect(surface, 0, 0, surface->w, surface->h, map_rgb(fmt, palette[0]));
    if (options.backdrop && backdrop != nullptr && backdrop->width > 0 &&
        backdrop->height > 0) {
        // Nearest-neighbour scale to the surface. The shell art is 640x400,
        // which is also the game's mode (the original's own bottom bars are
        // clipped at that height too).
        for (int y = 0; y < surface->h; ++y) {
            const int sy = y * backdrop->height / surface->h;
            auto* row = static_cast<Uint32*>(static_cast<void*>(
                static_cast<Uint8*>(surface->pixels) + y * surface->pitch));
            for (int x = 0; x < surface->w; ++x) {
                const int sx = x * backdrop->width / surface->w;
                const std::uint8_t index =
                    backdrop->pixels[static_cast<std::size_t>(sy) *
                                         static_cast<std::size_t>(backdrop->width) +
                                     static_cast<std::size_t>(sx)];
                const std::uint32_t rgb =
                    index < backdrop->palette.size() ? backdrop->palette[index] : 0;
                row[x] = map_rgb(fmt, rgb);
            }
        }
    }

    // ---- windows, back to front ------------------------------------------
    std::vector<GuiWindow*> order;
    for (auto& w : system.screen().windows) {
        if (w.visible && w.w > 0 && w.h > 0) order.push_back(&w);
    }
    std::sort(order.begin(), order.end(),
              [](const GuiWindow* a, const GuiWindow* b) { return a->z < b->z; });

    TTF_Font* font = nullptr;
    if (TTF_WasInit() == 0) TTF_Init();
    font = TTF_OpenFont(kFontPath, 11);

    auto text = [&](int x, int y, const std::string& s, std::uint32_t rgb) {
        if (s.empty()) return;
        if (font == nullptr) return;
        SDL_Color c{static_cast<Uint8>((rgb >> 16) & 0xFF), static_cast<Uint8>((rgb >> 8) & 0xFF),
                    static_cast<Uint8>(rgb & 0xFF), 255};
        SDL_Surface* ts = TTF_RenderUTF8_Blended(font, s.c_str(), c);
        if (ts == nullptr) return;
        SDL_Rect dst{x, y, ts->w, ts->h};
        SDL_BlitSurface(ts, nullptr, surface, &dst);
        SDL_FreeSurface(ts);
    };
    auto text_width = [&](const std::string& s) -> int {
        if (font == nullptr || s.empty()) return static_cast<int>(s.size()) * 6;
        int w = 0, h = 0;
        if (TTF_SizeUTF8(font, s.c_str(), &w, &h) != 0) return static_cast<int>(s.size()) * 6;
        return w;
    };

    for (GuiWindow* win : order) {
        const bool hovered = options.hover_window == win->name;
        // Background: `bkd` if given, else the standard panel fill.
        const int fill_index = win->bkd > 0 ? win->bkd : 10;
        fill_rect(surface, win->x + 2, win->y + 2, win->w - 4, win->h - 4,
                  map_rgb(fmt, palette[static_cast<std::size_t>(fill_index) & 0xFF]));
        frame(surface, win->x, win->y, win->w, win->h, palette, false);

        const std::string title = win->title_text(options.language);
        if (!title.empty()) {
            fill_rect(surface, win->x + 2, win->y + 2, win->w - 4, 13,
                      map_rgb(fmt, palette[hovered ? 12 : 11]));
            hline(surface, win->x + 2, win->y + 15, win->w - 4, map_rgb(fmt, palette[9]));
            text(win->x + 5, win->y + 1, title, palette[15]);
        }

        // Windows whose content is a text resource (credits) draw it directly.
        const SDL_Rect text_client = GuiSystem::client_rect(*win);
        for (std::size_t i = 0; i < win->lines.size(); ++i) {
            const int y = text_client.y + 2 + static_cast<int>(i) * 12;
            if (y + 12 > text_client.y + text_client.h) break;
            text(text_client.x + 4, y, win->lines[i], palette[15]);
        }

        // Strategic map, when the driver supplied a dataset. The planner and
        // campaign screens name their map window `mp_map` and `map`.
        if (options.dataset != nullptr && gui_is_map_window(win->name)) {
            const SDL_Rect client = GuiSystem::client_rect(*win);
            if (client.w > 8 && client.h > 8) {
                const int work_w = map_work_w(*win, client.w);
                const int work_h = map_work_h(*win, client.h);
                const int max_sx = std::max(0, work_w - client.w);
                const int max_sy = std::max(0, work_h - client.h);
                const int sx_off = std::clamp(win->scroll_x_pos, 0, max_sx);
                const int sy_off = std::clamp(win->scroll_y_pos, 0, max_sy);
                SDL_Surface* map =
                    map_for(*options.dataset, system, *win, work_w, work_h,
                            surface->format);
                if (map != nullptr) {
                    // Both surfaces share a format, so copy rows directly.
                    // SDL_BlitSurface rejects some format pairs created this
                    // way, and a memcpy is both safe and fast here.
                    if (SDL_LockSurface(surface) == 0) {
                        if (SDL_LockSurface(map) == 0) {
                            const int copy_w =
                                std::max(0, std::min(client.w, map->w - sx_off));
                            const int bytes = copy_w * 4;
                            for (int y = 0;
                                 y < std::min(client.h, map->h - sy_off); ++y) {
                                const int sy = client.y + y;
                                if (sy < 0 || sy >= surface->h) continue;
                                Uint8* dst = static_cast<Uint8*>(surface->pixels) +
                                             sy * surface->pitch + client.x * 4;
                                const Uint8* src =
                                    static_cast<const Uint8*>(map->pixels) +
                                    (sy_off + y) * map->pitch + sx_off * 4;
                                std::memcpy(dst, src, static_cast<std::size_t>(bytes));
                            }
                            SDL_UnlockSurface(map);
                        }
                        SDL_UnlockSurface(surface);
                    }

                    // Planner edits drawn over the map: appended waypoints and
                    // the selected target area. They live in the GUI model, so
                    // the map surface itself stays cacheable.
                    if (win->name == "mp_map") {
                        const RenderOptions opt =
                            map_options(system, *win, work_w, work_h);
                        const MapProjection p =
                            map_projection(*options.dataset, opt);
                        for (const auto& w : system.mp_waypoints()) {
                            const int x = client.x + p.screen_x(w.first) - sx_off;
                            const int y = client.y + p.screen_y(w.second) - sy_off;
                            fill_rect(surface, x - 2, y - 2, 5, 5,
                                      map_rgb(fmt, 0x33FF99));
                        }
                        const auto& missions = options.dataset->missions;
                        const int mi = system.camp_mission();
                        if (mi >= 0 && mi < static_cast<int>(missions.size())) {
                            const auto& m = missions[mi];
                            const int ti = system.mp_target_index();
                            const int ai = system.mp_area_index();
                            if (ti >= 0 && ti < static_cast<int>(m.targets.size()) &&
                                ai >= 0 &&
                                ai < static_cast<int>(m.targets[ti].areas.size()) &&
                                m.targets[ti].areas[ai].pos) {
                                const auto& pos = *m.targets[ti].areas[ai].pos;
                                const int cx = client.x + p.screen_x(pos.x) - sx_off;
                                const int cy = client.y + p.screen_y(pos.y) - sy_off;
                                const Uint32 ring = map_rgb(fmt, 0xFFF1A0);
                                for (int a = 0; a < 360; a += 12) {
                                    const double r = 0.01745329252 * a;
                                    fill_rect(surface,
                                              cx + static_cast<int>(std::lround(11 * std::cos(r))),
                                              cy + static_cast<int>(std::lround(11 * std::sin(r))),
                                              1, 1, ring);
                                }
                            }
                        }
                    }

                    // Zoom-mode rubber band: screen coordinates clipped to
                    // this map's client area.
                    if (options.band) {
                        const Uint32 band = map_rgb(fmt, 0xFFF1A0);
                        const int bx0 = std::clamp(
                            std::min(options.band_x0, options.band_x1), client.x,
                            client.x + client.w - 1);
                        const int bx1 = std::clamp(
                            std::max(options.band_x0, options.band_x1), client.x,
                            client.x + client.w - 1);
                        const int by0 = std::clamp(
                            std::min(options.band_y0, options.band_y1), client.y,
                            client.y + client.h - 1);
                        const int by1 = std::clamp(
                            std::max(options.band_y0, options.band_y1), client.y,
                            client.y + client.h - 1);
                        hline(surface, bx0, by0, bx1 - bx0 + 1, band);
                        hline(surface, bx0, by1, bx1 - bx0 + 1, band);
                        vline(surface, bx0, by0, by1 - by0 + 1, band);
                        vline(surface, bx1, by0, by1 - by0 + 1, band);
                    }
                }
            }
        }

        // Map scrollbar: the work area is taller than the client, so the
        // original shows a vertical bar over the right edge.
        if (gui_is_map_window(win->name)) {
            MapScrollbar sb;
            const SDL_Rect client = GuiSystem::client_rect(*win);
            if (map_scrollbar(*win, client, &sb)) {
                fill_rect(surface, sb.track.x, sb.track.y, sb.track.w, sb.track.h,
                          map_rgb(fmt, palette[9]));
                frame(surface, sb.track.x, sb.track.y, sb.track.w, sb.track.h,
                      palette, false);
                const Uint32 fg = map_rgb(fmt, palette[14]);
                for (int i = 0; i < 3; ++i) {
                    hline(surface, sb.track.x + 4 + i, sb.track.y + 7 - i,
                          5 - 2 * i, fg);
                    hline(surface, sb.track.x + 4 + i,
                          sb.track.y + sb.track.h - 8 + i, 5 - 2 * i, fg);
                }
                fill_rect(surface, sb.thumb.x + 1, sb.thumb.y, sb.thumb.w - 2,
                          sb.thumb.h, map_rgb(fmt, palette[12]));
                frame(surface, sb.thumb.x, sb.thumb.y, sb.thumb.w, sb.thumb.h,
                      palette, false);
            }
        }

        // Icons live in the client area; scrollable windows offset them and
        // clip to the client rectangle.
        const SDL_Rect client = GuiSystem::client_rect(*win);
        SDL_Rect previous_clip;
        SDL_GetClipRect(surface, &previous_clip);
        if (win->scroll_x || win->scroll_y) SDL_SetClipRect(surface, &client);

        for (auto& icon : win->icons) {
            if (icon.disable || icon.hidden) continue;
            const bool selected = system.is_selected(win->name, icon.name);
            const bool icon_hover =
                hovered && options.hover_icon == icon.name;

            const int left = client.x + icon.x - win->scroll_x_pos;
            const int top = client.y + icon.y - win->scroll_y_pos;
            const int w = icon.w;
            const int h = icon.h;

            // `0` is hollow, not black.
            int bg = selected ? (icon.bkds > 0 ? icon.bkds : icon.bkd) : icon.bkd;
            int border = selected ? (icon.boxs > 0 ? icon.boxs : icon.box) : icon.box;
            if (icon_hover && icon.boxs > 0) border = icon.boxs;

            if (visible_colour(bg) && w > 0 && h > 0) {
                fill_rect(surface, left, top, w, h,
                          map_rgb(fmt, palette[static_cast<std::size_t>(bg) & 0xFF]));
            }
            if (visible_colour(icon.line) && w > 0) {
                hline(surface, left, top, w,
                      map_rgb(fmt, palette[static_cast<std::size_t>(icon.line) & 0xFF]));
            }
            if (visible_colour(border) && w > 0 && h > 0) {
                frame(surface, left, top, w, h, palette, false);
            }

            // An editable field shows its current contents rather than the
            // declared label. A few planner labels are live values from the
            // GUI model (the selected target and takeoff base).
            std::string label =
                icon.editable_len >= 0 ? icon.value : icon.label(options.language);
            if (win->name == "mp_strike_info" && icon.name == "name") {
                label = system.mp_target().empty() ? "N/A" : system.mp_target();
            } else if (win->name == "mp_strike_info" && icon.name == "targeted") {
                label = system.mp_target().empty() ? "N/A" : "YES";
            } else if (win->name == "mp_base_set" && icon.name == "base") {
                label = system.mp_base().empty() ? "------" : system.mp_base();
            } else if (icon.name == "mode" || icon.name == "mapmode") {
                label = system.camp_zoom_mode() ? "ZOOM" : "EDIT";
            }
            if (!label.empty()) {
                const int tw = text_width(label);
                int tx = left + 2;
                if (icon.textpos == "centre") {
                    tx = left + std::max((w - tw) / 2, 0);
                } else if (icon.textpos == "right") {
                    tx = left + std::max(w - tw - 2, 0);
                }
                int ty = top + 1;
                if (h > 14) ty = top + (h - 13) / 2;
                const int colour_index = selected && icon.txtclrs > 0
                                             ? icon.txtclrs
                                             : (icon.txtclr > 0 ? icon.txtclr : 15);
                text(tx, ty, label,
                     palette[static_cast<std::size_t>(colour_index) & 0xFF]);
            }
            if (icon.editable_len >= 0 && system.focused_field() == &icon) {
                const int caret_x = left + 2 + text_width(label) + 1;
                fill_rect(surface, caret_x, top + 2, 1, std::max(h - 4, 6),
                          palette[15]);
            }
        }
        if (win->scroll_x || win->scroll_y) SDL_SetClipRect(surface, &previous_clip);
    }

    // ---- placeholder cursor ----------------------------------------------
    if (options.cursor && options.cursor_x >= 0) {
        const int x = options.cursor_x;
        const int y = options.cursor_y;
        const Uint32 white = map_rgb(fmt, 0xFFFFFF);
        const Uint32 black = map_rgb(fmt, 0x000000);
        for (int i = 0; i < 10; ++i) {
            fill_rect(surface, x + i, y + i, 2, 1, black);
            fill_rect(surface, x + i, y + i, 1, 2, black);
        }
        for (int i = 0; i < 8; ++i) {
            fill_rect(surface, x + i, y + i, 1, 1, white);
        }
        fill_rect(surface, x + 2, y + 7, 4, 1, black);
        fill_rect(surface, x + 3, y + 8, 3, 3, black);
    }

    if (font != nullptr) TTF_CloseFont(font);
}

bool render_gui_png(const DidArchive& archive, const std::string& screen_name,
                    const std::string& path, const GuiRenderOptions& options) {
    GuiLoader loader(archive);
    std::string error;
    auto screen = loader.load(screen_name, &error);
    if (!screen) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return false;
    }

    // `pal weappal` and friends name palettes that are not in the archive, so
    // fall back to the GUI palette rather than rendering everything black.
    Palette palette{};
    if (auto loaded = load_gui_palette(archive, screen->palette)) palette = *loaded;
    if (palette[15] == 0 && palette[1] == 0) {
        if (auto fallback = load_gui_palette(archive, "gui")) palette = *fallback;
    }
    // DID1ST.DAT carries the later screens but no palettes at all; those live
    // in DID.DAT. Read the companion archive rather than render in black.
    if (palette[15] == 0 && palette[1] == 0 && !options.data_dir.empty()) {
        const std::filesystem::path companion =
            std::filesystem::path(options.data_dir) / "DID.DAT";
        if (std::filesystem::exists(companion)) {
            try {
                DidArchive other(companion);
                if (auto loaded = load_gui_palette(other, screen->palette))
                    palette = *loaded;
                else if (auto gui = load_gui_palette(other, "gui"))
                    palette = *gui;
                else if (auto plain = load_gui_palette(other, "2000"))
                    palette = *plain;
            } catch (const std::exception&) {
                // Keep the black palette; the screen still renders geometry.
            }
        }
    }

    GuiSystem system;
    system.set_screen(std::move(*screen));
    // Screens declare a `startup` routine; without it the windows that routine
    // raises (Network Settings, IPX Settings) never appear. The renderer does
    // not know the handlers, so it registers the same observed set as the
    // driver does.
    ef::register_observed_routines(system, options.dataset);
    system.run_startup();
    if (options.camp_zoom >= 0) system.set_camp_zoom(options.camp_zoom);
    if (options.camp_sam) system.set_camp_sam(true);
    if (options.camp_ewr) system.set_camp_ewr(true);
    if (options.camp_sides >= 0)
        system.set_camp_side_mask(static_cast<unsigned>(options.camp_sides));
    if (options.camp_layers >= 0)
        system.set_camp_layer_mask(static_cast<unsigned>(options.camp_layers));
    if (options.camp_pan_x != 0 || options.camp_pan_y != 0)
        system.set_camp_pan(options.camp_pan_x, options.camp_pan_y);
    if (options.camp_mode >= 0) system.set_camp_map_mode(options.camp_mode);
    if (options.camp_flags >= 0) system.set_camp_flags(options.camp_flags != 0);
    if (options.camp_fuel >= 0) system.set_mp_icons(options.camp_fuel != 0);
    if (!options.open_window.empty()) {
        system.open_window(options.open_window);
        system.bring_to_front(options.open_window);
    }

    SDL_Surface* surface =
        SDL_CreateRGBSurfaceWithFormat(0, kScreenW, kScreenH, 32, SDL_PIXELFORMAT_RGBA32);
    if (surface == nullptr) return false;
    std::optional<IlbmImage> backdrop;
    if (options.backdrop) backdrop = load_backdrop(archive, system.screen().picture);
    fill_credits(system, archive);
    if (!options.data_dir.empty()) fill_hiscores(system, options.data_dir);
    draw_gui(surface, system, palette, options, backdrop ? &*backdrop : nullptr);

    SDL_Surface* converted = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_RGB24, 0);
    SDL_FreeSurface(surface);
    if (converted == nullptr) return false;
    const bool ok = IMG_SavePNG(converted, path.c_str()) == 0;
    SDL_FreeSurface(converted);
    return ok;
}

}  // namespace ef
