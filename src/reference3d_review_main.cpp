#include "reference3d_review.h"
#include "drawing_input.h"
#include "feature_flags.h"
#include "ui_theme.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QMessageBox>

int main(int argc, char **argv) {
    static_assert(toon::features::experimental3D, "The reference review app requires the explicit experimental flag.");
    QApplication app(argc, argv);
    QApplication::setOrganizationName("ToonPro");
    QApplication::setApplicationName("Toon Pro 3D Review");
    QApplication::setApplicationVersion(TOON_APP_VERSION);
    QCommandLineParser parser;
    parser.setApplicationDescription("Independent experimental reference-3D review workspace.");
    parser.addHelpOption(); parser.addVersionOption();
    parser.addPositionalArgument("scene", "Optional self-contained .toon3d scene to open for review.", "[scene]");
    parser.process(app);
    if(parser.positionalArguments().size()>1)parser.showHelp(1);
    toon::configureDrawingInput();
    toon::applyTheme();
    toon::Reference3DReviewWindow window;
    if(!parser.positionalArguments().isEmpty()){
        QString error;
        if(!window.openScene(parser.positionalArguments().first(),&error))
            QMessageBox::warning(&window,"Open 3D reference scene",error);
    }
    window.show();
    return app.exec();
}
