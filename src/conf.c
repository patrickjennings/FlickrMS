#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>

#include "conf.h"


#define KEY	"2e66493ec959256a79e4e5a3da7df729"
#define SECRET	"c1b99d47790391c3"
#define	URL	"http://www.flickr.com/services/auth/?mobile=1&api_key=2e66493ec959256a79e4e5a3da7df729&perms=delete&api_sig=6417abca6880d676c010600e8c65a045"


static char conf_file_name[] = ".flickcurl.conf";


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
	#define FROB_SIZE 12
	char FROB[FROB_SIZE];
	char command[25] = "flickcurl -a ";
	FILE *fp;

	if(!conf_path)
		return FAIL;
	fp = fopen(conf_path, "w");
	if(!fp)
		return FAIL;

	fputs("[flickr]\napi_key=" KEY "\nsecret=" SECRET "\n", fp);
	fclose(fp);

	fputs("Go to " URL "\nPaste the application code here:\n", stdout);

	if(!fgets(FROB, FROB_SIZE, stdin))
		return FAIL;

	strcat(command, FROB);
	printf("%s\n", command);
	system(command);

	return SUCCESS;
}

int check_conf_file() {
	char *conf_path;

	conf_path = get_conf_path();
	if(!conf_path)
		return FAIL;
	if(access(conf_path, F_OK) < 0)	/* If conf file doesn't exist, create and ask for new frob */
		if(create_conf(conf_path))
			return FAIL;

	free(conf_path);
	return SUCCESS;
}

