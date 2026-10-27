#include "stAppSystem.h"

#include "io/SettingsManager.h"

#include <algorithm>

namespace st::appsystems {

namespace {

// Function-local: registration runs during static initialisation, in whatever order the
//	linker put the translation units.
std::vector<AppSystem*>& Systems () {
    static std::vector<AppSystem*> all;
    return all;
}

} // namespace

void Register (AppSystem* system) {
    if (system == nullptr) return;
    std::vector<AppSystem*>& all = Systems();
    if (std::find(all.begin(), all.end(), system) != all.end()) return;
    all.push_back(system);
    std::stable_sort(all.begin(), all.end(), [](const AppSystem* a, const AppSystem* b) { return a->Order() < b->Order(); });
}

const std::vector<AppSystem*>& All () { return Systems(); }

void InitAll (GameRenderPath& gamePath) {
    for (AppSystem* s : Systems()) s->Init(gamePath);
}

void UpdateAll (wi::scene::Scene& scene, float dt) {
    for (AppSystem* s : Systems()) s->Update(scene, dt);
}

void LoadAll () {
    for (AppSystem* s : Systems()) s->LoadFrom(st::SettingsManager::Get().SubCompound(s->Name()));
}

void SaveAll () {
    for (AppSystem* s : Systems()) s->SaveTo(st::SettingsManager::Get().SubCompound(s->Name()));
}

void ShutdownAll () {
    // Reverse order: a later system may hold on to an earlier one (lasers trace through optics).
    const std::vector<AppSystem*>& all = Systems();
    for (auto it = all.rbegin(); it != all.rend(); ++it) (*it)->Shutdown();
}

} // namespace st::appsystems
