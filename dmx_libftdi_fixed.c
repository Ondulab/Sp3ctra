#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ftdi.h>
#include <string.h>
#include <signal.h>

volatile int running = 1;

void signal_handler(int sig) {
    printf("\n🛑 Arrêt demandé...\n");
    running = 0;
}

int send_dmx_break_proper(struct ftdi_context *ftdi) {
    int ret;
    
    // Method 1: Use proper FTDI break functions
    ret = ftdi_set_line_property2(ftdi, BITS_8, STOP_BIT_2, NONE, BREAK_ON);
    if (ret < 0) {
        // Fallback Method 2: Manual break via bitbang mode
        ret = ftdi_set_bitmode(ftdi, 0x01, BITMODE_BITBANG);
        if (ret < 0) return ret;
        
        ret = ftdi_write_data(ftdi, (unsigned char*)"\x00", 1); // Force line low
        if (ret < 0) return ret;
        
        usleep(176); // DMX break minimum 176µs
        
        ret = ftdi_write_data(ftdi, (unsigned char*)"\x01", 1); // Force line high  
        if (ret < 0) return ret;
        
        usleep(12); // Mark after break 12µs
        
        // Return to normal serial mode
        ret = ftdi_set_bitmode(ftdi, 0x00, BITMODE_RESET);
        return ret;
    }
    
    // Method 1 worked - complete the proper break sequence
    usleep(176); // DMX break minimum 176µs
    
    ret = ftdi_set_line_property2(ftdi, BITS_8, STOP_BIT_2, NONE, BREAK_OFF);
    if (ret < 0) return ret;
    
    usleep(12); // Mark after break 12µs
    
    return 0;
}

int main() {
    struct ftdi_context *ftdi;
    int ret;
    unsigned char dmx_frame[513]; // DMX frame: start code + 512 channels
    
    // Gestionnaire pour Ctrl+C
    signal(SIGINT, signal_handler);
    
    printf("🔧 DMX libftdi FIXED version - proper DMX break for Pi\n");
    
    // Initialize libftdi
    ftdi = ftdi_new();
    if (ftdi == NULL) {
        fprintf(stderr, "❌ ftdi_new failed\n");
        return -1;
    }
    
    // Open FTDI device
    ret = ftdi_usb_open(ftdi, 0x0403, 0x6001);  // Standard FTDI VID/PID
    if (ret < 0) {
        fprintf(stderr, "❌ Unable to open FTDI device: %s\n", ftdi_get_error_string(ftdi));
        ftdi_free(ftdi);
        return -1;
    }
    
    printf("✅ FTDI device opened successfully\n");
    
    // Configure for DMX: 250000 bps, 8N2
    ret = ftdi_set_baudrate(ftdi, 250000);
    if (ret < 0) {
        fprintf(stderr, "❌ Set baud rate failed: %s\n", ftdi_get_error_string(ftdi));
    } else {
        printf("✅ Baud rate set to 250000\n");
    }
    
    ret = ftdi_set_line_property(ftdi, BITS_8, STOP_BIT_2, NONE);
    if (ret < 0) {
        fprintf(stderr, "❌ Set line properties failed: %s\n", ftdi_get_error_string(ftdi));
    } else {
        printf("✅ Line properties set (8N2)\n");
    }
    
    // Reset any previous bitmode settings
    ret = ftdi_set_bitmode(ftdi, 0x00, BITMODE_RESET);
    if (ret < 0) {
        printf("⚠️  Bitmode reset warning: %s\n", ftdi_get_error_string(ftdi));
    }
    
    // Prepare DMX frame
    memset(dmx_frame, 0, sizeof(dmx_frame));
    dmx_frame[0] = 0;  // DMX start code
    
    // Set first 18 channels RGB (6 spots x 3 channels) - WHITE
    for (int i = 0; i < 18; i += 3) {
        dmx_frame[1 + i] = 255;     // Red
        dmx_frame[2 + i] = 255;     // Green  
        dmx_frame[3 + i] = 255;     // Blue
    }
    
    printf("🔄 Starting FIXED DMX stream with proper break (Ctrl+C to stop)...\n");
    printf("📡 Using 176µs break + 12µs mark (DMX-512 standard)\n");
    printf("📡 Sending WHITE to 6 spots (18 channels) at 44 FPS\n");
    
    int frame_count = 0;
    int break_method = 1; // Start with method 1
    
    while(running) {
        // Send proper DMX break
        ret = send_dmx_break_proper(ftdi);
        if (ret < 0) {
            fprintf(stderr, "❌ DMX break failed at frame %d: %s\n", frame_count, ftdi_get_error_string(ftdi));
            break;
        }
        
        // Send DMX data frame
        ret = ftdi_write_data(ftdi, dmx_frame, 55); // Start code + 54 channels
        if (ret < 0) {
            fprintf(stderr, "❌ Frame %d data write failed: %s\n", frame_count, ftdi_get_error_string(ftdi));
            break;
        }
        
        frame_count++;
        if (frame_count % 100 == 0) {
            printf("✅ %d DMX frames sent with FIXED break (%.1f seconds)\n", frame_count, frame_count/44.0);
        }
        
        usleep(22727); // ~44 FPS DMX standard (22.727ms between frames)
    }
    
    // Cleanup
    ftdi_usb_close(ftdi);
    ftdi_free(ftdi);
    printf("🎉 FIXED DMX stream stopped after %d frames (%.1f seconds)\n", frame_count, frame_count/44.0);
    
    return 0;
}
