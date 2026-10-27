#pragma once
// st::GameRenderPath - the game's 3D render path (Detroit: a wi::RenderPath3D).
//
// Two things on top of RenderPath3D:
//   - forcedResolution: the editor's "Game Viewport > Resolution"
//   - beforePostprocess: passes that record at the top of the post process chain, where this
//     frame's render data is already uploaded. A system that needs its own depth maps before
//     its custom post process samples them hangs them here (Simtary's projector shadows do).
//
// Extensions add to beforePostprocess and to RenderPath3D::custom_post_processes from their
// st::AppSystem::Init (stAppSystem.h); the path itself knows nothing about them.

#include "Simtary.h"

#include <functional>
#include <vector>

namespace st {

class GameRenderPath : public wi::RenderPath3D {
public:
	// Editor "Game Viewport > Resolution": render the game at a fixed size instead of at
	// the window canvas size, the way a Unity/Unreal game view does. (0, 0) follows the
	// canvas, which is what a shipping build always does - st::EditorUI is the only thing
	// that sets this, and it clears it again when editor mode leaves.
	//
	// Re-applied here rather than assigned once because wi::Application::Update calls
	// activePath->init(canvas) EVERY frame, which overwrites width/height. PreUpdate is the
	// first virtual call after that init and still runs before RenderPath2D::Update notices
	// the size and reallocates, so this is the one place the override survives.
	XMUINT2 forcedResolution = XMUINT2(0, 0);

	// Recorded first thing in RenderPostprocessChain, in the order they were added.
	std::vector<std::function<void(wi::graphics::CommandList)>> beforePostprocess;

	void PreUpdate() override {
		if (forcedResolution.x >= 16 && forcedResolution.y >= 16) {
			width = forcedResolution.x;
			height = forcedResolution.y;
		}
		wi::RenderPath3D::PreUpdate();
	}

	void RenderPostprocessChain(wi::graphics::CommandList cmd) const override {
		for (const auto& pass : beforePostprocess) {
			if (pass) pass(cmd);
		}
		wi::RenderPath3D::RenderPostprocessChain(cmd);
	}
};

} // namespace st
