//
//  Display.h
//  SSS_Viewer
//
//  Created by Zhonx on 16/12/2023.
//

#ifndef Display_h
#define Display_h

#include "config.h"  // For CIS_MAX_PIXELS_NB
#include <stdint.h>  // For uint8_t etc.

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

#ifndef NO_SFML
// Function declarations when SFML is available
int display_Init(sfRenderWindow *window);
void display_cleanup(void);
void printImageRGB(sfRenderWindow *window, uint8_t *buffer_R, uint8_t *buffer_G,
                   uint8_t *buffer_B, sfTexture *background_texture,
                   sfTexture *foreground_texture);
#else
// Stub implementations when SFML is disabled (NO_SFML defined)
static inline int display_Init(sfRenderWindow *window) {
    (void)window; // Suppress unused parameter warning
    printf("Display system disabled (NO_SFML)\n");
    return 0;
}

static inline void display_cleanup(void) {
    // Do nothing - display is disabled
}

static inline void printImageRGB(sfRenderWindow *window, uint8_t *buffer_R, uint8_t *buffer_G,
                                 uint8_t *buffer_B, sfTexture *background_texture,
                                 sfTexture *foreground_texture) {
    // Suppress unused parameter warnings
    (void)window;
    (void)buffer_R;
    (void)buffer_G;
    (void)buffer_B;
    (void)background_texture;
    (void)foreground_texture;
    // Do nothing - display is disabled
}
#endif // NO_SFML

#endif /* Display_h */
