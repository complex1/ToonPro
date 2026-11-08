#include "stroke_filter.h"
#include <QLineF>
#include <algorithm>
#include <cmath>

namespace toon {
static bool validSample(QPointF point, qreal pressure) {
    return std::isfinite(point.x()) && std::isfinite(point.y()) && std::isfinite(pressure);
}
StrokeSample StrokeFilter::begin(QPointF point, qreal pressure, qreal strength) {
    reset();
    m_failed=false;
    if (!validSample(point, pressure)) {m_failed=true;return {};}
    m_active = true;
    m_legacy = true;m_spacing=1;
    m_length = std::isfinite(strength) ? qBound(0.0, strength, 100.0) * .24 : 0;
    m_raw = m_filtered = m_tip = {point, qBound(0.0, pressure, 1.0)};
    return m_tip;
}
StrokeSample StrokeFilter::begin(QPointF point,qreal pressure,const StrokeFilterSettings &settings) {
    const auto sample=begin(point,pressure,settings.strength);
    if(!m_active)return sample;
    m_legacy=false;m_settings=settings;
    if(m_settings.mode!=StrokeFilterMode::Smooth&&m_settings.mode!=StrokeFilterMode::Stabilizer)m_settings.mode=StrokeFilterMode::Smooth;
    m_settings.strength=std::isfinite(settings.strength)?qBound(0.,settings.strength,100.):0;
    m_settings.pixelScale=std::isfinite(settings.pixelScale)&&settings.pixelScale>0?qBound(.0001,settings.pixelScale,10000.):1;
    m_settings.deadZone=std::isfinite(settings.deadZone)?qBound(0.,settings.deadZone,20.):0;
    const qreal amount=m_settings.strength/100;
    m_length=(m_settings.mode==StrokeFilterMode::Stabilizer?96*amount*amount:24*amount)*m_settings.pixelScale;
    m_spacing=m_settings.pixelScale;m_untilSample=m_spacing;
    m_pathRaw=m_raw;m_progress=m_filteredProgress=0;m_history.append({m_raw,0});
    return sample;
}
void StrokeFilter::reset() {
    m_active = false;
    m_untilSample = 1;
    m_history.clear();m_progress=m_filteredProgress=0;
}
void StrokeFilter::fail(){reset();m_failed=true;}
void StrokeFilter::integrate(const StrokeSample &next, qreal distance) {
    // Exact solution to dy/ds = (x(s) - y) / length for a linear input
    // segment. Splitting that segment into more tablet events gives the same
    // result, including linearly interpolated pressure.
    const qreal decay = std::exp(-distance / m_length);
    const qreal response = -std::expm1(-distance / m_length);
    const qreal ramp = distance - m_length * response;
    m_filtered.point = m_filtered.point * decay + m_raw.point * response
        + (next.point - m_raw.point) * (ramp / distance);
    m_filtered.pressure = m_filtered.pressure * decay + m_raw.pressure * response
        + (next.pressure - m_raw.pressure) * (ramp / distance);
    m_raw = next;
}
void StrokeFilter::appendConfigured(const StrokeSample &next,QVector<StrokeSample> &output) {
    const qreal distance=QLineF(m_tip.point,next.point).length();
    if(distance<1e-9){m_tip.pressure=next.pressure;return;}
    if(!std::isfinite(distance)||distance/m_spacing>4096||output.size()+distance/m_spacing>65536){output.clear();fail();return;}
    const int steps=qMax(1,int(std::ceil(distance/m_spacing-1e-9)));
    const auto start=m_tip;
    for(int i=1;i<=steps;++i) {
        const qreal fraction=qreal(i)/steps;
        output.append({start.point+(next.point-start.point)*fraction,
            qBound(0.,start.pressure+(next.pressure-start.pressure)*fraction,1.)});
    }
    m_tip=next;
}
void StrokeFilter::processConfigured(const StrokeSample &next,QVector<StrokeSample> &output,bool forceEndpoint) {
    const auto delta=next.point-m_pathRaw.point;const qreal distance=QLineF({},delta).length();
    const qreal zone=forceEndpoint||m_settings.mode!=StrokeFilterMode::Stabilizer?0:m_settings.deadZone*m_settings.pixelScale;
    if(distance<=zone+1e-9) {
        // A stationary pen still controls pressure, but sensor tremor cannot
        // accumulate travelled distance and slowly walk the stable tip away.
        m_pathRaw.pressure=m_filtered.pressure=m_tip.pressure=next.pressure;
        if(!m_history.isEmpty())m_history.last().sample.pressure=next.pressure;
        return;
    }
    const StrokeSample accepted{next.point-delta*(zone/distance),next.pressure};
    const qreal step=distance-zone,previousProgress=m_progress;
    m_progress+=step;m_history.append({accepted,m_progress});
    if(m_settings.mode==StrokeFilterMode::Stabilizer) {
        const auto offset=accepted.point-m_filtered.point;const qreal separation=QLineF({},offset).length();
        if(separation>m_length) {
            const qreal fraction=1-m_length/separation;
            m_filtered.point+=offset*fraction;
            m_filteredProgress+=(m_progress-m_filteredProgress)*fraction;
        }
        const qreal response=-std::expm1(-step/qMax(m_spacing,m_length*.15));
        m_filtered.pressure+=(accepted.pressure-m_filtered.pressure)*response;
    } else {
        const qreal decay=std::exp(-step/m_length),response=-std::expm1(-step/m_length);
        const qreal ramp=step-m_length*response;
        m_filtered.point=m_filtered.point*decay+m_pathRaw.point*response+(accepted.point-m_pathRaw.point)*(ramp/step);
        m_filtered.pressure=m_filtered.pressure*decay+m_pathRaw.pressure*response+(accepted.pressure-m_pathRaw.pressure)*(ramp/step);
        m_filteredProgress=m_filteredProgress*decay+previousProgress*response+ramp;
    }
    m_pathRaw=accepted;appendConfigured(m_filtered,output);
    // Retain enough ordered path for the remaining lag plus a local averaging
    // window. A bounded queue also protects very long, sub-leash scribbles.
    const qreal oldest=qMax(0.,m_filteredProgress-2*m_length-8*m_spacing);
    int remove=0;while(remove+1<m_history.size()-1&&m_history[remove+1].distance<oldest)++remove;
    remove=qMax(remove,int(m_history.size())-4096);
    if(remove>0)m_history.remove(0,remove);
}
StrokeSample StrokeFilter::historyAt(qreal distance) const {
    if(m_history.isEmpty())return m_pathRaw;
    const auto right=std::lower_bound(m_history.cbegin(),m_history.cend(),distance,
        [](const HistorySample &sample,qreal value){return sample.distance<value;});
    if(right==m_history.cbegin())return right->sample;
    if(right==m_history.cend())return m_history.last().sample;
    const auto &a=*(right-1),&b=*right;const qreal fraction=(distance-a.distance)/(b.distance-a.distance);
    return {a.sample.point+(b.sample.point-a.sample.point)*fraction,
        a.sample.pressure+(b.sample.pressure-a.sample.pressure)*fraction};
}
StrokeSample StrokeFilter::weightedHistory(qreal distance,qreal radius) const {
    if(radius<1e-9||m_history.size()<2)return historyAt(distance);
    QPointF point;qreal pressure=0,weight=0;
    // Exact integration of a triangular kernel over piecewise-linear history.
    // Splitting a history edge cannot change the resulting average.
    const auto firstEdge=std::lower_bound(m_history.cbegin(),m_history.cend(),distance-radius,
        [](const HistorySample &sample,qreal value){return sample.distance<value;});
    for(int i=qMax(1,int(firstEdge-m_history.cbegin()));i<m_history.size();++i) {
        if(m_history[i-1].distance>distance+radius)break;
        const qreal left=qMax(distance-radius,m_history[i-1].distance),right=qMin(distance+radius,m_history[i].distance);
        if(right<=left)continue;
        const qreal split=qBound(left,distance,right);
        for(const auto range:{std::pair{left,split},std::pair{split,right}}) {
            const qreal a=range.first,b=range.second;if(b-a<1e-12)continue;
            const auto first=historyAt(a),last=historyAt(b);
            const qreal wa=qMax(0.,1-qAbs(a-distance)/radius),wb=qMax(0.,1-qAbs(b-distance)/radius),wm=(wa+wb)/2;
            const qreal scale=(b-a)/6;
            point+=(first.point*wa+(first.point+last.point)*(2*wm)+last.point*wb)*scale;
            pressure+=(first.pressure*wa+(first.pressure+last.pressure)*(2*wm)+last.pressure*wb)*scale;
            weight+=(wa+4*wm+wb)*scale;
        }
    }
    return weight>1e-12?StrokeSample{point/weight,qBound(0.,pressure/weight,1.)}:historyAt(distance);
}
void StrokeFilter::completeTail(QVector<StrokeSample> &output) {
    if(m_history.isEmpty())return;
    const qreal start=qBound(m_history.first().distance,m_filteredProgress,m_progress),length=m_progress-start;
    if(!std::isfinite(length)||length/m_spacing>512){output.clear();fail();return;}
    if(length<1e-9){appendConfigured(m_pathRaw,output);return;}
    const qreal radius=qMin(m_length*.3,length*.5);
    const auto first=weightedHistory(start,radius);
    const auto offset=m_tip.point-first.point;const qreal pressureOffset=m_tip.pressure-first.pressure;
    const int count=qMax(1,int(std::ceil(length/m_spacing-1e-9)));
    for(int i=1;i<=count;++i) {
        const qreal fraction=qreal(i)/count,position=start+length*fraction;
        auto value=weightedHistory(position,qMin(m_length*.3,(m_progress-position)*.5));
        const qreal blend=1-fraction*fraction*(3-2*fraction);
        value.point+=offset*blend;value.pressure=qBound(0.,value.pressure+pressureOffset*blend,1.);
        appendConfigured(value,output);
        if(!m_active)return;
    }
}
QVector<StrokeSample> StrokeFilter::advance(QPointF point, qreal pressure) {
    QVector<StrokeSample> samples;
    if (!m_active) return samples;
    if(!validSample(point,pressure)){fail();return samples;}
    const StrokeSample next{point, qBound(0.0, pressure, 1.0)};
    const qreal distance = QLineF(m_raw.point, point).length();
    // Never interpolate a corrupted, implausibly distant input event. Canvas
    // cancels the gesture rather than letting it connect through the artwork.
    if (distance > 50000 || (!m_legacy&&distance/m_spacing>65536)) { fail(); return samples; }
    if (m_length == 0) {
        m_raw = m_filtered = m_tip = next;
        samples.append(next);
        return samples;
    }
    if (distance < 1e-9) {
        if(!m_legacy&&next.pressure==m_raw.pressure)return samples;
        m_raw.pressure = m_filtered.pressure = m_tip.pressure = next.pressure;
        if(!m_legacy){m_pathRaw.pressure=next.pressure;if(!m_history.isEmpty())m_history.last().sample.pressure=next.pressure;}
        return samples;
    }
    const StrokeSample start = m_raw;
    qreal travelled = 0;
    // Emit at fixed one-pixel intervals of raw path length. This also prevents
    // event frequency from changing the polygon rendered by the raster engine.
    while (distance - travelled >= m_untilSample - 1e-9 && distance - travelled > 1e-9) {
        const qreal step = qMin(m_untilSample, distance - travelled);
        travelled += step;
        const qreal fraction = travelled / distance;
        const StrokeSample sample{start.point+(next.point-start.point)*fraction,start.pressure+(next.pressure-start.pressure)*fraction};
        if(m_legacy){integrate(sample,step);m_tip=m_filtered;samples.append(m_tip);}
        else processConfigured(sample,samples);
        if(!m_active){samples.clear();return samples;}
        m_untilSample = m_spacing;
    }
    const qreal remainder = distance - travelled;
    if (remainder > 1e-9) {
        if(m_legacy)integrate(next, remainder);
        m_untilSample -= remainder;
    }
    if(!m_legacy)m_raw=next;
    return samples;
}
QVector<StrokeSample> StrokeFilter::finish(QPointF point, qreal pressure) {
    auto samples = advance(point, pressure);
    if (!m_active) return samples;
    if(!m_legacy) {
        if(m_length>0) {
            if(QLineF(m_pathRaw.point,m_raw.point).length()>1e-9)
                processConfigured(m_raw,samples,m_settings.finishAtEndpoint&&m_progress>1e-9);
            if(m_active&&m_settings.finishAtEndpoint)completeTail(samples);
        }
        reset();return samples;
    }
    // The lag is bounded by the filter length plus one sampling pixel. Finish it directly,
    // within this gesture, so lifting never leaves a missing end or a timer tail.
    if (m_length > 0 && QLineF(m_tip.point, m_raw.point).length() > 1e-9) {
        m_tip = m_raw;
        samples.append(m_tip);
    }
    reset();
    return samples;
}
} // namespace toon
