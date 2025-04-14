#include <QApplication>
#include <QSurfaceFormat>
#include "MainWindow.h"

int main(int argc, char *argv[])
{
    // Configuration du format OpenGL pour Qt
    QSurfaceFormat format;
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8);
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(format);
    
    QApplication app(argc, argv);
    
    MainWindow mainWindow;
    mainWindow.resize(3456, 1160); // Tailles définies dans config.h
    mainWindow.show();
    
    return app.exec();
}
