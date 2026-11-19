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

#include <filesystem>
#include <functional>
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

    //! `key` / `skey` activators, lowercased (e.g. "esc", "ret", "c", "f1").
    std::string key;
    std::string skey;

    std::string textpos = "left";
    std::map<std::string, std::string> text;  //!< "en"/"fr"/"de" -> string

    int esg = -1;  //!< exclusive selection group
    bool disable = false;
    bool hidden = false;

    //! `editable N` declares a text field that accepts N characters. -1 is a
    //! plain label.
    int editable_len = -1;
    //! `editableoff`: the field exists but does not start in edit mode.
    bool editable_off = false;
    //! Current contents of an editable field (or the initial text).
    std::string value;

    //! `toggle click`: activation flips the selected state instead of setting
    //! it. Used by the checkbox-style widgets in `camp.gui`.
    bool toggle = false;

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

    //! Scrollable client area (`xscroll` / `yscroll`) and its current offset.
    bool scroll_x = false, scroll_y = false;
    int scroll_x_pos = 0, scroll_y_pos = 0;

    //! `infrontof <window>` places this window ahead of the named one;
    //! `jumptofront` raises it on open.
    std::string infront_of;
    bool jump_to_front = false;

    //! Routine references that run when the window closes or per frame.
    std::string close_down, main_loop;

    std::vector<GuiWidget> icons;

    //! Plain text block, one entry per line. Used by windows whose content is
    //! a resource rather than widgets (the credits panel).
    std::vector<std::string> lines;

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

    //! Client rectangle of a window: the area icons are laid out in.
    static SDL_Rect client_rect(const GuiWindow& window);

    //! Scrolls a window, clamped to its content. Returns true if it moved.
    bool scroll_window(const std::string& name, int dx, int dy);

    //! Dispatches a key to the first visible widget bound to it.
    //! Accepts `esc`, `ret`, `space`, `f1`.., or a single character.
    bool key_press(const std::string& key);

    //! Focused editable field, if any.
    GuiWidget* focused_field();
    void focus_field(const std::string& window, const std::string& icon);
    bool type_text(const std::string& utf8);  //!< appends to the focused field
    bool backspace();

    //! Routines: the C half of the GUI. Registration lets the driver supply
    //! behaviour for names the `.gui` files reference but cannot define.
    //! The handler is told which window and icon fired it, because several
    //! routines are shared by a whole group (a radio set, a mission grid).
    void register_routine(const std::string& name,
                          std::function<void(GuiSystem&, const std::string& window,
                                            const std::string& icon)> handler);

    //! Set by the Quick Combat Accept routine. The reconstruction has no
    //! mission loader yet, so this records the decision instead.
    void set_arcade_accepted(bool on) { arcade_accepted_ = on; }
    bool arcade_accepted() const { return arcade_accepted_; }
    //! Routines referenced by the current screen but not registered.
    std::vector<std::string> missing_routines() const;
    //! Every routine name the current screen references, sorted.
    std::vector<std::string> referenced_routines() const;

    //! Runs the screen's `startup` routine if one is registered. Called by the
    //! driver after loading a screen.
    bool run_startup();

    void request_quit() { quit_requested_ = true; }
    bool quit_requested() const { return quit_requested_; }

    const std::vector<std::string>& log() const { return log_; }

private:
    GuiScreen screen_;
    int next_z_ = 1;
    std::map<std::pair<std::string, std::string>, bool> selected_;
    bool arcade_accepted_ = false;
    std::vector<std::string> screen_requests_;
    std::vector<std::string> log_;

    std::pair<std::string, std::string> focus_;  //!< window, icon
    std::map<std::string,
             std::function<void(GuiSystem&, const std::string&, const std::string&)>>
        routines_;
    std::vector<std::string> unimplemented_;
    bool quit_requested_ = false;
};

//! Registers the routines whose behaviour has been established by running the
//! original software and observing it. Deliberately small: everything else is
//! reported as missing rather than guessed.
void register_observed_routines(GuiSystem& system);

//! Fills the `hiscores` window from the loose `HISCORES.DAT` file: a 4-byte
//! header then ten 25-byte records of name[16], three pad bytes, level u8 and
//! score u32 (little-endian). Returns the number of rows filled.
int fill_hiscores(GuiSystem& system, const std::filesystem::path& data_dir);

//! The credits panel is an empty window in `mainmenu.gui`; its text is the
//! `supergui\gfxcred.txt` resource. Fills `lines` once, when visible.
void fill_credits(GuiSystem& system, const DidArchive& archive);

//! Maps a key event to the name the `.gui` files use ("esc", "ret", "f1", "c").
std::string gui_key_name(const SDL_Keysym& key);

//! `colours\<name>.col` as 0x00RRGGBB.
std::optional<Palette> load_gui_palette(const DidArchive& archive,
                                        const std::string& name = "gui");

}  // namespace ef
