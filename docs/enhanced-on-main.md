# Selected Enhanced features on upstream main

This branch starts at official upstream `264f53ee0eca9d52d39f6ccf1a890eecec018e6e`
(v0.5.0). It ports selected ideas and code from
[SSunnKing's Enhanced fork](https://github.com/SSunnKing/ZeldaWWHDRecompAndroid---ENHANCED)
at `2f28a4012fd2b5001695d1c8fd96d453d6304878` onto main's current architecture.
The original upstream commits remain ancestors. This is not a merge of the old
Android renderer or launcher. New gameplay/input options and LSFG are off by default;
CPU affinity defaults to upstream's automatic policy.

## Options

| Feature | Location and behavior |
| --- | --- |
| Four combat buttons | Settings → Controls → camera extras → Enhanced touch controls: independently enable Jump attack, Vertical slash, Spin attack, and Dodge/backflip. Use the existing touch layout editor to place/size them. Attacks require a sword. Dodge uses left/right stick direction, or backflips from rest. |
| Camera-distance pinch | Same section: enable **Pinch changes camera distance**. In Flexible touch mode, spread two free fingers to move the eye closer; pinch together to move away. Range 0.5–2×. This takes precedence over FOV pinch while enabled. The slider and reset button also control it. Distance resets at app restart. |
| Right-side double tap | Same section: enable **Double-tap right camera area to center**. Two short, nearby taps in the free right camera area produce a brief ZL hold. Drags, long holds, cancelled touches, and pinch takeover do not count. |
| Touch haptics | Same section: **Touch button vibration**. Uses Android's standard keyboard-tap feedback and respects system haptic preferences. Separate from gameplay rumble. |
| CPU core policy | Settings → Graphics: Automatic, Performance cores, Prime renderer, Prime game thread. Three-cluster devices use the middle cluster for Performance; two-cluster devices use the fast cluster. Other frame threads use Performance when one role uses Prime. Automatic restores upstream's original thread masks. ADPF and thermal controls remain active. The displayed status reports unavailable topology or refused affinity. |
| Performance HUD | Settings → Graphics → Overlay → **Customize performance HUD**. Select FPS, frame interval, averages/logic rate, public thermals, CPU/GPU temperatures, GPU load, device details, detailed timings, graph, active settings, and process CPU use. Position/scale/background persist. Enable dragging while Settings is open, then drag the HUD's handle. CPU use sums cores; 100% means one fully busy core. Missing device sensors remain unavailable. |
| Lossless Scaling FG | Settings → Graphics → **Lossless Scaling frame generation (experimental)**. Import your own compatible `Lossless.dll`, enable LSFG, choose 2×/3×/4×, Performance/Quality, flow resolution and UI detection. The HUD distinguishes rendered FPS from LSFG output FPS. |

Macro sequences pass through main's virtual controller and existing input mapping.
Their timed ZL holds temporarily bypass the touch ZL latch. Hiding the controls,
opening settings, losing focus, changing style, cancelling input, or losing the
playable context releases their generated input. User-held buttons are combined
with the sequence rather than released by its timer.

Distance zoom is applied **after** upstream camera interpolation chooses a view.
The guest's eye coordinates and collision-call CPU state are restored afterwards.
Upstream's drawn-camera history receives the actual eye, so dependent sky/audio
tracking can use it. Zoom-out uses the game's region-mapped camera collision check;
event and first-person views keep the original distance.

## LSFG scope and limitations

The LSFG host compute graph is adapted from Enhanced under MPL-2.0. The port uses
main's Vulkan allocator, command recording, submission retirement, compositor,
overlay, and swapchain recovery. It does not introduce a second swapchain owner.
No DLL, proprietary shader bytes, game assets, textures, pictures, or BMD files
are included in this change.

The supported shader fingerprint is SHA-1
`fc6092a72b94003fac3d68e9a7b54954422db757` (the shader set supported by Enhanced).
This is a fingerprint of the two shader sets, not the DLL file. Unknown versions
are rejected before shader execution. The imported copy lives at
`<config directory>/enhanced/Lossless.dll`; it is read as data, never executed.
Import does not enable the feature automatically.

This initial adapter uses the **FP32** network and conservative, synchronized
presentation. It requires a compute-capable Vulkan queue and supported storage
and sampled image formats. It currently supports single-screen SDR output.
HDR, stereo, dual-display operation and fast forward use normal presentation.
Invalid/missing DLLs or setup failures also fall back, with a visible status.
Generation pauses if the selected multiplier exceeds the measured display rate;
on Android, the requested refresh rate includes the multiplier.

Generated frames do not increase game logic FPS. They consume GPU time and add
latency; slower devices may lose performance. For a 30→60 LSFG configuration,
disable main's own frame interpolation. The synchronized adapter favors correct
resource retirement over maximum throughput. Real-device pacing, visual quality,
thermal behavior, and resize/suspend/resume still require device testing with a
supported DLL. This branch does not establish LSFG as production-ready.

## Maintaining the upstream relationship

Keep official main as the `upstream` remote and merge its real commits normally:

```sh
git remote add upstream https://github.com/ZeldaWWHDRecomp/ZeldaWWHDRecomp.git
# If it already exists, use git remote set-url upstream <the same URL> instead.
git fetch upstream
git switch enhanced-on-main
git merge upstream/main
git rev-list --left-right --count upstream/main...HEAD
```

The first number should be zero after an update. Do not use an `ours` merge to mark
new upstream code as integrated. Future upstream changes can still cause conflicts;
ancestry alone cannot guarantee source/API compatibility.

Most behavior lives in new `enhanced_*`, `camera_distance`, `cpu_affinity`, and
`lsfg*` modules. Review these small upstream integration points after merging:

- `TouchControls.java`: control registration, gestures, lifecycle reset, input union.
- `mods/steer.cpp`, `mods/context_touch.cpp`, `interp.cpp`: gesture dispatch, timed
  ZL semantics, and temporary camera drawing/history.
- `platform/perf_hint.*`, game/render thread registration: CPU role assignment.
- `overlay/overlay.cpp`: setting rows and configurable HUD presentation.
- `gfx/vulkan/backend.cpp`: optional device feature, primary present hook, teardown.
- `platform/display_rate.cpp`: Android's requested LSFG output rate.

## Verification

Use main's own game-free runtime build path:

```sh
python tools/recomp/stubgen.py /tmp/wwhd-stub-gen
cmake -S . -B build-check -DGEN_DIR=/tmp/wwhd-stub-gen \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build-check --target wwhd enhanced_affinity_test \
  enhanced_camera_test lsfg_dll_test steer_test touch_play_test \
  context_touch_test interp_camera_test gamepad_touch_test vulkan_record_worker_test
ctest --test-dir build-check --output-on-failure \
  -R 'enhanced_|steer|touch_play|context_touch|interp_camera|gamepad_touch|vulkan_record_worker'
```

`enhanced_camera_test` is registered on Unix hosts. The macro/gesture test is
registered when Java and javac are available. It checks sequence timing,
replacement/cancellation, and rejection of drags, holds, pinches, and distant taps.
Camera tests check collision limiting, exclusions, guest-state restoration and
exception cleanup. CPU policy tests cover one/two/three clusters and unreadable
cores. DLL tests reject absent, empty, malformed, and out-of-range PE inputs.

The game-free executable is a build check, not a playable game. A complete Android
APK build and a playtest are separate validation steps. No Android playtest or
successful LSFG shader execution has been performed for this port.

Validation performed for this port: the complete Linux runtime linked successfully
with Clang 18 using upstream's generated game stubs; 13 selected CTest cases passed.
The touched Java control classes and their real SDL classes compiled against
Android API 35 (with generated resource symbols and an activity stand-in).
The new JNI bridge and affinity/camera sources also passed an Android-conditional
syntax check on the host. This does not replace an NDK/Gradle APK build.
The GPU smoke test was skipped because this environment could not start a Vulkan
window; no successful GPU/device rendering result is claimed.
