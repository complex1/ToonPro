#include "advanced_catalog_brushes.h"
#include "brush_engine.h"
#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QRadialGradient>
#include <cmath>
#include <functional>

namespace toon {
namespace {
constexpr qreal pi=3.14159265358979323846;
struct Entry {const char *id,*name,*family,*tags;};
constexpr Entry entries[]{
    {"meadow-tuft-set", "Meadow Tuft Set", "Grass and ground cover", "Grass and ground cover,Multi-tip,Dynamic"},
    {"tall-grass-set", "Tall Grass Set", "Grass and ground cover", "Grass and ground cover,Multi-tip,Dynamic"},
    {"dry-grass-set", "Dry Grass Set", "Grass and ground cover", "Grass and ground cover,Multi-tip,Dynamic"},
    {"reed-bed", "Reed Bed", "Grass and ground cover", "Grass and ground cover,Multi-tip,Dynamic"},
    {"wildflower-scatter", "Wildflower Scatter", "Grass and ground cover", "Grass and ground cover,Multi-tip,Dynamic"},
    {"dandelion-heads", "Dandelion Heads", "Grass and ground cover", "Grass and ground cover,Multi-tip,Dynamic"},
    {"fern-ground-cover", "Fern Ground Cover", "Grass and ground cover", "Grass and ground cover,Multi-tip,Dynamic"},
    {"weed-cracks", "Weed Cracks", "Grass and ground cover", "Grass and ground cover,Multi-tip,Dynamic"},
    {"leaf-litter", "Leaf Litter", "Grass and ground cover", "Grass and ground cover,Multi-tip,Dynamic"},
    {"snow-grass-fringe", "Snow Grass Fringe", "Grass and ground cover", "Grass and ground cover,Multi-tip,Dynamic"},
    {"trampled-grass", "Trampled Grass", "Grass and ground cover", "Grass and ground cover,Multi-tip,Dynamic"},
    {"broadleaf-crown-set", "Broadleaf Crown Set", "Trees and foliage", "Trees and foliage,Multi-tip,Dynamic"},
    {"conifer-crown-set", "Conifer Crown Set", "Trees and foliage", "Trees and foliage,Multi-tip,Dynamic"},
    {"bare-tree-set", "Bare Tree Set", "Trees and foliage", "Trees and foliage,Multi-tip,Dynamic"},
    {"bamboo-leaf-set", "Bamboo Leaf Set", "Trees and foliage", "Trees and foliage,Multi-tip,Dynamic"},
    {"palm-frond-set", "Palm Frond Set", "Trees and foliage", "Trees and foliage,Multi-tip,Dynamic"},
    {"shrub-cluster-set", "Shrub Cluster Set", "Trees and foliage", "Trees and foliage,Multi-tip,Dynamic"},
    {"hedge-edge", "Hedge Edge", "Trees and foliage", "Trees and foliage,Multi-tip,Dynamic"},
    {"cherry-blossom-cluster", "Cherry Blossom Cluster", "Trees and foliage", "Trees and foliage,Multi-tip,Dynamic"},
    {"autumn-canopy", "Autumn Canopy", "Trees and foliage", "Trees and foliage,Multi-tip,Dynamic"},
    {"distant-forest-strip", "Distant Forest Strip", "Trees and foliage", "Trees and foliage,Repeat,Dynamic"},
    {"cumulus-mass-set", "Cumulus Mass Set", "Clouds and atmosphere", "Clouds and atmosphere,Multi-tip,Dynamic"},
    {"cumulus-edge-set", "Cumulus Edge Set", "Clouds and atmosphere", "Clouds and atmosphere,Multi-tip,Dynamic"},
    {"towering-cloud-set", "Towering Cloud Set", "Clouds and atmosphere", "Clouds and atmosphere,Multi-tip,Dynamic"},
    {"thin-cloud-veil", "Thin Cloud Veil", "Clouds and atmosphere", "Clouds and atmosphere,Soft stamp,Dynamic"},
    {"wind-streak-cloud", "Wind Streak Cloud", "Clouds and atmosphere", "Clouds and atmosphere,Multi-tip,Dynamic"},
    {"storm-underside", "Storm Underside", "Clouds and atmosphere", "Clouds and atmosphere,Multi-tip,Dynamic"},
    {"smoke-puff-set", "Smoke Puff Set", "Clouds and atmosphere", "Clouds and atmosphere,Multi-tip,Dynamic"},
    {"rock-silhouette-set", "Rock Silhouette Set", "Ground rock and terrain", "Ground rock and terrain,Multi-tip,Dynamic"},
    {"boulder-set", "Boulder Set", "Ground rock and terrain", "Ground rock and terrain,Multi-tip,Dynamic"},
    {"gravel-scatter", "Gravel Scatter", "Ground rock and terrain", "Ground rock and terrain,Multi-tip,Dynamic"},
    {"scree-edge", "Scree Edge", "Ground rock and terrain", "Ground rock and terrain,Multi-tip,Dynamic"},
    {"mountain-ridge-set", "Mountain Ridge Set", "Ground rock and terrain", "Ground rock and terrain,Multi-tip,Dynamic"},
    {"pebble-edge", "Pebble Edge", "Ground rock and terrain", "Ground rock and terrain,Multi-tip,Dynamic"},
    {"ripple-set", "Ripple Set", "Water and reflections", "Water and reflections,Multi-tip,Dynamic"},
    {"rain-ring-set", "Rain Ring Set", "Water and reflections", "Water and reflections,Multi-tip,Dynamic"},
    {"wave-crest-set", "Wave Crest Set", "Water and reflections", "Water and reflections,Multi-tip,Dynamic"},
    {"splash-crown-set", "Splash Crown Set", "Water and reflections", "Water and reflections,Multi-tip,Dynamic"},
    {"droplet-set", "Droplet Set", "Water and reflections", "Water and reflections,Multi-tip,Dynamic"},
    {"star-scatter-set", "Star Scatter Set", "Manga effects and decoration", "Manga effects and decoration,Multi-tip,Dynamic"},
    {"petal-screen", "Petal Screen", "Manga effects and decoration", "Manga effects and decoration,Multi-tip,Dynamic"},
    {"snowflake-set", "Snowflake Set", "Weather and particles", "Weather and particles,Multi-tip,Dynamic"},
    {"falling-petals", "Falling Petals", "Weather and particles", "Weather and particles,Multi-tip,Dynamic"},
    {"debris-spray", "Debris Spray", "Weather and particles", "Weather and particles,Multi-tip,Dynamic"},
    {"distant-bird-flock", "Distant Bird Flock", "Dynamic subject systems", "Dynamic subject systems,Multi-tip,Dynamic"},
    {"soaring-bird-flock", "Soaring Bird Flock", "Dynamic subject systems", "Dynamic subject systems,Multi-tip,Dynamic"},
    {"perched-bird-row", "Perched Bird Row", "Dynamic subject systems", "Dynamic subject systems,Multi-tip,Dynamic"},
    {"butterfly-drift", "Butterfly Drift", "Dynamic subject systems", "Dynamic subject systems,Multi-tip,Dynamic"},
    {"fish-school", "Fish School", "Dynamic subject systems", "Dynamic subject systems,Multi-tip,Dynamic"},
    {"feather-drift", "Feather Drift", "Dynamic subject systems", "Dynamic subject systems,Multi-tip,Dynamic"},
    {"forest-builder", "Forest Builder", "Dynamic subject systems", "Dynamic subject systems,Repeat,Dynamic"},
    {"painted-cloud", "Painted Cloud", "Painted color stamps", "Painted color stamps,Colored stamp"},
    {"painted-tree", "Painted Tree", "Painted color stamps", "Painted color stamps,Colored stamp"},
    {"painted-building", "Painted Building", "Painted color stamps", "Painted color stamps,Colored stamp"},
};
qreal random(int variant,int channel){return brushRandom(30197,variant,channel);}
QPainterPath leaf(QPointF root,QPointF tip,qreal width){const QPointF d=tip-root;const qreal length=std::hypot(d.x(),d.y());const QPointF n(-d.y()/qMax(1.,length)*width,d.x()/qMax(1.,length)*width);QPainterPath p(root);p.cubicTo(root+d*.28+n,root+d*.7+n,tip);p.cubicTo(root+d*.72-n*.55,root+d*.2-n*.6,root);return p;}
void ellipse(QPainterPath &path,QPointF center,qreal x,qreal y){path.addEllipse(QRectF(center-QPointF(x,y),QSizeF(x*2,y*2)));}
void branch(QPainter &p,QPointF base,qreal angle,qreal length,int depth,int variant){
    const QPointF end=base+QPointF(std::cos(angle),-std::sin(angle))*length;
    p.setPen(QPen(Qt::white,qMax(1.4,depth*2.5),Qt::SolidLine,Qt::RoundCap));p.drawLine(base,end);
    if(depth>0){branch(p,end,angle+.3+random(variant,depth)*.35,length*.65,depth-1,variant+5);branch(p,end,angle-.36-random(variant+4,depth)*.3,length*.67,depth-1,variant+13);}
}
QImage blurred(QImage image,int radius){
    if(radius<=0)return image;QImage temp(image.size(),QImage::Format_Grayscale8);
    for(int y=0;y<image.height();++y){int sum=0;for(int k=-radius;k<=radius;++k)if(k>=0&&k<image.width())sum+=image.constScanLine(y)[k];auto *dst=temp.scanLine(y);for(int x=0;x<image.width();++x){dst[x]=sum/(radius*2+1);if(x-radius>=0)sum-=image.constScanLine(y)[x-radius];if(x+radius+1<image.width())sum+=image.constScanLine(y)[x+radius+1];}}
    for(int x=0;x<image.width();++x){int sum=0;for(int k=-radius;k<=radius;++k)if(k>=0&&k<image.height())sum+=temp.constScanLine(k)[x];for(int y=0;y<image.height();++y){image.scanLine(y)[x]=sum/(radius*2+1);if(y-radius>=0)sum-=temp.constScanLine(y-radius)[x];if(y+radius+1<image.height())sum+=temp.constScanLine(y+radius+1)[x];}}return image;
}
QImage artwork(const QString &id,const QString &family,int variant){
    QImage canvas(512,512,QImage::Format_ARGB32_Premultiplied);canvas.fill(Qt::black);QPainter p(&canvas);p.setRenderHint(QPainter::Antialiasing);p.scale(2,2);p.setPen(Qt::NoPen);p.setBrush(Qt::white);
    quint32 salt=2166136261U;for(const auto c:id.toUtf8()){salt^=quint8(c);salt*=16777619U;}
    const auto value=[salt](int v,int channel){return random(v+int(salt%100000),channel);};
    const QPointF root(128,248);
    if(family=="Grass and ground cover"){
        if(id.contains("litter")){
            for(int i=0;i<5;++i){p.save();p.translate(58+i*34,112+(value(variant,i)-.5)*42);p.rotate(-50+value(variant,i+20)*100);p.drawPath(leaf({-24,0},{24,0},10+value(variant,i+80)*10));p.restore();}
        }else if(id.contains("clover")){
            for(int i=0;i<7;++i){QPointF tip(55+i*23,110+value(variant,i)*80);p.setPen(QPen(Qt::white,2));p.drawLine(root,tip);p.setPen(Qt::NoPen);for(int a=0;a<3;++a){const qreal angle=a*2*pi/3;p.drawEllipse(tip+QPointF(std::cos(angle)*10,std::sin(angle)*10),11,9);}}
        }else if(id.contains("dandelion")||id.contains("flower")){
            const int flowers=id.contains("dandelion")?4:7;
            for(int i=0;i<flowers;++i){const QPointF base(92+value(variant,i+90)*72,248),tip(40+value(variant,i)*174,48+value(variant,i+20)*135);p.setPen(QPen(Qt::white,1.5));p.drawLine(base,tip);p.setPen(Qt::NoPen);
                if(id.contains("dandelion")){p.setPen(QPen(Qt::white,1));const qreal r=11+value(variant,i+30)*9;for(int k=0;k<16;++k){const qreal a=k*2*pi/16;const QPointF d(std::cos(a),std::sin(a));p.drawLine(tip+d*2,tip+d*r);p.drawLine(tip+d*r,tip+d*(r-3)+QPointF(-d.y(),d.x())*3);}p.setPen(Qt::NoPen);}
                else{for(int k=0;k<5;++k){const qreal a=k*2*pi/5;p.drawEllipse(tip+QPointF(std::cos(a)*5,std::sin(a)*5),3.5,5.5);}p.drawPath(leaf(base+(tip-base)*.35,base+(tip-base)*.35+QPointF(i%2?23:-23,-18),5));}
            }
        }else{
            const bool tall=id.contains("tall")||id.contains("reed"),flat=id.contains("trampled");const int count=id.contains("weed")?5:id.contains("fern")?7:tall?9:13;
            for(int i=0;i<count;++i){const qreal spread=(i-(count-1)/2.)*(flat?21:12);const qreal height=flat?25+value(variant,i)*60:tall?130+value(variant,i)*100:50+value(variant,i)*120;
                const QPointF base=QPointF(128+(value(variant,i+60)-.5)*30,248);
                const QPointF tip(128+spread+35*(value(variant,i+40)-.5)+(id.contains("wind")?48:0),248-height);
                const qreal width=id.contains("dry")?1.8:id.contains("reed")?2.6:3.5+value(variant,i+50)*4;
                if(id.contains("fern")){p.setPen(QPen(Qt::white,1.6));p.drawLine(base,tip);p.setPen(Qt::NoPen);for(int j=1;j<=8;++j){const auto at=base+(tip-base)*(j/10.);for(int side:{-1,1})p.drawPath(leaf(at,at+QPointF(side*(26-j*2),-13),3.2));}}
                else p.drawPath(leaf(base,tip,width));
                if(id.contains("reed"))p.drawRoundedRect(QRectF(tip-QPointF(3,16),QSizeF(6,23)),3,3);
            }
            if(id.contains("snow")){p.setBrush(Qt::black);QPainterPath snow({12,230});for(int i=0;i<16;++i)snow.lineTo(12+i*16,211+value(variant,i+95)*24);snow.lineTo(250,256);snow.lineTo(0,256);snow.closeSubpath();p.drawPath(snow);}
        }
    }else if(family=="Trees and foliage"||id=="forest-builder"||id=="painted-tree"){
        const bool whole=id.contains("crown")||id.contains("tree")||id.contains("forest");
        if(id=="distant-forest-strip"||id=="forest-builder"){
            p.setCompositionMode(QPainter::CompositionMode_Lighten);const int count=id=="distant-forest-strip"?5:3;
            for(int i=0;i<count;++i){const qreal h=125+value(variant,i)*105;const QString type=(i+variant)%3==0?"conifer-crown-set":(i+variant)%3==1?"broadleaf-crown-set":"bare-tree-set";const auto tree=artwork(type,"Trees and foliage",(i+variant)%6);p.drawImage(QRectF(10+i*210./count,248-h*248./256.,100,h),tree);}p.setCompositionMode(QPainter::CompositionMode_SourceOver);
        }else if(id=="conifer-branch"){
            p.setPen(QPen(Qt::white,3));p.drawLine(QPointF(30,218),QPointF(219,57));
            for(int i=0;i<15;++i){const QPointF at(36+i*12,211-i*10);const qreal length=20+value(variant,i)*25;p.drawLine(at,at+QPointF(-length,-length*.2));p.drawLine(at,at+QPointF(length*.2,length));}p.setPen(Qt::NoPen);
        }else if(id.contains("bare")){branch(p,root,pi/2,84,4,variant);}
        else if(id.contains("conifer")||id=="forest-builder"){
            const qreal h=165+value(variant,1)*60;QPainterPath tree(QPointF(128,248-h));for(int j=1;j<=9;++j){const qreal y=248-h+j*h/10.;const qreal half=(y-(248-h))*.36*(.8+value(variant,j)*.3);tree.lineTo(128-half,y);tree.lineTo(128-half*.55,y+7);}tree.lineTo(122,240);tree.lineTo(134,240);for(int j=9;j>=1;--j){const qreal y=248-h+j*h/10.;const qreal half=(y-(248-h))*.36*(.8+value(variant,j+20)*.3);tree.lineTo(128+half*.55,y+7);tree.lineTo(128+half,y);}tree.closeSubpath();p.drawPath(tree);p.drawRect(QRectF(123,210,10,38));
        }else if(id.contains("palm")){
            p.setPen(QPen(Qt::white,2));for(int j=0;j<6;++j){const QPointF base(128,201),tip(28+j*40,45+value(variant,j)*45);p.drawLine(base,tip);for(int k=1;k<10;++k){const auto at=base+(tip-base)*(k/11.);const qreal length=27*(1-k/12.);p.drawLine(at,at+QPointF(-length,-17));p.drawLine(at,at+QPointF(length,-17));}}p.setPen(Qt::NoPen);
        }else if(id.contains("bamboo")||id.contains("twig")){
            for(int j=0;j<9;++j){const qreal a=(-160+j*20)*pi/180;const QPointF tip=QPointF(128,190)+QPointF(std::cos(a),std::sin(a))*(72+value(variant,j)*55);p.drawPath(leaf({128,195},tip,id.contains("bamboo")?5:8));}p.setPen(QPen(Qt::white,4));p.drawLine(128,195,128,248);
        }else{
            if(whole){p.setPen(QPen(Qt::white,12,Qt::SolidLine,Qt::RoundCap));p.drawLine(root,{127,123});p.setPen(QPen(Qt::white,5));p.drawLine(QPointF(128,193),QPointF(88,139));p.drawLine(QPointF(128,181),QPointF(164,130));p.setPen(Qt::NoPen);}
            const QPointF center(128,whole?106:139);const int clusters=id.contains("blossom")?40:22;
            for(int i=0;i<clusters;++i){const qreal a=value(variant,i)*2*pi,r=std::sqrt(value(variant,i+30))*78;const QPointF at=center+QPointF(std::cos(a)*r,std::sin(a)*r*.76);const qreal size=12+value(variant,i+60)*22;for(int j=0;j<5;++j){const qreal da=j*2*pi/5;p.drawPath(leaf(at,at+QPointF(std::cos(da),std::sin(da))*size,5+value(variant,i+90)*6));}}
        }
    }else if(family=="Clouds and atmosphere"||id=="painted-cloud"){
        QPainterPath cloud;cloud.setFillRule(Qt::WindingFill);const bool tower=id.contains("tower");const bool streak=id.contains("streak")||id.contains("veil")||id.contains("edge");
        for(int i=0;i<20;++i){const qreal x=tower?128+(value(variant,i)-.5)*90:30+value(variant,i)*190;const qreal y=tower?38+value(variant,i+30)*170:128+(value(variant,i+30)-.5)*(streak?24:75);const qreal r=18+value(variant,i+60)*30;ellipse(cloud,{x,y},streak?r*1.45:r,streak?r*.3:r*.7);}p.drawPath(cloud);
    }else if(family=="Ground rock and terrain"||id=="painted-building"){
        if(id.contains("mountain")){
            QPainterPath ridge({5,201});for(int i=0;i<10;++i)ridge.lineTo(5+i*27,64+value(variant,i)*113);ridge.lineTo(251,222);ridge.lineTo(5,222);ridge.closeSubpath();p.drawPath(ridge);
        }else if(id=="painted-building"){
            const qreal w=80+value(variant,0)*70,h=115+value(variant,1)*105;const QRectF wall(128-w/2,248-h,w,h);p.drawRect(wall);QPainterPath roof;roof.moveTo(wall.topLeft());roof.lineTo(128,wall.top()-20-value(variant,2)*15);roof.lineTo(wall.topRight());roof.closeSubpath();p.drawPath(roof);p.setBrush(Qt::black);for(int row=0;row<5;++row)for(int col=0;col<3;++col)p.drawRect(QRectF(wall.left()+14+col*(w-28)/3,wall.top()+14+row*(h-25)/5,10,13));
        }else{const int count=id.contains("gravel")?10:id.contains("scree")?7:id.contains("pebble")?5:1;for(int j=0;j<count;++j){QPointF c=count==1?QPointF(128,174):QPointF(36+j*185./qMax(1,count-1),164+value(variant,j)*55);const qreal r=count==1?80:10+value(variant,j+30)*17;QPainterPath rock;for(int k=0;k<9;++k){const qreal a=k*2*pi/9,rr=r*(.7+value(variant+j,k)*.3);const QPointF point=c+QPointF(std::cos(a)*rr,std::sin(a)*rr*.7);if(!k)rock.moveTo(point);else rock.lineTo(point);}rock.closeSubpath();p.drawPath(rock);}}
    }else if(family=="Water and reflections"){
        if(id.contains("droplet")){p.drawPath(leaf({128,197},{120+value(variant,0)*20,35+variant*8.},24+variant*3));}
        else if(id.contains("splash")){
            QPainterPath crown({35,180});for(int j=0;j<6;++j){const qreal x=35+j*35,top=82+value(variant,j)*65;crown.quadTo(x+8,150,x+13,top);crown.quadTo(x+18,155,x+30,177);}crown.quadTo(130,221,35,190);crown.closeSubpath();p.drawPath(crown);p.setBrush(Qt::black);p.drawEllipse(QRectF(60,175,138,16));p.setBrush(Qt::white);for(int i=0;i<5;++i)p.drawEllipse(QPointF(44+value(variant,i+30)*170,55+value(variant,i+40)*65),2.5,4.);
        }else if(id.contains("wave")){
            const qreal crest=70+value(variant,1)*25;QPainterPath wave({22,202});wave.cubicTo(81,194,89,crest+5,142,crest);wave.cubicTo(181,crest-5,208,crest+18,212,crest+49);wave.cubicTo(192,crest+20,161,crest+22,158,crest+45);wave.cubicTo(152,crest+80,202,184,233,196);wave.quadTo(122,219,22,202);p.drawPath(wave);p.setBrush(Qt::black);p.drawEllipse(QRectF(142,crest+23,37,24));p.setBrush(Qt::white);for(int i=0;i<8;++i)p.drawEllipse(QPointF(113+value(variant,i+20)*106,crest+value(variant,i+30)*35),2,2);
        }else if(id.contains("rain-ring")){
            p.setBrush(Qt::NoBrush);p.setPen(QPen(Qt::white,1.8));const qreal radius=35+variant*12;p.drawEllipse(QRectF(128-radius,140-radius*.15,radius*2,radius*.3));if(variant>2)p.drawEllipse(QRectF(128-radius*.75,140-radius*.1125,radius*1.5,radius*.225));
        }else{
            p.setBrush(Qt::NoBrush);p.setPen(QPen(Qt::white,2.5));const qreal width=70+value(variant,0)*40;for(int j=0;j<3;++j)p.drawArc(QRectF(128-width+j*13,124+j*8,width*2-j*26,27-j*5),int(value(variant,j+10)*40*16),int((230+value(variant,j+15)*100)*16));
        }
    }else if(id.contains("bird")){
        if(id.contains("perched")){
            p.save();if(variant%2){p.translate(256,0);p.scale(-1,1);}p.translate(128,178);p.rotate((variant/2-1)*5);p.translate(-128,-178);p.drawEllipse(QRectF(90,95,74,65));p.drawEllipse(QRectF(139,69,36,36));QPainterPath beak;beak.moveTo(171,78);beak.lineTo(194,88);beak.lineTo(171,94);beak.closeSubpath();p.drawPath(beak);QPainterPath tail;tail.moveTo(103,135);tail.lineTo(57,177);tail.lineTo(88,164);tail.lineTo(119,153);tail.closeSubpath();p.drawPath(tail);p.setPen(QPen(Qt::white,3));p.drawLine(119,150,117,178);p.drawLine(139,152,139,178);p.drawLine(110,178,126,178);p.drawLine(132,178,148,178);p.restore();
        }else if(id.contains("soaring")){
            QPainterPath bird({127,132});bird.cubicTo(92,126,63,107+variant*2,21,109-variant*3);bird.lineTo(49,127);bird.quadTo(90,142,122,144);bird.lineTo(119,170);bird.lineTo(128,163);bird.lineTo(138,170);bird.lineTo(135,144);bird.quadTo(178,144,229,119+variant*3);bird.lineTo(241,100+variant*3);bird.cubicTo(190,110,163,125,132,132);bird.quadTo(130,101,127,132);bird.closeSubpath();p.drawPath(bird);
        }else{
            const qreal phase=variant%6;QPainterPath bird({128,145});bird.cubicTo(104,108-phase*7,75,100+phase*10,31,81+phase*10);bird.quadTo(81,149,121,151);bird.lineTo(127,171);bird.lineTo(135,151);bird.quadTo(181,144,227,80+phase*10);bird.cubicTo(184,100+phase*10,151,108-phase*7,128,145);p.drawPath(bird);p.drawEllipse(QPointF(128,140),6,12);
        }
    }
    else if(id.contains("butterfly")){QPainterPath wings;for(int side:{-1,1}){ellipse(wings,{qreal(128+side*(32+variant*3)),107.},35+variant*2,40+(variant%3)*4);ellipse(wings,{128+side*28.,161.},24+variant*1.5,27+(variant%2)*5);}p.drawPath(wings);p.drawEllipse(QPointF(128,137),5,40);}
    else if(id.contains("fish")){p.drawPath(leaf({49,128},{208+variant*3.,128},25+variant*3));QPainterPath tail;tail.moveTo(68,128);tail.lineTo(27,90);tail.lineTo(27,166);tail.closeSubpath();p.drawPath(tail);}
    else if(id.contains("feather")){p.drawPath(leaf({88,217},{167+variant*3.,35},35));p.setPen(QPen(Qt::black,2));p.drawLine(92,210,167+variant*3,40);}
    else if(id.contains("snowflake")){p.setPen(QPen(Qt::white,3));for(int k=0;k<6;++k){p.save();p.translate(128,128);p.rotate(k*60);p.drawLine(QPointF(0,0),QPointF(0,-74-variant*3.5));for(int j=1;j<4;++j){p.drawLine(0,-j*20,10+variant*2,-j*20-10-variant);p.drawLine(0,-j*20,-10-variant*2,-j*20-10-variant);}p.restore();}}
    else if(id.contains("star")){QPainterPath star;const int points=4+variant%3;for(int k=0;k<points*2;++k){const qreal a=k*pi/points,r=k%2?17+variant*2:86+(variant/3)*7;const QPointF point=QPointF(128,128)+QPointF(std::cos(a),std::sin(a))*r;if(!k)star.moveTo(point);else star.lineTo(point);}star.closeSubpath();p.drawPath(star);}
    else if(id.contains("debris")){for(int j=0;j<7;++j){const QPointF c(40+value(variant,j)*170,45+value(variant,j+30)*165);p.save();p.translate(c);p.rotate(value(variant,j+60)*180);p.drawRect(QRectF(-7,-4,8+value(variant,j+90)*13,7));p.restore();}}
    else {p.save();p.translate(128,128);p.rotate(-35+variant*13+(value(variant,99)-.5)*30);p.drawPath(leaf({-45,30},{54,-30},28+value(variant,101)*24));p.restore();}
    p.end();auto result=canvas.scaled(256,256,Qt::IgnoreAspectRatio,Qt::SmoothTransformation).convertToFormat(QImage::Format_Grayscale8);
    if(family=="Clouds and atmosphere"||id=="painted-cloud"){
        result=blurred(blurred(result,10),10);
        for(int y=0;y<result.height();++y)for(int x=0;x<result.width();++x){const qreal edge=qBound(0.,qMin(qMin(x,result.width()-1-x),qMin(y,result.height()-1-y))/16.,1.);result.scanLine(y)[x]=qRound(result.constScanLine(y)[x]*edge);}
    }
    return result;
}
bool grounded(const QString &id,const QString &family){return (family=="Grass and ground cover"&&!id.contains("litter"))||(family=="Trees and foliage"&&(id.contains("crown")||id.contains("tree")||id.contains("forest")))||id=="forest-builder"||id=="painted-tree"||id=="painted-building";}
QImage colored(const QImage &mask,const QString &id,int variant){
    QImage image(mask.size(),QImage::Format_ARGB32_Premultiplied);
    for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x){const qreal shade=.72+.28*(1-y/256.);QColor c=id.contains("cloud")?QColor::fromRgbF(.78+shade*.2,.81+shade*.17,.86+shade*.13):id.contains("building")?QColor::fromRgbF(.67*shade,.45*shade,.32*shade):QColor::fromHsvF(.24+variant*.008,.55,.28+shade*.35);const int a=mask.constScanLine(y)[x];image.setPixel(x,y,qPremultiply(qRgba(c.red(),c.green(),c.blue(),a)));}return image;
}
BrushPreset make(const Entry &entry){
    const QString id=QString::fromLatin1(entry.id),family=QString::fromLatin1(entry.family);BrushPreset preset;
    preset.id="builtin/catalog/"+id;preset.name=QString::fromUtf8(entry.name);preset.builtIn=true;preset.source="Original Toon Pro multi-tip artwork";
    auto &b=preset.brush;b.pose.overrideRotation=true;b.pose.rotation=0;b.pressureSize=false;b.spacing=.7;b.size=96;b.hardness=1;b.smoothingEnabled=false;b.sizeDynamics.jitter=.22;b.sizeDynamics.minimum=.65;b.angleDynamics.jitter=.025;b.flowDynamics.control=BrushControl::Pressure;b.flowDynamics.minimum=.35;
    const bool color=family=="Painted color stamps";QVector<QImage> tips;QVector<QPointF> anchors;
    for(int i=0;i<6;++i){auto image=artwork(id,family,i);if(color)image=colored(image,id,i);tips.append(image);anchors.append(grounded(id,family)?QPointF(.5,248./256.):QPointF(.5,.5));}
    setBrushTipImages(b,tips,anchors);b.tipOrder=BrushTipOrder::Shuffle;
    if(family=="Clouds and atmosphere"||id=="painted-cloud"){b.size=id.contains("tower")?640:480;b.spacing=.055;b.opacity=.075;b.sizeDynamics.jitter=.15;b.flowDynamics.minimum=.2;}
    if(family=="Trees and foliage"||id=="forest-builder"){b.size=grounded(id,family)?360:110;b.spacing=grounded(id,family)?.7:.45;b.angleDynamics.jitter=grounded(id,family)?0:.05;}
    if(family=="Ground rock and terrain"){b.size=id.contains("mountain")?640:id.contains("boulder")?320:90;b.spacing=.85;}
    if(family=="Grass and ground cover"){b.size=115;b.spacing=.38;b.angleDynamics.jitter=0;}
    if(family=="Dynamic subject systems"){b.size=id.contains("bird")?36:64;b.spacing=1.5;b.scatter=.45;b.scatterBothAxes=!id.contains("perched");if(id.contains("fish")||id.contains("feather"))b.angleDynamics.control=BrushControl::Direction;}
    if(id=="forest-builder"){b.size=420;b.spacing=.55;b.scatter=0;b.angleDynamics.jitter=0;}
    if(id.contains("perched")){b.scatter=0;for(auto &f:b.tipFrames)f.anchor={.5,178./256.};}
    if(family=="Weather and particles"||family=="Manga effects and decoration"){b.size=32;b.spacing=1.1;b.scatter=1.2;b.scatterBothAxes=true;b.angleDynamics.jitter=.4;}
    if(family=="Water and reflections"){b.size=id.contains("wave")?180:85;b.spacing=.9;b.angleDynamics.jitter=.01;}
    if(color){b.size=id.contains("building")?480:id.contains("tree")?420:480;b.angleDynamics.jitter=0;b.opacity=1;b.spacing=.9;}
    if(!color&&(family=="Trees and foliage"||family=="Grass and ground cover")){b.colorDynamics.enabled=true;b.colorDynamics.perTip=true;b.colorDynamics.hue=.015;b.colorDynamics.brightness=.08;}
    return preset;
}
}
QVector<BrushPreset> advancedCatalogBrushPresets(){static const auto presets=[] {QVector<BrushPreset> result;for(const auto &entry:entries)result.append(make(entry));return result;}();return presets;}
QString advancedCatalogBrushCategory(const QString &id){for(const auto &entry:entries)if(id=="builtin/catalog/"+QString::fromLatin1(entry.id))return QString::fromLatin1(entry.family);return {};}
QStringList advancedCatalogBrushTags(const QString &id){for(const auto &entry:entries)if(id=="builtin/catalog/"+QString::fromLatin1(entry.id)){auto tags=QString::fromLatin1(entry.tags).split(',');tags.append({"Standard","Default","Anime","Dynamic","Multiple tips","No repeat"});tags.removeDuplicates();return tags;}return {};}
void refineCatalogBrushQuality(BrushPreset &preset){
    const auto id=preset.id.section('/',-1);QString family;
    if(QStringList{"short-grass-tuft","wind-grass","clover-patch"}.contains(id))family="Grass and ground cover";
    else if(QStringList{"broadleaf-cluster","conifer-branch","distant-canopy","vine-leaves"}.contains(id))family="Trees and foliage";
    if(family.isEmpty())return;
    QVector<QImage> images;QVector<QPointF> anchors;for(int i=0;i<6;++i){images.append(artwork(id,family,i));anchors.append(family=="Grass and ground cover"?QPointF(.5,248./256.):QPointF(.5,.5));}
    setBrushTipImages(preset.brush,images,anchors);preset.brush.tipOrder=BrushTipOrder::Shuffle;
}
}
