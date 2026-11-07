// The Editor Viewport's view cube and orthographic mode. EditorUI members, kept out of
//	imeditor.cpp because none of it touches the rest of the editor except through the free
//	camera's state (camPos_ / camRot_).
//
// Hand-drawn rather than ImGuizmo::ViewManipulate, which has no face labels and, in this
//	left-handed engine, builds a top view whose up vector turns the camera 180 degrees.
//
// World convention: +Y up, +Z forward, so a vehicle's FRONT is the +Z face and the default
//	camera (looking along +Z) sees REAR.

#include "imeditor.h"

#include "wiScene.h"
#include "wiEnums.h"
#include "imgui_internal.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

using wi::ecs::Entity;
using wi::ecs::INVALID_ENTITY;

namespace {

constexpr float kHalfPi     = 1.57079632679f;
constexpr float kTwoPi      = 6.28318530718f;
constexpr float kInner      = 0.68f;  // 1 - chamfer, on a cube of half-size 1
constexpr float kCubeEye    = 7.0f;   // cube-space eye distance: only a mild perspective
constexpr float kOrbitStep  = 0.2617994f; // 15 degrees per arrow click
constexpr float kOrbitTime  = 0.25f;
constexpr float kDragSpeed  = 0.01f;  // radians per pixel
constexpr float kDefaultPitch = 0.25f;
constexpr float kMinOrthoSize = 0.02f;
constexpr float kMaxOrthoSize = 100000.0f;
constexpr float kIdleAlpha  = 0.35f;  // see-through until the pointer is on it
constexpr float kFadeSpeed  = 8.0f;   // per second

enum class Part { None, Region, ArrowLeft, ArrowRight, ArrowUp, ArrowDown, RollLeft, RollRight, Home, Ortho };

struct V3 { float x, y, z; };

float Dot(V3 a, V3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

V3 Cross(V3 a, V3 b) {
    return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}

V3 Add(V3 a, V3 b) {
    return { a.x + b.x, a.y + b.y, a.z + b.z };
}

V3 Scale(V3 a, float s) {
    return { a.x * s, a.y * s, a.z * s };
}

struct Basis { V3 right, up, forward; };

Basis BasisOf(const XMFLOAT3& rot) {
    const XMVECTOR q = XMQuaternionRotationRollPitchYaw(rot.x, rot.y, rot.z);
    XMFLOAT3 r, u, f;
    XMStoreFloat3(&r, XMVector3Rotate(XMVectorSet(1, 0, 0, 0), q));
    XMStoreFloat3(&u, XMVector3Rotate(XMVectorSet(0, 1, 0, 0), q));
    XMStoreFloat3(&f, XMVector3Rotate(XMVectorSet(0, 0, 1, 0), q));
    return { { r.x, r.y, r.z }, { u.x, u.y, u.z }, { f.x, f.y, f.z } };
}

// Cube space to screen.
struct CubeView {
    Basis basis;
    ImVec2 center;
    float scale;

    ImVec2 Project(V3 p) const {
        const float k = scale * kCubeEye / (kCubeEye + Dot(p, basis.forward));
        return ImVec2(center.x + Dot(p, basis.right) * k, center.y - Dot(p, basis.up) * k);
    }
};

// One of the 26 clickable regions of the chamfered cube, named by the direction the eye ends
//	up in when it is clicked: 6 faces (quads), 12 edges (quads), 8 corners (triangles).
int BuildRegion(const int d[3], V3 out[4]) {
    const int nonZero = (d[0] != 0) + (d[1] != 0) + (d[2] != 0);
    float p[4][3] = {};
    int count = 0;
    if (nonZero == 1) {
        const int a = d[0] != 0 ? 0 : (d[1] != 0 ? 1 : 2);
        const int u = (a + 1) % 3;
        const int v = (a + 2) % 3;
        const float corners[4][2] = { { kInner, kInner }, { kInner, -kInner }, { -kInner, -kInner }, { -kInner, kInner } };
        for (int i = 0; i < 4; ++i) {
            p[i][a] = (float)d[a];
            p[i][u] = corners[i][0];
            p[i][v] = corners[i][1];
        }
        count = 4;
    } else if (nonZero == 2) {
        const int w = d[0] == 0 ? 0 : (d[1] == 0 ? 1 : 2);
        const int a = (w + 1) % 3;
        const int b = (w + 2) % 3;
        const float sa = (float)d[a];
        const float sb = (float)d[b];
        p[0][w] = -kInner; p[0][a] = sa;          p[0][b] = sb * kInner;
        p[1][w] = -kInner; p[1][a] = sa * kInner; p[1][b] = sb;
        p[2][w] = kInner;  p[2][a] = sa * kInner; p[2][b] = sb;
        p[3][w] = kInner;  p[3][a] = sa;          p[3][b] = sb * kInner;
        count = 4;
    } else {
        for (int i = 0; i < 3; ++i) {
            for (int axis = 0; axis < 3; ++axis) {
                p[i][axis] = (float)d[axis] * (axis == i ? 1.0f : kInner);
            }
        }
        count = 3;
    }
    for (int i = 0; i < count; ++i) {
        out[i] = { p[i][0], p[i][1], p[i][2] };
    }
    return count;
}

bool PointInConvex(ImVec2 m, const ImVec2* poly, int count) {
    bool pos = false;
    bool neg = false;
    for (int i = 0; i < count; ++i) {
        const ImVec2 a = poly[i];
        const ImVec2 b = poly[(i + 1) % count];
        const float c = (b.x - a.x) * (m.y - a.y) - (b.y - a.y) * (m.x - a.x);
        pos = pos || c > 0.0f;
        neg = neg || c < 0.0f;
    }
    return !(pos && neg);
}

ImU32 Shade(float base, float light, bool hot) {
    const float v = base * (0.55f + 0.45f * light);
    if (hot) {
        return IM_COL32((int)(v * 0.55f), (int)(v * 0.95f), (int)std::min(255.0f, v * 1.6f), 240);
    }
    return IM_COL32((int)v, (int)v, (int)v, 235);
}

// ImGui has no text on a plane, so the label is laid out flat and its vertices moved onto the
//	face afterwards. `unitsPerPixel` is shared by all faces so every label is the same size.
void DrawFaceLabel(ImDrawList* dl, const CubeView& view, const char* text, V3 normal, V3 faceUp,
    float unitsPerPixel, ImU32 col) {
    ImFont* font = ImGui::GetFont();
    const float px = ImGui::GetFontSize();
    const ImVec2 size = font->CalcTextSizeA(px, FLT_MAX, 0.0f, text);
    const V3 faceRight = Cross(faceUp, Scale(normal, -1.0f));
    const ImVec2 origin = view.center; // inside the clip rect: ImGui culls lines outside it
    const int first = dl->VtxBuffer.Size;
    dl->AddText(font, px, origin, col, text);
    for (int i = first; i < dl->VtxBuffer.Size; ++i) {
        ImDrawVert& v = dl->VtxBuffer[i];
        const float u = (v.pos.x - origin.x - size.x * 0.5f) * unitsPerPixel;
        const float w = (size.y * 0.5f - (v.pos.y - origin.y)) * unitsPerPixel;
        v.pos = view.Project(Add(normal, Add(Scale(faceRight, u), Scale(faceUp, w))));
    }
}

const char* FaceLabel(const int d[3], V3& faceUp) {
    faceUp = { 0.0f, 1.0f, 0.0f };
    if (d[0] > 0) return "RIGHT";
    if (d[0] < 0) return "LEFT";
    if (d[2] > 0) return "FRONT";
    if (d[2] < 0) return "REAR";
    faceUp = { 0.0f, 0.0f, d[1] > 0 ? 1.0f : -1.0f };
    return d[1] > 0 ? "TOP" : "BOTTOM";
}

const char* PartTooltip(Part part) {
    switch (part) {
    case Part::ArrowLeft:  return "Orbit left 15 degrees";
    case Part::ArrowRight: return "Orbit right 15 degrees";
    case Part::ArrowUp:    return "Orbit up 15 degrees";
    case Part::ArrowDown:  return "Orbit down 15 degrees";
    case Part::RollLeft:   return "Roll the view left 15 degrees";
    case Part::RollRight:  return "Roll the view right 15 degrees";
    case Part::Home:       return "Home view";
    default:               return nullptr;
    }
}

} // namespace

float st::EditorUI::CamPivotDistance(wi::scene::Scene& scene, Entity selected) const {
    const XMVECTOR pos = XMLoadFloat3(&camPos_);
    const XMVECTOR q = XMQuaternionRotationRollPitchYaw(camRot_.x, camRot_.y, camRot_.z);
    const XMVECTOR forward = XMVector3Rotate(XMVectorSet(0, 0, 1, 0), q);

    if (selected != INVALID_ENTITY) {
        if (const wi::scene::TransformComponent* t = scene.transforms.GetComponent(selected)) {
            const XMFLOAT3 p = t->GetPosition();
            const float depth = XMVectorGetX(XMVector3Dot(XMVectorSubtract(XMLoadFloat3(&p), pos), forward));
            if (depth > 0.5f) {
                return depth;
            }
        }
    }

    // Nothing selected: turn around whatever is in the middle of the view.
    const wi::primitive::Ray ray(pos, forward);
    const wi::scene::PickResult hit = wi::scene::Pick(ray,
        wi::enums::FILTER_OPAQUE | wi::enums::FILTER_TRANSPARENT, ~0u, scene);
    if (hit.entity != INVALID_ENTITY && hit.distance > 0.5f) {
        return hit.distance;
    }
    return camPivotDist_;
}

void st::EditorUI::StartCamOrbit(XMFLOAT3 toRot, float distance) {
    const XMVECTOR q = XMQuaternionRotationRollPitchYaw(camRot_.x, camRot_.y, camRot_.z);
    const XMVECTOR forward = XMVector3Rotate(XMVectorSet(0, 0, 1, 0), q);
    XMStoreFloat3(&camOrbitPivot_, XMVectorAdd(XMLoadFloat3(&camPos_), XMVectorScale(forward, distance)));
    camPivotDist_ = distance;
    camOrbitFrom_ = camRot_;
    toRot.y = camRot_.y + std::remainder(toRot.y - camRot_.y, kTwoPi); // the short way round
    camOrbitTo_ = toRot;
    camOrbitT_ = 0.0f;
    camFlyT_ = -1.0f;
}

void st::EditorUI::PlaceCamOnOrbit() {
    const XMVECTOR q = XMQuaternionRotationRollPitchYaw(camRot_.x, camRot_.y, camRot_.z);
    const XMVECTOR forward = XMVector3Rotate(XMVectorSet(0, 0, 1, 0), q);
    XMStoreFloat3(&camPos_, XMVectorSubtract(XMLoadFloat3(&camOrbitPivot_), XMVectorScale(forward, camPivotDist_)));
}

void st::EditorUI::StepCamOrbit(float dt) {
    if (camOrbitT_ < 0.0f) {
        return;
    }
    camOrbitT_ = std::min(1.0f, camOrbitT_ + dt / kOrbitTime);
    const float s = 1.0f - (1.0f - camOrbitT_) * (1.0f - camOrbitT_) * (1.0f - camOrbitT_); // ease out
    XMStoreFloat3(&camRot_, XMVectorLerp(XMLoadFloat3(&camOrbitFrom_), XMLoadFloat3(&camOrbitTo_), s));
    PlaceCamOnOrbit();
    if (camOrbitT_ >= 1.0f) {
        camOrbitT_ = -1.0f;
    }
}

void st::EditorUI::OrbitToDirection(wi::scene::Scene& scene, Entity selected, int dx, int dy, int dz) {
    const float len = std::sqrt((float)(dx * dx + dy * dy + dz * dz));
    if (len <= 0.0f) {
        return;
    }
    XMFLOAT3 to;
    to.z = 0.0f;
    to.x = std::asin((float)dy / len); // the eye above the pivot looks down: positive pitch
    if (dx == 0 && dz == 0) {
        // Straight up or down has no yaw of its own: keep the nearest square-on one.
        to.y = std::round(camRot_.y / kHalfPi) * kHalfPi;
    } else {
        to.y = std::atan2((float)-dx, (float)-dz);
    }
    StartCamOrbit(to, CamPivotDistance(scene, selected));
}

void st::EditorUI::SetCamOrtho(wi::scene::Scene& scene, Entity selected, bool ortho) {
    if (ortho == camOrtho_) {
        return;
    }
    const float tanHalf = std::tan(camFov_ * 0.5f);
    if (ortho) {
        // The same height at the depth being looked at, so the switch does not jump.
        camPivotDist_ = CamPivotDistance(scene, selected);
        camOrthoSize_ = std::clamp(2.0f * camPivotDist_ * tanHalf, kMinOrthoSize, kMaxOrthoSize);
    } else {
        // Zooming in ortho moved nothing, so bring the eye to where that zoom would have put
        //	it: the pivot stays put and keeps its size on screen.
        const XMVECTOR q = XMQuaternionRotationRollPitchYaw(camRot_.x, camRot_.y, camRot_.z);
        const XMVECTOR forward = XMVector3Rotate(XMVectorSet(0, 0, 1, 0), q);
        const float distance = camOrthoSize_ / (2.0f * tanHalf);
        XMStoreFloat3(&camPos_, XMVectorAdd(XMLoadFloat3(&camPos_), XMVectorScale(forward, camPivotDist_ - distance)));
        camPivotDist_ = distance;
    }
    camOrtho_ = ortho;
    camFlyT_ = -1.0f;
    camOrbitT_ = -1.0f;
}

float st::EditorUI::ViewHeightAtPivot() const {
    return camOrtho_ ? camOrthoSize_ : 2.0f * camPivotDist_ * std::tan(camFov_ * 0.5f);
}

bool st::EditorUI::DrawViewCube(wi::scene::Scene& scene, Entity selected, ImVec2 imagePos, ImVec2 imageSize) {
    viewImageHeight_ = imageSize.y;

    // Queued from outside (toolbar, bridge), which do not have the scene and selection at hand.
    if (viewOrthoRequest_ >= 0) {
        SetCamOrtho(scene, selected, viewOrthoRequest_ != 0);
        viewOrthoRequest_ = -1;
    }
    if (viewFaceRequested_) {
        OrbitToDirection(scene, selected, viewFaceRequest_.x, viewFaceRequest_.y, viewFaceRequest_.z);
        viewFaceRequested_ = false;
    }

    const float ui = ImGui::GetFontSize() / 13.0f;
    const float panel = 112.0f * ui;
    if (imageSize.x < panel * 2.0f || imageSize.y < panel * 1.5f) {
        return false; // the panel is too small to give up a corner
    }

    const ImVec2 panelMin(imagePos.x + imageSize.x - panel - 6.0f, imagePos.y + 6.0f);
    const ImVec2 panelMax(panelMin.x + panel, panelMin.y + panel);
    const ImVec2 center(panelMin.x + panel * 0.5f, panelMin.y + panel * 0.5f);

    ImGui::SetCursorScreenPos(panelMin);
    // An item, unlike the rest of the viewport: while it is hovered ImGuizmo refuses to start a
    //	drag, and while it is active the free camera does not see the mouse.
    const bool clicked = ImGui::InvisibleButton("##viewcube", ImVec2(panel, panel));
    const bool itemHot = ImGui::IsItemHovered() || clicked;
    const bool itemActive = ImGui::IsItemActive();
    const bool activated = ImGui::IsItemActivated();
    const bool released = ImGui::IsItemDeactivated();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const int firstVertex = dl->VtxBuffer.Size; // everything below fades together, at the end
    const ImVec2 mouse = ImGui::GetIO().MousePos;

    CubeView view;
    view.basis = BasisOf(camRot_);
    view.center = center;
    view.scale = panel * 0.235f;
    const V3 eye = Scale(view.basis.forward, -kCubeEye);

    // Hit test the small controls first, then the cube.
    const float ring = panel * 0.44f;
    const float dx = mouse.x - center.x;
    const float dy = mouse.y - center.y;
    const float radius = std::sqrt(dx * dx + dy * dy);
    const float angle = std::atan2(dy, dx);
    const float grab = 9.0f * ui;
    const ImVec2 orthoPos(panelMax.x - 9.0f * ui, panelMin.y + 9.0f * ui);
    const ImVec2 homePos(panelMax.x - 11.0f * ui, panelMax.y - 11.0f * ui);
    const auto inReach = [&](ImVec2 p) {
        return std::abs(mouse.x - p.x) < grab && std::abs(mouse.y - p.y) < grab;
    };
    const float rollInner = kHalfPi + 0.3f;
    const float rollOuter = kHalfPi + 1.0f;

    Part part = Part::None;
    int hotRegion[3] = { 0, 0, 0 };
    if (itemHot || itemActive) {
        if (inReach(orthoPos)) {
            part = Part::Ortho;
        } else if (inReach(homePos)) {
            part = Part::Home;
        } else if (inReach(ImVec2(center.x - ring, center.y))) {
            part = Part::ArrowLeft;
        } else if (inReach(ImVec2(center.x + ring, center.y))) {
            part = Part::ArrowRight;
        } else if (inReach(ImVec2(center.x, center.y - ring))) {
            part = Part::ArrowUp;
        } else if (inReach(ImVec2(center.x, center.y + ring))) {
            part = Part::ArrowDown;
        } else if (std::abs(radius - ring) < grab && angle < -rollInner && angle > -rollOuter) {
            part = Part::RollLeft;
        } else if (std::abs(radius - ring) < grab && angle > -kHalfPi + 0.3f && angle < -kHalfPi + 1.0f) {
            part = Part::RollRight;
        }
    }

    // The cube. Convex, so back-face culling alone gets the draw order right.
    int labelDirs[6][3];
    int labelCount = 0;
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            for (int z = -1; z <= 1; ++z) {
                if (x == 0 && y == 0 && z == 0) {
                    continue;
                }
                const int d[3] = { x, y, z };
                V3 pts[4];
                const int count = BuildRegion(d, pts);
                V3 mid = { 0.0f, 0.0f, 0.0f };
                for (int i = 0; i < count; ++i) {
                    mid = Add(mid, pts[i]);
                }
                mid = Scale(mid, 1.0f / (float)count);
                const int nonZero = (x != 0) + (y != 0) + (z != 0);
                const V3 normal = Scale(V3{ (float)x, (float)y, (float)z }, 1.0f / std::sqrt((float)nonZero));
                const V3 toFace = Add(mid, Scale(eye, -1.0f));
                if (Dot(normal, toFace) >= 0.0f) {
                    continue;
                }
                ImVec2 screen[4];
                for (int i = 0; i < count; ++i) {
                    screen[i] = view.Project(pts[i]);
                }
                bool hot = false;
                if ((itemHot || itemActive) && part == Part::None && PointInConvex(mouse, screen, count)) {
                    part = Part::Region;
                    hotRegion[0] = x;
                    hotRegion[1] = y;
                    hotRegion[2] = z;
                    hot = true;
                }
                const float light = std::max(0.0f, -Dot(normal, view.basis.forward));
                const float base = nonZero == 1 ? 150.0f : (nonZero == 2 ? 118.0f : 100.0f);
                dl->AddConvexPolyFilled(screen, count, Shade(base, light, hot && !viewCubeDragged_));
                dl->AddPolyline(screen, count, IM_COL32(25, 25, 25, 200), ImDrawFlags_Closed, 1.0f);
                if (nonZero == 1) {
                    labelDirs[labelCount][0] = x;
                    labelDirs[labelCount][1] = y;
                    labelDirs[labelCount][2] = z;
                    ++labelCount;
                }
            }
        }
    }

    // Labels, all at the size that lets the longest one fit a face.
    const float longest = ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, 0.0f, "BOTTOM").x;
    const float unitsPerPixel = (kInner * 1.8f) / std::max(1.0f, longest);
    for (int i = 0; i < labelCount; ++i) {
        const int* d = labelDirs[i];
        V3 faceUp;
        const char* label = FaceLabel(d, faceUp);
        DrawFaceLabel(dl, view, label, V3{ (float)d[0], (float)d[1], (float)d[2] }, faceUp,
            unitsPerPixel, IM_COL32(18, 18, 18, 240));
    }

    // World axes, from the middle of the cube: X red, Y green, Z blue.
    const ImVec2 axisStart = view.Project(V3{ 0.0f, 0.0f, 0.0f });
    dl->AddLine(axisStart, view.Project(V3{ 1.55f, 0.0f, 0.0f }), IM_COL32(235, 70, 70, 255), 1.5f * ui);
    dl->AddLine(axisStart, view.Project(V3{ 0.0f, 1.55f, 0.0f }), IM_COL32(90, 210, 90, 255), 1.5f * ui);
    dl->AddLine(axisStart, view.Project(V3{ 0.0f, 0.0f, 1.55f }), IM_COL32(80, 120, 245, 255), 1.5f * ui);

    // Controls around it.
    const auto tint = [&](Part p) {
        return part == p ? IM_COL32(235, 235, 235, 255) : IM_COL32(165, 165, 165, 170);
    };
    const float tip = 7.0f * ui;
    const float wing = 6.0f * ui;
    dl->AddTriangleFilled(ImVec2(center.x, center.y - ring - tip), ImVec2(center.x - wing, center.y - ring + tip * 0.5f),
        ImVec2(center.x + wing, center.y - ring + tip * 0.5f), tint(Part::ArrowUp));
    dl->AddTriangleFilled(ImVec2(center.x, center.y + ring + tip), ImVec2(center.x + wing, center.y + ring - tip * 0.5f),
        ImVec2(center.x - wing, center.y + ring - tip * 0.5f), tint(Part::ArrowDown));
    dl->AddTriangleFilled(ImVec2(center.x - ring - tip, center.y), ImVec2(center.x - ring + tip * 0.5f, center.y + wing),
        ImVec2(center.x - ring + tip * 0.5f, center.y - wing), tint(Part::ArrowLeft));
    dl->AddTriangleFilled(ImVec2(center.x + ring + tip, center.y), ImVec2(center.x + ring - tip * 0.5f, center.y - wing),
        ImVec2(center.x + ring - tip * 0.5f, center.y + wing), tint(Part::ArrowRight));

    // The two curved arrows beside the top one, each ending in a head pointing down its side.
    const auto rollArrow = [&](float from, float to, Part p) {
        dl->PathClear();
        dl->PathArcTo(center, ring, from, to, 12);
        dl->PathStroke(tint(p), ImDrawFlags_None, 3.0f * ui);
        const float side = to < from ? 1.0f : -1.0f;
        const ImVec2 radial(std::cos(to), std::sin(to));
        const ImVec2 along(std::sin(to) * side, -std::cos(to) * side);
        const ImVec2 end(center.x + radial.x * ring, center.y + radial.y * ring);
        dl->AddTriangleFilled(
            ImVec2(end.x + along.x * tip, end.y + along.y * tip),
            ImVec2(end.x + radial.x * wing, end.y + radial.y * wing),
            ImVec2(end.x - radial.x * wing, end.y - radial.y * wing), tint(p));
    };
    rollArrow(-rollInner, -rollOuter, Part::RollLeft);
    rollArrow(-kHalfPi + 0.3f, -kHalfPi + 1.0f, Part::RollRight);

    // Solid dot = orthographic, ring = perspective.
    if (camOrtho_) {
        dl->AddCircleFilled(orthoPos, 5.0f * ui, tint(Part::Ortho));
    } else {
        dl->AddCircle(orthoPos, 5.0f * ui, tint(Part::Ortho), 0, 1.5f * ui);
    }

    // Home: a small cube seen from a corner.
    {
        const float h = 6.0f * ui;
        const ImVec2 top(homePos.x, homePos.y - h);
        const ImVec2 upperRight(homePos.x + h * 0.87f, homePos.y - h * 0.5f);
        const ImVec2 lowerRight(homePos.x + h * 0.87f, homePos.y + h * 0.5f);
        const ImVec2 bottom(homePos.x, homePos.y + h);
        const ImVec2 lowerLeft(homePos.x - h * 0.87f, homePos.y + h * 0.5f);
        const ImVec2 upperLeft(homePos.x - h * 0.87f, homePos.y - h * 0.5f);
        const ImU32 c = tint(Part::Home);
        dl->AddLine(top, upperRight, c, ui);
        dl->AddLine(upperRight, lowerRight, c, ui);
        dl->AddLine(lowerRight, bottom, c, ui);
        dl->AddLine(bottom, lowerLeft, c, ui);
        dl->AddLine(lowerLeft, upperLeft, c, ui);
        dl->AddLine(upperLeft, top, c, ui);
        dl->AddLine(homePos, upperLeft, c, ui);
        dl->AddLine(homePos, upperRight, c, ui);
        dl->AddLine(homePos, bottom, c, ui);
    }

    // Press on the cube and drag = free orbit. A press that does not travel is a click.
    if (activated) {
        viewCubeDragged_ = false;
        viewCubeDownOnCube_ = part == Part::Region;
    }
    if (itemActive && viewCubeDownOnCube_) {
        const ImVec2 delta = ImGui::GetIO().MouseDelta;
        if (!viewCubeDragged_ && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 3.0f * ui)) {
            viewCubeDragged_ = true;
            StartCamOrbit(camRot_, CamPivotDistance(scene, selected));
            camOrbitT_ = -1.0f; // the pivot is all that was wanted; the mouse drives from here
        }
        if (viewCubeDragged_ && (delta.x != 0.0f || delta.y != 0.0f)) {
            camRot_.y += delta.x * kDragSpeed;
            camRot_.x = std::clamp(camRot_.x + delta.y * kDragSpeed, -kHalfPi, kHalfPi);
            PlaceCamOnOrbit();
        }
        if (viewCubeDragged_) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        }
    }

    if (released || clicked) {
        if (!viewCubeDragged_ && clicked) {
            // Consecutive arrow clicks add up instead of restarting from wherever the last
            //	animation happens to have reached.
            XMFLOAT3 rot = camOrbitT_ >= 0.0f ? camOrbitTo_ : camRot_;
            rot.y = std::remainder(rot.y, kTwoPi);
            bool orbit = true;
            switch (part) {
            case Part::Region:
                OrbitToDirection(scene, selected, hotRegion[0], hotRegion[1], hotRegion[2]);
                orbit = false;
                break;
            case Part::ArrowLeft:  rot.y += kOrbitStep; break;
            case Part::ArrowRight: rot.y -= kOrbitStep; break;
            case Part::ArrowUp:    rot.x = std::min(rot.x + kOrbitStep, kHalfPi); break;
            case Part::ArrowDown:  rot.x = std::max(rot.x - kOrbitStep, -kHalfPi); break;
            case Part::RollLeft:   rot.z -= kOrbitStep; break;
            case Part::RollRight:  rot.z += kOrbitStep; break;
            case Part::Home:       rot = XMFLOAT3(kDefaultPitch, 0.0f, 0.0f); break;
            case Part::Ortho:
                SetCamOrtho(scene, selected, !camOrtho_);
                orbit = false;
                break;
            default:
                orbit = false;
                break;
            }
            if (orbit) {
                StartCamOrbit(rot, CamPivotDistance(scene, selected));
            }
        }
        viewCubeDragged_ = false;
        viewCubeDownOnCube_ = false;
    }

    if (itemHot && !itemActive) {
        if (part == Part::Ortho) {
            ImGui::SetTooltip("%s", camOrtho_ ? "Orthographic: click for perspective" : "Perspective: click for orthographic");
        } else if (const char* tip_text = PartTooltip(part)) {
            ImGui::SetTooltip("%s", tip_text);
        }
    }

    // One alpha over all of it, labels and arrows included, rather than threading it through
    //	every colour above.
    const float fadeTarget = (itemHot || itemActive) ? 1.0f : kIdleAlpha;
    const float fadeStep = kFadeSpeed * ImGui::GetIO().DeltaTime;
    viewCubeAlpha_ += std::clamp(fadeTarget - viewCubeAlpha_, -fadeStep, fadeStep);
    for (int i = firstVertex; i < dl->VtxBuffer.Size; ++i) {
        ImU32& col = dl->VtxBuffer[i].col;
        const ImU32 alpha = (ImU32)(((col >> IM_COL32_A_SHIFT) & 0xFF) * viewCubeAlpha_);
        col = (col & ~IM_COL32_A_MASK) | (alpha << IM_COL32_A_SHIFT);
    }

    return itemHot || itemActive;
}
