// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 velteyn

#include "did.h"
#include "guirender.h"
#include "meshview.h"
#include "modelview.h"
#include "parser.h"
#include "render.h"
#include "shape3d.h"
#include "texture.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {

void usage() {
    std::printf(
        "OpenEF2000\n"
        "  --data DIR          game data directory (default: ./game-data)\n"
        "  --mission N         mission index (default: 0)\n"
        "  --stats             print dataset counts\n"
        "  --list              list missions\n"
        "  --focus             fit the map to the selected mission\n"
        "  --sam-coverage      draw SAM engagement rings (radius is assumed)\n"
        "  --ewr-coverage      draw early-warning radar rings\n"
        "  --coverage-scale N  scale those assumed radii (default 1.0)\n"
        "  --bounds X0,Y0,X1,Y1  frame the map explicitly (world units)\n"
        "  --export FILE.png   render the selected mission to PNG\n"
        "  --interactive       open the SDL window\n"
        "  --models            list the 3D shape entries of DID.DAT\n"
        "  --mesh NAME         load one shape (e.g. efa, f22) from DID.DAT\n"
        "  --lod N             level of detail for --mesh (default: 0)\n"
        "  --export-mesh FILE  write the loaded mesh as Wavefront OBJ\n"
        "  --mesh-preview FILE render the loaded mesh to a PNG sheet\n"
        "  --clumps FILE       clump-name list for the model viewer\n"
        "  --camera YAW,PITCH[,ZOOM]  viewer camera angles in degrees\n"
        "  --render FILE.png   render the textured mesh to PNG (uses --camera)\n"
        "  --texture NAME      force one .tm atlas for every polygon\n"
        "  --palette NAME      palette in colours\\<NAME>.col (default 2000)\n"
        "  --gui SCREEN        load a GUI screen, e.g. mainmenu or missplan\n"
        "  --gui-render FILE   render that screen to PNG (headless)\n"
        "  --gui-list          list the GUI screens in DID.DAT\n"
        "  --gui-routines      with --gui SCREEN, list the routines it needs\n"
        "  --gui-selftest      run deterministic GUI runtime checks\n"
        "  --gui-lang LANG     en|fr|de (default en)\n");
}

//! Locates the game data when --data is not given. The engine never ships
//! assets, so the conventional place is a local `game-data` directory that the
//! user populates from their own copy of the game.
std::string default_data() {
    namespace fs = std::filesystem;
    for (const char* candidate : {"game-data", "../game-data", "../../game-data"}) {
        if (fs::exists(fs::path(candidate) / "DID.DAT") ||
            fs::exists(fs::path(candidate) / "MISSION.CFG")) {
            return candidate;
        }
    }
    return "game-data";
}

struct CoverageOptions {
    bool sam = false;
    bool ewr = false;
    float scale = 1.0f;
};

int run_interactive(const ef::Dataset& ds, int selected, bool focus,
                    const CoverageOptions& coverage) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window* win = SDL_CreateWindow("OpenEF2000",
                                       SDL_WINDOWPOS_CENTERED,
                                       SDL_WINDOWPOS_CENTERED, 1280, 900, 0);
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!win || !ren) {
        std::fprintf(stderr, "SDL window: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    int n = static_cast<int>(ds.missions.size());
    selected = std::clamp(selected, 0, std::max(0, n - 1));
    ef::print_mission(ds, selected);
    bool running = true;
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                running = false;
            } else if (ev.type == SDL_KEYDOWN) {
                switch (ev.key.keysym.sym) {
                    case SDLK_ESCAPE:
                        running = false;
                        break;
                    case SDLK_n:
                    case SDLK_DOWN:
                    case SDLK_PAGEDOWN:
                        if (n > 0) selected = std::min(selected + 1, n - 1);
                        ef::print_mission(ds, selected);
                        break;
                    case SDLK_p:
                    case SDLK_UP:
                    case SDLK_PAGEUP:
                        if (n > 0) selected = std::max(selected - 1, 0);
                        ef::print_mission(ds, selected);
                        break;
                    case SDLK_HOME:
                        selected = 0;
                        ef::print_mission(ds, selected);
                        break;
                    case SDLK_END:
                        selected = std::max(0, n - 1);
                        ef::print_mission(ds, selected);
                        break;
                    case SDLK_s: {
                        ef::RenderOptions opt;
                        opt.selected = selected;
                        opt.focus = focus;
                        opt.sam_coverage = coverage.sam;
                        opt.ewr_coverage = coverage.ewr;
                        opt.coverage_scale = coverage.scale;
                        SDL_Surface* surf = ef::render_map(ds, opt);
                        if (surf) {
                            bool ok = ef::save_png(surf, "ef2000-mission.png");
                            std::printf("save %s\n", ok ? "ok" : "failed");
                            SDL_FreeSurface(surf);
                        }
                        break;
                    }
                    default:
                        break;
                }
            }
        }

        ef::RenderOptions opt;
        opt.selected = selected;
        opt.focus = focus;
        opt.sam_coverage = coverage.sam;
        opt.ewr_coverage = coverage.ewr;
        opt.coverage_scale = coverage.scale;
        SDL_Surface* surf = ef::render_map(ds, opt);
        if (surf) {
            SDL_Texture* tex = SDL_CreateTextureFromSurface(ren, surf);
            if (tex) {
                SDL_RenderClear(ren);
                SDL_RenderCopy(ren, tex, nullptr, nullptr);
                SDL_RenderPresent(ren);
                SDL_DestroyTexture(tex);
            }
            SDL_FreeSurface(surf);
        }
        SDL_Delay(16);
    }

    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}

// A set of `.3` shapes that the interactive viewer can cycle through.
class ModelSet {
public:
    ModelSet(const ef::DidArchive& archive, std::vector<std::string> names)
        : archive_(archive), names_(std::move(names)) {}

    const std::vector<std::string>& names() const { return names_; }
    int size() const { return static_cast<int>(names_.size()); }
    int lod() const { return lod_; }
    const std::string& current_name() const { return current_; }
    const ef::Mesh& mesh() const { return mesh_; }
    const ef::Shape* shape() const { return shape_.get(); }

    bool load(int index, int lod) {
        if (names_.empty()) return false;
        index = ((index % size()) + size()) % size();
        lod_ = lod;
        index_ = index;

        auto entry = archive_.resolve(names_[static_cast<std::size_t>(index)], "3\\", ".3");
        if (!entry) return false;
        auto bytes = archive_.read(*entry);
        if (!bytes) return false;
        auto shape = std::make_unique<ef::Shape>(ef::Shape::parse(*bytes));
        if (lod < 0 || static_cast<std::size_t>(lod) >= shape->lods().size()) return false;
        mesh_ = ef::execute_shape(*shape, static_cast<std::size_t>(lod), ef::ShapeParameters{}, 0);
        shape_ = std::move(shape);
        current_ = names_[static_cast<std::size_t>(index)];
        return true;
    }

    int index() const { return index_; }

private:
    const ef::DidArchive& archive_;
    std::vector<std::string> names_;
    std::unique_ptr<ef::Shape> shape_;
    ef::Mesh mesh_;
    std::string current_;
    int lod_ = 0;
    int index_ = 0;
};

std::vector<std::string> shape_names(const std::filesystem::path& data,
                                     const ef::DidArchive& archive,
                                     const std::string& override_path) {
    namespace fs = std::filesystem;
    std::vector<fs::path> candidates;
    if (!override_path.empty()) candidates.emplace_back(override_path);
    candidates.push_back(data / "CLUMP_3.TXT");
    candidates.push_back(data.parent_path() / "CLUMP_3.TXT");

    std::vector<std::string> names;
    for (const auto& path : candidates) {
        std::ifstream list(path);
        if (!list) continue;
        std::string name;
        while (std::getline(list, name)) {
            while (!name.empty() && (name.back() == '\r' || name.back() == ' ')) name.pop_back();
            if (name.empty()) continue;
            if (archive.resolve(name, "3\\", ".3")) names.push_back(name);
        }
        if (!names.empty()) {
            std::printf("model list: %s (%zu meshes)\n", path.string().c_str(), names.size());
            break;
        }
    }
    if (names.empty()) {
        names = {"efa", "f22", "a10", "f15", "f16", "tornado", "su27", "mig29",
                 "mi24", "f117a", "b2", "harrier", "jas39", "f14", "f18", "su35",
                 "mig21", "r767", "hercules", "efacpt"};
        std::vector<std::string> filtered;
        for (const auto& n : names) {
            if (archive.resolve(n, "3\\", ".3")) filtered.push_back(n);
        }
        names = std::move(filtered);
        std::printf("model list: built-in (%zu meshes)\n", names.size());
    }
    return names;
}

// Slot 0 is the shape's hard-coded texture; slot k is `init3d.txt` global k.
//
// This literal mapping is correct: `efa.3` uses slot 6 (tm_efa1, the grey
// camo panel atlas) for the wings and slot 7 (tm_efa2) for the spine, nozzles
// and turbine face, exactly as the opcode indices say.  Earlier attempts to
// infer a single atlas per model were misled by the wrong palette.
ef::TextureSlots build_slots(const ef::TextureBank& textures, const ef::Shape& shape,
                             const std::string& override_name, std::string& chosen) {
    const std::size_t count = textures.global_slots().size() + 1;
    ef::TextureSlots slots(count, nullptr);

    chosen = override_name;
    if (!chosen.empty()) {
        if (const ef::Texture* atlas = textures.get(chosen)) {
            for (auto& slot : slots) slot = atlas;
            return slots;
        }
    }

    if (shape.texture_name()) slots[0] = textures.get(*shape.texture_name());
    const auto& globals = textures.global_slots();
    for (std::size_t k = 1; k < globals.size() && k < count; ++k) {
        if (!globals[k].empty()) slots[k] = textures.get(globals[k]);
    }
    return slots;
}

int run_model_viewer(ModelSet& models, const ef::TextureBank& textures, int start,
                     bool wireframe, ef::OrbitCamera camera,
                     const std::string& texture_override = {}) {
    if (models.size() == 0) {
        std::fprintf(stderr, "no meshes to show\n");
        return 1;
    }
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window* win = SDL_CreateWindow("OpenEF2000 Model Viewer",
                                       SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       1280, 900, SDL_WINDOW_RESIZABLE);
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!win || !ren) {
        std::fprintf(stderr, "SDL window: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    models.load(start, 0);
    camera.wireframe = wireframe;
    bool running = true;
    bool dirty = true;
    int drag_x = 0, drag_y = 0;
    bool dragging = false;

    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                running = false;
            } else if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT) {
                dragging = true;
                drag_x = ev.button.x;
                drag_y = ev.button.y;
            } else if (ev.type == SDL_MOUSEBUTTONUP && ev.button.button == SDL_BUTTON_LEFT) {
                dragging = false;
            } else if (ev.type == SDL_MOUSEMOTION && dragging) {
                camera.rotate(static_cast<float>(ev.motion.x - drag_x) * 0.01f,
                              static_cast<float>(ev.motion.y - drag_y) * 0.01f);
                drag_x = ev.motion.x;
                drag_y = ev.motion.y;
                dirty = true;
            } else if (ev.type == SDL_MOUSEWHEEL) {
                camera.dolly(ev.wheel.y > 0 ? 1.1f : 1.0f / 1.1f);
                dirty = true;
            } else if (ev.type == SDL_KEYDOWN) {
                const int lod = models.lod();
                switch (ev.key.keysym.sym) {
                    case SDLK_ESCAPE: running = false; break;
                    case SDLK_LEFT: camera.rotate(-0.1f, 0.0f); dirty = true; break;
                    case SDLK_RIGHT: camera.rotate(0.1f, 0.0f); dirty = true; break;
                    case SDLK_UP: camera.rotate(0.0f, -0.1f); dirty = true; break;
                    case SDLK_DOWN: camera.rotate(0.0f, 0.1f); dirty = true; break;
                    case SDLK_EQUALS: case SDLK_PLUS: camera.dolly(1.15f); dirty = true; break;
                    case SDLK_MINUS: camera.dolly(1.0f / 1.15f); dirty = true; break;
                    case SDLK_w: camera.wireframe = !camera.wireframe; dirty = true; break;
                    case SDLK_d: camera.decals = !camera.decals; dirty = true; break;
                    case SDLK_c: camera.cull = !camera.cull; dirty = true; break;
                    case SDLK_t: camera.textured = !camera.textured; dirty = true; break;
                    case SDLK_n: models.load(models.index() + 1, 0); dirty = true; break;
                    case SDLK_p: models.load(models.index() - 1, 0); dirty = true; break;
                    case SDLK_l: {
                        const auto* shape = models.shape();
                        const int count = shape ? static_cast<int>(shape->lods().size()) : 1;
                        models.load(models.index(), (lod + 1) % (count > 0 ? count : 1));
                        dirty = true;
                        break;
                    }
                    case SDLK_s: dirty = true; break;
                    default: break;
                }
            }
        }

        if (dirty) {
            ef::TextureSlots slots;
            std::string atlas_name;
            int w = 0, h = 0;
            SDL_GetRendererOutputSize(ren, &w, &h);
            SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA32);
            if (surf) {
                if (camera.textured && models.shape()) {
                    slots = build_slots(textures, *models.shape(), texture_override, atlas_name);
                }
                ef::draw_model(surf, models.mesh(), camera, 0, 0, w, h,
                               camera.textured ? &slots : nullptr, &textures.palette());
                SDL_Texture* tex = SDL_CreateTextureFromSurface(ren, surf);
                if (tex) {
                    SDL_RenderClear(ren);
                    SDL_RenderCopy(ren, tex, nullptr, nullptr);
                    SDL_RenderPresent(ren);
                    SDL_DestroyTexture(tex);
                }
                SDL_FreeSurface(surf);
            }
            std::printf("\r%-10s LOD %d  %zu tris  pal %-9s%s%s   ",
                        models.current_name().c_str(), models.lod(),
                        models.mesh().triangle_count(),
                        atlas_name.empty() ? textures.palette_name().c_str()
                                           : atlas_name.c_str(),
                        camera.textured ? " +tex" : "", camera.decals ? " +decals" : "");
            std::fflush(stdout);
            dirty = false;
        }
        SDL_Delay(8);
    }

    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}

}  // namespace


// Routines whose effect has been established by running the original software
// and observing the result. Keep this list small and evidence-backed: each
// entry is a fact, not a guess, and the rest stay unimplemented on purpose.
void register_observed_routines(ef::GuiSystem& system) {
    // QuitEF2000: its own name, and the `quit`/`sure` windows that reference it.
    system.register_routine("QuitEF2000", [](ef::GuiSystem& gui) {
        gui.request_quit();
    });
    // gpwClickCampaign: pressing C (or clicking the Campaign panel) raises the
    // "Campaign Level Setting" dialog, which is the `campaign_level` window.
    system.register_routine("gpwClickCampaign", [](ef::GuiSystem& gui) {
        gui.open_window("campaign_level");
    });
}

// Interactive GUI driver: hit-testing, hover/selection events and screen
// switching. The data is the original .gui text; only the drawing style is
// approximated.
int run_gui_viewer(const ef::DidArchive& did, const std::string& start_screen,
                   const std::string& lang) {
    ef::GuiLoader loader(did);
    std::string error;
    auto screen = loader.load(start_screen, &error);
    if (!screen) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    ef::Palette palette{};
    if (auto loaded = ef::load_gui_palette(did, screen->palette)) palette = *loaded;
    if (palette[15] == 0) {
        if (auto fallback = ef::load_gui_palette(did, "gui")) palette = *fallback;
    }

    ef::GuiSystem system;
    system.set_screen(std::move(*screen));

    register_observed_routines(system);

    std::optional<ef::IlbmImage> backdrop =
        ef::load_backdrop(did, system.screen().picture);

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window* window = SDL_CreateWindow("OpenEF2000 GUI", SDL_WINDOWPOS_CENTERED,
                                          SDL_WINDOWPOS_CENTERED, 960, 600, SDL_WINDOW_SHOWN);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (window == nullptr || renderer == nullptr) {
        std::fprintf(stderr, "SDL window: %s\n", SDL_GetError());
        return 1;
    }
    SDL_RenderSetLogicalSize(renderer, 640, 400);

    ef::GuiRenderOptions options;
    options.language = lang;

    std::string hover_window, hover_icon;
    std::size_t printed_logs = 0;
    bool dirty = true, running = true;
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = false;

            if (ev.type == SDL_TEXTINPUT) {
                // Typing goes to the focused field; the `.gui` files declare
                // fields with `editable <max length>`.
                if (system.type_text(ev.text.text)) dirty = true;
                continue;
            }
            if (ev.type == SDL_KEYDOWN) {
                const bool have_field = system.focused_field() != nullptr;
                if (ev.key.keysym.sym == SDLK_BACKSPACE && have_field) {
                    if (system.backspace()) dirty = true;
                    continue;
                }
                if (ev.key.keysym.sym == SDLK_ESCAPE) {
                    if (have_field) {
                        system.focus_field(std::string(), std::string());
                        dirty = true;
                    } else {
                        running = false;
                    }
                    continue;
                }
                if (ev.key.keysym.sym == SDLK_RETURN && have_field) {
                    system.focus_field(std::string(), std::string());
                    dirty = true;
                    continue;
                }
                // Do not steal printable keys while a field has focus.
                const std::string name = ef::gui_key_name(ev.key.keysym);
                if (!name.empty() && !have_field) {
                    const bool shift = (ev.key.keysym.mod & KMOD_SHIFT) != 0;
                    if (system.key_press(shift ? "shift+" + name : name)) dirty = true;
                }
                continue;
            }
            if (ev.type == SDL_MOUSEWHEEL) {
                int mx = 0, my = 0;
                SDL_GetMouseState(&mx, &my);
                float lx = 0, ly = 0;
                SDL_RenderWindowToLogical(renderer, mx, my, &lx, &ly);
                if (auto* w = system.window_at(static_cast<int>(lx), static_cast<int>(ly))) {
                    // `xscroll`/`yscroll` declare the axis; three lines a notch.
                    if (system.scroll_window(w->name, 0, -ev.wheel.y * 3)) dirty = true;
                }
                continue;
            }
            if (ev.type == SDL_MOUSEMOTION || ev.type == SDL_MOUSEBUTTONDOWN ||
                ev.type == SDL_MOUSEBUTTONUP) {
                int mx = 0, my = 0;
                SDL_GetMouseState(&mx, &my);
                float lx = 0, ly = 0;
                SDL_RenderWindowToLogical(renderer, mx, my, &lx, &ly);
                const int px = static_cast<int>(lx);
                const int py = static_cast<int>(ly);
                options.cursor_x = px;
                options.cursor_y = py;

                auto* win = system.window_at(px, py);
                std::string icon;
                if (win != nullptr) {
                    // Icons sit in the client area, shifted by the scroll
                    // offset; hit testing must follow the same geometry.
                    const SDL_Rect client = ef::GuiSystem::client_rect(*win);
                    for (auto& i : win->icons) {
                        if (i.disable || i.hidden) continue;
                        const int lx = px - (client.x + i.x - win->scroll_x_pos);
                        const int ly = py - (client.y + i.y - win->scroll_y_pos);
                        if (lx >= 0 && ly >= 0 && lx < i.w && ly < i.h) {
                            icon = i.name;
                            break;
                        }
                    }
                }
                const std::string new_window = win ? win->name : std::string();
                if (new_window != hover_window || icon != hover_icon) {
                    if (!hover_icon.empty()) system.fire(hover_window, hover_icon, "leaving");
                    hover_window = new_window;
                    hover_icon = icon;
                    if (!hover_icon.empty()) system.fire(hover_window, hover_icon, "entering");
                    dirty = true;
                }
                if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT) {
                    // Clicking a text field focuses it; clicking elsewhere blurs.
                    if (!hover_icon.empty()) {
                        system.focus_field(hover_window, hover_icon);
                        system.fire(hover_window, hover_icon, "click");
                    } else {
                        system.focus_field(std::string(), std::string());
                    }
                    dirty = true;
                }
                if (ev.type == SDL_MOUSEBUTTONUP && ev.button.button == SDL_BUTTON_LEFT &&
                    !hover_icon.empty()) {
                    system.fire(hover_window, hover_icon, "release");
                    dirty = true;
                }
                // Surface the verbs that ran: routines are C entry points the
                // reconstruction does not implement yet, so the trace is how
                // we discover what each screen expects.
                const auto& logs = system.log();
                for (; printed_logs < logs.size(); ++printed_logs) {
                    std::fprintf(stderr, "  %s\n", logs[printed_logs].c_str());
                }
                if (ev.type == SDL_MOUSEBUTTONDOWN && !win && hover_icon.empty()) {
                    // Click outside: nothing.
                }
                (void)win;
            }
        }

        if (system.quit_requested()) running = false;

        for (const auto& next : system.take_screen_requests()) {
            auto loaded = loader.load(next, &error);
            if (!loaded) continue;
            system.set_screen(std::move(*loaded));
            backdrop = ef::load_backdrop(did, system.screen().picture);
            // The hover pair belongs to the old screen's windows.
            hover_window.clear();
            hover_icon.clear();
            dirty = true;
        }

        if (dirty) {
            SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, 640, 400, 32,
                                                                  SDL_PIXELFORMAT_RGBA32);
            if (surface != nullptr) {
                options.cursor = true;
                options.hover_window = hover_window;
                options.hover_icon = hover_icon;
                ef::draw_gui(surface, system, palette, options,
                             backdrop ? &*backdrop : nullptr);
                SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surface);
                if (tex != nullptr) {
                    SDL_RenderClear(renderer);
                    SDL_RenderCopy(renderer, tex, nullptr, nullptr);
                    SDL_RenderPresent(renderer);
                    SDL_DestroyTexture(tex);
                }
                SDL_FreeSurface(surface);
            }
            std::printf("\r%-12s %-20s   ", system.screen().name.c_str(),
                        (hover_window + (hover_icon.empty() ? "" : "." + hover_icon)).c_str());
            std::fflush(stdout);
            dirty = false;
        }
        SDL_Delay(8);
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}


// Deterministic checks for the GUI runtime. Uses a synthetic screen so it does
// not depend on game data, window managers or timing, and runs in CI.
int run_gui_selftest() {
    const char* dsl = R"(
window test
xpos 0
ypos 0
xsize 100
ysize 50
open
yscroll

icon tall
xpos 0
ypos 200
xsize 10
ysize 20

icon field
xpos 0
ypos 0
xsize 40
ysize 10
editable 3

icon keyed
xpos 0
ypos 10
xsize 10
ysize 10
key x
call release
routine DoThing

icon sub
xpos 0
ypos 20
xsize 10
ysize 10
call click
submenu extra

window extra
xpos 200
ypos 0
xsize 50
ysize 50
)";

    int failures = 0;
    auto check = [&](const char* what, bool ok) {
        std::printf("  %-46s %s\n", what, ok ? "ok" : "FAIL");
        if (!ok) ++failures;
    };

    ef::GuiSystem system;
    system.set_screen(ef::GuiLoader::parse("selftest", dsl));

    check("window marked `open` is visible", system.find_window("test") != nullptr &&
                                                system.find_window("test")->visible);
    check("window without `open` starts hidden",
          system.find_window("extra") != nullptr && !system.find_window("extra")->visible);

    check("key binding dispatches", system.key_press("x"));
    check("key binding logged the routine",
          system.log().size() == 1 && system.log()[0].find("DoThing") != std::string::npos);
    const auto missing = system.missing_routines();
    check("unregistered routine is reported missing",
          std::find(missing.begin(), missing.end(), "DoThing") != missing.end());

    system.focus_field("test", "field");
    check("editable field takes focus", system.focused_field() != nullptr);
    for (const char* ch : {"a", "b", "c", "d"}) system.type_text(ch);
    auto* field = system.find_widget("test", "field");
    check("editable field stops at its declared length",
          field != nullptr && field->value == "abc");
    system.backspace();
    check("backspace edits the field", field != nullptr && field->value == "ab");

    check("scroll clamps to content", system.scroll_window("test", 0, 1000) &&
                                          system.find_window("test")->scroll_y_pos == 170);
    check("scroll clamps at the top",
          system.scroll_window("test", 0, -1000) &&
              system.find_window("test")->scroll_y_pos == 0);

    system.fire("test", "sub", "click");
    check("submenu opens the named window in the same file",
          system.find_window("extra") != nullptr && system.find_window("extra")->visible);

    std::printf("%s (%d failure%s)\n", failures == 0 ? "gui selftest: PASS" : "gui selftest: FAIL",
                failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}

int main(int argc, char** argv) {
    std::string data = default_data();
    std::string export_path;
    std::string mesh_name;
    std::string export_mesh;
    std::string mesh_preview;
    std::string render_path;
    std::string clumps_path;
    std::string texture_override;
    std::string palette_name = "2000";
    std::string gui_screen;
    std::string gui_render;
    std::string gui_lang = "en";
    bool gui_list = false;
    bool gui_routines = false;
    bool gui_selftest = false;
    bool sam_coverage = false, ewr_coverage = false;
    float coverage_scale = 1.0f;
    std::optional<std::array<int, 4>> map_bounds;
    float cam_yaw = 0.75f, cam_pitch = 0.35f, cam_zoom = 1.0f;
    int mission = 0;
    int lod = 0;
    bool stats = false, list = false, interactive = false, focus = false;
    bool models = false;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--data") == 0 && i + 1 < argc) {
            data = argv[++i];
        } else if (std::strcmp(argv[i], "--mission") == 0 && i + 1 < argc) {
            mission = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--export") == 0 && i + 1 < argc) {
            export_path = argv[++i];
        } else if (std::strcmp(argv[i], "--stats") == 0) {
            stats = true;
        } else if (std::strcmp(argv[i], "--list") == 0) {
            list = true;
        } else if (std::strcmp(argv[i], "--models") == 0) {
            models = true;
        } else if (std::strcmp(argv[i], "--mesh") == 0 && i + 1 < argc) {
            mesh_name = argv[++i];
        } else if (std::strcmp(argv[i], "--lod") == 0 && i + 1 < argc) {
            lod = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--export-mesh") == 0 && i + 1 < argc) {
            export_mesh = argv[++i];
        } else if (std::strcmp(argv[i], "--mesh-preview") == 0 && i + 1 < argc) {
            mesh_preview = argv[++i];
        } else if (std::strcmp(argv[i], "--render") == 0 && i + 1 < argc) {
            render_path = argv[++i];
        } else if (std::strcmp(argv[i], "--clumps") == 0 && i + 1 < argc) {
            clumps_path = argv[++i];
        } else if (std::strcmp(argv[i], "--texture") == 0 && i + 1 < argc) {
            texture_override = argv[++i];
        } else if (std::strcmp(argv[i], "--palette") == 0 && i + 1 < argc) {
            palette_name = argv[++i];
        } else if (std::strcmp(argv[i], "--gui") == 0 && i + 1 < argc) {
            gui_screen = argv[++i];
        } else if (std::strcmp(argv[i], "--gui-render") == 0 && i + 1 < argc) {
            gui_render = argv[++i];
        } else if (std::strcmp(argv[i], "--gui-lang") == 0 && i + 1 < argc) {
            gui_lang = argv[++i];
        } else if (std::strcmp(argv[i], "--gui-list") == 0) {
            gui_list = true;
        } else if (std::strcmp(argv[i], "--gui-routines") == 0) {
            gui_routines = true;
        } else if (std::strcmp(argv[i], "--gui-selftest") == 0) {
            gui_selftest = true;
        } else if (std::strcmp(argv[i], "--sam-coverage") == 0) {
            sam_coverage = true;
        } else if (std::strcmp(argv[i], "--ewr-coverage") == 0) {
            ewr_coverage = true;
        } else if (std::strcmp(argv[i], "--coverage-scale") == 0 && i + 1 < argc) {
            coverage_scale = static_cast<float>(std::atof(argv[++i]));
        } else if (std::strcmp(argv[i], "--bounds") == 0 && i + 1 < argc) {
            int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
            if (std::sscanf(argv[++i], "%d,%d,%d,%d", &x0, &y0, &x1, &y1) == 4) {
                map_bounds = std::array<int, 4>{x0, y0, x1, y1};
            }
        } else if (std::strcmp(argv[i], "--camera") == 0 && i + 1 < argc) {
            // "yaw,pitch,zoom" in degrees (zoom optional)
            if (std::sscanf(argv[++i], "%f,%f,%f", &cam_yaw, &cam_pitch, &cam_zoom) >= 2) {
                cam_yaw = cam_yaw * 3.14159265358979f / 180.0f;
                cam_pitch = cam_pitch * 3.14159265358979f / 180.0f;
            }
        } else if (std::strcmp(argv[i], "--focus") == 0) {
            focus = true;
        } else if (std::strcmp(argv[i], "--interactive") == 0) {
            interactive = true;
        } else if (std::strcmp(argv[i], "--help") == 0 ||
                   std::strcmp(argv[i], "-h") == 0) {
            usage();
            return 0;
        } else {
            std::fprintf(stderr, "unknown option: %s\n", argv[i]);
            usage();
            return 2;
        }
    }

    if (gui_selftest) return run_gui_selftest();

    if (gui_list || !gui_screen.empty()) {
        namespace fs = std::filesystem;
        const fs::path archive_path = fs::path(data) / "DID.DAT";
        if (!fs::exists(archive_path)) {
            std::fprintf(stderr, "no DID.DAT in %s\n", data.c_str());
            return 1;
        }
        ef::DidArchive did(archive_path);
        if (gui_list) {
            static const char* kScreens[] = {
                "mainmenu", "campaign", "camp", "missplan", "summary", "weapons",
                "pickweap", "players", "custom", "system", "arcade", "arcade2",
                "archigh", "arcload", "multmenu", "multi", "net", "gnet", "network",
                "netinfo", "netmiss", "netmsg", "simul", "simul2", "simpick",
                "simload", "campload", "directp", "intro", "loading", "col",
                "rxmiss", "txmiss", "menu", "ef2000"};
            std::size_t count = 0;
            for (const char* name : kScreens) {
                if (auto entry = did.resolve(name, "gui\\", ".gui")) {
                    std::printf("  gui\\%s.gui  entry %u  %u B stored\n", name,
                                entry->index, entry->size);
                    ++count;
                }
            }
            std::printf("%zu screens\n", count);
            return 0;
        }
        if (gui_routines) {
            ef::GuiLoader loader(did);
            std::string error;
            auto screen = loader.load(gui_screen, &error);
            if (!screen) {
                std::fprintf(stderr, "%s\n", error.c_str());
                return 1;
            }
            ef::GuiSystem system;
            system.set_screen(std::move(*screen));
            register_observed_routines(system);
            const auto referenced = system.referenced_routines();
            const auto missing = system.missing_routines();
            std::printf("%s: %zu routine references, %zu implemented, %zu missing\n",
                        gui_screen.c_str(), referenced.size(),
                        referenced.size() - missing.size(), missing.size());
            for (const auto& name : referenced) {
                const bool done = std::find(missing.begin(), missing.end(), name) ==
                                  missing.end();
                std::printf("  %-28s %s\n", name.c_str(), done ? "implemented" : "--");
            }
            return 0;
        }
        if (!gui_render.empty()) {
            ef::GuiRenderOptions options;
            options.language = gui_lang;
            if (!ef::render_gui_png(did, gui_screen, gui_render, options)) {
                return 1;
            }
            std::printf("wrote %s\n", gui_render.c_str());
            return 0;
        }
        return run_gui_viewer(did, gui_screen, gui_lang);
    }

    if (models || !mesh_name.empty()) {
        namespace fs = std::filesystem;
        const fs::path archive_path = fs::path(data) / "DID.DAT";
        if (!fs::exists(archive_path)) {
            std::fprintf(stderr, "no DID.DAT in %s\n", data.c_str());
            return 1;
        }
        try {
            ef::DidArchive did(archive_path);
            std::printf("%s: %s, %zu entries, %zu prefixes, %zu extensions\n",
                        archive_path.string().c_str(), did.variant().c_str(),
                        did.entries().size(), did.prefixes().size(),
                        did.extensions().size());

            if (models) {
                // Cross-reference the shipped clump-name list against the
                // archive so the aircraft -> mesh mapping is visible.
                fs::path clumps = fs::path(data) / "CLUMP_3.TXT";
                if (!fs::exists(clumps)) {
                    clumps = fs::path(data).parent_path() / "CLUMP_3.TXT";
                }
                std::ifstream list(clumps);
                if (!list) {
                    std::printf("(no %s; pass --mesh NAME to load one shape)\n",
                                clumps.string().c_str());
                } else {
                    std::size_t total = 0, found = 0;
                    std::string name;
                    while (std::getline(list, name)) {
                        while (!name.empty() &&
                               (name.back() == '\r' || name.back() == ' ')) {
                            name.pop_back();
                        }
                        if (name.empty()) continue;
                        ++total;
                        if (did.resolve(name, "3\\", ".3")) ++found;
                    }
                    std::printf("%zu of %zu clump names have a 3\\*.3 mesh\n", found,
                                total);
                }
            }

            if (!mesh_name.empty()) {
                auto entry = did.resolve(mesh_name, "3\\", ".3");
                if (!entry) entry = did.resolve(mesh_name, "ss\\", ".ssd");
                if (!entry) {
                    std::fprintf(stderr, "%s: not found in %s\n", mesh_name.c_str(),
                                 archive_path.string().c_str());
                    return 1;
                }
                auto bytes = did.read(*entry);
                if (!bytes) {
                    std::fprintf(stderr, "%s: cannot decode entry %u\n",
                                 mesh_name.c_str(), entry->index);
                    return 1;
                }
                const ef::Shape shape = ef::Shape::parse(*bytes);
                if (lod < 0 || static_cast<std::size_t>(lod) >= shape.lods().size()) {
                    std::fprintf(stderr, "%s: LOD %d out of range (%zu available)\n",
                                 mesh_name.c_str(), lod, shape.lods().size());
                    return 1;
                }
                ef::ShapeParameters params{};
                const ef::Mesh mesh = ef::execute_shape(shape, static_cast<std::size_t>(lod),
                                                        params, 0);
                ef::Vec3f lo{}, hi{};
                const bool have_bounds = mesh.bounds(lo, hi);
                std::printf("%s: entry %u, %zu bytes, class 0x%04X, %zu LODs, "
                            "%zu triangles",
                            mesh_name.c_str(), entry->index, bytes->size(),
                            shape.class_id(), shape.lods().size(),
                            mesh.triangle_count());
                if (have_bounds) {
                    std::printf(" [%.0f..%.0f, %.0f..%.0f, %.0f..%.0f]", lo.x, hi.x,
                                lo.y, hi.y, lo.z, hi.z);
                }
                std::printf("\n");

                if (!export_mesh.empty()) {
                    std::ofstream f(export_mesh);
                    f << ef::mesh_to_obj(mesh, mesh_name);
                    std::printf("wrote %s\n", export_mesh.c_str());
                }
                if (!mesh_preview.empty()) {
                    if (!ef::render_mesh_png(mesh, mesh_preview)) {
                        std::fprintf(stderr, "preview failed: %s\n", SDL_GetError());
                        return 1;
                    }
                    std::printf("wrote %s\n", mesh_preview.c_str());
                }
                if (!render_path.empty()) {
                    ef::TextureBank textures(did, palette_name);
                    std::string chosen;
                    ef::TextureSlots slots =
                        build_slots(textures, shape, texture_override, chosen);
                    ef::OrbitCamera camera;
                    camera.yaw = cam_yaw;
                    camera.pitch = cam_pitch;
                    camera.zoom = cam_zoom;
                    camera.decals = false;
                    if (!ef::render_model_png(mesh, camera, &slots, &textures.palette(),
                                              render_path)) {
                        std::fprintf(stderr, "render failed\n");
                        return 1;
                    }
                    std::printf("wrote %s (palette %s)\n", render_path.c_str(),
                                textures.palette_name().c_str());
                }
                if (interactive) {
                    auto names = shape_names(std::filesystem::path(data), did, clumps_path);
                    int start_index = 0;
                    for (std::size_t k = 0; k < names.size(); ++k) {
                        if (names[k] == mesh_name) start_index = static_cast<int>(k);
                    }
                    ef::OrbitCamera camera;
                    camera.yaw = cam_yaw;
                    camera.pitch = cam_pitch;
                    camera.zoom = cam_zoom;
                    ModelSet models(did, std::move(names));
                    ef::TextureBank textures(did, palette_name);
                    if (textures.ready()) {
                        std::printf("palette: colours\\%s\n", textures.palette_name().c_str());
                    } else {
                        std::fprintf(stderr, "warning: no palette found; textures disabled\n");
                    }
                    return run_model_viewer(models, textures, start_index, false, camera,
                                            texture_override);
                }
            }
        } catch (const std::exception& exc) {
            std::fprintf(stderr, "%s\n", exc.what());
            return 1;
        }
        return 0;
    }

    ef::Dataset ds = ef::load_dataset(data);
    if (ds.missions.empty() && ds.bnas.empty()) {
        std::fprintf(stderr, "no data loaded from %s\n", data.c_str());
        return 1;
    }

    if (stats) ef::print_stats(ds);
    if (list) {
        for (std::size_t i = 0; i < ds.missions.size(); ++i) {
            const auto& m = ds.missions[i];
            std::printf("%4zu  %-7s %-8s %-18s %s\n", i, m.type.c_str(),
                        m.side.c_str(), m.role.c_str(), m.status.c_str());
        }
    }
    if (interactive) {
        return run_interactive(ds, mission, focus,{sam_coverage, ewr_coverage, coverage_scale});
    }

    if (!export_path.empty()) {
        ef::RenderOptions opt;
        opt.selected = mission;
        opt.focus = focus;
        opt.sam_coverage = sam_coverage;
        opt.ewr_coverage = ewr_coverage;
        opt.coverage_scale = coverage_scale;
        opt.bounds = map_bounds;
        SDL_Surface* surf = ef::render_map(ds, opt);
        if (!surf) {
            std::fprintf(stderr, "render failed: %s\n", SDL_GetError());
            return 1;
        }
        bool ok = ef::save_png(surf, export_path);
        SDL_FreeSurface(surf);
        if (!ok) {
            std::fprintf(stderr, "save failed: %s\n", SDL_GetError());
            return 1;
        }
        std::printf("wrote %s\n", export_path.c_str());
        ef::print_mission(ds, mission);
        return 0;
    }

    if (!stats && !list) {
        ef::print_stats(ds);
        ef::print_mission(ds, mission);
    }
    return 0;
}
