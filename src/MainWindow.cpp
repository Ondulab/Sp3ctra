#include "MainWindow.h"
#include "AudioVisualizer.h"
#include <QLabel>
#include <QVBoxLayout>
#include <QStatusBar>
#include <QMessageBox>

// Inclure les en-têtes C supplémentaires
extern "C" {
    #include "multithreading.h"
    #include "synth.h"
    #include "udp.h"
}

// Wrappers pour les fonctions de thread C
extern "C" void* udpThread(void* arg);
extern "C" void* audioProcessingThread(void* arg);
extern "C" void* dmxSendingThread(void* arg);

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), m_running(false)
{
    setWindowTitle("CISYNTH");
    
    // Créer le widget central
    QWidget *centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);
    
    // Créer le layout
    QVBoxLayout *layout = new QVBoxLayout(centralWidget);
    
    // Créer le visualiseur audio
    m_visualizer = new AudioVisualizer(this);
    layout->addWidget(m_visualizer);
    
    // Créer la barre d'état
    m_statusLabel = new QLabel("Prêt");
    statusBar()->addWidget(m_statusLabel);
    
    // Initialiser les structures de données
    m_context = std::make_unique<Context>();
    m_dmxContext = std::make_unique<DMXContext>();
    m_audioData = std::make_unique<AudioData>();
    m_doubleBuffer = std::make_unique<DoubleBuffer>();
    
    // Initialiser la fenêtre SFML
    sfVideoMode mode = {CIS_MAX_PIXELS_NB, WINDOWS_HEIGHT, 32};
    sfRenderWindow* window = sfRenderWindow_create(mode, "CISYNTH", sfResize | sfClose, NULL);
    if (!window) {
        QMessageBox::critical(this, "Erreur", "Impossible de créer la fenêtre SFML");
        return;
    }
    m_context->window = window;
    
    // Initialiser les composants
    initializeAudio();
    initializeUDP();
    initializeDMX();
    
    // Démarrer les threads
    startThreads();
    
    // Configurer le timer de mise à jour
    connect(&m_updateTimer, &QTimer::timeout, this, &MainWindow::updateVisualization);
    m_updateTimer.start(16); // ~60 FPS
}

MainWindow::~MainWindow()
{
    // Arrêter les threads
    stopThreads();
    
    // Nettoyer les ressources
    audio_Cleanup();
    
    // Libérer les ressources UDP
    if (m_context->si_other) {
        delete m_context->si_other;
        m_context->si_other = nullptr;
    }
    
    if (m_context->si_me) {
        delete m_context->si_me;
        m_context->si_me = nullptr;
    }
    
    // Libérer la fenêtre SFML
    if (m_context->window) {
        sfRenderWindow_destroy(m_context->window);
        m_context->window = nullptr;
    }
}

void MainWindow::initializeAudio()
{
    // Initialiser les données audio
    initAudioData(m_audioData.get(), AUDIO_CHANNEL, AUDIO_BUFFER_SIZE);
    audio_Init(m_audioData.get());
    synth_IfftInit();
    
    // Démarrer l'unité audio
    OSStatus status = startAudioUnit();
    if (status != 0) {
        QMessageBox::critical(this, "Erreur", "Impossible d'initialiser l'audio");
    }
}

void MainWindow::initializeUDP()
{
    // Initialiser UDP
    struct sockaddr_in *si_other = new struct sockaddr_in();
    struct sockaddr_in *si_me = new struct sockaddr_in();
    int s = udp_Init(si_other, si_me);
    
    if (s < 0) {
        QMessageBox::critical(this, "Erreur", "Impossible d'initialiser UDP");
        delete si_other;
        delete si_me;
        return;
    }
    
    // Configurer le contexte
    m_context->socket = s;
    m_context->si_other = si_other;
    m_context->si_me = si_me;
}

void MainWindow::initializeDMX()
{
#ifdef USE_DMX
    // Initialiser DMX
    int dmxFd = init_Dmx();
    if (dmxFd < 0) {
        QMessageBox::warning(this, "Avertissement", "Impossible d'initialiser DMX");
    }
    
    // Configurer le contexte DMX
    m_dmxContext->fd = dmxFd;
    m_dmxContext->running = 1;
    m_dmxContext->colorUpdated = 0;
    pthread_mutex_init(&m_dmxContext->mutex, NULL);
    pthread_cond_init(&m_dmxContext->cond, NULL);
#endif
}

void MainWindow::startThreads()
{
    // Initialiser le double buffer
    initDoubleBuffer(m_doubleBuffer.get());
    
    // Configurer le contexte global
    m_context->audioData = m_audioData.get();
    m_context->doubleBuffer = m_doubleBuffer.get();
    m_context->dmxCtx = m_dmxContext.get();
    m_context->running = 1;
    
    // Démarrer les threads
    m_running = true;
    
#ifdef USE_DMX
    // Démarrer le thread DMX
    pthread_t dmxThreadId;
    if (pthread_create(&dmxThreadId, NULL, dmxSendingThread, (void *)m_dmxContext.get()) != 0) {
        QMessageBox::warning(this, "Avertissement", "Impossible de démarrer le thread DMX");
    }
#endif
    
    // Démarrer le thread UDP
    pthread_t udpThreadId;
    if (pthread_create(&udpThreadId, NULL, udpThread, (void *)m_context.get()) != 0) {
        QMessageBox::critical(this, "Erreur", "Impossible de démarrer le thread UDP");
    }
    
    // Démarrer le thread audio
    pthread_t audioThreadId;
    if (pthread_create(&audioThreadId, NULL, audioProcessingThread, (void *)m_context.get()) != 0) {
        QMessageBox::critical(this, "Erreur", "Impossible de démarrer le thread audio");
    }
    
    // Configurer la priorité du thread audio
    struct sched_param param;
    param.sched_priority = 80;
    pthread_setschedparam(audioThreadId, SCHED_RR, &param);
}

void MainWindow::stopThreads()
{
    if (m_running) {
        m_context->running = 0;
        m_dmxContext->running = 0;
        m_running = false;
    }
}

void MainWindow::updateVisualization()
{
    // Vérifier si le double buffer contient de nouvelles données
    pthread_mutex_lock(&m_doubleBuffer->mutex);
    int dataReady = m_doubleBuffer->dataReady;
    if (dataReady) {
        m_doubleBuffer->dataReady = 0;
    }
    pthread_mutex_unlock(&m_doubleBuffer->mutex);
    
    if (dataReady) {
        // Mettre à jour le visualiseur avec les nouvelles données
        m_visualizer->updateData(
            m_doubleBuffer->processingBuffer_R,
            m_doubleBuffer->processingBuffer_G,
            m_doubleBuffer->processingBuffer_B,
            CIS_MAX_PIXELS_NB
        );
        
        // Calculer la couleur moyenne et mettre à jour le contexte DMX
        DMXSpot zoneSpots[DMX_NUM_SPOTS];
        computeAverageColorPerZone(
            m_doubleBuffer->processingBuffer_R,
            m_doubleBuffer->processingBuffer_G,
            m_doubleBuffer->processingBuffer_B,
            CIS_MAX_PIXELS_NB,
            zoneSpots
        );
        
        pthread_mutex_lock(&m_dmxContext->mutex);
        memcpy(m_dmxContext->spots, zoneSpots, sizeof(zoneSpots));
        m_dmxContext->colorUpdated = 1;
        pthread_cond_signal(&m_dmxContext->cond);
        pthread_mutex_unlock(&m_dmxContext->mutex);
    }
}
