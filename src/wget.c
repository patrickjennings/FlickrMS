#include <stdio.h>
#include <string.h>
#include <curl/curl.h>

#include "wget.h"

static CURL *curl_global;

int wget_init() {
    if(curl_global_init(CURL_GLOBAL_ALL))
        return FAIL;

    if(!(curl_global = curl_easy_init()))
        return FAIL;

    return SUCCESS;
}

void wget_destroy() {
    curl_easy_cleanup(curl_global);
    curl_global_cleanup();
}

int wget(const char *in, const char *out) {
    CURL *curl;
    CURLcode res;
    FILE *fp;

    if(!(curl = curl_easy_duphandle(curl_global)))
        return FAIL;

    if(!(fp = fopen(out, "wb")))    // Open in binary
        return FAIL;

    // Set the curl easy options
    curl_easy_setopt(curl, CURLOPT_URL, in);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);

    res = curl_easy_perform(curl);  // Perform the download and write

    curl_easy_cleanup(curl);
    fclose(fp);
    return res;
}

