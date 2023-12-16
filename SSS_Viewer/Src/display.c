//
//  Display.c
//  SSS_Viewer
//
//  Created by Zhonx on 16/12/2023.
//
#include "config.h"

#include <SFML/Graphics.h>
#include <SFML/Network.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <errno.h>
#include <fcntl.h>
#include <termios.h>

#include "display.h"

//#define CALIBRATION
//#define SSS_MOD_MODE

// Fonction pour quitter avec un message d'erreur
static void die(const char *s)
{
    perror(s);
    exit(1);
}

uint32_t greyScale(uint32_t rbg888)
{
    static uint32_t grey, r, g, b;

    r = rbg888             & 0xFF; // ___________XXXXX
    g = (rbg888 >> 8)     & 0xFF; // _____XXXXXX_____
    b = (rbg888 >> 12)     & 0xFF; // XXXXX___________

    grey = (r * 299 + g * 587 + b * 114);
    return grey >> 2;
}

int display_Init(sfRenderWindow *window)
{
    if (!window) die("sfRenderWindow_create failed");
    
    printf("SFML FULL CONFIGURED\n");
    
    return 0;
}

void printImage(sfRenderWindow *window, uint32_t *image_buff, sfTexture* background_texture, sfTexture* foreground_texture)
{
    // Créer une image pour la nouvelle ligne
    sfImage* image = sfImage_createFromColor(CIS_PIXELS_NB, 1, sfBlack);

    // Définir la couleur de chaque pixel de la nouvelle ligne
    for (int x = 0; x < CIS_PIXELS_NB; x++) {
        sfColor color = sfColor_fromRGB(
            image_buff[x] & 0xFF,
            (image_buff[x] >> 8) & 0xFF,
            (image_buff[x] >> 16) & 0xFF
        );
        sfImage_setPixel(image, x, 0, color);
    }

    // Créer une texture à partir de l'image de la nouvelle ligne
    sfTexture* line_texture = sfTexture_createFromImage(image, NULL);

    // Copier le contenu de la texture de fond dans la texture de premier plan avec un décalage d'une ligne vers le bas
    sfTexture_updateFromTexture(foreground_texture, background_texture, 0, 1);

    // Dessiner la nouvelle ligne en haut de la texture de premier plan
    sfTexture_updateFromImage(foreground_texture, image, 0, 0);

    // Dessiner la texture de premier plan sur la fenêtre
    sfSprite* foreground_sprite = sfSprite_create();
    sfSprite_setTexture(foreground_sprite, foreground_texture, sfTrue);
    sfRenderWindow_drawSprite(window, foreground_sprite, NULL);

    // Afficher le contenu de la fenêtre
    sfRenderWindow_display(window);

    // Copier le contenu de la texture de premier plan dans la texture de fond pour la prochaine itération
    sfTexture_updateFromTexture(background_texture, foreground_texture, 0, 0);

    // Nettoyage
    sfImage_destroy(image);
    sfTexture_destroy(line_texture);
    sfSprite_destroy(foreground_sprite);
}

void printRawData(sfRenderWindow *window, uint32_t *image_buff, sfTexture* background_texture, sfTexture* foreground_texture)
{
    sfRenderWindow_clear(window, sfBlack);

    // Création d'une ligne verticale pour chaque x
    for (int x = 0; x < CIS_PIXELS_NB; x++) {
        // Dessiner une ligne verticale noire
        sfVertexArray *line = sfVertexArray_create();
        sfVertexArray_setPrimitiveType(line, sfLinesStrip);
        sfVertex vertex1 = {.position = {x, 0}, .color = sfBlack};
        sfVertex vertex2 = {.position = {x, WINDOWS_HEIGHT}, .color = sfBlack};
        sfVertexArray_append(line, vertex1);
        sfVertexArray_append(line, vertex2);
        sfRenderWindow_drawVertexArray(window, line, NULL);
        sfVertexArray_destroy(line);

        // Dessiner le point vert
        sfVertexArray *point = sfVertexArray_create();
        sfVertexArray_setPrimitiveType(point, sfPoints);
        sfVertex vertex = {.position = {x, (float)(image_buff[x] * (WINDOWS_HEIGHT / 8192.0f))}, .color = sfGreen};
        sfVertexArray_append(point, vertex);
        sfRenderWindow_drawVertexArray(window, point, NULL);
        sfVertexArray_destroy(point);
    }

    // Préparation pour dessiner les textures
    sfSprite* background_sprite = sfSprite_create();
    sfSprite* foreground_sprite = sfSprite_create();
    sfSprite_setTexture(background_sprite, background_texture, sfTrue);
    sfSprite_setTexture(foreground_sprite, foreground_texture, sfTrue);

    // Dessiner la texture d'arrière-plan
    sfRenderWindow_drawSprite(window, background_sprite, NULL);

    // Dessiner la texture d'avant-plan
    sfRenderWindow_drawSprite(window, foreground_sprite, NULL);

    // Afficher le contenu de la fenêtre à l'écran
    sfRenderWindow_display(window);

    // Libération des ressources
    sfSprite_destroy(background_sprite);
    sfSprite_destroy(foreground_sprite);
}
