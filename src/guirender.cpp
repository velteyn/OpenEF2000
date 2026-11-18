// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 velteyn

#include "guirender.h"

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

void draw_gui(SDL_Surface* surface, GuiSystem& system, const Palette& palette,
              const GuiRenderOptions& options, const IlbmImage* backdrop) {
    if (surface == nullptr) return;
    SDL_PixelFormat* fmt = surface->format;

    // ---- backdrop ---------------------------------------------------------
    fill_rect(surface, 0, 0, surface->w, surface->h, map_rgb(fmt, palette[0]));
    if (options.backdrop && backdrop != nullptr && backdrop->width > 0 &&
        backdrop->height > 0) {
        // Nearest-neighbour scale to the surface.
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
            // declared label.
            const std::string label =
                icon.editable_len >= 0 ? icon.value : icon.label(options.language);
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

    GuiSystem system;
    system.set_screen(std::move(*screen));
    // Screens declare a `startup` routine; without it the windows that routine
    // raises (Network Settings, IPX Settings) never appear. The renderer does
    // not know the handlers, so it registers the same observed set as the
    // driver does.
    ef::register_observed_routines(system);
    system.run_startup();
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
    draw_gui(surface, system, palette, options, backdrop ? &*backdrop : nullptr);

    SDL_Surface* converted = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_RGB24, 0);
    SDL_FreeSurface(surface);
    if (converted == nullptr) return false;
    const bool ok = IMG_SavePNG(converted, path.c_str()) == 0;
    SDL_FreeSurface(converted);
    return ok;
}

}  // namespace ef
