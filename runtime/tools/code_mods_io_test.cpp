#include "mods/code_mods_io.h"
#include <cassert>
#include <iostream>
#include <string>
int main(int argc,char** argv) {
    assert(argc==2);
    const auto scratch=std::filesystem::u8path(argv[1]).parent_path()/"reader-validation.json";
    auto rejected=[&] {
        bool failed=false;
        try { mods::code::read_build_json(scratch); }
        catch(const std::exception&) { failed=true; }
        assert(failed);
    };
    rejected(); // no publication yet
    std::ofstream(scratch)<<"{broken";
    rejected();
    std::ofstream(scratch)<<std::string(1024*1024+1,' ');
    rejected();
    std::filesystem::remove(scratch);
    // Keep polling until the parent writer signals completion over stdin.
    std::string command;
    while(std::getline(std::cin,command)&&command!="stop") {
        auto value=mods::code::read_build_json(std::filesystem::u8path(argv[1]));
        assert(value.get("stage").string()=="building");
        assert(value.get("payload").string().size()==512*1024);
        std::cout<<"read"<<std::endl;
    }
}
