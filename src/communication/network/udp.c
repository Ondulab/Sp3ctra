//
//  udp.c
//  SSS_Viewer
//
//  Created by Zhonx on 16/12/2023.
//
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <errno.h>
#include <fcntl.h>
#include <termios.h>

#include "error.h"
#include "logger.h"
#include "udp.h"
#include "../synthesis/luxwave/synth_luxwave.h"
#include "../../config/config_loader.h"

// #define CALIBRATION
// #define SSS_MOD_MODE

// ip addr
// sudo ip link set enx00e04c781b25 up
// sudo ip addr add 192.168.0.50/24 dev enx00e04c781b25
// memo on linux terminal : sudo nc -u -l 55151

/**
 * Check if an IP address is multicast (224.0.0.0 - 239.255.255.255)
 * @param ip_str IP address string (e.g., "239.100.100.100")
 * @return 1 if multicast, 0 otherwise
 */
static int is_multicast_address(const char* ip_str) {
  struct in_addr addr;
  if (inet_pton(AF_INET, ip_str, &addr) != 1) {
    return 0;  // Invalid address
  }
  
  uint32_t ip = ntohl(addr.s_addr);
  // Multicast range: 224.0.0.0 (0xE0000000) to 239.255.255.255 (0xEFFFFFFF)
  return (ip >= 0xE0000000 && ip <= 0xEFFFFFFF);
}

int udp_Init(struct sockaddr_in *si_other, struct sockaddr_in *si_me) {
  (void)si_other; // Mark si_other as unused
  int s;
  
  // Get network configuration from global config
  extern sp3ctra_config_t g_sp3ctra_config;
  const char* udp_address = g_sp3ctra_config.udp_address;
  int udp_port = g_sp3ctra_config.udp_port;
  const char* multicast_interface = g_sp3ctra_config.multicast_interface;
  
  // Use defaults if not configured
  if (udp_address[0] == '\0') {
    udp_address = "239.100.100.100";  // Default multicast address
  }
  if (udp_port == 0) {
    udp_port = PORT;  // Use PORT constant as fallback
  }

  // Creating UDP socket
  if ((s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
    die("socket");
  }

  log_info("UDP", "Creating UDP socket for %s:%d", udp_address, udp_port);

  // Enable socket address reuse to prevent "Address already in use" errors
  int reuse = 1;
  if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) == -1) {
    log_warning("UDP", "Failed to set SO_REUSEADDR: %s", strerror(errno));
    // Continue anyway, this is not critical
  }

  // 🔧 CRITICAL FIX: SO_REUSEPORT DISABLED
  // SO_REUSEPORT causes packets to be lost after socket restart because the kernel
  // keeps routing packets to the old (closed) socket for an indeterminate time.
  // SO_REUSEADDR alone is sufficient to prevent "Address already in use" errors
  // and allows clean socket restart without packet loss.
  //
  // NOTE: This means only ONE process can listen on port 55151 at a time.
  // If you need multiple processes, use different ports or multicast with IGMP.
#ifdef SO_REUSEPORT_DISABLED_DUE_TO_RESTART_BUG
  if (setsockopt(s, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse)) == -1) {
    log_warning("UDP", "Failed to set SO_REUSEPORT: %s", strerror(errno));
  } else {
    log_info("UDP", "SO_REUSEPORT enabled");
  }
#else
  log_info("UDP", "SO_REUSEPORT disabled (prevents packet loss on restart)");
#endif

  // Set socket timeout so recvfrom() can be interrupted for clean shutdown
  struct timeval timeout;
  timeout.tv_sec = 0;
  timeout.tv_usec = 100000; // 100ms timeout
  if (setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == -1) {
    log_warning("UDP", "Failed to set SO_RCVTIMEO: %s", strerror(errno));
    // Continue anyway, this is not critical for operation
  }

  // Initialisation de la structure
  memset(si_me, 0, sizeof(*si_me));
  si_me->sin_family = AF_INET;
  si_me->sin_port = htons(udp_port);
  si_me->sin_addr.s_addr = htonl(INADDR_ANY);

  // Liaison de la socket au port
  if (bind(s, (struct sockaddr *)si_me, sizeof(*si_me)) == -1) {
    log_error("UDP", "Failed to bind UDP socket to port %d", udp_port);
    log_error("UDP", "This usually means the port is already in use by another process");
    log_error("UDP", "Try waiting a few seconds or check if another instance is running");
    log_error("UDP", "bind error: %s", strerror(errno));
    close(s);
    return -1;
  }

  log_info("UDP", "Socket bound to port %d", udp_port);
  
  // Check if address is multicast and join the group
  if (is_multicast_address(udp_address)) {
    struct ip_mreq mreq;
    memset(&mreq, 0, sizeof(mreq));
    
    // Set multicast group address
    if (inet_pton(AF_INET, udp_address, &mreq.imr_multiaddr) != 1) {
      log_error("UDP", "Invalid multicast address: %s", udp_address);
      close(s);
      return -1;
    }
    
    // Set interface address (INADDR_ANY or specific interface)
    if (multicast_interface != NULL && multicast_interface[0] != '\0') {
      // Use specific interface
      if (inet_pton(AF_INET, multicast_interface, &mreq.imr_interface) != 1) {
        log_error("UDP", "Invalid multicast interface address: %s", multicast_interface);
        close(s);
        return -1;
      }
      log_info("UDP", "Using multicast interface: %s", multicast_interface);
    } else {
      // Use INADDR_ANY (let system choose)
      mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    }
    
    // Join multicast group
    if (setsockopt(s, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
      log_error("UDP", "Failed to join multicast group %s: %s", udp_address, strerror(errno));
      close(s);
      return -1;
    }
    
    // Enable multicast loopback (receive own packets)
    unsigned char loop = 1;
    if (setsockopt(s, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop)) < 0) {
      log_warning("UDP", "Failed to set IP_MULTICAST_LOOP: %s", strerror(errno));
      // Continue anyway, this is not critical
    }
    
    log_info("UDP", "Successfully joined multicast group %s", udp_address);
  } else {
    log_info("UDP", "Unicast mode - listening on %s:%d", udp_address, udp_port);
  }

  // Retourne le descripteur de la socket
  return s;
}

void udp_cleanup(int socket_fd) {
  if (socket_fd >= 0) {
    log_info("UDP", "Closing UDP socket");
    
    // ✅ FIX: Force immediate close with SO_LINGER to avoid TIME_WAIT state
    // This prevents SO_REUSEPORT from routing packets to the old socket
    struct linger so_linger;
    so_linger.l_onoff = 1;   // Enable linger
    so_linger.l_linger = 0;  // Timeout = 0 (immediate close, send RST)
    
    if (setsockopt(socket_fd, SOL_SOCKET, SO_LINGER, &so_linger, sizeof(so_linger)) < 0) {
      log_warning("UDP", "Failed to set SO_LINGER: %s", strerror(errno));
    }
    
    // Shutdown socket FIRST to unblock any pending recvfrom() calls
    // This allows the UDP thread to terminate cleanly
    shutdown(socket_fd, SHUT_RDWR);
    
    // Close the socket (will be immediate due to SO_LINGER)
    close(socket_fd);
    
    log_info("UDP", "Socket closed (immediate via SO_LINGER)");
  }
}
