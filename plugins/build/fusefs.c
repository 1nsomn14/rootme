// FUSE: Filesystem in USErspace
// fusefs.c - FUSE filesystem handler
// Made by @LukeGix
#define FUSE_USE_VERSION 31
#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <err.h>
#include <sys/uio.h>
#include <assert.h>
#include <stdlib.h>

#define FILE_TARGET "/lol"
unsigned int file_size = 0x10;
char file_buffer[4096];
int len = 10;

// FUSE3: getattr butuh parameter tambahan struct fuse_file_info *fi
static int FUSE_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi){
    int res = 0;
    memset(stbuf, 0, sizeof(struct stat));
    if (strcmp(path, "/") == 0) {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
    } else if (strcmp(path, FILE_TARGET) == 0) {
        stbuf->st_mode = S_IFREG | 0666;
        stbuf->st_nlink = 1;
        stbuf->st_size = file_size;
        stbuf->st_blocks = 0;
    } else {
        res = -ENOENT;
    }
    return res;
}

// FUSE3: readdir butuh parameter tambahan enum fuse_readdir_flags flags
// FUSE3: filler butuh 5 argumen (tambahan flags di akhir)
static int FUSE_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi, enum fuse_readdir_flags flags) {
    filler(buf, ".", NULL, 0, 0);
    filler(buf, "..", NULL, 0, 0);
    filler(buf, "lol", NULL, 0, 0);
    return 0;
}

static int FUSE_open(const char *path, struct fuse_file_info *fi) {
    return 0;
}

static int FUSE_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi){
    if(strcmp(path, FILE_TARGET) == 0){
        printf("[+] Pausing kernel thread for 5s\n");
        sleep(2);
        memcpy(buf, file_buffer, size);
    }
    return size;
}

static int FUSE_write(const char *path, const char *buf_to_write, size_t size, off_t offset, struct fuse_file_info *fi){
    if(strcmp(path, FILE_TARGET) == 0){
        assert(offset <= 4096 && (file_size + size) <= 4096);
        // Write in no-append mode
        if(offset == 0){
            memset(file_buffer, 0, 4096);
            file_size = 0;
        }
        memcpy(file_buffer+offset, buf_to_write, size);
        file_size += size;
    }
    return size;
}

// Just random stubs
static int FUSE_setxattr(const char *a, const char *b, const char *c, size_t d, int e){
    return 0;
}

// FUSE3: truncate butuh struct fuse_file_info *fi
static int FUSE_truncate(const char *a, off_t b, struct fuse_file_info *fi){
    return 0;
}

// FUSE3: chmod butuh struct fuse_file_info *fi
static int FUSE_chmod(const char *a, mode_t b, struct fuse_file_info *fi){
    return 0;
}

// FUSE3: chown butuh struct fuse_file_info *fi
static int FUSE_chown(const char *a, uid_t b, gid_t c, struct fuse_file_info *fi){
    return 0;
}

// FUSE3: utimens butuh struct fuse_file_info *fi
static int FUSE_utimens(const char *a, const struct timespec tv[2], struct fuse_file_info *fi){
    return 0;
}

static struct fuse_operations FUSE_ops = {
    .getattr    = FUSE_getattr,
    .readdir    = FUSE_readdir,
    .open       = FUSE_open,
    .read       = FUSE_read,
    .write      = FUSE_write,
    .setxattr   = FUSE_setxattr,
    .truncate   = FUSE_truncate,
    .chmod      = FUSE_chmod,
    .chown      = FUSE_chown,
    .utimens    = FUSE_utimens
};

int main(int argc, char *argv[]) {
    // Initialization of the filesystem
    memset(file_buffer, 'A', sizeof(file_buffer));
    return fuse_main(argc, argv, &FUSE_ops, NULL);
}