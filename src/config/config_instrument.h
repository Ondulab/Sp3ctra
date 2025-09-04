/* config_instrument.h */

#ifndef __CONFIG_INSTRUMENT_H__
#define __CONFIG_INSTRUMENT_H__

/**************************************************************************************
 * CIS Definitions
 **************************************************************************************/
#define CIS_400DPI_PIXELS_NB         (3456)
#define CIS_200DPI_PIXELS_NB         (1728)

#define UDP_MAX_NB_PACKET_PER_LINE   (12)
#define CIS_MAX_PIXELS_NB            (CIS_400DPI_PIXELS_NB)

// Ensure UDP_LINE_FRAGMENT_SIZE is an integer
#if (CIS_MAX_PIXELS_NB % UDP_MAX_NB_PACKET_PER_LINE) != 0
#error "CIS_MAX_PIXELS_NB must be divisible by UDP_MAX_NB_PACKET_PER_LINE."
#endif

// Size of each UDP line fragment (number of pixels per packet)
#define UDP_LINE_FRAGMENT_SIZE       (CIS_MAX_PIXELS_NB / UDP_MAX_NB_PACKET_PER_LINE)

#define PORT                         (55151)                // Port for incoming data
#define DEFAULT_MULTI                "192.168.0.1"
#define DEFAULT_PORT                 PORT

#endif // __CONFIG_INSTRUMENT_H__
