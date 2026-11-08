#include "line_art.h"
#include "line_art_quality_fixture.h"
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>
#include <future>

using namespace toon;
namespace {
bool nearInk(const QImage &image,int x,int y,int radius=2) {
    for(int yy=qMax(0,y-radius);yy<=qMin(image.height()-1,y+radius);++yy)
        for(int xx=qMax(0,x-radius);xx<=qMin(image.width()-1,x+radius);++xx)if(qAlpha(image.pixel(xx,yy))>32)return true;
    return false;
}
int noisePixels(const QImage &image) {
    int count=0;for(int y=360;y<420;++y)if(std::abs(y-390)>4)for(int x=0;x<640;++x)count+=qAlpha(image.pixel(x,y))>32;return count;
}
int longestGap(const QImage &image) {
    int gap=0,longest=0;for(int x=90;x<570;++x){gap=nearInk(image,x,390)?0:gap+1;longest=qMax(longest,gap);}return longest;
}
QImage whiteInk(const QImage &image) {
    QImage white(image.size(),QImage::Format_ARGB32_Premultiplied);white.fill(Qt::white);QPainter p(&white);p.drawImage(0,0,image);return white;
}
LineArtResult run(const QImage &image,LineArtSettings settings={}) {return convertLineArt({image,image.size()},settings);}
}
class LineArtQualityTest:public QObject {
    Q_OBJECT
private slots:
    void backgroundCleanupRejectsGrainButKeepsWindows(){
        const auto source=lineArtArchitectureFixture(true);auto raw=LineArtSettings{};raw.textureSuppression=0;
        const auto unfiltered=run(source,raw),background=run(source,backgroundLineArtSettings());
        QVERIFY(background.error.isEmpty());QVERIFY(noisePixels(background.lines)<50);
        QVERIFY2(noisePixels(unfiltered.lines)>noisePixels(background.lines)+30,"Cleanup must remove grain, not just change the line color.");
        for(int row=0;row<3;++row)for(int column=0;column<3;++column){
            const int x=82+column*48,y=185+row*48;QVERIFY(nearInk(background.lines,x,y));QVERIFY(nearInk(background.lines,x+29,y+33));QVERIFY(nearInk(background.lines,x+14,y+16));
        }
    }
    void faintLongEdgesStayContinuousWithAndWithoutGrain(){
        for(bool noise:{false,true})for(const auto &settings:{LineArtSettings{},backgroundLineArtSettings()}){
            const auto result=run(lineArtArchitectureFixture(noise),settings);QVERIFY(result.error.isEmpty());
            QVERIFY2(longestGap(result.lines)<=4,qPrintable(QString("A faint structural line has a %1-pixel break").arg(longestGap(result.lines))));
        }
    }
    void gradientsStayQuietAndTransparentMarginsStayEmpty(){
        QImage gradient(160,120,QImage::Format_ARGB32_Premultiplied);
        for(int y=0;y<120;++y)for(int x=0;x<160;++x)gradient.setPixel(x,y,qRgb(x+40,x+40,x+40));
        const auto result=run(gradient);QVERIFY(result.error.isEmpty());
        int ink=0;for(int y=15;y<105;++y)for(int x=15;x<145;++x)ink+=qAlpha(result.lines.pixel(x,y))>32;QVERIFY(ink<5);
        gradient.fill(Qt::transparent);{QPainter p(&gradient);p.fillRect(35,25,80,60,Qt::black);}
        const auto transparent=run(gradient);for(int y=0;y<120;++y)for(int x=0;x<160;++x)if(!qAlpha(gradient.pixel(x,y)))QCOMPARE(qAlpha(transparent.lines.pixel(x,y)),0);
    }
    void antialiasingAndWidthPreserveSeparateParallelLines(){
        QImage image(160,140,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::white);
        {QPainter p(&image);p.setRenderHint(QPainter::Antialiasing);p.setPen(QPen(Qt::black,1));p.drawRect(15,15,125,105);p.drawLine(58,30,58,100);p.drawLine(64,30,64,100);}
        auto s=LineArtSettings{};s.width=.5;const auto thin=run(image,s);s.width=3;const auto broad=run(image,s);
        qint64 thinAlpha=0,broadAlpha=0;int fractional=0;
        for(int y=0;y<140;++y)for(int x=0;x<160;++x){const int alpha=qAlpha(thin.lines.pixel(x,y));thinAlpha+=alpha;broadAlpha+=qAlpha(broad.lines.pixel(x,y));fractional+=alpha>0&&alpha<255;}
        QVERIFY(fractional>100);QVERIFY(broadAlpha>thinAlpha*3);QVERIFY(broadAlpha<thinAlpha*9);
        QVERIFY(nearInk(thin.lines,15,15));int filledGap=0;for(int y=35;y<95;++y)filledGap+=qAlpha(thin.lines.pixel(61,y))>32;QVERIFY(filledGap<6);
    }
    void strongSmallDetailsSurviveTextureCleanup(){
        QImage image(80,80,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::white);{QPainter p(&image);p.setRenderHint(QPainter::Antialiasing);p.setBrush(Qt::black);p.setPen(Qt::NoPen);p.drawEllipse(QRectF(30,30,4,4));}
        auto settings=backgroundLineArtSettings();settings.fragments=0;const auto result=run(image,settings);QVERIFY(nearInk(result.lines,32,32,4));
    }
    void backgroundPresetPreservesColorAndShadingChoices(){
        LineArtSettings s;s.ink=Qt::red;s.shade=Qt::green;s.deepShade=Qt::blue;s.base=true;s.shading=LineArtShading::Grayscale;s.toneSpacing=15;
        const auto preset=backgroundLineArtSettings(s);QCOMPARE(preset.ink,s.ink);QCOMPARE(preset.shade,s.shade);QCOMPARE(preset.deepShade,s.deepShade);QCOMPARE(preset.base,s.base);QCOMPARE(preset.shading,s.shading);QCOMPARE(preset.toneSpacing,s.toneSpacing);
        s.textureSuppression=101;QVERIFY(!validateLineArtSettings(s).isEmpty());
    }
    void filteringCanBeCancelledAndNeverEditsTheInput(){
        const auto image=lineArtArchitectureFixture(true).scaled(2400,1575),copy=image.copy();auto cancel=std::make_shared<std::atomic_bool>(false);
        auto future=std::async(std::launch::async,[&]{return convertLineArt({image,image.size()},backgroundLineArtSettings(),cancel);});
        QTest::qWait(20);cancel->store(true);QVERIFY(future.wait_for(std::chrono::seconds(3))==std::future_status::ready);QVERIFY(future.get().cancelled);QCOMPARE(image,copy);
    }
    void captureQualityEvidence(){
        const auto directory=qEnvironmentVariable("TOON_LINE_ART_QUALITY_DIR");if(directory.isEmpty())return;QDir().mkpath(directory);
        const auto fixture=lineArtArchitectureFixture(true);const auto ordinary=run(fixture),background=run(fixture,backgroundLineArtSettings());
        QJsonObject metrics{{"fixtureNoisePixelsDefault",noisePixels(ordinary.lines)},{"fixtureNoisePixelsBackground",noisePixels(background.lines)},{"faintLineLongestGapDefault",longestGap(ordinary.lines)},{"faintLineLongestGapBackground",longestGap(background.lines)}};
        QVERIFY(whiteInk(background.lines).save(directory+"/architecture-cleanup.png"));
        const QImage photo(qEnvironmentVariable("TOON_LINE_ART_QUALITY_PHOTO"));
        if(!photo.isNull()){
            const auto input=photo.convertToFormat(QImage::Format_ARGB32_Premultiplied);LineArtSettings settings;settings.smoothing=1.5;
            QElapsedTimer timer;timer.start();const auto result=run(input,settings);metrics["photoMilliseconds"]=int(timer.elapsed());QVERIFY2(result.error.isEmpty(),qPrintable(result.error));
            const auto after=whiteInk(result.lines);QVERIFY(after.save(directory+"/building-clean.png"));
            const auto bg=run(input,backgroundLineArtSettings(settings));QVERIFY(whiteInk(bg.lines).save(directory+"/building-background.png"));
            const QImage before(qEnvironmentVariable("TOON_LINE_ART_QUALITY_BEFORE"));
            if(before.size()==after.size()){
                QImage compare(after.width()*2,after.height()+42,QImage::Format_ARGB32_Premultiplied);compare.fill(Qt::white);QPainter painter(&compare);painter.setPen(Qt::black);QFont font=painter.font();font.setPointSize(14);painter.setFont(font);
                painter.drawText(QRect(12,0,after.width()-24,40),Qt::AlignVCenter,"Previous extraction");painter.drawText(QRect(after.width()+12,0,after.width()-24,40),Qt::AlignVCenter,"Updated extraction");painter.drawImage(0,42,before);painter.drawImage(after.width(),42,after);painter.end();QVERIFY(compare.save(directory+"/before-after.png"));
            }
        }
        QFile file(directory+"/metrics.json");QVERIFY(file.open(QIODevice::WriteOnly));file.write(QJsonDocument(metrics).toJson());
    }
};
QTEST_MAIN(LineArtQualityTest)
#include "test_line_art_quality.moc"
