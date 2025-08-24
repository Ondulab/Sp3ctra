//
//  error.c
//  SSS_Viewer
//
//  Created by Zhonx on 17/12/2023.
//
#include "stdlib.h"

#include "error.h"

// Function to exit with an error message
void die(const char *s) {
  perror(s);
  exit(1);
}
