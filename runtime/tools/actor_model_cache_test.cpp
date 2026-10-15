#include "mods/actor_model_cache.h"
#include "mods/mod_json.h"
#include "static_model_fixture.h"
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
int main(int argc,char** argv){
    namespace fs=std::filesystem;
    if(argc>1&&std::string(argv[1])=="--converter"){
        std::string output,identity;for(int i=2;i+1<argc;++i){if(std::string(argv[i])=="--out")output=argv[i+1];if(std::string(argv[i])=="--identity")identity=argv[i+1];}
        if(std::string(argv[2])=="fail")return 1;
        if(std::string(argv[2])=="reuse"&&!fs::is_regular_file(output))return 2;
        if(std::string(argv[2])!="missing"&&std::string(argv[2])!="reuse"){std::ofstream file(output,std::ios::binary);file.write("Yaz0synthetic-test",18);}
        mods::json::Value result;result["identity"]=std::string(argv[2])=="wrong"?"wrong":identity;std::cout<<mods::json::dump(result)<<'\n';return 0;
    }
    const auto root=fs::temp_directory_path()/("wwhd-model-cache-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Clean{fs::path p;~Clean(){std::error_code e;fs::remove_all(p,e);}}clean{root};
    fs::create_directories(root/"package with spaces/models");auto fixture_doc=static_model_fixture::cube_document();fixture_doc["nodes"].array[0]["translation"]=mods::json::parse("[1,2,3]");fixture_doc["nodes"].array[0]["scale"]=mods::json::parse("[2,3,4]");const auto fixture=static_model_fixture::glb(fixture_doc,static_model_fixture::cube_binary());{std::ofstream file(root/"package with spaces/models/own.glb",std::ios::binary);file.write(reinterpret_cast<const char*>(fixture.data()),std::streamsize(fixture.size()));}
    const std::vector<std::string> command{fs::absolute(argv[0]).string(),"--converter"};
    story_notebook::ManifestModel model{"post","models/own.glb"};
    auto prepare=[&](const std::string& mod,const std::string& tool){return actor_model_cache::prepare(mod,model,(root/"package with spaces").string(),(root/"data").string(),command,tool,"game with spaces");};
    const auto first=prepare("owner","ok");assert(first.size()==31&&first.starts_with("mod"));
    const auto guest="/vol/content/Common/Object/"+first+".szs";const auto file=actor_model_cache::archive_for(guest);assert(file==(root/"data/models"/(first+".szs")).string());
    story_notebook::WorldPoint minimum,maximum;assert(actor_model_cache::bounds_for(first,minimum,maximum));assert(minimum.x==0&&minimum.y==50&&minimum.z==100&&maximum.x==200&&maximum.y==350&&maximum.z==500);
    assert(!actor_model_cache::bounds_for("missing",minimum,maximum));
    const auto second=prepare("other","ok");assert(second!=first);assert(prepare("owner","reuse")==first);
    for(const auto& path:{first+".szs","/vol/content/Common/Object/../Object/"+first+".szs","/vol/content/Common/object/"+first+".szs",guest+"/tail","/vol/content/Common/Object/"+first+".bfres"})assert(actor_model_cache::archive_for(path).empty());
    auto fails=[&](const std::string& tool){bool threw=false;try{prepare("owner",tool);}catch(const std::exception&){threw=true;}assert(threw&&actor_model_cache::archive_for(guest).empty()&&!actor_model_cache::bounds_for(first,minimum,maximum));};
    fails("fail");prepare("owner","ok");fails("wrong");
    fs::remove(file);fails("missing");
    assert(prepare(std::string(64,'a'),"ok").size()==31);
    bool long_owner_failed=false;try{prepare(std::string(65,'a'),"ok");}catch(const std::exception&){long_owner_failed=true;}assert(long_owner_failed);
    model.source="../outside.glb";fails("ok");
    assert(actor_model_cache::archive_for("/vol/content/Common/Object/mod0000000000000000000000000000.szs").empty());
    std::cout<<"actor model cache tests passed\n";
}
