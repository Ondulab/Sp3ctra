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

int main(void)
{
    int dmxFd = init_Dmx();
    if (dmxFd < 0)
    {
        perror("Error initializing DMX");
        return EXIT_FAILURE;
    }
    DMXContext *dmxCtx = malloc(sizeof(DMXContext));
    if (dmxCtx == NULL)
    {
        perror("Error allocating DMXContext");
        close(dmxFd);
        return EXIT_FAILURE;
    }
    dmxCtx->fd = dmxFd;
    dmxCtx->running = 1;
    dmxCtx->colorUpdated = 0;
    pthread_mutex_init(&dmxCtx->mutex, NULL);
    pthread_cond_init(&dmxCtx->cond, NULL);

    // Initialize CSFML
    sfVideoMode mode = { WINDOWS_WIDTH, WINDOWS_HEIGHT, 32 };
    sfRenderWindow *window = sfRenderWindow_create(mode, "CSFML Viewer", sfResize | sfClose, NULL);
    if (!window)
    {
        perror("Error creating CSFML window");
        close(dmxCtx->fd);
        free(dmxCtx);
        return EXIT_FAILURE;
    }

    // Initialize UDP and Audio
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

    // Create double buffer
    DoubleBuffer db;
    initDoubleBuffer(&db);

    // Build global context structure
    Context context = { 0 };
    context.window = window;
    context.socket = s;
    context.si_other = &si_other;
    context.si_me = &si_me;
    context.audioData = &audioData;
    context.doubleBuffer = &db;
    context.dmxCtx = dmxCtx;

    // Create threads
    pthread_t udpThreadId, imageThreadId, audioThreadId, dmxThreadId;
    if (pthread_create(&dmxThreadId, NULL, dmxSendingThread, (void *)context.dmxCtx) != 0)
    {
        perror("Error creating DMX thread");
        close(dmxCtx->fd);
        free(dmxCtx);
        sfRenderWindow_destroy(window);
        return EXIT_FAILURE;
    }
    if (pthread_create(&udpThreadId, NULL, udpThread, (void *)&context) != 0)
    {
        perror("Error creating UDP thread");
        sfRenderWindow_destroy(window);
        return EXIT_FAILURE;
    }
    if (pthread_create(&imageThreadId, NULL, imageProcessingThread, (void *)&context) != 0)
    {
        perror("Error creating image processing thread");
        sfRenderWindow_destroy(window);
        return EXIT_FAILURE;
    }
    if (pthread_create(&audioThreadId, NULL, audioProcessingThread, (void *)&context) != 0)
    {
        perror("Error creating audio processing thread");
        sfRenderWindow_destroy(window);
        return EXIT_FAILURE;
    }

    // Main CSFML loop
    while (sfRenderWindow_isOpen(window))
    {
        sfEvent event;
        while (sfRenderWindow_pollEvent(window, &event))
        {
            if (event.type == sfEvtClosed)
            {
                sfRenderWindow_close(window);
            }
        }
    }

    // Cleanup and join threads
    pthread_join(udpThreadId, NULL);
    pthread_join(imageThreadId, NULL);
    pthread_join(audioThreadId, NULL);
    // (Optionally join DMX thread if required)

    audio_Cleanup();
    cleanupAudioData(&audioData);
    sfRenderWindow_destroy(window);

    return 0;
}
