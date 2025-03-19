/* multithreading.c */

#include "config.h"
#include "multithreading.h"
#include "context.h"
#include "error.h"
#include "synth.h"
#include "display.h"
#include "udp.h"
#include "audio.h"
#include "dmx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <SFML/Graphics.h>
#include <SFML/Network.h>
#include <errno.h>

/*------------------------------------------------------------------------------
    Helper Functions
------------------------------------------------------------------------------*/

// Wait for the double buffer to be ready
static void WaitForDoubleBuffer(DoubleBuffer *db)
{
    pthread_mutex_lock(&db->mutex);
    while (!db->dataReady)
    {
        pthread_cond_wait(&db->cond, &db->mutex);
    }
    pthread_mutex_unlock(&db->mutex);
}

// Wait for DMX color update using condition variable
static void WaitForDMXColorUpdate(DMXContext *ctx)
{
    pthread_mutex_lock(&ctx->mutex);
    while (!ctx->colorUpdated)
    {
        pthread_cond_wait(&ctx->cond, &ctx->mutex);
    }
    pthread_mutex_unlock(&ctx->mutex);
}

void initDoubleBuffer(DoubleBuffer *db)
{
    if (pthread_mutex_init(&db->mutex, NULL) != 0)
    {
        fprintf(stderr, "Error: Mutex initialization failed\n");
        exit(EXIT_FAILURE);
    }
    if (pthread_cond_init(&db->cond, NULL) != 0)
    {
        fprintf(stderr, "Error: Condition variable initialization failed\n");
        exit(EXIT_FAILURE);
    }

    db->activeBuffer_R = (uint8_t *) malloc(CIS_MAX_PIXELS_NB * sizeof(uint8_t));
    db->activeBuffer_G = (uint8_t *) malloc(CIS_MAX_PIXELS_NB * sizeof(uint8_t));
    db->activeBuffer_B = (uint8_t *) malloc(CIS_MAX_PIXELS_NB * sizeof(uint8_t));

    db->processingBuffer_R = (uint8_t *) malloc(CIS_MAX_PIXELS_NB * sizeof(uint8_t));
    db->processingBuffer_G = (uint8_t *) malloc(CIS_MAX_PIXELS_NB * sizeof(uint8_t));
    db->processingBuffer_B = (uint8_t *) malloc(CIS_MAX_PIXELS_NB * sizeof(uint8_t));

    if (!db->activeBuffer_R || !db->activeBuffer_G || !db->activeBuffer_B ||
        !db->processingBuffer_R || !db->processingBuffer_G || !db->processingBuffer_B)
    {
        fprintf(stderr, "Error: Allocation of image buffers failed\n");
        exit(EXIT_FAILURE);
    }

    db->dataReady = 0;
}

void swapBuffers(DoubleBuffer *db)
{
    uint8_t *temp = NULL;

    temp = db->activeBuffer_R;
    db->activeBuffer_R = db->processingBuffer_R;
    db->processingBuffer_R = temp;

    temp = db->activeBuffer_G;
    db->activeBuffer_G = db->processingBuffer_G;
    db->processingBuffer_G = temp;

    temp = db->activeBuffer_B;
    db->activeBuffer_B = db->processingBuffer_B;
    db->processingBuffer_B = temp;
}

/*------------------------------------------------------------------------------
    Thread Implementations
------------------------------------------------------------------------------*/

void *udpThread(void *arg)
{
    Context *ctx = (Context *) arg;
    DoubleBuffer *db = ctx->doubleBuffer;
    int s = ctx->socket;
    struct sockaddr_in *si_other = ctx->si_other;
    socklen_t slen = sizeof(*si_other);
    ssize_t recv_len;
    struct packet_Image packet;

    /* Variables locales pour reconstitution de ligne */
    uint32_t currentLineId = 0;
    bool *receivedFragments = (bool *) calloc(UDP_MAX_NB_PACKET_PER_LINE, sizeof(bool));
    if (receivedFragments == NULL)
    {
        perror("Error allocating receivedFragments");
        exit(EXIT_FAILURE);
    }
    uint32_t fragmentCount = 0;

    while (ctx->running)
    {
        recv_len = recvfrom(s, &packet, sizeof(packet), 0, (struct sockaddr *) si_other, &slen);
        if (recv_len < 0)
        {
            continue;
        }

        if (packet.type != IMAGE_DATA_HEADER)
        {
            continue;
        }

        if (currentLineId != packet.line_id)
        {
            currentLineId = packet.line_id;
            memset(receivedFragments, 0, packet.total_fragments * sizeof(bool));
            fragmentCount = 0;
        }

        uint32_t offset = packet.fragment_id * packet.fragment_size;
        if (!receivedFragments[packet.fragment_id])
        {
            receivedFragments[packet.fragment_id] = true;
            fragmentCount++;
            memcpy(&db->activeBuffer_R[offset], packet.imageData_R, packet.fragment_size);
            memcpy(&db->activeBuffer_G[offset], packet.imageData_G, packet.fragment_size);
            memcpy(&db->activeBuffer_B[offset], packet.imageData_B, packet.fragment_size);
        }

        if (fragmentCount == packet.total_fragments)
        {
            pthread_mutex_lock(&db->mutex);
            swapBuffers(db);
            db->dataReady = true;
            pthread_cond_signal(&db->cond);
            pthread_mutex_unlock(&db->mutex);
        }
    }

    free(receivedFragments);
    return NULL;
}

void *dmxSendingThread(void *arg)
{
    DMXContext *dmxCtx = (DMXContext *) arg;
    unsigned char frame[DMX_FRAME_SIZE];

    while (dmxCtx->running)
    {
        WaitForDMXColorUpdate(dmxCtx);

        DMXSpot currentSpots[DMX_NUM_SPOTS];
        pthread_mutex_lock(&dmxCtx->mutex);
        memcpy(currentSpots, dmxCtx->spots, sizeof(currentSpots));
        dmxCtx->colorUpdated = 0;
        pthread_mutex_unlock(&dmxCtx->mutex);

        // Appliquer le profil couleur pour chaque zone
        for (int i = 0; i < DMX_NUM_SPOTS; i++)
        {
            double redFactor = 1.0;
            double greenFactor = 1.0;
            double blueFactor = 1.0;
            applyColorProfile(&currentSpots[i].red,
                              &currentSpots[i].green,
                              &currentSpots[i].blue,
                              redFactor,
                              greenFactor,
                              blueFactor);
        }

        // Réinitialiser la trame DMX et définir le start code
        memset(frame, 0, DMX_FRAME_SIZE);
        frame[0] = 0;

        // Pour chaque spot, insérer les 7 canaux dans la trame à partir de l'adresse indiquée
        for (int i = 0; i < DMX_NUM_SPOTS; i++)
        {
            int base = currentSpots[i].channel;
            // On suppose ici que la valeur 'channel' correspond à l'adresse DMX de départ (1-indexée)
            frame[base]     = currentSpots[i].dimmer;
            frame[base + 1] = currentSpots[i].red;
            frame[base + 2] = currentSpots[i].green;
            frame[base + 3] = currentSpots[i].blue;
            frame[base + 4] = currentSpots[i].white;
            frame[base + 5] = currentSpots[i].mode;
            frame[base + 6] = currentSpots[i].strobo;
        }

        if (send_dmx_frame(dmxCtx->fd, frame, DMX_FRAME_SIZE) < 0)
        {
            perror("Error sending DMX frame");
        }

        usleep(25000);
    }

    close(dmxCtx->fd);
    return NULL;
}

void *audioProcessingThread(void *arg)
{
    Context *context = (Context *) arg;
    DoubleBuffer *db = context->doubleBuffer;

    while (context->running)
    {
        // Wait until one of the buffers is ready for processing
        WaitForDoubleBuffer(db);

        // Mark double buffer as free, swap it
        pthread_mutex_lock(&db->mutex);
        db->dataReady = false;
        swapBuffers(db);
        pthread_mutex_unlock(&db->mutex);

        // Call your synthesis routine to fill the audio buffers
        synth_AudioProcess(db->processingBuffer_R,
                           db->processingBuffer_G,
                           db->processingBuffer_B);
    }
    
    return NULL;
}
