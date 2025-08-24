//
//  Display.h
//  SSS_Viewer
//
//  Created by Zhonx on 16/12/2023.
//

#ifndef Display_h
#define Display_h

#include "config.h"  // For CIS_MAX_PIXELS_NB
#include <pthread.h> // For pthread_mutex_t
#include <stdint.h>  // For uint8_t etc.
#include <stdio.h>

// SFML Includes needed for type definitions in function prototypes
#ifndef NO_SFML
// If SFML is NOT disabled, include the SFML headers
#include <SFML/Graphics.h>
#else
// If SFML IS disabled (NO_SFML is defined),
// provide forward declarations for SFML types used in function prototypes.
typedef struct sfRenderWindow sfRenderWindow;
typedef struct sfTexture sfTexture;
typedef struct sfImage
    sfImage; // Added as it's used internally by display functions
typedef struct sfSprite sfSprite; // Added
typedef struct sfColor sfColor;   // Added
// Add other SFML types if they appear in prototypes and NO_SFML is possible
#endif // NO_SFML

int display_Init(sfRenderWindow *window);
void printImageRGB(sfRenderWindow *window, uint8_t *buffer_R, uint8_t *buffer_G,
                   uint8_t *buffer_B, sfTexture *background_texture,
                   sfTexture *foreground_texture);
void printRawData(sfRenderWindow *window, uint32_t *image_buff,
                  sfTexture *background_texture, sfTexture *foreground_texture);

#endif /* Display_h */
