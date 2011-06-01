#ifndef FILE_MANAGER_H
#define FILE_MANAGER_H

#include "common.h"

int file_manager_init();
void file_manager_kill();
void add_file(const char *path);

#endif
