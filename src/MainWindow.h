#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTimer>
#include <QThread>
#include <memory>

// Inclure les en-têtes C avec extern "C"
extern "C" {
    #include "audio.h"
    #include "config.h"
    #include "context.h"
    #include "dmx.h"
    #include "doublebuffer.h"
}

class AudioVisualizer;
class QLabel;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void updateVisualization();

private:
    void initializeAudio();
    void initializeUDP();
    void initializeDMX();
    void startThreads();
    void stopThreads();

    AudioVisualizer *m_visualizer;
    QLabel *m_statusLabel;
    QTimer m_updateTimer;
    
    // Structures de données C
    std::unique_ptr<Context> m_context;
    std::unique_ptr<DMXContext> m_dmxContext;
    std::unique_ptr<AudioData> m_audioData;
    std::unique_ptr<DoubleBuffer> m_doubleBuffer;
    
    // Threads
    QThread m_udpThread;
    QThread m_audioThread;
    QThread m_dmxThread;
    
    bool m_running;
};

#endif // MAINWINDOW_H
