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
    unsigned char frame[FRAME_SIZE];
    memset(frame, 0, FRAME_SIZE);
    frame[0] = 0;  // Start code
    frame[1] = 0;  // Mode (if used)
    frame[5] = 0;  // Speed (if used)

    while (dmxCtx->running)
    {
        WaitForDMXColorUpdate(dmxCtx);

        pthread_mutex_lock(&dmxCtx->mutex);
        uint8_t avgR = dmxCtx->avgR;
        uint8_t avgG = dmxCtx->avgG;
        uint8_t avgB = dmxCtx->avgB;
        dmxCtx->colorUpdated = 0;
        pthread_mutex_unlock(&dmxCtx->mutex);

        double redFactor = 0.9;
        double greenFactor = 1.0;
        double blueFactor = 0.4;
        applyColorProfile(&avgR, &avgG, &avgB, redFactor, greenFactor, blueFactor);

        frame[2] = avgR;
        frame[3] = avgG;
        frame[4] = avgB;

        if (send_dmx_frame(dmxCtx->fd, frame, FRAME_SIZE) < 0)
        {
            perror("Error sending DMX frame");
        }

        usleep(25000);  // 25 ms interval
    }

    close(dmxCtx->fd);
    return NULL;
}

void *audioProcessingThread(void *arg)
{
    Context *context = (Context *) arg;
    DoubleBuffer *db = context->doubleBuffer;

    uint16_t *monoBuffer = (uint16_t *) malloc(CIS_MAX_PIXELS_NB * sizeof(uint16_t));
    if (monoBuffer == NULL)
    {
        fprintf(stderr, "Error: Allocation of monoBuffer failed\n");
        exit(EXIT_FAILURE);
    }

    int32_t *convertedBuffer = (int32_t *) malloc(CIS_MAX_PIXELS_NB * sizeof(int32_t));
    if (convertedBuffer == NULL)
    {
        fprintf(stderr, "Error: Allocation of convertedBuffer failed\n");
        free(monoBuffer);
        exit(EXIT_FAILURE);
    }

    while (context->running)
    {
        WaitForDoubleBuffer(db);

        for (uint32_t i = 0; i < CIS_MAX_PIXELS_NB; i++)
        {
            uint8_t r = db->processingBuffer_R[i];
            uint8_t g = db->processingBuffer_G[i];
            uint8_t b = db->processingBuffer_B[i];
            uint8_t gray = (uint8_t)((r * 299 + g * 587 + b * 114) / 1000);
            monoBuffer[i] = (gray << 8) | gray;
        }

        for (uint32_t i = 0; i < CIS_MAX_PIXELS_NB; i++)
        {
            int16_t sample16 = *((int16_t *)&monoBuffer[i]);
            convertedBuffer[i] = (int32_t) sample16;
        }

        pthread_mutex_lock(&db->mutex);
        db->dataReady = false;
        swapBuffers(db);
        pthread_mutex_unlock(&db->mutex);

        synth_AudioProcess(convertedBuffer, audio_samples);
    }

    free(convertedBuffer);
    free(monoBuffer);
    return NULL;
}
