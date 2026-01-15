// Stubs for removed functionality (display, EQ, SFML) - VST migration
#include <stddef.h>

// Global variable stub
volatile int keepRunning = 1;

// MIDI callback stubs for removed EQ
void midi_cb_audio_eq_low_gain(const void *param, void *user_data) { (void)param; (void)user_data; }
void midi_cb_audio_eq_mid_gain(const void *param, void *user_data) { (void)param; (void)user_data; }
void midi_cb_audio_eq_mid_freq(const void *param, void *user_data) { (void)param; (void)user_data; }
void midi_cb_audio_eq_high_gain(const void *param, void *user_data) { (void)param; (void)user_data; }

// MIDI callback stubs for removed display
void midi_cb_display_line_thickness(const void *param, void *user_data) { (void)param; (void)user_data; }
void midi_cb_display_initial_line_position(const void *param, void *user_data) { (void)param; (void)user_data; }
void midi_cb_display_udp_scroll_speed(const void *param, void *user_data) { (void)param; (void)user_data; }
void midi_cb_display_orientation(const void *param, void *user_data) { (void)param; (void)user_data; }

// SFML stubs
void* sfCircleShape_create(void) { return NULL; }
void sfCircleShape_destroy(void* shape) { (void)shape; }
void sfCircleShape_setRadius(void* shape, float radius) { (void)shape; (void)radius; }
void sfCircleShape_setFillColor(void* shape, void* color) { (void)shape; (void)color; }
void sfCircleShape_setPosition(void* shape, void* position) { (void)shape; (void)position; }
void sfCircleShape_setOrigin(void* shape, void* origin) { (void)shape; (void)origin; }
void sfCircleShape_setOutlineColor(void* shape, void* color) { (void)shape; (void)color; }
void sfCircleShape_setOutlineThickness(void* shape, float thickness) { (void)shape; (void)thickness; }
void sfRenderWindow_drawCircleShape(void* window, void* shape, void* states) { (void)window; (void)shape; (void)states; }
void sfRenderWindow_drawPrimitives(void* window, void* vertices, unsigned int vertexCount, int type, void* states) { 
    (void)window; (void)vertices; (void)vertexCount; (void)type; (void)states; 
}
typedef struct { unsigned int x, y; } sfVector2u;
sfVector2u sfRenderWindow_getSize(void* window) { 
    (void)window; 
    sfVector2u size = {800, 600}; 
    return size; 
}

// SFML color stub
struct { unsigned char r, g, b, a; } sfRed = {255, 0, 0, 255};
