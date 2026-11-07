# Next: plugins and the rest of the Stratary / Simtary split

Paused 2026-10-27. Where the split stands, and what to pick up next, in order.

## Done

- **Workspace layout**: `HMMWV4/Stratary` (engine, public MIT repo `Stratary.git`),
  `HMMWV4/Simtary` (simulation extension, private, not a git repo yet),
  `HMMWV4/Projects/Milistry`, `HMMWV4/Projects/template`. Projects find the engine through
  `STRATARY_ROOT` or that layout; Milistry finds Simtary through `SIMTARY_EXT_ROOT`.
- **Extension mechanism** (`cmake/SimtaryExtension.cmake`): `stratary_add_extension(dir)`,
  `stratary_define_extension(NAME FRAMEWORK_DIR INCLUDES LIBS RUNTIME ENGINE_SHADERS)`,
  `simtary_add_app(... EXTENSIONS <name> PLUGINS <name>)`. Extension sources compile with the
  framework (host side of a MODULE build).
- **One-way boundary check** (end of `cmake/SimtaryRenderBoundary.cmake`): Stratary's
  `Framework/` may not name `flight/`, JSBSim, Laser/Projector/Optics, `online/`, EOS.
- **Seams in Stratary**:
  - `stAppSystem.h` - `st::AppSystem` + `ST_REGISTER_APP_SYSTEM`: `Init(GameRenderPath&)`,
    `Start(AppInfo)`, `PreUpdate(dt)`, `Update(scene, dt)`, `LoadFrom`/`SaveTo` (options.stad
    sub-compound `Name()`), `SettingsGUI()` (Graphics Settings > Content), `Shutdown()` (reverse).
  - `render/GameRenderPath.h` - the game path (was `ProjectorRenderPath`), `beforePostprocess`.
  - `damage/Damage.h` `DamageHooks` (keyed by owner entity - see the staging-scene note in
    `../Simtary/CLAUDE.md`).
- **Moved to Simtary**: `flight/` + `libs/JSBSimC` + `flight_test`; laser / projector / optics
  + `StProjectorCS` / `StLaserCS`; crash detection (now in stFlight).
- **First plugin**: `plugins/EOS` (SDK lookup, `online/`, `EosAppSystem.cpp`, `ONLINE.md`).
  Games configure it with `st::online::EosPlatform::Get().Configure(eos)` before `st::Run`.
- **Verified**: Milistry builds and runs (flighttest crash, Cinema projector, a laser, EOS
  login); `flight_test` passes from the extension; `template` builds and runs with Stratary
  alone.

## Next plugins (tier 1: build-time, same mechanism as EOS)

Each one: move the code to `plugins/<Name>/Framework/...`, move its SDK lookup out of
`CMakeLists.txt` into `plugins/<Name>/CMakeLists.txt`, drop its link/copy lines from
`cmake/SimtaryApp.cmake` (the plugin's `LIBS` / `RUNTIME` replace them), replace the core's
direct calls with hooks, add its names to the boundary check, add `PLUGINS <Name>` to the
games that use it, and verify a build without it (`template`).

### 1. GNS (GameNetworkingSockets) - easiest of the three

- Code: `net/GnsTransport.cpp`. SDK: `SimtaryGNS` (`CMakeLists.txt` ~431, `SimtaryApp.cmake`
  link ~649 and copy ~1011).
- Core coupling: `net/NetSession.cpp:103` calls `CreateGnsTransport()`, declared in
  `net/Transport.h:58` (returns nullptr without GNS).
- Hook needed: a **transport registry** in `net/Transport.h` - `RegisterTransport(name, factory)`;
  NetSession asks for `"gns"` by name and falls back to TCP. The plugin registers its factory at
  static init. TCP stays in the core.

### 2. BHS streaming - medium

- Code: `stream/StreamHost.*`. SDK: `SimtaryBHS` (`CMakeLists.txt` ~450, link ~652, copy ~1019).
  The library itself is `Stratary/bhs/` (own code) - decide whether it moves into the plugin
  folder too.
- Core coupling (5 places):
  - `stRun.cpp:175` `ParseCommandLine()` and `:279-285` - `--stream` decides the window
    (hidden, stream-sized) BEFORE the window exists.
  - `display/DisplaySettings.cpp:151, 180-183` - a viewer counts as focus; the frame cap follows
    the stream fps.
  - `input/InputSystem.cpp:97` - a viewer counts as keyboard focus.
  - `SubWinStatus.cpp:22` - no native loading window when hidden.
  - `stApp.cpp` - Initialize / Update / Shutdown (can become an AppSystem: `Start`, `PreUpdate`).
- Hooks needed: an **early command-line / window-config hook** (runs in `st::Run` before the
  window: may change size and visibility), and a small **"remote viewer present" query** the
  core asks instead of naming StreamHost (focus, frame cap, loading window).

### 3. OpenXR - hardest

- Code: `xr/XRSystem.*`. SDK: `SimtaryOpenXR` (`CMakeLists.txt` ~383-395, link ~646, copy ~1003).
- Core coupling:
  - `render/detroit/DetroitHost.cpp:132` `RenderEyes(...)` and `:152` `MirrorTexture()` - the
    renderer draws the eyes and the desktop mirror (`ComposeMode::XRMirror`).
  - `stRun.cpp:392` `EndFrame()` - submit after present.
  - `render/GraphicsAPI.cpp:114` - `--xr` forces DX12.
  - `stApp.cpp` - Initialize / Update / Shutdown, the `xrDesktopOff_` compose mode.
- Hooks needed: **render hooks in DetroitRenderer** (extra views rendered with the game path's
  settings, an optional "compose source" for the desktop image), an **end-of-frame hook** in
  `st::Run`, and a way for a plugin to **request a graphics API** before the device is created.

### Later plugins (start as plugins from day one)

ONNX, Luau (Roblox-style API), PhysX 5, DLSS, Kinect (libfreenect), Mediapipe, Voice Chat.
Simtary's own: Chrono, ROS2, SPlisHSPlasH, LBM, LVGL, OpenUSD - inside the Simtary repo, same
pattern.

## Not plugins, but still open

1. **Vehicle split** - `vehicle/VehiclePreset` (HMMWV M1097A2 real specs) and
   `VehicleHudComponent.cpp` (`stVehicleHud`, simulated vs published table) -> Simtary.
   `stVehicle` (Jolt vehicle + stInput) stays in Stratary with a plain default.
2. **Repos (owner's steps)**:
   - Stratary: decide the licence, `LICENSE` holder, README (name, extension/plugin model), then
     the first commit and push. Submodules are staged (`.gitmodules` + 7 gitlinks), nothing
     committed.
   - Simtary: `git init`, private repo, `LICENSE` (source-available), README, `.gitignore`.
   - Milistry: repo has no commits and no remote yet.
3. **Optional rename**: `SimtaryBootstrap` / `simtary_add_app` / `SIMTARY_*` / the `simtary` MCP
   server -> Stratary names. Cosmetic; last.
4. **Tier 2 runtime plugins** (C-ABI DLLs: mods, third-party, closed or LGPL code) - design
   `stratary_plugin.h` only when a real need appears. `libs/JSBSimC` is the pattern.

## Gotchas found on the way

- Milistry is a **MODULE** build: `--target Milistry` does not rebuild `Milistry.dll`. Build
  `--target Milistry_Module Milistry`, or a stale module fails to load with error 127.
- Targets of an `EXCLUDE_FROM_ALL` extension are not in the Visual Studio solution:
  `flight_test` builds with MSBuild on `build/<preset>/_ext/Simtary/flight_test.vcxproj`.
- A component's `Start` can run while its map is still in the loader's **staging scene** -
  its `scene` pointer is not the live one. Key cross-component registries by entity, not
  (scene, entity).
- After moving folders, nested CMake caches (`build/*/_descriptor`) must go too.
- Output folders keep files from earlier builds (copy steps never delete): a plugin's DLL can
  linger next to a game that no longer uses it until a clean build.
