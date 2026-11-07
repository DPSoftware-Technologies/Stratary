# Splash screen templates

Two editable SVGs (Inkscape, Figma, Illustrator, Affinity), one for each splash mode. Paint your art, put your logos in the image, hide the preview and guide layers,
and export.

| Mode | Template | Paint at | Export to |
|---|---|---|---|
| Desktop: a borderless window before the game window opens | `dsplash_template.svg` | 1440 x 810 | `assets/contents/dsplash.bmp` |
| InWindow: inside the game window | `splash_template.svg` | 1280 x 720 | `assets/contents/splash.bmp` |

Export as an uncompressed **24-bit BMP**, which is what SDL's BMP loader expects. No alpha.

## Who draws what

**You** draw everything in the image, logos included. The engine has no logo setting.

**The engine** draws only this, on top of the image:

| | Desktop | InWindow |
|---|---|---|
| Title | project name, DINish Bold 40, x 28, baseline y 742 | none |
| Status | DINish Light 26, x 28, baseline y 782 | DINish Light 16, centred, bottom of the window |
| Progress bar | y 802-810, full width | none |

Keep the Desktop strip (y 688-810) dark, because the text is white. A hard edge or a fade
into your art both work. Logos go on the right, in x 1040-1412, y 704-790. The engine's text
stays left of x 1008, and a long status line is shortened to fit.

## Size

The Desktop window shows the image at half its pixel size at 100% display scale, so 1440 x 810
is a 720 x 405 window, and at 200% it is pixel for pixel. All Desktop positions above are in
image pixels at 1440 x 810.

The InWindow image is shown centred at its own pixel size and shrunk if the window is smaller,
so it is never cropped. Make its edges match `AppConfig::splashBackground` (black by default)
or the edge of the image will show on a bigger window.

## Choosing the mode

`AppConfig::splashMode` is `Auto`, `Desktop` or `InWindow`, and `--splash=desktop` or
`--splash=inwindow` overrides it for one run. `Auto` means Desktop, except on gamescope (SteamOS
game mode). Android and `--stream` are always InWindow, and a missing `dsplash.bmp` falls back to
InWindow. `AppConfig::splashBarColor` sets the bar colour.

## Font

The templates use DINish Bold and Light (SIL Open Font License 1.1). The engine has both compiled in. Install them to see
the templates as intended; a missing font only changes the preview.
