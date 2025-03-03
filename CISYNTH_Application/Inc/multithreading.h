#ifndef MULTITHREADING_H
#define MULTITHREADING_H

#include <pthread.h>

#ifdef _WIN32
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <io.h>
#include <stdint.h>
typedef SSIZE_T ssize_t;
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include "math.h"
#endif

#ifdef _WIN32
#define PACKED_STRUCT __declspec(align(4))
#else
#define PACKED_STRUCT __attribute__((aligned(4)))
#endif

typedef struct DoubleBuffer
{
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    // Buffers for the red channel
    uint8_t *activeBuffer_R;
    uint8_t *processingBuffer_R;
    // Buffers for the green channel
    uint8_t *activeBuffer_G;
    uint8_t *processingBuffer_G;
    // Buffers for the blue channel
    uint8_t *activeBuffer_B;
    uint8_t *processingBuffer_B;
    uint8_t dataReady;
} DoubleBuffer;

//---------------------------------------------------------------------------------------------------------------------------------------------------------
// DEFINES
//---------------------------------------------------------------------------------------------------------------------------------------------------------

#define CIS_400DPI_PIXELS_NB                    (3456)
#define CIS_200DPI_PIXELS_NB                    (1728)

#define CIS_MAX_PIXELS_PER_LANE                 (CIS_400DPI_PIXELS_PER_LANE)

// Number of UDP packets per line
#define UDP_MAX_NB_PACKET_PER_LINE              (12)

#define CIS_400DPI_PIXELS_NB                    (3456)
#define CIS_200DPI_PIXELS_NB                    (1728)

#define CIS_MAX_PIXELS_NB                       (CIS_400DPI_PIXELS_NB)

// Ensure UDP_LINE_FRAGMENT_SIZE is an integer
#if (CIS_MAX_PIXELS_NB % UDP_MAX_NB_PACKET_PER_LINE) != 0
  #error "CIS_MAX_PIXELS_NB must be divisible by UDP_NB_PACKET_PER_LINE."
#endif

// Size of each UDP line fragment (number of pixels per packet)
#define UDP_LINE_FRAGMENT_SIZE                  (CIS_MAX_PIXELS_NB / UDP_MAX_NB_PACKET_PER_LINE)

#define PORT                                    (55151)    //The port on which to listen for incoming data

#define DEFAULT_MULTI                           "192.168.0.1"
#define DEFAULT_PORT                            PORT

//---------------------------------------------------------------------------------------------------------------------------------------------------------
//  COMMON STRUCTURE CIS / MAX
//---------------------------------------------------------------------------------------------------------------------------------------------------------

typedef enum
{
    SW1  = 0,
    SW2,
    SW3,
}buttonIdTypeDef;

typedef enum
{
    SWITCH_RELEASED = 0,
    SWITCH_PRESSED
}buttonStateTypeDef;

typedef enum
{
    LED_1 = 0,
    LED_2,
    LED_3,
}ledIdTypeDef;

typedef enum
{
    STARTUP_INFO_HEADER = 0x11,
    IMAGE_DATA_HEADER = 0x12,
    IMU_DATA_HEADER = 0x13,
    BUTTON_DATA_HEADER= 0x14,
    LED_DATA_HEADER = 0x15,
}CIS_Packet_HeaderTypeDef;

typedef enum
{
    IMAGE_COLOR_R = 0,
    IMAGE_COLOR_G,
    IMAGE_COLOR_B,
}CIS_Packet_ImageColorTypeDef;

typedef enum
{
    CIS_CAL_REQUESTED = 0,
    CIS_CAL_START,
    CIS_CAL_PLACE_ON_WHITE,
    CIS_CAL_PLACE_ON_BLACK,
    CIS_CAL_EXTRACT_INNACTIVE_REF,
    CIS_CAL_EXTRACT_EXTREMUMS,
    CIS_CAL_EXTRACT_OFFSETS,
    CIS_CAL_COMPUTE_GAINS,
    CIS_CAL_END,
}CIS_Calibration_StateTypeDef;

// Packet header structure defining the common header for all packet types// Structure for packets containing startup information like version info
struct PACKED_STRUCT packet_StartupInfo
{
    CIS_Packet_HeaderTypeDef type;         // Identifies the data type
    uint32_t packet_id;                   // Sequence number, useful for ordering packets
    uint8_t version_info[32];             // Information about the version, and other startup details
};

// Structure for image data packets, including metadata for image fragmentation
struct PACKED_STRUCT packet_Image
{
    CIS_Packet_HeaderTypeDef type;                     // Identifies the data type
    uint32_t packet_id;                               // Sequence number, useful for ordering packets
    uint32_t line_id;                                  // Line identifier
    uint8_t fragment_id;                              // Fragment position
    uint8_t total_fragments;                          // Total number of fragments for the complete image
    uint16_t fragment_size;                           // Size of this particular fragment
    uint8_t imageData_R[UDP_LINE_FRAGMENT_SIZE];       // Pointer to the fragmented red image data
    uint8_t imageData_G[UDP_LINE_FRAGMENT_SIZE];      // Pointer to the fragmented green image data
    uint8_t imageData_B[UDP_LINE_FRAGMENT_SIZE];    // Pointer to the fragmented blue image data
};

struct PACKED_STRUCT button_State
{
    buttonStateTypeDef state;
    uint32_t pressed_time;
};

// Structure for packets containing button state information
struct PACKED_STRUCT packet_Button
{
    CIS_Packet_HeaderTypeDef type;                         // Identifies the data type
    uint32_t packet_id;                   // Sequence number, useful for ordering packets
    buttonIdTypeDef button_id;                 // Id of the button
    struct button_State button_state;         // State of the led A
};

struct PACKED_STRUCT led_State
{
    uint16_t brightness_1;
    uint16_t time_1;
    uint16_t glide_1;
    uint16_t brightness_2;
    uint16_t time_2;
    uint16_t glide_2;
    uint32_t blink_count;
};

// Structure for packets containing leds state
struct PACKED_STRUCT packet_Leds
{
    CIS_Packet_HeaderTypeDef type;                         // Identifies the data type
    uint32_t packet_id;                   // Sequence number, useful for ordering packets
    ledIdTypeDef led_id;                 // Id of the led
    struct led_State led_state;         // State of the selected led
};

// Structure for packets containing sensor data (accelerometer and gyroscope)
struct PACKED_STRUCT packet_IMU
{
    CIS_Packet_HeaderTypeDef type;                         // Identifies the data type
    uint32_t packet_id;                   // Sequence number, useful for ordering packets
    float_t acc[3];                   // Accelerometer data: x, y, and z axis
    float_t gyro[3];                  // Gyroscope data: x, y, and z axis
    float_t integrated_acc[3];        // Accelerometer data: x, y, and z axis
    float_t integrated_gyro[3];       // Gyroscope data: x, y, and z axis
};

struct PACKED_STRUCT cisRgbBuffers
{
    uint8_t R[CIS_MAX_PIXELS_NB];
    uint8_t G[CIS_MAX_PIXELS_NB];
    uint8_t B[CIS_MAX_PIXELS_NB];
};

//---------------------------------------------------------------------------------------------------------------------------------------------------------
// PROTOTYPES
//---------------------------------------------------------------------------------------------------------------------------------------------------------

void initDoubleBuffer(DoubleBuffer *db);
void swapBuffers(DoubleBuffer *db);
void *udpThread(void *arg);
void *imageProcessingThread(void *arg);
void *dmxSendingThread(void *arg);
void *audioProcessingThread(void *arg);

#endif
