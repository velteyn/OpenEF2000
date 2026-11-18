#include "parser.h"
#include "render.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
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
        "  --interactive       open the SDL window\n");
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
    int mission = 0;
    bool stats = false, list = false, interactive = false, focus = false;

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
