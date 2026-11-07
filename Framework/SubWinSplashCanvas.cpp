#include "SubWinSplashCanvas.h"
#include "fonts/DinishFonts.h"

#include "Utility/stb_truetype.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

// tools/splash_template/dsplash_template.svg, in pixels of its 1440 x 810 image.
constexpr float kDesignHeight   = 810.0f;
constexpr float kDesignWidth    = 1440.0f;
constexpr float kTextX          = 28.0f;
constexpr float kTextRight      = 1008.0f;
constexpr float kTitleBaseline  = 742.0f;
constexpr float kTitleSize      = 40.0f;
constexpr float kStatusBaseline = 782.0f;
constexpr float kStatusSize     = 26.0f;
constexpr float kBarTop         = 802.0f;

struct Face {
    stbtt_fontinfo info = {};
    bool ok = false;

    Face(const unsigned char* data) {
        ok = stbtt_InitFont(&info, data, stbtt_GetFontOffsetForIndex(data, 0)) != 0;
    }
};

// One code point out of UTF-8. Asset paths can hold anything, and a bad byte must not stop the
// line, so it just comes back as itself.
int NextCodepoint(const std::string& text, size_t& i) {
    const unsigned char c = (unsigned char)text[i++];
    int extra = c >= 0xF0 ? 3 : (c >= 0xE0 ? 2 : (c >= 0xC0 ? 1 : 0));
    if (extra == 0 || i + extra > text.size()) {
        return c;
    }
    int code = c & (0x3F >> extra);
    while (extra-- > 0) {
        code = (code << 6) | ((unsigned char)text[i++] & 0x3F);
    }
    return code;
}

float TextWidth(const Face& face, const std::string& text, float size) {
    const float scale = stbtt_ScaleForMappingEmToPixels(&face.info, size);
    float x = 0.0f;
    int previous = 0;
    for (size_t i = 0; i < text.size();) {
        const int code = NextCodepoint(text, i);
        int advance = 0;
        int bearing = 0;
        stbtt_GetCodepointHMetrics(&face.info, code, &advance, &bearing);
        if (previous != 0) {
            x += stbtt_GetCodepointKernAdvance(&face.info, previous, code) * scale;
        }
        x += advance * scale;
        previous = code;
    }
    return x;
}

// Drop the middle of an over-long line: it is usually a path, where the folder and the file
// name are the parts that say something. Same rule as the plain loading window.
std::string ElideMiddle(const Face& face, const std::string& text, float size, float maxWidth) {
    if (TextWidth(face, text, size) <= maxWidth) {
        return text;
    }
    const std::string gap = "...";
    for (size_t drop = 1; drop + 4 < text.size(); ++drop) {
        const size_t front = (text.size() - drop) / 2;
        const size_t back = text.size() - drop - front;
        const std::string candidate = text.substr(0, front) + gap + text.substr(text.size() - back);
        if (TextWidth(face, candidate, size) <= maxWidth) {
            return candidate;
        }
    }
    return gap;
}

// White text. Stays inside the buffer, whatever the font does.
void DrawText(std::vector<uint32_t>& target, int width, int height, const Face& face,
    const std::string& text, float x, float baseline, float size) {
    const float scale = stbtt_ScaleForMappingEmToPixels(&face.info, size);
    std::vector<unsigned char> glyph;
    int previous = 0;
    for (size_t i = 0; i < text.size();) {
        const int code = NextCodepoint(text, i);
        if (previous != 0) {
            x += stbtt_GetCodepointKernAdvance(&face.info, previous, code) * scale;
        }
        const float fraction = x - std::floor(x);
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        stbtt_GetCodepointBitmapBoxSubpixel(&face.info, code, scale, scale, fraction, 0.0f, &x0, &y0, &x1, &y1);
        const int w = x1 - x0;
        const int h = y1 - y0;
        if (w > 0 && h > 0) {
            glyph.resize(size_t(w) * h);
            stbtt_MakeCodepointBitmapSubpixel(&face.info, glyph.data(), w, h, w, scale, scale, fraction, 0.0f, code);
            const int left = (int)std::floor(x) + x0;
            const int top = (int)std::floor(baseline) + y0;
            for (int gy = 0; gy < h; ++gy) {
                const int py = top + gy;
                if (py < 0 || py >= height) {
                    continue;
                }
                for (int gx = 0; gx < w; ++gx) {
                    const int px = left + gx;
                    if (px < 0 || px >= width) {
                        continue;
                    }
                    const uint32_t a = glyph[size_t(gy) * w + gx];
                    if (a == 0) {
                        continue;
                    }
                    uint32_t& d = target[size_t(py) * width + px];
                    const uint32_t b = ((d & 0xFF) * (255 - a) + 255 * a) / 255;
                    const uint32_t g = (((d >> 8) & 0xFF) * (255 - a) + 255 * a) / 255;
                    const uint32_t r = (((d >> 16) & 0xFF) * (255 - a) + 255 * a) / 255;
                    d = 0xFF000000u | (r << 16) | (g << 8) | b;
                }
            }
        }
        int advance = 0;
        int bearing = 0;
        stbtt_GetCodepointHMetrics(&face.info, code, &advance, &bearing);
        x += advance * scale;
        previous = code;
    }
}

// Each window pixel is the average of the image pixels under it. The usual case is exactly 2:1.
void ScaleArt(const SplashArt& art, std::vector<uint32_t>& out, int width, int height) {
    out.resize(size_t(width) * height);
    for (int y = 0; y < height; ++y) {
        const int sy0 = y * art.height / height;
        const int sy1 = std::max(sy0 + 1, (y + 1) * art.height / height);
        for (int x = 0; x < width; ++x) {
            const int sx0 = x * art.width / width;
            const int sx1 = std::max(sx0 + 1, (x + 1) * art.width / width);
            uint32_t b = 0, g = 0, r = 0, n = 0;
            for (int sy = sy0; sy < sy1 && sy < art.height; ++sy) {
                const uint32_t* row = art.pixels.data() + size_t(sy) * art.width;
                for (int sx = sx0; sx < sx1 && sx < art.width; ++sx) {
                    const uint32_t c = row[sx];
                    b += c & 0xFF;
                    g += (c >> 8) & 0xFF;
                    r += (c >> 16) & 0xFF;
                    ++n;
                }
            }
            n = std::max(n, 1u);
            out[size_t(y) * width + x] = 0xFF000000u | ((r / n) << 16) | ((g / n) << 8) | (b / n);
        }
    }
}

} // namespace

SplashCanvas::SplashCanvas(const SplashArt& art, int windowWidth, int windowHeight)
    : width_(windowWidth), height_(windowHeight), barColor_(art.barColor),
      scale_((float)windowHeight / kDesignHeight) {
    ScaleArt(art, base_, width_, height_);
    pixels_ = base_;
    const Face bold(st::fonts::kDinishBold);
    if (bold.ok) {
        DrawText(base_, width_, height_, bold, art.title, kTextX * scale_, kTitleBaseline * scale_, kTitleSize * scale_);
    }
}

void SplashCanvas::Render(const std::string& status, float progress) {
    std::memcpy(pixels_.data(), base_.data(), pixels_.size() * sizeof(uint32_t));

    const Face light(st::fonts::kDinishLight);
    if (light.ok && !status.empty()) {
        const float size = kStatusSize * scale_;
        const float x = kTextX * scale_;
        const float maxWidth = width_ * (kTextRight / kDesignWidth) - x;
        DrawText(pixels_, width_, height_, light, ElideMiddle(light, status, size, maxWidth),
            x, kStatusBaseline * scale_, size);
    }

    const int barTop = std::min(height_ - 1, (int)std::round(kBarTop * scale_));
    const int barEnd = (int)std::round(width_ * std::clamp(progress, 0.0f, 100.0f) / 100.0f);
    for (int y = barTop; y < height_; ++y) {
        uint32_t* row = pixels_.data() + size_t(y) * width_;
        std::fill(row, row + barEnd, barColor_);
    }
}
