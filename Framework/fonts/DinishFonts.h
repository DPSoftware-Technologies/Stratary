#ifndef ST_DINISH_FONTS_H
#define ST_DINISH_FONTS_H

#include <cstddef>

// DINish, compiled in because the desktop splash draws text before any file system or GPU is
// up. Bold is the splash title, Light its status line.
namespace st::fonts {
extern const unsigned char kDinishBold[];
extern const size_t kDinishBoldSize;
extern const unsigned char kDinishLight[];
extern const size_t kDinishLightSize;
}

#endif
