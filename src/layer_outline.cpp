#include "layer_outline.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace toon {
namespace {
constexpr float infinity=1e20f;
// Lower envelope of squared-distance parabolas. This separable transform is
// linear in the number of pixels, independently of the chosen outline width.
void distanceLine(const std::vector<float> &input,int n,std::vector<float> &output,
                  std::vector<int> &nearest,std::vector<int> &sites,std::vector<double> &limits){
    int last=-1;
    for(int q=0;q<n;++q){
        if(input[q]>=infinity)continue;
        double start=-std::numeric_limits<double>::infinity();
        while(last>=0){const int p=sites[last];start=(double(input[q])-input[p]+double(q)*q-double(p)*p)/(2.*(q-p));if(start>limits[last])break;--last;}
        ++last;sites[last]=q;limits[last]=last==0?-std::numeric_limits<double>::infinity():start;limits[last+1]=std::numeric_limits<double>::infinity();
    }
    if(last<0){std::fill_n(output.begin(),n,infinity);std::fill_n(nearest.begin(),n,-1);return;}
    int k=0;for(int q=0;q<n;++q){while(k<last&&limits[k+1]<q)++k;const int p=sites[k];output[q]=float(double(q-p)*(q-p)+input[p]);nearest[q]=p;}
}
}
bool applyLayerOutline(QImage &image,qreal width,QColor color,const std::function<bool()> &cancelled){
    if(image.isNull()||image.format()!=QImage::Format_ARGB32_Premultiplied||!std::isfinite(width)||width<0||width>128||!color.isValid()||color.alpha()!=255)return false;
    if(width==0)return true;
    const auto stopped=[&]{return cancelled&&cancelled();};if(stopped())return false;
    const int w=image.width(),h=image.height(),longest=qMax(w,h);const qint64 count=qint64(w)*h;
    if(count*13+qint64(longest)*28+8>128LL*1024*1024)return false;
    try{
        std::vector<float> distance(size_t(count),infinity),input(longest),output(longest);
        std::vector<int> nearest(size_t(count),-1),indices(longest),sites(longest),labels(longest);
        std::vector<double> limits(size_t(longest)+1);std::vector<uchar> strength(size_t(count),0);
        bool any=false;
        for(int y=0;y<h;++y){
            if(stopped())return false;const auto *row=reinterpret_cast<const QRgb*>(image.constScanLine(y));
            for(int x=0;x<w;++x){
                const int alpha=qAlpha(row[x]);input[x]=alpha?0:infinity;if(!alpha)continue;any=true;int maximum=alpha;
                // A one-pixel neighborhood prevents antialias fringes from
                // weakening the entire expanded border. Uniform translucent
                // regions retain their opacity rather than acquiring a fill.
                if(alpha<255)for(int yy=qMax(0,y-1);yy<=qMin(h-1,y+1);++yy){const auto *other=reinterpret_cast<const QRgb*>(image.constScanLine(yy));for(int xx=qMax(0,x-1);xx<=qMin(w-1,x+1);++xx)maximum=qMax(maximum,qAlpha(other[xx]));}
                strength[size_t(y)*w+x]=uchar(maximum);
            }
            distanceLine(input,w,output,indices,sites,limits);
            for(int x=0;x<w;++x){distance[size_t(y)*w+x]=output[x];nearest[size_t(y)*w+x]=indices[x]<0?-1:y*w+indices[x];}
        }
        if(!any)return true;
        for(int x=0;x<w;++x){
            if(stopped())return false;
            for(int y=0;y<h;++y){input[y]=distance[size_t(y)*w+x];labels[y]=nearest[size_t(y)*w+x];}
            distanceLine(input,h,output,indices,sites,limits);
            for(int y=0;y<h;++y){distance[size_t(y)*w+x]=output[y];nearest[size_t(y)*w+x]=indices[y]<0?-1:labels[indices[y]];}
        }
        QImage result=image.copy();if(result.isNull())return false;
        for(int y=0;y<h;++y){
            if(stopped())return false;auto *row=reinterpret_cast<QRgb*>(result.scanLine(y));
            for(int x=0;x<w;++x){
                const auto i=size_t(y)*w+x;const int site=nearest[i];if(site<0)continue;
                const int oldAlpha=qAlpha(row[x]);if(oldAlpha==255)continue;
                const double coverage=qBound(0.,width+1-std::sqrt(double(distance[i])),1.);
                const int expanded=qRound(strength[site]*coverage),extra=qMax(0,expanded-oldAlpha);if(!extra)continue;
                // Normalized destination coverage makes the composite alpha
                // max(source, expanded), without accumulating alpha twice.
                const int border=qBound(0,qRound(extra*255./(255-oldAlpha)),255),visible=(border*(255-oldAlpha)+127)/255;
                row[x]=qRgba(qRed(row[x])+(color.red()*visible+127)/255,qGreen(row[x])+(color.green()*visible+127)/255,qBlue(row[x])+(color.blue()*visible+127)/255,oldAlpha+visible);
            }
        }
        image=std::move(result);return true;
    }catch(const std::bad_alloc &){return false;}
}
}
