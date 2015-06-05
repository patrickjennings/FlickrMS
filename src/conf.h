#ifndef CONF_H
#define CONF_H

#include "common.h"

char *get_conf_path();
int create_conf(char *conf_path, flickcurl *fc);
int check_conf_file(char *conf_path, flickcurl *fc);

#endif
