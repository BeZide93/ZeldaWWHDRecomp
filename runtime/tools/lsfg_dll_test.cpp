// SPDX-License-Identifier: MPL-2.0
#include "gfx/vulkan/lsfg_dll.h"
#include <array>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>
int main(int argc,char** argv) {
    assert(argc==2);
    std::array<gfxvk::fg::dll::Shader,49> shaders;
    std::string error;
    const std::string file=argv[1];
    assert(!gfxvk::fg::dll::read(file+".missing",shaders.data(),error));
    for (int size : {0,1,60,64,255,4096}) {
        std::ofstream out(file,std::ios::binary);std::string data(size,'X');out.write(data.data(),data.size());out.close();
        error.clear();assert(!gfxvk::fg::dll::read(file,shaders.data(),error));assert(!error.empty());
    }
    // An attacker-controlled PE offset must fail rather than wrap or read beyond the buffer.
    std::string data(256,'\0');data[0x3c]=data[0x3d]=data[0x3e]=data[0x3f]=char(0xff);
    std::ofstream out(file,std::ios::binary);out.write(data.data(),data.size());out.close();
    assert(!gfxvk::fg::dll::read(file,shaders.data(),error));
    std::filesystem::remove(file);
}
