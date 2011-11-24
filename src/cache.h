#ifndef CACHE_H
#define CACHE_H

#include "common.h"

typedef struct {
	char *name;
	char *id;
	time_t time;
	unsigned int size;
    unsigned short dirty;
} cached_information;


int flickr_cache_init();
void flickr_cache_kill();

int photoDelete(char *photo_id);
int get_photoset_names(char ***names);
int get_photo_names(const char *photoset, char ***names);
cached_information *photoset_lookup(const char *photoset);
cached_information *photo_lookup(const char *photoset, const char *photo);
void free_cached_info(cached_information *ci);
char *get_photo_uri(const char *photoset, const char *photo);
int set_photo_name(const char *photoset, const char *photo, const char *newname);
int set_photo_size(const char *photoset, const char *photo, unsigned int newsize);
int set_photo_dirty( const char *photoset, const char *photo, unsigned short dirty );
int get_photo_dirty( const char *photoset, const char *photo );
int create_empty_photo( const char *pphotoset, const char *photo );

#endif
