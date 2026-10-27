#pragma once
// Breakable parts - the "stDamage" native component (DamageComponent.cpp), and the calls the game
// and tools reach it through.
//
//     NCI_3 = "stDamage"   NCA_3_parts = "TailRotor,Blade_1,Blade_2"   NCA_3_partHealth = 100
//
// Every listed part is a named node under the entity - a model's object, with whatever hangs
// under it. Damage lowers a part's health; at zero the part BREAKS:
//
//   - it leaves the hierarchy for a new root "Debris_<part>", which gets a dynamic rigid body
//     whose convex hull is built from the part's own vertices (a hidden proxy mesh under it)
//   - the body starts with the velocity the part had - the vehicle's, plus whatever spin a
//     DamageHooks::debrisSpin reports for it (a rotor it was on) - and a small kick away
//   - the vehicle's DamageHooks hear about it (Simtary's stFlight takes the loss into its model)
//   - the game hears about it through SetPartBrokenHandler (Milistry sets off a small explosion)
//
// DestroyVehicle breaks every part quietly and raises one `crash` event instead of ten. What
// decides a crash is not stDamage's business - whoever simulates the vehicle calls it (Simtary's
// stFlight does, from touchdown speed and rotor strikes).
//
// A sibling stInput with a "BreakPart" action breaks the next intact part in the list - a
// debug key. Editor Stop puts everything back (debris is made during Play).
// Not yet: repair, saving the damage with the vehicle, networking it.

#include "wiECS.h"
#include "wiMath.h"

#include <functional>
#include <string>
#include <vector>

namespace wi::scene { struct Scene; }

namespace st::damage {

struct PartBrokenEvent {
    wi::scene::Scene* scene = nullptr;
    wi::ecs::Entity owner = wi::ecs::INVALID_ENTITY;   // the entity carrying the stDamage
    wi::ecs::Entity debris = wi::ecs::INVALID_ENTITY;  // the new root the part now hangs under
    std::string part;
    XMFLOAT3 position = XMFLOAT3(0, 0, 0);             // the part's centre, scene space
    bool engine = false;                               // the part was the engine: a bigger bang
    bool crash = false;                                // the whole vehicle was destroyed (part = "Crash")
};
using PartBrokenHandler = std::function<void (const PartBrokenEvent&)>;
void SetPartBrokenHandler (PartBrokenHandler handler);

// Per-vehicle hooks: how something that simulates the vehicle takes part without stDamage
// knowing it exists. Set them from that component's Start, clear them in its Destroy; they are
// kept by owner entity, so the order the two components start in does not matter, and neither
// does a map that starts its components in the loader's staging scene before the merge.
struct DamageHooks {
    // Spin (rad/s, scene space) and the point it turns about, for a part about to leave as
    // debris. false = the default: a random tumble about the part's own centre.
    std::function<bool (const std::string& part, wi::ecs::Entity partEntity, XMFLOAT3& spin, XMFLOAT3& hub)> debrisSpin;
    // A part has broken off - also each part DestroyVehicle breaks.
    std::function<void (const std::string& part, wi::ecs::Entity partEntity)> partBroken;
    // DestroyVehicle ran.
    std::function<void ()> destroyed;
};
void SetHooks (wi::scene::Scene& scene, wi::ecs::Entity owner, DamageHooks hooks);
void ClearHooks (wi::scene::Scene& scene, wi::ecs::Entity owner);

// Damage every breakable part within reach of a blast of `kg` TNT at `at` (scene space).
void ApplyExplosion (wi::scene::Scene& scene, const XMFLOAT3& at, float kg);

// The stDamage on `owner`. false (with `error`) when there is none or no such intact part.
bool BreakPart (wi::scene::Scene& scene, wi::ecs::Entity owner, const std::string& part, std::string* error = nullptr);
bool DamagePart (wi::scene::Scene& scene, wi::ecs::Entity owner, const std::string& part, float amount,
                 std::string* error = nullptr);
// Every part breaks quietly, then one `crash` event. false when there is no running stDamage or
// it is already destroyed.
bool DestroyVehicle (wi::scene::Scene& scene, wi::ecs::Entity owner, const char* why);

struct PartState {
    std::string name;
    float health = 0.0f;
    bool broken = false;
};
// The parts of the stDamage on `owner`, in list order; empty when there is none running.
std::vector<PartState> Parts (wi::scene::Scene& scene, wi::ecs::Entity owner);
// One line per part: "name  health  [broken]".
std::string Status (wi::scene::Scene& scene, wi::ecs::Entity owner);

} // namespace st::damage
