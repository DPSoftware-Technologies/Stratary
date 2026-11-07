#ifndef SUB_WIN_SPLASH_CANVAS_H
#define SUB_WIN_SPLASH_CANVAS_H

#include <cstdint>
#include <string>
#include <vector>

// The picture of the desktop splash (AppConfig::desktopSplashImage), composed on the CPU into
// a BGRA pixel buffer: there is no GPU yet when it is first shown, and one buffer means Win32
// and X11 only have to copy it to the screen and look the same.
//
// The art is the developer's, logos included. On top of it this draws only the project name,
// the status line and the progress bar, at the positions in tools/splash_template. Those are
// in pixels of a 1440 x 810 image, scaled with the window's height.

struct SplashArt {
    std::vector<uint32_t> pixels;   // BGRA, top row first
    int width = 0;
    int height = 0;
    std::string title;
    uint32_t barColor = 0xFFFFFF00; // BGRA in memory, 0xAARRGGBB as a value
};

class SplashCanvas {
public:
    SplashCanvas(const SplashArt& art, int windowWidth, int windowHeight);

    // Compose one frame. progress is 0..100, and 0 draws no bar.
    void Render(const std::string& status, float progress);

    const uint32_t* Pixels() const { return pixels_.data(); }
    int Width() const { return width_; }
    int Height() const { return height_; }

private:
    int width_ = 0;
    int height_ = 0;
    uint32_t barColor_ = 0xFFFFFF00;
    float scale_ = 1.0f;
    std::vector<uint32_t> base_;    // the art scaled to the window, with the title on it
    std::vector<uint32_t> pixels_;
};

#endif
