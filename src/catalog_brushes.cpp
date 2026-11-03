#include "catalog_brushes.h"
#include "specialized_catalog_brushes.h"
#include "advanced_catalog_brushes.h"

#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <array>
#include <cmath>
#include <functional>

namespace toon {
namespace {
constexpr qreal pi = 3.14159265358979323846;
enum Family { Ink, Sketch, Soft, Grass, Trees, Clouds, Terrain, Walls, Water, Tones, Effects, Weather, Color };
struct Entry { const char *id; const char *name; Family family; const char *tags; };

// Metadata is compiled in: opening the library never depends on a development
// checkout, a network request, an installed font, or an external asset pack.
constexpr Entry entries[]{
    {"clean-contour", "Clean Contour", Ink, "Pen,Contour,Cleanup,Pressure"},
    {"tapered-hairline", "Tapered Hairline", Ink, "Pen,Hair,Taper,Pressure"},
    {"fine-facial-detail", "Fine Facial Detail", Ink, "Pen,Face,Fine,Pressure"},
    {"cloth-crease", "Cloth Crease", Ink, "Pen,Cloth,Folds,Pressure"},
    {"dry-manga-nib", "Dry Manga Nib", Ink, "Ink,Manga,Dry,Texture"},
    {"bold-silhouette", "Bold Silhouette", Ink, "Ink,Silhouette,Opaque"},
    {"delicate-eyelash", "Delicate Eyelash", Ink, "Pen,Eyelash,Fine,Taper"},
    {"hair-ribbon-ink", "Hair Ribbon Ink", Ink, "Ink,Hair,Flat"},
    {"brush-letter-ink", "Brush Letter Ink", Ink, "Ink,Lettering,Pressure"},
    {"round-detail-mono", "Round Detail Mono", Ink, "Pen,Monoline,Uniform,Round"},
    {"angular-prop-ink", "Angular Prop Ink", Ink, "Ink,Props,Square"},
    {"rough-contour", "Rough Contour", Ink, "Ink,Contour,Rough"},
    {"fine-scratch-ink", "Fine Scratch Ink", Ink, "Ink,Fine,Scratch"},
    {"foliage-ink", "Foliage Ink", Ink, "Ink,Foliage,Direction"},
    {"fabric-edge-ink", "Fabric Edge Ink", Ink, "Ink,Fabric,Dry"},
    {"calligraphic-accent", "Calligraphic Accent", Ink, "Ink,Calligraphy,Flat"},
    {"heavy-brush-accent", "Heavy Brush Accent", Ink, "Ink,Bold,Pressure"},
    {"dry-edge-highlight", "Dry Edge Highlight", Ink, "Ink,Dry,Highlight"},
    {"construction-lead", "Construction Lead", Sketch, "Pencil,Construction,Light"},
    {"rough-layout-pencil", "Rough Layout Pencil", Sketch, "Pencil,Layout,Rough"},
    {"fine-layout-pencil", "Fine Layout Pencil", Sketch, "Pencil,Layout,Fine"},
    {"soft-graphite-side", "Soft Graphite Side", Sketch, "Pencil,Graphite,Tilt,Shading"},
    {"broken-graphite", "Broken Graphite", Sketch, "Pencil,Graphite,Broken"},
    {"graphite-hatching", "Graphite Hatching", Sketch, "Pencil,Graphite,Hatching"},
    {"powder-charcoal", "Powder Charcoal", Sketch, "Charcoal,Powder,Soft"},
    {"charcoal-edge", "Charcoal Edge", Sketch, "Charcoal,Dry,Edge"},
    {"chalk-block", "Chalk Block", Sketch, "Chalk,Flat,Grain"},
    {"chalk-detail", "Chalk Detail", Sketch, "Chalk,Fine,Highlight"},
    {"dry-pastel", "Dry Pastel", Sketch, "Pastel,Dry,Grain"},
    {"cont-accent", "Conté Accent", Sketch, "Conte,Dry,Dense"},
    {"velvet-shade", "Velvet Shade", Soft, "Soft,Shading,Flow"},
    {"edge-glaze", "Edge Glaze", Soft, "Soft,Glaze,Edge"},
    {"fine-aerosol", "Fine Aerosol", Soft, "Airbrush,Mist,Build-up"},
    {"granular-spray", "Granular Spray", Soft, "Spray,Grain,Particles"},
    {"soft-oval", "Soft Oval", Soft, "Soft,Oval,Direction"},
    {"broad-light-wash", "Broad Light Wash", Soft, "Soft,Wash,Transparent"},
    {"soft-rim-light", "Soft Rim Light", Soft, "Soft,Rim light,Highlight"},
    {"dense-airbrush", "Dense Airbrush", Soft, "Airbrush,Dense,Build-up"},
    {"dusty-spray", "Dusty Spray", Soft, "Spray,Dust,Grain"},
    {"coarse-spatter", "Coarse Spatter", Soft, "Spray,Spatter,Coarse"},
    {"needle-spatter", "Needle Spatter", Soft, "Spray,Spatter,Direction"},
    {"fine-pollen", "Fine Pollen", Soft, "Spray,Pollen,Sparse"},
    {"soft-grain-glaze", "Soft Grain Glaze", Soft, "Soft,Grain,Glaze"},
    {"short-grass-tuft", "Short Grass Tuft", Grass, "Stamp,Grass,Tuft,Nature"},
    {"wind-grass", "Wind Grass", Grass, "Stamp,Grass,Wind,Nature"},
    {"clover-patch", "Clover Patch", Grass, "Stamp,Clover,Ground cover,Nature"},
    {"moss-fringe", "Moss Fringe", Grass, "Texture,Moss,Edge,Nature"},
    {"broadleaf-cluster", "Broadleaf Cluster", Trees, "Stamp,Leaves,Tree,Nature"},
    {"conifer-branch", "Conifer Branch", Trees, "Stamp,Conifer,Branch,Nature"},
    {"distant-canopy", "Distant Canopy", Trees, "Stamp,Canopy,Tree,Distant"},
    {"bare-twig", "Bare Twig", Trees, "Stamp,Twig,Branch,Winter"},
    {"vine-leaves", "Vine Leaves", Trees, "Stamp,Vine,Leaves,Direction"},
    {"bark-edge", "Bark Edge", Trees, "Texture,Bark,Wood,Edge"},
    {"cloud-edge-soft", "Cloud Edge Soft", Clouds, "Stamp,Cloud,Soft,Sky"},
    {"wispy-cloud", "Wispy Cloud", Clouds, "Stamp,Cloud,Wisp,Sky"},
    {"ground-mist", "Ground Mist", Clouds, "Soft,Mist,Fog,Atmosphere"},
    {"sunlit-cloud-rim", "Sunlit Cloud Rim", Clouds, "Stamp,Cloud,Highlight,Sky"},
    {"dust-haze", "Dust Haze", Clouds, "Soft,Dust,Haze,Atmosphere"},
    {"mountain-haze", "Mountain Haze", Clouds, "Soft,Mountain,Haze,Distant"},
    {"stone-grain", "Stone Grain", Terrain, "Texture,Stone,Grain,Ground"},
    {"sand-speckle", "Sand Speckle", Terrain, "Spray,Sand,Speckle,Ground"},
    {"soil-grain", "Soil Grain", Terrain, "Texture,Soil,Grain,Ground"},
    {"cracked-earth", "Cracked Earth", Terrain, "Texture,Earth,Cracks,Tile"},
    {"mud-speckles", "Mud Speckles", Terrain, "Spray,Mud,Speckle,Ground"},
    {"rock-strata", "Rock Strata", Terrain, "Texture,Rock,Strata,Direction"},
    {"cliff-facet", "Cliff Facet", Terrain, "Stamp,Cliff,Facet,Rock"},
    {"snow-crust", "Snow Crust", Terrain, "Texture,Snow,Crust,Grain"},
    {"plaster-grain", "Plaster Grain", Walls, "Texture,Wall,Plaster,Grain"},
    {"brick-surface", "Brick Surface", Walls, "Texture,Wall,Brick,Tile"},
    {"wood-grain", "Wood Grain", Walls, "Texture,Wood,Grain,Tile"},
    {"rough-concrete", "Rough Concrete", Walls, "Texture,Concrete,Rough,Wall"},
    {"smooth-concrete", "Smooth Concrete", Walls, "Texture,Concrete,Smooth,Wall"},
    {"stone-block-surface", "Stone Block Surface", Walls, "Texture,Stone,Block,Tile"},
    {"roof-tile-surface", "Roof Tile Surface", Walls, "Texture,Roof,Tile"},
    {"asphalt-grain", "Asphalt Grain", Walls, "Texture,Asphalt,Road,Grain"},
    {"stucco-wall", "Stucco Wall", Walls, "Texture,Stucco,Wall,Grain"},
    {"peeling-paint", "Peeling Paint", Walls, "Stamp,Paint,Chips,Weathering"},
    {"wall-cracks", "Wall Cracks", Walls, "Stamp,Wall,Cracks,Weathering"},
    {"water-stains", "Water Stains", Walls, "Stamp,Water,Stains,Weathering"},
    {"rust-flecks", "Rust Flecks", Walls, "Spray,Rust,Flecks,Weathering"},
    {"paint-roller-grain", "Paint Roller Grain", Walls, "Texture,Paint,Roller,Grain"},
    {"fabric-weave", "Fabric Weave", Walls, "Texture,Fabric,Weave,Tile"},
    {"tatami-surface", "Tatami Surface", Walls, "Texture,Tatami,Weave,Tile"},
    {"calm-ripples", "Calm Ripples", Water, "Stamp,Water,Ripple,Reflection"},
    {"broken-reflection", "Broken Reflection", Water, "Texture,Water,Reflection,Highlight"},
    {"foam-flecks", "Foam Flecks", Water, "Spray,Water,Foam,Flecks"},
    {"surf-spray", "Surf Spray", Water, "Spray,Water,Surf,Droplets"},
    {"caustic-surface", "Caustic Surface", Water, "Texture,Water,Caustic,Tile"},
    {"parallel-hatch", "Parallel Hatch", Tones, "Stamp,Manga,Hatch,Direction"},
    {"stipple-shadow", "Stipple Shadow", Tones, "Spray,Manga,Stipple,Shadow"},
    {"crosshatch-tile", "Crosshatch Tile", Tones, "Texture,Manga,Crosshatch,Tile"},
    {"engraved-shade", "Engraved Shade", Tones, "Texture,Manga,Etched,Shade"},
    {"action-streak", "Action Streak", Effects, "Stamp,Manga,Action,Streak"},
    {"impact-fleck", "Impact Fleck", Effects, "Spray,Manga,Impact,Flecks"},
    {"dry-speed-texture", "Dry Speed Texture", Effects, "Texture,Manga,Speed,Dry"},
    {"rain-hatching", "Rain Hatching", Effects, "Stamp,Manga,Rain,Hatching"},
    {"spark-cross", "Spark Cross", Effects, "Stamp,Manga,Spark,Highlight"},
    {"flower-screen", "Flower Screen", Effects, "Texture,Manga,Flower,Tile"},
    {"fine-rain", "Fine Rain", Weather, "Spray,Rain,Fine,Weather"},
    {"heavy-rain", "Heavy Rain", Weather, "Spray,Rain,Heavy,Weather"},
    {"snow-flurry", "Snow Flurry", Weather, "Spray,Snow,Soft,Weather"},
    {"ember-drift", "Ember Drift", Weather, "Spray,Ember,Flecks,Particles"},
    {"ash-drift", "Ash Drift", Weather, "Spray,Ash,Grain,Particles"},
    {"dust-motes", "Dust Motes", Weather, "Spray,Dust,Sparse,Particles"},
    {"firefly-glow", "Firefly Glow", Weather, "Airbrush,Firefly,Glow,Particles"},
    {"painted-leaf-cluster", "Painted Leaf Cluster", Color, "Colored stamp,Leaves,Nature"},
    {"painted-flower", "Painted Flower", Color, "Colored stamp,Flower,Nature"},
    {"painted-pebble", "Painted Pebble", Color, "Colored stamp,Pebble,Stone"},
    {"painted-branch", "Painted Branch", Color, "Colored stamp,Branch,Tree"},
    {"painted-moss-patch", "Painted Moss Patch", Color, "Colored stamp,Moss,Nature"},
    {"painted-water-foam", "Painted Water Foam", Color, "Colored stamp,Water,Foam"},
    {"painted-ground-patch", "Painted Ground Patch", Color, "Colored stamp,Ground,Grass"},
};
static_assert(std::size(entries) == 112);
constexpr const char *familyNames[]{
    "Line art and ink", "Sketch and dry media", "Soft paint and spray", "Grass and ground cover",
    "Trees and foliage", "Clouds and atmosphere", "Ground rock and terrain", "Walls and construction surfaces",
    "Water and reflections", "Manga tones and hatching", "Manga effects and decoration", "Weather and particles", "Painted color stamps"
};

quint32 saltFor(const char *text) {
    quint32 value = 2166136261U;
    while (*text) { value ^= quint8(*text++); value *= 16777619U; }
    return value;
}
qreal noise(int x, int y, quint32 salt) {
    quint32 n = quint32(x) * 0x9e3779b9U ^ quint32(y) * 0x85ebca6bU ^ salt;
    n ^= n >> 16; n *= 0x7feb352dU; n ^= n >> 15; n *= 0x846ca68bU; n ^= n >> 16;
    return (n & 0xffffU) / 65535.0;
}
QImage mask(const std::function<void(QPainter &)> &draw) {
    QImage image(128, 128, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::black);
    QPainter p(&image); p.setRenderHint(QPainter::Antialiasing); p.setPen(Qt::NoPen); p.setBrush(Qt::white);
    draw(p); p.end();
    return image.convertToFormat(QImage::Format_Grayscale8);
}
QPainterPath leaf(QPointF base, QPointF tip, qreal width) {
    const auto d = tip - base;
    const qreal length = std::hypot(d.x(), d.y());
    const QPointF n(-d.y() / qMax(1.0, length) * width, d.x() / qMax(1.0, length) * width);
    QPainterPath path(base);
    path.cubicTo(base + d * .22 + n, base + d * .66 + n, tip);
    path.cubicTo(base + d * .70 - n * .65, base + d * .28 - n * .55, base);
    return path;
}
void organicBlob(QPainter &p, QPointF center, qreal radius, quint32 seed, int corners = 12) {
    QPainterPath path;
    for (int i = 0; i < corners; ++i) {
        const qreal a = i * 2 * pi / corners;
        const qreal r = radius * (.55 + .45 * noise(i, 3, seed));
        const QPointF point = center + QPointF(std::cos(a), std::sin(a)) * r;
        if (i == 0) path.moveTo(point); else path.lineTo(point);
    }
    path.closeSubpath(); p.drawPath(path);
}

QImage dryTip(quint32 seed, bool square, qreal density, qreal fibres = 0) {
    QImage result(96, 96, QImage::Format_Grayscale8);
    for (int y = 0; y < 96; ++y) {
        auto *row = result.scanLine(y);
        for (int x = 0; x < 96; ++x) {
            const qreal nx = (x + .5 - 48) / 45, ny = (y + .5 - 48) / 45;
            const qreal edge = square ? qMax(qAbs(nx), qAbs(ny)) : std::hypot(nx, ny);
            const qreal grit = noise(x, y, seed), fibre = noise(x / 2, y / 12, seed ^ 8147U);
            const qreal value = qBound(0.0, (1 - edge) * 10, 1.0) *
                (grit < density ? .4 + .6 * grit : .015) * (1 - fibres + fibres * fibre);
            row[x] = uchar(qRound(value * 255));
        }
    }
    return result;
}

QImage flatTip(qreal height, bool pointed = false) {
    return mask([=](QPainter &p) {
        if (pointed) p.drawPath(leaf({10, 64}, {118, 64}, height));
        else p.drawRoundedRect(QRectF(8, 64 - height / 2, 112, height), 1, 1);
    });
}

QImage grassTip(int variant, quint32 seed) {
    return mask([=](QPainter &p) {
        if (variant == 2) {
            for (int i = 0; i < 11; ++i) {
                const QPointF c(22 + noise(i, 2, seed) * 83, 23 + noise(i, 3, seed) * 82);
                const qreal r = 6 + noise(i, 4, seed) * 5;
                p.setPen(QPen(Qt::white, 1.2)); p.drawLine(c, {c.x() - 3, 118}); p.setPen(Qt::NoPen);
                for (int k = 0; k < 3; ++k) {
                    const qreal a = (k * 120 - 90) * pi / 180;
                    p.drawEllipse(c + QPointF(std::cos(a), std::sin(a)) * r * .7, r, r * .85);
                }
            }
        } else if (variant == 3) {
            for (int i = 0; i < 95; ++i) {
                const qreal x = 8 + noise(i, 1, seed) * 112, y = 50 + noise(i, 2, seed) * 58;
                const qreal r = 1 + noise(i, 3, seed) * 4;
                p.setBrush(QColor::fromRgbF(.5 + .5 * noise(i, 5, seed), .5 + .5 * noise(i, 5, seed), .5 + .5 * noise(i, 5, seed)));
                p.drawEllipse(QPointF(x, y), r, r * 1.6);
            }
        } else {
            for (int i = 0; i < 18; ++i) {
                const qreal bx = 26 + noise(i, 1, seed) * 67;
                const qreal height = 26 + noise(i, 2, seed) * 80;
                const qreal bend = variant == 1 ? 20 + noise(i, 3, seed) * 25 : (noise(i, 3, seed) - .5) * 43;
                QPainterPath blade({bx, 118});
                blade.cubicTo({bx + bend * .05, 118 - height * .45}, {bx + bend * .4, 118 - height * .9}, {qBound(6.0, bx + bend, 121.0), 118 - height});
                blade.cubicTo({bx + bend * .2, 118 - height * .65}, {bx + 4, 118 - height * .2}, {bx + 4, 118});
                p.drawPath(blade);
            }
        }
    });
}

QImage foliageTip(int variant, quint32 seed) {
    return mask([=](QPainter &p) {
        if (variant == 0) {
            for (int i = 0; i < 31; ++i) {
                const qreal a = noise(i, 1, seed) * 2 * pi, r = std::sqrt(noise(i, 2, seed)) * 38;
                const QPointF base(64 + std::cos(a) * r, 68 + std::sin(a) * r);
                const QPointF end = base + QPointF(std::cos(a), std::sin(a)) * (12 + noise(i, 3, seed) * 12);
                p.drawPath(leaf(base, end, 6 + noise(i, 4, seed) * 5));
            }
        } else if (variant == 1) {
            p.setPen(QPen(Qt::white, 3, Qt::SolidLine, Qt::RoundCap)); p.drawLine(QPointF(63, 117), QPointF(65, 10));
            for (int i = 0; i < 12; ++i) {
                const qreal y = 24 + i * 7.4, extent = 13 + i * 2.6;
                for (int side : {-1, 1}) {
                    p.setPen(QPen(Qt::white, 1.6, Qt::SolidLine, Qt::RoundCap));
                    const QPointF end(64 + side * extent, y + 15); p.drawLine({64, y - 2}, end);
                    for (int j = 1; j < 6; ++j) {
                        const QPointF base(64 + side * extent * j / 6, y - 2 + j * 17 / 6.0);
                        p.drawLine(base, base + QPointF(side * 10, -8 - noise(i, j, seed) * 5));
                    }
                }
            }
        } else if (variant == 2) {
            for (int i = 0; i < 25; ++i) {
                const qreal a = noise(i, 1, seed) * 2 * pi, r = std::sqrt(noise(i, 2, seed)) * 35;
                const qreal radius = 8 + noise(i, 3, seed) * 13;
                organicBlob(p, {64 + std::cos(a) * r, 64 + std::sin(a) * r * .65}, radius, seed + i, 15);
            }
        } else if (variant == 3) {
            QPainterPath stem({38, 119}); stem.cubicTo({59, 85}, {56, 51}, {83, 10});
            p.setPen(QPen(Qt::white, 3, Qt::SolidLine, Qt::RoundCap)); p.setBrush(Qt::NoBrush); p.drawPath(stem);
            for (int i = 0; i < 9; ++i) {
                const qreal t = .15 + i * .085; const auto base = stem.pointAtPercent(t); const int side = i % 2 ? 1 : -1;
                const QPointF end = base + QPointF(side * (15 + noise(i, 2, seed) * 24), -16 - noise(i, 3, seed) * 14);
                p.setPen(QPen(Qt::white, 2 - t, Qt::SolidLine, Qt::RoundCap)); p.drawLine(base, end);
                p.drawLine(base * .45 + end * .55, end + QPointF(side * 5, 12));
            }
        } else if (variant == 4) {
            QPainterPath stem({7, 79}); stem.cubicTo({40, 18}, {82, 109}, {122, 43});
            p.setBrush(Qt::NoBrush); p.setPen(QPen(Qt::white, 2)); p.drawPath(stem); p.setPen(Qt::NoPen); p.setBrush(Qt::white);
            for (int i = 0; i < 9; ++i) {
                const auto base = stem.pointAtPercent(.08 + .105 * i); const int side = i % 2 ? 1 : -1;
                p.drawPath(leaf(base, base + QPointF(7, side * (19 + noise(i, 1, seed) * 13)), 8));
            }
        } else {
            for (int i = 0; i < 18; ++i) {
                const qreal x = 10 + i * 6, y = 9 + noise(i, 1, seed) * 40;
                p.setPen(QPen(Qt::white, 1 + noise(i, 2, seed) * 4));
                QPainterPath path({x, y}); path.cubicTo({x - 5, 50}, {x + 7, 77}, {x - 2, 119 - noise(i, 3, seed) * 30}); p.drawPath(path);
            }
        }
    });
}

QImage cloudTip(bool wisp, bool rim, quint32 seed) {
    QImage result(128, 128, QImage::Format_Grayscale8);
    for (int y = 0; y < 128; ++y) {
        auto *row = result.scanLine(y);
        for (int x = 0; x < 128; ++x) {
            qreal value = 0;
            for (int i = 0; i < (wisp ? 4 : 9); ++i) {
                const qreal cx = wisp ? 57 + i * 5 : 23 + noise(i, 2, seed) * 82;
                const qreal cy = wisp ? 48 + i * 8 + std::sin(x / 47.0) * 7 : 47 + noise(i, 3, seed) * 33;
                const qreal rx = wisp ? 46 - i * 4 : 11 + noise(i, 4, seed) * 17;
                const qreal ry = wisp ? 3 + i * .8 : 10 + noise(i, 5, seed) * 14;
                const qreal d = std::pow((x - cx) / rx, 2) + std::pow((y - cy) / ry, 2);
                const qreal puff = rim ? std::exp(-d * 2) * qBound(0.0, (cy + 3 - y) / 9, 1.0) : std::exp(-d * 2);
                value = qMax(value, puff);
            }
            const qreal edge = qBound(0.0, qMin(qMin(x, 127 - x), qMin(y, 127 - y)) / 9.0, 1.0);
            row[x] = uchar(qRound(255 * value * edge));
        }
    }
    return result;
}

// Compact particulate tips contain a small cluster of original marks; the
// engine need not expand every event into dozens of separate brush dabs.
QImage particles(quint32 seed, int marks, qreal radius, int style = 0) {
    return mask([=](QPainter &p) {
        for (int i = 0; i < marks; ++i) {
            const qreal a = noise(i, 1, seed) * 2 * pi, distance = std::sqrt(noise(i, 2, seed)) * (53 - radius);
            const QPointF c(64 + std::cos(a) * distance, 64 + std::sin(a) * distance);
            const qreal r = radius * (.3 + noise(i, 3, seed) * .7);
            const int gray = qRound(130 + noise(i, 4, seed) * 125); p.setBrush(QColor(gray, gray, gray));
            if (style == 1) organicBlob(p, c, r, seed + i, 6);
            else if (style == 2) p.drawPath(leaf(c - QPointF(r * 2, r * 4), c + QPointF(r * 2, r * 4), r * .55));
            else if (style == 3) {
                QRadialGradient gradient(c, r); gradient.setColorAt(0, QColor(gray, gray, gray)); gradient.setColorAt(1, Qt::black);
                // Preserve earlier particles under this particle's soft black
                // falloff: mask coverage combines by maximum, not replacement.
                p.save(); p.setCompositionMode(QPainter::CompositionMode_Lighten);
                p.setBrush(gradient); p.drawEllipse(c, r, r); p.restore();
            } else p.drawEllipse(c, r, r);
        }
    });
}

enum Pattern { FineGrain, CoarseGrain, Bricks, Wood, Blocks, Tiles, Asphalt, Stucco, Weave, Tatami, Cracks, Strata, Caustics, Crosshatch, Engraving, Flowers, Speed };
QImage pattern(Pattern type, quint32 seed, int revision) {
    // All periodic fields use a 128-pixel domain. Seeded cells wrap at the tile
    // boundary, so no extra seam appears when the artist paints a broad area.
    QImage result(128, 128, QImage::Format_Grayscale8);
    std::array<QPointF, 20> sites;
    for (int i = 0; i < int(sites.size()); ++i) sites[i] = {noise(i, 11, seed) * 128, noise(i, 12, seed) * 128};
    for (int y = 0; y < 128; ++y) {
        auto *row = result.scanLine(y);
        for (int x = 0; x < 128; ++x) {
            const qreal fine = noise(x, y, seed);
            // Smooth the coarse grain across its wrapped 32×32 lattice. Hard
            // nearest-cell samples produced visible four-pixel square blocks.
            const int gx = x / 4, gy = y / 4;
            const qreal fx = (x % 4 + .5) / 4, fy = (y % 4 + .5) / 4;
            const qreal sx = fx * fx * (3 - 2 * fx), sy = fy * fy * (3 - 2 * fy);
            const qreal top = std::lerp(noise(gx, gy, seed ^ 1349U), noise((gx + 1) % 32, gy, seed ^ 1349U), sx);
            const qreal bottom = std::lerp(noise(gx, (gy + 1) % 32, seed ^ 1349U), noise((gx + 1) % 32, (gy + 1) % 32, seed ^ 1349U), sx);
            const qreal coarse = std::lerp(top, bottom, sy);
            qreal v = .2 + .8 * fine;
            switch (type) {
            case FineGrain: v = .45 + .4 * fine + .15 * coarse; break;
            case CoarseGrain: v = .05 + .35 * fine + .6 * coarse; break;
            case Bricks: {
                const int bx = (x + ((y / 32) % 2) * 32) % 64;
                const bool line = y % 32 < 3 || bx < 3;
                v = revision >= 2 ? (line ? 1 : 0) : line ? .06 : .68 + .27 * fine; break;
            }
            case Wood: {
                const qreal warp = 3 * std::sin(y * 2 * pi / 128) + 1.5 * std::sin(y * 6 * pi / 128);
                const qreal phase = (x + warp) * 2 * pi / 16;
                v = revision >= 2 ? qBound(0.0, (.22 - std::abs(std::sin(phase))) / .16, 1.0)
                    : .2 + .65 * std::pow(.5 + .5 * std::sin(phase), .35) + fine * .12; break;
            }
            case Blocks: {
                const int by = y / 32, bx = (x + (by % 2) * 19) % 64;
                const bool line = y % 32 < 3 || bx < 3 || (by % 2 == 0 && bx > 42 && bx < 45);
                v = revision >= 2 ? (line ? 1 : 0) : line ? .06 : .35 + .45 * coarse + fine * .15; break;
            }
            case Tiles: {
                const int tx = (x + (y / 32) * 16) % 32, ty = y % 32;
                const qreal arc = 20 + 6 * std::cos((tx - 16) * pi / 32);
                const bool line = tx < 2 || std::abs(ty - arc) < 1.5;
                v = revision >= 2 ? (line ? 1 : 0) : line ? .04 : .4 + .5 * (1 - tx / 32.0); break;
            }
            case Asphalt: v = fine > .85 ? .95 : .12 + .45 * coarse + .12 * fine; break;
            case Stucco: v = .12 + .75 * std::pow(coarse, .6) + .1 * fine; break;
            case Weave:
                v = revision >= 2 ? (((x / 8 + y / 8) % 2 == 0 ? y % 8 < 2 : x % 8 < 2) ? 1 : 0)
                    : ((x / 4 + y / 4) % 2 == 0 ? .8 : .35) * (x % 4 == 0 || y % 4 == 0 ? .3 : 1); break;
            case Tatami:
                if (revision >= 2) v = y % 8 < 2 || x % 32 < 2 ? 1 : 0;
                else { v = .2 + .7 * std::pow(.5 + .5 * std::sin(y * 2 * pi / 8), .35); if (x % 32 < 2) v *= .3; }
                break;
            case Strata: {
                const qreal phase = (y + 3 * std::sin(x * 2 * pi / 128)) * 2 * pi / 16;
                v = revision >= 2 ? qBound(0.0, (.28 - std::abs(std::sin(phase))) / .18, 1.0)
                    : .12 + .7 * std::pow(.5 + .5 * std::sin(phase), 4) + fine * .15; break;
            }
            case Cracks: case Caustics: {
                qreal first = 1000, second = 1000;
                for (const auto &site : sites) {
                    qreal dx = std::abs(x - site.x()), dy = std::abs(y - site.y()); dx = qMin(dx, 128 - dx); dy = qMin(dy, 128 - dy);
                    const qreal d = std::hypot(dx, dy);
                    if (d < first) { second = first; first = d; } else second = qMin(second, d);
                }
                const qreal edge = second - first;
                v = type == Cracks ? (revision >= 2 ? qBound(0.0, (1.5 - edge), 1.0)
                    : .16 + .8 * qBound(0.0, edge / 1.5, 1.0)) : std::exp(-edge * edge / 3.5); break;
            }
            case Crosshatch: v = ((x + y) % 16 < 2 || (x - y + 128) % 16 < 2) ? .95 : revision >= 2 ? 0 : .01; break;
            case Engraving: v = (y % 8 < 2 && noise(x / 8, y / 8, seed) > .15) ? .65 + .35 * fine : revision >= 2 ? 0 : .01; break;
            case Speed: v = (y % 8 < 2 && noise(x / 32, y, seed) > .3) ? .5 + .5 * fine : revision >= 2 ? 0 : .01; break;
            case Flowers: v = 0; break;
            }
            row[x] = uchar(qRound(qBound(0.0, v, 1.0) * 255));
        }
    }
    if (type == Flowers) {
        return mask([](QPainter &p) {
            for (const QPointF c : {QPointF(32, 32), QPointF(96, 96)}) {
                for (int i = 0; i < 5; ++i) {
                    const qreal a = i * 2 * pi / 5;
                    p.save(); p.translate(c + QPointF(std::cos(a), std::sin(a)) * 12); p.rotate(i * 72); p.drawEllipse(QRectF(-10, -6, 20, 12)); p.restore();
                }
                p.setBrush(Qt::black); p.drawEllipse(c, 5, 5); p.setBrush(Qt::white);
            }
        });
    }
    return result;
}

QImage accentTip(int type, quint32 seed) {
    return mask([=](QPainter &p) {
        if (type == 0) { // Flat ripple arcs, deliberately incomplete and unequal.
            p.setBrush(Qt::NoBrush);
            for (int i = 0; i < 4; ++i) {
                p.setPen(QPen(Qt::white, 1.7 + i * .55));
                p.drawArc(QRectF(9 + i * 7, 31 + i * 13, 108 - i * 14, 13), 8 * 16, (135 + i * 21) * 16);
                p.drawArc(QRectF(9 + i * 7, 31 + i * 13, 108 - i * 14, 13), 192 * 16, 92 * 16);
            }
        } else if (type == 1) { // Dry horizontal reflection marks.
            for (int i = 0; i < 15; ++i) {
                const qreal y = 19 + i * 6.5, width = 20 + noise(i, 1, seed) * 86;
                p.drawPath(leaf({64 - width / 2, y}, {64 + width / 2, y}, 1 + noise(i, 2, seed) * 3));
            }
        } else if (type == 2) { // Parallel hatch stamp, not a regular halftone.
            p.setPen(QPen(Qt::white, 1.5, Qt::SolidLine, Qt::RoundCap));
            for (int i = 0; i < 9; ++i) p.drawLine(QPointF(16 + i * 11, 26), QPointF(16 + i * 11, 102));
        } else if (type == 3) {
            QPainterPath path({7, 65}); path.cubicTo({33, 57}, {60, 54}, {120, 64}); path.cubicTo({64, 67}, {31, 70}, {7, 65}); p.drawPath(path);
        } else if (type == 4) {
            p.setPen(QPen(Qt::white, 1.8, Qt::SolidLine, Qt::RoundCap));
            for (int i = 0; i < 8; ++i) { const qreal x = 13 + i * 12; p.drawLine(QPointF(x, 25 + noise(i, 1, seed) * 22), QPointF(x + 17, 93 + noise(i, 2, seed) * 15)); }
        } else if (type == 5) {
            QPainterPath path; path.moveTo(64, 7); path.lineTo(70, 57); path.lineTo(120, 64); path.lineTo(70, 70);
            path.lineTo(64, 120); path.lineTo(57, 70); path.lineTo(7, 64); path.lineTo(57, 57); path.closeSubpath(); p.drawPath(path);
        } else if (type == 6) {
            QPainterPath path; path.moveTo(18, 108); path.lineTo(40, 28); path.lineTo(81, 11); path.lineTo(113, 72); path.lineTo(91, 114); path.closeSubpath(); p.drawPath(path);
            p.setBrush(QColor(145, 145, 145)); QPainterPath plane; plane.moveTo(81, 11); plane.lineTo(69, 84); plane.lineTo(91, 114); plane.lineTo(113, 72); plane.closeSubpath(); p.drawPath(plane);
        } else if (type == 7) {
            for (int i = 0; i < 12; ++i) organicBlob(p, {18 + noise(i, 1, seed) * 92, 18 + noise(i, 2, seed) * 92}, 5 + noise(i, 3, seed) * 17, seed + i, 7);
        } else if (type == 8) {
            p.setBrush(Qt::NoBrush); p.setPen(QPen(Qt::white, 2, Qt::SolidLine, Qt::RoundCap));
            QPainterPath crack({51, 8});
            for (int i = 0; i < 9; ++i) {
                const QPointF c(38 + noise(i, 1, seed) * 45, 11 + i * 12); crack.lineTo(c);
                if (i % 2) { p.drawLine(c, c + QPointF(i % 3 ? -24 : 24, 10)); }
            }
            p.drawPath(crack);
        } else if (type == 9) {
            for (int i = 0; i < 15; ++i) {
                const qreal x = 14 + i * 7, length = 30 + noise(i, 2, seed) * 78;
                QLinearGradient gradient(x, 14, x, 14 + length); gradient.setColorAt(0, QColor(170, 170, 170)); gradient.setColorAt(1, Qt::black); p.setBrush(gradient);
                p.drawRoundedRect(QRectF(x, 14 + noise(i, 1, seed) * 12, 2 + noise(i, 3, seed) * 7, length), 2, 2);
            }
        }
    });
}

QImage coloredTip(int kind, quint32 seed) {
    QImage image(128, 128, QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent);
    QPainter p(&image); p.setRenderHint(QPainter::Antialiasing); p.setPen(Qt::NoPen);
    auto paintedLeaf = [&](QPointF base, QPointF tip, qreal width, int hue) {
        QLinearGradient gradient(tip, base); gradient.setColorAt(0, QColor::fromHsv(hue, 125, 189)); gradient.setColorAt(.48, QColor::fromHsv(hue + 3, 171, 126)); gradient.setColorAt(1, QColor::fromHsv(hue + 6, 175, 63));
        p.setBrush(gradient); const auto shape = leaf(base, tip, width); p.drawPath(shape);
        p.setPen(QPen(QColor(185, 197, 114, 145), .8)); p.drawLine(base, tip); p.setPen(Qt::NoPen);
    };
    if (kind == 0) {
        for (int i = 0; i < 19; ++i) {
            const qreal a = noise(i, 1, seed) * 2 * pi, r = noise(i, 2, seed) * 25;
            const QPointF base(64 + std::cos(a) * r, 69 + std::sin(a) * r);
            paintedLeaf(base, base + QPointF(std::cos(a), std::sin(a)) * (20 + noise(i, 3, seed) * 20), 9, 78 + int(noise(i, 4, seed) * 23));
        }
    } else if (kind == 1) {
        for (int i = 0; i < 7; ++i) {
            p.save(); p.translate(64, 64); p.rotate(i * 360.0 / 7);
            QLinearGradient gradient(0, -6, 0, -44); gradient.setColorAt(0, QColor("#98566d")); gradient.setColorAt(.5, QColor("#e99eaf")); gradient.setColorAt(1, QColor("#ffe0dd")); p.setBrush(gradient);
            p.drawPath(leaf({0, -4}, {1, -48}, 22)); p.restore();
        }
        p.setBrush(QColor("#7e5638")); p.drawEllipse(QPointF(64, 64), 13, 13);
        for (int i = 0; i < 26; ++i) { const qreal a = noise(i, 1, seed) * 2 * pi, r = noise(i, 2, seed) * 11; p.setBrush(QColor(224 + int(noise(i, 3, seed) * 30), 183, 83)); p.drawEllipse(QPointF(64 + std::cos(a) * r, 64 + std::sin(a) * r), 1.3, 1.3); }
    } else if (kind == 2) {
        QRadialGradient gradient(QPointF(46, 41), 75); gradient.setColorAt(0, QColor("#b9b8b1")); gradient.setColorAt(.45, QColor("#92938e")); gradient.setColorAt(1, QColor("#444c50")); p.setBrush(gradient);
        QPainterPath path; path.moveTo(17, 85); path.cubicTo(9, 58, 33, 23, 68, 24); path.cubicTo(94, 22, 115, 56, 111, 83); path.cubicTo(104, 106, 32, 111, 17, 85); p.drawPath(path);
        p.save(); p.setClipPath(path);
        for (int i = 0; i < 450; ++i) { const qreal shade = noise(i, 3, seed); p.setBrush(QColor(shade > .5 ? 236 : 44, shade > .5 ? 230 : 49, shade > .5 ? 215 : 55, 20 + int(shade * 30))); p.drawEllipse(QPointF(noise(i, 1, seed) * 128, noise(i, 2, seed) * 128), .5 + shade, .5 + shade); } p.restore();
    } else if (kind == 3) {
        QPainterPath branch({13, 109}); branch.cubicTo({46, 87}, {63, 50}, {107, 20});
        p.setPen(QPen(QColor("#5e4834"), 7, Qt::SolidLine, Qt::RoundCap)); p.drawPath(branch); p.setPen(QPen(QColor("#aa8b57"), 2, Qt::SolidLine, Qt::RoundCap)); p.drawPath(branch); p.setPen(Qt::NoPen);
        for (int i = 0; i < 7; ++i) { const auto base = branch.pointAtPercent(.13 + i * .11); const int side = i % 2 ? 1 : -1; paintedLeaf(base, base + QPointF(side * 23, -21), 9, 79 + i * 3); }
    } else if (kind == 4) {
        for (int i = 0; i < 90; ++i) {
            const qreal a = noise(i, 1, seed) * 2 * pi, r = std::sqrt(noise(i, 2, seed)); const QPointF c(64 + std::cos(a) * r * 48, 68 + std::sin(a) * r * 29);
            const qreal radius = 3 + noise(i, 3, seed) * 9; QRadialGradient gradient(c - QPointF(2, 3), radius); gradient.setColorAt(0, QColor("#9aa957")); gradient.setColorAt(.6, QColor("#5d772e")); gradient.setColorAt(1, QColor("#334c26")); p.setBrush(gradient); organicBlob(p, c, radius, seed + i);
        }
    } else if (kind == 5) {
        for (int i = 0; i < 32; ++i) {
            const QPointF c(14 + noise(i, 1, seed) * 100, 40 + noise(i, 2, seed) * 52); const qreal r = 2 + noise(i, 3, seed) * 9;
            p.setBrush(QColor("#81aeb9")); p.drawEllipse(c + QPointF(1, 2), r, r * .55); p.setBrush(QColor("#e7f2ee")); p.drawEllipse(c, r, r * .48);
            p.setBrush(QColor("#ffffff")); p.drawEllipse(c - QPointF(r * .3, r * .12), r * .38, r * .13);
        }
    } else {
        for (int i = 0; i < 65; ++i) { const qreal a = noise(i, 1, seed) * 2 * pi, r = std::sqrt(noise(i, 2, seed)); p.setBrush(QColor::fromHsv(28 + int(noise(i, 3, seed) * 14), 105, 75 + int(noise(i, 4, seed) * 70))); organicBlob(p, {64 + std::cos(a) * r * 48, 86 + std::sin(a) * r * 20}, 3 + noise(i, 5, seed) * 11, seed + i, 6); }
        for (int i = 0; i < 37; ++i) { const QPointF base(19 + noise(i, 1, seed) * 88, 74 + noise(i, 2, seed) * 29); paintedLeaf(base, base + QPointF((noise(i, 3, seed) - .5) * 19, -10 - noise(i, 4, seed) * 34), 1.8, 69 + int(noise(i, 5, seed) * 20)); }
    }
    p.end(); return image;
}

void pressureFlow(BrushSettings &b, qreal minimum = .1) { b.flowDynamics.control = BrushControl::Pressure; b.flowDynamics.minimum = minimum; }
void texture(BrushSettings &b, Pattern type, quint32 seed, int revision, qreal depth = 1) {
    b.texture.enabled = true; b.texture.pattern = pattern(type, seed, revision); b.texture.name = "Original catalog surface";
    b.texture.depth = depth; b.texture.eachTip = false; b.texture.mode = MaskBlend::Multiply;
}
void variation(BrushSettings &b, qreal size, qreal angle, qreal scatter = 0) {
    b.sizeDynamics.jitter = size; b.angleDynamics.jitter = angle; b.scatter = scatter;
}
void naturalColor(BrushSettings &b, int revision) {
    b.colorDynamics.enabled = true; b.colorDynamics.hue = .015; b.colorDynamics.saturation = .035; b.colorDynamics.brightness = .055;
    b.colorDynamics.perTip = revision >= 2;
}

BrushPreset makePreset(const Entry &e, int index, int revision) {
    BrushPreset preset; preset.id = "builtin/catalog/" + QString::fromLatin1(e.id); preset.name = QString::fromUtf8(e.name);
    preset.source = "Toon Pro original anime catalog"; preset.builtIn = true;
    auto &b = preset.brush; const quint32 seed = saltFor(e.id);
    b.spacing = .12; b.sizeDynamics.minimum = .3;
    if (e.family == Ink) {
        constexpr qreal sizes[]{12, 15, 4, 9, 19, 64, 7, 26, 27, 5, 20, 15, 3, 17, 8, 28, 43, 12};
        constexpr qreal minimum[]{.07, .015, .18, .12, .12, .4, .01, .09, .025, 1, .3, .12, .2, .12, .22, .28, .07, .16};
        b.size = sizes[index]; b.sizeDynamics.minimum = minimum[index]; b.spacing = .055; preset.smoothing = index < 4 ? 12 : 5;
        b.sizeDynamics.response = {0, .15, .4, .7, 1}; pressureFlow(b, .72);
        if (index == 1 || index == 6) b.sizeDynamics.response = {0, .08, .23, .54, 1};
        if (index == 9) { b.pressureSize = false; b.flowDynamics.control = BrushControl::Off; }
        if (index == 4 || index == 11 || index == 12 || index == 14 || index == 17) {
            b.tipMask = dryTip(seed, index == 4 || index == 14, index == 11 ? .85 : .68, .45);
            b.spacing = .12; b.angleDynamics.control = BrushControl::Direction;
        }
        if (index == 7 || index == 10 || index == 15) { b.tipMask = flatTip(index == 10 ? 62 : 27); b.angle = index == 15 ? -43 : -25; }
        if (index == 8) { b.tipMask = flatTip(45, true); b.angle = -35; }
        if (index == 13) { b.tipMask = flatTip(40, true); b.angleDynamics.control = BrushControl::Direction; b.spacing = .12; }
        if (index == 16) { b.tipMask = dryTip(seed, false, .92, .15); b.spacing = .07; }
    } else if (e.family == Sketch) {
        constexpr qreal sizes[]{5, 16, 4, 44, 13, 3, 56, 25, 42, 7, 32, 15};
        constexpr qreal flows[]{.24, .42, .58, .3, .48, .6, .18, .68, .6, .72, .44, .8};
        b.size = sizes[index]; b.opacity = flows[index]; pressureFlow(b, .04); b.sizeDynamics.minimum = .35; preset.smoothing = index < 3 || index == 5 ? 5 : 0;
        b.hardness = index == 6 ? .05 : .78;
        texture(b, index < 6 ? FineGrain : CoarseGrain, seed, revision, index == 0 ? .65 : .95);
        if (index == 2 || index == 5) b.pressureSize = false;
        if (index == 3) { b.tipMask = dryTip(seed, false, .88, .1); b.sizeDynamics.control = BrushControl::Tilt; b.sizeDynamics.minimum = .22; b.hardness = .95; b.pressureSize = false; b.roundness = .45; b.projection = true; }
        if (index == 4 || index > 6) { b.tipMask = dryTip(seed, index == 8 || index == 11, .55 + index * .023, .2); b.spacing = .16; }
        if (index == 7 || index == 8 || index == 11) { b.roundness = .55; b.angle = -25; }
        if (index == 10) { b.angleDynamics.jitter = .09; b.flowDynamics.response = {0, .35, .64, .86, 1}; }
    } else if (e.family == Soft) {
        constexpr qreal sizes[]{78, 34, 104, 64, 90, 148, 23, 76, 83, 78, 63, 100, 96};
        constexpr qreal flows[]{.12, .09, .045, .52, .14, .07, .16, .24, .3, .9, .75, .65, .12};
        b.size = sizes[index]; b.opacity = flows[index]; b.hardness = 0; b.pressureSize = false; b.spacing = .14; pressureFlow(b, .02);
        if (index == 4) { b.roundness = .38; b.angleDynamics.control = BrushControl::Direction; }
        if (index == 1) b.roundness = .65;
        if (index == 5) b.strokeOpacity = .6;
        if (index == 2 || index == 7) { b.airbrush = true; b.airbrushRate = index == 2 ? 16 : 22; }
        if (index == 3 || (index >= 8 && index <= 11)) {
            b.tipMask = particles(seed, index == 11 ? 13 : index == 3 ? 43 : 26, index == 9 ? 8 : index == 10 ? 2.4 : 2.2, index == 10 ? 2 : index == 8 || index == 9 ? 1 : 0);
            b.hardness = 1; // Particle coverage is authored in the tip, not a blurred solid brush.
            b.spacing = index == 11 ? .8 : .4; variation(b, .25, index == 10 ? .04 : 1, .25); b.scatterBothAxes = true;
        }
        if (index == 12) texture(b, FineGrain, seed, revision, .35);
    } else if (e.family == Grass) {
        b.tipMask = grassTip(index, seed); b.size = index == 0 ? 62 : index == 1 ? 89 : index == 2 ? 75 : 46;
        b.spacing = index == 3 ? .35 : .82; b.opacity = index == 3 ? .72 : 1; b.sizeDynamics.minimum = .55;
        variation(b, .14, index == 2 ? .15 : .025); naturalColor(b, revision);
    } else if (e.family == Trees) {
        b.tipMask = foliageTip(index, seed); b.size = index == 0 ? 83 : index == 1 ? 108 : index == 2 ? 126 : index == 3 ? 86 : index == 4 ? 53 : 70;
        b.spacing = index == 5 ? .25 : index == 4 ? .75 : .88; b.sizeDynamics.minimum = .5;
        variation(b, .16, index == 0 || index == 2 ? .1 : .035); naturalColor(b, revision);
        if (index == 4) b.angleDynamics.control = BrushControl::Direction;
        if (index == 5) pressureFlow(b, .1);
    } else if (e.family == Clouds) {
        b.size = index == 0 ? 126 : index == 1 ? 166 : index == 2 ? 172 : index == 3 ? 96 : index == 4 ? 155 : 224;
        b.pressureSize = false; b.hardness = 0; b.opacity = index == 3 ? .3 : index == 4 ? .08 : .16; b.spacing = .18; pressureFlow(b, .03);
        if (index == 0 || index == 1 || index == 3) { b.tipMask = cloudTip(index == 1, index == 3, seed); b.hardness = 1; variation(b, .1, .018); }
        if (index == 2 || index == 5) b.roundness = index == 2 ? .3 : .5;
        if (index == 4) texture(b, FineGrain, seed, revision, .55);
    } else if (e.family == Terrain) {
        b.size = index == 6 ? 88 : 92; b.opacity = .7; b.sizeDynamics.minimum = .6; pressureFlow(b, .1); b.spacing = .22;
        constexpr Pattern patterns[]{CoarseGrain, FineGrain, CoarseGrain, Cracks, CoarseGrain, Strata, CoarseGrain, CoarseGrain};
        if (index == 1 || index == 4) { b.tipMask = particles(seed, index == 1 ? 50 : 20, index == 1 ? 1.6 : 5, index == 4 ? 3 : 0); b.spacing = .5; variation(b, .2, 1, .25); }
        else if (index == 6) { b.tipMask = accentTip(6, seed); b.spacing = .75; b.opacity = .85; variation(b, .16, .04); }
        else { texture(b, patterns[index], seed, revision, index == 0 ? .7 : .95); b.hardness = index == 7 ? .6 : .9; }
        if (index == 7) { b.texture.contrast = .35; b.texture.brightness = -.2; }
    } else if (e.family == Walls) {
        constexpr Pattern patterns[]{FineGrain, Bricks, Wood, CoarseGrain, FineGrain, Blocks, Tiles, Asphalt, Stucco, CoarseGrain, Cracks, Strata, CoarseGrain, CoarseGrain, Weave, Tatami};
        b.size = index == 9 || index == 10 ? 88 : 116; b.pressureSize = false; b.hardness = .8; b.opacity = .72; b.spacing = .15; pressureFlow(b, .1);
        if (index == 9 || index == 10 || index == 11) { b.tipMask = accentTip(index == 9 ? 7 : index == 10 ? 8 : 9, seed); b.spacing = .9; b.opacity = index == 11 ? .45 : .9; variation(b, .1, index == 11 ? 0 : .08); }
        else if (index == 12) { b.tipMask = particles(seed, 28, 4, 1); b.spacing = .5; variation(b, .2, 1, .15); }
        else texture(b, patterns[index], seed, revision, index == 0 || index == 4 ? .55 : 1);
        if (index == 13) { b.tipMask = flatTip(85); b.angle = 90; }
    } else if (e.family == Water) {
        b.size = index == 0 ? 98 : index == 1 ? 102 : index == 2 ? 66 : index == 3 ? 92 : 124; b.pressureSize = false; b.opacity = .8; pressureFlow(b, .1);
        if (index < 2) { b.tipMask = accentTip(index, seed); b.spacing = index == 0 ? .85 : .4; variation(b, .15, 0); }
        else if (index < 4) { b.tipMask = particles(seed, index == 2 ? 26 : 41, index == 2 ? 4 : 1.5, index == 2 ? 1 : 2); b.spacing = .42; variation(b, .3, .02, .3); }
        else { texture(b, Caustics, seed, revision); b.spacing = .18; }
    } else if (e.family == Tones) {
        b.size = index == 0 ? 55 : index == 1 ? 3 : 96; b.pressureSize = false; pressureFlow(b, .25);
        if (index == 0) { b.tipMask = accentTip(2, seed); b.spacing = .42; b.angleDynamics.control = BrushControl::Direction; }
        else if (index == 1) { b.spacing = 1.5; b.scatter = 3.8; b.scatterBothAxes = true; b.count = 2; b.sizeDynamics.jitter = .4; b.sizeDynamics.minimum = .45; }
        else { texture(b, index == 2 ? Crosshatch : Engraving, seed, revision); b.spacing = .16; }
        preset.compatibility = {"Hand-built shading marks; not resolution-aware screentones."};
    } else if (e.family == Effects) {
        b.size = index == 0 ? 109 : index == 1 ? 64 : index == 2 ? 104 : index == 3 ? 76 : index == 4 ? 43 : 116;
        b.pressureSize = false; b.spacing = .85; pressureFlow(b, .3);
        if (index == 0) { b.tipMask = accentTip(3, seed); b.angleDynamics.control = BrushControl::Direction; }
        if (index == 1) { b.tipMask = particles(seed, 14, 7, 1); b.scatter = .4; b.spacing = .6; variation(b, .3, 1, .4); }
        if (index == 2 || index == 5) { texture(b, index == 2 ? Speed : Flowers, seed, revision); b.spacing = .15; }
        if (index == 3) { b.tipMask = accentTip(4, seed); variation(b, .1, .01); }
        if (index == 4) { b.tipMask = accentTip(5, seed); b.spacing = 1.7; variation(b, .35, .035, .3); }
    } else if (e.family == Weather) {
        b.size = index == 0 ? 77 : index == 1 ? 105 : index == 2 ? 86 : index == 3 ? 70 : index == 4 ? 83 : index == 5 ? 67 : 17;
        b.pressureSize = false; b.spacing = index == 6 ? 1.8 : .75; b.opacity = index == 0 ? .45 : index == 1 ? .8 : index == 5 ? .2 : .65; pressureFlow(b, .03);
        if (index != 6) b.tipMask = particles(seed, index == 5 ? 10 : index == 1 ? 10 : 18, index == 0 ? 1.1 : index == 1 ? 2.9 : index == 2 ? 5 : index == 3 ? 2.4 : 2.8, index < 2 || index == 3 ? 2 : index == 2 || index == 5 ? 3 : 1);
        else b.hardness = 0;
        variation(b, index < 2 ? .2 : .45, index < 2 ? .01 : .2, index == 6 ? 2 : .45); b.scatterBothAxes = true;
        b.flowDynamics.jitter = .15;
        if (index != 6) { b.count = 2; b.countControl = BrushControl::Pressure; }
    } else {
        b.colorTip = coloredTip(index, seed); b.size = index == 0 ? 86 : index == 1 ? 72 : index == 2 ? 64 : index == 3 ? 112 : index == 4 ? 90 : index == 5 ? 98 : 126;
        b.sizeDynamics.minimum = .55; b.spacing = 1.05; variation(b, .12, index == 2 || index == 5 || index == 6 ? .018 : .05);
        b.opacityDynamics.control = BrushControl::Pressure; b.opacityDynamics.minimum = .8;
        preset.compatibility = {"Original layered procedural color artwork. Source colors are retained; foreground-color jitter does not tint colored stamps."};
    }
    if (revision >= 2) {
        // Grain and line patterns are continuous fields. Their canvas-anchored
        // texture already supplies detail, so a hard circular paint footprint
        // only adds visible overlapping disks, particularly at low pressure.
        // Keep intentional scenery/particle stamps separate from this treatment.
        const bool field = (e.family == Terrain && b.texture.enabled)
            || (e.family == Walls && b.texture.enabled)
            || (e.family == Water && index == 4)
            || (e.family == Tones && index >= 2)
            || (e.family == Effects && (index == 2 || index == 5));
        if (field) {
            b.spacing = .08;
            b.hardness = .08;
            b.opacity = .32;
            if (e.family == Walls && index == 13) {
                // The roller keeps its broad rectangular footprint, but its
                // authored edge is softened instead of printing repeated blocks.
                b.hardness = .15;
            }
        }
        const bool linePattern = (e.family == Terrain && (index == 3 || index == 5))
            || (e.family == Walls && (index == 1 || index == 2 || index == 5
                || index == 6 || index == 14 || index == 15))
            || (e.family == Tones && index >= 2)
            || (e.family == Effects && index == 2);
        if (linePattern) {
            // A zero texel must remain transparent even after many passes.
            // Partial texture depth mixes the round brush back into every gap.
            b.texture.depth = 1;
            b.texture.name = "Original line pattern";
            b.opacity = .55;
        }
        if (e.family == Soft && b.tipMask.isNull()) {
            const qreal spacing = index == 4 ? .06 : .08;
            b.opacity *= spacing / b.spacing;
            b.spacing = spacing;
        }
        if (e.family == Clouds) {
            b.opacity *= .08 / b.spacing;
            b.spacing = .08;
            if (!b.tipMask.isNull()) b.sizeDynamics.jitter = .18;
        }
        if (e.family == Grass) {
            variation(b, .28, index == 2 ? .22 : .045);
            b.pressureSize = false;
            pressureFlow(b, .18);
        } else if (e.family == Trees) {
            if (index != 5) variation(b, .27, index == 0 || index == 2 ? .16 : .055);
            b.pressureSize = false;
            pressureFlow(b, .18);
        } else if (e.family == Color) {
            // RGBA tips retain their painted colors. Vary their geometry per
            // dab, rather than suggesting unsupported color tint dynamics.
            variation(b, .28, index == 2 || index == 5 || index == 6 ? .035 : .10);
            b.pressureSize = false;
        }
    }
    return preset;
}

QVector<BrushPreset> makePresets(int revision) {
        QVector<BrushPreset> result; result.reserve(std::size(entries));
        std::array<int, 13> indices{};
        for (const auto &entry : entries) result.push_back(makePreset(entry, indices[entry.family]++, revision));
        if (revision >= 3) { for (auto &preset : result) refineCatalogBrushQuality(preset); result += advancedCatalogBrushPresets(); }
        if (revision >= 4) result += specializedCatalogBrushPresets(revision >= 5);
        return result;
}
const QVector<BrushPreset> &presets(int revision = catalogBrushRecipeRevision) {
    if (revision == 1) { static const auto original = makePresets(1); return original; }
    if (revision == 2) { static const auto previous = makePresets(2); return previous; }
    if (revision == 3) { static const auto previous = makePresets(3); return previous; }
    if (revision == 4) { static const auto previous = makePresets(4); return previous; }
    static const auto value = makePresets(catalogBrushRecipeRevision);
    return value;
}
const Entry *findEntry(const QString &id) {
    static const QString prefix = "builtin/catalog/";
    if (!id.startsWith(prefix)) return nullptr;
    const auto suffix = id.sliced(prefix.size());
    for (const auto &entry : entries) if (suffix == QLatin1String(entry.id)) return &entry;
    return nullptr;
}
} // namespace

QVector<BrushPreset> catalogBrushPresets(int revision) { return presets(revision); }
QString catalogBrushCategory(const QString &id) {
    const auto *entry = findEntry(id);
    if (entry) return QString::fromLatin1(familyNames[entry->family]);
    const auto advanced = advancedCatalogBrushCategory(id);
    return advanced.isEmpty() ? specializedCatalogBrushCategory(id) : advanced;
}
QStringList catalogBrushTags(const QString &id) {
    const auto *entry = findEntry(id);
    if (!entry) { const auto advanced = advancedCatalogBrushTags(id); return advanced.isEmpty() ? specializedCatalogBrushTags(id) : advanced; }
    QStringList tags{"Standard", "Default", "Anime"};
    tags.append(QString::fromLatin1(entry->tags).split(','));
    const auto &b = presets()[entry - entries].brush;
    if (!b.tipFrames.isEmpty()) tags.append({"Multiple tips", "No repeat"});
    if (b.texture.name == "Original line pattern") tags.append({"Pattern", "Line-only"});
    if (b.sizeDynamics.jitter > 0 || b.angleDynamics.jitter > 0 || b.flowDynamics.jitter > 0 ||
        b.scatter > 0 || (b.colorDynamics.enabled && (b.colorDynamics.hue > 0 || b.colorDynamics.brightness > 0)))
        tags.append({"Dynamic", "Random"});
    return tags;
}
} // namespace toon
