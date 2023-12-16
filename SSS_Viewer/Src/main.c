/*
 ============================================================================
 Name        : SSS_Viewer.c
 Author      : ZHONX
 Version     :
 Copyright   : Your copyright notice
 Description : SSS image visualization
 ============================================================================
 */
#include "config.h"

#include <SFML/Graphics.h>
#include <SFML/Network.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include<arpa/inet.h>
#include<sys/socket.h>

#include <errno.h>
#include <fcntl.h>
#include <termios.h>

#include "synth.h"
#include "display.h"
#include "udp.h"


//#define CALIBRATION
//#define SSS_MOD_MODE

// Fonction pour quitter avec un message d'erreur
static void die(const char *s)
{
    perror(s);
    exit(1);
}

int main_loop(sfRenderWindow *window, int s, struct sockaddr_in *si_other, struct sockaddr_in *si_me) {
    ssize_t recv_len;
    uint32_t buf[UDP_PACKET_SIZE];
    uint32_t image_buff[CIS_PIXELS_NB];
    static uint32_t curr_packet = 0, curr_packet_header = 0;
    unsigned int slen = sizeof(*si_other);

    sfTexture* background_texture = sfTexture_create(WINDOWS_WIDTH, WINDOWS_HEIGHT);
    sfTexture* foreground_texture = sfTexture_create(WINDOWS_WIDTH, WINDOWS_HEIGHT);
    sfSprite* background_sprite = sfSprite_create();
    sfSprite* foreground_sprite = sfSprite_create();
    sfSprite_setTexture(background_sprite, background_texture, sfTrue);
    sfSprite_setTexture(foreground_sprite, foreground_texture, sfTrue);

    while (sfRenderWindow_isOpen(window)) {
        sfEvent event;
        while (sfRenderWindow_pollEvent(window, &event)) {
            if (event.type == sfEvtClosed) {
                sfRenderWindow_close(window);
            }
        }

        for (curr_packet = 0; curr_packet < UDP_NB_PACKET_PER_LINE; curr_packet++) {
            if ((recv_len = recvfrom(s, (uint8_t*)buf, UDP_PACKET_SIZE * sizeof(int32_t), 0, (struct sockaddr *)si_other, &slen)) == -1) {
                die("recvfrom()");
            }
            curr_packet_header = buf[0];
            memcpy(&image_buff[curr_packet_header], &buf[1], recv_len - (UDP_HEADER_SIZE * sizeof(int32_t)));

#ifdef SSS_MOD_MODE
            for (int idx = NUMBER_OF_NOTES; --idx >= 0;) {
                image_audio_buff[idx] = greyScale(image_buff[(idx * PIXELS_PER_NOTE)]);
            }
#endif
        }

        printImage(window, image_buff, background_texture, foreground_texture);
    }

    sfSprite_destroy(background_sprite);
    sfSprite_destroy(foreground_sprite);
    sfTexture_destroy(background_texture);
    sfTexture_destroy(foreground_texture);
    sfRenderWindow_destroy(window);

    return 0;
}


int main(void)
{
    //--------------------------------------SDL2 INIT-------------------------------------------//
    // Initialisation de CSFML
    sfVideoMode mode = {WINDOWS_WIDTH, WINDOWS_HEIGHT, 32};
    sfRenderWindow* window = sfRenderWindow_create(mode, "CSFML Viewer", sfResize | sfClose, NULL);
    struct sockaddr_in si_other;
    struct sockaddr_in si_me;
    
    synth_IfftInit();
    display_Init(window);
    int s = udp_Init(&si_other, &si_me);

    main_loop(window, s, &si_other, &si_me);

    return 0;
}
