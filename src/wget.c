#include <stdio.h>
#include <string.h>
#include <curl/curl.h>

#include "wget.h"

size_t write(void *ptr, size_t size, size_t nmemb, FILE *stream) {
	return fwrite(ptr, size, nmemb, stream);
}

int wget(const char *in, const char *out) {
	CURL *curl;
	CURLcode res;
	FILE *fp;

	if(!(curl = curl_easy_init()))
		return FAIL;

	if(!(fp = fopen(out, "wb")))	// Open in binary
		return FAIL;

	// Set the curl easy options
	curl_easy_setopt(curl, CURLOPT_URL, in);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);

	res = curl_easy_perform(curl);	// Perform the download and write

	curl_easy_cleanup(curl);
	fclose(fp);
	return res;
}

