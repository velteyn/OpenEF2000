#include "did.h"
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
#include <memory>
#include <string>
#include <vector>

namespace {

void usage() {
    std::printf(
        "EF2000 Mission Explorer\n"
        "  --data DIR          game data directory (default: ../EF20003DFX2)\n"
        "  --mission N         mission index (default: 0)\n"
        "  --stats             print dataset counts\n"
        "  --list              list missions\n"
        "  --focus             fit the map to the selected mission\n"
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
        "  --palette NAME      palette in colours\\<NAME>.col (default 2000)\n");
}

std::string default_data() {
    namespace fs = std::filesystem;
    if (fs::exists("../EF20003DFX2/MISSION.CFG")) return "../EF20003DFX2";
    if (fs::exists("EF20003DFX2/MISSION.CFG")) return "EF20003DFX2";
    return "../EF20003DFX2";
}

int run_interactive(const ef::Dataset& ds, int selected, bool focus) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window* win = SDL_CreateWindow("EF2000 Mission Explorer",
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
    candidates.push_back(data.parent_path() / "EF2000" / "CLUMP_3.TXT");

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
    SDL_Window* win = SDL_CreateWindow("EF2000 Model Viewer",
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
                    clumps = fs::path(data).parent_path() / "EF2000" / "CLUMP_3.TXT";
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
    if (interactive) return run_interactive(ds, mission, focus);

    if (!export_path.empty()) {
        ef::RenderOptions opt;
        opt.selected = mission;
        opt.focus = focus;
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
