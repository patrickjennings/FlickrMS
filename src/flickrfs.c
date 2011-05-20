#define FUSE_USE_VERSION 26
#define _XOPEN_SOURCE 500

#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>

#include "cache.h"
#include "wget.h"


#define SUCCESS		0
#define FAIL		-1
#define PERMISSIONS	0755
#define BUFFER_SIZE	1024
#define TMP_DIR_NAME	".flickrfs"


static uid_t uid;	/* The user id of the user that mounted the filesystem */
static gid_t gid;	/* The group id of the user */
static char *tmp_path;

static char emptystr[] = "";


/**
 * Helper functions
**/

/*
 * Returns the first index of a '/' or negative if non exists in the
 * path supplied.
 */
static int slash_index(const char *path) {
	unsigned int i;
	if(!path)
		return FAIL;
	for(i = 0; i < strlen(path); i++)
		if(path[i] == '/')
			return i;
	return FAIL;
}

/* 
 * Internal method for splitting a path of the format:
 * "/photosetname/photoname"
 * into: photoset = "photosetname" and photo = "photoname"
 */
static int split_path(const char *path, char **photoset, char **photo) {
	int i;
	char *path_dup;

	path_dup = strdup(path + 1);
	i = slash_index(path_dup);

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
static inline int setUser() {
	struct passwd *pw;
	if(!(pw = getpwnam(getenv("USER"))))
		return FAIL;

	uid = pw->pw_uid;
	gid = pw->pw_gid;

	return SUCCESS;
}

/*
 * Set the path to the directory that will be used to get the image
 * data from Flickr.
 */
static inline int setTMPDir() {
	char *home;

	if(!(home = getenv("HOME")))
		return FAIL;

	tmp_path = (char *)malloc(strlen(home) + strlen(TMP_DIR_NAME) + 2);

	if(!tmp_path)
		return FAIL;
	sprintf(tmp_path, "%s/%s", home, TMP_DIR_NAME);

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
static int ffs_getattr(const char *path, struct stat *stbuf) {
	memset((void *)stbuf, 0, sizeof(struct stat));
	/* Root */
	if(!strcmp(path, "/")) {
		set_stbuf(stbuf, S_IFDIR | PERMISSIONS, uid, gid, 0, 0, 1);	/* FIXME: Total size of all files... or leave at 0? */
		return SUCCESS;
	}
	else {
		const cached_information *ci;
		const char *lookup_path = path + 1;
		int index = slash_index(lookup_path);
		if(index < 1) {
			if((ci = photoset_lookup(lookup_path))) {
				set_stbuf(stbuf, S_IFDIR | PERMISSIONS, uid, gid, ci->size, ci->time, 1);
				return SUCCESS;
			}
			else if((ci = photo_lookup(emptystr, lookup_path))) {
			    set_stbuf(stbuf, S_IFREG | PERMISSIONS, uid, gid, ci->size, ci->time, 1);
			    return SUCCESS;
			}
		}
		else {
			char *photoset = (char *)malloc(index + 1);
			if(!photoset)
				return -ENOMEM;
			strncpy(photoset, lookup_path, index);
			photoset[index] = '\0';

			ci = photo_lookup(photoset, lookup_path + index + 1);
			free(photoset);
			if(ci) {
				set_stbuf(stbuf, S_IFREG | PERMISSIONS, uid, gid, ci->size, ci->time, 1);
				return SUCCESS;
			}
		}
	}
	return -ENOENT;
}

/* Read directory */
static int ffs_readdir(const char *path, void *buf,
  fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
	int size, i;
	char **names;
	(void)offset;
	(void)fi;

	if(!strcmp(path, "/")) {
		size = get_photo_names(emptystr, &names);
		for(i = 0; i < size; i++)
			filler(buf, names[i], NULL, 0);
		if(size > 0)
			free(names);
		size = get_photoset_names(&names);
	}
	else
		size = get_photo_names(path + 1, &names);

	if(size < 0)
		return -ENOENT;

	filler(buf, ".", NULL, 0);
	filler(buf, "..", NULL, 0);

	for(i = 0; i < size; i++)
		filler(buf, names[i], NULL, 0);
	free(names);

	return SUCCESS;
}

static int ffs_mkdir(const char *path, mode_t mode) {
	(void)mode;
	printf("Path: %s\n", path);
	return SUCCESS;
}

static int ffs_rename(const char *oldpath, const char *newpath) {
	const char *old_path = oldpath + 1;
	const char *new_path = newpath + 1;
	int old_index = slash_index(old_path);
	int new_index = slash_index(new_path);

	if(old_index < 1 && new_index < 1)
		set_photo_name(emptystr, old_path, new_path);
	/*else {
		char *old_set = (char *)malloc(old_index + 1);
		char *new_set = (char *)malloc(new_index + 1);
		if(!old_set || !new_set)
			return -ENOMEM;
		strncpy(old_set, old_path, old_index);
		strncpy(new_set, new_path, new_index);
		old_set[old_index] = '\0';
		new_set[new_index] = '\0';

		set_photo_name(old_set, old_path + old_index + 1, new_path + new_index + 1);
	}*/

	return SUCCESS;
}

static int ffs_open(const char *path, struct fuse_file_info *fi) {
	char *photo;
	char *photoset;
	const char *uri;
	char *wget_path;
	int fd;
	struct stat buf;

	if(split_path(path, &photoset, &photo))
		return FAIL;

	uri = get_photo_uri(photoset, photo);
	if(!uri)
		return FAIL;

	wget_path = (char *)malloc(strlen(tmp_path) + strlen(path) + 1);
	wget_path[0] = '\0';

	strcat(wget_path, tmp_path);
	strcat(wget_path, "/");
	strcat(wget_path, photoset);
	mkdir(wget_path, PERMISSIONS);		/* Create photoset directory if it doesn't exist */

	strcpy(wget_path, tmp_path);
	strcat(wget_path, path);
	if(wget(uri, wget_path) < 0)		/* Get the image from flickr and put it into the tmp dir */
		return FAIL;

	fd = open(wget_path, fi->flags);
	fi->fh = fd;

	if(stat(wget_path, &buf))
		return FAIL;
	set_photo_size(photoset, photo, buf.st_size);

	free(wget_path);
	free(photoset);
	free(photo);
	return (fd < 0)?FAIL:SUCCESS;
}

static int ffs_read(const char *path, char *buf, size_t size,
  off_t offset, struct fuse_file_info *fi) {
  	(void)path;
	return pread(fi->fh, buf, size, offset);
}

static int ffs_write(const char *path, const char *buf, size_t size,
  off_t offset, struct fuse_file_info *fi) {
	(void)path;
	(void)buf;
	(void)size;
	(void)offset;
	(void)fi;
	return 0;
}

/**
 * Main function
**/


static struct fuse_operations flickrfs_oper = {
	.getattr = ffs_getattr,
	.readdir = ffs_readdir,
	.mkdir = ffs_mkdir,
	.open = ffs_open,
	.read = ffs_read,
	.write = ffs_write,
	.rename = ffs_rename
};

int main(int argc, char *argv[]) {
	int ret;

	if((ret = setUser()))
		return ret;

	if((ret = setTMPDir()) == FAIL)
		return ret;

	if((ret = flickr_cache_init()))
		return ret;

	ret = fuse_main(argc, argv, &flickrfs_oper, NULL);

	flickr_cache_kill();
	free(tmp_path);
	return ret;
}
