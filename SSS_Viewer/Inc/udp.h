//
//  udp.h
//  SSS_Viewer
//
//  Created by Zhonx on 16/12/2023.
//

#ifndef udp_h
#define udp_h

#include <stdio.h>

int udp_Init(struct sockaddr_in *si_other, struct sockaddr_in *si_me);

#endif /* udp_h */
