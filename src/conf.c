#include <string.h>
#include <unistd.h>

#include "conf.h"

#define FAIL	-1
#define SUCCESS	0

static char conf_file_name[15] = ".flickcurl.conf";

char *get_conf_path() {
        char *conf_path;
	char *home;

	home = getenv("HOME");
	if(!home)
		return 0;
	conf_path = (char *)malloc(strlen(home) + strlen(conf_file_name) + 2);
	if(!conf_path)
		return 0;
	strcpy(conf_path, home);
	strcat(conf_path, "/");
	strcat(conf_path, conf_file_name);

	return conf_path;
}

int create_conf(char *conf_path) {
	FILE *fp;

	if(!conf_path)
		return FAIL;
	fp = fopen(conf_path, "w");
	if(!fp)
		return FAIL;

	fputs("[flickr]\napi_key=2e66493ec959256a79e4e5a3da7df729\nsecret=c1b99d47790391c3"
		,fp);
	
	fclose(fp);
	return SUCCESS;
}

int check_conf_file() {
	char *conf_path;
	struct stat buf;

	conf_path = get_conf_path();
	if(!conf_path)
		return FAIL;
	if(access(conf_path, F_OK) < 0)
		if(create_conf(conf_path))
			return FAIL;

	free(conf_path);
	return SUCCESS;
}
