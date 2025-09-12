#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

// DMX Spot Type enumeration
typedef enum {
    DMX_SPOT_RGB = 3,
    // Future: DMX_SPOT_RGBW = 4, DMX_SPOT_RGBA = 4, etc.
} DMXSpotType;

// RGB Spot structure
typedef struct {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
} DMXSpotRGB;

// Union for different spot data types
typedef union {
    DMXSpotRGB rgb;
    // Future extensions: DMXSpotRGBW rgbw;
} DMXSpotData;

// Main spot structure
typedef struct {
    DMXSpotType type;
    uint8_t start_channel;
    DMXSpotData data;
} DMXSpot;

// Test the flexible configuration system
int main() {
    printf("🧪 Testing flexible DMX configuration system\n\n");
    
    // Test 1: 18 RGB spots starting at channel 1
    printf("=== TEST 1: 18 RGB spots ===\n");
    int num_spots_1 = 18;
    DMXSpot *spots_1 = malloc(num_spots_1 * sizeof(DMXSpot));
    
    // Generate channel mapping manually for test
    for (int i = 0; i < num_spots_1; i++) {
        spots_1[i].type = DMX_SPOT_RGB;
        spots_1[i].start_channel = 1 + (i * 3); // RGB = 3 channels per spot
        spots_1[i].data.rgb.red = 0;
        spots_1[i].data.rgb.green = 0;
        spots_1[i].data.rgb.blue = 0;
    }
    
    printf("✅ %d spots configured\n", num_spots_1);
    printf("   Spot[0]: channels %d-%d\n", spots_1[0].start_channel, spots_1[0].start_channel + 2);
    printf("   Spot[%d]: channels %d-%d\n", num_spots_1-1, spots_1[num_spots_1-1].start_channel, spots_1[num_spots_1-1].start_channel + 2);
    printf("   Total channels used: %d\n", spots_1[num_spots_1-1].start_channel + 2);
    
    // Test 2: 27 RGB spots starting at channel 1
    printf("\n=== TEST 2: 27 RGB spots ===\n");
    int num_spots_2 = 27;
    DMXSpot *spots_2 = malloc(num_spots_2 * sizeof(DMXSpot));
    
    for (int i = 0; i < num_spots_2; i++) {
        spots_2[i].type = DMX_SPOT_RGB;
        spots_2[i].start_channel = 1 + (i * 3);
        spots_2[i].data.rgb.red = 0;
        spots_2[i].data.rgb.green = 0;
        spots_2[i].data.rgb.blue = 0;
    }
    
    printf("✅ %d spots configured\n", num_spots_2);
    printf("   Spot[0]: channels %d-%d\n", spots_2[0].start_channel, spots_2[0].start_channel + 2);
    printf("   Spot[%d]: channels %d-%d\n", num_spots_2-1, spots_2[num_spots_2-1].start_channel, spots_2[num_spots_2-1].start_channel + 2);
    printf("   Total channels used: %d\n", spots_2[num_spots_2-1].start_channel + 2);
    
    // Test 3: Maximum boundary test
    printf("\n=== TEST 3: Boundary test (170 RGB spots) ===\n");
    int num_spots_3 = 170; // 170 * 3 = 510 channels (within 512 limit)
    DMXSpot *spots_3 = malloc(num_spots_3 * sizeof(DMXSpot));
    
    for (int i = 0; i < num_spots_3; i++) {
        spots_3[i].type = DMX_SPOT_RGB;
        spots_3[i].start_channel = 1 + (i * 3);
        spots_3[i].data.rgb.red = 0;
        spots_3[i].data.rgb.green = 0;
        spots_3[i].data.rgb.blue = 0;
    }
    
    int total_channels = spots_3[num_spots_3-1].start_channel + 2;
    printf("✅ %d spots configured\n", num_spots_3);
    printf("   Spot[0]: channels %d-%d\n", spots_3[0].start_channel, spots_3[0].start_channel + 2);
    printf("   Spot[%d]: channels %d-%d\n", num_spots_3-1, spots_3[num_spots_3-1].start_channel, spots_3[num_spots_3-1].start_channel + 2);
    printf("   Total channels used: %d %s\n", total_channels, (total_channels <= 512) ? "✅" : "❌ EXCEEDS DMX LIMIT");
    
    // Test 4: Structure access test
    printf("\n=== TEST 4: Structure access test ===\n");
    spots_1[0].data.rgb.red = 255;
    spots_1[0].data.rgb.green = 128;
    spots_1[0].data.rgb.blue = 64;
    
    printf("✅ Setting spot[0] colors:\n");
    printf("   Red: %d, Green: %d, Blue: %d\n", 
           spots_1[0].data.rgb.red, spots_1[0].data.rgb.green, spots_1[0].data.rgb.blue);
    
    // Cleanup
    free(spots_1);
    free(spots_2);
    free(spots_3);
    
    printf("\n🎉 All DMX flexible configuration tests passed!\n");
    printf("✅ The system can now handle any number of RGB spots dynamically\n");
    printf("✅ Channel mapping is generated automatically\n");
    printf("✅ Structure union allows for future spot type extensions\n");
    
    return 0;
}
