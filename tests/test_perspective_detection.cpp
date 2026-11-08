#include "perspective_detection.h"
#include "guide_geometry.h"
#include <QElapsedTimer>
#include <QPainter>
#include <QRandomGenerator>
#include <QtTest>
#include <algorithm>
#include <cmath>
#include <limits>

using namespace toon;
namespace {
QImage canvas(QSize size={640,480}){QImage image(size,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::white);return image;}
void fan(QPainter &p,QPointF vp,const QVector<QPointF> &ends,double startFraction=.22){for(const auto end:ends)p.drawLine(vp+(end-vp)*startFraction,end);}
QImage onePointImage(QPointF vp={322,192}) {
    auto image=canvas();QPainter p(&image);p.setRenderHint(QPainter::Antialiasing);p.setPen(QPen(QColor(30,38,46),2));
    fan(p,vp,{{24,25},{220,15},{460,20},{615,90},{620,330},{540,460},{300,460},{90,460},{20,280}});
    p.drawRect(QRectF(170,100,285,205));return image;
}
QImage twoPointImage(QPointF first={-350,140},QPointF second={950,190}) {
    auto image=canvas();QPainter p(&image);p.setRenderHint(QPainter::Antialiasing);p.setPen(QPen(QColor(32,42,52),2));
    fan(p,first,{{130,450},{220,380},{310,440},{400,330},{510,400},{620,290}},0);
    fan(p,second,{{15,270},{65,365},{150,440},{250,350},{360,450},{440,390}},0);return image;
}
const PerspectiveCandidate *closest(const PerspectiveDetectionResult &r,QPointF point){const PerspectiveCandidate *best=nullptr;double error=1e30;for(const auto &c:r.candidates)if(!c.parallel){const auto d=QLineF(c.point,point).length();if(d<error){error=d;best=&c;}}return best;}
void addFan(QVector<PerspectiveSegment> &segments,QPointF vp,const QVector<QPointF> &ends){for(auto end:ends)segments.append({{vp+(end-vp)*.25,end},1,true});}
QString diagnostic(const PerspectiveDetectionResult &r){QString s=QString("%1; %2 segments; %3 candidates").arg(r.error.isEmpty()?r.message:r.error).arg(r.segments.size()).arg(r.candidates.size());for(const auto &c:r.candidates)s+=QString(" [%1: %2,%3; support %4; error %5]").arg(c.parallel?"parallel":"finite").arg(c.parallel?c.direction.x():c.point.x()).arg(c.parallel?c.direction.y():c.point.y()).arg(c.supportingSegments.size()).arg(c.meanAngularError);return s;}
void metric(const char *fixture,const PerspectiveDetectionResult &r,const QVector<QPointF> &expected,qint64 elapsed){
    if(!qEnvironmentVariableIsSet("TOON_PERSPECTIVE_METRICS"))return;
    QStringList errors;for(auto point:expected){const auto *c=closest(r,point);errors.append(c?QString::number(QLineF(c->point,point).length(),'f',3):"missing");}
    qInfo().noquote()<<QString("%1: errors_px=[%2], elapsed_ms=%3; %4").arg(fixture,errors.join(",")).arg(elapsed).arg(diagnostic(r));
    if(qEnvironmentVariableIsSet("TOON_PERSPECTIVE_EDGE_DETAILS"))for(const auto &c:r.candidates){qInfo()<<"Candidate"<<c.point<<c.parallel;for(int i:c.supportingSegments)qInfo()<<i<<r.segments[i].line;}
}
}
class PerspectiveDetectionTest:public QObject {
    Q_OBJECT
private slots:
    void architecturalOnePointFindsKnownConvergenceAndPreservesSource(){
        const QPointF expected(322,192);const auto image=onePointImage(expected),before=image;QElapsedTimer timer;timer.start();const auto result=detectPerspective(image);metric("One-point 640x480",result,{expected},timer.elapsed());QVERIFY2(result.error.isEmpty(),qPrintable(result.error));const auto *found=closest(result,expected);QVERIFY2(found,qPrintable(diagnostic(result)));QVERIFY2(QLineF(found->point,expected).length()<8,qPrintable(diagnostic(result)));QVERIFY(found->supportingSegments.size()>=5);QCOMPARE(std::count_if(result.candidates.cbegin(),result.candidates.cend(),[](const auto &c){return !c.parallel;}),1);QCOMPARE(image,before);
    }
    void architecturalTwoPointRecoversOffCanvasPoints(){
        const QPointF first(-350,140),second(950,190);PerspectiveDetectionSettings settings;settings.mode=PerspectiveMode::TwoPoint;QElapsedTimer timer;timer.start();const auto result=detectPerspective(twoPointImage(first,second),settings);metric("Two-point off-canvas",result,{first,second},timer.elapsed());const auto *a=closest(result,first),*b=closest(result,second);QVERIFY2(a&&b&&a!=b,qPrintable(diagnostic(result)));QVERIFY2(QLineF(a->point,first).length()<20,qPrintable(diagnostic(result)));QVERIFY2(QLineF(b->point,second).length()<20,qPrintable(diagnostic(result)));
    }
    void croppedRegionKeepsOriginalCoordinates(){
        auto image=canvas({1400,1000});const auto local=onePointImage();{QPainter p(&image);p.drawImage(350,240,local);}PerspectiveDetectionSettings settings;settings.region={350,240,640,480};const auto result=detectPerspective(image,settings);const QPointF expected(672,432);const auto *found=closest(result,expected);QVERIFY2(found,qPrintable(diagnostic(result)));QVERIFY2(QLineF(found->point,expected).length()<8,qPrintable(diagnostic(result)));QCOMPARE(result.sourceSize,image.size());
    }
    void largeImageReductionRetainsGeometry(){
        const auto image=onePointImage().scaled(2560,1920,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);const QPointF expected(1288,768);QElapsedTimer timer;timer.start();const auto result=detectPerspective(image);metric("One-point 2560x1920",result,{expected},timer.elapsed());const auto *found=closest(result,expected);QVERIFY2(found,qPrintable(diagnostic(result)));QVERIFY2(QLineF(found->point,expected).length()<32,qPrintable(diagnostic(result)));
    }
    void rotatedHorizonConsensusAndOutliers(){
        QVector<PerspectiveSegment> segments;const QPointF first(-400,100),second(1100,100);addFan(segments,first,{{50,450},{150,320},{280,450},{400,300},{510,420},{610,250}});addFan(segments,second,{{10,260},{70,390},{180,450},{280,350},{400,450},{500,380}});
        QTransform rotation;rotation.translate(320,240);rotation.rotate(19);rotation.translate(-320,-240);for(auto &s:segments)s.line=rotation.map(s.line);
        segments.append({{{100,50},{130,180}},.2,true});segments.append({{{600,30},{470,210}},.2,true});
        const auto result=fitPerspectiveSegments(segments,{640,480});const auto *a=closest(result,rotation.map(first)),*b=closest(result,rotation.map(second));QVERIFY2(a&&b&&a!=b,qPrintable(diagnostic(result)));QVERIFY(QLineF(a->point,rotation.map(first)).length()<.01);QVERIFY(QLineF(b->point,rotation.map(second)).length()<.01);QVERIFY(a->meanAngularError<.001);QVERIFY(b->meanAngularError<.001);
    }
    void parallelDirectionsStayParallel(){
        QVector<PerspectiveSegment> segments;for(int y:{50,130,230,350,430})segments.append({{{30,qreal(y)},{610,qreal(y)}},1,true});
        const auto result=fitPerspectiveSegments(segments,{640,480});QVERIFY2(!result.candidates.isEmpty(),qPrintable(diagnostic(result)));QVERIFY(result.candidates[0].parallel);QVERIFY(std::abs(result.candidates[0].direction.y())<1e-6);QVERIFY(std::none_of(result.candidates.cbegin(),result.candidates.cend(),[](const auto &c){return !c.parallel;}));
        const auto guides=createPerspectiveGuides(result.candidates,{0},{320,240},{0,0,640,480});QCOMPARE(guides.size(),1);QCOMPARE(guides[0].type,GuideType::ParallelRuler);QVERIFY(validGuideGeometry(guides[0]));
    }
    void duplicatedEdgeFragmentsCannotInventConsensus(){
        QVector<PerspectiveSegment> segments;for(int i=0;i<12;++i)segments.append({{{40.,40.+i*.05},{600.,400.+i*.05}},1,true});const auto result=fitPerspectiveSegments(segments,{640,480});QVERIFY(result.candidates.isEmpty());
        segments.clear();for(int i=0;i<6;++i)segments.append({{{10.+i*100,140.},{90.+i*100,140.}},1,true});QVERIFY(fitPerspectiveSegments(segments,{640,480}).candidates.isEmpty());
    }
    void unsupportedImagesDoNotInventGrids_data(){QTest::addColumn<int>("kind");QTest::newRow("blank")<<0;QTest::newRow("pixel noise")<<1;QTest::newRow("organic ellipses")<<2;}
    void unsupportedImagesDoNotInventGrids(){
        QFETCH(int,kind);auto image=canvas();
        if(kind==1){QRandomGenerator random(4242);for(int y=0;y<image.height();++y){auto *row=reinterpret_cast<QRgb*>(image.scanLine(y));for(int x=0;x<image.width();++x){const int v=random.bounded(256);row[x]=qRgb(v,v,v);}}}
        else if(kind==2){QPainter p(&image);p.setRenderHint(QPainter::Antialiasing);p.setPen(QPen(Qt::black,2));for(const auto &rect:{QRectF(60,40,160,220),QRectF(320,120,190,140),QRectF(130,320,200,95)})p.drawEllipse(rect);}
        const auto result=detectPerspective(image);QVERIFY2(result.candidates.isEmpty(),qPrintable(diagnostic(result)));QVERIFY(result.error.isEmpty());
    }
    void disabledLinesInvalidateTheRelevantFamily(){
        QVector<PerspectiveSegment> segments;addFan(segments,{320,190},{{20,20},{300,20},{620,20},{620,400},{320,460},{20,400}});auto result=fitPerspectiveSegments(segments,{640,480});QVERIFY(!result.candidates.isEmpty());for(int i=0;i<4;++i)segments[i].enabled=false;result=fitPerspectiveSegments(segments,{640,480});QVERIFY(result.candidates.isEmpty());
    }
    void cancellationAndInvalidInputDoNotPublishPartialResults(){
        const auto image=onePointImage();int checks=0;const auto result=detectPerspective(image,{},[&]{return ++checks==5;});QVERIFY(result.cancelled);QVERIFY(result.segments.isEmpty());QVERIFY(result.candidates.isEmpty());QVERIFY(checks>=5);
        PerspectiveDetectionSettings settings;settings.sensitivity=std::numeric_limits<double>::quiet_NaN();QVERIFY(!detectPerspective(image,settings).error.isEmpty());QVERIFY(!detectPerspective({}).error.isEmpty());
        QVector<PerspectiveSegment> malformed{{{{0,0},{std::numeric_limits<double>::infinity(),1}},1,true}};QVERIFY(!fitPerspectiveSegments(malformed,{640,480}).error.isEmpty());
    }
    void manualLineFallbackSupportsFiniteAndParallelWithoutFabrication(){
        const auto finite=perspectiveFromLines({{40,40},{200,200}},{{300,40},{140,200}});QVERIFY(finite);QVERIFY(!finite->parallel);QVERIFY(QLineF(finite->point,{170,170}).length()<1e-7);
        const auto parallel=perspectiveFromLines({{10,20},{300,20}},{{30,80},{400,80}});QVERIFY(parallel);QVERIFY(parallel->parallel);QVERIFY(!perspectiveFromLines({{10,20},{10,20}},{{30,80},{400,80}}));QVERIFY(!perspectiveFromLines({{10,20},{300,20}},{{30,20},{400,20}}));
    }
    void guideFactoryDoesNotInventHorizonOrVertical(){
        PerspectiveCandidate a,b;a.point={-300,180};b.point={900,220};const QVector<PerspectiveCandidate> candidates{a,b};
        const auto one=createPerspectiveGuides(candidates,{0},{320,240},{0,0,640,480},PerspectiveMode::OnePoint);QCOMPARE(one.size(),1);QCOMPARE(one[0].type,GuideType::VanishingPoint);QVERIFY(validGuideGeometry(one[0]));QVERIFY(createPerspectiveGuides(candidates,{0},{320,240},{0,0,640,480},PerspectiveMode::TwoPoint).isEmpty());
        const auto two=createPerspectiveGuides(candidates,{0,1},{300,200},{0,0,640,480},PerspectiveMode::TwoPoint);QCOMPARE(two.size(),1);QCOMPARE(two[0].type,GuideType::TwoPoint);QVERIFY(!two[0].vertical);QVERIFY(validGuideGeometry(two[0]));
        QVERIFY(createPerspectiveGuides(candidates,{0,1},{320,240},{0,0,640,480},PerspectiveMode::OnePoint).isEmpty());
        auto invalid=candidates;invalid[1].point.setX(std::numeric_limits<double>::infinity());QVERIFY(createPerspectiveGuides(invalid,{0,1},{320,240},{0,0,640,480}).isEmpty());
    }
};
QTEST_APPLESS_MAIN(PerspectiveDetectionTest)
#include "test_perspective_detection.moc"
