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
#ifdef __LINUX__
#ifndef NO_SFML
#include <SFML/Graphics.h>
#endif
#else // macOS
#include <SFML/Graphics.h>
#endif

// Forward declare SFML types if NO_SFML is defined and SFML/Graphics.h isn't
// included
#if defined(__LINUX__) && defined(NO_SFML)
typedef struct sfRenderWindow sfRenderWindow;
typedef struct sfTexture sfTexture;
// Add other SFML types if they appear in prototypes and NO_SFML is possible
#endif

int display_Init(sfRenderWindow *window);
void printImageRGB(sfRenderWindow *window, uint8_t *buffer_R, uint8_t *buffer_G,
                   uint8_t *buffer_B, sfTexture *background_texture,
                   sfTexture *foreground_texture);
void printRawData(sfRenderWindow *window, uint32_t *image_buff,
                  sfTexture *background_texture, sfTexture *foreground_texture);

#endif /* Display_h */
