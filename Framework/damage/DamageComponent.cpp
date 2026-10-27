// The "stDamage" native component - see damage/Damage.h.

#include "damage/Damage.h"

#include "stNativeComponent.h"
#include "input/InputComponent.h"

#include "wiScene.h"
#include "wiPhysics.h"
#include "wiBacklog.h"
#include "wiRandom.h"
#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <vector>

using namespace wi::scene;
using wi::ecs::Entity;
using wi::ecs::INVALID_ENTITY;

namespace st {

namespace {

st::damage::PartBrokenHandler g_handler;

// DamageHooks by owner entity: set by whoever simulates the vehicle, possibly before its
// stDamage has started, so they live here rather than on the component. Keyed by the entity
// alone - ids are unique for the whole process - because the setter may start while the map
// still sits in the loader's staging scene, and the breaking happens after the merge into the
// live one: the two see different Scene pointers for the same vehicle.
struct HookEntry {
    Entity owner;
    st::damage::DamageHooks hooks;
};
std::vector<HookEntry> g_hooks;

st::damage::DamageHooks HooksFor (Entity owner) {
    for (const HookEntry& h : g_hooks)
        if (h.owner == owner) return h.hooks;
    return {};
}

XMMATRIX WorldOf (const Scene& scene, Entity e) {
    const TransformComponent* t = scene.transforms.GetComponent(e);
    return t ? XMLoadFloat4x4(&t->world) : XMMatrixIdentity();
}

XMFLOAT3 Cross (const XMFLOAT3& a, const XMFLOAT3& b) {
    return XMFLOAT3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}

} // namespace

struct DamageComponent : NativeComponent {
    // Debris bodies and the scene hierarchy are main-thread work.
    ST_NATIVE_COMPONENT_MAIN_THREAD()

    std::string parts = "TailRotor,Blade_1,Blade_2,Blade_3,Blade_4,Stabilizer,EngineDeck,"
                        "Wheel_Main_L,Wheel_Main_R,Wheel_Tail";
    float partHealth = 100.0f;
    float maxEjectSpeed = 60.0f;    // m/s, cap on how fast debris leaves the vehicle (a blade tip is faster)
    float debrisDensity = 15.0f;    // kg per m2 of the part's surface: skin plus the structure under it
                                    //	(a UH-60 main blade comes out ~120 kg, the real one is 116)

    struct Part {
        std::string name;
        float health = 100.0f;
        bool broken = false;
        Entity debris = INVALID_ENTITY;
    };
    std::vector<Part> parts_;

    static std::vector<DamageComponent*>& Registry () {
        static std::vector<DamageComponent*> all;
        return all;
    }

    void Start () override {
        Bind(parts, "parts");
        Bind(partHealth, "partHealth");
        Bind(maxEjectSpeed, "maxEjectSpeed");
        Bind(debrisDensity, "debrisDensity");
        parts_.clear();
        std::stringstream ss(parts);
        std::string name;
        while (std::getline(ss, name, ',')) {
            name.erase(0, name.find_first_not_of(" \t"));
            name.erase(name.find_last_not_of(" \t") + 1);
            if (name.empty()) continue;
            Part p; p.name = name; p.health = partHealth;
            parts_.push_back(p);
        }
        Registry().push_back(this);
        if (const TransformComponent* t = GetComponent<TransformComponent>()) lastPos_ = t->GetPosition();
    }

    void Destroy () override {
        auto& r = Registry();
        r.erase(std::remove(r.begin(), r.end(), this), r.end());
        // The wreckage belongs to the vehicle: made at runtime, nothing else would ever remove
        //	it - a scene reload unloads only what came out of the map file.
        if (scene != nullptr) {
            for (Part& p : parts_) {
                if (p.debris != INVALID_ENTITY && scene->transforms.Contains(p.debris)) scene->Entity_Remove(p.debris, true);
                p.debris = INVALID_ENTITY;
            }
        }
    }

    void Update (float dt) override {
        if (const TransformComponent* t = GetComponent<TransformComponent>()) {
            const XMFLOAT3 p = t->GetPosition();
            if (dt > 0.0f) velocity_ = XMFLOAT3((p.x - lastPos_.x) / dt, (p.y - lastPos_.y) / dt, (p.z - lastPos_.z) / dt);
            lastPos_ = p;
        }
        // Debug key: break the next intact part.
        if (input_ == nullptr) {
            input_ = GetComponent<st::InputComponent>();
            if (input_ != nullptr) idBreak_ = input_->Find("BreakPart");
        }
        if (input_ != nullptr && idBreak_ >= 0 && input_->Pressed(idBreak_)) {
            for (const Part& p : parts_) {
                if (!p.broken && Break(p.name)) break;
            }
        }
        // A new body exists only after the physics system has run: give it its velocity then.
        for (size_t i = 0; i < pending_.size();) {
            Pending& k = pending_[i];
            RigidBodyPhysicsComponent* rb = scene->rigidbodies.GetComponent(k.debris);
            if (rb != nullptr && rb->physicsobject) {
                wi::physics::SetLinearVelocity(*rb, k.velocity);
                wi::physics::SetAngularVelocity(*rb, k.spin);
                pending_.erase(pending_.begin() + i);
            } else if (rb == nullptr || ++k.frames > 60) {
                pending_.erase(pending_.begin() + i);
            } else {
                ++i;
            }
        }
    }

    Part* Find (const std::string& name) {
        for (Part& p : parts_) if (p.name == name) return &p;
        return nullptr;
    }

    bool Damage (const std::string& name, float amount) {
        Part* p = Find(name);
        if (p == nullptr || p->broken) return false;
        p->health -= amount;
        if (p->health <= 0.0f) Break(name);
        return true;
    }

    // Centre of the part's meshes in scene space, or false when it has none.
    bool PartCentre (Entity part, XMFLOAT3& out) const {
        XMFLOAT3 lo(1e9f, 1e9f, 1e9f), hi(-1e9f, -1e9f, -1e9f);
        bool any = false;
        for (size_t i = 0; i < scene->objects.GetCount(); ++i) {
            const Entity oe = scene->objects.GetEntity(i);
            if (oe != part && !scene->Entity_IsDescendant(oe, part)) continue;
            if (i >= scene->aabb_objects.size()) break;
            const wi::primitive::AABB* box = &scene->aabb_objects[i];   // parallel to objects
            lo = XMFLOAT3(std::min(lo.x, box->_min.x), std::min(lo.y, box->_min.y), std::min(lo.z, box->_min.z));
            hi = XMFLOAT3(std::max(hi.x, box->_max.x), std::max(hi.y, box->_max.y), std::max(hi.z, box->_max.z));
            any = true;
        }
        if (any) out = XMFLOAT3((lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f, (lo.z + hi.z) * 0.5f);
        return any;
    }

    bool Break (const std::string& name) {
        Part* p = Find(name);
        if (p == nullptr || p->broken) return false;
        const Entity part = scene->Entity_FindByName(name, entity);
        if (part == INVALID_ENTITY) {
            wi::backlog::post("stDamage: no part named '" + name + "' under this entity", wi::backlog::LogLevel::Warning);
            p->broken = true;   // nothing to break; do not ask again
            return false;
        }

        // What the part was doing, before it leaves the hierarchy.
        const XMMATRIX partWorld = WorldOf(*scene, part);
        XMFLOAT3 centre;
        if (!PartCentre(part, centre)) XMStoreFloat3(&centre, partWorld.r[3]);
        // A copy: the hooks may be replaced while this part is being broken.
        const st::damage::DamageHooks hooks = HooksFor(entity);
        XMFLOAT3 spin(0, 0, 0), hub(0, 0, 0);
        const bool spun = hooks.debrisSpin && hooks.debrisSpin(name, part, spin, hub);
        const bool engine = name.find("Engine") != std::string::npos;

        // Velocity at the part's centre: the vehicle's, the spin it was carried by, and a kick outward.
        const XMFLOAT3 arm(centre.x - hub.x, centre.y - hub.y, centre.z - hub.z);
        const XMFLOAT3 swirl = spun ? Cross(spin, arm) : XMFLOAT3(0, 0, 0);
        XMFLOAT3 away(centre.x - lastPos_.x, 0.0f, centre.z - lastPos_.z);
        const float awayLen = std::sqrt(away.x * away.x + away.z * away.z);
        if (awayLen > 1e-3f) { away.x /= awayLen; away.z /= awayLen; }
        XMFLOAT3 rel(swirl.x + away.x * 3.0f, swirl.y + 2.0f, swirl.z + away.z * 3.0f);
        const float relLen = std::sqrt(rel.x * rel.x + rel.y * rel.y + rel.z * rel.z);
        if (relLen > maxEjectSpeed) {
            const float k = maxEjectSpeed / relLen;
            rel = XMFLOAT3(rel.x * k, rel.y * k, rel.z * k);
        }
        const XMFLOAT3 velocity(velocity_.x + rel.x, velocity_.y + rel.y, velocity_.z + rel.z);
        if (!spun) {
            spin = XMFLOAT3(wi::random::GetRandom(-3.0f, 3.0f), wi::random::GetRandom(-3.0f, 3.0f),
                            wi::random::GetRandom(-3.0f, 3.0f));
        }

        // A new root exactly where the part is, and the part under it.
        const Entity debris = scene->Entity_CreateTransform("Debris_" + name);
        if (TransformComponent* dt = scene->transforms.GetComponent(debris)) {
            XMVECTOR s, r, t;
            XMMatrixDecompose(&s, &r, &t, partWorld);
            XMStoreFloat3(&dt->scale_local, s);
            XMStoreFloat4(&dt->rotation_local, r);
            XMStoreFloat3(&dt->translation_local, t);
            dt->SyncWorldFromLocal(wi::scene::GetRenderOrigin());
            dt->SetDirty();
            dt->UpdateTransform();
        }
        scene->Component_Detach(part);
        scene->Component_Attach(part, debris);

        // Collision: a convex hull of the part's own vertices, in the debris root's space.
        const XMMATRIX toDebris = XMMatrixInverse(nullptr, WorldOf(*scene, debris));
        const Entity hull = scene->Entity_CreateTransform("DebrisHull_" + name);
        MeshComponent& mesh = scene->meshes.Create(hull);
        double area = 0.0;   // m2, for the mass
        for (size_t i = 0; i < scene->objects.GetCount(); ++i) {
            const Entity oe = scene->objects.GetEntity(i);
            if (oe != part && !scene->Entity_IsDescendant(oe, part)) continue;
            const MeshComponent* src = scene->meshes.GetComponent(scene->objects[i].meshID);
            if (src == nullptr) continue;
            const XMMATRIX m = WorldOf(*scene, oe) * toDebris;
            const size_t first = mesh.vertex_positions.size();
            for (const XMFLOAT3& v : src->vertex_positions) {
                XMFLOAT3 q;
                XMStoreFloat3(&q, XMVector3Transform(XMLoadFloat3(&v), m));
                mesh.vertex_positions.push_back(q);
            }
            // LOD 0 only: further levels repeat the same surface.
            uint32_t indexCount = uint32_t(src->indices.size());
            if (src->subsets_per_lod > 0 && !src->subsets.empty()) {
                indexCount = 0;
                for (uint32_t s = 0; s < src->subsets_per_lod && s < src->subsets.size(); ++s)
                    indexCount = std::max(indexCount, src->subsets[s].indexOffset + src->subsets[s].indexCount);
            }
            for (uint32_t t = 0; t + 2 < indexCount && t + 2 < src->indices.size(); t += 3) {
                const XMVECTOR a = XMLoadFloat3(&mesh.vertex_positions[first + src->indices[t]]);
                const XMVECTOR b = XMLoadFloat3(&mesh.vertex_positions[first + src->indices[t + 1]]);
                const XMVECTOR c = XMLoadFloat3(&mesh.vertex_positions[first + src->indices[t + 2]]);
                area += 0.5 * XMVectorGetX(XMVector3Length(XMVector3Cross(b - a, c - a)));
            }
        }
        scene->Component_Attach(hull, debris, true);

        RigidBodyPhysicsComponent& rb = scene->rigidbodies.Create(debris);
        if (!mesh.vertex_positions.empty()) {
            rb.shape = RigidBodyPhysicsComponent::CollisionShape::CONVEX_HULL;
            rb.mesh_source = RigidBodyPhysicsComponent::MeshSource::ProxyEntity;
            rb.collision_mesh_entity = hull;
            rb.mass = std::clamp(float(area) * debrisDensity, 5.0f, 600.0f);
        } else {
            rb.shape = RigidBodyPhysicsComponent::CollisionShape::SPHERE;
            rb.sphere.radius = 0.3f;
            rb.mass = 10.0f;
        }
        rb.friction = 0.6f;
        rb.restitution = 0.2f;
        pending_.push_back({ debris, velocity, spin, 0 });

        p->broken = true;
        p->health = 0.0f;
        p->debris = debris;
        wi::backlog::post("stDamage: '" + name + "' broke off (" + std::to_string(int(rb.mass)) + " kg debris)");

        if (hooks.partBroken) hooks.partBroken(name, part);
        if (g_handler && !quiet_) {
            st::damage::PartBrokenEvent e;
            e.scene = scene; e.owner = entity; e.debris = debris; e.part = name; e.position = centre; e.engine = engine;
            g_handler(e);
        }
        return true;
    }

    // The whole vehicle: every part comes off and the game gets one big event, not ten small ones.
    bool IsDestroyed () const { return destroyed_; }

    void DestroyVehicle (const char* why) {
        if (destroyed_) return;
        destroyed_ = true;
        wi::backlog::post(std::string("stDamage: destroyed (") + why + ")", wi::backlog::LogLevel::Warning);
        XMFLOAT3 centre = lastPos_;
        centre.y += 1.5f;
        quiet_ = true;
        for (size_t i = 0; i < parts_.size(); ++i)
            if (!parts_[i].broken) Break(parts_[i].name);
        quiet_ = false;
        const st::damage::DamageHooks hooks = HooksFor(entity);
        if (hooks.destroyed) hooks.destroyed();
        if (g_handler) {
            st::damage::PartBrokenEvent e;
            e.scene = scene; e.owner = entity; e.part = "Crash"; e.position = centre; e.engine = true; e.crash = true;
            g_handler(e);
        }
    }


    void ExplosionAt (const XMFLOAT3& at, float kg) {
        // Reach grows with the cube root of the charge, like the blast itself (10 kg: ~8.6 m).
        const float reach = 4.0f * std::cbrt(std::max(kg, 0.0f));
        if (reach <= 0.0f) return;
        std::vector<std::pair<std::string, float>> hits;
        for (const Part& p : parts_) {
            if (p.broken) continue;
            const Entity part = scene->Entity_FindByName(p.name, entity);
            XMFLOAT3 c;
            if (part == INVALID_ENTITY || !PartCentre(part, c)) continue;
            const float d = std::sqrt((c.x - at.x) * (c.x - at.x) + (c.y - at.y) * (c.y - at.y) + (c.z - at.z) * (c.z - at.z));
            if (d < reach) hits.push_back({ p.name, 250.0f * (1.0f - d / reach) });
        }
        // Breaking a part reparents it and adds entities: apply after the scan.
        for (const auto& h : hits) Damage(h.first, h.second);
    }

    void DrawDebug () override {
        ImGui::TextDisabled("Parts (break = leave the vehicle as debris)");
        for (const Part& p : parts_) {
            ImGui::PushID(p.name.c_str());
            if (p.broken) {
                ImGui::TextColored(ImVec4(1, 0.4f, 0.3f, 1), "%-14s broken", p.name.c_str());
            } else {
                ImGui::Text("%-14s %5.0f", p.name.c_str(), p.health);
                ImGui::SameLine();
                if (ImGui::SmallButton("Break")) Break(p.name);
            }
            ImGui::PopID();
        }
    }

private:
    struct Pending { Entity debris; XMFLOAT3 velocity; XMFLOAT3 spin; int frames; };
    std::vector<Pending> pending_;
    XMFLOAT3 lastPos_ = XMFLOAT3(0, 0, 0);
    XMFLOAT3 velocity_ = XMFLOAT3(0, 0, 0);
    st::InputComponent* input_ = nullptr;
    int idBreak_ = -1;
    bool destroyed_ = false, quiet_ = false;
};

namespace damage {

namespace {
DamageComponent* FindOn (Scene& scene, Entity owner) {
    for (DamageComponent* c : DamageComponent::Registry())
        if (c->scene == &scene && c->entity == owner) return c;
    return nullptr;
}
} // namespace

void SetPartBrokenHandler (PartBrokenHandler handler) { g_handler = std::move(handler); }

void SetHooks (Scene&, Entity owner, DamageHooks hooks) {
    for (HookEntry& h : g_hooks) {
        if (h.owner == owner) { h.hooks = std::move(hooks); return; }
    }
    g_hooks.push_back({ owner, std::move(hooks) });
}

void ClearHooks (Scene&, Entity owner) {
    g_hooks.erase(std::remove_if(g_hooks.begin(), g_hooks.end(), [&](const HookEntry& h) { return h.owner == owner; }),
                  g_hooks.end());
}

bool DestroyVehicle (Scene& scene, Entity owner, const char* why) {
    DamageComponent* c = FindOn(scene, owner);
    if (c == nullptr || c->IsDestroyed()) return false;
    c->DestroyVehicle(why);
    return true;
}

std::vector<PartState> Parts (Scene& scene, Entity owner) {
    std::vector<PartState> out;
    if (DamageComponent* c = FindOn(scene, owner)) {
        for (const DamageComponent::Part& p : c->parts_) out.push_back({ p.name, p.health, p.broken });
    }
    return out;
}

void ApplyExplosion (Scene& scene, const XMFLOAT3& at, float kg) {
    // Copy: a break can run the handler, which may create or destroy components.
    const std::vector<DamageComponent*> all = DamageComponent::Registry();
    for (DamageComponent* c : all)
        if (c->scene == &scene) c->ExplosionAt(at, kg);
}

bool BreakPart (Scene& scene, Entity owner, const std::string& part, std::string* error) {
    DamageComponent* c = FindOn(scene, owner);
    if (c == nullptr) { if (error) *error = "no running stDamage on that entity (Play first)"; return false; }
    if (!c->Break(part)) { if (error) *error = "no intact part '" + part + "'"; return false; }
    return true;
}

bool DamagePart (Scene& scene, Entity owner, const std::string& part, float amount, std::string* error) {
    DamageComponent* c = FindOn(scene, owner);
    if (c == nullptr) { if (error) *error = "no running stDamage on that entity (Play first)"; return false; }
    if (!c->Damage(part, amount)) { if (error) *error = "no intact part '" + part + "'"; return false; }
    return true;
}

std::string Status (Scene& scene, Entity owner) {
    DamageComponent* c = FindOn(scene, owner);
    if (c == nullptr) return "no running stDamage";
    std::string s;
    for (const DamageComponent::Part& p : c->parts_) {
        char line[96];
        std::snprintf(line, sizeof(line), "%s  %.0f%s\n", p.name.c_str(), p.health, p.broken ? "  [broken]" : "");
        s += line;
    }
    return s;
}

} // namespace damage

} // namespace st

using StDamageComponent = st::DamageComponent;
ST_REGISTER_FRAMEWORK_COMPONENT_AS(StDamageComponent, "stDamage", "Vehicle")
