#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pwd.h>
#include <sys/types.h>

#include "cache.h"


#define SUCCESS		0
#define FAIL		-1
#define PERMISSIONS	0755


static uid_t uid;	/* The user id of the user that mounted the filesystem */
static gid_t gid;	/* The group id of the user */


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
 * Sets the uid/gid variables to the user's (who mounted the filesystem)
 * uid/gid. Want to only give the user access to their flickr account.
 */
static int setUser() {
    struct passwd *pw;
    if(!(pw = getpwnam(getenv("USER"))))
	return FAIL;
    
    uid = pw->pw_uid;
    gid = pw->pw_gid;
    
    return SUCCESS;
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
		cached_information *ci;
		const char *lookup_path = path + 1;
		int index = slash_index(lookup_path);
		if(index < 1) {
			if((ci = photoset_lookup(lookup_path))) {
				set_stbuf(stbuf, S_IFDIR | PERMISSIONS, uid, gid, ci->size, ci->time, 1);
				return SUCCESS;
			}
			else if((ci = photo_lookup("", lookup_path))) {
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

/*
 * Read directory
 */
static int ffs_readdir(const char *path, void *buf,
  fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
	int size, i;
	char **names;
	(void)offset;
	(void)fi;

	if(!strcmp(path, "/")) {
		size = get_photo_names("", &names);
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

static int ffs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
	(void)mode;
	(void)fi;
	printf("Create: %s\n", path);
	return SUCCESS;
}

static int ffs_write(const char *path, const char *buf,
  size_t size, off_t offset, struct fuse_file_info *fi) {
	(void)path;
	(void)buf;
	(void)offset;
	(void)fi;
	return size;
}

static int ffs_rename(const char *oldpath, const char *newpath) {
	const char *old_path = oldpath + 1;
	const char *new_path = newpath + 1;
	int old_index = slash_index(old_path);
	int new_index = slash_index(new_path);

	if(old_index < 1 && new_index < 1)
		set_photo_name("", old_path, new_path);
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
	(void)fi;
	return SUCCESS;
}


/**
 * Main function
**/


static struct fuse_operations flickrfs_oper = {
	.getattr = ffs_getattr,
	.readdir = ffs_readdir,
	.mkdir = ffs_mkdir,
	.open = ffs_open,
	.create = ffs_create,
	.write = ffs_write,
	.rename = ffs_rename
};

int main(int argc, char *argv[]) {
	int ret;

	if(setUser())
		return FAIL;

	if(flickr_cache_init())
		return FAIL;

	ret = fuse_main(argc, argv, &flickrfs_oper, NULL);

	flickr_cache_kill();

	return ret;
}
