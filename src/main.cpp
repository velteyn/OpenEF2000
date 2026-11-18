#include "did.h"
#include "meshview.h"
#include "parser.h"
#include "render.h"
#include "shape3d.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

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
        "  --mesh-preview FILE render the loaded mesh to a PNG sheet\n");
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

}  // namespace

int main(int argc, char** argv) {
    std::string data = default_data();
    std::string export_path;
    std::string mesh_name;
    std::string export_mesh;
    std::string mesh_preview;
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
                const fs::path clumps = fs::path(data) / "CLUMP_3.TXT";
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
