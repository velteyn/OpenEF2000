// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 velteyn

#include "gui.h"

#include "campaigngen.h"
#include "conflict.h"
#include "model.h"
#include "stats.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <cstdlib>
#include <fstream>
#include <iterator>

namespace ef {
namespace {

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trim(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.pop_back();
    std::size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    return s.substr(i);
}

//! `english` / `french` / `german`, tolerating the truncated `englis` in
//! mainmenu.gui. No token at all also means English.
std::optional<std::string> language(const std::string& token) {
    const std::string t = lower(token);
    if (t.rfind("englis", 0) == 0) return "en";
    if (t.rfind("french", 0) == 0) return "fr";
    if (t.rfind("german", 0) == 0) return "de";
    if (t.rfind("spanish", 0) == 0) return "es";
    if (t.rfind("italian", 0) == 0) return "it";
    return std::nullopt;
}

std::vector<std::string> words_of(const std::string& line) {
    std::vector<std::string> out;
    std::size_t p = 0;
    while (p < line.size()) {
        while (p < line.size() && (line[p] == ' ' || line[p] == '\t')) ++p;
        if (p >= line.size()) break;
        auto e = line.find_first_of(" \t", p);
        if (e == std::string::npos) e = line.size();
        out.push_back(line.substr(p, e - p));
        p = e;
    }
    return out;
}

//! Text after the first `count` words, i.e. `text english Mission Debrief`
//! with count=2 gives "Mission Debrief".
std::string rest_after(const std::string& line, std::size_t count) {
    std::size_t p = 0;
    for (std::size_t i = 0; i < count; ++i) {
        while (p < line.size() && (line[p] == ' ' || line[p] == '\t')) ++p;
        while (p < line.size() && line[p] != ' ' && line[p] != '\t') ++p;
    }
    return trim(line.substr(std::min(p, line.size())));
}

int to_int(const std::string& s, int fallback = 0) {
    char* end = nullptr;
    const long v = std::strtol(s.c_str(), &end, 0);
    return end == s.c_str() ? fallback : static_cast<int>(v);
}

//! Verbs that can follow a `call <event>` / `select <event>` block.
bool is_action_verb(const std::string& head) {
    static const char* kV[] = {"routine", "screen", "task", "openwindow", "closewindow",
                               "submenu", "return", "openmenu", "closedown"};
    for (const char* v : kV) {
        if (head == v) return true;
    }
    return false;
}

}  // namespace

std::string GuiWidget::label(const std::string& lang) const {
    if (auto it = text.find(lang); it != text.end()) return it->second;
    if (auto it = text.find("en"); it != text.end()) return it->second;
    return {};
}

std::optional<GuiAction> GuiWidget::action(const std::string& event) const {
    for (const auto& a : actions) {
        if (a.event == event) return a;
    }
    return std::nullopt;
}

GuiScreen GuiLoader::parse(const std::string& name, const std::string& text) {
    GuiScreen screen;
    screen.name = name;

    // `screen.windows` grows, so a stable index is used instead of a pointer.
    std::optional<std::size_t> wi;      // current window index
    std::optional<std::size_t> ii;      // current icon index within that window
    std::string event = "click";        // verbs before any `call` attach to click

    auto current_window = [&]() -> GuiWindow* {
        return wi && *wi < screen.windows.size() ? &screen.windows[*wi] : nullptr;
    };
    auto current_icon = [&]() -> GuiWidget* {
        auto* w = current_window();
        if (w == nullptr || !ii || *ii >= w->icons.size()) return nullptr;
        return &w->icons[*ii];
    };
    auto add_action = [&](const std::string& verb, const std::string& value) {
        if (auto* icon = current_icon()) {
            icon->actions.push_back(GuiAction{event, verb, value});
        } else if (auto* win = current_window()) {
            // Window-level verbs: the map windows bind their click handler
            // this way (`call Pressed` + `routine mpClickMap`).
            win->actions.push_back(GuiAction{event, verb, value});
        }
    };

    std::size_t pos = 0;
    while (pos <= text.size()) {
        auto eol = text.find('\n', pos);
        if (eol == std::string::npos) eol = text.size();
        std::string line = text.substr(pos, eol - pos);
        pos = eol + 1;

        if (auto semi = line.find(';'); semi != std::string::npos) line.erase(semi);
        line = trim(line);
        while (!line.empty() && line.back() == '\\') line = trim(line.substr(0, line.size() - 1));
        if (line.empty()) continue;

        // The DSL mixes `xpos 0` and `xpos=0`; normalise the leading token.
        {
            const std::string first = line.substr(0, line.find_first_of(" \t"));
            if (auto eq = first.find('='); eq != std::string::npos && eq + 1 < first.size()) {
                const std::string key = first.substr(0, eq);
                const std::string value = first.substr(eq + 1);
                const std::string rest = trim(line.substr(first.size()));
                line = key + " " + value + (rest.empty() ? "" : " " + rest);
            }
        }

        const auto words = words_of(line);
        const std::string head = lower(words[0]);

        // ---- block openers -------------------------------------------------
        if (head == "window" || head == "menu") {
            if (words.size() > 1) {
                GuiWindow w;
                w.name = words[1];
                screen.windows.push_back(std::move(w));
                screen.order.push_back(screen.windows.back().name);
                wi = screen.windows.size() - 1;
            } else {
                wi.reset();
            }
            ii.reset();
            continue;
        }
        if (head == "icon" && words.size() > 1) {
            if (auto* w = current_window(); w != nullptr) {
                GuiWidget icon;
                icon.name = words[1];
                // An icon that omits geometry inherits it from the previous
                // icon; that is how the debrief label/value rows and the
                // LEAD..FOUR row are formed.
                if (!w->icons.empty()) {
                    const GuiWidget& prev = w->icons.back();
                    icon.x = prev.x;
                    icon.y = prev.y;
                    icon.w = prev.w;
                    icon.h = prev.h;
                }
                w->icons.push_back(std::move(icon));
                ii = w->icons.size() - 1;
                event = "click";
            }
            continue;
        }

        // ---- screen-level directives ---------------------------------------
        if (head == "startup") { if (words.size() > 1) screen.startup = words[1]; continue; }
        if (head == "picture") { if (words.size() > 1) screen.picture = words[1]; continue; }
        if (head == "pal") { if (words.size() > 1) screen.palette = words[1]; continue; }
        if (head == "include") { if (words.size() > 1) screen.includes.push_back(words[1]); continue; }

        // ---- geometry ------------------------------------------------------
        GuiWindow* win = current_window();
        GuiWidget* icon = current_icon();
        if (words.size() > 1) {
            const int value = to_int(words[1]);
            if (head == "xpos" || head == "ypos" || head == "xsize" || head == "ysize") {
                int* slot = nullptr;
                if (icon != nullptr) {
                    slot = head == "xpos"    ? &icon->x
                           : head == "ypos"  ? &icon->y
                           : head == "xsize" ? &icon->w
                                             : &icon->h;
                } else if (win != nullptr) {
                    slot = head == "xpos"    ? &win->x
                           : head == "ypos"  ? &win->y
                           : head == "xsize" ? &win->w
                                             : &win->h;
                }
                if (slot != nullptr) *slot = value;
                continue;
            }
            if (head == "workxsize" && win && !icon) { win->work_w = value; continue; }
            if (head == "workysize" && win && !icon) { win->work_h = value; continue; }

            if (icon != nullptr) {
                if (head == "box") { icon->box = value; continue; }
                if (head == "bkd") { icon->bkd = value; continue; }
                if (head == "boxs") { icon->boxs = value; continue; }
                if (head == "bkds") { icon->bkds = value; continue; }
                if (head == "line") { icon->line = value; continue; }
                if (head == "textlen") { icon->textlen = value; continue; }
                if (head == "txtclr") { icon->txtclr = value; continue; }
                if (head == "txtclrs") { icon->txtclrs = value; continue; }
                if (head == "esg") { icon->esg = value; continue; }
            } else if (win != nullptr) {
                if (head == "box") { win->box = value; continue; }
                if (head == "bkd") { win->bkd = value; continue; }
                if (head == "backgroundcolour") { win->background_colour = value; continue; }
            }
        }

        // ---- text ----------------------------------------------------------
        if (head == "text" || head == "title") {
            if (words.size() < 2) continue;
            auto* dest = (head == "text" && icon != nullptr) ? &icon->text : (win ? &win->title : nullptr);
            if (dest == nullptr) continue;
            if (auto lang = language(words[1])) {
                (*dest)[*lang] = rest_after(line, 2);
            } else {
                (*dest)["en"] = rest_after(line, 1);
            }
            continue;
        }
        if (head == "textpos" && icon != nullptr && words.size() > 1) {
            icon->textpos = lower(words[1]);
            continue;
        }
        if (icon != nullptr && (head == "key" || head == "skey") && words.size() > 1) {
            (head == "key" ? icon->key : icon->skey) = lower(words[1]);
            continue;
        }
        if (icon != nullptr && head == "editable") {
            // `editable N` is a text field of maximum length N.
            icon->editable_len = words.size() > 1 ? to_int(words[1]) : 0;
            if (icon->value.empty()) icon->value = icon->label("en");
            continue;
        }
        if (icon != nullptr && head == "editableoff") {
            // Declares the initial state only; the field stays editable.
            icon->editable_off = true;
            continue;
        }
        if (icon != nullptr && head == "toggle" && words.size() > 1 &&
            lower(words[1]) == "click") {
            icon->toggle = true;
            continue;
        }

        // ---- events and verbs ----------------------------------------------
        if ((head == "select" || head == "unselect") && icon != nullptr && words.size() > 1) {
            // `select entering` reads "select on entering".
            icon->actions.push_back(GuiAction{lower(words[1]), head, ""});
            continue;
        }
        if (head == "call" && words.size() > 1) {
            event = lower(words[1]);
            continue;
        }
        if (is_action_verb(head)) {
            if (words.size() > 1) add_action(head, words[1]);
            continue;
        }
        if (head == "routine" && words.size() > 1) {
            add_action(head, words[1]);
            continue;
        }

        // ---- window behaviour ----------------------------------------------
        if (win != nullptr && icon == nullptr) {
            if (head == "open") { win->open = true; continue; }
            if (head == "moveable") { win->moveable = true; continue; }
            if (head == "close") { win->close = true; continue; }
            if (head == "is3d") { win->is3d = true; continue; }
            if (head == "fullsize") { win->fullsize = true; continue; }
            if (head == "nojump") { win->nojump = true; continue; }
            if (head == "xscroll") { win->scroll_x = true; continue; }
            if (head == "yscroll") { win->scroll_y = true; continue; }
            if (head == "jumptofront") { win->jump_to_front = true; continue; }
            if (head == "infrontof" && words.size() > 1) { win->infront_of = words[1]; continue; }
            if (head == "closedown" && words.size() > 1) { win->close_down = words[1]; continue; }
            if (head == "mainloop" && words.size() > 1) { win->main_loop = words[1]; continue; }
            if (head == "onopen" && words.size() > 1) { win->on_open = words[1]; continue; }
            if (head == "onclose" && words.size() > 1) { win->on_close = words[1]; continue; }
            if (head == "redraw" && words.size() > 1) { win->redraw = words[1]; continue; }
            if (head == "update" && words.size() > 1) { win->update = words[1]; continue; }
        }
        if (icon != nullptr) {
            if (head == "disable") { icon->disable = true; continue; }
            if (head == "enable") { icon->disable = false; continue; }
        }
        // Everything else (xscroll, yscroll, resize, back, infrontof, offscreen,
        // skey, toggle, ...) is behaviour we do not model yet.
    }
    return screen;
}

std::optional<GuiScreen> GuiLoader::load(const std::string& name, std::string* error) const {
    auto entry = archive_.resolve(name, "gui\\", ".gui");
    if (!entry) {
        if (error) *error = "gui\\" + name + ".gui not found";
        return std::nullopt;
    }
    auto bytes = archive_.read(*entry);
    if (!bytes) {
        if (error) *error = "cannot read gui\\" + name + ".gui";
        return std::nullopt;
    }
    GuiScreen screen = parse(name, std::string(bytes->begin(), bytes->end()));

    // Follow `include`: merge the included screen's windows, later declarations
    // replacing earlier ones with the same name.
    for (const auto& inc : screen.includes) {
        auto inc_entry = archive_.resolve(inc, "gui\\", ".gui");
        if (!inc_entry) continue;
        auto inc_bytes = archive_.read(*inc_entry);
        if (!inc_bytes) continue;
        GuiScreen other = parse(inc, std::string(inc_bytes->begin(), inc_bytes->end()));
        for (auto& w : other.windows) {
            auto it = std::find_if(screen.windows.begin(), screen.windows.end(),
                                   [&](const GuiWindow& e) { return e.name == w.name; });
            if (it != screen.windows.end()) {
                const std::string keep = it->name;
                *it = std::move(w);
                it->name = keep;
            } else {
                screen.order.push_back(w.name);
                screen.windows.push_back(std::move(w));
            }
        }
    }
    return screen;
}

// ---------------------------------------------------------------------------
// runtime
// ---------------------------------------------------------------------------

void GuiSystem::set_screen(GuiScreen screen) {
    screen_ = std::move(screen);
    selected_.clear();
    next_z_ = 1;
    apply_initial_visibility();
}

void GuiSystem::apply_initial_visibility() {
    for (auto& w : screen_.windows) {
        // Visible if the screen marks it `open`; `menu` is the title bar the
        // system opens itself.
        w.visible = w.open || w.name == "menu";
        w.z = w.visible ? next_z_++ : 0;
    }
    // `infrontof <window>` asks for a specific stacking order; satisfy it by
    // swapping ranks so the window ends up ahead of its reference.
    for (auto& w : screen_.windows) {
        if (!w.visible || w.infront_of.empty()) continue;
        auto* target = find_window(w.infront_of);
        if (target != nullptr && target->visible && w.z < target->z) {
            std::swap(w.z, target->z);
        }
    }
}

GuiWindow* GuiSystem::find_window(const std::string& name) {
    for (auto& w : screen_.windows) {
        if (w.name == name) return &w;
    }
    return nullptr;
}

GuiWidget* GuiSystem::find_widget(const std::string& window, const std::string& icon) {
    auto* w = find_window(window);
    if (w == nullptr) return nullptr;
    for (auto& i : w->icons) {
        if (i.name == icon) return &i;
    }
    return nullptr;
}

GuiWindow* GuiSystem::window_at(int px, int py) {
    GuiWindow* best = nullptr;
    for (auto& w : screen_.windows) {
        if (!w.visible || w.w <= 0 || w.h <= 0) continue;
        if (px < w.x || py < w.y || px >= w.x + w.w || py >= w.y + w.h) continue;
        if (best == nullptr || w.z > best->z) best = &w;
    }
    return best;
}

void GuiSystem::open_window(const std::string& name) {
    auto* w = find_window(name);
    if (w == nullptr) return;
    w->visible = true;
    w->z = next_z_++;
    // `jumptofront` asks for the window to be raised whenever it opens.
    if (w->jump_to_front) bring_to_front(name);
    run_window_hook(name, w->on_open);
}

void GuiSystem::close_window(const std::string& name) {
    auto* w = find_window(name);
    if (w == nullptr || !w->visible) return;
    run_window_hook(name, w->on_close);
    if (auto* still = find_window(name)) still->visible = false;
}

// Window hooks (`onopen`/`onclose`) are routine names, like icon verbs. A hook
// that reopens or closes its own window would recurse, so it is skipped.
void GuiSystem::run_window_hook(const std::string& window, const std::string& name) {
    if (name.empty()) return;
    if (std::find(hook_stack_.begin(), hook_stack_.end(), name) != hook_stack_.end())
        return;
    auto it = routines_.find(name);
    if (it == routines_.end()) {
        if (std::find(unimplemented_.begin(), unimplemented_.end(), name) ==
            unimplemented_.end())
            unimplemented_.push_back(name);
        return;
    }
    hook_stack_.push_back(name);
    it->second(*this, window, std::string());
    hook_stack_.pop_back();
}

void GuiSystem::bring_to_front(const std::string& name) {
    if (auto* w = find_window(name)) w->z = next_z_++;
}

void GuiSystem::select(const std::string& window, const std::string& icon, bool on) {
    auto* w = find_window(window);
    if (w == nullptr) return;
    GuiWidget* target = nullptr;
    for (auto& i : w->icons) {
        if (i.name == icon) target = &i;
    }
    if (target == nullptr) return;
    // `esg 0` means "no exclusive group" in the shipped files: the map
    // option checkboxes and plain buttons use it. Only positive groups are
    // exclusive.
    if (target->esg > 0) {
        for (auto& other : w->icons) {
            if (&other != target && other.esg == target->esg) {
                selected_[{window, other.name}] = false;
            }
        }
    }
    selected_[{window, icon}] = on;
}

bool GuiSystem::is_selected(const std::string& window, const std::string& icon) const {
    auto it = selected_.find({window, icon});
    return it != selected_.end() && it->second;
}

std::vector<GuiAction> GuiSystem::fire(const std::string& window,
                                       const std::string& icon,
                                       const std::string& event) {
    auto* widget = find_widget(window, icon);
    if (widget == nullptr) return {};
    // Copy: handlers may mutate the widget list through open/close.
    const auto actions = widget->actions;
    return run_actions(actions, window, icon, event);
}

std::vector<GuiAction> GuiSystem::fire_window(const std::string& window,
                                              const std::string& event) {
    auto* w = find_window(window);
    if (w == nullptr) return {};
    const auto actions = w->actions;
    return run_actions(actions, window, std::string(), event);
}

std::vector<GuiAction> GuiSystem::run_actions(const std::vector<GuiAction>& actions,
                                              const std::string& window,
                                              const std::string& icon,
                                              const std::string& event) {
    std::vector<GuiAction> ran;
    for (const auto& a : actions) {
        if (a.event != event) continue;
        if (a.verb == "select") {
            if (!icon.empty()) {
                const bool flip = [&] {
                    auto* w = find_window(window);
                    if (w == nullptr) return false;
                    for (auto& i : w->icons)
                        if (i.name == icon) return i.toggle;
                    return false;
                }() && is_selected(window, icon);
                select(window, icon, !flip);
            }
            continue;
        }
        if (a.verb == "unselect") {
            if (!icon.empty()) select(window, icon, false);
            continue;
        }
        if (a.verb == "screen" || a.verb == "task") screen_requests_.push_back(a.value);
        if (a.verb == "openwindow") open_window(a.value);
        if (a.verb == "closewindow") close_window(a.value);
        if (a.verb == "submenu" || a.verb == "openmenu") {
            // The named menu is defined in the same file (`menu=NAME` becomes a
            // window), so opening it is resolution rather than guesswork.
            if (find_window(a.value) != nullptr) {
                open_window(a.value);
                bring_to_front(a.value);
            } else {
                screen_requests_.push_back(a.value);
            }
        }
        if (a.verb == "routine") {
            if (auto it = routines_.find(a.value); it != routines_.end()) {
                it->second(*this, window, icon);
            } else if (std::find(unimplemented_.begin(), unimplemented_.end(), a.value) ==
                       unimplemented_.end()) {
                unimplemented_.push_back(a.value);
            }
        }
        log_.push_back(window + (icon.empty() ? "" : "." + icon) + ":" + event +
                       " -> " + a.verb + (a.value.empty() ? "" : " " + a.value));
        ran.push_back(a);
    }
    return ran;
}

SDL_Rect GuiSystem::client_rect(const GuiWindow& window) {
    // Icon coordinates are client-area coordinates: below the title bar when
    // the window has one, matching the renderer.
    const int title = window.title.empty() ? 0 : 16;
    return SDL_Rect{window.x, window.y + title, window.w,
                    std::max(window.h - title, 0)};
}

bool GuiSystem::scroll_window(const std::string& name, int dx, int dy) {
    auto* w = find_window(name);
    if (w == nullptr) return false;

    const SDL_Rect client = client_rect(*w);
    // A window may declare a work area larger than its client (the map
    // windows are 400x400 inside 400x339/400x380), so the scroll extent comes
    // from that when present.
    int content_w = w->work_w > 0 ? w->work_w : 0;
    int content_h = w->work_h > 0 ? w->work_h : 0;
    for (const auto& icon : w->icons) {
        content_w = std::max(content_w, icon.x + icon.w);
        content_h = std::max(content_h, icon.y + icon.h);
    }
    const int max_x = w->scroll_x ? std::max(0, content_w - client.w) : 0;
    const int max_y = w->scroll_y ? std::max(0, content_h - client.h) : 0;

    const int old_x = w->scroll_x_pos, old_y = w->scroll_y_pos;
    w->scroll_x_pos = std::clamp(w->scroll_x_pos + dx, 0, max_x);
    w->scroll_y_pos = std::clamp(w->scroll_y_pos + dy, 0, max_y);
    return old_x != w->scroll_x_pos || old_y != w->scroll_y_pos;
}
void GuiSystem::scroll_window_to(const std::string& name, int x, int y) {
    auto* w = find_window(name);
    if (w == nullptr) return;
    const SDL_Rect client = client_rect(*w);
    int content_w = w->work_w > 0 ? w->work_w : 0;
    int content_h = w->work_h > 0 ? w->work_h : 0;
    for (const auto& icon : w->icons) {
        content_w = std::max(content_w, icon.x + icon.w);
        content_h = std::max(content_h, icon.y + icon.h);
    }
    const int max_x = w->scroll_x ? std::max(0, content_w - client.w) : 0;
    const int max_y = w->scroll_y ? std::max(0, content_h - client.h) : 0;
    w->scroll_x_pos = std::clamp(x, 0, max_x);
    w->scroll_y_pos = std::clamp(y, 0, max_y);
}


bool GuiSystem::key_press(const std::string& raw) {
    std::string key = lower(raw);
    bool shift = false;
    if (key.rfind("shift+", 0) == 0) {
        shift = true;
        key = key.substr(6);
    }
    // Common spellings for the same physical key.
    if (key == "return" || key == "enter") key = "ret";
    if (key == "escape") key = "esc";

    // Topmost window first, so a dialog swallows keys from what is beneath it.
    std::vector<GuiWindow*> order;
    for (auto& w : screen_.windows) {
        if (w.visible) order.push_back(&w);
    }
    std::sort(order.begin(), order.end(),
              [](const GuiWindow* a, const GuiWindow* b) { return a->z > b->z; });

    for (GuiWindow* w : order) {
        for (auto& icon : w->icons) {
            if (icon.disable || icon.hidden) continue;
            // `skey` is a screen-local binding, not shift+key: the shipped
            // Quick Combat screen activates on plain R/P/T and F1..F12. With
            // Shift held, prefer the shift binding but still accept the plain
            // one, which is what the original does.
            const bool matches_plain = !icon.key.empty() && icon.key == key;
            const bool matches_screen = !icon.skey.empty() && icon.skey == key;
            if (!(shift ? (matches_screen || matches_plain)
                        : (matches_plain || matches_screen)))
                continue;
            // A key activates the widget the same way releasing the mouse does;
            // fire both activation events and let the file's verbs decide.
            fire(w->name, icon.name, "release");
            fire(w->name, icon.name, "click");
            return true;
        }
    }
    return false;
}

GuiWidget* GuiSystem::focused_field() {
    if (focus_.first.empty()) return nullptr;
    auto* widget = find_widget(focus_.first, focus_.second);
    if (widget == nullptr || widget->editable_len < 0) return nullptr;
    return widget;
}

void GuiSystem::focus_field(const std::string& window, const std::string& icon) {
    auto* widget = find_widget(window, icon);
    focus_ = (widget != nullptr && widget->editable_len >= 0)
                 ? std::make_pair(window, icon)
                 : std::make_pair(std::string(), std::string());
}

bool GuiSystem::type_text(const std::string& utf8) {
    auto* field = focused_field();
    if (field == nullptr) return false;
    if (static_cast<int>(field->value.size()) >= field->editable_len) return false;
    field->value += utf8;
    return true;
}

bool GuiSystem::backspace() {
    auto* field = focused_field();
    if (field == nullptr || field->value.empty()) return false;
    field->value.pop_back();
    return true;
}

bool GuiSystem::run_startup() {
    if (screen_.startup.empty()) return false;
    if (auto it = routines_.find(screen_.startup); it != routines_.end()) {
        it->second(*this, screen_.name, std::string());
        return true;
    }
    if (std::find(unimplemented_.begin(), unimplemented_.end(), screen_.startup) ==
        unimplemented_.end()) {
        unimplemented_.push_back(screen_.startup);
    }
    return false;
}

void GuiSystem::register_routine(const std::string& name,
                                 std::function<void(GuiSystem&, const std::string&,
                                                   const std::string&)> handler) {
    routines_[name] = std::move(handler);
}

std::vector<std::string> GuiSystem::referenced_routines() const {
    std::vector<std::string> names;
    auto add = [&](const std::string& n) {
        if (n.empty()) return;
        if (std::find(names.begin(), names.end(), n) == names.end()) names.push_back(n);
    };
    add(screen_.startup);
    for (const auto& w : screen_.windows) {
        add(w.on_open);
        add(w.on_close);
        add(w.redraw);
        add(w.update);
        add(w.close_down);
        add(w.main_loop);
        for (const auto& a : w.actions) {
            if (a.verb == "routine") add(a.value);
        }
        for (const auto& icon : w.icons) {
            for (const auto& a : icon.actions) {
                if (a.verb == "routine") add(a.value);
            }
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

std::vector<std::string> GuiSystem::missing_routines() const {
    std::vector<std::string> missing;
    for (const auto& name : referenced_routines()) {
        if (routines_.find(name) == routines_.end()) missing.push_back(name);
    }
    return missing;
}

std::vector<std::string> GuiSystem::take_screen_requests() {
    auto out = std::move(screen_requests_);
    screen_requests_.clear();
    return out;
}

// Routines whose effect has been established by running the original software
// and observing the result. Keep this list small and evidence-backed: each
// entry is a fact, not a guess, and the rest stay unimplemented on purpose.

namespace {

//! Human label for a mission role, as the briefing window shows it.
const char* role_label(const std::string& role) {
    if (role == "AF_KILL") return "Airfield Denial";
    if (role == "INTERD") return "Interdiction";
    if (role == "CAP") return "Combat Air Patrol";
    if (role == "INTC") return "Intercept";
    if (role == "SEAD") return "SEAD";
    if (role == "AWACS_PATROL") return "AWACS Patrol";
    if (role == "JSTARS_PATROL") return "JSTARS Patrol";
    if (role == "REFUELLER_PATROL") return "Tanker Patrol";
    if (role == "AWACS_KILL") return "Anti-AWACS";
    if (role == "REFUELLER_KILL") return "Anti-Tanker";
    if (role == "SEA_SUPPLY") return "Sea Supply";
    if (role == "AIR_DEFENSE") return "Air Defence";
    return role.c_str();
}

std::string strip_quotes(std::string s) {
    if (!s.empty() && s.front() == '"') s.erase(s.begin());
    if (!s.empty() && s.back() == '"') s.pop_back();
    return s;
}

//! First named target area of a mission, without its quotes.
std::string mission_target_name(const Mission& m) {
    for (const auto& t : m.targets) {
        for (const auto& a : t.areas) {
            if (!a.name.empty()) return strip_quotes(a.name);
        }
    }
    return "-";
}

//! Display form of the first target element (`RUNWAY` -> `Runway`).
std::string mission_target_kind(const Mission& m) {
    for (const auto& t : m.targets) {
        for (const auto& a : t.areas) {
            for (const auto& e : a.elements) {
                if (e.type.empty()) continue;
                std::string out;
                bool upper = true;
                for (char c : e.type) {
                    if (c == '_') {
                        out.push_back(' ');
                        upper = true;
                        continue;
                    }
                    out.push_back(upper
                                      ? static_cast<char>(std::toupper(
                                            static_cast<unsigned char>(c)))
                                      : static_cast<char>(std::tolower(
                                            static_cast<unsigned char>(c))));
                    upper = false;
                }
                return out;
            }
        }
    }
    return "-";
}

std::string pretty_weapon(std::string w) {
    for (char& c : w) {
        if (c == '_') c = ' ';
    }
    return w;
}

//! The air-to-ground weapon the strike flight carries, as the overview shows.
std::string mission_weapon(const Mission& m) {
    static const char* const kAirToAir[] = {
        "NO_WEAPON", "ASRAAM",     "AMRAAM", "SIDEWINDER", "SS225",
        "CENTRE_FUEL", "APHID",    "ALAMO",  "ARCHER",     "ADDER",
        "AIM_9",     "AIM_120",    "R550",   "R73",        "R27",
        "R77"};
    const Flight* strike = nullptr;
    for (const auto& f : m.flights) {
        if (f.role == "STRIKE" || f.role == "SEAD_STRIKE") {
            strike = &f;
            break;
        }
    }
    if (strike == nullptr) {
        if (m.flights.empty() || m.flights.front().pylons.empty()) return "-";
        return pretty_weapon(m.flights.front().pylons.front().second);
    }
    for (const auto& [slot, weapon] : strike->pylons) {
        (void)slot;
        bool air_to_air = false;
        for (const char* skip : kAirToAir) {
            if (weapon == skip) {
                air_to_air = true;
                break;
            }
        }
        if (!air_to_air) return pretty_weapon(weapon);
    }
    return "-";
}

//! Nearest other airfield on the mission's side, shown as the diversion.
std::string mission_diversion(const Dataset& ds, const Mission& m) {
    const int from = mission_base_index(m);
    if (from < 0 || from >= static_cast<int>(ds.bases.size())) return "-";
    const Base& home = ds.bases[static_cast<std::size_t>(from)];
    if (!home.pos) return "-";
    int best = -1;
    long best_d = -1;
    for (int i = 0; i < static_cast<int>(ds.bases.size()); ++i) {
        if (i == from) continue;
        const Base& b = ds.bases[static_cast<std::size_t>(i)];
        if (!b.pos || b.type == "PORT" || b.side != m.side) continue;
        const long dx = b.pos->x - home.pos->x;
        const long dy = b.pos->y - home.pos->y;
        const long d = dx * dx + dy * dy;
        if (best_d < 0 || d < best_d) {
            best_d = d;
            best = i;
        }
    }
    if (best < 0) return "-";
    return strip_quotes(ds.bases[static_cast<std::size_t>(best)].name);
}

int role_planes(const Mission& m, const char* role) {
    int n = 0;
    for (const auto& f : m.flights) {
        if (f.role == role) n += f.count;
    }
    return n;
}

//! Fills the Mission Overview window from the selected campaign mission.
void fill_mission_overview(GuiSystem& gui, const Dataset& ds) {
    if (gui.find_window("missionoverview") == nullptr || ds.missions.empty()) return;
    const int mi = std::clamp(gui.camp_mission(), 0,
                              static_cast<int>(ds.missions.size()) - 1);
    const Mission& m = ds.missions[mi];
    const auto set = [&](const char* icon, const std::string& value) {
        if (auto* w = gui.find_widget("missionoverview", icon); w != nullptr) {
            w->text["en"] = value;
            w->value = value;
        }
    };

    const int base_index = mission_base_index(m);
    std::string base = "-";
    if (base_index >= 0 && base_index < static_cast<int>(ds.bases.size()))
        base = strip_quotes(ds.bases[static_cast<std::size_t>(base_index)].name);

    int planes = 0;
    for (const auto& f : m.flights) planes += f.count;

    set("location", mission_target_name(m));
    set("target1", mission_target_kind(m));
    set("weapon", mission_weapon(m));
    set("base", base);
    set("diversion", mission_diversion(ds, m));
    set("role", role_label(m.role));
    set("planes", std::to_string(planes));
    set("strike", std::to_string(role_planes(m, "STRIKE")));
    set("wildweasel", std::to_string(role_planes(m, "WW")));
    set("escort", std::to_string(role_planes(m, "ESCORT")));
}

//! Rewrites the campaign mission list. The list shows the flying missions of
//! the side selected in the map mode bar, in mission-file order.
void fill_mission_list(GuiSystem& gui, const Dataset& ds) {
    GuiWindow* win = gui.find_window("missionsel");
    if (win == nullptr) return;

    const auto side_visible = [&](const std::string& side) {
        if (gui.camp_side() == 1) return side == "RUSSIAN";
        if (gui.camp_side() == 2)
            return side == "SWEDISH" || side == "FINNISH" || side == "BALTIC";
        return side == "NATO";
    };

    std::vector<int> rows;
    for (int i = 0; i < static_cast<int>(ds.missions.size()); ++i) {
        const Mission& m = ds.missions[static_cast<std::size_t>(i)];
        if (m.type != "PLANE") continue;
        if (side_visible(m.side)) rows.push_back(i);
    }
    if (rows.empty()) {
        for (int i = 0; i < static_cast<int>(ds.missions.size()); ++i) {
            const Mission& m = ds.missions[static_cast<std::size_t>(i)];
            if (m.type == "PLANE") rows.push_back(i);
        }
    }
    gui.set_camp_rows(rows);

    for (auto& icon : win->icons) {
        int n = std::atoi(icon.name.c_str());
        if (n < 1) continue;
        if (n <= static_cast<int>(rows.size())) {
            const Mission& m = ds.missions[static_cast<std::size_t>(rows[n - 1])];
            std::string label = role_label(m.role);
            const std::string target = mission_target_name(m);
            if (target != "-") label += " " + target;
            if (label.size() > 28) label.resize(28);
            icon.text["en"] = label;
            icon.value = label;
            icon.txtclr = 15;   // white on the dark panel
            icon.txtclrs = 0;   // dark text on the white selected row
            icon.bkds = 15;
            icon.disable = false;
            icon.hidden = false;
        } else {
            icon.text["en"].clear();
            icon.value.clear();
            icon.disable = true;
        }
    }
}

//! Fills the Mission Briefing window from the selected campaign mission.
void fill_mission_brief(GuiSystem& gui, const Dataset& ds) {
    if (gui.find_window("missionbrief") == nullptr || ds.missions.empty()) return;
    const int mi = std::clamp(gui.camp_mission(), 0,
                              static_cast<int>(ds.missions.size()) - 1);
    const Mission& m = ds.missions[mi];

    const auto set = [&](const char* icon, const std::string& value) {
        if (auto* w = gui.find_widget("missionbrief", icon); w != nullptr) {
            w->text["en"] = value;
            w->value = value;
        }
    };

    // Mission record: base, id, groups.
    const int id = m.values.size() > 1 ? m.values[1] : mi;
    char number[16];
    std::snprintf(number, sizeof(number), "%04d", id);
    set("missionnum", number);

    // The campaign clock, formatted as the briefing shows it.
    int hour = 0, minute = 0;
    if (ds.world_status.size() >= 3) {
        hour = ds.world_status[1];
        minute = ds.world_status[2];
    }
    char clock[16];
    std::snprintf(clock, sizeof(clock), "%02d:%02d:00", hour, minute);
    set("time", clock);

    set("type", role_label(m.role));

    char flight[8];
    std::snprintf(flight, sizeof(flight), "%03d", 1);
    set("flightnum", flight);
    set("callsign", "Lead");

    std::string target = "-";
    for (const auto& t : m.targets) {
        for (const auto& a : t.areas) {
            if (!a.name.empty()) {
                target = a.name;
                if (!target.empty() && target.front() == '"') target.erase(0, 1);
                if (!target.empty() && target.back() == '"') target.pop_back();
                break;
            }
        }
        if (target != "-") break;
    }
    set("target", target);

    int planes = 0;
    for (const auto& f : m.flights) planes += f.count;
    set("planes", std::to_string(planes));

    bool refuel = false;
    for (const auto& r : m.routes) {
        for (const auto& w : r.waypoints) {
            if (w.action == "REFUEL") refuel = true;
        }
    }
    // Support depends on the side still fielding a living platform: shooting
    // down the last AWACS turns the flag off. Fall back to the mission list
    // when no campaign roster was loaded.
    bool awacs = false;
    bool jstars = false;
    bool tanker = false;
    if (const auto it = ds.support.find(m.side); it != ds.support.end()) {
        awacs = it->second.awacs;
        jstars = it->second.jstars;
        tanker = it->second.tanker;
    } else {
        awacs = m.role == "AWACS_PATROL";
        jstars = m.role == "JSTARS_PATROL";
        for (const auto& other : ds.missions) {
            if (other.side != m.side) continue;
            if (other.role == "AWACS_PATROL") awacs = true;
            if (other.role == "JSTARS_PATROL") jstars = true;
        }
        tanker = true;
    }
    set("awacs", awacs ? "Yes" : "No");
    set("jstars", jstars ? "Yes" : "No");
    set("refuel", refuel && tanker ? "Yes" : "No");
}

}  // namespace

void select_campaign_mission(GuiSystem& system, const Dataset& dataset, int index) {
    system.set_camp_mission(index);
    if (!dataset.missions.empty()) {
        const int n = std::clamp(index, 0, static_cast<int>(dataset.missions.size()) - 1);
        const auto& rows = system.camp_rows();
        bool selected = false;
        for (std::size_t r = 0; r < rows.size(); ++r) {
            if (rows[r] == n) {
                const std::string row = std::to_string(r + 1);
                system.select("missionsel", row, true);
                if (GuiWindow* win = system.find_window("missionsel")) {
                    for (const auto& icon : win->icons) {
                        if (icon.name != row) continue;
                        win->scroll_y_pos = std::max(0, icon.y - 42);
                        break;
                    }
                }
                selected = true;
                break;
            }
        }
        if (!selected) system.select("missionsel", std::to_string(n + 1), true);
    }
    fill_mission_brief(system, dataset);
    fill_mission_overview(system, dataset);
}

void register_observed_routines(GuiSystem& system, const Dataset* dataset,
                                const std::filesystem::path& data_dir,
                                std::function<void()> refresh_data) {
    // Each entry below was established by running the shipped software and
    // observing the result, with the `.gui` data corroborating it.

    // Quit raises the `sure` dialog ("Quit EF2000 ?" with Yes/No).
    system.register_routine("QuitEF2000", [](GuiSystem& gui, const std::string&,
                                              const std::string&) {
        gui.open_window("sure");
    });
    system.register_routine("QuitEF2000yes", [](GuiSystem& gui, const std::string&,
                                                 const std::string&) {
        gui.request_quit();
    });
    system.register_routine("QuitEF2000no", [](GuiSystem& gui, const std::string&,
                                                const std::string&) {
        gui.close_window("sure");
    });

    // Campaign raises the "Campaign Level Setting" dialog.
    system.register_routine("gpwClickCampaign", [](GuiSystem& gui, const std::string&,
                                                    const std::string&) {
        gui.open_window("campaign_level");
    });

    // Multiplay: Network Settings and IPX Settings, both opened by the screen's
    // `startup gnStartup`.
    system.register_routine("gnStartup", [](GuiSystem& gui, const std::string&,
                                             const std::string&) {
        gui.open_window("settings");
        gui.open_window("IpxSettings");
    });

    // Quick Combat: the included hi-score panel is not marked `open`, so the
    // screen's startup routine raises it.
    system.register_routine("InitArcade", [](GuiSystem& gui, const std::string&,
                                              const std::string&) {
        gui.open_window("hiscores");
    });

    // Quick Combat: Rookie/Pro/Topgun share `esg 1`, so selecting one clears
    // the others and the indicator follows the selection.
    system.register_routine("gpwChangeDifficulty",
                            [](GuiSystem& gui, const std::string& window,
                               const std::string& icon) {
        if (window != "difficulty") return;
        if (icon != "rookie" && icon != "pro" && icon != "topgun") return;
        gui.select(window, icon, true);
    });

    // Quick Combat: mission grid 01..12 shares `esg 2`. Selecting a cell moves
    // the highlight; the Summary text is mission data and is not wired yet.
    system.register_routine("gpwSelectLevel",
                            [](GuiSystem& gui, const std::string& window,
                               const std::string& icon) {
        if (window != "miss_sel") return;
        if (icon.size() != 2) return;
        if (icon[0] < '0' || icon[0] > '9') return;
        if (icon[1] < '0' || icon[1] > '9') return;
        gui.select(window, icon, true);
    });

    // Quick Combat: Accept starts the chosen mission. The reconstruction has
    // no mission loader, so record the decision for the driver.
    system.register_routine("gpwArcadeAccepted",
                            [](GuiSystem& gui, const std::string&,
                               const std::string&) {
        gui.set_arcade_accepted(true);
    });

    // Campaign Level Setting: rows 0..9 are one `esg 3` group. `select` is
    // already what the data asks for; the routine records the chosen level.
    system.register_routine("gcSetCampaignDifficulty",
                            [](GuiSystem& gui, const std::string& window,
                               const std::string& icon) {
        if (icon.size() != 1 || icon[0] < '0' || icon[0] > '9') return;
        gui.select(window, icon, true);
        gui.set_campaign_difficulty(icon[0] - '0');
    });

    // Campaign Level Setting: "Influence Campaign?" Yes/No, one `esg 1` group.
    system.register_routine("gcPerformanceYes",
                            [](GuiSystem& gui, const std::string& window,
                               const std::string&) {
        gui.select(window, "yes", true);
        gui.set_campaign_influence(true);
    });
    system.register_routine("gcPerformanceNo",
                            [](GuiSystem& gui, const std::string& window,
                               const std::string&) {
        gui.select(window, "no", true);
        gui.set_campaign_influence(false);
    });

    // Observed: Accept closes the dialog and raises "Generating Campaign...".
    // Generation itself belongs to the campaign module; this records the
    // decision and shows the same window.
    system.register_routine("gcStartNewCampaign",
                            [](GuiSystem& gui, const std::string&,
                               const std::string&) {
        gui.set_campaign_started(true);
        gui.close_window("campaign_level");
        gui.open_window("generatecamp");
    });

    // Load a previous campaign: the `campfiles` window exists for exactly that
    // and is raised by the dialog's Load control.
    system.register_routine("gcLoadOldCampaign",
                            [](GuiSystem& gui, const std::string&,
                               const std::string&) {
        gui.open_window("campfiles");
    });
    system.register_routine("gcLoadfileAccepted",
                            [](GuiSystem& gui, const std::string&,
                               const std::string&) {
        gui.close_window("campfiles");
        gui.set_campaign_started(true);
    });

    // The main menu's startup routine has no visible effect in the shipped
    // game: every panel window is already `open`, and no other window is
    // raised. Registered so the screen does not report a missing routine.
    system.register_routine("InitMainMenu",
                            [](GuiSystem&, const std::string&,
                               const std::string&) {});

    // `generatecamp` declares `onopen gpwGenerateCampaign`; Accept raises that
    // window. This is the Accept path from the Campaign Level dialog: it
    // rebuilds CAMPAIGN.CG and MISSION.CFG in place, then leaves the
    // "Generating Campaign..." panel for the campaign map -- exactly the
    // original's sequence. With no data directory (GUI selftest) it only
    // records the decision.
    system.register_routine("gpwGenerateCampaign",
                            [data_dir, refresh_data = std::move(refresh_data)](
                                GuiSystem& gui, const std::string&,
                                const std::string&) {
        gui.set_campaign_started(true);
        if (data_dir.empty()) return;
        const auto conflict_path = data_dir / "CONFLICT.CG";
        const auto campaign_path = data_dir / "CAMPAIGN.CG";
        if (!std::filesystem::exists(conflict_path)) return;

        Scenario scenario;
        Scenario state;
        std::vector<TargetSite> table;
        std::string error;
        if (!load_conflict(conflict_path.string(), &scenario, &error)) return;
        const bool have_state = std::filesystem::exists(campaign_path) &&
                                load_conflict(campaign_path.string(), &state, &error);
        if (!load_target_table(data_dir.string(), &table, &error)) return;

        GenSettings settings;
        settings.data_dir = data_dir.string();
        settings.difficulty = gui.campaign_difficulty();
        settings.influence = gui.campaign_influence() ? 1 : 0;
        settings.seed = static_cast<std::uint32_t>(std::time(nullptr));

        GeneratedCampaign generated;
        if (!generate_campaign(scenario, have_state ? &state : nullptr, table,
                               settings, &generated, &error))
            return;
        if (!write_campaign_file(campaign_path.string(), scenario, generated, &error))
            return;
        if (!generated.missions.save((data_dir / "MISSION.CFG").string(), &error))
            return;

        if (refresh_data) refresh_data();
        gui.close_window("generatecamp");
        gui.request_screen("campaign");
    });

    // `debrief` declares `onclose gpwCloseDebrief`. The window is already
    // hidden by the caller; there is no debrief model to clear yet.
    system.register_routine("gpwCloseDebrief",
                            [](GuiSystem&, const std::string&,
                               const std::string&) {});

    // Credits: the window declares `onopen onopenCredits` / `onclose
    // oncloseCredits`. Opening shows the top of the list; closing is handled
    // by the caller, so the hook has nothing left to do.
    system.register_routine("onopenCredits",
                            [](GuiSystem& gui, const std::string& window,
                               const std::string&) {
        gui.scroll_window(window, 0, -1000000);
    });
    system.register_routine("oncloseCredits",
                            [](GuiSystem&, const std::string&,
                               const std::string&) {});

    // Simulator mission browser. `first` is entry 0 and the numbered icons
    // continue from "2", so "2" is entry 1.
    system.register_routine("gpwMissionSelected",
                            [](GuiSystem& gui, const std::string& window,
                               const std::string& icon) {
        int index = -1;
        if (icon == "first") index = 0;
        else if (!icon.empty()) {
            const int n = std::atoi(icon.c_str());
            if (n >= 2 && n <= 16) index = n - 1;
        }
        if (index < 0) return;
        gui.select(window, icon, true);
        gui.set_sim_mission(index);
    });

    // Parameters dialog: the icons only run `select`, so Accept reads the
    // selected icon of each group and records the choices.
    system.register_routine("gpwMissionAccepted",
                            [](GuiSystem& gui, const std::string&,
                               const std::string&) {
        const auto on = [&](const char* icon) {
            return gui.is_selected("params", icon);
        };
        if (on("ttwilight")) gui.set_sim_time(0);
        else if (on("tday")) gui.set_sim_time(1);
        else if (on("tnight")) gui.set_sim_time(2);
        if (on("wpoor")) gui.set_sim_weather(0);
        else if (on("wfair")) gui.set_sim_weather(1);
        else if (on("wgood")) gui.set_sim_weather(2);
        if (on("rookie")) gui.set_sim_difficulty(0);
        else if (on("pro")) gui.set_sim_difficulty(1);
        else if (on("topgun")) gui.set_sim_difficulty(2);
        gui.set_sim_default_weapons(on("defweapyes"));
        gui.set_sim_review_weapons(on("revweapyes"));
        gui.set_mission_accepted(true);
        gui.close_window("params");
    });

    // `onopenParams` restores the current choices when the dialog is shown.
    system.register_routine("onopenParams",
                            [](GuiSystem& gui, const std::string&,
                               const std::string&) {
        const char* time_icon = gui.sim_time() == 0 ? "ttwilight"
                              : gui.sim_time() == 2 ? "tnight" : "tday";
        const char* weather_icon = gui.sim_weather() == 0 ? "wpoor"
                                 : gui.sim_weather() == 2 ? "wgood" : "wfair";
        const char* diff_icon = gui.sim_difficulty() == 0 ? "rookie"
                              : gui.sim_difficulty() == 2 ? "topgun" : "pro";
        gui.select("params", time_icon, true);
        gui.select("params", weather_icon, true);
        gui.select("params", diff_icon, true);
        gui.select("params", gui.sim_default_weapons() ? "defweapyes" : "defweapno", true);
        gui.select("params", gui.sim_review_weapons() ? "revweapyes" : "revweapno", true);
    });

    // The mission list and details are filled from the mission data by the
    // driver; there is nothing to initialise in the GUI model itself.
    system.register_routine("InitPickMiss",
                            [](GuiSystem&, const std::string&,
                               const std::string&) {});
    system.register_routine("onopenCurrentMission",
                            [](GuiSystem&, const std::string&,
                               const std::string&) {});

    system.register_routine("gpwExitSimulator",
                            [](GuiSystem& gui, const std::string&,
                               const std::string&) {
        gui.request_screen("mainmenu");
    });

    // "WEAPONS REVIEW" opens the weapons screen.
    system.register_routine("gpwClickWeaponReview",
                            [](GuiSystem& gui, const std::string&,
                               const std::string&) {
        gui.request_screen("weapons");
    });

    // Quick Combat post-mission ---------------------------------------------
    system.register_routine("InitArcade2",
                            [](GuiSystem&, const std::string&,
                               const std::string&) {});
    system.register_routine("gpwContinueArcade",
                            [](GuiSystem& gui, const std::string&,
                               const std::string&) {
        gui.set_arcade_continued(true);
    });
    system.register_routine("gpwAcceptHiscore",
                            [](GuiSystem& gui, const std::string&,
                               const std::string&) {
        // The name lives in the enter-name field; the driver persists it.
        std::string name;
        if (auto* w = gui.find_widget("entername", "nametext")) name = w->value;
        gui.set_arcade_hiscore(name, true);
    });
    system.register_routine("arcNextDifficulty",
                            [](GuiSystem& gui, const std::string&,
                               const std::string&) {
        gui.set_arcade_round(gui.arcade_round() + 1);
    });
    system.register_routine("arcEndgameAccept",
                            [](GuiSystem& gui, const std::string&,
                               const std::string&) {
        gui.set_arcade_endgame(true);
        gui.request_screen("mainmenu");
    });

    // System tools: memory readout and magnifier are renderer-owned.
    system.register_routine("guiupdateMemoryUse",
                            [](GuiSystem&, const std::string&,
                               const std::string&) {});
    system.register_routine("updatemagnify",
                            [](GuiSystem&, const std::string&,
                               const std::string&) {});

    // Weapons configuration -------------------------------------------------
    // Pre-flight loadout: pick a pylon/missile, rotate the aircraft view,
    // clear one or all stations, restore defaults and accept.
    system.register_routine("InitPickWeap",
                            [](GuiSystem& gui, const std::string&,
                               const std::string&) {
        gui.set_weap_view(0);
        gui.set_weap_angle(0, 0);
        gui.set_weap_cleared_all(false);
        gui.set_weap_accepted(false);
    });
    system.register_routine("gpwMissileSelected",
                            [](GuiSystem& gui, const std::string& window,
                               const std::string& icon) {
        gui.select(window, icon, true);
        gui.set_weap_selected(icon);
        gui.set_weap_cleared_all(false);
    });
    system.register_routine("gpwPlaneAngled",
                            [](GuiSystem& gui, const std::string&,
                               const std::string&) {
        gui.set_weap_view(0);
        gui.set_weap_angle(0, 0);
    });
    system.register_routine("gpwPlaneFrontOn",
                            [](GuiSystem& gui, const std::string&,
                               const std::string&) {
        gui.set_weap_view(1);
    });
    system.register_routine("gpwPlaneSideOn",
                            [](GuiSystem& gui, const std::string&,
                               const std::string&) {
        gui.set_weap_view(2);
    });
    system.register_routine("gpwPlaneUnderneath",
                            [](GuiSystem& gui, const std::string&,
                               const std::string&) {
        gui.set_weap_view(3);
    });
    system.register_routine("gpwTurnXPlane",
                            [](GuiSystem& gui, const std::string&,
                               const std::string&) {
        gui.set_weap_angle((gui.weap_x_angle() + 15) % 360, gui.weap_y_angle());
    });
    system.register_routine("gpwTurnYPlane",
                            [](GuiSystem& gui, const std::string&,
                               const std::string&) {
        gui.set_weap_angle(gui.weap_x_angle(), (gui.weap_y_angle() + 15) % 360);
    });
    system.register_routine("gpwSetClearMissile",
                            [](GuiSystem& gui, const std::string&,
                               const std::string&) {
        gui.set_weap_selected(std::string());
    });
    system.register_routine("gpwSetClearAllMissiles",
                            [](GuiSystem& gui, const std::string&,
                               const std::string&) {
        gui.set_weap_selected(std::string());
        gui.set_weap_cleared_all(true);
    });
    system.register_routine("gpwUseDefaults",
                            [](GuiSystem& gui, const std::string&,
                               const std::string&) {
        gui.set_weap_cleared_all(false);
        gui.set_weap_selected(std::string());
    });
    system.register_routine("gpwWeaponsAccepted",
                            [](GuiSystem& gui, const std::string&,
                               const std::string&) {
        gui.set_weap_accepted(true);
    });
    for (const char* name : {"onopenBigCurrentWeapon", "onopenCurrentWeapon",
                             "gpwRedrawPlaneWindow", "gpwUpdatePlaneWindow"}) {
        system.register_routine(name, [](GuiSystem&, const std::string&,
                                         const std::string&) {});
    }

    // Summary screen -------------------------------------------------------
    // The bars and graphs are drawn by the renderer from campaign statistics;
    // the GUI layer only has to track menus and detail requests.
    system.register_routine("sumgencamp",
                            [](GuiSystem& gui, const std::string&,
                               const std::string&) {
        if (gui.find_window("sure") != nullptr) gui.open_window("sure");
        else gui.request_screen("camp");
    });
    system.register_routine("gpwshowsummaryinfo",
                            [](GuiSystem& gui, const std::string& window,
                               const std::string& icon) {
        // The startup call has no icon; open the General page's balance view
        // and its key so the screen is not an empty set of tabs.
        if (icon.empty()) {
            gui.open_window("balance");
            gui.open_window("drawkey");
            return;
        }
        gui.select(window, icon, true);
        gui.set_summary_selected(icon);
    });
    for (const char* name : {"sumairbar", "sumgroundbar", "sumstatbar",
                             "sumgenbar", "sumaircraftgraph", "sumgroundgraph",
                             "sumstaticgraph", "sumgengraph", "sumbalance",
                             "sumdrawkey"}) {
        system.register_routine(name, [](GuiSystem&, const std::string&,
                                         const std::string&) {});
    }

    // Mission planner ------------------------------------------------------
    // The planner reuses the camp map state (side/layer/coverage/zoom) and
    // adds its own selection and assignment state.
    system.register_routine("mpInitMP",
                            [](GuiSystem& gui, const std::string&,
                               const std::string&) {
        gui.set_mp_speed(500);
        gui.set_mp_slot(0);
        gui.set_mp_assign(0);
        // Entering the planner starts from a clean view of the selected
        // mission rather than inheriting the campaign map's pan and zoom.
        gui.set_camp_zoom(0);
        gui.set_camp_pan(0, 0);
        gui.set_camp_flags(true);
        // The shipped screen marks only `help` open; the planner's own
        // startup raises its panels. The data's left-hand windows overlap
        // (the role/aircraft and assignment panels occupy the same rows as
        // the strike list), so the default page is the non-overlapping core:
        // option tabs, map and map bar, strike list, Fly Mission.
        for (const char* w : {"campaign", "mp_options", "mp_map",
                              "mp_map_options", "mp_strike_list",
                              "mp_add_mission"}) {
            gui.open_window(w);
        }
    });
    system.register_routine("mpSetWindows",
                            [](GuiSystem& gui, const std::string&,
                               const std::string&) {
        // "Strike Type" is the only option tab the data wires (esg 1). Show
        // its panel set and hide the mutually positioned siblings.
        gui.set_mp_windows(true);
        for (const char* w : {"mp_strike_list", "mp_add_mission"}) {
            gui.open_window(w);
        }
        for (const char* w : {"mp_strike_info", "mp_role_type", "mp_ac_types",
                              "mp_base_set", "mp_base_flights", "mp_target_list",
                              "mp_assign_targ", "mp_wp_set", "mp_wp_heights",
                              "mp_atc_set", "mp_atc_list"}) {
            gui.close_window(w);
        }
    });
    system.register_routine("mpToggleIcons",
                            [](GuiSystem& gui, const std::string&,
                               const std::string&) {
        gui.set_mp_icons(!gui.mp_icons());
    });
    system.register_routine("mpStrikeListSelected",
                            [](GuiSystem& gui, const std::string& window,
                               const std::string& icon) {
        gui.select(window, icon, true);
        gui.set_mp_strike(icon);
    });
    system.register_routine("mpTargetListSelected",
                            [](GuiSystem& gui, const std::string& window,
                               const std::string& icon) {
        gui.select(window, icon, true);
        gui.set_mp_target(icon);
    });
    system.register_routine("mpSetRole",
                            [](GuiSystem& gui, const std::string& window,
                               const std::string& icon) {
        gui.select(window, icon, true);
        gui.set_mp_role(icon);
    });
    system.register_routine("mpSetACType",
                            [](GuiSystem& gui, const std::string& window,
                               const std::string& icon) {
        gui.select(window, icon, true);
        gui.set_mp_actype(icon);
    });
    for (const char* name : {"mpType1Click", "mpType2Click",
                             "mpType3Click", "mpType4Click"}) {
        system.register_routine(name, [](GuiSystem& gui, const std::string& window,
                                         const std::string& icon) {
            gui.select(window, icon, true);
            gui.set_mp_actype(icon);
        });
    }
    for (const char* name : {"mpMission1Click", "mpMission2Click",
                             "mpMission3Click", "mpMission4Click"}) {
        system.register_routine(name, [](GuiSystem& gui, const std::string& window,
                                         const std::string& icon) {
            // Icon names are m1_0..m4_0 (and follow-ups), so the first
            // character after 'm' is the slot.
            gui.select(window, icon, true);
            if (icon.size() > 1 && icon[0] == 'm' && icon[1] >= '1' && icon[1] <= '4')
                gui.set_mp_slot(icon[1] - '1');
        });
    }
    system.register_routine("mpPrevAssign",
                            [](GuiSystem& gui, const std::string&,
                               const std::string&) {
        gui.set_mp_assign(std::max(gui.mp_assign() - 1, 0));
    });
    system.register_routine("mpNextAssign",
                            [](GuiSystem& gui, const std::string&,
                               const std::string&) {
        gui.set_mp_assign(gui.mp_assign() + 1);
    });
    system.register_routine("mpMakeAssign",
                            [](GuiSystem& gui, const std::string&,
                               const std::string&) {
        gui.set_mp_target(gui.mp_strike());
    });
    system.register_routine("mpViewAssign",
                            [](GuiSystem& gui, const std::string&,
                               const std::string&) {
        gui.open_window("mp_target_view");
    });
    system.register_routine("mpClearWPRoute",
                            [](GuiSystem& gui, const std::string&,
                               const std::string&) {
        gui.set_mp_target(std::string());
        gui.set_mp_target_area(-1, -1);
        gui.clear_mp_waypoints();
    });
    system.register_routine("mpIncSpeed",
                            [](GuiSystem& gui, const std::string&,
                               const std::string&) {
        gui.set_mp_speed(std::min(gui.mp_speed() + 50, 1200));
    });
    system.register_routine("mpDecSpeed",
                            [](GuiSystem& gui, const std::string&,
                               const std::string&) {
        gui.set_mp_speed(std::max(gui.mp_speed() - 50, 100));
    });
    system.register_routine("mpWPZoomToggle",
                            [](GuiSystem& gui, const std::string&,
                               const std::string&) {
        gui.set_mp_wp_zoom(!gui.mp_wp_zoom());
    });
    system.register_routine("mpClickATC",
                            [](GuiSystem& gui, const std::string& window,
                               const std::string& icon) {
        gui.select(window, icon, true);
        gui.set_mp_atc(icon);
    });
    system.register_routine("mpFlyMission",
                            [](GuiSystem& gui, const std::string&,
                               const std::string&) {
        gui.set_mp_fly(true);
    });
    system.register_routine("mpClickCampaign",
                            [](GuiSystem& gui, const std::string&,
                               const std::string&) {
        // The bar's menu is the "Scrap Mission?" confirmation; the data puts
        // the two answers in the `sure` window.
        gui.open_window("sure");
    });
    system.register_routine("mpReturnToCampaign",
                            [](GuiSystem& gui, const std::string&,
                               const std::string&) {
        gui.close_window("sure");
        gui.request_screen("camp");
    });
    system.register_routine("mpNoQuit",
                            [](GuiSystem& gui, const std::string&,
                               const std::string&) {
        gui.close_window("sure");
    });
    system.register_routine("mpWarningOkClick",
                            [](GuiSystem& gui, const std::string&,
                               const std::string&) {
        gui.close_window("mp_warning");
    });

    // Strategic map clicks -------------------------------------------------
    // The window binds `mpMapSelect` (or `mpClickMap` in the other shipped
    // variant) to the client area; the campaign map binds `CampMoveWP` on
    // release. The driver converts the screen point to world coordinates and
    // leaves it in the model, so these handlers only resolve it against data.
    const auto unquote = [](std::string s) {
        if (!s.empty() && s.front() == '"') s.erase(s.begin());
        if (!s.empty() && s.back() == '"') s.pop_back();
        return s;
    };
    const auto dist2 = [](const Vec3& a, int x, int y) {
        const long long dx = a.x - x, dy = a.y - y;
        return dx * dx + dy * dy;
    };

    const auto planner_map_click = [dataset, unquote, dist2](GuiSystem& gui) {
        if (dataset == nullptr || !gui.map_click_valid()) return;
        const Dataset& ds = *dataset;
        if (ds.missions.empty()) return;
        const int cx = gui.map_click_x(), cy = gui.map_click_y();
        const long long pick = std::max(1, gui.map_click_radius());
        const long long pick2 = pick * pick;
        const int mi = std::clamp(gui.camp_mission(), 0,
                                  static_cast<int>(ds.missions.size()) - 1);
        const Mission& m = ds.missions[mi];

        // A target area of the selected mission: the map draws its ring, so a
        // click inside the pick radius selects it and reflects it in the
        // strike list.
        int best_t = -1, best_a = -1;
        long long best_d = LLONG_MAX;
        for (int t = 0; t < static_cast<int>(m.targets.size()); ++t) {
            const auto& areas = m.targets[t].areas;
            for (int a = 0; a < static_cast<int>(areas.size()); ++a) {
                if (!areas[a].pos) continue;
                const long long d = dist2(*areas[a].pos, cx, cy);
                if (d < best_d) { best_d = d; best_t = t; best_a = a; }
            }
        }
        if (best_t >= 0 && best_d <= pick2) {
            const auto& area = m.targets[best_t].areas[best_a];
            gui.set_mp_target(unquote(area.name));
            gui.set_mp_target_area(best_t, best_a);
            static const std::pair<const char*, const char*> kAreaIcons[] = {
                {"EWR", "ewr_site"},       {"CHOKE", "choke_point"},
                {"POLITICAL", "pol_indust_site"}, {"POL_", "pol_site"},
                {"AIRFIELD", "airfield"},  {"PORT", "port"},
                {"DAM", "dam"},
            };
            for (const auto& e : kAreaIcons) {
                if (area.type.find(e.first) != std::string::npos) {
                    gui.select("mp_strike_list", e.second, true);
                    break;
                }
            }
            return;
        }

        // A takeoff base: BASE records carry a position in MISSION.CFG.
        int best_b = -1;
        best_d = LLONG_MAX;
        for (int b = 0; b < static_cast<int>(ds.bases.size()); ++b) {
            if (!ds.bases[b].pos) continue;
            const long long d = dist2(*ds.bases[b].pos, cx, cy);
            if (d < best_d) { best_d = d; best_b = b; }
        }
        if (best_b >= 0 && best_d <= pick2 * 4) {
            gui.set_mp_base(unquote(ds.bases[best_b].name));
            return;
        }

        // Empty map: append a waypoint to the strike route.
        gui.add_mp_waypoint(cx, cy);
    };

    const auto campaign_map_click = [dataset, dist2](GuiSystem& gui) {
        if (dataset == nullptr || !gui.map_click_valid()) return;
        const Dataset& ds = *dataset;
        const int cx = gui.map_click_x(), cy = gui.map_click_y();
        const long long pick = std::max(1, gui.map_click_radius());
        const long long reach = pick * pick * 9;  // mission blobs are larger
        int best = -1;
        long long best_d = LLONG_MAX;
        for (int i = 0; i < static_cast<int>(ds.missions.size()); ++i) {
            const Mission& m = ds.missions[i];
            if (m.pos) {
                const long long d = dist2(*m.pos, cx, cy);
                if (d < best_d) { best_d = d; best = i; }
            }
            for (const auto& t : m.targets) {
                for (const auto& a : t.areas) {
                    if (!a.pos) continue;
                    const long long d = dist2(*a.pos, cx, cy);
                    if (d < best_d) { best_d = d; best = i; }
                }
            }
        }
        if (best >= 0 && best_d <= reach) {
            select_campaign_mission(gui, ds, best);
        }
    };

    for (const char* name : {"mpMapSelect", "mpClickMap"}) {
        system.register_routine(
            name, [planner_map_click](GuiSystem& gui, const std::string&,
                                      const std::string&) {
                planner_map_click(gui);
            });
    }
    for (const char* name : {"gcClickMap", "CampMoveWP"}) {
        system.register_routine(
            name, [campaign_map_click](GuiSystem& gui, const std::string&,
                                       const std::string&) {
                campaign_map_click(gui);
            });
    }
    // The waypoint-height strip is a drag surface drawn by the renderer.
    system.register_routine("mpClickWPHeight",
                            [](GuiSystem&, const std::string&,
                               const std::string&) {});
    // Leaving the Options popup dismisses it, as the data's `call leaving`
    // asks.
    system.register_routine("onleaveOptions",
                            [](GuiSystem& gui, const std::string&,
                               const std::string&) {
        gui.close_window("pickoption");
    });
    // Per-frame redraw hooks; the map and target view are renderer-owned.
    for (const char* name : {"mpDrawFuelBar", "mpRedrawMap", "mpUpdateMap",
                             "mpRedrawTargetView", "mpRedrawWPHeights"}) {
        system.register_routine(name, [](GuiSystem&, const std::string&,
                                         const std::string&) {});
    }

    // Campaign shell -------------------------------------------------------
    // Startup: the shipped screen opens with Nato selected and the first
    // mission row current. Both exist in the data; set the model to match.
    system.register_routine("InitCampaign",
                            [dataset](GuiSystem& gui, const std::string&,
                                      const std::string&) {
        gui.set_camp_side(0);
        // The map opens showing the whole theatre. The original campaign
        // screen (captured at 640x400) draws every side's flags, so the
        // checkboxes start as an additive filter rather than preselecting the
        // home side.
        gui.set_camp_flags(true);
        gui.set_camp_zoom_mode(false);
        gui.set_camp_side_mask(7u);    // all sides
        gui.set_camp_layer(0);
        gui.set_camp_layer_mask(0x3Fu);  // all layers
        gui.set_camp_mission(0);
        if (dataset != nullptr && !dataset->missions.empty()) {
            fill_mission_list(gui, *dataset);
            const auto& rows = gui.camp_rows();
            if (!rows.empty()) {
                select_campaign_mission(gui, *dataset, rows.front());
            } else {
                gui.select("missionsel", "1", true);
            }
        } else {
            gui.select("missionsel", "1", true);
        }
    });

    // Debrief tabs and NEXT are shared with the simulator debrief screen.
    system.register_routine("gpwShowDebrief",
                            [](GuiSystem& gui, const std::string& window,
                               const std::string& icon) {
        int pilot = 0;
        if (icon == "1") pilot = 0;
        else if (icon == "2") pilot = 1;
        else if (icon == "3") pilot = 2;
        else if (icon == "4") pilot = 3;
        else return;
        gui.select(window, icon, true);
        gui.set_debrief_pilot(pilot);
    });
    system.register_routine("gpwNextFlight",
                            [](GuiSystem& gui, const std::string& window,
                               const std::string&) {
        const int next = std::min(gui.debrief_pilot() + 1, 3);
        gui.set_debrief_pilot(next);
        gui.select(window, std::to_string(next + 1), true);
    });

    // Map option toggles. They have no `select` verb, so the routine owns the
    // selection state and the renderer reads it.
    system.register_routine("mpToggleFlags",
                            [](GuiSystem& gui, const std::string& window,
                               const std::string& icon) {
        gui.set_camp_flags(!gui.camp_flags());
        gui.select(window, icon, gui.camp_flags());
    });
    system.register_routine("mpSamCoverage",
                            [](GuiSystem& gui, const std::string& window,
                               const std::string& icon) {
        gui.set_camp_sam(!gui.camp_sam());
        gui.select(window, icon, gui.camp_sam());
    });
    system.register_routine("mpEWRCoverage",
                            [](GuiSystem& gui, const std::string& window,
                               const std::string& icon) {
        gui.set_camp_ewr(!gui.camp_ewr());
        gui.select(window, icon, gui.camp_ewr());
    });
    system.register_routine("mpCycleMap",
                            [](GuiSystem& gui, const std::string& window,
                               const std::string& icon) {
        gui.set_camp_map_mode((gui.camp_map_mode() + 1) % 4);
        gui.select(window, icon, true);
    });
    // `a` toggles the map between editing and zooming in the original. The
    // label on the bar changes from EDIT to ZOOM; the renderer reads the state
    // rather than mutating the `.gui` text.
    for (const char* name : {"gcChangeMode", "mpChangeMode"}) {
        system.register_routine(name, [](GuiSystem& gui, const std::string&,
                                         const std::string&) {
            gui.set_camp_zoom_mode(!gui.camp_zoom_mode());
        });
    }
    system.register_routine("mpZoomInMap",
                            [](GuiSystem& gui, const std::string&,
                               const std::string&) {
        gui.set_camp_zoom(std::min(gui.camp_zoom() + 1, 6));
    });
    system.register_routine("mpZoomOutMap",
                            [](GuiSystem& gui, const std::string&,
                               const std::string&) {
        gui.set_camp_zoom(std::max(gui.camp_zoom() - 1, 0));
    });

    // Map modes: side (Nato/Russian/Neutral) and layer (air defence, support,
    // navy, comms, supply, political) are two independent toggle sets.
    system.register_routine("gpwMapModeSelected",
                            [dataset](GuiSystem& gui, const std::string& window,
                                      const std::string& icon) {
        static const std::pair<const char*, int> sides[] = {
            {"nato", 0}, {"russian", 1}, {"neutral", 2}};
        static const std::pair<const char*, int> layers[] = {
            {"airdef", 0}, {"airsup", 1}, {"navy", 2},
            {"comms", 3}, {"supply", 4}, {"polit", 5}};
        const bool on = !gui.is_selected(window, icon);
        for (const auto& e : sides) {
            if (icon == e.first) {
                const unsigned bit = 1u << e.second;
                gui.set_camp_side(e.second);
                gui.set_camp_side_mask(on ? (gui.camp_side_mask() | bit)
                                          : (gui.camp_side_mask() & ~bit));
                gui.select(window, icon, on);
                if (dataset != nullptr) {
                    fill_mission_list(gui, *dataset);
                    const auto& rows = gui.camp_rows();
                    if (!rows.empty())
                        select_campaign_mission(gui, *dataset, rows.front());
                }
                return;
            }
        }
        for (const auto& e : layers) {
            if (icon == e.first) {
                const unsigned bit = 1u << e.second;
                gui.set_camp_layer(e.second);
                gui.set_camp_layer_mask(on ? (gui.camp_layer_mask() | bit)
                                           : (gui.camp_layer_mask() & ~bit));
                gui.select(window, icon, on);
                return;
            }
        }
    });

    // Mission list in the campaign screen; entries are numbered 1..N.
    system.register_routine("gpwCampMissionSelected",
                            [dataset](GuiSystem& gui, const std::string& window,
                                      const std::string& icon) {
        const int n = std::atoi(icon.c_str());
        if (n < 1) return;
        gui.select(window, icon, true);
        if (dataset == nullptr) {
            gui.set_camp_mission(n - 1);
            return;
        }
        const auto& rows = gui.camp_rows();
        if (n <= static_cast<int>(rows.size()))
            select_campaign_mission(gui, *dataset, rows[n - 1]);
        else
            select_campaign_mission(gui, *dataset, n - 1);
    });

    // Time advance: each click of "View 8hr." moves the campaign clock 8h.
    system.register_routine("gpwClickTimeAdjust",
                            [](GuiSystem& gui, const std::string&,
                               const std::string&) {
        gui.set_camp_time_shift(gui.camp_time_shift() + 8);
    });
    system.register_routine("mpClickV8Okay",
                            [](GuiSystem& gui, const std::string&,
                               const std::string&) {
        gui.set_camp_time_shift(gui.camp_time_shift() + 8);
        gui.close_window("mpView8ToMP");
    });

    // Target view: the panes are opened/closed by the picker's verbs; the
    // routine records the mode and the visibility.
    system.register_routine("gpwOnOpenTargetWindow",
                            [](GuiSystem& gui, const std::string&,
                               const std::string&) {
        gui.set_camp_target_view(true);
        gui.open_window("targetwin");
        gui.open_window("targetzoom");
    });
    system.register_routine("gpwCloseTargetView",
                            [](GuiSystem& gui, const std::string&,
                               const std::string&) {
        gui.set_camp_target_view(false);
        gui.close_window("targetwin");
        gui.close_window("targetzoom");
    });
    system.register_routine("gpwZoomTargetView",
                            [](GuiSystem& gui, const std::string& window,
                               const std::string& icon) {
        int mode = 0;
        if (icon == "area") mode = 0;
        else if (icon == "zoom") mode = 1;
        else if (icon == "targview") mode = 2;
        else return;
        gui.select(window, icon, true);
        gui.set_camp_map_mode(mode);
    });

    // 8-hour view test switches and its warning dialog.
    system.register_routine("gpwTestClicked",
                            [](GuiSystem& gui, const std::string& window,
                               const std::string& icon) {
        gui.select(window, icon, true);
        gui.close_window("test");
    });
    system.register_routine("cgWarningOkClick",
                            [](GuiSystem& gui, const std::string&,
                               const std::string&) {
        gui.close_window("cg_warning");
    });

    // Accept/ready: the campaign mission is chosen and the flight is launched.
    system.register_routine("gpwCampAccepted",
                            [](GuiSystem& gui, const std::string&,
                               const std::string&) {
        gui.set_camp_ready(true);
        gui.close_window("missionbrief");
        gui.close_window("missionoverview");
    });
    system.register_routine("gnReadyToFly",
                            [](GuiSystem& gui, const std::string&,
                               const std::string&) {
        gui.set_camp_ready(true);
        gui.close_window("generatecamp");
    });

    system.register_routine("gpwExitCampaign",
                            [](GuiSystem& gui, const std::string&,
                               const std::string&) {
        gui.request_screen("mainmenu");
    });
    system.register_routine("mpMissPlanTask",
                            [](GuiSystem& gui, const std::string&,
                               const std::string&) {
        gui.request_screen("missplan");
    });

    // Per-frame redraw hooks. The map is drawn by the renderer from the state
    // above, so these only exist to keep the routine set complete.
    for (const char* name : {"gpwUpdateMap", "gpwRedrawMap",
                             "gpwUpdateTargetWindow", "gpwRedrawTargetWindow",
                             "gpwUpdateTime"}) {
        system.register_routine(name, [](GuiSystem&, const std::string&,
                                         const std::string&) {});
    }

    // Debrief: the pilot tabs share `esg 1`; selecting one shows that pilot's
    // results. The results themselves are mission data and not modelled.
    system.register_routine("simShowDebrief",
                            [](GuiSystem& gui, const std::string& window,
                               const std::string& icon) {
        int pilot = 0;
        if (icon == "1") pilot = 0;
        else if (icon == "2") pilot = 1;
        else if (icon == "3") pilot = 2;
        else if (icon == "4") pilot = 3;
        else return;
        gui.select(window, icon, true);
        gui.set_debrief_pilot(pilot);
    });
    system.register_routine("InitSim2",
                            [](GuiSystem&, const std::string&,
                               const std::string&) {});

    // Custom mission editor: the enemy list is one `esg 4` group of 34
    // entries, all sharing `ChooseEnemy`.
    system.register_routine("ChooseEnemy",
                            [](GuiSystem& gui, const std::string& window,
                               const std::string& icon) {
        int index = -1;
        if (icon == "first") index = 0;
        else if (!icon.empty()) {
            const int n = std::atoi(icon.c_str());
            if (n >= 1 && n <= 34) index = n - 1;
        }
        if (index < 0) return;
        gui.select(window, icon, true);
        gui.set_custom_enemy_type(index);
    });

    // The enemy preview is a 3D view; nothing to redraw in the GUI model.
    system.register_routine("RedrawEnemyView",
                            [](GuiSystem&, const std::string&,
                               const std::string&) {});

    // `InitCustom` is the screen startup. The shipped screen opens with the
    // first entry of each group selected; those come from the data's
    // `select reset` markers, which the loader already records as actions.
    system.register_routine("InitCustom",
                            [](GuiSystem&, const std::string&,
                               const std::string&) {});

    // Accept reads the selections and records the custom setup. Starting the
    // mission belongs to the mission loader, which is not reconstructed.
    system.register_routine("AcceptCustom",
                            [](GuiSystem& gui, const std::string&,
                               const std::string&) {
        const auto on_custom = [&](const char* icon) {
            return gui.is_selected("custom", icon);
        };
        int friendlies = 1, enemies = 1;
        if (on_custom("first")) friendlies = 1;
        else if (on_custom("2")) friendlies = 2;
        else if (on_custom("3")) friendlies = 3;
        else if (on_custom("4")) friendlies = 4;
        else if (on_custom("5")) friendlies = 5;
        else if (on_custom("6")) friendlies = 6;
        else if (on_custom("7")) friendlies = 7;
        else if (on_custom("8")) friendlies = 8;
        if (on_custom("efirst")) enemies = 1;
        else if (on_custom("e2")) enemies = 2;
        else if (on_custom("e3")) enemies = 3;
        else if (on_custom("e4")) enemies = 4;
        else if (on_custom("e5")) enemies = 5;
        else if (on_custom("e6")) enemies = 6;
        else if (on_custom("e7")) enemies = 7;
        else if (on_custom("e8")) enemies = 8;
        else if (on_custom("e9")) enemies = 9;
        else if (on_custom("e10")) enemies = 10;
        else if (on_custom("e11")) enemies = 11;
        else if (on_custom("e12")) enemies = 12;
        gui.set_custom_counts(friendlies, enemies);
        gui.set_custom_guns(on_custom("gunsyes"));
        gui.set_custom_accepted(true);
    });

    // Simulator: picking a mission type records which one; the icon's own
    // `screen simul` verb does the navigation.
    system.register_routine("gpwSimTypeSelected",
                            [](GuiSystem& gui, const std::string& window,
                               const std::string& icon) {
        int type = 0;
        if (icon == "first") type = 0;
        else if (icon.size() == 1 && icon[0] >= '2' && icon[0] <= '6')
            type = icon[0] - '1';
        else return;
        gui.select(window, icon, true);
        gui.set_sim_type(type);
    });
}

int fill_hiscores(GuiSystem& system, const std::filesystem::path& data_dir) {
    auto* window = system.find_window("hiscores");
    if (window == nullptr || !window->visible) return 0;

    std::ifstream file(data_dir / "HISCORES.DAT", std::ios::binary);
    if (!file) return 0;
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)),
                                    std::istreambuf_iterator<char>());
    if (bytes.size() < 4 + 10 * 25) return 0;

    // Level indices follow the Difficulty panel's order: Rookie, Pro, Topgun.
    static const char* kLevels[] = {"Rookie", "Pro", "Topgun"};
    int filled = 0;
    for (int row = 1; row <= 10; ++row) {
        const std::size_t off = 4 + static_cast<std::size_t>(row - 1) * 25;
        std::string name(reinterpret_cast<const char*>(bytes.data() + off), 16);
        while (!name.empty() && (name.back() == '\0' || name.back() == ' ')) name.pop_back();
        // 16-byte name, four pad bytes, level, then the score as u32.
        const std::uint8_t level = bytes[off + 20];
        const std::uint32_t score = static_cast<std::uint32_t>(bytes[off + 21]) |
                                    (static_cast<std::uint32_t>(bytes[off + 22]) << 8) |
                                    (static_cast<std::uint32_t>(bytes[off + 23]) << 16) |
                                    (static_cast<std::uint32_t>(bytes[off + 24]) << 24);

        const std::string suffix = std::to_string(row);
        if (auto* w = system.find_widget("hiscores", "name" + suffix); w != nullptr) {
            w->text["en"] = name;
            w->value = name;
        }
        if (auto* w = system.find_widget("hiscores", "level" + suffix); w != nullptr) {
            w->text["en"] = level >= 1 && level <= 3 ? kLevels[level - 1] : "";
            w->value = w->text["en"];
        }
        if (auto* w = system.find_widget("hiscores", "score" + suffix); w != nullptr) {
            w->text["en"] = std::to_string(score);
            w->value = w->text["en"];
        }
        ++filled;
    }
    return filled;
}

void fill_credits(GuiSystem& system, const DidArchive& archive) {
    auto* window = system.find_window("credits");
    if (window == nullptr || !window->visible || !window->lines.empty()) return;
    auto entry = archive.resolve("gfxcred", "supergui\\", ".txt");
    if (!entry) return;
    auto bytes = archive.read(*entry);
    if (!bytes) return;

    const std::string text(bytes->begin(), bytes->end());
    std::size_t pos = 0;
    while (pos <= text.size() && window->lines.size() < 32) {
        auto eol = text.find('\n', pos);
        if (eol == std::string::npos) eol = text.size();
        std::string line = text.substr(pos, eol - pos);
        while (!line.empty() && line.back() == '\r') line.pop_back();
        // The file separates name and role with tabs; expand for the renderer.
        std::string expanded;
        for (char ch : line) {
            if (ch == '\t') expanded += "    ";
            else expanded += ch;
        }
        window->lines.push_back(expanded);
        pos = eol + 1;
    }
}

std::string gui_key_name(const SDL_Keysym& key) {
    if (key.sym >= SDLK_F1 && key.sym <= SDLK_F12) {
        return "f" + std::to_string(key.sym - SDLK_F1 + 1);
    }
    switch (key.sym) {
        case SDLK_ESCAPE: return "esc";
        case SDLK_RETURN:
        case SDLK_KP_ENTER: return "ret";
        case SDLK_SPACE: return "space";
        case SDLK_TAB: return "tab";
        case SDLK_BACKSPACE: return "backspace";
        case SDLK_UP: return "up";
        case SDLK_DOWN: return "down";
        case SDLK_LEFT: return "left";
        case SDLK_RIGHT: return "right";
        default: break;
    }
    if (key.sym >= 32 && key.sym < 127) {
        char c = static_cast<char>(key.sym);
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        return std::string(1, c);
    }
    return {};
}

std::optional<Palette> load_gui_palette(const DidArchive& archive, const std::string& name) {
    auto entry = archive.resolve(name, "colours\\", ".col");
    if (!entry) return std::nullopt;
    auto bytes = archive.read(*entry);
    if (!bytes || bytes->size() != 768) return std::nullopt;

    std::uint8_t peak = 0;
    for (auto v : *bytes) peak = std::max(peak, v);
    const int scale = peak <= 63 ? 255 : 63;

    Palette out{};
    for (std::size_t i = 0; i < 256; ++i) {
        const auto r = static_cast<std::uint32_t>(((*bytes)[i * 3 + 0] * scale) / 63);
        const auto g = static_cast<std::uint32_t>(((*bytes)[i * 3 + 1] * scale) / 63);
        const auto b = static_cast<std::uint32_t>(((*bytes)[i * 3 + 2] * scale) / 63);
        out[i] = (r << 16) | (g << 8) | b;
    }
    return out;
}

}  // namespace ef
