// Conservative placement queries. Native resource data stays on the player's machine.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace story_world_placement {
struct Point {float x=0,y=0,z=0;};
struct Bounds {Point minimum,maximum;};
struct ProtectedBounds {std::string stage;int room=-1;std::string kind,id;Bounds bounds;};
struct ProtectedRoute {std::string stage;int room=-1;std::string id;std::vector<Point> points;float radius=0;bool closed=false;};
struct Coverage {std::string stage;int room=-1;bool collision=false,routes=false,events=false;std::string reason;};
struct CoverageRegion {std::string stage;int room=-1;Bounds bounds;};
// One entry of the room's own layout (add_native_layout): what an island building is checked against
// (check_building). The position is in world space; scale is the layout's three bytes (scaled tags only).
struct LayoutEntry {std::string stage;int room=-1;std::string tag,name;Point position;uint8_t scale[3]{};bool scaled=false;unsigned index=0;};
// layout and layout_routes are in world space: a sea room's own layout already is (its entries are not moved
// by the room's MULT placement again, unlike `bounds` and `routes`, which world objects use as they are).
struct Index {std::vector<ProtectedBounds> bounds;std::vector<ProtectedRoute> routes;std::vector<Coverage> coverage;std::vector<CoverageRegion> regions;std::vector<LayoutEntry> layout;std::vector<ProtectedRoute> layout_routes;};
// message: plain words for mod makers ("a door of the island"); native: the game's own name when known ("KNOB00D")
struct Conflict {std::string kind,id,message,native;};
bool valid(const Bounds&);
// Degrees, with the native actor's RY * RX * RZ rotation order.
bool transform_bounds(const Bounds&,Point position,Point rotation,Point scale,Bounds&);
std::vector<Conflict> check(const std::string& stage,int room,const Bounds&,const Index&,float clearance=100);
struct SweepResult {Point allowed_delta;std::vector<Conflict> conflicts;bool blocked=false;};
// Continuous horizontal motion of a fixed conservative wave/tilt envelope.
// A nonzero vertical delta is refused. Earliest contact stops the blocked axis;
// a second continuous query permits sliding along an unblocked axis. Unknown
// coverage, initial overlap and malformed dynamic obstacles stop all movement.
SweepResult sweep(const std::string& stage,int room,const Bounds& start,Point desired_delta,const Index&,float clearance=100);
// Bounded readers of original stage resources. Invalid or unsupported input never grants coverage.
// `world_position` and `world_yaw` are the room's placement from its stage MULT entry.
bool add_native_collision(Index&,const std::string& stage,int room,const std::vector<uint8_t>& dzb,Point world_position={},float world_yaw=0,std::string* why=nullptr);
bool add_native_layout(Index&,const std::string& stage,int room,const std::vector<uint8_t>& dzr,Point world_position={},float world_yaw=0,std::string* why=nullptr);
// Load loose or packed stage archives and their BFRES embedded DZB/DZR files.
// Never writes, exports or caches native bytes. A returned index may have incomplete coverage.
Index load_native_room(const std::string& game_directory,const std::string& stage,int room);
// ---- island buildings (story_game_pieces.h): a precise check instead of the world objects' wide
// event columns. What a building must keep clear of, by layout entry, with its real extent where it is
// known (a tag area's own radius and height from its scale; a door, chest, villager or item by its kind)
// and a reasonable default for any other object. Grass, flowers, trees, palms, birds, crabs and the
// light and weather tags (kytag) are ignored: a building may stand over them.
enum class Keep {None,Villager,Chest,Door,Item,Switch,Event,Object};
struct Extent {Keep keep=Keep::Object;float radius=150,below=50,above=400;};
inline constexpr float kRouteHalfWidth=120;   // a walking route's corridor half width (Link-sized walkers)
inline constexpr float kBuildingMargin=50;    // added around the building's footprint
Extent building_extent(const LayoutEntry&);
const char* keep_words(Keep);
// Conflicts of a building's world bounds with the room's layout entries and routes: kind is what is in the
// way in plain words ("villager", "villager route", "chest", "door", "item", "switch", "cutscene area",
// "object"), id the layout entry ("TGDR:KNOB00D:3"), message a sentence. Coverage is checked by check().
std::vector<Conflict> check_building(const std::string& stage,int room,const Bounds& footprint,const Index&,float margin=kBuildingMargin);
}
