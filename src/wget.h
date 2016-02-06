#ifndef WGET_H
#define WGET_H

#include "common.h"

int wget_init();
void wget_destroy();
int wget(const char *in, const char *out);

#endif
