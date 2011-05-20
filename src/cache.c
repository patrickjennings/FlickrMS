/**
 * Patrick Jennings
 *
 * TODO: Enhance (performance-wise) thread-safety mechanisms
 *	* Release lock while communicating with Flickr.
 * TODO: Enhance aging/invalidating the cache
 *	* Place cache cleaning in seperate thread. Would require a r/w lock.
**/

#include <flickcurl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <glib.h>
#include <pthread.h>

#include "cache.h"


#define DEFAULT_CACHE_TIMEOUT	30
#define BUFFER_SIZE		1024
#define CONF_FILE_NAME		".flickcurl.conf"

#define GET_PHOTO_SIZE		's'

#define CACHE_UNSET		0
#define CACHE_SET		1

#define SUCCESS			0
#define FAIL			-1


typedef struct {
	cached_information ci;
	unsigned short set;
	GHashTable *photo_ht;
} cached_photoset;

typedef struct {
	cached_information ci;
	char *uri;
} cached_photo;


static GHashTable *photoset_ht;			/* The photoset cache */
static cached_photoset *cached_nophotoset;	/* A pointer to the cached information of photos NOT in a photoset */
static pthread_mutex_t cache_lock;		/* To make thread safe */
static time_t last_cleaned;			/* To age/invalidate the cache */

static flickcurl *fc;


/**
 * ===Flickr API Methods===
**/

int photoDelete(char *photo_id) {
	return flickcurl_photos_delete(fc, photo_id);
}

void photoUpload(char *path, char *descript, char *title, char *tags) {
	flickcurl_upload_status* status;
	flickcurl_upload_params params;

	memset(&params, '\0', sizeof(flickcurl_upload_params));
	params.safety_level = 1;	/* default safety */
	params.content_type = 1;	/* default photo */
	params.photo_file = path;
	params.description = descript;
	params.title = title;
	params.tags = tags;

	status = flickcurl_photos_upload_params(fc, &params);
	if(status)
		flickcurl_free_upload_status(status);
}

/*
 * Reads the users configuration file
 * located in ~/.flickcurl.conf
*/
static void read_conf(void* userdata, const char* key, const char* value) {
	flickcurl *fc = (flickcurl *)userdata;
	if(!strcmp(key, "api_key"))
		flickcurl_set_api_key(fc, value);
	else if(!strcmp(key, "secret"))
		flickcurl_set_shared_secret(fc, value);
	else if(!strcmp(key, "auth_token"))
		flickcurl_set_auth_token(fc, value);
}

/*
 * Initialize the flickcurl connection
*/
static int flickr_init() {
	char* home;
	char config_path[BUFFER_SIZE];
	char *login;

	flickcurl_init();
	fc = flickcurl_new();
        
	home = getenv("HOME");
	/* Buffer overflow protection. no home directory paths greater than so many chars. */
	if(home && (strlen(home) < (BUFFER_SIZE - 20)))
		sprintf(config_path, "%s/%s", home, CONF_FILE_NAME);
	else
		return FAIL;

	/* Read from the config file: ~/.flickcurl.conf */
	if(read_ini_config(config_path, "flickr", fc, read_conf))
		return FAIL;

	login = flickcurl_test_login(fc);

	if(!login)
		return FAIL;

	free(login);
	return SUCCESS;
}

static void flickr_kill() {
	flickcurl_free(fc);
	flickcurl_finish();
}


/**
 * ===Cache Methods===
**/

/* Creates a new cached_photoset using the pointer and name/id provided */
static int new_cached_photoset(cached_photoset **cps, char *name, char *id) {
	unsigned int i;
	cached_information *ci;

	*cps = (cached_photoset *)malloc(sizeof(cached_photoset));
	if(!cps)
		return -1;

	ci = &((*cps)->ci);
	ci->name = strdup(name);
	ci->id = strdup(id);
	ci->time = 0;
	ci->size = 0;
	(*cps)->set = CACHE_UNSET;
	(*cps)->photo_ht = g_hash_table_new(g_str_hash, g_str_equal);

	/* Replace backslashes with spaces */
	for(i = 0; i < strlen(ci->name); i++) {
		if(ci->name[i] == '/')
			ci->name[i] = ' ';
	}
	return 0;
}

/* All of our keys and values will be dynamic so we will want to free them. */
static gboolean free_photo_ht(gpointer key, gpointer value, gpointer user_data) {
	(void)user_data;
	free(key);
	free(value);
	return TRUE;
}

/* All of our keys and values will be dynamic so we will want to free them. */
static gboolean free_photoset_ht(gpointer key, gpointer value, gpointer user_data) {
	(void)user_data;
	free(key);
	g_hash_table_foreach_remove(((cached_photoset *)value)->photo_ht, free_photo_ht, NULL);
	free(value);
	return TRUE;
}

/*
 * Checks whether the cache should be cleaned. Time can be changed in
 * DEFAULT_CACHE_TIMEOUT define.
*/
static int check_cache() {
	flickcurl_photoset **fps;
	cached_photoset *cps;
	int i;

	if((time(NULL) - last_cleaned) < DEFAULT_CACHE_TIMEOUT)
		return SUCCESS;

	/* Wipe entire cache */
	g_hash_table_foreach_remove(photoset_ht, free_photoset_ht, NULL);

	/* Create an empty photoset container for the photos not in a photoset */
	if(new_cached_photoset(&cps, "", ""))
		return FAIL;

	cached_nophotoset = cps;
	g_hash_table_insert(photoset_ht, g_strdup(""), cps);

	if(!(fps = flickcurl_photosets_getList(fc, NULL)))
		return FAIL;

	/* Add the photosets to the cache */
	for(i = 0; fps[i]; i++) {
		if(new_cached_photoset(&cps, fps[i]->title, fps[i]->id))
			return FAIL;
		g_hash_table_insert(photoset_ht, g_strdup(cps->ci.name), cps);
	}
	flickcurl_free_photosets(fps);

	last_cleaned = time(NULL);

	return SUCCESS;
}

static int check_photoset_cache(cached_photoset *cps) {
	flickcurl_photo **fp;
	int j;

	if(!cps)
		return FAIL;
	if(!(cps->photo_ht))
		return FAIL;
	if(cps->set)
		return SUCCESS;

	if(check_cache())
		return FAIL;

	/* Are we searching for photos in a photoset or not? */
	if(cps == cached_nophotoset) {		/* Get photos NOT in a photoset */
		if(!(fp = flickcurl_photos_getNotInSet(fc, 0, 0, NULL, NULL, 0, "date_taken", -1, -1)))
			return FAIL;
	}
	else {					/* Add the photos of the photoset into the cache */
		if(!(fp = flickcurl_photosets_getPhotos(fc, cps->ci.id, "date_taken", 0, -1, -1)))
			return FAIL;
	}

	/* Add photos to photoset cache */
	for(j = 0; fp[j]; j++) {
		cached_photo *cp;
		struct tm tm;
		memset(&tm, 0, sizeof(struct tm));

		if(!(cp = (cached_photo *)malloc(sizeof(cached_photo))))
			return FAIL;
		cp->uri = strdup(flickcurl_photo_as_source_uri(fp[j], GET_PHOTO_SIZE));
		cp->ci.name = strdup(fp[j]->fields[PHOTO_FIELD_title].string);
		cp->ci.id = strdup(fp[j]->id);
		cp->ci.size = 1024;	/* Trick so that file managers do not think file is empty... */

		sscanf(fp[j]->fields[PHOTO_FIELD_dates_taken].string, "%d-%d-%d %d:%d:%d",
		  &(tm.tm_year), &(tm.tm_mon), &(tm.tm_mday), &(tm.tm_hour), &(tm.tm_min), &(tm.tm_sec));
		tm.tm_year = tm.tm_year - 1900;		/* Years since 1990 */
		tm.tm_mon--;				/* Programmers start with 0... */
		tm.tm_sec--;
		tm.tm_min--;
		tm.tm_hour--;
		cp->ci.time = mktime(&tm);
		
		/* Can't place empty or duplicate names into the hash table. If this is the case, use the photo id instead. */
		if(cp->ci.name[0] == '\0' || g_hash_table_lookup(cps->photo_ht, cp->ci.name))
			g_hash_table_insert(cps->photo_ht, strdup(cp->ci.id), cp);
		else
			g_hash_table_insert(cps->photo_ht, strdup(cp->ci.name), cp);
	}
	flickcurl_free_photos(fp);

	cps->ci.time = time(NULL);
	cps->ci.size = j;
	cps->set = CACHE_SET;
	
	return SUCCESS;
}

/*
 * Initiates a new flickcurl connection and creates the caching
 * mechanism.
*/
int flickr_cache_init() {
	g_thread_init(NULL);
	if(flickr_init())
		return FAIL;
	photoset_ht = g_hash_table_new(g_str_hash, g_str_equal);
	last_cleaned = 0;
	pthread_mutex_init(&cache_lock, NULL);
	check_cache();
	return SUCCESS;
}

/*
 * Destroys the caches and the flickcurl connection
*/
void flickr_cache_kill() {
	/* Wipe existing cache */
	g_hash_table_foreach_remove(photoset_ht, free_photoset_ht, NULL);
	g_hash_table_destroy(photoset_ht);
	pthread_mutex_destroy(&cache_lock);
	flickr_kill();
}

/**
* ===Accessing Data Methods===
**/

/* Creates an array of strings cooresponding to the users photosets.
 * Returns the number of photosets or negative for an error.
 *
 * IMPORTANT: Make sure you free(names) (but not the strings within)
 * after you are done!
 */
int get_photoset_names(char ***names) {
	GHashTableIter iter;
	char *key;
	int size, i;
    
	if(!names)
		return FAIL;

	pthread_mutex_lock(&cache_lock);
	if(check_cache()) {
		pthread_mutex_unlock(&cache_lock);
		return FAIL;
	}

	/* We dont want to add the "" photoset (used for photos without a photoset) into this list */
	size = g_hash_table_size(photoset_ht) - 1;

	if(!(*names = (char **)malloc(sizeof(char *) * size))) {
		pthread_mutex_unlock(&cache_lock);
		return FAIL;
	}

	/* Add each photoset to the list. We add the keys since the names may be duplicates/NULL */
	g_hash_table_iter_init(&iter, photoset_ht);
	i = 0;
	while(g_hash_table_iter_next(&iter, (gpointer)&key, NULL)) {
		if(key && strcmp(key, "")) {
			(*names)[i] = key;
			i++;
		}
	}
	pthread_mutex_unlock(&cache_lock);
	return i;
}

/* Creates an array of strings cooresponding to  the users photos
 * of a certain photoset. Returns the number of photos in the
 * photoset or negative signaling an error.
 *
 * IMPORTANT: Make sure you free(names) (but not the strings within)
 * after you are done!
 */
int get_photo_names(const char *photoset, char ***names) {
	GHashTableIter iter;
	char *key;
	cached_photoset *cps;
	int i, size;

	if(!names || !photoset)
		return FAIL;

	pthread_mutex_lock(&cache_lock);
	if(check_cache()) {
		pthread_mutex_unlock(&cache_lock);
		return FAIL;
	}

	/* If the photoset is not found in the cache, return */
	if(!(cps = g_hash_table_lookup(photoset_ht, photoset))) {
		pthread_mutex_unlock(&cache_lock);
		return FAIL;
	}

	if(check_photoset_cache(cps)) {
		pthread_mutex_unlock(&cache_lock);
		return FAIL;
	}

	size = g_hash_table_size(cps->photo_ht);

	if(!(*names = (char **)malloc(sizeof(char *) * size))) {
		pthread_mutex_unlock(&cache_lock);
		return FAIL;
	}

	/* Add each photo to the list. We add the keys since the names may be duplicates/NULL */
	g_hash_table_iter_init(&iter, cps->photo_ht);
	for(i = 0; g_hash_table_iter_next(&iter, (gpointer)&key, NULL); i++)
			(*names)[i] = key;
	pthread_mutex_unlock(&cache_lock);
	return size;
}

/* Looks for the photoset specified in the argument.
 * Returns pointer to the stored cached_information
 * or 0 if not found.
 */
const cached_information *photoset_lookup(const char *photoset) {
	cached_photoset *cps;

	pthread_mutex_lock(&cache_lock);
	if(check_cache()) {
		pthread_mutex_unlock(&cache_lock);
		return 0;
	}
	cps = g_hash_table_lookup(photoset_ht, photoset);
	pthread_mutex_unlock(&cache_lock);
	if(cps)
		return &(cps->ci);
	else
		return 0;
}

/*
 * Internal method to get the cached_photo of
 * a particular photo.
 */
static cached_photo *get_photo(const char *photoset, const char *photo) {
	cached_photoset *cps;
	cached_photo *cp;

	pthread_mutex_lock(&cache_lock);
	if(check_cache()) {
		pthread_mutex_unlock(&cache_lock);
		return 0;
	}

	if(!(cps = g_hash_table_lookup(photoset_ht, photoset))) {
		pthread_mutex_unlock(&cache_lock);
		return 0;
	}

	if(check_photoset_cache(cps)) {
		pthread_mutex_unlock(&cache_lock);
		return 0;
	}
	cp = g_hash_table_lookup(cps->photo_ht, photo);
	pthread_mutex_unlock(&cache_lock);
	return cp;
}

/* Looks for the photo specified in the arguments.
 * Returns pointer to the stored cached_information
 * or 0 if not found.
 */
const cached_information *photo_lookup(const char *photoset, const char *photo) {
	cached_photo *cp;
	if((cp = get_photo(photoset, photo)))
		return &(cp->ci);
	else
		return 0;
}

/* Returns the URI used to get the actual image of
 * picture.
 */
const char *get_photo_uri(const char *photoset, const char *photo) {
	cached_photo *cp;
	if((cp = get_photo(photoset, photo)))
		return cp->uri;
	else
		return 0;
}

/* Renames the photo specified in the args to the
 * new name.
 */
int set_photo_name(const char *photoset, const char *photo, const char *newname) {
	const cached_information *ci;
	int ret;

	if(!strcmp(photo, newname))
		return SUCCESS;
	
	pthread_mutex_lock(&cache_lock);
	if(!(ci = photo_lookup(photoset, photo))) {
		pthread_mutex_unlock(&cache_lock);
		return FAIL;
	}

	ret = flickcurl_photos_setMeta(fc, ci->id, newname, "");
	last_cleaned = 0;
	pthread_mutex_unlock(&cache_lock);

	return ret;
}

/*int set_photo_photoset(const char *oldset, const char *newset, const char *photo) {
	
}*/

/* Sets the photos size */
int set_photo_size(const char *photoset, const char *photo, unsigned int newsize) {
	cached_photo *cp;
	if(!(cp = get_photo(photoset, photo)))
		return FAIL;
	cp->ci.size = newsize;
	return SUCCESS;
}

