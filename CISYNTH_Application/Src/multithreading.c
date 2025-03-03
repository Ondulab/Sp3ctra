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
#include "multithreading.h"

#include "context.h"
#include "error.h"
#include "synth.h"
#include "display.h"
#include "udp.h"
#include "audio.h"
#include "dmx.h"

// Assume CIS_MAX_PIXELS_NB is defined in config.h

// Initialize the double buffer by allocating memory for each channel.
// The caller is responsible for freeing these buffers on program termination.
void initDoubleBuffer(DoubleBuffer *db)
{
    pthread_mutex_init(&db->mutex, NULL);
    pthread_cond_init(&db->cond, NULL);

    db->activeBuffer_R = (uint8_t *)malloc(CIS_MAX_PIXELS_NB * sizeof(uint8_t));
    db->activeBuffer_G = (uint8_t *)malloc(CIS_MAX_PIXELS_NB * sizeof(uint8_t));
    db->activeBuffer_B = (uint8_t *)malloc(CIS_MAX_PIXELS_NB * sizeof(uint8_t));

    db->processingBuffer_R = (uint8_t *)malloc(CIS_MAX_PIXELS_NB * sizeof(uint8_t));
    db->processingBuffer_G = (uint8_t *)malloc(CIS_MAX_PIXELS_NB * sizeof(uint8_t));
    db->processingBuffer_B = (uint8_t *)malloc(CIS_MAX_PIXELS_NB * sizeof(uint8_t));

    if (!db->activeBuffer_R || !db->activeBuffer_G || !db->activeBuffer_B ||
        !db->processingBuffer_R || !db->processingBuffer_G || !db->processingBuffer_B)
    {
        fprintf(stderr, "Error: Allocation of image buffers failed\n");
        exit(EXIT_FAILURE);
    }

    db->dataReady = false;
}

void swapBuffers(DoubleBuffer *db)
{
    uint8_t *temp;

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

void *udpThread(void *arg)
{
    Context *ctx = (Context *)arg;
    DoubleBuffer *db = ctx->doubleBuffer;
    int s = ctx->socket;
    struct sockaddr_in *si_other = ctx->si_other;
    socklen_t slen = sizeof(*si_other);

    ssize_t recv_len;
    struct packet_Image packet;

    // Variables static pour la reconstitution d'une ligne
    static uint32_t curr_line_id = 0;
    static bool received_fragments[UDP_MAX_NB_PACKET_PER_LINE] = { false };
    static uint32_t fragment_count = 0;
    static bool line_complete = false;

    while (true)
    {
        recv_len = recvfrom(s, &packet, sizeof(packet), 0,
                             (struct sockaddr *)si_other, &slen);
        if (recv_len < 0)
        {
            // En cas d'erreur, on continue (vous pouvez ajouter une journalisation)
            continue;
        }

        // Traiter uniquement les paquets ayant le bon header
        if (packet.type != IMAGE_DATA_HEADER)
        {
            continue;
        }

        // Détection du début d'une nouvelle ligne
        if (curr_line_id != packet.line_id)
        {
            curr_line_id = packet.line_id;
            memset(received_fragments, 0, packet.total_fragments * sizeof(bool));
            fragment_count = 0;
            line_complete = false;
        }

        uint32_t offset = packet.fragment_id * packet.fragment_size;
        if (!received_fragments[packet.fragment_id])
        {
            received_fragments[packet.fragment_id] = true;
            fragment_count++;

            // Copier les données du fragment pour chaque canal dans les buffers actifs
            memcpy(&db->activeBuffer_R[offset],
                   packet.imageData_R, packet.fragment_size);
            memcpy(&db->activeBuffer_G[offset],
                   packet.imageData_G, packet.fragment_size);
            memcpy(&db->activeBuffer_B[offset],
                   packet.imageData_B, packet.fragment_size);
        }

        // Vérifier si tous les fragments de la ligne ont été reçus
        if ((fragment_count == packet.total_fragments) && (!line_complete))
        {
            bool allReceived = true;
            for (uint32_t i = 0; i < packet.total_fragments; i++)
            {
                if (!received_fragments[i])
                {
                    allReceived = false;
                    break;
                }
            }
            if (allReceived)
            {
                line_complete = true;
                pthread_mutex_lock(&db->mutex);
                // Effectuer le swap ici pour que le buffer rempli devienne le buffer de traitement
                swapBuffers(db);
                db->dataReady = true;
                pthread_cond_signal(&db->cond);
                pthread_mutex_unlock(&db->mutex);
            }
        }
    }
    return NULL;
}

// Nouvelle fonction pour afficher une ligne d'image en combinant les 3 canaux
void printImageRGB(sfRenderWindow *window,
                   uint8_t *buffer_R,
                   uint8_t *buffer_G,
                   uint8_t *buffer_B,
                   sfTexture *background_texture,
                   sfTexture *foreground_texture)
{
    // Créer une image d'une ligne (largeur = CIS_MAX_PIXELS_NB, hauteur = 1)
    sfImage *image = sfImage_create(CIS_MAX_PIXELS_NB, 1);
    for (int x = 0; x < CIS_MAX_PIXELS_NB; x++)
    {
        sfColor color = sfColor_fromRGB(buffer_R[x], buffer_G[x], buffer_B[x]);
        sfImage_setPixel(image, x, 0, color);
    }

    // Créer une texture à partir de l'image de la ligne
    sfTexture *line_texture = sfTexture_createFromImage(image, NULL);

    // Mettre à jour la texture de premier plan : décaler le fond vers le bas
    sfTexture_updateFromTexture(foreground_texture, background_texture, 0, 1);
    // Afficher la nouvelle ligne en haut
    sfTexture_updateFromImage(foreground_texture, image, 0, 0);

    sfSprite *foreground_sprite = sfSprite_create();
    sfSprite_setTexture(foreground_sprite, foreground_texture, sfTrue);
    sfRenderWindow_drawSprite(window, foreground_sprite, NULL);
    sfRenderWindow_display(window);

    // Copier le contenu de foreground dans background pour la prochaine itération
    sfTexture_updateFromTexture(background_texture, foreground_texture, 0, 0);

    // Nettoyage
    sfImage_destroy(image);
    sfTexture_destroy(line_texture);
    sfSprite_destroy(foreground_sprite);
}

void *imageProcessingThread(void *arg)
{
    Context *context = (Context *)arg;
    DMXContext *dmxCtx = context->dmxCtx;  // Récupération du pointeur DMX

    // Initialisation des textures et sprites pour l’affichage
    sfTexture *background_texture = sfTexture_create(WINDOWS_WIDTH, WINDOWS_HEIGHT);
    sfTexture *foreground_texture = sfTexture_create(WINDOWS_WIDTH, WINDOWS_HEIGHT);
    sfSprite *background_sprite = sfSprite_create();
    sfSprite *foreground_sprite = sfSprite_create();
    sfSprite_setTexture(background_sprite, background_texture, sfTrue);
    sfSprite_setTexture(foreground_sprite, foreground_texture, sfTrue);

    while (sfRenderWindow_isOpen(context->window))
    {
        // Attendre que la donnée soit prête
        pthread_mutex_lock(&context->doubleBuffer->mutex);
        while (!context->doubleBuffer->dataReady)
        {
            pthread_cond_wait(&context->doubleBuffer->cond, &context->doubleBuffer->mutex);
        }
        pthread_mutex_unlock(&context->doubleBuffer->mutex);

        // Traitement de l'image
        printImageRGB(context->window,
                      context->doubleBuffer->processingBuffer_R,
                      context->doubleBuffer->processingBuffer_G,
                      context->doubleBuffer->processingBuffer_B,
                      background_texture,
                      foreground_texture);

        // Marquer la donnée comme traitée
        pthread_mutex_lock(&context->doubleBuffer->mutex);
        context->doubleBuffer->dataReady = false;
        pthread_mutex_unlock(&context->doubleBuffer->mutex);

        // Calcul de la couleur moyenne
        uint8_t avgR, avgG, avgB;
        computeAverageColorWeighted(context->doubleBuffer->processingBuffer_R, //computeAverageColor computeAverageColorGamma computeAverageColorWeighted computeAverageColorSegmented
                            context->doubleBuffer->processingBuffer_G,
                            context->doubleBuffer->processingBuffer_B,
                            CIS_MAX_PIXELS_NB, &avgR, &avgG, &avgB);
        
        // Mise à jour du DMXContext via le pointeur récupéré
        pthread_mutex_lock(&dmxCtx->mutex);
        dmxCtx->avgR = avgR;
        dmxCtx->avgG = avgG;
        dmxCtx->avgB = avgB;
        dmxCtx->colorUpdated = 1;
        pthread_cond_signal(&dmxCtx->cond);
        pthread_mutex_unlock(&dmxCtx->mutex);
    }

    // Nettoyage des ressources graphiques
    sfTexture_destroy(background_texture);
    sfTexture_destroy(foreground_texture);
    sfSprite_destroy(background_sprite);
    sfSprite_destroy(foreground_sprite);

    return NULL;
}

void *dmxSendingThread(void *arg)
{
    DMXContext *dmxCtx = (DMXContext *)arg;
    unsigned char frame[FRAME_SIZE];
    memset(frame, 0, FRAME_SIZE);
    frame[0] = 0;  // Start code
    frame[1] = 0;  // Mode (si utilisé)
    frame[5] = 0;  // Speed (si utilisé)

    while (dmxCtx->running)
    {
        pthread_mutex_lock(&dmxCtx->mutex);
        while (!dmxCtx->colorUpdated)
        {
            pthread_cond_wait(&dmxCtx->cond, &dmxCtx->mutex);
        }
        // Récupérer la couleur moyenne mise à jour
        uint8_t avgR = dmxCtx->avgR;
        uint8_t avgG = dmxCtx->avgG;
        uint8_t avgB = dmxCtx->avgB;
        dmxCtx->colorUpdated = 0;
        pthread_mutex_unlock(&dmxCtx->mutex);

        // Remplir la trame DMX aux indices correspondant aux canaux R, G, B
        frame[2] = avgR;
        frame[3] = avgG;
        frame[4] = avgB;

        //printf("RGB values: R=%d, G=%d, B=%d\n", avgR, avgG, avgB);
        
        if (send_dmx_frame(dmxCtx->fd, frame, FRAME_SIZE) < 0)
        {
            perror("Error sending DMX frame");
        }

        usleep(25000); // Intervalle de 25 ms entre les trames
    }

    close(dmxCtx->fd);
    return NULL;
}

void *audioProcessingThread(void *arg)
{
    Context *context = (Context *)arg;
    DoubleBuffer *db = context->doubleBuffer;
    
    // Allocation d'un buffer composite pour des données 16-bit (2 octets par pixel)
    uint16_t *monoBuffer = (uint16_t *)malloc(CIS_MAX_PIXELS_NB * sizeof(uint16_t));
    if (monoBuffer == NULL)
    {
        fprintf(stderr, "Error: Allocation of monoBuffer failed\n");
        exit(EXIT_FAILURE);
    }

    // Allocation d'un buffer temporaire pour des échantillons 32-bit
    int32_t *convertedBuffer = (int32_t *)malloc(CIS_MAX_PIXELS_NB * sizeof(int32_t));
    if (convertedBuffer == NULL)
    {
        fprintf(stderr, "Error: Allocation of convertedBuffer failed\n");
        free(monoBuffer);
        exit(EXIT_FAILURE);
    }

    while (true)
    {
        pthread_mutex_lock(&db->mutex);
        if (db->dataReady)
        {
            // Conversion des canaux R, G, B en niveaux de gris 16-bit.
            // On suppose ici que les valeurs de R, G, B sont sur 8 bits.
            for (uint32_t i = 0; i < CIS_MAX_PIXELS_NB; i++)
            {
                uint8_t r = db->processingBuffer_R[i];
                uint8_t g = db->processingBuffer_G[i];
                uint8_t b = db->processingBuffer_B[i];
                // Conversion en niveau de gris sur 8 bits en utilisant la formule pondérée
                uint8_t gray = (uint8_t)((r * 299 + g * 587 + b * 114) / 1000);
                // Expansion en 16-bit en dupliquant la valeur sur les deux octets (exemple : 0xAB devient 0xABAB)
                monoBuffer[i] = (gray << 8) | gray;
            }
            
            // Conversion de chaque échantillon 16-bit en un entier signé 32-bit.
            // Le cast via int16_t permet de conserver la valeur signée si besoin.
            for (uint32_t i = 0; i < CIS_MAX_PIXELS_NB; i++)
            {
                int16_t sample16 = *((int16_t *)&monoBuffer[i]);
                convertedBuffer[i] = (int32_t)sample16;
            }
            
            db->dataReady = false;
            swapBuffers(db);
        }
        pthread_mutex_unlock(&db->mutex);

        // Traitement audio à partir des échantillons convertis.
        synth_AudioProcess(convertedBuffer, audio_samples);
    }

    free(convertedBuffer);
    free(monoBuffer);
    return NULL;
}

