#include "main_window.h"
#include "project_workspace_window.h"
#include "drawing_input.h"
#include "package_smoke.h"
#include <QApplication>
#include <QCommandLineParser>

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    toon::configureDrawingInput();
    QApplication::setOrganizationName("ToonPro");
    QApplication::setApplicationName("Toon Pro");
    QApplication::setApplicationVersion(TOON_APP_VERSION);
    QCommandLineParser parser;
    parser.setApplicationDescription("A desktop workspace for hand-drawn anime production.");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"package-smoke", "Verify an isolated packaged runtime in a new output directory.", "directory"});
    parser.addOption({"require-3d", "Require 3D rendering in the packaged runtime verification."});
    parser.addPositionalArgument("project", "Optional new-format .toonstudio project folder to open.", "[project]");
    parser.process(app);
    toon::applyTheme();
    if (parser.isSet("package-smoke")) return toon::runPackageSmoke(parser.value("package-smoke"), parser.isSet("require-3d"));
    toon::ProjectWorkspaceWindow window;
    window.show();
    const auto files = parser.positionalArguments();
    if (!files.isEmpty()) window.openManagedProject(files.first());
    return app.exec();
}
