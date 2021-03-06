#ifndef CACHE_H
#define CACHE_H

#include "common.h"

#define PHOTO_SIZE_UNSET 0

typedef struct {
    char *name;
    char *id;
    char *uri;
    time_t time;
    unsigned int size;
    unsigned short dirty;
} cached_information;


int flickr_cache_init();
void flickr_cache_kill();

int photoDelete(char *photo_id);
unsigned int get_photoset_names(char ***names);
unsigned int get_photo_names(const char *photoset, char ***names);
cached_information *photoset_lookup(const char *photoset);
cached_information *photo_lookup(const char *photoset, const char *photo);
void free_cached_info(cached_information *ci);
char *get_photo_uri(const char *photoset, const char *photo);
int set_photo_name(const char *photoset, const char *photo, const char *newname);
int set_photoset_name(const char *photoset, const char *newname);
int set_photo_size(const char *photoset, const char *photo, unsigned int newsize);
int set_photo_dirty(const char *photoset, const char *photo, unsigned short dirty);
int get_photo_dirty(const char *photoset, const char *photo);
int create_empty_photoset(const char *photoset);
int create_empty_photo(const char *pphotoset, const char *photo);
int upload_photo(const char *photoset, const char *photo, const char *path);
int set_photo_photoset(const char *photoset, const char *photo, const char *new_photoset);
int remove_photo_from_cache(const char *photoset, const char *photo);

#endif
