#pragma once
// st::AppSystem - a frame-level system an EXTENSION adds to the app without the app knowing it.
//
// Stratary's own systems (lens flare, day/night, terrain...) are wired into st::App by hand.
// An extension (cmake/SimtaryExtension.cmake) cannot be: Stratary never includes its headers.
// It registers an AppSystem instead, and st::App drives every registered one:
//
//   Init(gamePath)    once, when the game render path exists (after the project's
//                     OnRenderPathSetup) - load shaders, hang passes on the path
//   LoadFrom(tag)     right after Init, from options.stad's sub-compound Name()
//   Start(info)       once, later: the app's own services are up (graphics device, XR,
//                     the network session) - platform services that need to know whether
//                     this is a server start here (EOS)
//   PreUpdate(dt)     every frame BEFORE the scene: with the network session, after the
//                     editor bridge - callbacks that feed the scene this frame (EOS ticks)
//   Update(scene, dt) every frame after the scene update, before the renderer's own effects,
//                     in Order() - lower first
//   SettingsGUI()     a section in Graphics Settings > Content (DevUI)
//   SaveTo(tag)       on exit, before Shutdown
//   Shutdown()        on exit, in reverse order, while the graphics device still exists
//
//   struct MySystem : st::AppSystem { ... };
//   ST_REGISTER_APP_SYSTEM(MySystem)          // one static instance, registered at static init
//
// Main thread only, like everything st::App calls.

#include "io/Nbt.h"

#include <string>
#include <vector>

namespace wi::scene { struct Scene; }

namespace st {

class GameRenderPath;

// What Start() gets to know about the app.
struct AppInfo {
    std::string name;       // AppConfig::name
    std::string version;    // ST_APP_VERSION
};

struct AppSystem {
    virtual ~AppSystem() = default;

    // Identifier and options.stad sub-compound key - keep it stable, saved settings use it.
    virtual const char* Name () const = 0;
    // Header of its Graphics Settings section; nullptr = no section.
    virtual const char* Label () const { return nullptr; }
    virtual int Order () const { return 0; }

    virtual void Init (GameRenderPath& /*gamePath*/) {}
    virtual void Start (const AppInfo& /*info*/) {}
    virtual void PreUpdate (float /*dt*/) {}
    virtual void Update (wi::scene::Scene& /*scene*/, float /*dt*/) {}
    virtual void SettingsGUI () {}
    virtual void LoadFrom (const nbt::Tag& /*in*/) {}
    virtual void SaveTo (nbt::Tag& /*out*/) const {}
    virtual void Shutdown () {}
};

namespace appsystems {

void Register (AppSystem* system);
// Every registered system, in Order().
const std::vector<AppSystem*>& All ();

// What st::App and the Detroit renderer call.
void InitAll (GameRenderPath& gamePath);
void StartAll (const AppInfo& info);
void PreUpdateAll (float dt);
void UpdateAll (wi::scene::Scene& scene, float dt);
void LoadAll ();
void SaveAll ();
void ShutdownAll ();

} // namespace appsystems

} // namespace st

#define ST_REGISTER_APP_SYSTEM(TYPE)                                                   \
    namespace {                                                                        \
    struct TYPE##_AppSystemRegistrar {                                                 \
        TYPE##_AppSystemRegistrar () { static TYPE instance; st::appsystems::Register(&instance); } \
    } TYPE##_appSystemRegistrar;                                                       \
    }
