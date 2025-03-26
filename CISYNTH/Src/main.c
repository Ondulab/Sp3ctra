#include "config.h"
#include <SFML/Graphics.h>
#include <SFML/Network.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <errno.h>
#include <fcntl.h>
#include <termios.h>
#include "dmx.h"
#include "error.h"
#include "synth.h"
#include "display.h"
#include "udp.h"
#include "audio.h"
#include "multithreading.h"
#include "context.h"
#include "spectral_generator.h"

int main(void)
{
    //spectral_generator();
    //printf("end of program");
    //return EXIT_FAILURE;            // pour print only
    
    int dmxFd = init_Dmx();
#ifdef USE_DMX
    if (dmxFd < 0)
    {
        perror("Error initializing DMX");
        //return EXIT_FAILURE;
    }
#endif
    
    DMXContext *dmxCtx = malloc(sizeof(DMXContext));
    if (dmxCtx == NULL)
    {
        perror("Error allocating DMXContext");
        close(dmxFd);
        //return EXIT_FAILURE;
    }
    dmxCtx->fd = dmxFd;
    dmxCtx->running = 1;
    dmxCtx->colorUpdated = 0;
    pthread_mutex_init(&dmxCtx->mutex, NULL);
    pthread_cond_init(&dmxCtx->cond, NULL);

    /* Initialize CSFML */
    sfVideoMode mode = { WINDOWS_WIDTH, WINDOWS_HEIGHT, 32 };
    sfRenderWindow *window = sfRenderWindow_create(mode, "CSFML Viewer", sfResize | sfClose, NULL);
    if (!window)
    {
        perror("Error creating CSFML window");
        close(dmxCtx->fd);
        free(dmxCtx);
        return EXIT_FAILURE;
    }

    /* Initialize UDP and Audio */
    struct sockaddr_in si_other, si_me;
    
    AudioData audioData;
    initAudioData(&audioData, AUDIO_CHANNEL, AUDIO_BUFFER_SIZE);
    audio_Init(&audioData);
    synth_IfftInit();
    display_Init(window);

    int s = udp_Init(&si_other, &si_me);
    if (s < 0)
    {
        perror("Error initializing UDP");
        sfRenderWindow_destroy(window);
        close(dmxCtx->fd);
        free(dmxCtx);
        return EXIT_FAILURE;
    }

    OSStatus status = startAudioUnit();  // Handle status as needed

    /* Create double buffer */
    DoubleBuffer db;
    initDoubleBuffer(&db);

    /* Build global context structure */
    Context context = { 0 };
    context.window = window;
    context.socket = s;
    context.si_other = &si_other;
    context.si_me = &si_me;
    context.audioData = &audioData;
    context.doubleBuffer = &db;
    context.dmxCtx = dmxCtx;
    context.running = 1; // Flag de terminaison pour le contexte

    /* Create textures and sprites for rendering in main thread */
    sfTexture *backgroundTexture = sfTexture_create(WINDOWS_WIDTH, WINDOWS_HEIGHT);
    sfTexture *foregroundTexture = sfTexture_create(WINDOWS_WIDTH, WINDOWS_HEIGHT);
    sfSprite *backgroundSprite = sfSprite_create();
    sfSprite *foregroundSprite = sfSprite_create();
    sfSprite_setTexture(backgroundSprite, backgroundTexture, sfTrue);
    sfSprite_setTexture(foregroundSprite, foregroundTexture, sfTrue);

    /* Create threads for UDP, Audio, and DMX (pas de thread d'affichage) */
    pthread_t udpThreadId, audioThreadId, dmxThreadId;
#ifdef USE_DMX
    if (pthread_create(&dmxThreadId, NULL, dmxSendingThread, (void *)context.dmxCtx) != 0)
    {
        perror("Error creating DMX thread");
        close(dmxCtx->fd);
        free(dmxCtx);
        sfRenderWindow_destroy(window);
        return EXIT_FAILURE;
    }
#endif
    if (pthread_create(&udpThreadId, NULL, udpThread, (void *)&context) != 0)
    {
        perror("Error creating UDP thread");
        sfRenderWindow_destroy(window);
        return EXIT_FAILURE;
    }
    if (pthread_create(&audioThreadId, NULL, audioProcessingThread, (void *)&context) != 0)
    {
        perror("Error creating audio processing thread");
        sfRenderWindow_destroy(window);
        return EXIT_FAILURE;
    }
    
    struct sched_param param;
    param.sched_priority = 80; // Choisir une priorité adaptée
    pthread_setschedparam(audioThreadId, SCHED_RR, &param);

    /* Main loop (gestion des événements et rendu) */
    sfEvent event;
    sfClock *clock = sfClock_create();
    unsigned int frameCount = 0;
    
    while (sfRenderWindow_isOpen(window))
    {
        /* Gestion des événements dans le thread principal */
        while (sfRenderWindow_pollEvent(window, &event))
        {
            if (event.type == sfEvtClosed)
            {
                sfRenderWindow_close(window);
                context.running = 0;
                dmxCtx->running = 0;
            }
        }

        /* Vérifier si le double buffer contient de nouvelles données */
        pthread_mutex_lock(&db.mutex);
        int dataReady = db.dataReady;
        if (dataReady)
        {
            db.dataReady = 0;
        }
        pthread_mutex_unlock(&db.mutex);

        if (dataReady)
        {
            /* Rendu de la nouvelle ligne à partir du buffer */
            printImageRGB(window,
                          db.processingBuffer_R,
                          db.processingBuffer_G,
                          db.processingBuffer_B,
                          backgroundTexture,
                          foregroundTexture);

            /* Calcul de la couleur moyenne et mise à jour du contexte DMX */
            DMXSpot zoneSpots[DMX_NUM_SPOTS];
            computeAverageColorPerZone(db.processingBuffer_R,
                                       db.processingBuffer_G,
                                       db.processingBuffer_B,
                                       CIS_MAX_PIXELS_NB,
                                       zoneSpots);

            pthread_mutex_lock(&dmxCtx->mutex);
            memcpy(dmxCtx->spots, zoneSpots, sizeof(zoneSpots));
            dmxCtx->colorUpdated = 1;
            pthread_cond_signal(&dmxCtx->cond);
            pthread_mutex_unlock(&dmxCtx->mutex);

            frameCount++;  // Compter chaque image affichée
        }

#ifdef PRINT_FPS
        float elapsedTime = 0.0f;
        /* Calcul du temps écoulé et affichage du taux de rafraîchissement */
        elapsedTime = sfClock_getElapsedTime(clock).microseconds / 1000000.0f;
        if (elapsedTime >= 1.0f)
        {
            float fps = frameCount / elapsedTime;
            printf("Refresh rate: %.2f FPS\n", fps);
            sfClock_restart(clock);
            frameCount = 0;
        }
#endif

        /* Petite pause pour limiter la charge CPU */
        usleep(100);
    }
    
    sfClock_destroy(clock);

    /* Terminaison et synchronisation */
    context.running = 0;
    dmxCtx->running = 0;

    pthread_join(udpThreadId, NULL);
    pthread_join(audioThreadId, NULL);
    pthread_join(dmxThreadId, NULL);

    audio_Cleanup();
    cleanupAudioData(&audioData);
    sfTexture_destroy(backgroundTexture);
    sfTexture_destroy(foregroundTexture);
    sfSprite_destroy(backgroundSprite);
    sfSprite_destroy(foregroundSprite);
    sfRenderWindow_destroy(window);

    return 0;
}
