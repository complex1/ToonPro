# Lucide icons

These SVGs are unmodified files from [Lucide](https://lucide.dev), pinned to
[`dab294f313897bacaa968b725b3259fa37c17bd1`](https://github.com/lucide-icons/lucide/tree/dab294f313897bacaa968b725b3259fa37c17bd1/icons)
and retrieved on 2026-10-14. Only the icons used by Toon Pro's workspace are
vendored. No package manager or remote request is required at runtime.

The 0.42 brush catalog adds `pencil`, `sprout`, `trees`, `cloud`, `mountain`,
`brick-wall`, and `hash`, retrieved from the same pinned commit on 2026-10-29.
They distinguish subject groups in the fixed-width Toolbox column.

The 0.43 drawing mode uses `maximize-2`, retrieved from the same pinned commit
on 2026-10-29, to distinguish entering fullscreen from fitting the canvas.

The accompanying `LICENSE` is the complete upstream license, including the ISC
license and the MIT notice for icons derived from Feather. It is also embedded
in the application at `:/icons/lucide/LICENSE`.

`src/icons.cpp` renders these SVGs through Qt SVG. The SVG geometry is unchanged;
the `currentColor` stroke is resolved to Toon Pro's light foreground color at
render time, or a muted color for disabled controls. The icon engine renders at
the requested display scale so icons remain sharp on high DPI displays.

To update or add an icon, download its `icons/<name>.svg` from a pinned upstream
commit, retain the corresponding upstream license, update this notice, and add
the file to `resources/toon_icons.qrc`. Call `toon::appIcon("<name>")` without
the extension.

The selection context bar also uses `grip-vertical`, `ellipsis`, `square-dashed`,
`square-plus`, `square-minus`, `combine`, `contrast`, `move`, `check`, `x`,
`pin`, `focus`, and `paint-bucket`, from the same pinned commit above.

The drawing workspace redesign additionally uses `circle`, `sliders-horizontal`, `grid-2x2`, `droplet`, `pen-tool`, `lasso`, `eye`, `eye-off`, `lock-keyhole`, `lock-keyhole-open`, `link-2`, `blend`, `arrow-left-right` from the same pinned commit. Icon rasters are cached by name, size, scale and palette color; the runtime color follows the shared theme.

`split` is also included for symmetry, from the same pinned commit.

`triangle` is included for the three-point perspective guide, from the same pinned commit.

The expanded drawing tools additionally use `pipette`, `rectangle-horizontal`, `minus`, `pentagon`, `spline`, `wand-sparkles`, `type`, `mouse-pointer-2`, `copy`, `scissors`, `clipboard-paste`, `circle-dashed`, `square-dashed-mouse-pointer`, `sun`, `flame`, `radius`, `bold`, `italic`, `paintbrush`, `zoom-in`, from the same pinned commit.

`triangle-alert` is included for canvas action feedback, from the same pinned commit and retrieved on 2026-10-15.

`box` and `video` are included for 3D reference navigation, from the same pinned commit, retrieved 2026-10-16.

The Timeline uses `play`, `pause`, `chevron-left`, `chevron-right`, `skip-back`, `skip-forward`, and `repeat`, from the same pinned commit, retrieved 2026-10-19.

Audio tracks use `music`, `volume-2`, and `volume-x`, from the same pinned commit, retrieved 2026-10-20.
