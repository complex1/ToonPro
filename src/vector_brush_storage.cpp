#include "vector_brush_storage.h"
#include "editable_object.h"
#include "brush_settings.h"

#include <QCache>
#include <QBitArray>
#include <QCryptographicHash>
#include <QDataStream>
#include <QJsonArray>
#include <QIODevice>
#include <QSet>
#include <cmath>

namespace toon {
namespace {
bool fail(QString *error, const QString &message) { if (error) *error = message; return false; }
bool range(qreal value, qreal low, qreal high) { return std::isfinite(value) && value >= low && value <= high; }
bool point(QPointF value) { return range(value.x(),-32768,32768) && range(value.y(),-32768,32768); }
bool number(const QJsonValue &value, qreal *out) {
    if (!value.isDouble() || !std::isfinite(value.toDouble())) return false;
    *out = value.toDouble(); return true;
}
bool resource(const QImage &image) {
    return image.isNull() || (image.format() == QImage::Format_Grayscale8
        && image.width() <= maxVectorBrushResourceDimension && image.height() <= maxVectorBrushResourceDimension);
}
QJsonArray pathToJson(const QPainterPath &path) {
    QJsonArray result;
    for (int i=0;i<path.elementCount();++i) {
        const auto e = path.elementAt(i); result.append(QJsonArray{int(e.type),e.x,e.y});
    }
    return result;
}
bool pathFromJson(const QJsonArray &json, QPainterPath *path) {
    if (json.size() > 262144) return false;
    auto read = [&](int i, int type, QPointF *out) {
        if (i >= json.size() || !json[i].isArray()) return false;
        const auto a = json[i].toArray(); qreal x,y;
        if (a.size()!=3 || a[0]!=type || !number(a[1],&x) || !number(a[2],&y)) return false;
        *out={x,y}; return point(*out);
    };
    for (int i=0;i<json.size();++i) {
        const auto a=json[i].toArray(); if (a.size()!=3) return false;
        const int type=a[0].toInt(-1); QPointF p,c1,c2;
        if (i==0 && type!=QPainterPath::MoveToElement) return false;
        if (type==QPainterPath::MoveToElement && read(i,type,&p)) path->moveTo(p);
        else if (type==QPainterPath::LineToElement && read(i,type,&p)) path->lineTo(p);
        else if (type==QPainterPath::CurveToElement && read(i,type,&c1)
            && read(i+1,QPainterPath::CurveToDataElement,&c2) && read(i+2,QPainterPath::CurveToDataElement,&p)) {
            path->cubicTo(c1,c2,p); i+=2;
        } else return false;
    }
    return true;
}
bool color(const QJsonValue &value, QColor *out) {
    if (!value.isString()) return false;
    const auto text=value.toString(); *out=QColor(text);
    return text.size()==9 && text.startsWith('#') && out->isValid()
        && out->name(QColor::HexArgb)==text.toLower();
}
}

bool validVectorBrushResourceId(const QString &id) {
    if (id.size()!=64) return false;
    for (const auto c : id) if (!((c>='0' && c<='9') || (c>='a' && c<='f'))) return false;
    return true;
}
QString vectorBrushResourceId(const QImage &image) {
    if (image.isNull()) return {};
    // Immutable QImage cache keys avoid rehashing shared stamp images while
    // dragging an editable path or recording undo metadata.
    static thread_local QCache<qint64,QString> cache(1024);
    if (const auto cached=cache.object(image.cacheKey())) return *cached;
    QByteArray size; QDataStream stream(&size,QIODevice::WriteOnly);
    stream << qint32(image.width()) << qint32(image.height()) << qint32(image.format());
    QCryptographicHash hash(QCryptographicHash::Sha256); hash.addData(size);
    const qsizetype bytes = image.format()==QImage::Format_Grayscale8 ? image.width() : image.bytesPerLine();
    for (int y=0;y<image.height();++y) hash.addData(QByteArrayView(reinterpret_cast<const char *>(image.constScanLine(y)),bytes));
    const auto id=QString::fromLatin1(hash.result().toHex()); cache.insert(image.cacheKey(),new QString(id)); return id;
}

bool validateVectorBrushStroke(const VectorBrushStroke &s, QString *error) {
    if (!validateBrushSettings(s.settings,error)) return false;
    if (!s.settings.color.isValid() || !s.settings.backgroundColor.isValid()
        || !resource(s.settings.tipMask) || !resource(s.settings.texture.pattern) || !resource(s.settings.dual.tipMask))
        return fail(error,"Vector brush colors or image resources are invalid.");
    if (s.samples.isEmpty() || s.samples.size()>maxVectorBrushSamples || s.sourceGeometry.size()!=32
        || !range(s.canvasSize.width(),64,8192) || !range(s.canvasSize.height(),64,8192)
        || !range(s.selectionFeather,0,100) || s.selection.elementCount()>262144)
        return fail(error,"Vector brush capture exceeds its supported limits.");
    qreal previous=0; QBitArray dabIndices(maxVectorBrushSamples);
    for (const auto &sample:s.samples) {
        const auto &input=sample.input;
        if (!point(sample.point) || !range(input.pressure,0,1) || !range(input.tiltX,-90,90)
            || !range(input.tiltY,-90,90) || !range(input.rotation,-720,720) || !range(input.wheel,0,1)
            || !range(input.speed,0,1000000) || !range(sample.direction,-360,360) || !range(sample.initialDirection,-360,360)
            || !range(sample.position,previous,1) || sample.dabIndex>=maxVectorBrushSamples
            || dabIndices.testBit(int(sample.dabIndex)))
            return fail(error,"Vector brush contains invalid tablet samples.");
        previous=sample.position; dabIndices.setBit(int(sample.dabIndex));
    }
    for (int i=0;i<s.selection.elementCount();++i) {
        const auto e=s.selection.elementAt(i); if (!point({e.x,e.y})) return fail(error,"Vector brush selection exceeds the artwork range.");
    }
    return true;
}

bool usesModernVectorBrushCapture(const VectorBrushStroke &s) {
    if (usesModernBrushFeatures(s.settings)) return true;
    for (const auto &sample : s.samples) if (sample.input.speed != 0) return true;
    return false;
}
QJsonObject vectorBrushStrokeToJson(const VectorBrushStroke &s) {
    QJsonArray samples;
    const bool modern = usesModernVectorBrushCapture(s);
    for (const auto &p:s.samples) {
        QJsonArray sample{p.point.x(),p.point.y(),p.input.pressure,p.input.tiltX,
            p.input.tiltY,p.input.rotation,p.input.wheel,p.direction,p.initialDirection,p.position,double(p.dabIndex)};
        if (modern) sample.append(p.input.speed);
        samples.append(sample);
    }
    QJsonObject result{{"version",modern?6:s.settings.colorDynamics.applyToColorTip?5:usesAdvancedBrushTips(s.settings)?4:usesExtendedBrushDynamics(s.settings)?3:s.settings.colorTip.isNull()?1:2},{"settings",brushSettingsToJson(s.settings)},
        {"color",s.settings.color.name(QColor::HexArgb)},{"backgroundColor",s.settings.backgroundColor.name(QColor::HexArgb)},
        {"seed",double(s.seed)},{"samples",samples},{"sourceGeometry",QString::fromLatin1(s.sourceGeometry.toHex())},
        {"canvasSize",QJsonArray{s.canvasSize.width(),s.canvasSize.height()}},
        {"selection",pathToJson(s.selection)},{"selectionFillRule",int(s.selection.fillRule())},
        {"selectionFeather",s.selectionFeather},{"selectionActive",s.selectionActive},{"alphaLocked",s.alphaLocked}};
    for (const auto &[key,image]:{std::pair{"tipResource",&s.settings.tipMask},
         {"textureResource",&s.settings.texture.pattern},{"dualTipResource",&s.settings.dual.tipMask},{"colorTipResource",&s.settings.colorTip},{"surfaceResource",&s.settings.surface.image}})
        if (!image->isNull()) result[key]=vectorBrushResourceId(*image);
    return result;
}

bool vectorBrushStrokeFromJson(const QJsonObject &o, VectorBrushStroke *stroke, QString *error) {
    const QSet<QString> fields{"version","settings","color","backgroundColor","seed","samples","sourceGeometry",
        "canvasSize","selection","selectionFillRule","selectionFeather","selectionActive","alphaLocked","tipResource","textureResource","dualTipResource","colorTipResource","surfaceResource"};
    for (auto it=o.begin();it!=o.end();++it) if (!fields.contains(it.key())) return fail(error,"Unknown vector brush capture field.");
    if (o.contains("colorTipResource") && o.value("version") != 2 && o.value("version") != 3 && o.value("version") != 4 && o.value("version") != 5 && o.value("version") != 6)
        return fail(error,"Colored vector stamps require capture version 2 or later.");
    VectorBrushStroke s; qreal seed;
    const auto canvas=o.value("canvasSize").toArray(); qreal width,height;
    if ((o.value("version")!=1 && o.value("version")!=2 && o.value("version")!=3 && o.value("version")!=4 && o.value("version")!=5 && o.value("version")!=6) || !o.value("settings").isObject()
        || !brushSettingsFromJson(o.value("settings").toObject(),s.settings,error)
        || !color(o.value("color"),&s.settings.color) || !color(o.value("backgroundColor"),&s.settings.backgroundColor)
        || !number(o.value("seed"),&seed) || seed<0 || seed>4294967295.0 || seed!=std::floor(seed)
        || canvas.size()!=2 || !number(canvas[0],&width) || !number(canvas[1],&height)
        || !o.value("samples").isArray() || o.value("samples").toArray().size()>maxVectorBrushSamples
        || !o.value("sourceGeometry").isString() || !validVectorBrushResourceId(o.value("sourceGeometry").toString())
        || !o.value("selection").isArray() || !pathFromJson(o.value("selection").toArray(),&s.selection)
        || (o.value("selectionFillRule")!=0 && o.value("selectionFillRule")!=1)
        || !number(o.value("selectionFeather"),&s.selectionFeather) || !o.value("selectionActive").isBool() || !o.value("alphaLocked").isBool())
        return fail(error,"Missing or invalid vector brush capture data.");
    if (o.value("version").toInt() < 6 && (usesModernBrushFeatures(s.settings) || o.contains("surfaceResource")))
        return fail(error,"Connected brushes, surfaces and advanced dynamics require capture version 6.");
    if (s.settings.surface.kind == BrushSurfaceKind::Image && !o.contains("surfaceResource"))
        return fail(error,"An image surface requires its image resource.");
    if (o.value("version").toInt() < 5 && s.settings.colorDynamics.applyToColorTip)
        return fail(error,"Image color dynamics require capture version 5.");
    if (o.value("version").toInt() < 4 && usesAdvancedBrushTips(s.settings))
        return fail(error,"Tip sets and larger brushes require capture version 4.");
    if (!s.settings.tipFrames.isEmpty() && !o.contains("tipResource") && !o.contains("colorTipResource"))
        return fail(error,"A brush tip set requires its atlas resource.");
    if (o.value("version").toInt() < 3 && usesExtendedBrushDynamics(s.settings))
        return fail(error,"Response curves, stroke jitter and rotation controls require capture version 3.");
    s.seed=quint32(seed); s.canvasSize={width,height}; s.sourceGeometry=QByteArray::fromHex(o.value("sourceGeometry").toString().toLatin1());
    const auto fillRule=Qt::FillRule(o.value("selectionFillRule").toInt());
    // Setting even the existing rule on a default empty QPainterPath creates a
    // MoveTo(0,0). Keep an absent selection empty across project round trips.
    if (s.selection.fillRule()!=fillRule) s.selection.setFillRule(fillRule);
    s.selectionActive=o.value("selectionActive").toBool(); s.alphaLocked=o.value("alphaLocked").toBool();
    for (const auto &value:o.value("samples").toArray()) {
        if (!value.isArray()) return fail(error,"Invalid vector brush sample.");
        const auto a=value.toArray(); const int count = o.value("version").toInt() >= 6 ? 12 : 11; if (a.size()!=count) return fail(error,"Invalid vector brush sample.");
        qreal n[12]{}; for (int i=0;i<count;++i) if (!number(a[i],&n[i])) return fail(error,"Invalid vector brush sample.");
        if(n[10]<0 || n[10]>=maxVectorBrushSamples || n[10]!=std::floor(n[10])) return fail(error,"Invalid vector brush dab index.");
        VectorBrushSample p; p.point={n[0],n[1]}; p.input={n[2],n[3],n[4],n[5],n[6],n[11]};
        p.direction=n[7]; p.initialDirection=n[8]; p.position=n[9]; p.dabIndex=quint64(n[10]); s.samples.append(p);
    }
    for (const auto key:{"tipResource","textureResource","dualTipResource","colorTipResource","surfaceResource"})
        if (o.contains(key) && (!o.value(key).isString() || !validVectorBrushResourceId(o.value(key).toString())))
            return fail(error,"Invalid vector brush image reference.");
    if (!validateVectorBrushStroke(s,error)) return false;
    *stroke=std::move(s); return true;
}
} // namespace toon
