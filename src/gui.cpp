// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 velteyn

#include "gui.h"

#include <algorithm>
#include <cctype>
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
    if (target->esg >= 0) {
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
    std::vector<GuiAction> ran;
    auto* widget = find_widget(window, icon);
    if (widget == nullptr) return ran;

    // Copy: handlers may mutate the widget list through open/close.
    const auto actions = widget->actions;
    for (const auto& a : actions) {
        if (a.event != event) continue;
        if (a.verb == "select") {
            const bool flip = widget->toggle && is_selected(window, icon);
            select(window, icon, !flip);
            continue;
        }
        if (a.verb == "unselect") { select(window, icon, false); continue; }
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
        log_.push_back(window + "." + icon + ":" + event + " -> " + a.verb +
                       (a.value.empty() ? "" : " " + a.value));
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
    int content_w = 0, content_h = 0;
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
void register_observed_routines(GuiSystem& system) {
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
    // window. Campaign generation belongs to the campaign module, which is not
    // reconstructed, so the decision is recorded and the window stays up.
    system.register_routine("gpwGenerateCampaign",
                            [](GuiSystem& gui, const std::string&,
                               const std::string&) {
        gui.set_campaign_started(true);
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
