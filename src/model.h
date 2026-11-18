#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ef {

struct Vec3 {
    int x = 0, y = 0, z = 0;
};

struct Waypoint {
    int index = 0;
    Vec3 p;
    int a = 0, b = 0;
    std::string action;
};

struct Route {
    int count = 0;
    std::vector<Waypoint> waypoints;
};

struct Group {
    int count = 0;
    std::string type, role, status;
    std::optional<Vec3> pos;
};

struct TargetElement {
    std::string slot, kind, type, name;
};

struct AttackNode {
    Vec3 p;
    std::string action;
};

struct AttackRoute {
    int count = 0;
    std::vector<AttackNode> nodes;
};

struct AttackPattern {
    std::string type;
    int count = 0;
    std::vector<AttackRoute> routes;
};

struct TargetArea {
    std::string type, name;
    std::optional<Vec3> pos;
    std::vector<TargetElement> elements;
    std::vector<AttackPattern> patterns;
};

struct Target {
    int mission = 0, group = 0;
    std::optional<Vec3> pos;
    std::vector<TargetArea> areas;
    std::vector<AttackPattern> patterns;
};

struct Flight {
    int count = 0;
    std::string aircraft, role, status;
    std::optional<Vec3> pos;
    std::vector<std::pair<int, std::string>> pylons;
};

struct Mission {
    int line = 0;
    std::string type, side, role, status;
    std::vector<int> values;
    std::optional<Vec3> pos;
    std::vector<Group> groups;
    std::vector<Route> routes;
    std::vector<Target> targets;
    std::vector<Flight> flights;
    int pilot_slots = 0;
};

struct Base {
    int line = 0;
    std::string side, type, name;
    std::optional<Vec3> pos;
};

struct Bna {
    int line = 0;
    std::string type;
    int base = 0;
    std::optional<Vec3> pos;
    std::vector<Route> routes;
};

struct ConflictArea {
    std::string name, side, flag;
    int value = 0;
};

struct Dataset {
    std::string dir;
    std::string mission_path, navig_path, conflict_path;
    int num_missions_declared = 0;
    int num_bna_declared = 0;
    int num_areas_declared = 0;
    std::vector<int> world_status;
    std::vector<Mission> missions;
    std::vector<Base> bases;
    std::vector<Bna> bnas;
    std::vector<ConflictArea> areas;
};

}  // namespace ef
