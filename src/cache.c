#include <flickcurl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <glib.h>
#include <pthread.h>

#include "cache.h"
#include "conf.h"


#define DEFAULT_CACHE_TIMEOUT   14400 /* In seconds. */

/* Valid sizes: http://librdf.org/flickcurl/api/flickcurl-section-photo.html#flickcurl-photo-as-source-uri */
#define GET_PHOTO_SIZE      'o'
#define PHOTOS_PER_API_CALL 100
#define PHOTO_EXTRAS        "date_taken,url_o,original_format"


/* Photo parameters */
#define SAFETY_LEVEL    1
#define CONTENT_TYPE    1

#define CACHE_UNSET     0
#define CACHE_SET       1


typedef struct {
    cached_information ci;
    unsigned short set;
    GHashTable *photo_ht;
} cached_photoset;

typedef struct {
    cached_information ci;
} cached_photo;


static GHashTable *photoset_ht;             /* The photoset cache */
static pthread_rwlock_t cache_lock;         /* To make thread safe */
static time_t last_cleaned;                 /* To age/invalidate the cache */

static flickcurl *fc;


static inline cached_photo *create_cached_photo() {
    return (cached_photo *)calloc(1, sizeof(cached_photo));
}

static inline cached_photoset *create_cached_photoset() {
    return (cached_photoset *)calloc(1, sizeof(cached_photoset));
}

static inline GHashTable *create_cache() {
    return g_hash_table_new(g_str_hash, g_str_equal);
}


/*
 * Initialize the flickcurl connection
*/
static int flickr_init() {
    char *conf_path;
    char *login;

    flickcurl_init();
    fc = flickcurl_new();

    conf_path = get_conf_path();
    if(!conf_path)
        return FAIL;

    if(check_conf_file(conf_path, fc))
        return FAIL;

    /* Read from the config file, ~/.flickcurl.conf */
    if(flickcurl_config_read_ini(fc, conf_path, "flickr", fc, flickcurl_config_var_handler))
        return FAIL;

    login = flickcurl_test_login(fc);
    if(!login)
        return FAIL;

    free(login);
    free(conf_path);
    return SUCCESS;
}

static void flickr_kill() {
    flickcurl_free(fc);
    flickcurl_finish();
}


/**
 * ===Cache Methods===
**/

/* Creates a new cached_photoset using the photoset or a blank one if NULL is passed in */
static int new_cached_photoset(cached_photoset **cps, flickcurl_photoset *fps) {
    unsigned int i;
    cached_information *ci;

    *cps = create_cached_photoset();
    if(!cps)
        return FAIL;

    ci = &((*cps)->ci);
    ci->name = strdup(fps ? fps->title : "");
    ci->id = strdup(fps ? fps->id : "");
    ci->time = 0;
    ci->size = fps ? (unsigned int)fps->photos_count : 0;
    ci->dirty = CLEAN;
    (*cps)->set = CACHE_UNSET;
    (*cps)->photo_ht = g_hash_table_new(g_str_hash, g_str_equal);

    /* Replace backslashes with spaces */
    for(i = 0; i < strlen(ci->name); i++) {
        if(ci->name[i] == '/')
            ci->name[i] = ' ';
    }
    return SUCCESS;
}

static cached_information *copy_cached_info(const cached_information *ci) {
    cached_information *newci = ci?(cached_information *)malloc(sizeof(cached_information)):NULL;
    if(!newci)
        return NULL;
    *newci = *ci;
    if(ci->name)
        newci->name = strdup(ci->name);
    if(ci->id)
        newci->id = strdup(ci->id);
    return newci;
}

void free_cached_info(cached_information *ci) {
    if(ci) {
        free(ci->name);
        free(ci->id);
        free(ci);
    }
}

/* All of our keys and values will be dynamic so we will want to free them. */
static gboolean free_photo_ht(gpointer key, gpointer value, gpointer user_data) {
    (void)user_data;
    cached_photo *cp = value;

    if(cp->ci.dirty == CLEAN) {
        free(cp->ci.uri);
        free(cp->ci.name);
        free(cp->ci.id);
        free(key);
        free(value);
        return TRUE;
    }
    else {
        return FALSE;
    }
}

/* All of our keys and values will be dynamic so we will want to free them. */
static gboolean free_photoset_ht(gpointer key, gpointer value, gpointer user_data) {
    GHashTable *photo_ht;
    (void)user_data;

    cached_photoset *cps = value;
    photo_ht = cps->photo_ht;

    g_hash_table_foreach_remove(photo_ht, free_photo_ht, NULL);

    if(cps->ci.dirty == CLEAN && g_hash_table_size(photo_ht) == 0) {
        g_hash_table_destroy(photo_ht);

        free(key);
        free(cps->ci.name);
        free(cps->ci.id);
        free(value);
        return TRUE;
    }
    else {
        cps->set = (cps->ci.dirty == CLEAN) ? CACHE_UNSET : CACHE_SET;
        return FALSE;
    }
}

/*
 * Checks whether the cache should be cleaned. Time can be changed in
 * DEFAULT_CACHE_TIMEOUT define.
 * Assumes there is a lock initiated
*/
static int check_cache() {
    flickcurl_photoset **fps;
    cached_photoset *cps;
    int i;

    if((time(NULL) - last_cleaned) < DEFAULT_CACHE_TIMEOUT)
        return SUCCESS;

    pthread_rwlock_unlock(&cache_lock); /* Release the read lock and lock for writting */
    pthread_rwlock_wrlock(&cache_lock);

    if((time(NULL) - last_cleaned) < DEFAULT_CACHE_TIMEOUT)
        return SUCCESS;

    /* Wipe clean entries from the cache. */
    g_hash_table_foreach_remove(photoset_ht, free_photoset_ht, NULL);

    if(g_hash_table_size(photoset_ht) == 0) {
        g_hash_table_destroy(photoset_ht);
        photoset_ht = create_cache();
    }

    if(!g_hash_table_lookup(photoset_ht, "")) {
        /* Create an empty photoset container for the photos not in a photoset */
        if(new_cached_photoset(&cps, NULL))
            return FAIL;

        g_hash_table_insert(photoset_ht, strdup(""), cps);
    }

    if(!(fps = flickcurl_photosets_getList(fc, NULL)))
        return FAIL;

    /* Add the photosets to the cache */
    for(i = 0; fps[i]; i++) {
        if(!g_hash_table_lookup(photoset_ht, fps[i]->title)) {
            if(new_cached_photoset(&cps, fps[i]))
                return FAIL;
            g_hash_table_insert(photoset_ht, strdup(cps->ci.name), cps);
        }
    }
    flickcurl_free_photosets(fps);

    last_cleaned = time(NULL);

    return SUCCESS;
}

static int populate_photoset_cache(cached_photoset *cps, flickcurl_photo **fp) {
    int j = 0;

    if(!fp)
        return FAIL;

    /* Add photos to photoset cache */
    for(; fp[j]; j++) {
        cached_photo *cp;
        struct tm tm = {0};
        char *title;
        char *id;

        title = fp[j]->fields[PHOTO_FIELD_title].string;
        id    = fp[j]->id;

        /* Check if dirty version already exists in the database. */
        if((cp = g_hash_table_lookup(cps->photo_ht, title))) {
            if(!strcmp(cp->ci.id, id))
                continue;
        }
        else if((cp = g_hash_table_lookup(cps->photo_ht, id))) {
            if(!strcmp(cp->ci.id, id))
                continue;
            else
                continue;              /* TODO: Need to figure out what to do here. */
        }

        if(!(cp = create_cached_photo())) {
            return FAIL;
        }

        cp->ci.uri = flickcurl_photo_as_source_uri(fp[j], GET_PHOTO_SIZE);
        cp->ci.name = strdup(title);
        cp->ci.id = strdup(id);
        cp->ci.size = PHOTO_SIZE_UNSET;
        cp->ci.dirty = CLEAN;

        sscanf(fp[j]->fields[PHOTO_FIELD_dates_taken].string, "%4d-%2d-%2d %2d:%2d:%2d",
          &(tm.tm_year), &(tm.tm_mon), &(tm.tm_mday), &(tm.tm_hour), &(tm.tm_min), &(tm.tm_sec));
        tm.tm_year = tm.tm_year - 1900;     /* Years since 1900 */
        tm.tm_mon--;                        /* Programmers start with 0... */
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

    return j;
}

static inline flickcurl_photo **get_photoset_photos(cached_photoset *cps, int page) {
    flickcurl_photo **fp;

    /* Are we searching for photos in a photoset or not? */
    if(!strcmp(cps->ci.id, "")) {   /* Get photos NOT in a photoset */
        if(!(fp = flickcurl_photos_getNotInSet(fc, 0, 0, NULL, NULL, 0, PHOTO_EXTRAS, PHOTOS_PER_API_CALL, page)))
            return NULL;
    }
    else {                          /* Add the photos of the photoset into the cache */
        if(!(fp = flickcurl_photosets_getPhotos(fc, cps->ci.id, PHOTO_EXTRAS, 0, PHOTOS_PER_API_CALL, page)))
            return NULL;
    }

    return fp;
}

/*
 * The photosets are filled dynamically based on which photosets are loaded
 * (it would be a waste to load all flickr info if not needed).
 * This method needs to be called in order to fill the photoset cache
 * with photo information.
 * Assumes there is a lock initiated
 */
static int check_photoset_cache(cached_photoset *cps) {
    flickcurl_photo **fp;
    unsigned int total_size = 0;
    int processed = 0;
    int page = 0;

    if(!cps)
        return FAIL;
    if(!(cps->photo_ht))
        return FAIL;
    if(cps->set)
        return SUCCESS;

    pthread_rwlock_unlock(&cache_lock); /* Release the read lock and lock for writting */
    pthread_rwlock_wrlock(&cache_lock);

    if(cps->set)
        return SUCCESS;

    while((fp = get_photoset_photos(cps, page++))) {
        processed = populate_photoset_cache(cps, fp);
        flickcurl_free_photos(fp);
        if(processed < 0)
            return FAIL;

        total_size += (unsigned int)processed;

        if(processed < PHOTOS_PER_API_CALL)
            break;
    }

    cps->ci.time = time(NULL);
    cps->ci.size = total_size;
    cps->set = CACHE_SET;

    return SUCCESS;
}

/*
 * Initiates a new flickcurl connection and creates the caching
 * mechanism.
*/
int flickr_cache_init() {
    if(flickr_init())
        return FAIL;
    photoset_ht = create_cache();
    last_cleaned = 0;
    pthread_rwlock_init(&cache_lock, NULL);
    return SUCCESS;
}

/*
 * Destroys the caches and the flickcurl connection
*/
void flickr_cache_kill() {
    /* Wipe existing cache */
    pthread_rwlock_wrlock(&cache_lock);
    pthread_rwlock_destroy(&cache_lock);
    g_hash_table_foreach_remove(photoset_ht, free_photoset_ht, NULL);
    g_hash_table_destroy(photoset_ht);
    flickr_kill();
}

/**
* ===Accessing Data Methods===
**/

/* Creates an array of strings cooresponding to the users photosets.
 * Returns the number of photosets or negative for an error.
 *
 * IMPORTANT: Make sure you free(names) after you are done!
 */
unsigned int get_photoset_names(char ***names) {
    GHashTableIter iter;
    char *key;
    unsigned int size, i;

    if(!names)
        return 0;

    pthread_rwlock_rdlock(&cache_lock);
    if(check_cache()) {
        pthread_rwlock_unlock(&cache_lock);
        return 0;
    }

    /* We dont want to add the "" photoset (used for photos without a photoset) into this list */
    size = g_hash_table_size(photoset_ht) - 1;

    if(!(*names = (char **)malloc(sizeof(*names) * size))) {
        pthread_rwlock_unlock(&cache_lock);
        return 0;
    }

    /* Add each photoset to the list. We add the keys since the names may be duplicates/NULL */
    g_hash_table_iter_init(&iter, photoset_ht);
    i = 0;
    while(g_hash_table_iter_next(&iter, (gpointer)&key, NULL)) {
        if(key && strcmp(key, "")) {
            (*names)[i] = strdup(key);
            i++;
        }
    }
    pthread_rwlock_unlock(&cache_lock);
    return i;
}

/* Creates an array of strings cooresponding to  the users photos
 * of a certain photoset. Returns the number of photos in the
 * photoset or negative signaling an error.
 *
 * IMPORTANT: Make sure you free(names) (but not the strings within)
 * after you are done!
 */
unsigned int get_photo_names(const char *photoset, char ***names) {
    GHashTableIter iter;
    char *key;
    cached_photoset *cps;
    cached_photo *cp;
    unsigned int i, size;

    if(!names || !photoset)
        return 0;

    pthread_rwlock_rdlock(&cache_lock);
    if(check_cache())
        goto fail;

    /* If the photoset is not found in the cache, return */
    if(!(cps = g_hash_table_lookup(photoset_ht, photoset)))
        goto fail;

    if(check_photoset_cache(cps))
        goto fail;

    size = g_hash_table_size(cps->photo_ht);

    if(!(*names = (char **)malloc(sizeof(*names) * size)))
        goto fail;

    /* Add each photo to the list. We add the keys since the names may be duplicates/NULL */
    g_hash_table_iter_init(&iter, cps->photo_ht);
    for(i = 0; g_hash_table_iter_next(&iter, (gpointer)&key, (gpointer)&cp); i++)
    {
        (*names)[i] = strdup(key);
    }

    pthread_rwlock_unlock(&cache_lock);
    return size;

fail:   pthread_rwlock_unlock(&cache_lock);
    return 0;
}

/* Looks for the photoset specified in the argument.
 * Returns pointer to the stored cached_information
 * or 0 if not found.
 */
cached_information *photoset_lookup(const char *photoset) {
    cached_photoset *cps;
    cached_information *ci_copy = NULL;

    pthread_rwlock_rdlock(&cache_lock);
    if(check_cache())
        goto fail;

    cps = g_hash_table_lookup(photoset_ht, photoset);
    if(cps)
        ci_copy = copy_cached_info(&(cps->ci));

fail: pthread_rwlock_unlock(&cache_lock);
    return ci_copy;
}

/*
 * Internal method to get the cached_photo of
 * a particular photo.
 * Assumes there is a lock initiated
 */
static cached_photo *get_photo(const char *photoset, const char *photo) {
    cached_photoset *cps;

    if(check_cache())
        return NULL;

    if(!(cps = g_hash_table_lookup(photoset_ht, photoset)))
        return NULL;

    if(check_photoset_cache(cps))
        return NULL;

    return g_hash_table_lookup(cps->photo_ht, photo);
}

/* Looks for the photo specified in the arguments.
 * Returns pointer to the stored cached_information
 * or 0 if not found.
 */
cached_information *photo_lookup(const char *photoset, const char *photo) {
    cached_photo *cp;
    cached_information *ci_copy = NULL;

    pthread_rwlock_rdlock(&cache_lock);
    if((cp = get_photo(photoset, photo)))
        ci_copy = copy_cached_info(&(cp->ci));
    pthread_rwlock_unlock(&cache_lock);

    return ci_copy;
}

/* Returns the URI used to get the actual image of
 * picture.
 */
char *get_photo_uri(const char *photoset, const char *photo) {
    cached_photo *cp;
    char *uri_copy = NULL;

    pthread_rwlock_rdlock(&cache_lock);
    if((cp = get_photo(photoset, photo))) {
        if(cp->ci.uri) {
            uri_copy = strdup(cp->ci.uri);
        }
    }
    pthread_rwlock_unlock(&cache_lock);

    return uri_copy;
}

/* Renames the photo specified in the args to the
 * new name.
 */
int set_photo_name(const char *photoset, const char *photo, const char *newname) {
    cached_photo *cp;

    pthread_rwlock_wrlock(&cache_lock);
    if(!(cp = get_photo(photoset, photo))) {
        pthread_rwlock_unlock(&cache_lock);
        return FAIL;
    }

    flickcurl_photos_setMeta(fc, cp->ci.id, newname, "");
    last_cleaned = 0;
    pthread_rwlock_unlock(&cache_lock);
    return SUCCESS;
}

/* Renames the photoset */
int set_photoset_name(const char *photoset, const char *newname) {
    void *key, *value;
    cached_photoset *cps;
    int retval = FAIL;

    pthread_rwlock_wrlock(&cache_lock);

    if(g_hash_table_lookup_extended(photoset_ht, photoset, &key, &value)) {
        cps = value;

        if(cps->ci.dirty == CLEAN)
            if(flickcurl_photosets_editMeta(fc, cps->ci.id, newname, NULL))
                goto fail;

        free(cps->ci.name);
        cps->ci.name = strdup(newname);

        g_hash_table_remove(photoset_ht, photoset);
        g_hash_table_insert(photoset_ht, strdup(newname), cps);

        free(key);
        retval = SUCCESS;
    }

fail: pthread_rwlock_unlock(&cache_lock);

    return retval;
}

/* Sets the photos size */
int set_photo_size(const char *photoset, const char *photo, unsigned int newsize) {
    cached_photo *cp;

    pthread_rwlock_wrlock(&cache_lock);
    if(!(cp = get_photo(photoset, photo))) {
        pthread_rwlock_unlock(&cache_lock);
        return FAIL;
    }

    cp->ci.size = newsize;
    pthread_rwlock_unlock(&cache_lock);

    return SUCCESS;
}

int set_photo_dirty(const char *photoset, const char *photo, unsigned short dirty) {
    cached_photo *cp;

    pthread_rwlock_wrlock(&cache_lock);
    if(!(cp = get_photo(photoset, photo))) {
        pthread_rwlock_unlock(&cache_lock);
        return FAIL;
    }

    cp->ci.dirty = dirty;
    pthread_rwlock_unlock(&cache_lock);

    return SUCCESS;
}

int get_photo_dirty(const char *photoset, const char *photo) {
    cached_photo *cp;
    unsigned short dirty;

    pthread_rwlock_rdlock(&cache_lock);
    if(!(cp = get_photo(photoset, photo))) {
        pthread_rwlock_unlock(&cache_lock);
        return FAIL;
    }
    dirty = cp->ci.dirty;
    pthread_rwlock_unlock(&cache_lock);

    return dirty;
}

int create_empty_photoset(const char *photoset) {
    cached_photoset *cps;
    int retval = FAIL;

    pthread_rwlock_wrlock(&cache_lock);

    if(g_hash_table_lookup(photoset_ht, photoset))
        goto fail;

    /* The new empty photoset */
    if(!(cps = create_cached_photoset()))
        goto fail;

    cps->ci.name = strdup(photoset);
    cps->ci.id = strdup("");
    cps->ci.dirty = DIRTY;
    cps->ci.time = time(NULL);
    cps->set = CACHE_SET;
    cps->photo_ht = create_cache();

    g_hash_table_insert(photoset_ht, strdup(cps->ci.name), cps);

    retval = SUCCESS;

fail: pthread_rwlock_unlock(&cache_lock);
    return retval;
}

int create_empty_photo(const char *photoset, const char *photo) {
    cached_photoset *cps;
    cached_photo *cp;
    int retval = FAIL;

    pthread_rwlock_wrlock(&cache_lock);

    cps = g_hash_table_lookup(photoset_ht, photoset);

    /* Check to see photoset exists. */
    if(!cps)
        goto fail;

   /* Check if photo already exists */
    if(g_hash_table_lookup(cps->photo_ht, photo))
        goto fail;

    /* The new empty photo */
    if(!(cp = create_cached_photo()))
        goto fail;

    cp->ci.name = strdup(photo);
    cp->ci.id = strdup("");
    cp->ci.dirty = DIRTY;
    cp->ci.time = time(NULL);
    cp->ci.size = PHOTO_SIZE_UNSET;

    g_hash_table_insert(cps->photo_ht, strdup(cp->ci.name), cp);

    retval = SUCCESS;

fail: pthread_rwlock_unlock(&cache_lock);
    return retval;
}

int upload_photo(const char *photoset, const char *photo, const char *path) {
    flickcurl_upload_status* status;
    flickcurl_upload_params params;
    cached_photoset *cps;
    cached_photo *cp;
    int retval = FAIL;

    pthread_rwlock_wrlock(&cache_lock);

    cps = g_hash_table_lookup(photoset_ht, photoset);

    if(!cps)
        goto fail;

    if(!(cp = g_hash_table_lookup(cps->photo_ht, photo)))
        goto fail;

    memset(&params, '\0', sizeof(flickcurl_upload_params));
    params.safety_level = SAFETY_LEVEL;    /* default safety */
    params.content_type = CONTENT_TYPE;    /* default photo */
    params.photo_file = path;
    params.title = cp->ci.name;

    status = flickcurl_photos_upload_params(fc, &params);

    if(status) {
        if(cps->ci.dirty == DIRTY) { // if photoset is dirty, create it
            char * photosetid = flickcurl_photosets_create(fc, cps->ci.name, NULL, status->photoid, NULL);

            if(photosetid) {
                cps->ci.id = photosetid;
                cps->ci.dirty = CLEAN;
            }
        }
        else if(strcmp(cps->ci.id, "")) { // if photoset has an id, add new photo to it
            flickcurl_photosets_addPhoto(fc, cps->ci.id, status->photoid);
        }

        flickcurl_free_upload_status(status);
    }

    cp->ci.dirty = CLEAN;

    cps->set = CACHE_UNSET;

    retval = SUCCESS;

fail: pthread_rwlock_unlock(&cache_lock);
    return retval;
}

int set_photo_photoset(const char *photoset, const char *photo, const char *new_photoset) {
    cached_photoset *cps;
    cached_photoset *new_cps;
    cached_photo *cp;
    int retval = FAIL;

    pthread_rwlock_wrlock(&cache_lock);

    cps = g_hash_table_lookup(photoset_ht, photoset);
    new_cps = g_hash_table_lookup(photoset_ht, new_photoset);

    if(!cps || !new_cps)
        goto fail;

    if(!(cp = g_hash_table_lookup(cps->photo_ht, photo)))
        goto fail;

    if(strcmp(cps->ci.id, "")) {
        flickcurl_photosets_removePhoto(fc, cps->ci.id, cp->ci.id);
    }

    if(strcmp(new_cps->ci.id, "")) {
        flickcurl_photosets_addPhoto(fc, new_cps->ci.id, cp->ci.id);
    }

    g_hash_table_foreach_remove(cps->photo_ht, free_photo_ht, NULL);
    g_hash_table_foreach_remove(new_cps->photo_ht, free_photo_ht, NULL);

    cps->set = CACHE_UNSET;
    new_cps->set = CACHE_UNSET;

    retval = SUCCESS;

fail: pthread_rwlock_unlock(&cache_lock);
    return retval;
}

int remove_photo_from_cache(const char *photoset, const char *photo) {
    void *key, *value;
    cached_photoset *cps;
    cached_photo *cp;
    int retval = FAIL;

    pthread_rwlock_wrlock(&cache_lock);

    if(!(cps = g_hash_table_lookup(photoset_ht, photoset)))
        goto fail;

    if(g_hash_table_lookup_extended(cps->photo_ht, photo, &key, &value)) {
        cp = value;

        if(cp->ci.dirty) {
            g_hash_table_remove(cps->photo_ht, photo);

            free(cp->ci.uri);
            free(cp->ci.name);
            free(cp->ci.id);
            free(key);
            free(value);

            retval = SUCCESS;
        }
    }

fail: pthread_rwlock_unlock(&cache_lock);
    return retval;
}

int photoDelete(char *photo_id) {
    return flickcurl_photos_delete(fc, photo_id);
}

