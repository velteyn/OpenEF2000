// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 velteyn

#include "guirender.h"

#include "fonts.h"
#include "render.h"
#include "stats.h"

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

int force_category_value(const std::vector<ForceCategory>& rows,
                         const std::string& name, int side) {
    for (const auto& row : rows) {
        if (row.name == name) return row.counts[static_cast<std::size_t>(side)];
    }
    return 0;
}

//! Value for a summary bar row prefix (fit/bom/anti/ewrsite/...).
int summary_category(const ForceStats& stats, const std::string& window,
                     const std::string& prefix, int side) {
    if (window == "genforcebar") {
        const auto& f = stats.sides[static_cast<std::size_t>(side)];
        if (prefix == "air") return f.aircraft;
        if (prefix == "gbased") return f.ground;
        if (prefix == "ship") return f.ships;
        if (prefix == "static") return f.structures;
    } else if (window == "airforcebar") {
        if (prefix == "fit") return force_category_value(stats.aircraft, "Fighters", side);
        if (prefix == "bom") return force_category_value(stats.aircraft, "Bombers", side);
        if (prefix == "tran") return force_category_value(stats.aircraft, "Transports", side);
        if (prefix == "ref") return force_category_value(stats.aircraft, "Refueller", side);
        if (prefix == "awac") return force_category_value(stats.aircraft, "AWACs", side);
    } else if (window == "groundforcebar") {
        if (prefix == "anti") return force_category_value(stats.ground, "AntiAircraft", side);
        if (prefix == "batt") return force_category_value(stats.ground, "BattleShips", side);
        if (prefix == "trashp") return force_category_value(stats.ground, "Transport Ships", side);
    } else if (window == "statforcebar") {
        if (prefix == "ewrsite") return force_category_value(stats.structures, "EWR Sites", side);
        if (prefix == "chokepnt") return force_category_value(stats.structures, "Choke Points", side);
        if (prefix == "polind") return force_category_value(stats.structures, "Political Ind.", side);
        if (prefix == "oilrig") return force_category_value(stats.structures, "Oilrigs", side);
        if (prefix == "airfield") return force_category_value(stats.structures, "Airfields", side);
        if (prefix == "port") return force_category_value(stats.structures, "Ports", side);
        if (prefix == "dam") return force_category_value(stats.structures, "Dams", side);
    }
    return 0;
}

//! Value for a summary value icon (`fitnbnato`, `antinbneut`, ...).
int summary_value(const ForceStats& stats, const std::string& window,
                  const std::string& icon) {
    if (window == "balance") {
        if (icon == "nato") return stats.side_total(ForceSide::nato);
        if (icon == "neutral") return stats.side_total(ForceSide::neutral);
        if (icon == "enemy") return stats.side_total(ForceSide::enemy);
        return -1;
    }
    static const std::pair<const char*, int> suffixes[] = {
        {"nbnato", 0}, {"nbneut", 1}, {"nbenemy", 2}};
    for (const auto& suffix : suffixes) {
        const std::string s = suffix.first;
        if (icon.size() <= s.size() || !icon.ends_with(s)) continue;
        return summary_category(stats, window, icon.substr(0, icon.size() - s.size()),
                                suffix.second);
    }
    return -1;
}

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
    const char* font_path = ui_font_path();
    font = font_path ? TTF_OpenFont(font_path, 11) : nullptr;

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

    std::optional<ForceStats> force_stats;
    if (options.dataset != nullptr) {
        force_stats = compute_force_stats(*options.dataset);
    }

    for (GuiWindow* win : order) {
        const bool hovered = options.hover_window == win->name;
        // Background: `bkd` if given, else the standard panel fill. The
        // title bar window in `menu.gui` is the only one that uses `bkd 15`;
        // the original leaves the backdrop visible there (the bar is drawn as
        // a frame with the artwork showing through), so 15 means transparent
        // for a window.
        const int fill_index = win->bkd > 0 ? win->bkd : 10;
        if (win->bkd != 15) {
            fill_rect(surface, win->x + 2, win->y + 2, win->w - 4, win->h - 4,
                      map_rgb(fmt,
                              palette[static_cast<std::size_t>(fill_index) & 0xFF]));
        }
        frame(surface, win->x, win->y, win->w, win->h, palette, false);

        std::string title = win->title_text(options.language);
        // The static mission files carry no loss history, so these windows
        // show current strength rather than "destroyed in the last 8 hours".
        if (win->name == "genforcebar") title = "Current Strength";
        else if (win->name == "airforcebar") title = "Current Air Strength";
        else if (win->name == "groundforcebar") title = "Current Ground Strength";
        else if (win->name == "statforcebar") title = "Current Structure Strength";
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
                std::string name = system.mp_base();
                if (name.empty() && options.dataset != nullptr) {
                    const auto& missions = options.dataset->missions;
                    const int mi = system.camp_mission();
                    if (mi >= 0 && mi < static_cast<int>(missions.size())) {
                        const int base = mission_base_index(missions[mi]);
                        if (base >= 0 &&
                            base < static_cast<int>(options.dataset->bases.size())) {
                            name = options.dataset->bases[base].name;
                            if (!name.empty() && name.front() == '"') name.erase(name.begin());
                            if (!name.empty() && name.back() == '"') name.pop_back();
                        }
                    }
                }
                label = name.empty() ? "------" : name;
            } else if (win->name == "mp_base_set" &&
                       (icon.name == "efas" || icon.name == "free_efas") &&
                       options.dataset != nullptr) {
                int base = find_base_index(*options.dataset, system.mp_base());
                if (base < 0) {
                    const auto& missions = options.dataset->missions;
                    const int mi = system.camp_mission();
                    if (mi >= 0 && mi < static_cast<int>(missions.size())) {
                        base = mission_base_index(missions[mi]);
                    }
                }
                label = std::to_string(base_efa_count(*options.dataset, base,
                                                      icon.name == "free_efas"));
            } else if (win->name == "mp_strike_info" && icon.name == "status" &&
                       options.dataset != nullptr) {
                const auto& missions = options.dataset->missions;
                const int mi = system.camp_mission();
                const int ti = system.mp_target_index();
                const int ai = system.mp_area_index();
                if (mi >= 0 && mi < static_cast<int>(missions.size()) &&
                    ti >= 0 && ti < static_cast<int>(missions[mi].targets.size()) &&
                    ai >= 0 &&
                    ai < static_cast<int>(missions[mi].targets[ti].areas.size())) {
                    label = missions[mi].targets[ti].areas[ai].type;
                }
            } else if (icon.name == "mode" || icon.name == "mapmode") {
                label = system.camp_zoom_mode() ? "ZOOM" : "EDIT";
            }
            if (force_stats && options.dataset != nullptr) {
                const int value = summary_value(*force_stats, win->name, icon.name);
                if (value >= 0) label = std::to_string(value);
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
                const int colour_index = selected && icon.txtclrs >= 0
                                             ? icon.txtclrs
                                             : (icon.txtclr >= 0 ? icon.txtclr : 15);
                text(tx, ty, label,
                     palette[static_cast<std::size_t>(colour_index) & 0xFF]);
            }
            if (icon.editable_len >= 0 && system.focused_field() == &icon) {
                const int caret_x = left + 2 + text_width(label) + 1;
                fill_rect(surface, caret_x, top + 2, 1, std::max(h - 4, 6),
                          palette[15]);
            }
        }
        // Summary bars: current strength per side, drawn beside the numeric
        // value icons the data places at x=90.
        if (force_stats && options.dataset != nullptr) {
            const SDL_Rect client = GuiSystem::client_rect(*win);
            const Uint32 side_cols[3] = {map_rgb(fmt, 0x5A9BFF),
                                         map_rgb(fmt, 0xF5D25F),
                                         map_rgb(fmt, 0xF05F5F)};
            auto bar = [&](int x, int y, int w, int h, int value, int maxv,
                           Uint32 colour) {
                fill_rect(surface, x, y, w, h, map_rgb(fmt, palette[9]));
                if (maxv > 0 && value > 0) {
                    const int fill = std::max(2, w * value / maxv);
                    fill_rect(surface, x, y, std::min(fill, w), h, colour);
                }
                frame(surface, x, y, w, h, palette, true);
            };

            if (win->name == "balance") {
                int maxv = 1;
                for (int side = 0; side < 3; ++side) {
                    maxv = std::max(maxv,
                                    force_stats->sides[static_cast<std::size_t>(side)].total());
                }
                for (int side = 0; side < 3; ++side) {
                    const int y = client.y + 5 + side * 15 - win->scroll_y_pos;
                    bar(client.x + 95, y, std::max(20, client.w - 105), 8,
                        force_stats->sides[static_cast<std::size_t>(side)].total(),
                        maxv, side_cols[side]);
                }
            } else {
                static const std::pair<const char*, int> gen_rows[] = {
                    {"air", 10}, {"gbased", 64}, {"ship", 120}, {"static", 175}};
                static const std::pair<const char*, int> air_rows[] = {
                    {"fit", 5}, {"bom", 60}, {"tran", 115},
                    {"ref", 170}, {"awac", 225}};
                static const std::pair<const char*, int> ground_rows[] = {
                    {"anti", 5}, {"batt", 60}, {"trashp", 115}};
                static const std::pair<const char*, int> stat_rows[] = {
                    {"ewrsite", 5}, {"chokepnt", 60}, {"polind", 115},
                    {"oilrig", 170}, {"airfield", 225}, {"port", 280},
                    {"dam", 335}};
                const std::pair<const char*, int>* rows = nullptr;
                int row_count = 0;
                if (win->name == "genforcebar") {
                    rows = gen_rows; row_count = 4;
                } else if (win->name == "airforcebar") {
                    rows = air_rows; row_count = 5;
                } else if (win->name == "groundforcebar") {
                    rows = ground_rows; row_count = 3;
                } else if (win->name == "statforcebar") {
                    rows = stat_rows; row_count = 7;
                }
                if (rows != nullptr) {
                    int maxv = 1;
                    for (int r = 0; r < row_count; ++r) {
                        for (int side = 0; side < 3; ++side) {
                            maxv = std::max(maxv, summary_category(
                                *force_stats, win->name, rows[r].first, side));
                        }
                    }
                    for (int r = 0; r < row_count; ++r) {
                        for (int side = 0; side < 3; ++side) {
                            const int value = summary_category(
                                *force_stats, win->name, rows[r].first, side);
                            const int y = client.y + rows[r].second + side * 15 -
                                          win->scroll_y_pos;
                            if (y < client.y || y + 6 > client.y + client.h) continue;
                            bar(client.x + 120, y, std::max(20, client.w - 128), 6,
                                value, maxv, side_cols[side]);
                        }
                    }
                }
            }
        }

        // Summary graphs: a grouped current-strength chart. The shipped
        // windows are time-series plots, but the static files have no history,
        // so the bars show the same current counts as the bar windows.
        if (force_stats && options.dataset != nullptr) {
            static const std::pair<const char*, int> gen_rows[] = {
                {"air", 0}, {"gbased", 0}, {"ship", 0}, {"static", 0}};
            static const std::pair<const char*, int> air_rows[] = {
                {"fit", 0}, {"bom", 0}, {"tran", 0}, {"ref", 0}, {"awac", 0}};
            static const std::pair<const char*, int> ground_rows[] = {
                {"anti", 0}, {"batt", 0}, {"trashp", 0}};
            static const std::pair<const char*, int> stat_rows[] = {
                {"ewrsite", 0}, {"chokepnt", 0}, {"polind", 0},
                {"oilrig", 0}, {"airfield", 0}, {"port", 0}, {"dam", 0}};
            const std::pair<const char*, int>* rows = nullptr;
            int row_count = 0;
            if (win->name == "genforcegraph") {
                rows = gen_rows; row_count = 4;
            } else if (win->name == "airforcegraph") {
                rows = air_rows; row_count = 5;
            } else if (win->name == "groundforcegraph") {
                rows = ground_rows; row_count = 3;
            } else if (win->name == "statforcegraph") {
                rows = stat_rows; row_count = 7;
            }
            if (rows != nullptr && row_count > 0) {
                const SDL_Rect client = GuiSystem::client_rect(*win);
                const int chart_x = client.x + 12;
                const int chart_y = client.y + 12;
                const int chart_w = std::max(40, client.w - 24);
                const int chart_h = std::max(40, client.h - 30);
                const int baseline = chart_y + chart_h;
                int maxv = 1;
                for (int r = 0; r < row_count; ++r) {
                    for (int side = 0; side < 3; ++side) {
                        maxv = std::max(maxv, summary_category(
                            *force_stats, win->name == "genforcegraph" ? "genforcebar"
                                          : win->name == "airforcegraph" ? "airforcebar"
                                          : win->name == "groundforcegraph" ? "groundforcebar"
                                          : "statforcebar",
                            rows[r].first, side));
                    }
                }
                const Uint32 cols[3] = {map_rgb(fmt, 0x5A9BFF),
                                        map_rgb(fmt, 0xF5D25F),
                                        map_rgb(fmt, 0xF05F5F)};
                const std::string bar_window =
                    win->name == "genforcegraph" ? "genforcebar"
                    : win->name == "airforcegraph" ? "airforcebar"
                    : win->name == "groundforcegraph" ? "groundforcebar"
                                                      : "statforcebar";
                const int group_w = std::max(6, chart_w / row_count);
                const int bar_w = std::max(2, (group_w - 6) / 3);
                hline(surface, chart_x, baseline, chart_w,
                      map_rgb(fmt, palette[9]));
                for (int r = 0; r < row_count; ++r) {
                    for (int side = 0; side < 3; ++side) {
                        const int value = summary_category(*force_stats, bar_window,
                                                           rows[r].first, side);
                        const int h = value > 0 ? std::max(2, chart_h * value / maxv) : 0;
                        const int x = chart_x + r * group_w + 3 + side * bar_w;
                        if (h > 0) fill_rect(surface, x, baseline - h, bar_w - 1, h,
                                             cols[side]);
                    }
                }
            }
        }

        // The loaded map-bar variant does not always carry the `mode` icon
        // (the running original shows `mAp mode: EDIT`). Draw the state at
        // the right of the bar so the `a` toggle is always visible.
        if (win->name == "mp_map_options") {
            bool has_mode = false;
            for (const auto& icon : win->icons) {
                if (icon.name == "mode" || icon.name == "mapmode") has_mode = true;
            }
            if (!has_mode) {
                const std::string mode = system.camp_zoom_mode() ? "ZOOM" : "EDIT";
                text(win->x + win->w - 30, win->y + 2, mode, palette[15]);
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
    if (options.camp_mission >= 0) {
        if (options.dataset != nullptr) {
            ef::select_campaign_mission(system, *options.dataset,
                                        options.camp_mission);
        } else {
            system.set_camp_mission(options.camp_mission);
        }
    }
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
