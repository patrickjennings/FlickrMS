#ifndef CONF_H
#define	CONF_H

#include "common.h"

char *get_conf_path();
int create_conf(char *conf_path);
int check_conf_file();

#endif
