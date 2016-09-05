#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <unistd.h>
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
    //curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

    res = curl_easy_perform(curl);  // Perform the download and write

    curl_easy_cleanup(curl);
    fsync(fileno(fp));
    fclose(fp);
    return res;
}

static size_t throw_away(void *ptr, size_t size, size_t nmemb, void *data)
{
    (void)ptr;
    (void)data;
    return (size_t)(size * nmemb);
}

/* Perform a HEAD request to the url to get the Content Length from
 * the headers. Does not get the body.
 */
int get_url_content_length(const char *url) {
    CURL *curl;
    CURLcode res;
    double content_length;

    if(!(curl = curl_easy_duphandle(curl_global)))
        return FAIL;

    // Set the curl easy options
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1); // Use HEADER request
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, throw_away);
    curl_easy_setopt(curl, CURLOPT_HEADER, 0L);
    //curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

    res = curl_easy_perform(curl);

    if(res) {
        curl_easy_cleanup(curl);
        return FAIL;
    }

    res = curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD, &content_length);
    curl_easy_cleanup(curl);

    return (res) ? FAIL : (int)round(content_length);
}
