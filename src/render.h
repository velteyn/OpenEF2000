#pragma once

#include "model.h"

#include <SDL2/SDL.h>

#include <string>

namespace ef {

struct RenderOptions {
    int width = 1280;
    int height = 900;
    int selected = 0;
    bool labels = true;
    bool focus = false;
};

SDL_Surface* render_map(const Dataset& ds, const RenderOptions& opt);
bool save_png(SDL_Surface* surface, const std::string& path);
void print_mission(const Dataset& ds, int index);
void print_stats(const Dataset& ds);

}  // namespace ef
