// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 velteyn

#pragma once

// Reconstructed EF2000 GUI runtime.
//
// The front-end is data: `gui\<screen>.gui` is a declarative text DSL with
// `window` and `icon` blocks, colour indices, localised strings and script
// hooks.  There is no widget bitmap atlas -- panels are drawn from palette
// indices (`box`/`bkd`, and `boxs`/`bkds` for the selected state, where index
// 0 means hollow), which is why the menu entries read as checkbox-like panels.
//
// Screen composition: `include <name>` merges another screen's windows, later
// declarations replacing earlier ones of the same name.  `screen`/`task <name>`
// switches screens; `openwindow`/`closewindow` change window visibility.
//
// The runtime keeps the *data* faithful and leaves the drawing style in
// guirender.cpp, which approximates the DOS frames because the original
// widget drawing code has not been reversed.

#include "did.h"
#include "texture.h"

#include <SDL2/SDL.h>

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace ef {

//! One verb attached to one event, in file order.
struct GuiAction {
    std::string event;  //!< "click", "release", "entering", "reset", "drag", ...
    std::string verb;   //!< "routine", "screen", "openwindow", "closewindow", ...
    std::string value;
};

struct GuiWidget {
    std::string name;

    //! Window-relative geometry.
    int x = 0, y = 0, w = 0, h = 0;

    //! Colour indices into `colours\gui.col`; -1 means unset, 0 means hollow.
    int box = -1, bkd = -1, boxs = -1, bkds = -1;
    int line = -1, txtclr = -1, txtclrs = -1, textlen = -1;

    std::string textpos = "left";
    std::map<std::string, std::string> text;  //!< "en"/"fr"/"de" -> string

    int esg = -1;  //!< exclusive selection group
    bool disable = false;
    bool editable = false;
    bool hidden = false;

    std::vector<GuiAction> actions;

    std::string label(const std::string& lang = "en") const;
    bool contains(int px, int py) const {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
    //! First verb for `event`, if the widget handles it.
    std::optional<GuiAction> action(const std::string& event) const;
};

struct GuiWindow {
    std::string name;

    int x = 0, y = 0, w = 0, h = 0;
    int work_w = 0, work_h = 0;

    int box = -1, bkd = -1, background_colour = -1;
    std::map<std::string, std::string> title;

    bool open = false;        //!< visible when the screen loads
    bool moveable = false;
    bool close = false;
    bool is3d = false;
    bool fullsize = false;
    bool nojump = false;

    std::string on_open, on_close, redraw, update;

    std::vector<GuiWidget> icons;

    //! Runtime state: z-order and hover/selection are kept by GuiSystem.
    bool visible = false;
    int z = 0;

    std::string title_text(const std::string& lang = "en") const {
        auto it = title.find(lang);
        return it == title.end() ? std::string() : it->second;
    }
};

struct GuiScreen {
    std::string name;
    std::string startup;
    std::string picture;
    std::string palette;      //!< `pal NAME`
    std::string include_of;   //!< chain for diagnostics
    std::vector<std::string> includes;
    std::vector<GuiWindow> windows;
    std::vector<std::string> order;  //!< window names in declaration order
};

//! Parses `gui\<name>.gui` from an archive, following `include`.
class GuiLoader {
public:
    explicit GuiLoader(const DidArchive& archive) : archive_(archive) {}

    std::optional<GuiScreen> load(const std::string& name,
                                  std::string* error = nullptr) const;

    //! Parses text directly (no includes followed).
    static GuiScreen parse(const std::string& name, const std::string& text);

private:
    const DidArchive& archive_;
};

//! Runtime state machine: active screen, visible window stack, selection.
class GuiSystem {
public:
    void set_screen(GuiScreen screen);
    GuiScreen& screen() { return screen_; }
    const GuiScreen& screen() const { return screen_; }

    GuiWindow* find_window(const std::string& name);
    GuiWidget* find_widget(const std::string& window, const std::string& icon);

    //! Window under a screen point, honouring z-order.
    GuiWindow* window_at(int px, int py);

    void open_window(const std::string& name);
    void close_window(const std::string& name);
    void bring_to_front(const std::string& name);

    //! Applies `open`/`openwindow` at load time.
    void apply_initial_visibility();

    //! Marks `widget` selected inside its `esg` group.
    void select(const std::string& window, const std::string& icon, bool on);

    bool is_selected(const std::string& window, const std::string& icon) const;

    //! Records a fired event for `--trace`; returns the verbs it ran.
    std::vector<GuiAction> fire(const std::string& window, const std::string& icon,
                                const std::string& event);

    //! Screens queued by `screen`/`task` verbs; consumed by the driver.
    std::vector<std::string> take_screen_requests();

    const std::vector<std::string>& log() const { return log_; }

private:
    GuiScreen screen_;
    int next_z_ = 1;
    std::map<std::pair<std::string, std::string>, bool> selected_;
    std::vector<std::string> screen_requests_;
    std::vector<std::string> log_;
};

//! `colours\<name>.col` as 0x00RRGGBB.
std::optional<Palette> load_gui_palette(const DidArchive& archive,
                                        const std::string& name = "gui");

}  // namespace ef
