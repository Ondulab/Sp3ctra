//
//  Display.h
//  SSS_Viewer
//
//  Created by Zhonx on 16/12/2023.
//

#ifndef Display_h
#define Display_h

#include <stdio.h>

int display_Init(sfRenderWindow *window);
void printImageRGB(sfRenderWindow *window, uint8_t *buffer_R, uint8_t *buffer_G, uint8_t *buffer_B, sfTexture *background_texture, sfTexture *foreground_texture);
void printRawData(sfRenderWindow *window, uint32_t *image_buff, sfTexture* background_texture, sfTexture* foreground_texture);

#endif /* Display_h */
