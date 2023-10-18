/*
 * This code is provided solely for the personal and private use of students
 * taking the CSC369H course at the University of Toronto. Copying for purposes
 * other than this use is expressly prohibited. All forms of distribution of
 * this code, including but not limited to public repositories on GitHub,
 * GitLab, Bitbucket, or any other online platform, whether as given or with
 * any changes, are expressly prohibited.
 *
 * Authors: Alexey Khrabrov, Karen Reid, Angela Demke Brown
 *
 * All of the files in this directory and all subdirectories are:
 * Copyright (c) 2022 Angela Demke Brown
 */

/**
 * CSC369 Assignment 5 - vsfs driver implementation.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

// Using 2.9.x FUSE API
#define FUSE_USE_VERSION 29
#include <fuse.h>

#include "vsfs.h"
#include "fs_ctx.h"
#include "options.h"
#include "util.h"
#include "bitmap.h"
#include "map.h"

//NOTE: All path arguments are absolute paths within the vsfs file system and
// start with a '/' that corresponds to the vsfs root directory.
//
// For example, if vsfs is mounted at "/tmp/my_userid", the path to a
// file at "/tmp/my_userid/dir/file" (as seen by the OS) will be
// passed to FUSE callbacks as "/dir/file".
//
// Paths to directories (except for the root directory - "/") do not end in a
// trailing '/'. For example, "/tmp/my_userid/dir/" will be passed to
// FUSE callbacks as "/dir".


/**
 * Initialize the file system.
 *
 * Called when the file system is mounted. NOTE: we are not using the FUSE
 * init() callback since it doesn't support returning errors. This function must
 * be called explicitly before fuse_main().
 *
 * @param fs    file system context to initialize.
 * @param opts  command line options.
 * @return      true on success; false on failure.
 */
static bool vsfs_init(fs_ctx *fs, vsfs_opts *opts)
{
	size_t size;
	void *image;
	
	// Nothing to initialize if only printing help
	if (opts->help) {
		return true;
	}

	// Map the disk image file into memory
	image = map_file(opts->img_path, VSFS_BLOCK_SIZE, &size);
	if (image == NULL) {
		return false;
	}

	return fs_ctx_init(fs, image, size);
}

/**
 * Cleanup the file system.
 *
 * Called when the file system is unmounted. Must cleanup all the resources
 * created in vsfs_init().
 */
static void vsfs_destroy(void *ctx)
{
	fs_ctx *fs = (fs_ctx*)ctx;
	if (fs->image) {
		munmap(fs->image, fs->size);
		fs_ctx_destroy(fs);
	}
}

/** Get file system context. */
static fs_ctx *get_fs(void)
{
	return (fs_ctx*)fuse_get_context()->private_data;
}

// HELPER: find pointer to inode given inode number
vsfs_inode * inode_location(vsfs_ino_t ino){
	fs_ctx *fs = get_fs();
	return &(fs->itable[ino]);
}

vsfs_dentry * dentry_location(unsigned long block_num){
	fs_ctx *fs = get_fs();
	return (vsfs_dentry *) (fs->image + block_num * VSFS_BLOCK_SIZE);
}

// uint32_t dentry_block_num(vsfs_dentry * dentry){
// 	fs_ctx *fs = get_fs();
// 	return (dentry - (vsfs_dentry *)(fs->image)) / VSFS_BLOCK_SIZE;
// }

// bool occupied_data_block(unsigned long block_num){
// 	fs_ctx *fs = get_fs();
// 	uint32_t nblks = fs->size / VSFS_BLOCK_SIZE;
// 	return (fs->sb->data_region <= block_num) && bitmap_isset(fs->dbmap, nblks, block_num);
// }


/* Returns the inode number for the element at the end of the path
 * if it exists.  If there is any error, return -1.
 * Possible errors include:
 *   - The path is not an absolute path
 *   - An element on the path cannot be found
 */
static int path_lookup(const char *path, vsfs_inode **ino, vsfs_dentry **den) {
	if(path[0] != '/') {
		fprintf(stderr, "Not an absolute path\n");
		return -ENOSYS;
	} 

	// TODO: complete this function and any helper functions
	if (strcmp(path, "/") == 0) {
		*ino = inode_location(VSFS_ROOT_INO);
		*den = dentry_location(0);
		return 0;
	}

	fs_ctx *fs = get_fs();
	(void) fs;

	// root directory i node
	vsfs_inode * root = inode_location(VSFS_ROOT_INO);
	// default return value

	vsfs_dentry * dentry;
	// # of dentries per block
	int dentry_per_block = VSFS_BLOCK_SIZE / sizeof(vsfs_dentry);

	// for each i_direct in root inode, try all dentries in the block


	uint32_t * indirect_base = (uint32_t *) (fs->image + root->i_indirect * VSFS_BLOCK_SIZE);

	for (vsfs_blk_t i = 0; i < root->i_blocks; i++){
		if (i < VSFS_NUM_DIRECT){
			// direct case
			dentry = dentry_location(root->i_direct[i]);
			for (int j = 0; j < dentry_per_block; j++){
				if (strcmp(dentry[j].name, path + 1) == 0){
					*ino = inode_location(dentry[j].ino);
					*den = &(dentry[j]);
					return dentry[j].ino;
				}
			}
		} else{
			// indirect case
			dentry = dentry_location(indirect_base[i - VSFS_NUM_DIRECT]);
			for (int j = 0; j < dentry_per_block; j++){
				if (strcmp(dentry[j].name, path + 1) == 0){
					*ino = inode_location(dentry[j].ino);
					*den = &(dentry[j]);
					return dentry[j].ino;
				}	
			}

		}
	}
	// find inode block fo the indirect data pointer, 
	//and do pass for all inodes in this block

	return -ENOSYS;

}

/**
 * Get file system statistics.
 *
 * Implements the statvfs() system call. See "man 2 statvfs" for details.
 * The f_bfree and f_bavail fields should be set to the same value.
 * The f_ffree and f_favail fields should be set to the same value.
 * The following fields can be ignored: f_fsid, f_flag.
 * All remaining fields are required.
 *
 * Errors: none
 *
 * @param path  path to any file in the file system. Can be ignored.
 * @param st    pointer to the struct statvfs that receives the result.
 * @return      0 on success; -errno on error.
 */
static int vsfs_statfs(const char *path, struct statvfs *st)
{
	(void)path;// unused
	fs_ctx *fs = get_fs();
	vsfs_superblock *sb = fs->sb; /* Get ptr to superblock from context */
	
	memset(st, 0, sizeof(*st));
	st->f_bsize   = VSFS_BLOCK_SIZE;   /* Filesystem block size */
	st->f_frsize  = VSFS_BLOCK_SIZE;   /* Fragment size */
	// The rest of required fields are filled based on the information 
	// stored in the superblock.
	st->f_blocks = sb->num_blocks;     /* Size of fs in f_frsize units */
	st->f_bfree  = sb->free_blocks;    /* Number of free blocks */
	st->f_bavail = sb->free_blocks;    /* Free blocks for unpriv users */
	st->f_files  = sb->num_inodes;     /* Number of inodes */
	st->f_ffree  = sb->free_inodes;    /* Number of free inodes */
	st->f_favail = sb->free_inodes;    /* Free inodes for unpriv users */

	st->f_namemax = VSFS_NAME_MAX;     /* Maximum filename length */

	return 0;
}

/**
 * Get file or directory attributes.
 *
 * Implements the lstat() system call. See "man 2 lstat" for details.
 * The following fields can be ignored: st_dev, st_ino, st_uid, st_gid, st_rdev,
 *                                      st_blksize, st_atim, st_ctim.
 * All remaining fields are required.
 *
 * NOTE: the st_blocks field is measured in 512-byte units (disk sectors);
 *       it should include any metadata blocks that are allocated to the 
 *       inode (for vsfs, that is the indirect block). 
 *
 * NOTE2: the st_mode field must be set correctly for files and directories.
 *
 * Errors:
 *   ENAMETOOLONG  the path or one of its components is too long.
 *   ENOENT        a component of the path does not exist.
 *   ENOTDIR       a component of the path prefix is not a directory.
 *
 * @param path  path to a file or directory.
 * @param st    pointer to the struct stat that receives the result.
 * @return      0 on success; -errno on error;
 */
static int vsfs_getattr(const char *path, struct stat *st)
{
	if (strlen(path) >= VSFS_PATH_MAX) return -ENAMETOOLONG;
	fs_ctx *fs = get_fs();
	(void) fs;

	memset(st, 0, sizeof(*st));

	//NOTE: This is just a placeholder that allows the file system to be 
	//      mounted without errors.
	//      You should remove this from your implementation.
	// create a inode to store the inode corresponding to the path

	vsfs_inode * res_inode;
	vsfs_dentry * res_dentry;
	// find such inode and return the inode number
	int res_inode_num = path_lookup(path, &res_inode, &res_dentry);
	(void) res_dentry;
	if (res_inode_num < 0){
		return -ENOENT;
	}

	st->st_mode = res_inode->i_mode;
	st->st_nlink = res_inode->i_nlink;
	st->st_size = res_inode->i_size;
	st->st_blocks = res_inode->i_blocks * VSFS_BLOCK_SIZE / 512;
	st->st_mtim = res_inode->i_mtime;

	if (res_inode->i_blocks > VSFS_NUM_DIRECT){
		st->st_blocks += (VSFS_BLOCK_SIZE / 512);
	}

	return 0;
}

/**
 * Read a directory.
 *
 * Implements the readdir() system call. Should call filler(buf, name, NULL, 0)
 * for each directory entry. See fuse.h in libfuse source code for details.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a directory.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a filler() call failed).
 *
 * @param path    path to the directory.
 * @param buf     buffer that receives the result.
 * @param filler  function that needs to be called for each directory entry.
 *                Pass 0 as offset (4th argument). 3rd argument can be NULL.
 * @param offset  unused.
 * @param fi      unused.
 * @return        0 on success; -errno on error.
 */
static int vsfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                        off_t offset, struct fuse_file_info *fi)
{
	(void)offset;// unused
	(void)fi;// unused
	fs_ctx *fs = get_fs();

	vsfs_inode * root = inode_location(VSFS_ROOT_INO);
	// default return value

	vsfs_dentry * dentry;
	// # of dentries per block
	int dentry_per_block = VSFS_BLOCK_SIZE / sizeof(vsfs_dentry);

	// for each i_direct in root inode, try all dentries in the block


	uint32_t * indirect_base = (uint32_t *) (fs->image + root->i_indirect * VSFS_BLOCK_SIZE);

	for (vsfs_blk_t i = 0; i < root->i_blocks; i++){
		if (i < VSFS_NUM_DIRECT){
			// direct case
			dentry = dentry_location(root->i_direct[i]);
			for (int j = 0; j < dentry_per_block; j++){
				if (dentry[j].ino == VSFS_INO_MAX){
					continue;
				}
				if (filler(buf, dentry[j].name, NULL, 0) != 0){
					return -ENOMEM;
				}	
			}
		} else{
			// indirect case
			dentry = dentry_location(indirect_base[i - VSFS_NUM_DIRECT]);
			for (int j = 0; j < dentry_per_block; j++){
				if (dentry[j].ino == VSFS_INO_MAX){
					continue;
				}
				if (filler(buf, dentry[j].name, NULL, 0) != 0){
					return -ENOMEM;
				}	
			}

		}
	}

	(void) path;
	//TODO: lookup the directory inode for given path and iterate through its
	// directory entries
	return 0;
}


/**
 * Create a directory.
 *
 * Implements the mkdir() system call.
 *
 * You do NOT need to implement this function. 
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" doesn't exist.
 *   The parent directory of "path" exists and is a directory.
 *   "path" and its components are not too long.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a malloc() call failed).
 *   ENOSPC  not enough free space in the file system.
 *
 * @param path  path to the directory to create.
 * @param mode  file mode bits.
 * @return      0 on success; -errno on error.
 */
static int vsfs_mkdir(const char *path, mode_t mode)
{
	mode = mode | S_IFDIR;
	fs_ctx *fs = get_fs();

	//OMIT: create a directory at given path with given mode
	(void)path;
	(void)mode;
	(void)fs;
	return -ENOSYS;
}

/**
 * Remove a directory.
 *
 * Implements the rmdir() system call.
 *
 * You do NOT need to implement this function. 
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a directory.
 *
 * Errors:
 *   ENOTEMPTY  the directory is not empty.
 *
 * @param path  path to the directory to remove.
 * @return      0 on success; -errno on error.
 */
static int vsfs_rmdir(const char *path)
{
	fs_ctx *fs = get_fs();

	//OMIT: remove the directory at given path (only if it's empty)
	(void)path;
	(void)fs;
	return -ENOSYS;
}

/**
 * Create a file.
 *
 * Implements the open()/creat() system call.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" doesn't exist.
 *   The parent directory of "path" exists and is a directory.
 *   "path" and its components are not too long.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a malloc() call failed).
 *   ENOSPC  not enough free space in the file system.
 *
 * @param path  path to the file to create.
 * @param mode  file mode bits.
 * @param fi    unused.
 * @return      0 on success; -errno on error.
 */
static int vsfs_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
	(void)fi;// unused
	assert(S_ISREG(mode));
	fs_ctx *fs = get_fs();

	//TODO: create a file at given path with given mode
	(void) path;
	// find first unused inode --> break if all inodes full
	uint32_t nblks = fs->size / VSFS_BLOCK_SIZE;
	uint32_t ino_index;
	if (fs->sb->free_inodes == 0){
		// no more inodes == free_inodes = 0
		return -ENOSPC;
	}
	bitmap_alloc(fs->ibmap, nblks, &ino_index);

	// find empty dentry

	vsfs_inode * root = inode_location(VSFS_ROOT_INO);
	// default return value

	vsfs_dentry * dentry;
	// # of dentries per block
	int dentry_per_block = VSFS_BLOCK_SIZE / sizeof(vsfs_dentry);

	// for existing data blocks, try to find empty dentries

	uint32_t * indirect_base = (uint32_t *) (fs->image + root->i_indirect * VSFS_BLOCK_SIZE);

	for (vsfs_blk_t i = 0; i < root->i_blocks; i++){
		if (i < VSFS_NUM_DIRECT){
			// direct case
			dentry = dentry_location(root->i_direct[i]);
			for (int j = 0; j < dentry_per_block; j++){
				//fprintf(stderr, "path_lookup: i = %d, j = %d, ino = %d\n", i, j, dentry[j].ino);	
				if (dentry[j].ino == VSFS_INO_MAX){
					dentry[j].ino = ino_index;
					strncpy(dentry[j].name, path + 1, VSFS_NAME_MAX);
					goto allocated;
				}	
			}
		} else{
			// indirect case
			dentry = dentry_location(indirect_base[i - VSFS_NUM_DIRECT]);
			for (int j = 0; j < dentry_per_block; j++){
				//fprintf(stderr, "path_lookup indirect: i = %d, j = %d, ino = %d\n", i, j, dentry[j].ino);	
				if (dentry[j].ino == VSFS_INO_MAX){
					dentry[j].ino = ino_index;
					strncpy(dentry[j].name, path + 1, VSFS_NAME_MAX);
					goto allocated;
				}	
			}
		}
	}

	// allocate new datablock if all existing data block full
	uint32_t dentry_index;	
	if (root->i_blocks < VSFS_NUM_DIRECT){
		// search in direct blocks
		// if curr node full, go to next node
		// new block direct
		if (bitmap_alloc(fs->dbmap, nblks, &dentry_index) != 0){
			return -ENOSPC;
		}
		bitmap_set(fs->dbmap, nblks, dentry_index, true);
		root->i_direct[root->i_blocks] = dentry_index;
		dentry = dentry_location(dentry_index);
		dentry[0].ino = ino_index;
		strncpy(dentry[0].name, path + 1, VSFS_NAME_MAX);	

	}else if (root->i_blocks == VSFS_NUM_DIRECT){
		// new block indirect
		uint32_t indirect_block_index;
		if (bitmap_alloc(fs->dbmap, nblks, &indirect_block_index) != 0){
			return -ENOSPC;
		}

		if (bitmap_alloc(fs->dbmap, nblks, &dentry_index) != 0){
			return -ENOSPC;
		}
		bitmap_set(fs->dbmap, nblks, indirect_block_index, true);
		bitmap_set(fs->dbmap, nblks, dentry_index, true);
		memset(fs->image + indirect_block_index * VSFS_BLOCK_SIZE, 0 , VSFS_BLOCK_SIZE);

		root->i_indirect = indirect_block_index;
		uint32_t * indirect_base = (uint32_t *) (fs->image + root->i_indirect * VSFS_BLOCK_SIZE);
		indirect_base[0] = dentry_index;

		dentry = dentry_location(dentry_index);
		dentry[0].ino = ino_index;
		strncpy(dentry[0].name, path + 1, VSFS_NAME_MAX);	
		fs->sb->free_blocks --;	

	} else if (root->i_blocks < 1029){
		//search in indirect blocks
		uint32_t * indirect_base = (uint32_t *) (fs->image + root->i_indirect * VSFS_BLOCK_SIZE);
		if (bitmap_alloc(fs->dbmap, nblks, &dentry_index) != 0){
			return -ENOSPC;
		};
		bitmap_set(fs->dbmap, nblks, dentry_index, true);

		// update linkage in indirect block
		indirect_base[root->i_blocks - VSFS_NUM_DIRECT] = dentry_index;


		dentry = dentry_location(dentry_index);
		dentry[0].ino = ino_index;
		strncpy(dentry[0].name, path + 1, VSFS_NAME_MAX);

	} else{
		// all dentry full
		return -ENOSPC;
	}

	for (int i = 1; i < dentry_per_block; i++){
		dentry[i].ino = VSFS_INO_MAX;
	}

	//update info if new block
	root->i_blocks ++;
	root->i_size += VSFS_BLOCK_SIZE;
	fs->sb->free_blocks --;

allocated:

	// mark that block as allocated, and update sb
	bitmap_set(fs->ibmap, nblks, ino_index, true);
	fs->sb->free_inodes --;
	
	// find location of new inode and initialize
	vsfs_inode * new = inode_location(ino_index);
	memset(new, 0, sizeof(vsfs_inode));
	new->i_mode = mode;
	new->i_nlink = 1;
	new->i_blocks = 0;
	new->i_size = 0;
	clock_gettime(CLOCK_REALTIME, &(new->i_mtime));
	clock_gettime(CLOCK_REALTIME, &(root->i_mtime));
	return 0;
}

/**
 * Remove a file.
 *
 * Implements the unlink() system call.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a file.
 *
 * Errors: none
 *
 * @param path  path to the file to remove.
 * @return      0 on success; -errno on error.
 */
static int vsfs_unlink(const char *path)
{
	fs_ctx *fs = get_fs();

	//TODO: remove the file at given path
	(void)path;
	(void)fs;

	// IMPORTANT: WE ALSO WANT TO KNOW THE DENTRY OF TEH PATH INODE
	vsfs_inode * res_inode;
	vsfs_dentry * target_dentry;
	int res_inode_num = path_lookup(path, &res_inode, &target_dentry);
	// must be a valid inode with the path
	assert(res_inode_num >= 0);


	uint32_t * indirect_base = (uint32_t *) (fs->image + res_inode->i_indirect * VSFS_BLOCK_SIZE);

	uint32_t nblks = fs->size / VSFS_BLOCK_SIZE;

	for (vsfs_blk_t i = 0; i < res_inode->i_blocks; i++){
		if (i < VSFS_NUM_DIRECT){
			// direct case
			memset(fs->image + res_inode->i_direct[i] * VSFS_BLOCK_SIZE, 0, VSFS_BLOCK_SIZE);
			fs->sb->free_blocks ++;
			bitmap_free(fs->dbmap, nblks, res_inode->i_direct[i]);
		} else{
			// indirect case
			memset(fs->image + indirect_base[i - VSFS_NUM_DIRECT] * VSFS_BLOCK_SIZE, 0, VSFS_BLOCK_SIZE);
			fs->sb->free_blocks ++;
			bitmap_free(fs->dbmap, nblks, indirect_base[i - VSFS_NUM_DIRECT]);
		}
	}

	if (res_inode->i_blocks > 5){
		memset(fs->image + res_inode->i_indirect * VSFS_BLOCK_SIZE, 0, VSFS_BLOCK_SIZE);
		fs->sb->free_blocks ++;
		bitmap_free(fs->dbmap, nblks, res_inode->i_indirect);
	}

	// clear inode
	bitmap_set(fs->ibmap, nblks, res_inode_num, false);
	fs->sb->free_inodes ++;
	memset(inode_location(res_inode_num), 0, sizeof(vsfs_inode));
	
	// find location of new inode and initialize

	vsfs_inode * root = inode_location(VSFS_ROOT_INO);
	clock_gettime(CLOCK_REALTIME, &(root->i_mtime));

	// clear dentry of root
	target_dentry->ino = VSFS_INO_MAX;
	memset(target_dentry->name, 0, VSFS_NAME_MAX);

	return 0;
}


/**
 * Change the modification time of a file or directory.
 *
 * Implements the utimensat() system call. See "man 2 utimensat" for details.
 *
 * NOTE: You only need to implement the setting of modification time (mtime).
 *       Timestamp modifications are not recursive. 
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists.
 *
 * Errors: none
 *
 * @param path   path to the file or directory.
 * @param times  timestamps array. See "man 2 utimensat" for details.
 * @return       0 on success; -errno on failure.
 */
static int vsfs_utimens(const char *path, const struct timespec times[2])
{
	fs_ctx *fs = get_fs();
	vsfs_inode *ino = NULL;
	
	//TODO: update the modification timestamp (mtime) in the inode for given
	// path with either the time passed as argument or the current time,
	// according to the utimensat man page
	(void)path;
	(void)fs;
	(void)ino;
	
	// 0. Check if there is actually anything to be done.
	if (times[1].tv_nsec == UTIME_OMIT) {
		// Nothing to do.
		return 0;
	}

	// 1. TODO: Find the inode for the final component in path
	vsfs_dentry * res_dentry;
	path_lookup(path, &ino, &res_dentry);
	(void) res_dentry;

	
	// 2. Update the mtime for that inode.
	//    This code is commented out to avoid failure until you have set
	//    'ino' to point to the inode structure for the inode to update.
	if (times[1].tv_nsec == UTIME_NOW) {
		if (clock_gettime(CLOCK_REALTIME, &(ino->i_mtime)) != 0) {
			// clock_gettime should not fail, unless you give it a
			// bad pointer to a timespec.
			assert(false);
		}
	} else {
		ino->i_mtime = times[1];
	}

	return 0;
}

/**
 * Change the size of a file.
 *
 * Implements the truncate() system call. Supports both extending and shrinking.
 * If the file is extended, the new uninitialized range at the end must be
 * filled with zeros.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a file.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a malloc() call failed).
 *   ENOSPC  not enough free space in the file system.
 *   EFBIG   write would exceed the maximum file size. 
 *
 * @param path  path to the file to set the size.
 * @param size  new file size in bytes.
 * @return      0 on success; -errno on error.
 */
static int vsfs_truncate(const char *path, off_t size)
{
	fs_ctx *fs = get_fs();

	//TODO: set new file size, possibly "zeroing out" the uninitialized range
	if (size > VSFS_BLOCK_SIZE * 1029){
		return -EFBIG;
	}

	uint32_t nblks = fs->size / VSFS_BLOCK_SIZE;
	vsfs_inode * file_inode;
	vsfs_dentry * file_dentry;
	path_lookup(path, &file_inode, &file_dentry);
	(void) file_dentry;

	if (size == (off_t) file_inode->i_size){
		return 0;
	}

	unsigned long old_block_count = file_inode->i_blocks;
	unsigned long old_block_end_pos = (file_inode->i_size - 1) % VSFS_BLOCK_SIZE;
	if (file_inode->i_size == 0){
		old_block_end_pos = -1;
	}

	// same as div_round_up
	unsigned long new_block_count = (unsigned long) ((size + VSFS_BLOCK_SIZE - 1) / VSFS_BLOCK_SIZE);

	unsigned long new_block_end_pos = (size - 1) % VSFS_BLOCK_SIZE;



	// fprintf(stderr, "old_count = %ld, old_end_pos = %ld, new_count = %ld, new_end_pos = %ld\n", old_block_count, old_block_end_pos, new_block_count, new_block_end_pos);
	// fprintf(stderr, "freeblocks  = %d\n", fs->sb->free_blocks);
	
	if (size < (off_t) file_inode->i_size){
		// shrink
		for (unsigned long i = 0; i < old_block_count; i++){
			if (i < VSFS_NUM_DIRECT){
				// direct case
				if (i < new_block_count - 1){
					continue;
				} else if (i < new_block_count){
					// zero out some
					memset(fs->image + file_inode->i_direct[i] * VSFS_BLOCK_SIZE + new_block_end_pos + 1, 0, VSFS_BLOCK_SIZE - new_block_end_pos - 1);
				} else{
					// de allocate all
					memset(fs->image + file_inode->i_direct[i] * VSFS_BLOCK_SIZE, 0, VSFS_BLOCK_SIZE);
					fs->sb->free_blocks ++;
					bitmap_free(fs->dbmap, nblks, file_inode->i_direct[i]);
					file_inode->i_direct[i] = 0;
				}
			} else{
				// indirect case
				uint32_t * indirect_base = (uint32_t *) (fs->image + file_inode->i_indirect * VSFS_BLOCK_SIZE);
				if (i < new_block_count - 1){
					continue;
				} else if (i < new_block_count){
					// zero out some
					memset(fs->image + indirect_base[i - VSFS_NUM_DIRECT] * VSFS_BLOCK_SIZE + new_block_end_pos + 1, 0, VSFS_BLOCK_SIZE - new_block_end_pos - 1);
				} else{
					// de allocate all
					memset(fs->image + indirect_base[i - VSFS_NUM_DIRECT] * VSFS_BLOCK_SIZE, 0, VSFS_BLOCK_SIZE);
					fs->sb->free_blocks ++;
					bitmap_free(fs->dbmap, nblks, indirect_base[i - VSFS_NUM_DIRECT]);
					indirect_base[i- VSFS_NUM_DIRECT] = 0;
				}	
			}
		}
		if (old_block_count > 5 && new_block_count <= 5){
			memset(fs->image + file_inode->i_indirect * VSFS_BLOCK_SIZE, 0, VSFS_BLOCK_SIZE);
			fs->sb->free_blocks ++;
			bitmap_free(fs->dbmap, nblks,  file_inode->i_indirect );
			file_inode->i_indirect  = 0;		
		}
		
	} else{
		// extend

		// need more new blocks, so no space
		if ((new_block_count - old_block_count + 1 > fs->sb->free_blocks) && old_block_count <= 5 && new_block_count > 5){
			return -ENOSPC;
		} else if (new_block_count - old_block_count > fs->sb->free_blocks){
			return -ENOSPC;
		}

		if (file_inode->i_size == 0){
			uint32_t index;
			assert(bitmap_alloc(fs->dbmap, nblks, &index) == 0);
			bitmap_set(fs->dbmap, nblks, index, true);
			file_inode->i_direct[0] = index;
			fs->sb->free_blocks --;
			// memset 0 for entire block
			memset(fs->image + index * VSFS_BLOCK_SIZE, 0, VSFS_BLOCK_SIZE);
		}

		for (unsigned long i = 0; i < nblks; i++){
			if (!bitmap_isset(fs->dbmap, nblks, i)){
				memset(fs->image + i * VSFS_BLOCK_SIZE, 0, VSFS_BLOCK_SIZE);
			}
		}


		bool indirect_initialized = false;
		if (old_block_count > 5){
			indirect_initialized = true;
		}
		for (unsigned long i = 0; i < new_block_count; i++){
			if (i < VSFS_NUM_DIRECT){
				// direct case
				if (i < old_block_count - 1){
					continue;
				} else if (i < old_block_count){
					// memset 0 latter portion
					memset(fs->image + file_inode->i_direct[i] * VSFS_BLOCK_SIZE + old_block_end_pos + 1, 0, VSFS_BLOCK_SIZE - old_block_end_pos - 1);
				} else{
					// set bit map and connect
					if (i == 0 && file_inode->i_size == 0){
						continue;
					}
					uint32_t index;
					assert(bitmap_alloc(fs->dbmap, nblks, &index) == 0);
					bitmap_set(fs->dbmap, nblks, index, true);
					file_inode->i_direct[i] = index;
					fs->sb->free_blocks --;
					// memset 0 for entire block
					memset(fs->image + index * VSFS_BLOCK_SIZE, 0, VSFS_BLOCK_SIZE);
				}
			} else{
				// indirect case
				if (!indirect_initialized){
					uint32_t new_indirect_block;
					assert(bitmap_alloc(fs->dbmap, nblks, &new_indirect_block) == 0);
					bitmap_set(fs->dbmap, nblks, new_indirect_block, true);
					file_inode->i_indirect = new_indirect_block;
					fs->sb->free_blocks --;
					indirect_initialized = true;
				}

				uint32_t * indirect_base = (uint32_t *) (fs->image + file_inode->i_indirect * VSFS_BLOCK_SIZE);
				if (i < old_block_count - 1){
					continue;
				} else if (i < old_block_count){
					// memset 0 latter portion
					memset(fs->image + indirect_base[i - VSFS_NUM_DIRECT] * VSFS_BLOCK_SIZE + old_block_end_pos + 1, 0, VSFS_BLOCK_SIZE - old_block_end_pos - 1);
				} else{
					// set bit map and connect
					uint32_t index;
					assert(bitmap_alloc(fs->dbmap, nblks, &index) == 0);
					bitmap_set(fs->dbmap, nblks, index, true);
					indirect_base[i - VSFS_NUM_DIRECT] = index;
					fs->sb->free_blocks --;
					// memset 0 for entire block
					memset(fs->image + index * VSFS_BLOCK_SIZE, 0, VSFS_BLOCK_SIZE);
				}	
			}
		}
	}

	file_inode->i_size = size;
	file_inode->i_blocks = new_block_count;
	clock_gettime(CLOCK_REALTIME, &(file_inode->i_mtime));
	return 0;
}


/**
 * Read data from a file.
 *
 * Implements the pread() system call. Must return exactly the number of bytes
 * requested except on EOF (end of file). Reads from file ranges that have not
 * been written to must return ranges filled with zeros. You can assume that the
 * byte range from offset to offset + size is contained within a single block.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a file.
 *
 * Errors: none
 *
 * @param path    path to the file to read from.
 * @param buf     pointer to the buffer that receives the data.
 * @param size    buffer size (number of bytes requested).
 * @param offset  offset from the beginning of the file to read from.
 * @param fi      unused.
 * @return        number of bytes read on success; 0 if offset is beyond EOF;
 *                -errno on error.
 */
static int vsfs_read(const char *path, char *buf, size_t size, off_t offset,
                     struct fuse_file_info *fi)
{
	(void)fi;// unused
	fs_ctx *fs = get_fs();

	//TODO: read data from the file at given offset into the buffer

	vsfs_inode * file_inode;
	vsfs_dentry * file_dentry;
	path_lookup(path, &file_inode, &file_dentry);

	// start of read later than end of file --> 0 bytes read
	if (offset >= (off_t) file_inode->i_size){
		return 0;
	}

	unsigned long block_idx = (unsigned long) offset / VSFS_BLOCK_SIZE;
	unsigned long block_pos = offset % VSFS_BLOCK_SIZE;

	// read less than size if reach EOF
	unsigned long read_length = (unsigned long) size;
	if ((block_idx + 1 == file_inode->i_blocks) && (VSFS_BLOCK_SIZE - block_pos < read_length)){
		// last block AND leftover < bufsize
		read_length = VSFS_BLOCK_SIZE - block_pos;
	}

	// find actual data block number
	unsigned long block_num;
	if (block_idx < VSFS_NUM_DIRECT){
		// data in direct block
		block_num = file_inode->i_direct[block_idx];
	} else{
		// data in indirect block
		uint32_t * indirect_base = (uint32_t *) (fs->image + file_inode->i_indirect * VSFS_BLOCK_SIZE);
		block_num = (unsigned long) indirect_base[block_idx - VSFS_NUM_DIRECT];
	}

	// read
	char * read_start = fs->image + block_num * VSFS_BLOCK_SIZE + block_pos;
	memcpy(buf, read_start, read_length);
	(void) file_dentry;
	return read_length;
}

/**
 * Write data to a file.
 *
 * Implements the pwrite() system call. Must return exactly the number of bytes
 * requested except on error. If the offset is beyond EOF (end of file), the
 * file must be extended. If the write creates a "hole" of uninitialized data,
 * the new uninitialized range must filled with zeros. You can assume that the
 * byte range from offset to offset + size is contained within a single block.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a file.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a malloc() call failed).
 *   ENOSPC  not enough free space in the file system.
 *   EFBIG   write would exceed the maximum file size 
 *
 * @param path    path to the file to write to.
 * @param buf     pointer to the buffer containing the data.
 * @param size    buffer size (number of bytes requested).
 * @param offset  offset from the beginning of the file to write to.
 * @param fi      unused.
 * @return        number of bytes written on success; -errno on error.
 */
static int vsfs_write(const char *path, const char *buf, size_t size,
                      off_t offset, struct fuse_file_info *fi)
{
	(void)fi;// unused
	fs_ctx *fs = get_fs();

	//TODO: write data from the buffer into the file at given offset, possibly
	// "zeroing out" the uninitialized range

	vsfs_inode * file_inode;
	vsfs_dentry * file_dentry;
	path_lookup(path, &file_inode, &file_dentry);
	(void) file_dentry;

	// file offset too large to write
	if (size + offset > file_inode->i_size){
		int ret = vsfs_truncate(path, size + offset);
		if (ret < 0){
			return ret;
		}
	}

	clock_gettime(CLOCK_REALTIME, &(file_inode->i_mtime));

	unsigned long block_idx = (unsigned long) offset / VSFS_BLOCK_SIZE;
	unsigned long block_pos = offset % VSFS_BLOCK_SIZE;

	// find actual data block number
	unsigned long block_num;
	if (block_idx < VSFS_NUM_DIRECT){
		// data in direct block
		block_num = file_inode->i_direct[block_idx];
	} else{
		// data in indirect block
		uint32_t * indirect_base = (uint32_t *) (fs->image + file_inode->i_indirect * VSFS_BLOCK_SIZE);
		block_num = (unsigned long) indirect_base[block_idx - VSFS_NUM_DIRECT];
	}

	// write
	char * write_start = fs->image + block_num * VSFS_BLOCK_SIZE + block_pos;
	memcpy(write_start, buf, size);

	return size;
}


static struct fuse_operations vsfs_ops = {
	.destroy  = vsfs_destroy,
	.statfs   = vsfs_statfs,
	.getattr  = vsfs_getattr,
	.readdir  = vsfs_readdir,
	.mkdir    = vsfs_mkdir,
	.rmdir    = vsfs_rmdir,
	.create   = vsfs_create,
	.unlink   = vsfs_unlink,
	.utimens  = vsfs_utimens,
	.truncate = vsfs_truncate,
	.read     = vsfs_read,
	.write    = vsfs_write,
};

int main(int argc, char *argv[])
{
	vsfs_opts opts = {0};// defaults are all 0
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
	if (!vsfs_opt_parse(&args, &opts)) return 1;
	fs_ctx fs = {0};
	if (!vsfs_init(&fs, &opts)) {
		fprintf(stderr, "Failed to mount the file system\n");
		return 1;
	}

	return fuse_main(args.argc, args.argv, &vsfs_ops, &fs);
}
