// The EOS plugin as a Stratary app system (stAppSystem.h): st::App drives the platform without
// knowing it exists. Start = after the network session is up (a headless --server creates
// the platform as a server), PreUpdate = every EOS callback fires on the main thread before
// the scene sees the frame, Shutdown = release + EOS_Shutdown on exit.

#include "online/EosPlatform.h"
#include "stAppSystem.h"

namespace {

struct EosAppSystem : st::AppSystem {
    const char* Name () const override { return "eos"; }

    void Start (const st::AppInfo& info) override {
        st::online::EosPlatform& eos = st::online::EosPlatform::Get();
        eos.Initialize(eos.Configured(), info.name, info.version);
    }
    void PreUpdate (float) override { st::online::EosPlatform::Get().Update(); }
    void Shutdown () override { st::online::EosPlatform::Get().Shutdown(); }
};

} // namespace

ST_REGISTER_APP_SYSTEM(EosAppSystem)
