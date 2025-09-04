/* Simplified stb_image_write.h for debug purposes */

#ifndef INCLUDE_STB_IMAGE_WRITE_H
#define INCLUDE_STB_IMAGE_WRITE_H

#ifdef __cplusplus
extern "C" {
#endif

// Function declarations
int stbi_write_png(char const *filename, int w, int h, int comp, const void *data, int stride_in_bytes);
int stbi_write_bmp(char const *filename, int w, int h, int comp, const void *data);

#ifdef __cplusplus
}
#endif

#ifdef STB_IMAGE_WRITE_IMPLEMENTATION

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Simplified BMP writer implementation
int stbi_write_bmp(char const *filename, int w, int h, int comp, const void *data)
{
    FILE *f = fopen(filename, "wb");
    if (!f) return 0;
    
    unsigned char *d = (unsigned char*)data;
    int pad = (4 - (w * 3) % 4) % 4;
    int filesize = 54 + (w * 3 + pad) * h;
    
    // BMP header
    unsigned char header[54] = {
        'B','M',                    // signature
        filesize&0xFF, (filesize>>8)&0xFF, (filesize>>16)&0xFF, (filesize>>24)&0xFF, // file size
        0,0,0,0,                    // reserved
        54,0,0,0,                   // data offset
        40,0,0,0,                   // info header size
        w&0xFF, (w>>8)&0xFF, (w>>16)&0xFF, (w>>24)&0xFF,     // width
        h&0xFF, (h>>8)&0xFF, (h>>16)&0xFF, (h>>24)&0xFF,     // height
        1,0,                        // planes
        24,0,                       // bits per pixel
        0,0,0,0,                    // compression
        0,0,0,0,                    // image size
        0,0,0,0,                    // x pixels per meter
        0,0,0,0,                    // y pixels per meter
        0,0,0,0,                    // colors used
        0,0,0,0                     // important colors
    };
    
    fwrite(header, 1, 54, f);
    
    // Write pixel data (BMP is bottom-up)
    unsigned char padding[3] = {0,0,0};
    for (int y = h-1; y >= 0; y--) {
        for (int x = 0; x < w; x++) {
            int idx = (y * w + x);
            if (comp == 1) {
                // Grayscale to BGR
                fputc(d[idx], f);
                fputc(d[idx], f);
                fputc(d[idx], f);
            } else if (comp == 3) {
                // RGB to BGR
                fputc(d[idx*3+2], f);
                fputc(d[idx*3+1], f);
                fputc(d[idx*3+0], f);
            } else if (comp == 4) {
                // RGBA to BGR (ignore alpha)
                fputc(d[idx*4+2], f);
                fputc(d[idx*4+1], f);
                fputc(d[idx*4+0], f);
            }
        }
        fwrite(padding, 1, pad, f);
    }
    
    fclose(f);
    return 1;
}

// Simplified PNG writer (actually writes BMP for simplicity)
int stbi_write_png(char const *filename, int w, int h, int comp, const void *data, int stride_in_bytes)
{
    // For simplicity, just write as BMP
    (void)stride_in_bytes; // unused
    return stbi_write_bmp(filename, w, h, comp, data);
}

#endif // STB_IMAGE_WRITE_IMPLEMENTATION

#endif // INCLUDE_STB_IMAGE_WRITE_H
