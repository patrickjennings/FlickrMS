#ifndef CACHE_H
#define CACHE_H

typedef struct {
	char *name;
	char *id;
	time_t time;
	unsigned int size;
} cached_information;


int flickr_cache_init();
void flickr_cache_kill();

int photoDelete(char *photo_id);
int get_photoset_names(char ***names);
int get_photo_names(const char *photoset, char ***names);
const cached_information *photoset_lookup(const char *photoset);
const cached_information *photo_lookup(const char *photoset, const char *photo);
const char *get_photo_uri(const char *photoset, const char *photo);
int set_photo_name(const char *photoset, const char *photo, const char *newname);

#endif
