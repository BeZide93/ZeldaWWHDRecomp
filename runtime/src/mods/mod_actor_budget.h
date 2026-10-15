#pragma once
#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <vector>
namespace mod_actor::budget {
inline constexpr uint64_t ModelLimit=2u*1024*1024,ModLimit=8u*1024*1024,RoomLimit=16u*1024*1024;
inline constexpr uint32_t BackingReserve=8u*1024*1024,SolidReserve=1024*1024;
struct Record {std::string mod,archive;int room=0;uint32_t backing=0,resource=0,instance=0,actor=0;bool loaded=false,active=false;};
struct Usage {uint64_t room=0;std::map<std::string,uint64_t> mods;};
// Cached backing retains its measured charge until the native archive unloads. Models shared by
// several actor declarations are counted once; each native actor and its solid heap count separately.
inline bool check(const std::vector<Record>& records,int room,Usage& out,std::string& why){
    out={};std::map<std::string,std::pair<std::string,uint64_t>> models;
    for(const auto& r:records){if(r.loaded||r.active){const uint64_t cost=uint64_t(r.backing)+r.resource;if(cost>ModelLimit){why="mod model exceeds 2 MiB of game heap allocations";return false;}auto& model=models[r.archive];model.first=r.mod;model.second=std::max(model.second,cost);}
        if(r.active){const uint64_t cost=uint64_t(r.instance)+r.actor;out.mods[r.mod]+=cost;if(r.room==room)out.room+=cost;}}
    for(const auto& [archive,model]:models){out.mods[model.first]+=model.second;out.room+=model.second;}
    for(const auto& [mod,bytes]:out.mods)if(bytes>ModLimit){why="mod actor allocations exceed 8 MiB: "+mod;return false;}
    if(out.room>RoomLimit){why="room mod actor allocations exceed 16 MiB";return false;}return true;
}
}
