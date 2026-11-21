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
#include <functional>
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

    //! Window-level verbs. The map windows bind a routine to the client area
    //! (`call Pressed` + `routine mpClickMap`), so clicks that miss every icon
    //! still reach the handler.
    std::vector<GuiAction> actions;

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
    //! Runs a window hook by routine name, guarding against self-recursion.
    void run_window_hook(const std::string& window, const std::string& name);

    bool is_selected(const std::string& window, const std::string& icon) const;

    //! Records a fired event for `--trace`; returns the verbs it ran.
    std::vector<GuiAction> fire(const std::string& window, const std::string& icon,
                                const std::string& event);

    //! Runs the window's own verbs (map clicks and other client-area events).
    std::vector<GuiAction> fire_window(const std::string& window,
                                       const std::string& event);

    //! Screens queued by `screen`/`task` verbs; consumed by the driver.
    std::vector<std::string> take_screen_requests();

    //! Client rectangle of a window: the area icons are laid out in.
    static SDL_Rect client_rect(const GuiWindow& window);

    //! Scrolls a window, clamped to its content. Returns true if it moved.
    bool scroll_window(const std::string& name, int dx, int dy);
    //! Sets the scroll position of a window, clamped to its content.
    void scroll_window_to(const std::string& name, int x, int y);

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

    //! Replaces a window's text-resource lines (the `summary` panel's
    //! briefing, the credits panel). Empty lines are kept as spacing.
    void set_window_lines(const std::string& window, std::vector<std::string> lines);

    //! Set by the Quick Combat Accept routine. The reconstruction has no
    //! mission loader yet, so this records the decision instead.
    void set_arcade_accepted(bool on) { arcade_accepted_ = on; }
    bool arcade_accepted() const { return arcade_accepted_; }

    //! Campaign Level Setting dialog state. `difficulty` is the row index
    //! 0..9 (0 = Rookie, 9 = TopGun); the shipped dialog opens with Pro
    //! selected, which is 6. `influence` is the Yes/No choice.
    void set_campaign_difficulty(int level) { campaign_difficulty_ = level; }
    int campaign_difficulty() const { return campaign_difficulty_; }
    void set_campaign_influence(bool on) { campaign_influence_ = on; }
    bool campaign_influence() const { return campaign_influence_; }
    //! Set when Accept starts generation (the generator itself is not
    //! reconstructed; the runtime shows the `generatecamp` window).
    void set_campaign_started(bool on) { campaign_started_ = on; }
    bool campaign_started() const { return campaign_started_; }

    //! Simulator mission type 0..5 (Free Flight .. Advanced Training).
    void set_sim_type(int type) { sim_type_ = type; }
    int sim_type() const { return sim_type_; }

    //! Simulator mission browser state. The list is `esg 1`; the parameters
    //! dialog chooses time, weather, difficulty and weapon options.
    void set_sim_mission(int index) { sim_mission_ = index; }
    int sim_mission() const { return sim_mission_; }
    void set_sim_time(int v) { sim_time_ = v; }
    int sim_time() const { return sim_time_; }
    void set_sim_weather(int v) { sim_weather_ = v; }
    int sim_weather() const { return sim_weather_; }
    void set_sim_difficulty(int v) { sim_difficulty_ = v; }
    int sim_difficulty() const { return sim_difficulty_; }
    void set_sim_default_weapons(bool v) { sim_default_weapons_ = v; }
    bool sim_default_weapons() const { return sim_default_weapons_; }
    void set_sim_review_weapons(bool v) { sim_review_weapons_ = v; }
    bool sim_review_weapons() const { return sim_review_weapons_; }
    void set_mission_accepted(bool v) { mission_accepted_ = v; }
    bool mission_accepted() const { return mission_accepted_; }

    //! Debrief pilot tab (0 = LEAD, 1..3 = TWO/THREE/FOUR).
    void set_debrief_pilot(int v) { debrief_pilot_ = v; }
    int debrief_pilot() const { return debrief_pilot_; }

    //! Custom mission editor state (the `custom` screen). Counts, gun-only,
    //! location, range, time, weather and difficulty are `esg` selections;
    //! `enemy_type` comes from the `enemysel` list.
    void set_custom_enemy_type(int v) { custom_enemy_type_ = v; }
    int custom_enemy_type() const { return custom_enemy_type_; }
    void set_custom_accepted(bool v) { custom_accepted_ = v; }
    bool custom_accepted() const { return custom_accepted_; }
    void set_custom_guns(bool v) { custom_guns_ = v; }
    bool custom_guns() const { return custom_guns_; }
    void set_custom_counts(int friendlies, int enemies) {
        custom_friendlies_ = friendlies;
        custom_enemies_ = enemies;
    }
    int custom_friendlies() const { return custom_friendlies_; }
    int custom_enemies() const { return custom_enemies_; }

    //! Campaign shell state. The map is drawn by the renderer; these are the
    //! choices it needs: side and layer filters, coverage overlays, zoom,
    //! selected mission, time shift, target view and the ready flag.
    void set_camp_side(int v) { camp_side_ = v; }
    int camp_side() const { return camp_side_; }
    void set_camp_layer(int v) { camp_layer_ = v; }
    int camp_layer() const { return camp_layer_; }
    void set_camp_flags(bool v) { camp_flags_ = v; }
    bool camp_flags() const { return camp_flags_; }
    void set_camp_sam(bool v) { camp_sam_ = v; }
    bool camp_sam() const { return camp_sam_; }
    void set_camp_ewr(bool v) { camp_ewr_ = v; }
    bool camp_ewr() const { return camp_ewr_; }
    void set_camp_map_mode(int v) { camp_map_mode_ = v; }
    int camp_map_mode() const { return camp_map_mode_; }
    void set_camp_zoom(int v) { camp_zoom_ = v; }
    int camp_zoom() const { return camp_zoom_; }
    void set_camp_mission(int v) { camp_mission_ = v; }
    int camp_mission() const { return camp_mission_; }
    //! Mission indices shown in the campaign mission list, in row order.
    void set_camp_rows(std::vector<int> rows) { camp_rows_ = std::move(rows); }
    const std::vector<int>& camp_rows() const { return camp_rows_; }
    void set_camp_time_shift(int v) { camp_time_shift_ = v; }
    int camp_time_shift() const { return camp_time_shift_; }
    void set_camp_target_view(bool v) { camp_target_view_ = v; }
    bool camp_target_view() const { return camp_target_view_; }
    void set_camp_ready(bool v) { camp_ready_ = v; }
    bool camp_ready() const { return camp_ready_; }

    //! Mission planner state: selected strike category and target, role and
    //! aircraft type, mission slot, assignment cursor, waypoint speed, the
    //! ATC entry and the "fly" decision. Lists are identified by their icon
    //! names, which is how the data names the entries.
    void set_mp_strike(const std::string& v) { mp_strike_ = v; }
    const std::string& mp_strike() const { return mp_strike_; }
    void set_mp_target(const std::string& v) { mp_target_ = v; }
    const std::string& mp_target() const { return mp_target_; }
    void set_mp_role(const std::string& v) { mp_role_ = v; }
    const std::string& mp_role() const { return mp_role_; }
    void set_mp_actype(const std::string& v) { mp_actype_ = v; }
    const std::string& mp_actype() const { return mp_actype_; }
    void set_mp_slot(int v) { mp_slot_ = v; }
    int mp_slot() const { return mp_slot_; }
    void set_mp_assign(int v) { mp_assign_ = v; }
    int mp_assign() const { return mp_assign_; }
    void set_mp_speed(int v) { mp_speed_ = v; }
    int mp_speed() const { return mp_speed_; }
    void set_mp_atc(const std::string& v) { mp_atc_ = v; }
    const std::string& mp_atc() const { return mp_atc_; }
    void set_mp_icons(bool v) { mp_icons_ = v; }
    bool mp_icons() const { return mp_icons_; }
    void set_mp_windows(bool v) { mp_windows_ = v; }
    bool mp_windows() const { return mp_windows_; }
    void set_mp_wp_zoom(bool v) { mp_wp_zoom_ = v; }
    bool mp_wp_zoom() const { return mp_wp_zoom_; }
    void set_mp_fly(bool v) { mp_fly_ = v; }
    bool mp_fly() const { return mp_fly_; }

    //! Map clicks. The viewer converts the screen point to world coordinates
    //! and leaves both the point and the pick radius (world units) here; the
    //! `mpClickMap`/`gcClickMap` routines read them.
    void set_map_click(int x, int y, int radius) {
        map_click_x_ = x; map_click_y_ = y; map_click_radius_ = radius;
        map_click_valid_ = true;
    }
    void clear_map_click() { map_click_valid_ = false; }
    bool map_click_valid() const { return map_click_valid_; }
    int map_click_x() const { return map_click_x_; }
    int map_click_y() const { return map_click_y_; }
    int map_click_radius() const { return map_click_radius_; }

    //! Planner edits made on the map: the takeoff base chosen by clicking,
    //! waypoints appended to the strike route, and the selected target area.
    //! They stay in the GUI model until the mission loader consumes them.
    void set_mp_base(const std::string& v) { mp_base_ = v; }
    const std::string& mp_base() const { return mp_base_; }
    void set_mp_target_area(int target, int area) {
        mp_target_index_ = target; mp_area_index_ = area;
    }
    int mp_target_index() const { return mp_target_index_; }
    int mp_area_index() const { return mp_area_index_; }
    void add_mp_waypoint(int x, int y) { mp_waypoints_.emplace_back(x, y); }
    void clear_mp_waypoints() { mp_waypoints_.clear(); }
    const std::vector<std::pair<int, int>>& mp_waypoints() const {
        return mp_waypoints_;
    }

    //! Strategic-map filters. Bits follow `MapSide`/`MapLayer` in render.h.
    void set_camp_side_mask(unsigned v) { camp_side_mask_ = v; }
    unsigned camp_side_mask() const { return camp_side_mask_; }
    void set_camp_layer_mask(unsigned v) { camp_layer_mask_ = v; }
    unsigned camp_layer_mask() const { return camp_layer_mask_; }

    //! Pan offset in world units for the embedded map.
    void set_camp_pan(int x, int y) { camp_pan_x_ = x; camp_pan_y_ = y; }
    int camp_pan_x() const { return camp_pan_x_; }
    int camp_pan_y() const { return camp_pan_y_; }

    //! Map interaction mode: edit (select/move waypoints) or zoom. The
    //! original toggles it with `a` (`gcChangeMode`/`mpChangeMode`).
    void set_camp_zoom_mode(bool v) { camp_zoom_mode_ = v; }
    bool camp_zoom_mode() const { return camp_zoom_mode_; }

    //! Weapons configuration: selected pylon/missile, view preset
    //! (0 angled, 1 front, 2 side, 3 underneath), rotation, and whether the
    //! loadout was cleared or accepted.
    void set_weap_selected(const std::string& v) { weap_selected_ = v; }
    const std::string& weap_selected() const { return weap_selected_; }
    void set_weap_view(int v) { weap_view_ = v; }
    int weap_view() const { return weap_view_; }
    void set_weap_angle(int x, int y) { weap_x_angle_ = x; weap_y_angle_ = y; }
    int weap_x_angle() const { return weap_x_angle_; }
    int weap_y_angle() const { return weap_y_angle_; }
    void set_weap_cleared_all(bool v) { weap_cleared_all_ = v; }
    bool weap_cleared_all() const { return weap_cleared_all_; }
    void set_weap_accepted(bool v) { weap_accepted_ = v; }
    bool weap_accepted() const { return weap_accepted_; }

    //! Quick Combat post-mission: continuation, hi-score entry and endgame.
    void set_arcade_continued(bool v) { arcade_continued_ = v; }
    bool arcade_continued() const { return arcade_continued_; }
    void set_arcade_hiscore(const std::string& name, bool accepted) {
        arcade_hiscore_name_ = name;
        arcade_hiscore_accepted_ = accepted;
    }
    const std::string& arcade_hiscore_name() const { return arcade_hiscore_name_; }
    bool arcade_hiscore_accepted() const { return arcade_hiscore_accepted_; }
    void set_arcade_endgame(bool v) { arcade_endgame_ = v; }
    bool arcade_endgame() const { return arcade_endgame_; }
    void set_arcade_round(int v) { arcade_round_ = v; }
    int arcade_round() const { return arcade_round_; }

    //! Summary screen: the element whose detail was requested.
    void set_summary_selected(const std::string& v) { summary_selected_ = v; }
    const std::string& summary_selected() const { return summary_selected_; }

    //! Queues a screen switch, the same way a `screen` verb does.
    void request_screen(const std::string& name) { screen_requests_.push_back(name); }
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
    //! Executes the verbs of `actions` matching `event`. Shared by widget and
    //! window-level dispatch.
    std::vector<GuiAction> run_actions(const std::vector<GuiAction>& actions,
                                       const std::string& window,
                                       const std::string& icon,
                                       const std::string& event);

    GuiScreen screen_;
    int next_z_ = 1;
    std::map<std::pair<std::string, std::string>, bool> selected_;
    bool arcade_accepted_ = false;
    int campaign_difficulty_ = 6;
    bool campaign_influence_ = false;
    bool campaign_started_ = false;
    int sim_type_ = 0;
    int sim_mission_ = 0;
    int sim_time_ = 1;        // day
    int sim_weather_ = 1;     // fair
    int sim_difficulty_ = 1;  // pro
    bool sim_default_weapons_ = true;
    bool sim_review_weapons_ = false;
    bool mission_accepted_ = false;
    int debrief_pilot_ = 0;
    int camp_side_ = 0;
    int camp_layer_ = 0;
    bool camp_flags_ = true;
    bool camp_sam_ = false;
    bool camp_ewr_ = false;
    int camp_map_mode_ = 0;
    int camp_zoom_ = 0;
    int camp_mission_ = 0;
    std::vector<int> camp_rows_;
    int camp_time_shift_ = 0;
    bool camp_target_view_ = false;
    bool camp_ready_ = false;
    std::string mp_strike_;
    std::string mp_target_;
    std::string mp_role_;
    std::string mp_actype_;
    int mp_slot_ = 0;
    int mp_assign_ = 0;
    int mp_speed_ = 500;
    std::string mp_atc_;
    bool mp_icons_ = false;
    bool mp_windows_ = true;
    bool mp_wp_zoom_ = false;
    bool mp_fly_ = false;
    std::string mp_base_;
    int mp_target_index_ = -1;
    int mp_area_index_ = -1;
    std::vector<std::pair<int, int>> mp_waypoints_;
    int map_click_x_ = 0;
    int map_click_y_ = 0;
    int map_click_radius_ = 0;
    bool map_click_valid_ = false;
    unsigned camp_side_mask_ = 7;   //!< NATO | Russian | Neutral
    unsigned camp_layer_mask_ = 0x3Fu;  //!< all six layers
    int camp_pan_x_ = 0;
    int camp_pan_y_ = 0;
    bool camp_zoom_mode_ = false;
    std::string summary_selected_;
    std::string weap_selected_;
    int weap_view_ = 0;
    int weap_x_angle_ = 0;
    int weap_y_angle_ = 0;
    bool weap_cleared_all_ = false;
    bool weap_accepted_ = false;
    bool arcade_continued_ = false;
    std::string arcade_hiscore_name_;
    bool arcade_hiscore_accepted_ = false;
    bool arcade_endgame_ = false;
    int arcade_round_ = 0;
    int custom_enemy_type_ = 0;
    bool custom_accepted_ = false;
    bool custom_guns_ = false;
    int custom_friendlies_ = 1;
    int custom_enemies_ = 1;
    std::vector<std::string> hook_stack_;
    std::vector<std::string> screen_requests_;
    std::vector<std::string> log_;

    std::pair<std::string, std::string> focus_;  //!< window, icon
    std::map<std::string,
             std::function<void(GuiSystem&, const std::string&, const std::string&)>>
        routines_;
    std::vector<std::string> unimplemented_;
    bool quit_requested_ = false;
};

//! Registers every routine the shipped `.gui` files reference. Each entry is
//! evidence-backed (runtime observation or data-entailed); the rest are
//! reported as missing rather than guessed. `dataset` is optional: the
//! strategic-map handlers need it to resolve a click to a target, base or
//! waypoint. Screens that do not use the map work without it.
struct Dataset;
void register_observed_routines(GuiSystem& system, const Dataset* dataset = nullptr,
                                const std::filesystem::path& data_dir = {},
                                std::function<void()> refresh_data = {},
                                std::function<std::optional<std::string>(
                                    const std::string&)> load_resource = {});

//! Fills the `hiscores` window from the loose `HISCORES.DAT` file: a 4-byte
//! header then ten 25-byte records of name[16], three pad bytes, level u8 and
//! score u32 (little-endian). Returns the number of rows filled.
int fill_hiscores(GuiSystem& system, const std::filesystem::path& data_dir);

//! The credits panel is an empty window in `mainmenu.gui`; its text is the
//! `supergui\gfxcred.txt` resource. Fills `lines` once, when visible.
void fill_credits(GuiSystem& system, const DidArchive& archive);

//! Selects campaign mission `index` and refreshes the Mission Briefing window
//! from it (number, time, type, target, aircraft count and the refuel, AWACS
//! and JSTARS support flags).
void select_campaign_mission(GuiSystem& system, const Dataset& dataset, int index);

//! Maps a key event to the name the `.gui` files use ("esc", "ret", "f1", "c").
std::string gui_key_name(const SDL_Keysym& key);

//! `colours\<name>.col` as 0x00RRGGBB.
std::optional<Palette> load_gui_palette(const DidArchive& archive,
                                        const std::string& name = "gui");

}  // namespace ef
