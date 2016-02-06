#define FUSE_USE_VERSION 26
#define _XOPEN_SOURCE 500

#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>

#include "cache.h"
#include "wget.h"


#define PERMISSIONS         0755
#define TMP_DIR_NAME        ".flickrms"
#define THUMBNAIL_TIMEOUT   1200


static uid_t uid;   /* The user id of the user that mounted the filesystem */
static gid_t gid;   /* The group id of the user */

static char *tmp_path;

static char emptystr[] = "";


/**
 * Helper functions
**/

/*
 * Returns the first index of a '/' or negative if non exists in the
 * path supplied.
 */
static int get_slash_index(const char *path) {
    unsigned int i;
    if(!path)
        return FAIL;
    for(i = 0; i < strlen(path); i++)
        if(path[i] == '/')
            return i;
    return -1;
}

/* 
 * Internal method for splitting a path of the format:
 * "/photosetname/photoname"
 * into: photoset = "photosetname" and photo = "photoname"
 */
static int get_photoset_photo_from_path(const char *path, char **photoset, char **photo) {
    int i;
    char *path_dup;

    path_dup = strdup(path + 1);
    i = get_slash_index(path_dup);

    if(!path || !photoset || !photo || !path_dup)
        return FAIL;

    if(i >= 0) {
        path_dup[i] = '\0';
        *photoset = strdup(path_dup);
        *photo = strdup(path_dup + i + 1);
    }
    else {
        *photoset = strdup(emptystr);
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
 * Gets the attributes (stat) of the node at path.
 */
static int fms_getattr(const char *path, struct stat *stbuf) {
    int retval = -ENOENT;
    memset((void *)stbuf, 0, sizeof(struct stat));

    if(!strcmp(path, "/")) { /* Path is mount directory */
        set_stbuf(stbuf, S_IFDIR | PERMISSIONS, uid, gid, 0, 0, 1); /* FIXME: Total size of all files... or leave at 0? */
        retval = SUCCESS;
    }
    else {
        cached_information *ci = NULL;
        const char *lookup_path = path + 1;             /* Point to char after root directory */
        int index = get_slash_index(lookup_path);           /* Look up first forward slash */
        if(index < 1) {                                 /* If forward slash doesn't exist, we are looking at a photo without a photoset or a photoset. */
            if((ci = photoset_lookup(lookup_path))) {   /* See if path is to a photoset (i.e. a directory ) */
                set_stbuf(stbuf, S_IFDIR | PERMISSIONS, uid, gid, ci->size, ci->time, 1);
                retval = SUCCESS;
            }
            else if((ci = photo_lookup(emptystr, lookup_path))) { /* See if path is to a photo (i.e. a file ) */
                set_stbuf(stbuf, S_IFREG | PERMISSIONS, uid, gid, ci->size, ci->time, 1);
                retval = SUCCESS;
            }
        }
        else {                                          /* If forward slash does exist, it means that we have a photo with a photoset (the chars before
                                                           the slash are the photoset name and the chars after the slash are the photo name. */
            char *photoset = (char *)malloc(index + 1);
            if(!photoset)
                retval = -ENOMEM;
            else {
                strncpy(photoset, lookup_path, index);  /* Extract the photoset from the path */
                photoset[index] = '\0';

                ci = photo_lookup(photoset, lookup_path + index + 1);   /* Look for the photo */
                if(ci) {
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
    int num_names, i;
    char **names;
    (void)offset;
    (void)fi;

    if(!strcmp(path, "/")) {                            /* Path is to mounted directory */
        num_names = get_photo_names(emptystr, &names);  /* Get all photo names with no photoset attached */
        for(i = 0; i < num_names; i++) {
            filler(buf, names[i], NULL, 0);
            free(names[i]);
        }
        if(num_names > 0)
            free(names);
        num_names = get_photoset_names(&names);         /* We are going to want to fill the photosets as well */
    }
    else
        num_names = get_photo_names(path + 1, &names); /* Get the names of photos in the photoset */

    if(num_names < 0)
        return -ENOENT;

    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);

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

    if(get_photoset_photo_from_path(path, &photoset, &photo))
        return FAIL;

    wget_path = (char *)malloc(strlen(tmp_path) + strlen(path) + 1);
    set_photoset_tmp_dir(wget_path, tmp_path, photoset);

    uri = get_photo_uri(photoset, photo);
    if( uri ) {
        mkdir(wget_path, PERMISSIONS);      /* Create photoset temp directory if it doesn't exist */

        strcpy(wget_path, tmp_path);
        strcat(wget_path, path);

        if(access(wget_path, F_OK)) {
            if(wget(uri, wget_path) < 0)  {   /* Get the image from flickr and put it into the temp dir if it doesn't already exist. */
                free( wget_path );
                return FAIL;
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
        free( wget_path );
        return FAIL;
    }

    if((time(NULL) - st_buf.st_mtime) > THUMBNAIL_TIMEOUT) {
        if(wget(uri, wget_path) < 0) {
            free( wget_path );
            return FAIL;
        }
    }

    fd = open(wget_path, fi->flags);
    fi->fh = fd;
    set_photo_size(photoset, photo, st_buf.st_size);

    free(uri);
    free(wget_path);
    free(photoset);
    free(photo);
    return (fd < 0)?FAIL:SUCCESS;
}

static int fms_read(const char *path, char *buf, size_t size,
  off_t offset, struct fuse_file_info *fi) {
    (void)path;
    return pread(fi->fh, buf, size, offset);
}

static int fms_write(const char *path, const char *buf, size_t size,
  off_t offset, struct fuse_file_info *fi) {
    (void)path;
    char *photoset, *photo;

    if(get_photoset_photo_from_path(path, &photoset, &photo))
        return FAIL;

    set_photo_dirty( photoset, photo, DIRTY );

    free(photoset);
    free(photo);
    return pwrite(fi->fh, buf, size, offset);
}

static int fms_flush(const char *path, struct fuse_file_info *fi) {
    (void)path;
    return close(fi->fh);
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

    if( get_photo_dirty( photoset, photo ) == DIRTY ) {
        upload_photo( photoset, photo, temp_scratch_path );
    }

    set_photoset_tmp_dir(temp_scratch_path, tmp_path, photoset);

    rmdir(temp_scratch_path);

    free(temp_scratch_path);
    free(photoset);
    free(photo);
    return SUCCESS;
}

static int fms_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
    int fd;
    char *photoset, *photo;
    char *temp_scratch_path;

    if(get_photoset_photo_from_path(path, &photoset, &photo))
        return FAIL;

    int retval = create_empty_photo( photoset, photo );
    
    free(photoset);
    free(photo);

    if( retval )
        return FAIL;

    temp_scratch_path = (char *)malloc(strlen(tmp_path) + strlen(path) + 1);
    strcpy(temp_scratch_path, tmp_path);
    strcat(temp_scratch_path, path);

    fd = creat(temp_scratch_path, mode);
    fi->fh = fd;

    free(temp_scratch_path);
    return (fd < 0)?FAIL:SUCCESS;
}

/* Only called after create. For new files. */
static int fms_fgetattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi) {
    (void)path;
    return fstat(fi->fh, stbuf);
}

static int fms_mkdir(const char *path, mode_t mode) {
    (void)mode;
    char *temp_scratch_path;
    const char *photoset = path + 1;

    if(get_slash_index(photoset) >= 0) // Can only mkdir on first level
        return FAIL;

    if(create_empty_photoset(photoset))
        return FAIL;

    temp_scratch_path = (char *)malloc(strlen(tmp_path) + strlen(path) + 1);
    strcpy(temp_scratch_path, tmp_path);
    strcat(temp_scratch_path, path);

    mkdir(temp_scratch_path, PERMISSIONS);      /* Create photoset tmp directory */

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
    .chown = fms_chown
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

    ret = fuse_main(argc, argv, &flickrms_oper, NULL);

    flickr_cache_kill();
    free(tmp_path);
    return ret;
}
