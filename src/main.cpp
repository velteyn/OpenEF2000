// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 velteyn

#include "campaigngen.h"
#include "campaign.h"
#include "conflict.h"
#include "mission.h"
#include "did.h"
#include "guirender.h"
#include "meshview.h"
#include "modelview.h"
#include "parser.h"
#include "render.h"
#include "shape3d.h"
#include "stats.h"
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
        "  --archive FILE      read GUI/data from this DiD archive (default DID1ST when present)\n"
        "  --dump-palette NAME print colours\\NAME.col as hex RGB\n"
        "  --gui-render FILE   render that screen to PNG (headless)\n"
        "  --gui-list          list the GUI screens in DID.DAT\n"
        "  --gui-routines      with --gui SCREEN, list the routines it needs\n"
        "  --gui-dump          with --gui SCREEN, print windows, icons, keys and verbs\n"
        "  --camp-zoom N       headless map zoom for --gui-render\n"
        "  --camp-sam/--camp-ewr  headless coverage overlays for --gui-render\n"
        "  --camp-side N       side bitmask (1 Nato, 2 Russian, 4 Neutral)\n"
        "  --camp-layer N      layer bitmask (1 airdef, 2 airsup, 4 navy,\n"
        "                      8 comms, 16 supply, 32 political)\n"
        "  --camp-pan X,Y      pan the map by world units\n"
        "  --camp-mode N       map style 0..3 (0/3 relief, 1/2 political)\n"
        "  --camp-noflags      hide unit markers (the `f` toggle)\n"
        "  --camp-fuel         planner fuel ring (assumed radius)\n"
        "  --camp-mission N    selected mission for headless GUI inspection\n"
        "  --conflict FILE     analyse a CONFLICT.CG / CAMPAIGN.CG\n"
        "  --conflict-roundtrip OUT  save and reload the scenario to verify the parser\n"
        "  --campaign-generate FILE  randomize target values from a scenario\n"
        "  --campaign-out DIR  write CAMPAIGN.CG into DIR\n"
        "  --campaign-seed N   generator seed (default 0x31415926)\n"
        "  --campaign-difficulty N  override the scenario difficulty\n"
        "  --gui-selftest      run deterministic GUI runtime checks\n"
        "  --boot              show the title splash, then the main menu\n"
        "  --gui-lang LANG     en|fr|de (default en)\n"
        "  --gui-window NAME   open this window before rendering\n");
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


// Interactive GUI driver: hit-testing, hover/selection events and screen
// switching. The data is the original .gui text; only the drawing style is
// approximated.
int run_gui_viewer(const ef::DidArchive& did, const std::string& start_screen,
                   const std::string& lang, const std::filesystem::path& data_dir,
                   bool boot) {
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
    // DID1ST has the later screens but no palettes; they live in DID.DAT.
    if (palette[15] == 0 && palette[1] == 0) {
        const std::filesystem::path companion = data_dir / "DID.DAT";
        if (std::filesystem::exists(companion)) {
            try {
                ef::DidArchive other(companion);
                if (auto gui = ef::load_gui_palette(other, "gui")) palette = *gui;
                else if (auto plain = ef::load_gui_palette(other, "2000")) palette = *plain;
            } catch (const std::exception&) {
                // Keep the black palette; geometry still shows.
            }
        }
    }

    ef::GuiSystem system;
    system.set_screen(std::move(*screen));

    std::optional<ef::Dataset> dataset;
    try {
        dataset = ef::load_dataset(data_dir.string());
    } catch (const std::exception&) {
        // The shell still works without mission data; the map stays blank.
    }

    ef::register_observed_routines(
        system, dataset ? &*dataset : nullptr, data_dir,
        [&dataset, &data_dir] {
            try {
                dataset = ef::load_dataset(data_dir.string());
            } catch (const std::exception&) {
                // Keep the previous data if the reload fails.
            }
        });
    system.run_startup();

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

    // The boot script holds a splash for 200 ticks before the main menu
    // (`STAT didlog 200` in supergui\gui.txt). `didlog` is not in the archive;
    // `load0` is the shipped title card.
    if (boot) {
        if (auto splash = ef::load_backdrop(did, "load0")) {
            SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(
                0, 640, 400, 32, SDL_PIXELFORMAT_RGBA32);
            if (surface != nullptr) {
                ef::draw_image(surface, *splash);
                SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surface);
                if (tex != nullptr) {
                    SDL_RenderClear(renderer);
                    SDL_RenderCopy(renderer, tex, nullptr, nullptr);
                    SDL_RenderPresent(renderer);
                    SDL_DestroyTexture(tex);
                }
                SDL_FreeSurface(surface);
            }
            // Hold the splash for the declared 200 ticks (~2 s). Poll inside
            // the wait: the previous form exited as soon as the queue was
            // empty, so the card never showed.
            const Uint32 until = SDL_GetTicks() + 2000;
            while (SDL_GetTicks() < until) {
                SDL_Event event;
                while (SDL_PollEvent(&event)) {
                    if (event.type == SDL_QUIT) return 0;
                }
                SDL_Delay(16);
            }
        }
    }

    ef::GuiRenderOptions options;
    options.language = lang;

    std::string hover_window, hover_icon;
    // A `release` action belongs to the widget the press started on: dragging
    // off the widget and releasing elsewhere must not activate it.
    std::string press_window, press_icon;
    // Map drag state: a press inside a map window either pans or, if the
    // pointer barely moved, counts as a click.
    std::string map_press_window;
    int map_last_x = 0, map_last_y = 0, map_moved = 0;
    // Zoom mode rubber band (the original's EDIT/ZOOM `a` toggle).
    bool band_active = false;
    int band_x0 = 0, band_y0 = 0, band_x1 = 0, band_y1 = 0;
    std::string scroll_drag_window;
    int scroll_drag_grab = 0;
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
                    const bool handled =
                        system.key_press(shift ? "shift+" + name : name);
                    // The map bar binds f/s/e/m/u/a in the shipped data, but
                    // the loaded variant does not always keep those keys. Fall
                    // back to the same map state when no widget handled them.
                    const bool on_map =
                        system.find_window("mp_map") != nullptr ||
                        system.find_window("map") != nullptr;
                    if (!handled && !shift && on_map) {
                        if (name == "a") {
                            system.set_camp_zoom_mode(!system.camp_zoom_mode());
                            dirty = true;
                        } else if (name == "f") {
                            system.set_camp_flags(!system.camp_flags());
                            dirty = true;
                        } else if (name == "s") {
                            system.set_camp_sam(!system.camp_sam());
                            dirty = true;
                        } else if (name == "e") {
                            system.set_camp_ewr(!system.camp_ewr());
                            dirty = true;
                        } else if (name == "m") {
                            system.set_camp_map_mode((system.camp_map_mode() + 1) % 4);
                            dirty = true;
                        } else if (name == "u") {
                            system.set_mp_icons(!system.mp_icons());
                            dirty = true;
                        }
                    }
                    if (handled) dirty = true;
                }
                continue;
            }
            if (ev.type == SDL_MOUSEWHEEL) {
                int mx = 0, my = 0;
                SDL_GetMouseState(&mx, &my);
                float lx = 0, ly = 0;
                SDL_RenderWindowToLogical(renderer, mx, my, &lx, &ly);
                if (auto* w = system.window_at(static_cast<int>(lx), static_cast<int>(ly))) {
                    // The wheel over the map's scrollbar scrolls; over the
                    // map itself it zooms around the cursor.
                    if (ef::gui_is_map_window(w->name)) {
                        SDL_Rect track{}, thumb{};
                        int max_scroll = 0;
                        if (ef::gui_map_scrollbar(*w, &track, &thumb, &max_scroll) &&
                            static_cast<int>(lx) >= track.x &&
                            static_cast<int>(lx) < track.x + track.w &&
                            static_cast<int>(ly) >= track.y &&
                            static_cast<int>(ly) < track.y + track.h) {
                            if (system.scroll_window(w->name, 0, -ev.wheel.y * 16)) {
                                dirty = true;
                            }
                            continue;
                        }
                    }
                    if (dataset && ef::gui_is_map_window(w->name)) {
                        if (ef::gui_map_zoom_at(system, *dataset, *w,
                                                static_cast<int>(lx),
                                                static_cast<int>(ly), ev.wheel.y)) {
                            dirty = true;
                        }
                    } else if (system.scroll_window(w->name, 0, -ev.wheel.y * 3)) {
                        // `xscroll`/`yscroll` declare the axis; three lines a notch.
                        dirty = true;
                    }
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
                if (ev.type == SDL_MOUSEMOTION && !scroll_drag_window.empty()) {
                    if (auto* sw = system.find_window(scroll_drag_window)) {
                        SDL_Rect track{}, thumb{};
                        int max_scroll = 0;
                        if (ef::gui_map_scrollbar(*sw, &track, &thumb, &max_scroll)) {
                            const int usable = track.h - 24;
                            const int denom = std::max(1, usable - thumb.h);
                            const int pos = std::clamp(
                                py - track.y - 12 - scroll_drag_grab, 0, denom);
                            system.scroll_window_to(scroll_drag_window, 0,
                                                    pos * max_scroll / denom);
                            dirty = true;
                        }
                    }
                }
                if (ev.type == SDL_MOUSEMOTION && !map_press_window.empty() &&
                    (ev.motion.state & SDL_BUTTON_LMASK) != 0 &&
                    dataset.has_value()) {
                    if (band_active) {
                        band_x1 = px;
                        band_y1 = py;
                        map_moved += std::abs(px - map_last_x) + std::abs(py - map_last_y);
                        map_last_x = px;
                        map_last_y = py;
                        dirty = true;
                    } else if (auto* mw = system.find_window(map_press_window)) {
                        const int dx = px - map_last_x, dy = py - map_last_y;
                        if (dx != 0 || dy != 0) {
                            if (ef::gui_map_pan(system, *dataset, *mw, dx, dy)) {
                                map_moved += std::abs(dx) + std::abs(dy);
                                dirty = true;
                            }
                            map_last_x = px;
                            map_last_y = py;
                        }
                    }
                }
                if (new_window != hover_window || icon != hover_icon) {
                    if (!hover_icon.empty()) system.fire(hover_window, hover_icon, "leaving");
                    hover_window = new_window;
                    hover_icon = icon;
                    if (!hover_icon.empty()) system.fire(hover_window, hover_icon, "entering");
                    dirty = true;
                }
                if (ev.type == SDL_MOUSEBUTTONDOWN &&
                    ev.button.button == SDL_BUTTON_RIGHT && win != nullptr &&
                    dataset.has_value() && ef::gui_is_map_window(win->name)) {
                    if (ef::gui_map_zoom_at(system, *dataset, *win, px, py, -1)) {
                        dirty = true;
                    }
                }
                if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT &&
                    win != nullptr && ef::gui_is_map_window(win->name)) {
                    SDL_Rect track{}, thumb{};
                    int max_scroll = 0;
                    if (ef::gui_map_scrollbar(*win, &track, &thumb, &max_scroll) &&
                        px >= track.x && px < track.x + track.w &&
                        py >= track.y && py < track.y + track.h) {
                        if (py < track.y + 12) {
                            system.scroll_window(win->name, 0, -16);
                        } else if (py >= track.y + track.h - 12) {
                            system.scroll_window(win->name, 0, 16);
                        } else if (py >= thumb.y && py < thumb.y + thumb.h) {
                            scroll_drag_window = win->name;
                            scroll_drag_grab = py - thumb.y;
                        } else {
                            const int usable = track.h - 24;
                            const int denom = std::max(1, usable - thumb.h);
                            const int pos = std::clamp(
                                py - track.y - 12 - thumb.h / 2, 0, denom);
                            system.scroll_window_to(win->name, 0,
                                                    pos * max_scroll / denom);
                        }
                        dirty = true;
                        continue;
                    }
                }
                if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT) {
                    SDL_Rect map_client{};
                    bool in_map_client = false;
                    if (win != nullptr && icon.empty() &&
                        ef::gui_is_map_window(win->name)) {
                        map_client = ef::GuiSystem::client_rect(*win);
                        in_map_client =
                            px >= map_client.x && py >= map_client.y &&
                            px < map_client.x + map_client.w &&
                            py < map_client.y + map_client.h;
                    }
                    if (in_map_client && dataset.has_value()) {
                        // A press starts either a pan or, if the pointer does
                        // not move, a click. Resolve it on release so dragging
                        // never edits the plan. In zoom mode the press starts
                        // a rubber-band zoom instead.
                        map_press_window = win->name;
                        map_last_x = px;
                        map_last_y = py;
                        map_moved = 0;
                        if (system.camp_zoom_mode()) {
                            band_active = true;
                            band_x0 = band_x1 = px;
                            band_y0 = band_y1 = py;
                        }
                        dirty = true;
                    } else if (!hover_icon.empty()) {
                        // Clicking a text field focuses it; clicking elsewhere blurs.
                        system.focus_field(hover_window, hover_icon);
                        press_window = hover_window;
                        press_icon = hover_icon;
                        system.fire(hover_window, hover_icon, "click");
                    } else {
                        system.focus_field(std::string(), std::string());
                    }
                    dirty = true;
                }
                if (ev.type == SDL_MOUSEBUTTONUP && ev.button.button == SDL_BUTTON_LEFT) {
                    if (!scroll_drag_window.empty()) {
                        scroll_drag_window.clear();
                        dirty = true;
                        continue;
                    }
                    if (band_active) {
                        if (auto* mw = system.find_window(map_press_window)) {
                            if (dataset.has_value() && map_moved < 4) {
                                ef::gui_map_zoom_at(system, *dataset, *mw, px, py, 1);
                            } else if (dataset.has_value()) {
                                ef::gui_map_zoom_rect(system, *dataset, *mw,
                                                      band_x0, band_y0, px, py);
                            }
                        }
                        band_active = false;
                        map_press_window.clear();
                        dirty = true;
                    } else if (!map_press_window.empty()) {
                        // A press that did not pan is a click: the campaign map
                        // binds its handler to release.
                        if (dataset.has_value() && map_moved < 4 &&
                            hover_window == map_press_window) {
                            if (auto* mw = system.find_window(map_press_window)) {
                                int wx = 0, wy = 0, radius = 0;
                                if (ef::gui_map_world_at(system, *dataset, *mw, px, py,
                                                         &wx, &wy, &radius)) {
                                    system.set_map_click(wx, wy, radius);
                                    // The planner binds `pressed`, the campaign
                                    // map binds `release`; the matching one runs.
                                    system.fire_window(map_press_window, "pressed");
                                    system.fire_window(map_press_window, "release");
                                }
                            }
                        }
                        map_press_window.clear();
                        dirty = true;
                    } else if (!press_icon.empty() && press_window == hover_window &&
                               press_icon == hover_icon) {
                        // Full click: the release counts only over the widget the
                        // press started on.
                        system.fire(hover_window, hover_icon, "release");
                        dirty = true;
                    }
                    press_window.clear();
                    press_icon.clear();
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

        ef::fill_credits(system, did);
        ef::fill_hiscores(system, data_dir);
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
                options.dataset = dataset ? &*dataset : nullptr;
                options.band = band_active;
                options.band_x0 = band_x0;
                options.band_y0 = band_y0;
                options.band_x1 = band_x1;
                options.band_y1 = band_y1;
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
startup TestStartup

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

icon radioA
xpos 20
ypos 20
xsize 10
ysize 10
esg 5

icon radioB
xpos 35
ypos 20
xsize 10
ysize 10
esg 5

icon ctx
xpos 50
ypos 20
xsize 10
ysize 10
key z
call click
routine Capture

icon exit
xpos 65
ypos 35
xsize 10
ysize 10
call click
routine gpwExitSimulator

icon skeyed
xpos 65
ypos 20
xsize 10
ysize 10
skey y
call click
routine ScreenKey

window extra
xpos 200
ypos 0
xsize 50
ysize 50

window missions
xpos 0
ypos 0
xsize 100
ysize 50
open

icon first
xpos 0
ypos 0
xsize 50
ysize 10
esg 1
call click
routine gpwMissionSelected

icon 2
xpos 0
ypos 10
xsize 50
ysize 10
esg 1
call click
routine gpwMissionSelected

window params
xpos 0
ypos 0
xsize 100
ysize 80

icon ttwilight
xpos 0
ypos 0
xsize 10
ysize 10
esg 2
call click
select click

icon tday
xpos 20
ypos 0
xsize 10
ysize 10
esg 2
call click
select click

icon wpoor
xpos 0
ypos 20
xsize 10
ysize 10
esg 3
call click
select click

icon wfair
xpos 20
ypos 20
xsize 10
ysize 10
esg 3
call click
select click

icon rookie
xpos 0
ypos 40
xsize 10
ysize 10
esg 12
call click
select click

icon pro
xpos 20
ypos 40
xsize 10
ysize 10
esg 12
call click
select click

icon defweapyes
xpos 0
ypos 60
xsize 10
ysize 10
esg 10
call click
select click

icon revweapyes
xpos 20
ypos 60
xsize 10
ysize 10
esg 11
call click
select click

icon accept
xpos 60
ypos 60
xsize 10
ysize 10
call release
routine gpwMissionAccepted

window planner
xpos 0
ypos 0
xsize 100
ysize 50
open

icon m2_0
xpos 0
ypos 0
xsize 10
ysize 10
call click
routine mpMission2Click

icon faster
xpos 20
ypos 0
xsize 10
ysize 10
call click
routine mpIncSpeed

icon fly
xpos 40
ypos 0
xsize 10
ysize 10
call click
routine mpFlyMission

window weapons
xpos 0
ypos 0
xsize 100
ysize 50
open

icon front
xpos 0
ypos 0
xsize 10
ysize 10
call click
routine gpwPlaneFrontOn

icon clearall
xpos 20
ypos 0
xsize 10
ysize 10
call click
routine gpwSetClearAllMissiles

icon acceptw
xpos 40
ypos 0
xsize 10
ysize 10
call click
routine gpwWeaponsAccepted
)";

    int failures = 0;
    auto check = [&](const char* what, bool ok) {
        std::printf("  %-46s %s\n", what, ok ? "ok" : "FAIL");
        if (!ok) ++failures;
    };

    ef::GuiSystem system;
    system.set_screen(ef::GuiLoader::parse("selftest", dsl));
    ef::register_observed_routines(system);

    check("window marked `open` is visible", system.find_window("test") != nullptr &&
                                                system.find_window("test")->visible);
    check("window without `open` starts hidden",
          system.find_window("extra") != nullptr && !system.find_window("extra")->visible);


    // Screens declare a `startup` routine; the driver supplies its behaviour.
    system.register_routine("TestStartup", [](ef::GuiSystem& gui,
                                               const std::string&,
                                               const std::string&) {
        gui.open_window("extra");
    });
    std::string ctx_window, ctx_icon;
    system.register_routine("Capture", [&](ef::GuiSystem&, const std::string& window,
                                           const std::string& icon) {
        ctx_window = window;
        ctx_icon = icon;
    });
    check("startup routine runs and can raise a window",
          system.run_startup() && system.find_window("extra")->visible);
    system.close_window("extra");

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
    system.scroll_window_to("test", 0, 40);
    check("scroll_window_to sets a clamped position",
          system.find_window("test")->scroll_y_pos == 40);

    system.fire("test", "sub", "click");
    check("submenu opens the named window in the same file",
          system.find_window("extra") != nullptr && system.find_window("extra")->visible);

    check("routine receives the window and icon that fired it",
          system.key_press("z") && ctx_window == "test" && ctx_icon == "ctx");

    bool screen_key_fired = false;
    system.register_routine("ScreenKey", [&](ef::GuiSystem&, const std::string&,
                                             const std::string&) {
        screen_key_fired = true;
    });
    check("skey is a screen-local binding, not shift+key",
          system.key_press("y") && screen_key_fired);

    system.select("test", "radioA", true);
    system.select("test", "radioB", true);
    check("same-esg icons behave as a radio group",
          !system.is_selected("test", "radioA") && system.is_selected("test", "radioB"));

    system.fire("missions", "2", "click");
    check("mission list selects an entry and records it",
          system.is_selected("missions", "2") && system.sim_mission() == 1);

    system.fire("params", "tday", "click");
    system.fire("params", "wfair", "click");
    system.fire("params", "pro", "click");
    system.fire("params", "defweapyes", "click");
    system.fire("params", "revweapyes", "click");
    system.fire("params", "accept", "release");
    check("parameters dialog records time, weather and difficulty",
          system.mission_accepted() && system.sim_time() == 1 &&
              system.sim_weather() == 1 && system.sim_difficulty() == 1);
    check("weapon options are recorded",
          system.sim_default_weapons() && system.sim_review_weapons());

    system.fire("planner", "m2_0", "click");
    check("planner records the mission slot", system.mp_slot() == 1);
    system.fire("planner", "faster", "click");
    check("planner speed step applies", system.mp_speed() == 550);
    system.fire("planner", "fly", "click");
    check("planner records the fly decision", system.mp_fly());

    system.fire("weapons", "front", "click");
    system.fire("weapons", "clearall", "click");
    system.fire("weapons", "acceptw", "click");
    check("weapons view preset, clear-all and accept are recorded",
          system.weap_view() == 1 && system.weap_cleared_all() &&
              system.weap_accepted());

    system.fire("test", "exit", "click");
    const auto screens = system.take_screen_requests();
    check("gpwExitSimulator queues the main menu",
          std::find(screens.begin(), screens.end(), "mainmenu") != screens.end());

    // Strategic-map clicks against a synthetic dataset. The window binds the
    // handler at window level (`call pressed` + `routine`), which is how the
    // shipped map windows do it.
    {
        ef::Dataset map_ds;
        ef::Mission m;
        m.side = "NATO";
        m.pos = ef::Vec3{0, 0, 0};
        ef::Target t;
        ef::TargetArea area;
        area.type = "POL_TYPE";
        area.name = "\"Depot\"";
        area.pos = ef::Vec3{100, 100, 0};
        t.areas.push_back(area);
        m.targets.push_back(t);
        map_ds.missions.push_back(m);

        ef::Mission far;
        far.side = "RUSSIAN";
        far.pos = ef::Vec3{300, 300, 0};
        map_ds.missions.push_back(far);

        ef::Base base;
        base.side = "NATO";
        base.name = "\"Home\"";
        base.pos = ef::Vec3{200, 200, 0};
        map_ds.bases.push_back(base);

        const char* map_dsl = R"(
window mp_map
xpos 0
ypos 0
xsize 100
ysize 100
open
call pressed
routine mpMapSelect

icon clear
xpos 0
ypos 0
xsize 10
ysize 10
call click
routine mpClearWPRoute

window map
xpos 0
ypos 0
xsize 100
ysize 100
open
call release
routine CampMoveWP

window mapbar
xpos 0
ypos 0
xsize 100
ysize 10
open

icon m_type
xpos 0
ypos 0
xsize 10
ysize 10
call click
routine mpCycleMap

icon flags
xpos 20
ypos 0
xsize 10
ysize 10
call click
routine mpToggleFlags

icon mode
xpos 40
ypos 0
xsize 10
ysize 10
call click
routine gcChangeMode

icon fuel
xpos 60
ypos 0
xsize 10
ysize 10
call click
routine mpToggleIcons
)";
        ef::GuiSystem map;
        map.set_screen(ef::GuiLoader::parse("maptest", map_dsl));
        ef::register_observed_routines(map, &map_ds);

        map.set_map_click(100, 100, 5);
        map.fire_window("mp_map", "pressed");
        check("map click selects the target area",
              map.mp_target() == "Depot" && map.mp_target_index() == 0 &&
                  map.mp_area_index() == 0);

        map.set_map_click(400, 400, 5);
        map.fire_window("mp_map", "pressed");
        check("map click on empty space appends a waypoint",
              map.mp_waypoints().size() == 1 &&
                  map.mp_waypoints()[0].first == 400);

        map.fire("mp_map", "clear", "click");
        check("clear route removes appended waypoints",
              map.mp_waypoints().empty());

        map.set_map_click(198, 200, 5);
        map.fire_window("mp_map", "pressed");
        check("map click near a base records the takeoff base",
              map.mp_base() == "Home");

        map.set_map_click(300, 300, 5);
        map.fire_window("map", "release");
        check("campaign map click selects the nearest mission",
              map.camp_mission() == 1);

        map.fire("mapbar", "m_type", "click");
        check("map style cycles", map.camp_map_mode() == 1);
        const bool flags_before = map.camp_flags();
        map.fire("mapbar", "flags", "click");
        check("flags toggle", map.camp_flags() != flags_before);
        map.fire("mapbar", "mode", "click");
        check("edit/zoom mode toggles", map.camp_zoom_mode());
        const bool fuel_before = map.mp_icons();
        map.fire("mapbar", "fuel", "click");
        check("fuel toggle", map.mp_icons() != fuel_before);
    }

    // Force statistics: counts by side and class, plus the planner's base
    // EFA readout. Built from a tiny synthetic dataset so the check is
    // independent of the shipped mission files.
    {
        ef::Dataset sd;
        auto mission = [&](const char* side) {
            ef::Mission m;
            m.side = side;
            return m;
        };

        ef::Mission nato = mission("NATO");
        ef::Flight f16;
        f16.count = 2; f16.aircraft = "F16";
        nato.flights.push_back(f16);
        ef::Group patriot;
        patriot.count = 3; patriot.type = "PATRIOT_1"; patriot.pos = ef::Vec3{1, 1, 0};
        nato.groups.push_back(patriot);
        ef::Target t;
        ef::TargetArea airfield;
        airfield.type = "AIRFIELD"; airfield.name = "\"Field\"";
        airfield.pos = ef::Vec3{2, 2, 0};
        t.areas.push_back(airfield);
        nato.targets.push_back(t);

        ef::Mission rus = mission("RUSSIAN");
        ef::Flight su;
        su.count = 1; su.aircraft = "SU27";
        rus.flights.push_back(su);
        ef::Group ship;
        ship.count = 2; ship.type = "TYPE42_CLASS"; ship.pos = ef::Vec3{3, 3, 0};
        rus.groups.push_back(ship);
        ef::Target rt;
        ef::TargetArea port;
        port.type = "PORT"; port.name = "\"Port\"";
        port.pos = ef::Vec3{4, 4, 0};
        rt.areas.push_back(port);
        rus.targets.push_back(rt);

        ef::Mission swe = mission("SWEDISH");
        ef::Flight jas;
        jas.count = 1; jas.aircraft = "JAS39";
        swe.flights.push_back(jas);

        ef::Mission plane = mission("NATO");
        plane.type = "PLANE";
        plane.values = {0, 42, 1};
        ef::Flight efa;
        efa.count = 2; efa.aircraft = "EFA";
        plane.flights.push_back(efa);
        ef::Route route;
        ef::Waypoint standby;
        standby.action = "STANDBY";
        route.waypoints.push_back(standby);
        plane.routes.push_back(route);

        sd.missions = {nato, rus, swe, plane};
        ef::Base base;
        base.side = "NATO"; base.name = "\"Home\"";
        base.pos = ef::Vec3{5, 5, 0};
        sd.bases.push_back(base);

        const ef::ForceStats st = ef::compute_force_stats(sd);
        check("statistics count aircraft by side",
              st.sides[0].aircraft == 4 && st.sides[1].aircraft == 1 &&
                  st.sides[2].aircraft == 1);
        check("statistics count ground, ships and structures",
              st.sides[0].ground == 3 && st.sides[2].ships == 2 &&
                  st.sides[0].structures == 1 && st.sides[2].structures == 1);
        check("statistics classify air and ground classes",
              ef::category_value(st.aircraft, "Fighters", 0) == 4 &&
                  ef::category_value(st.ground, "AntiAircraft", 0) == 3 &&
                  ef::category_value(st.ground, "BattleShips", 2) == 2);
        check("planner base readout counts EFAs",
              ef::mission_base_index(plane) == 0 &&
                  ef::base_efa_count(sd, 0, false) == 2 &&
                  ef::base_efa_count(sd, 0, true) == 2);
    }

    // Campaign generator foundations: the custom RNG and the target-value
    // pass must be reproducible from the seed.
    {
        ef::CampaignRng a(0x31415926u), b(0x31415926u);
        bool same = true;
        for (int i = 0; i < 8; ++i) same = same && (a.next() == b.next());
        check("campaign RNG is deterministic for a seed", same);

        ef::Scenario one;
        for (int i = 0; i < 6; ++i) {
            ef::ScenarioArea area;
            area.name = "A" + std::to_string(i);
            area.side = "NATO";
            area.airfield1 = 30;
            area.airfield2 = 30;
            one.areas.push_back(area);
        }
        ef::Scenario two = one;
        ef::CampaignRng r1(1234), r2(1234);
        ef::randomize_target_values(one, r1);
        ef::randomize_target_values(two, r2);
        bool equal = one.areas.size() == two.areas.size();
        for (std::size_t i = 0; equal && i < one.areas.size(); ++i) {
            equal = one.areas[i].value == two.areas[i].value;
        }
        check("target-value pass is reproducible from the seed", equal);
    }

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
    bool campaign_selftest = false;
    std::string campaign_accept_dir;
    std::string conflict_path;
    std::string conflict_out;
    std::string campaign_src;
    std::string campaign_state;
    std::string campaign_out;
    std::string mission_in;
    std::string mission_out;
    int campaign_difficulty = -1;
    int campaign_offset = 0;
    unsigned campaign_seed = 0x31415926u;
    std::string archive_override;
    std::string dump_palette;
    std::string gui_render;
    std::string gui_lang = "en";
    bool gui_list = false;
    bool gui_routines = false;
    bool gui_dump = false;
    int camp_zoom = -1;
    int camp_sides = -1, camp_layers = -1;
    int camp_pan_x = 0, camp_pan_y = 0;
    int camp_mode = -1, camp_flags = -1, camp_fuel = -1;
    int camp_mission = -1;
    bool camp_sam = false, camp_ewr = false;
    bool gui_selftest = false;
    bool boot = false;
    bool sam_coverage = false, ewr_coverage = false;
    float coverage_scale = 1.0f;
    std::optional<std::array<int, 4>> map_bounds;
    std::string gui_window;
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
        } else if (std::strcmp(argv[i], "--dump-palette") == 0 && i + 1 < argc) {
            dump_palette = argv[++i];
        } else if (std::strcmp(argv[i], "--archive") == 0 && i + 1 < argc) {
            archive_override = argv[++i];
        } else if (std::strcmp(argv[i], "--gui") == 0 && i + 1 < argc) {
            gui_screen = argv[++i];
        } else if (std::strcmp(argv[i], "--gui-render") == 0 && i + 1 < argc) {
            gui_render = argv[++i];
        } else if (std::strcmp(argv[i], "--gui-lang") == 0 && i + 1 < argc) {
            gui_lang = argv[++i];
        } else if (std::strcmp(argv[i], "--gui-window") == 0 && i + 1 < argc) {
            gui_window = argv[++i];
        } else if (std::strcmp(argv[i], "--gui-list") == 0) {
            gui_list = true;
        } else if (std::strcmp(argv[i], "--gui-routines") == 0) {
            gui_routines = true;
        } else if (std::strcmp(argv[i], "--gui-dump") == 0) {
            gui_dump = true;
        } else if (std::strcmp(argv[i], "--camp-zoom") == 0 && i + 1 < argc) {
            camp_zoom = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--camp-sam") == 0) {
            camp_sam = true;
        } else if (std::strcmp(argv[i], "--camp-ewr") == 0) {
            camp_ewr = true;
        } else if (std::strcmp(argv[i], "--camp-side") == 0 && i + 1 < argc) {
            camp_sides = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--camp-layer") == 0 && i + 1 < argc) {
            camp_layers = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--camp-pan") == 0 && i + 1 < argc) {
            std::sscanf(argv[++i], "%d,%d", &camp_pan_x, &camp_pan_y);
        } else if (std::strcmp(argv[i], "--camp-mode") == 0 && i + 1 < argc) {
            camp_mode = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--camp-noflags") == 0) {
            camp_flags = 0;
        } else if (std::strcmp(argv[i], "--camp-fuel") == 0) {
            camp_fuel = 1;
        } else if (std::strcmp(argv[i], "--camp-mission") == 0 && i + 1 < argc) {
            camp_mission = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--gui-selftest") == 0) {
            gui_selftest = true;
        } else if (std::strcmp(argv[i], "--boot") == 0) {
            boot = true;
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
        } else if (std::strcmp(argv[i], "--conflict") == 0 && i + 1 < argc) {
            conflict_path = argv[++i];
        } else if (std::strcmp(argv[i], "--conflict-roundtrip") == 0 && i + 1 < argc) {
            conflict_out = argv[++i];
        } else if (std::strcmp(argv[i], "--campaign-generate") == 0 && i + 1 < argc) {
            campaign_src = argv[++i];
        } else if (std::strcmp(argv[i], "--mission-roundtrip") == 0 && i + 1 < argc) {
            mission_in = argv[++i];
        } else if (std::strcmp(argv[i], "--mission-write") == 0 && i + 1 < argc) {
            mission_out = argv[++i];
        } else if (std::strcmp(argv[i], "--campaign-accept-dir") == 0 &&
                   i + 1 < argc) {
            campaign_accept_dir = argv[++i];
        } else if (std::strcmp(argv[i], "--campaign-selftest") == 0) {
            campaign_selftest = true;
        } else if (std::strcmp(argv[i], "--campaign-state") == 0 && i + 1 < argc) {
            campaign_state = argv[++i];
        } else if (std::strcmp(argv[i], "--campaign-out") == 0 && i + 1 < argc) {
            campaign_out = argv[++i];
        } else if (std::strcmp(argv[i], "--campaign-offset") == 0 && i + 1 < argc) {
            campaign_offset = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--campaign-seed") == 0 && i + 1 < argc) {
            campaign_seed = static_cast<unsigned>(std::strtoul(argv[++i], nullptr, 0));
        } else if (std::strcmp(argv[i], "--campaign-difficulty") == 0 && i + 1 < argc) {
            campaign_difficulty = std::atoi(argv[++i]);
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

    if (!mission_in.empty()) {
        ef::MissionDocument doc;
        std::string error;
        if (!ef::MissionDocument::load(mission_in, &doc, &error)) {
            std::fprintf(stderr, "%s\\n", error.c_str());
            return 1;
        }
        const auto counts = ef::count_missions(doc);
        std::printf("mission file %s\\n", mission_in.c_str());
        std::printf("  lines %zu  missions %d (gbased %d, plane %d, ship %d)\\n",
                    doc.lines.size(), counts.missions, counts.gbased, counts.plane,
                    counts.ship);
        std::printf("  flights %d  groups %d  bases %d  pilots %d  flak %d\\n",
                    counts.flights, counts.groups, counts.bases, counts.pilots,
                    counts.flak);
        std::printf("  strike areas %d  timeslots %d\\n", counts.strike_targets,
                    counts.timeslots);
        if (!mission_out.empty()) {
            if (!doc.save(mission_out, &error)) {
                std::fprintf(stderr, "%s\\n", error.c_str());
                return 1;
            }
            ef::MissionDocument again;
            if (!ef::MissionDocument::load(mission_out, &again, &error)) {
                std::fprintf(stderr, "%s\\n", error.c_str());
                return 1;
            }
            const bool same = again.lines.size() == doc.lines.size();
            std::printf("  round-trip %s (%zu lines)\\n", same ? "ok" : "MISMATCH",
                        again.lines.size());
            if (!same) return 1;
        }
        return 0;
    }

    if (!conflict_path.empty()) {
        ef::Scenario scenario;
        std::string error;
        if (!ef::load_conflict(conflict_path, &scenario, &error)) {
            std::fprintf(stderr, "%s\n", error.c_str());
            return 1;
        }
        std::printf("scenario %s\n", conflict_path.c_str());
        std::printf("  difficulty %d  performance %d %d\n", scenario.difficulty,
                    scenario.performance_a, scenario.performance_b);
        std::printf("  world status");
        for (int v : scenario.world_status) std::printf(" %d", v);
        std::printf("\n  areas %zu  mobiles %zu  pilots %zu  allegiance rows %zu\n",
                    scenario.areas.size(), scenario.mobiles.size(),
                    scenario.pilots.size(), scenario.allegiances.size());
        for (const char* kind : {"PL", "GB", "SH"}) {
            const auto groups = ef::group_mobiles(scenario, kind);
            int units = 0;
            for (const auto& g : groups) units += g.count();
            std::printf("  %s: %d groups, %d units\n", kind,
                        static_cast<int>(groups.size()), units);
            for (std::size_t i = 0; i < groups.size() && i < 5; ++i) {
                std::printf("    %-8s (%3d,%3d) %3d\n", groups[i].side.c_str(),
                            groups[i].pos.x, groups[i].pos.y, groups[i].count());
            }
        }
        if (!conflict_out.empty()) {
            if (!ef::save_conflict(conflict_out, scenario, &error)) {
                std::fprintf(stderr, "%s\n", error.c_str());
                return 1;
            }
            ef::Scenario again;
            if (!ef::load_conflict(conflict_out, &again, &error)) {
                std::fprintf(stderr, "%s\n", error.c_str());
                return 1;
            }
            const bool same =
                again.areas.size() == scenario.areas.size() &&
                again.mobiles.size() == scenario.mobiles.size() &&
                again.pilots.size() == scenario.pilots.size();
            std::printf("  round-trip %s\n", same ? "ok" : "MISMATCH");
            if (!same) return 1;
        }
        return 0;
    }

    if (!campaign_accept_dir.empty()) {
        // Drives the Campaign Level Accept path: the dialog raises
        // "Generating Campaign...", whose onopen hook rebuilds the files.
        ef::GuiSystem system;
        ef::register_observed_routines(system, nullptr, campaign_accept_dir);
        system.run_window_hook("generatecamp", "gpwGenerateCampaign");
        const auto campaign = std::filesystem::path(campaign_accept_dir) /
                              "CAMPAIGN.CG";
        const auto mission = std::filesystem::path(campaign_accept_dir) /
                             "MISSION.CFG";
        std::printf("campaign accept: %s\n", system.campaign_started() ? "ran" : "skipped");
        std::printf("  CAMPAIGN.CG %s\n",
                    std::filesystem::exists(campaign) ? "present" : "missing");
        std::printf("  MISSION.CFG %s\n",
                    std::filesystem::exists(mission) ? "present" : "missing");
        return 0;
    }

    if (campaign_selftest) {
        int failures = 0;
        auto check = [&](const char* what, bool ok) {
            std::printf("  %-52s %s\n", what, ok ? "ok" : "FAIL");
            if (!ok) ++failures;
        };
        std::vector<ef::TargetSite> table;
        std::string error;
        check("target table loads 543 areas",
              ef::load_target_table(data, &table, &error) && table.size() == 543);
        ef::Scenario scenario;
        const std::string conflict = data + "/CONFLICT.CG";
        check("scenario parses", ef::load_conflict(conflict, &scenario, &error));
        ef::Scenario state;
        const bool have_state = ef::load_conflict(data + "/CAMPAIGN.CG", &state, &error);

        ef::GenSettings settings;
        settings.seed = 0x31415926u;
        ef::GeneratedCampaign campaign;
        const bool ok = ef::generate_campaign(scenario, have_state ? &state : nullptr,
                                              table, settings, &campaign, &error);
        check("generator runs", ok);
        if (ok) {
            check("543 areas in table order", campaign.areas.size() == 543);
            check("77 bases", campaign.bases.size() == 77);
            const auto& expected =
                have_state && !state.mobiles.empty() ? state.mobiles
                                                     : scenario.mobiles;
            check("mobiles carried forward from the previous state",
                  campaign.mobiles.size() == expected.size() &&
                      campaign.state_mobiles.size() == expected.size());
            int airfields = 0, ports = 0;
            for (const auto& base : campaign.bases) {
                if (base.port) ++ports; else ++airfields;
            }
            check("28 airfields and 49 ports", airfields == 28 && ports == 49);
            check("air defence missions in range",
                  campaign.counts.gbased >= 400 && campaign.counts.gbased <= 560);
            check("229 air missions", campaign.counts.plane == 229);
            check("13 sea supply missions", campaign.counts.ship == 13);
            check("flight records in range",
                  campaign.counts.flights >= 250 && campaign.counts.flights <= 280);
            check("strike targets in range", campaign.counts.strike_targets >= 20 &&
                                                 campaign.counts.strike_targets <= 50);
            // Pins the value-draw rule: with a captured seed the upper half of the
        // state selects the thousands digit, one draw per airfield/port in
        // output order.
        {
            ef::CampaignRng probe(1795127529u);
            int got[5] = {};
            for (int& v : got) v = static_cast<int>((probe.next() >> 16) % 5);
            check("value draws match the captured sequence",
                  got[0] == 4 && got[1] == 4 && got[2] == 0 && got[3] == 4 &&
                      got[4] == 3);
        }
        {
            // A second capture, whose session consumed three draws before
            // generation started.
            ef::CampaignRng probe(1795137493u);
            for (int i = 0; i < 3; ++i) probe.next();
            int got[5] = {};
            for (int& v : got) v = static_cast<int>((probe.next() >> 16) % 5);
            check("value draws replay a second capture",
                  got[0] == 1 && got[1] == 0 && got[2] == 3 && got[3] == 1 &&
                      got[4] == 3);
        }
        check("mission file round-trips",
                  [&] {
                      ef::MissionDocument again;
                      const std::string tmp = "/tmp/openef2000-selftest.cfg";
                      if (!campaign.missions.save(tmp, &error)) return false;
                      if (!ef::MissionDocument::load(tmp, &again, &error)) return false;
                      std::remove(tmp.c_str());
                      return again.lines.size() == campaign.missions.lines.size();
                  }());
        }
        std::printf("%s (%d failure%s)\n",
                    failures == 0 ? "campaign selftest: PASS"
                                  : "campaign selftest: FAIL",
                    failures, failures == 1 ? "" : "s");
        return failures == 0 ? 0 : 1;
    }

    if (!campaign_src.empty()) {
        ef::Scenario scenario;
        std::string error;
        if (!ef::load_conflict(campaign_src, &scenario, &error)) {
            std::fprintf(stderr, "%s\n", error.c_str());
            return 1;
        }
        if (campaign_difficulty >= 0) scenario.difficulty = campaign_difficulty;

        ef::Scenario state;
        bool have_state = false;
        std::string state_path = campaign_state;
        if (state_path.empty()) state_path = data + "/CAMPAIGN.CG";
        if (!state_path.empty() && std::filesystem::exists(state_path)) {
            have_state = ef::load_conflict(state_path, &state, &error);
            if (!have_state) {
                std::fprintf(stderr, "%s\n", error.c_str());
                return 1;
            }
        }

        std::vector<ef::TargetSite> table;
        if (!ef::load_target_table(data, &table, &error)) {
            std::fprintf(stderr, "%s\n", error.c_str());
            return 1;
        }

        ef::GenSettings settings;
        settings.difficulty = scenario.difficulty;
        settings.seed = campaign_seed;
        settings.world_status = {0, 0, 0, 2};
        settings.draw_offset = campaign_offset;

        ef::GeneratedCampaign campaign;
        if (!ef::generate_campaign(scenario, have_state ? &state : nullptr, table,
                                   settings, &campaign, &error)) {
            std::fprintf(stderr, "%s\n", error.c_str());
            return 1;
        }

        int sides[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
        for (const auto& base : campaign.bases) {
            for (int i = 0; i < 9; ++i) {
                if (base.side == ef::side_name(i)) ++sides[i];
            }
        }
        std::printf("campaign seed 0x%08x difficulty %d\n", campaign_seed,
                    scenario.difficulty);
        std::printf("  areas %zu  bases %zu  mobiles %zu\n",
                    campaign.areas.size(), campaign.bases.size(),
                    campaign.mobiles.size());
        std::printf("  airfields");
        for (int i = 0; i < 9; ++i) {
            if (sides[i] != 0) std::printf(" %s=%d", ef::side_name(i), sides[i]);
        }
        std::printf("\n");
        std::printf("  missions %d (gbased %d, ship %d, plane %d), flights %d\n",
                    campaign.counts.missions, campaign.counts.gbased,
                    campaign.counts.ship, campaign.counts.plane,
                    campaign.counts.flights);

        if (!campaign_out.empty()) {
            std::filesystem::create_directories(campaign_out);
            const std::string out = campaign_out + "/CAMPAIGN.CG";
            if (!ef::write_campaign_file(out, scenario, campaign, &error)) {
                std::fprintf(stderr, "%s\n", error.c_str());
                return 1;
            }
            const std::string mission_out = campaign_out + "/MISSION.CFG";
            if (!campaign.missions.save(mission_out, &error)) {
                std::fprintf(stderr, "%s\n", error.c_str());
                return 1;
            }
            std::printf("wrote %s\n", out.c_str());
            std::printf("wrote %s\n", mission_out.c_str());
        }
        return 0;
    }

    if (boot) {
        if (gui_screen.empty()) gui_screen = "mainmenu";
    }

    if (gui_list || !gui_screen.empty() || !dump_palette.empty()) {
        namespace fs = std::filesystem;
        // EF2000 ships two archives. DID1ST.DAT holds the later, larger GUI
        // set (missplan, summary, custom, directp, intro) and the campaign
        // level dialog; prefer it when it is present.
        fs::path archive_path = fs::path(data) / "DID.DAT";
        if (!archive_override.empty()) {
            archive_path = fs::path(archive_override);
        } else if (fs::exists(fs::path(data) / "DID1ST.DAT")) {
            archive_path = fs::path(data) / "DID1ST.DAT";
        }
        if (!fs::exists(archive_path)) {
            std::fprintf(stderr, "no %s\n", archive_path.string().c_str());
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
            ef::register_observed_routines(system);
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
        if (!dump_palette.empty()) {
            auto pal = ef::load_gui_palette(did, dump_palette);
            if (!pal) {
                std::fprintf(stderr, "palette not found: %s\n", dump_palette.c_str());
                return 1;
            }
            for (int i = 0; i < 256; ++i) {
                std::printf("%3d %06x\n", i, (*pal)[static_cast<std::size_t>(i)]);
            }
            return 0;
        }
        if (gui_dump) {
            ef::GuiLoader loader(did);
            std::string error;
            auto screen = loader.load(gui_screen, &error);
            if (!screen) {
                std::fprintf(stderr, "%s\n", error.c_str());
                return 1;
            }
            std::printf("screen %s: %zu windows", gui_screen.c_str(),
                        screen->windows.size());
            if (!screen->startup.empty())
                std::printf(", startup %s", screen->startup.c_str());
            if (!screen->palette.empty())
                std::printf(", pal %s", screen->palette.c_str());
            if (!screen->picture.empty())
                std::printf(", picture %s", screen->picture.c_str());
            std::printf("\n");
            for (const auto& w : screen->windows) {
                std::printf("\nwindow %s %d,%d %dx%d%s%s%s\n", w.name.c_str(), w.x,
                            w.y, w.w, w.h, w.open ? " open" : "",
                            w.scroll_x ? " xscroll" : "", w.scroll_y ? " yscroll" : "");
                if (!w.title.empty()) {
                    auto t = w.title.find(gui_lang);
                    std::printf("  title \"%s\"\n",
                                (t != w.title.end() ? t->second : w.title.begin()->second).c_str());
                }
                if (w.work_w > 0 || w.work_h > 0) {
                    std::printf("  work %dx%d\n", w.work_w, w.work_h);
                }
                if (!w.on_open.empty()) std::printf("  onopen %s\n", w.on_open.c_str());
                if (!w.on_close.empty()) std::printf("  onclose %s\n", w.on_close.c_str());
                for (const auto& a : w.actions) {
                    std::printf("  %-10s -> %s%s%s\n", a.event.c_str(),
                                a.verb.c_str(), a.value.empty() ? "" : " ",
                                a.value.c_str());
                }
                for (const auto& i : w.icons) {
                    std::printf("  icon %-14s %3d,%-3d %3dx%-3d", i.name.c_str(), i.x,
                                i.y, i.w, i.h);
                    if (!i.key.empty()) std::printf(" key=%s", i.key.c_str());
                    if (!i.skey.empty()) std::printf(" skey=%s", i.skey.c_str());
                    if (i.bkd != -1) std::printf(" bkd=%d", i.bkd);
                    if (i.box != -1) std::printf(" box=%d", i.box);
                    if (i.esg >= 0) std::printf(" esg=%d", i.esg);
                    if (i.toggle) std::printf(" toggle");
                    if (i.editable_len >= 0) std::printf(" editable=%d", i.editable_len);
                    const std::string label = i.label(gui_lang);
                    if (!label.empty()) std::printf(" \"%s\"", label.c_str());
                    std::printf("\n");
                    for (const auto& a : i.actions) {
                        std::printf("    %-10s -> %s%s%s\n", a.event.c_str(),
                                    a.verb.c_str(), a.value.empty() ? "" : " ",
                                    a.value.c_str());
                    }
                }
            }
            return 0;
        }
        if (!gui_render.empty()) {
            ef::GuiRenderOptions options;
            options.language = gui_lang;
            options.open_window = gui_window;
            options.data_dir = data;
            std::optional<ef::Dataset> dataset;
            try {
                dataset = ef::load_dataset(data);
            } catch (const std::exception&) {
                // Screens without map data still render.
            }
            options.dataset = dataset ? &*dataset : nullptr;
            options.camp_zoom = camp_zoom;
            options.camp_sam = camp_sam;
            options.camp_ewr = camp_ewr;
            options.camp_sides = camp_sides;
            options.camp_layers = camp_layers;
            options.camp_pan_x = camp_pan_x;
            options.camp_pan_y = camp_pan_y;
            options.camp_mode = camp_mode;
            options.camp_flags = camp_flags;
            options.camp_fuel = camp_fuel;
            options.camp_mission = camp_mission;
            if (!ef::render_gui_png(did, gui_screen, gui_render, options)) {
                return 1;
            }
            std::printf("wrote %s\n", gui_render.c_str());
            return 0;
        }
        return run_gui_viewer(did, gui_screen, gui_lang, data, boot);
    }

    if (models || !mesh_name.empty()) {
        namespace fs = std::filesystem;
        // EF2000 ships two archives. DID1ST.DAT holds the later, larger GUI
        // set (missplan, summary, custom, directp, intro) and the campaign
        // level dialog; prefer it when it is present.
        fs::path archive_path = fs::path(data) / "DID.DAT";
        if (!archive_override.empty()) {
            archive_path = fs::path(archive_override);
        } else if (fs::exists(fs::path(data) / "DID1ST.DAT")) {
            archive_path = fs::path(data) / "DID1ST.DAT";
        }
        if (!fs::exists(archive_path)) {
            std::fprintf(stderr, "no %s\n", archive_path.string().c_str());
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
