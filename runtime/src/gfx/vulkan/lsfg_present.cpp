// SPDX-License-Identifier: MPL-2.0
// Opt-in adapter for main's renderer: it owns the swapchain, overlay, record worker and
// surface-loss recovery. We only temporarily substitute TV scan images and call its presenter.
#include "lsfg.h"
#include "deferred_dispatch.h"
#include "present.h"
#include "stereo.h"
#include "../hdr.h"
#include "../display_modes.h"
#include "overlay/hostui.h"
#include "mods/enhanced_touch.h"
#include "mods/mods.h"
#include "platform/host.h"
#include "platform/frame_generation.h"
#include "interp.h"
#include "imgui.h"
#include "runtime.h"
#ifdef __ANDROID__
#include "platform/android_documents.h"
#endif
#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <thread>

namespace gfxvk::fg {
void allocate_image(Image&,VkImageView&,uint32_t,uint32_t,VkFormat,VkImageUsageFlags);
namespace {
enhanced::Option enabled{"enhancedLsfg",0,1}, multiplier{"enhancedLsfgMultiplier",2,4};
enhanced::Option quality{"enhancedLsfgQuality",0,1}, flow{"enhancedLsfgFlow",100,100}, ui{"enhancedLsfgUi",1,1};
std::atomic<unsigned> revision{1};
std::mutex messageMutex;
std::string message="Off";
void status(std::string text) { std::lock_guard<std::mutex> lock(messageMutex); message=std::move(text); }
std::string status() { std::lock_guard<std::mutex> lock(messageMutex); return message; }
std::string dll_path() { return (std::filesystem::path(host::config_dir())/"enhanced"/"Lossless.dll").string(); }
Image input;
VkImageView inputView=VK_NULL_HANDLE;
Surface inputSurface;
unsigned applied=0;
bool failed=false;
std::atomic<uint64_t> presented{0};
void free_input() {
    if (inputView) vkDestroyImageView(R.device,inputView,nullptr);
    if (input.image) vkDestroyImage(R.device,input.image,nullptr);
    if (input.memory) vkFreeMemory(R.device,input.memory,nullptr);
    input={}; inputView=VK_NULL_HANDLE; inputSurface={};
}
void changed() { ++revision; }
void import_dll(std::string path) {
    hostui::post([path=std::move(path)]() mutable {
        try {
#ifdef __ANDROID__
            if (path.starts_with("content://")) path=host::android::copy_picked_document(path,128*1024*1024);
#endif
            auto error=check_dll(path);
            if (!error.empty()) { status(error); return; }
            const auto target=std::filesystem::path(dll_path());
            std::filesystem::create_directories(target.parent_path());
            auto temporary=target; temporary+=".new";
            std::filesystem::copy_file(path,temporary,std::filesystem::copy_options::overwrite_existing);
            std::filesystem::remove(target);
            std::filesystem::rename(temporary,target);
            status("Supported Lossless.dll imported. Enable LSFG to start."); changed();
        } catch (const std::exception& e) { status(std::string("DLL import failed: ")+e.what()); }
    });
}
}
void settings() {
    if (!ImGui::TreeNode("Lossless Scaling frame generation (experimental)")) return;
    bool on=enabled.get();
    if (ImGui::Checkbox("Enable LSFG",&on)) { enabled.set(on); changed(); }
    if (ImGui::Button("Import your Lossless.dll")) hostui::choose_file("Lossless.dll","dll",false,import_dll);
    int mult=std::max(2,multiplier.get())-2;
    if (ImGui::Combo("Generated output",&mult,"2x\0" "3x\0" "4x\0")) { multiplier.set(mult+2); changed(); }
    int q=quality.get();
    if (ImGui::Combo("LSFG network",&q,"Performance\0Quality\0")) { quality.set(q); changed(); }
    int scale=std::max(25,flow.get());
    if (ImGui::SliderInt("Flow resolution",&scale,25,100,"%d%%")) flow.value=scale;
    if (ImGui::IsItemDeactivatedAfterEdit()) { flow.set(scale); changed(); }
    bool hud=ui.get();
    if (ImGui::Checkbox("LSFG UI detection",&hud)) { ui.set(hud); changed(); }
    ImGui::TextWrapped("%s",status().c_str());
    ImGui::Text("LSFG GPU %.2f ms; output frames %llu",gpu_ms(),(unsigned long long)presented.load());
    ImGui::TextWrapped("Requires your own compatible Lossless Scaling DLL (LSFG 3). No DLL or shaders are downloaded or included. This port currently uses the FP32 network and single-screen SDR Vulkan output. HDR, stereo, dual displays and fast forward use ordinary presentation. Generated frames add latency and GPU work; they do not increase game logic FPS. Disable upstream frame interpolation for a 30-to-60 LSFG setup.");
    ImGui::TreePop();
}
void shutdown() {
    // Caller drained every submission, including the upstream recording worker.
    unload(); free_input(); applied=0; failed=false;
    enhanced::frame_generation::multiplier=1; enhanced::frame_generation::output_fps=0;
}
bool present_frames(const std::function<bool()>& present) {
    const unsigned current=revision.load();
    if (!enabled.get()) {
        if (loaded() || input.image) { flush(); shutdown(); }
        return false;
    }
    if (current != applied) { flush(); shutdown(); applied=current; }
    if (failed) return false;
    if (hdr::enabled.load() || gfx::g_has_drc_window || stereo::frame.mode != stereo::Off || mods::fast_forward_active()) {
        if (loaded() || input.image) { flush(); shutdown(); applied=current; }
        status("Paused: single-screen SDR output is required; normal presentation active.");
        return false;
    }
    auto* source=R.tv.scan.get();
    if (!source || !source->image || !R.tv.visible || R.tv.width<=0 || R.tv.height<=0) return false;
    try {
        // Drain before reusing network images, updating descriptors, or freeing any allocation.
        // This conservative path intentionally reuses main's own queue and submission retirement.
        end_encoder(); flush();
        if (!loaded()) {
            Config config; config.dll=dll_path(); config.performance=!quality.get();
            config.multiplier=std::max(2,multiplier.get()); config.flowScale=std::max(25,flow.get())/100.f;
            config.uiDetection=ui.get();
            if (!R.computeQueue || !load(config)) {
                const auto error=R.computeQueue ? last_error() : "This Vulkan queue cannot run LSFG compute shaders.";
                shutdown(); applied=current; failed=true; status(error); return false;
            }
        }
        enhanced::frame_generation::multiplier=config().multiplier;
        const auto w=source->extent.width, h=source->extent.height;
        if (!input.image || input.width!=w || input.height!=h) {
            destroy(); free_input();
            allocate_image(input,inputView,w,h,VK_FORMAT_R8G8B8A8_UNORM,
                           VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT);
            inputSurface.image=input.image; inputSurface.view=inputView; inputSurface.extent={w,h,1};
            inputSurface.fmt.pixel=VK_FORMAT_R8G8B8A8_UNORM;
            inputSurface.usage=VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;
            if (!resize(w,h,inputView)) throw std::runtime_error("Could not allocate LSFG network");
        }
        const auto srcFeatures=format_properties(source->fmt.pixel).optimalTilingFeatures;
        if (!(srcFeatures&VK_FORMAT_FEATURE_BLIT_SRC_BIT) || !(source->usage&VK_IMAGE_USAGE_TRANSFER_SRC_BIT) ||
            !(format_properties(VK_FORMAT_R8G8B8A8_UNORM).optimalTilingFeatures&VK_FORMAT_FEATURE_BLIT_DST_BIT))
            throw std::runtime_error("Unsupported LSFG input scan format");
        transition_image(source,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_READ_BIT);
        transition_image(&inputSurface,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
        auto cmd=command_buffer();
        VkImageBlit blit{}; blit.srcSubresource=blit.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
        blit.srcOffsets[1]=blit.dstOffsets[1]={int(w),int(h),1};
        record::vkCmdBlitImage(cmd,source->image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,input.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&blit,VK_FILTER_NEAREST);
        transition_image(&inputSurface,VK_IMAGE_LAYOUT_GENERAL,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT);
        const int base=interp::mode()==0?30:interp::output_fps();
        const int hz=interp::display_hz();
        const bool generate=hz<=0 || base*config().multiplier<=hz+2;
        const auto images=record(cmd,generate);
        // Make the network's writes visible to the original shader presenter.
        VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT; barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_TRANSFER_READ_BIT;
        record::vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&barrier,0,nullptr,0,nullptr);
        flush();
        struct Restore {
            std::unique_ptr<Surface> scan; bool srgb;
            ~Restore() { R.tv.scan=std::move(scan); R.tv.scanSrgb=srgb; }
        } restore{std::move(R.tv.scan),R.tv.scanSrgb};
        R.tv.scan=std::make_unique<Surface>();
        const auto start=std::chrono::steady_clock::now();
        const auto spacing=std::chrono::nanoseconds(1'000'000'000/std::max(1,base)/config().multiplier);
        for (size_t i=0;i<images.size();i++) {
            if (i) std::this_thread::sleep_until(start+spacing*i);
            auto& scan=*R.tv.scan; scan={};
            scan.image=images[i]->image; scan.view=view_of(images[i]); scan.extent={w,h,1};
            scan.width=w; scan.height=h; scan.fmt.pixel=VK_FORMAT_R8G8B8A8_UNORM;
            scan.layout=VK_IMAGE_LAYOUT_GENERAL;
            scan.usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            derive_dependency(scan.use,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_WRITE_BIT);
            // A blit from an sRGB image decodes its samples to linear UNORM.
            R.tv.scanSrgb=restore.srgb || restore.scan->fmt.pixel==VK_FORMAT_R8G8B8A8_SRGB || restore.scan->fmt.pixel==VK_FORMAT_B8G8R8A8_SRGB;
            if (present()) ++presented;
            transition_image(&scan,VK_IMAGE_LAYOUT_GENERAL,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);
            flush(); // all image descriptors/commands retire before this borrowed scan is reset
        }
        static auto sampledAt=std::chrono::steady_clock::now();
        static uint64_t sampledFrames=0;
        const auto now=std::chrono::steady_clock::now();
        const double seconds=std::chrono::duration<double>(now-sampledAt).count();
        if (seconds>=.5) {
            const uint64_t frames=presented.load();
            enhanced::frame_generation::output_fps=(frames-sampledFrames)/seconds;
            sampledAt=now; sampledFrames=frames;
        }
        status(generate ? "Active (FP32, conservative synchronized presentation)" : "Generation paused: multiplier exceeds display refresh rate.");
        return true;
    } catch (const std::exception& e) {
        const std::string error=e.what();
        flush(); // a device-loss error propagates to upstream instead of freeing in-flight resources
        shutdown(); applied=current; failed=true;
        status("LSFG disabled for this configuration: "+error);
        LOG("[fg] %s; ordinary presentation restored",error.c_str());
        return false;
    }
}
}
