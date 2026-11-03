# ABR parser fixtures and compatibility

`sampled-v2.abr` is an original, constructed test file, not an export from
Photoshop. It has a single 5 × 3, 8-bit raw sampled tip named **Test wedge**.
The asymmetric coverage is designed to reveal inversion and orientation errors.
It was generated for this repository and is dedicated to the public domain
under [CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/).
The other ABR test inputs are generated deterministically in `tests/test_abr.cpp`.
No third-party brush artwork or GPL implementation code is included.

The importer was independently written against format facts cross-checked in
these primary upstream sources (read October 14, 2026):

- [GIMP ABR reader](https://github.com/GNOME/gimp/blob/master/app/core/gimpbrush-load.c)
- [Krita ABR collection reader](https://invent.kde.org/graphics/krita/-/blob/master/libs/brush/kis_abr_brush_collection.cpp)
- [Krita mask coverage conversion](https://invent.kde.org/graphics/krita/-/blob/master/libs/brush/kis_brush.cpp)
- [Krita brush-tip compatibility documentation](https://docs.krita.org/en/reference_manual/resource_management/resource_brushtips.html)

The parser supports ABR 1, 2, 6.1, 6.2, 10.1 and 10.2 with 8-bit sampled tips, either raw
or PackBits compressed. Each PackBits row must decode to exactly the recorded
width. ABR sample bytes represent coverage directly (255 paints fully).
Version 2 stores a UTF-16BE sample name; versions 1, 6 and 10 receive numbered names.
Modern sample records have a 47-byte (subversion 1) or 301-byte (subversion 2) prefix and are padded
to four bytes; resource sections are bounded by their own declared lengths.

Computed brushes, fully transparent tips, 16-bit samples, other compression methods, pattern resources,
and Photoshop brush settings/dynamics are not imported. Supported samples may
be imported alongside unsupported ones, with warnings. Structural damage or a
resource limit failure rejects the entire import. Limits are 256 MiB per file,
128 sample records, 8192 source pixels per side, 268,435,456 decoded source pixels,
and 67,108,864 imported pixels. Large tips are smoothly resized to at most 1024
pixels on the longest edge with a report; the source ABR remains unchanged.

These fixtures verify binary parsing, coverage, naming, strict row boundaries,
failure atomicity, signed bounds, resizing and independent source/output limits.
`test_abr_packs.cpp` additionally accepts optional local packs through the
`TOON_ABR_PACKS_DIR` environment variable, exercising import, persistence and
raster/vector painting without adding third-party brush artwork to the repository.
Neither test suite establishes full Photoshop brush compatibility.
