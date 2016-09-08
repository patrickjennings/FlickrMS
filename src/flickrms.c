#define FUSE_USE_VERSION 30
#define _XOPEN_SOURCE 500

#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <ftw.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wfloat-conversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#include <wand/magick_wand.h>
#pragma GCC diagnostic pop

#include "cache.h"
#include "wget.h"


#define PERMISSIONS     0755        /* Cached file permissions. */
#define TMP_DIR_NAME    ".flickrms" /* Where to place cached photos. */
#define PHOTO_TIMEOUT   14400       /* In seconds. */


/* Determines whether to report the true file size in getattr before the file
 * has been downloaded locally. The true file size will always be set and cached
 * after a file has been downloaded (which happens when opening a file). If this
 * option is set, Flickrms will query the remote server to get only the file
 * size for each photo from the response headers but not download the photo.
 * Using the true photo size is recommended for GUI applications as they tend to
 * stat the file before opening.
 * Using a fake file size will be much faster for command line usage.
 */
#define USE_TRUE_PHOTO_SIZE 1       /* 1 will use the true size. 0 will use a fake size. */
#define FAKE_PHOTO_SIZE     1024    /* Only used when USE_TRUE_PHOTO_SIZE is set. */

/* Whether to clean the temporary directory on unmount.
 * The filesystem does not keep track of files that cannot be uploaded to Flickr,
 * such as lock and hidden files created by file browsers, after the file system
 * has been destroyed. This option clears the temp dir used for cached files.
 */
#define CLEAN_TMP_DIR_UMOUNT 1      /* 1 or 0. */


static uid_t uid;   /* The user id of the user that mounted the filesystem */
static gid_t gid;   /* The group id of the user */

static char *tmp_path;


/**
 * Helper functions
**/

/*
 * Returns the first index of a '/' or negative if non exists in the
 * path supplied.
 */
static size_t get_slash_index(const char *path, unsigned short *found) {
    size_t i;
    *found = 0;
    if(!path)
        return 0;
    for(i = 0; i < strlen(path); i++) {
        if(path[i] == '/') {
            *found = 1;
            break;
        }
    }
    return i;
}

/* 
 * Internal method for splitting a path of the format:
 * "/photosetname/photoname"
 * into: photoset = "photosetname" and photo = "photoname"
 */
static int get_photoset_photo_from_path(const char *path, char **photoset, char **photo) {
    size_t i;
    unsigned short found;
    char *path_dup;

    path_dup = strdup(path + 1);
    i = get_slash_index(path_dup, &found);

    if(!path || !photoset || !photo || !path_dup)
        return FAIL;

    if(found) {
        path_dup[i] = '\0';
        *photoset = strdup(path_dup);
        *photo = strdup(path_dup + i + 1);
    }
    else {
        *photoset = strdup("");
        *photo = strdup(path_dup);
    }

    free(path_dup);
    return SUCCESS;
}

/*
 * Sets the uid/gid variables to the user's (who mounted the filesystem)
 * uid/gid. Want to only give the user access to their flickr account.
 */
static inline int set_user_variables() {
    uid = getuid();
    gid = getgid();
    return SUCCESS;
}

/*
 * Set the path to the directory that will be used to get the image
 * data from Flickr.
 */
static inline int set_tmp_path() {
    char *home;

    if(!(home = getenv("HOME")))
        return FAIL;

    tmp_path = (char *)malloc(strlen(home) + strlen(TMP_DIR_NAME) + 2);
    if(!tmp_path)
        return FAIL;

    strcpy(tmp_path, home);
    strcat(tmp_path, "/");
    strcat(tmp_path, TMP_DIR_NAME);

    return 0 - mkdir(tmp_path, PERMISSIONS);
}

static int remove_tmp_file(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf) {
    (void)sb;
    (void)typeflag;
    if(ftwbuf->level == 0)
        return SUCCESS;
    return remove(fpath);
}

static inline void remove_tmp_path() {
    nftw(tmp_path, remove_tmp_file, 64, FTW_DEPTH | FTW_PHYS);
    free(tmp_path);
}

static inline void imagemagick_init() {
    MagickWandGenesis();
}

static inline void imagemagick_destroy() {
    MagickWandTerminus();
}


/**
 * File system functions
**/

static inline void set_stbuf(struct stat *stbuf, mode_t mode, uid_t uid,
  gid_t gid, off_t size, time_t time, nlink_t nlink) {
    stbuf->st_mode = mode;
    stbuf->st_uid = uid;
    stbuf->st_gid = gid;
    stbuf->st_size = size;
    stbuf->st_mtime = time;
    stbuf->st_nlink = nlink;
}

/*
 * Get photo size if needed.
 */
static int process_photo(const char *photoset, const char *photo, cached_information *ci) {
    if(ci->size == PHOTO_SIZE_UNSET && ci->uri) {
        int photo_size = FAKE_PHOTO_SIZE;

        if(USE_TRUE_PHOTO_SIZE) {
            photo_size = get_url_content_length(ci->uri);

            if(photo_size < 0)
                return FAIL;
        }

        ci->size = (unsigned int)photo_size;
    }
    else { /* Possibly dirty. Try stating cached directory. */
        struct stat st_buf;
        char *cached_path = (char *)malloc(strlen(photoset) + strlen(photo) + strlen(tmp_path) + 3);
        strcpy(cached_path, tmp_path);
        if(strcmp(photoset,"")) {
            strcat(cached_path, "/");
            strcat(cached_path, photoset);
        }
        strcat(cached_path, "/");
        strcat(cached_path, photo);

        if(!stat(cached_path, &st_buf)) {
            ci->size = (unsigned int)st_buf.st_size;
        }

        free(cached_path);
    }

    set_photo_size(photoset, photo, ci->size);
    return SUCCESS;
}

static int prime_photo_size_cache(const char *photoset, char **names, unsigned int num_names) {
    unsigned int i;

    #pragma omp parallel for
    for(i = 0; i < num_names; i++) {
        cached_information *ci = photo_lookup(photoset, names[i]);

        if(ci) {
            process_photo(photoset, names[i], ci);
            free_cached_info(ci);
        }
    }

    return SUCCESS;
}

/*
 * Gets the attributes (stat) of the node at path.
 */
static int fms_getattr(const char *path, struct stat *stbuf) {
    int retval = -ENOENT;
    memset((void *)stbuf, 0, sizeof(struct stat));

    if(!strcmp(path, "/")) { /* Path is mount directory */
        /* FIXME: Total size of all files... or leave at 0? */
        set_stbuf(stbuf, S_IFDIR | PERMISSIONS, uid, gid, 0, 0, 1);
        retval = SUCCESS;
    }
    else {
        cached_information *ci = NULL;
        /* Point to charcter after root directory */
        const char *lookup_path = path + 1;
        unsigned short found;
        size_t index = get_slash_index(lookup_path, &found);    /* Look up first forward slash */
        /* If forward slash doesn't exist, we are looking at a photo without a photoset or a photoset. */
        if(!found) {
            if((ci = photoset_lookup(lookup_path))) {   /* See if path is to a photoset (i.e. a directory ) */
                set_stbuf(stbuf, S_IFDIR | PERMISSIONS, uid, gid, ci->size, ci->time, 1);
                retval = SUCCESS;
            }
            else if((ci = photo_lookup("", lookup_path))) { /* See if path is to a photo (i.e. a file ) */
                process_photo("", lookup_path, ci);
                set_stbuf(stbuf, S_IFREG | PERMISSIONS, uid, gid, ci->size, ci->time, 1);
                retval = SUCCESS;
            }
        }
        else {
            /* If forward slash does exist, it means that we have a photo with a photoset (the chars before
             * the slash are the photoset name and the chars after the slash are the photo name.
             */
            char *photoset = (char *)malloc(index + 1);
            if(!photoset)
                retval = -ENOMEM;
            else {
                strncpy(photoset, lookup_path, index);  /* Extract the photoset from the path */
                photoset[index] = '\0';

                ci = photo_lookup(photoset, lookup_path + index + 1);   /* Look for the photo */
                if(ci) {
                    process_photo(photoset, lookup_path + index + 1, ci);
                    set_stbuf(stbuf, S_IFREG | PERMISSIONS, uid, gid, ci->size, ci->time, 1);
                    retval = SUCCESS;
                }
                free(photoset);
            }
        }
        if(ci)
            free_cached_info(ci);
    }
    return retval;
}

/* Read directory */
static int fms_readdir(const char *path, void *buf,
  fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
    unsigned int num_names, i;
    char **names;
    (void)offset;
    (void)fi;

    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);

    if(!strcmp(path, "/")) {                      /* Path is to mounted directory */
        num_names = get_photoset_names(&names);   /* Report photoset names */
        for(i = 0; i < num_names; i++) {
            filler(buf, names[i], NULL, 0);
            free(names[i]);
        }
        if(num_names > 0)
            free(names);

        num_names = get_photo_names("", &names);  /* Get all photo names with no photoset attached */
    }
    else
        num_names = get_photo_names(path + 1, &names); /* Get the names of photos in the photoset */

    if(num_names > 0)
        prime_photo_size_cache(path + 1, names, num_names);

    for(i = 0; i < num_names; i++) {
        filler(buf, names[i], NULL, 0);
        free(names[i]);
    }
    free(names);

    return SUCCESS;
}

static int fms_rename(const char *old_path, const char *new_path) {
    char *old_photo;
    char *old_photoset;
    char *new_photo;
    char *new_photoset;

    if(get_photoset_photo_from_path(old_path, &old_photoset, &old_photo))
        return FAIL;

    if(get_photoset_photo_from_path(new_path, &new_photoset, &new_photo))
        return FAIL;

    if(strcmp(old_photo, new_photo)) {
        if(set_photo_name(old_photoset, old_photo, new_photo)) {
            if(set_photoset_name(old_path + 1, new_path + 1))
                return FAIL;
        }
        else {
            free(old_photo);
            old_photo = strdup(new_photo);
        }
    }

    if(strcmp(old_photoset, new_photoset))
        if(set_photo_photoset(old_photoset, old_photo, new_photoset))
            return FAIL;

    free(old_photo);
    free(old_photoset);
    free(new_photo);
    free(new_photoset);
    return SUCCESS;
}

static inline void set_photoset_tmp_dir(char *dir_path, const char *tmp_path,
  const char *photoset) {
    strcpy(dir_path, tmp_path);
    strcat(dir_path, "/");
    strcat(dir_path, photoset);
}

static int fms_open(const char *path, struct fuse_file_info *fi) {
    char *photo;
    char *photoset;
    char *uri;
    char *wget_path;
    int fd;
    struct stat st_buf;

    #define RET(ret) free(wget_path); free(uri); free(photo); free(photoset); return ret;

    if(get_photoset_photo_from_path(path, &photoset, &photo))
        return FAIL;

    wget_path = (char *)malloc(strlen(tmp_path) + strlen(path) + 1);
    set_photoset_tmp_dir(wget_path, tmp_path, photoset);

    uri = get_photo_uri(photoset, photo);
    if(uri) {
        mkdir(wget_path, PERMISSIONS);      /* Create photoset temp directory if it doesn't exist */

        strcpy(wget_path, tmp_path);
        strcat(wget_path, path);

        if(access(wget_path, F_OK)) {
            /* Get the image from flickr and put it into the temp dir if it doesn't already exist. */
            if(wget(uri, wget_path) < 0) {
                RET(FAIL)
            }
        }
    }
    else
    {
        /* Photo dirty? Try to open anyway. */
        strcpy(wget_path, tmp_path);
        strcat(wget_path, path);
    }

    if(stat(wget_path, &st_buf)) {
        RET(FAIL)
    }

    if((time(NULL) - st_buf.st_mtime) > PHOTO_TIMEOUT) {
        if(uri && wget(uri, wget_path) < 0) {
            RET(FAIL)
        }
    }

    fd = open(wget_path, fi->flags);
    if(fd < 0) {
        RET(-errno)
    }

    fi->fh = (uint64_t)fd;
    set_photo_size(photoset, photo, (unsigned int)st_buf.st_size);

    RET(SUCCESS)
}

static int fms_read(const char *path, char *buf, size_t size,
  off_t offset, struct fuse_file_info *fi) {
    (void)path;
    ssize_t ret = pread((int)fi->fh, buf, size, offset);
    return (ret < 0) ? -errno : (int)ret;
}

static int fms_write(const char *path, const char *buf, size_t size,
  off_t offset, struct fuse_file_info *fi) {
    (void)path;
    char *photoset, *photo;
    ssize_t ret;

    if(get_photoset_photo_from_path(path, &photoset, &photo))
        return FAIL;

    set_photo_dirty(photoset, photo, DIRTY);

    free(photoset);
    free(photo);
    ret = pwrite((int)fi->fh, buf, size, offset);

    return (ret < 0) ? -errno : (int)ret;
}

static int fms_flush(const char *path, struct fuse_file_info *fi) {
    (void)path;
    (void)fi;
    return SUCCESS;
}

static int fms_release(const char *path, struct fuse_file_info *fi) {
    (void)fi;
    char *photoset, *photo;
    char *temp_scratch_path;  

    if(get_photoset_photo_from_path(path, &photoset, &photo))
        return FAIL;

    temp_scratch_path = (char *)malloc(strlen(tmp_path) + strlen(path) + 1);
    strcpy(temp_scratch_path, tmp_path);
    strcat(temp_scratch_path, path);

    if(get_photo_dirty(photoset, photo) == DIRTY) {
        MagickWand *mw = NewMagickWand();

        if(!mw)
            return FAIL;

        if(MagickPingImage(mw, temp_scratch_path))
            upload_photo(photoset, photo, temp_scratch_path);

        DestroyMagickWand(mw);
    }

    free(temp_scratch_path);
    free(photoset);
    free(photo);

    int ret = close((int)fi->fh);
    return (ret < 0) ? -errno : SUCCESS;
}

static int fms_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
    int fd;
    char *photoset, *photo;
    char *temp_scratch_path;

    if(get_photoset_photo_from_path(path, &photoset, &photo))
        return FAIL;

    int retval = create_empty_photo(photoset, photo);
    
    free(photoset);
    free(photo);

    if(retval)
        return FAIL;

    temp_scratch_path = (char *)malloc(strlen(tmp_path) + strlen(path) + 1);
    strcpy(temp_scratch_path, tmp_path);
    strcat(temp_scratch_path, path);

    fd = creat(temp_scratch_path, mode);
    fi->fh = (uint64_t)fd;

    free(temp_scratch_path);
    return (fd < 0) ? -errno : SUCCESS;
}

/* Only called after create. For new files. */
static int fms_fgetattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi) {
    (void)path;
    int ret = fstat((int)fi->fh, stbuf);
    return (ret < 0) ? -errno : SUCCESS;
}

static int fms_mkdir(const char *path, mode_t mode) {
    (void)mode;
    char *temp_scratch_path;
    const char *photoset = path + 1;
    unsigned short found;

    get_slash_index(photoset, &found);
    if(found)                           // Can only mkdir on first level
        return FAIL;

    if(create_empty_photoset(photoset))
        return FAIL;

    temp_scratch_path = (char *)malloc(strlen(tmp_path) + strlen(path) + 1);
    strcpy(temp_scratch_path, tmp_path);
    strcat(temp_scratch_path, path);

    mkdir(temp_scratch_path, PERMISSIONS);      // Create photoset tmp directory

    free(temp_scratch_path);

    return SUCCESS;
}

int fms_statfs(const char *path, struct statvfs* stbuf) {
    (void)path;
    return statvfs(tmp_path, stbuf);
}

int fms_chmod(const char *path, mode_t mode) {
    char *temp_scratch_path;
    int retval = FAIL;

    temp_scratch_path = (char *)malloc(strlen(tmp_path) + strlen(path) + 1);
    strcpy(temp_scratch_path, tmp_path);
    strcat(temp_scratch_path, path);

    retval = chmod(temp_scratch_path, mode);

    free(temp_scratch_path);

    return retval;
}

int fms_chown(const char *path, uid_t uid, gid_t gid) {
    char *temp_scratch_path;
    int retval = FAIL;

    temp_scratch_path = (char *)malloc(strlen(tmp_path) + strlen(path) + 1);
    strcpy(temp_scratch_path, tmp_path);
    strcat(temp_scratch_path, path);

    retval = chown(temp_scratch_path, uid, gid);

    free(temp_scratch_path);

    return retval;
}

int fms_unlink(const char *path) {
    char *photoset, *photo;
    char *temp_scratch_path;
    int retval = FAIL;

    if(get_photoset_photo_from_path(path, &photoset, &photo))
        return FAIL;

    if(remove_photo_from_cache(photoset, photo)) {
        free(photoset);
        free(photo);
        return FAIL;
    }

    temp_scratch_path = (char *)malloc(strlen(tmp_path) + strlen(path) + 1);
    strcpy(temp_scratch_path, tmp_path);
    strcat(temp_scratch_path, path);

    retval = unlink(temp_scratch_path);

    free(temp_scratch_path);
    free(photoset);
    free(photo);

    return retval;
}


/**
 * Main function
**/


static struct fuse_operations flickrms_oper = {
    .getattr = fms_getattr,
    .readdir = fms_readdir,
    .open = fms_open,
    .read = fms_read,
    .write = fms_write,
    .flush = fms_flush,
    .release = fms_release,
    .rename = fms_rename,
    .create = fms_create,
    .fgetattr = fms_fgetattr,
    .mkdir = fms_mkdir,
    .statfs = fms_statfs,
    .chmod = fms_chmod,
    .chown = fms_chown,
    .unlink = fms_unlink
};

int main(int argc, char *argv[]) {
    int ret;

    if((ret = set_user_variables()))
        return ret;
    if((ret = set_tmp_path()) == FAIL)
        return ret;
    if((ret = flickr_cache_init()))
        return ret;
    if((ret = wget_init()))
        return ret;

    imagemagick_init();

    ret = fuse_main(argc, argv, &flickrms_oper, NULL);

    flickr_cache_kill();
    wget_destroy();
    imagemagick_destroy();

    if(CLEAN_TMP_DIR_UMOUNT)
        remove_tmp_path();

    return ret;
}
