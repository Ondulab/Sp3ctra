//
//  error.c
//  SSS_Viewer
//
//  Created by Zhonx on 17/12/2023.
//
#include "stdlib.h"

#include "error.h"

// Fonction pour quitter avec un message d'erreur
void die(const char *s)
{
    perror(s);
    exit(1);
}
