#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <flickcurl.h>

#include "conf.h"


#define KEY "2e66493ec959256a79e4e5a3da7df729"
#define SECRET  "c1b99d47790391c3"


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

int create_conf(char *conf_path, flickcurl *fc) {
    #define VERIFIER_SIZE 12
    char verifier[VERIFIER_SIZE];

    flickcurl_set_oauth_client_key(fc, KEY);
    flickcurl_set_oauth_client_secret(fc, SECRET);

    if(flickcurl_oauth_create_request_token(fc, NULL))
        return FAIL;

    const char *uri = flickcurl_oauth_get_authorize_uri(fc);

    printf( "Go to: %s\n", uri );
    printf( "Enter the 9-digit Verifier: " );

    if(!fgets(verifier, VERIFIER_SIZE, stdin))
        return FAIL;

    if(flickcurl_oauth_create_access_token(fc, verifier))
        return FAIL;

    if(flickcurl_config_write_ini(fc, conf_path, "flickr"))
        return FAIL;

    return SUCCESS;
}

int check_conf_file(char *conf_path, flickcurl *fc) {
    if(access(conf_path, F_OK) < 0) /* If conf file doesn't exist, create */
        if(create_conf(conf_path, fc))
            return FAIL;

    return SUCCESS;
}

