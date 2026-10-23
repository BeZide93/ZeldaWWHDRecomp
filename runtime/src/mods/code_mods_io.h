// Build metadata is atomically replaced by setup while the game polls it.
#pragma once
#include "mod_json.h"
#include <filesystem>
#include <fstream>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace mods::code {
inline json::Value read_build_json(const std::filesystem::path& path) {
    constexpr size_t limit=1024*1024;
    const auto fail=[] { exception_report::raise("Code-mod build configuration is unavailable; run setup again from a complete release folder"); };
    std::string data;
#ifdef _WIN32
    struct File {
        HANDLE handle;
        ~File(){if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);}
    } file{CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
                       nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr)};
    if(file.handle==INVALID_HANDLE_VALUE)fail();
    LARGE_INTEGER size{};
    if(!GetFileSizeEx(file.handle,&size)||size.QuadPart<0||size.QuadPart>limit)fail();
    data.resize(size_t(size.QuadPart));
    DWORD count=0;
    if(!ReadFile(file.handle,data.data(),DWORD(data.size()),&count,nullptr)||count!=data.size())fail();
#else
    std::ifstream file(path,std::ios::binary);
    if(!file)fail();
    // Bound the read itself, including a file that grows after opening.
    data.resize(limit+1);
    file.read(data.data(),std::streamsize(data.size()));
    data.resize(size_t(file.gcount()));
    if(file.bad()||data.size()>limit)fail();
#endif
    return json::parse(data);
}
}
