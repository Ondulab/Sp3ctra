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

#include <errno.h>
#include <fcntl.h>
#include <termios.h>

#include "error.h"
#include "udp.h"

// #define CALIBRATION
// #define SSS_MOD_MODE

// ip addr
// sudo ip link set enx00e04c781b25 up
// sudo ip addr add 192.168.0.50/24 dev enx00e04c781b25
// memo on linux terminal : sudo nc -u -l 55151

int udp_Init(struct sockaddr_in *si_other, struct sockaddr_in *si_me) {
  (void)si_other; // Mark si_other as unused
  int s;

  // Création d'une socket UDP
  if ((s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
    die("socket");
  }

  printf("CREATE UDP SOCKET\n");

  // Enable socket address reuse to prevent "Address already in use" errors
  int reuse = 1;
  if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) == -1) {
    perror("Warning: Failed to set SO_REUSEADDR");
    // Continue anyway, this is not critical
  }

  // Initialisation de la structure
  memset(si_me, 0, sizeof(*si_me));
  si_me->sin_family = AF_INET;
  si_me->sin_port = htons(PORT);
  si_me->sin_addr.s_addr = htonl(INADDR_ANY);

  // Liaison de la socket au port
  if (bind(s, (struct sockaddr *)si_me, sizeof(*si_me)) == -1) {
    printf("ERROR: Failed to bind UDP socket to port %d\n", PORT);
    printf(
        "This usually means the port is already in use by another process.\n");
    printf(
        "Try waiting a few seconds or check if another instance is running.\n");
    perror("bind");
    close(s);
    return -1;
  }

  printf("BIND SOCKET to port %d\n", PORT);

  // Retourne le descripteur de la socket
  return s;
}

void udp_cleanup(int socket_fd) {
  if (socket_fd >= 0) {
    printf("CLOSING UDP SOCKET\n");
    close(socket_fd);
  }
}
