#pragma once
#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>
namespace mod_actor::budget {
inline constexpr uint64_t ModelLimit=2u*1024*1024,ModLimit=8u*1024*1024,RoomLimit=16u*1024*1024;
// Game piece sets (story_game_pieces.h): a set archive may take up to PieceRoomLimit, and the pieces of a
// room (each set archive once, each placement) stay within it together (the island buildings' budget).
inline constexpr uint64_t PieceRoomLimit=8u*1024*1024;
inline constexpr uint32_t BackingReserve=8u*1024*1024,SolidReserve=1024*1024;
struct Record {std::string mod,archive;int room=0;uint32_t backing=0,resource=0,instance=0,actor=0;bool loaded=false,active=false,piece=false;};
struct Usage {uint64_t room=0,pieces=0;std::map<std::string,uint64_t> mods;};
// Cached backing retains its measured charge until the native archive unloads. Models shared by
// several actor declarations are counted once; each native actor and its solid heap count separately.
inline bool check(const std::vector<Record>& records,int room,Usage& out,std::string& why){
    out={};std::map<std::string,std::pair<std::string,uint64_t>> models;
    std::set<std::string> piece_archives;
    for(const auto& r:records){if(r.loaded||r.active){const uint64_t cost=uint64_t(r.backing)+r.resource;if(cost>(r.piece?PieceRoomLimit:ModelLimit)){why=r.piece?"game piece set exceeds 8 MiB of game heap allocations":"mod model exceeds 2 MiB of game heap allocations";return false;}auto& model=models[r.archive];model.first=r.mod;model.second=std::max(model.second,cost);if(r.piece&&(r.active?r.room==room:true))piece_archives.insert(r.archive);}
        if(r.active){const uint64_t cost=uint64_t(r.instance)+r.actor;out.mods[r.mod]+=cost;if(r.room==room){out.room+=cost;if(r.piece)out.pieces+=cost;}}}
    for(const auto& [archive,model]:models){out.mods[model.first]+=model.second;out.room+=model.second;if(piece_archives.contains(archive))out.pieces+=model.second;}
    if(out.pieces>PieceRoomLimit){why="game pieces in this room exceed 8 MiB (each set archive once, each placement)";return false;}
    for(const auto& [mod,bytes]:out.mods)if(bytes>ModLimit){why="mod actor allocations exceed 8 MiB: "+mod;return false;}
    if(out.room>RoomLimit){why="room mod actor allocations exceed 16 MiB";return false;}return true;
}
}
