#include "fuse_local.h"

#include "link.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>

static void *fs_init(struct fuse_conn_info *conn);
static int fs_getattr(const char *path, struct stat *stbuf);
static int fs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                      off_t offset, struct fuse_file_info *fi);
static int fs_open(const char *path, struct fuse_file_info *fi);
static int fs_read(const char *path, char *buf, size_t size, off_t offset,
                   struct fuse_file_info *fi);

static struct fuse_operations fs_oper = {
    .getattr	= fs_getattr,
    .readdir	= fs_readdir,
    .open		= fs_open,
    .read		= fs_read,
    .init       = fs_init
};

int fuse_local_init(int argc, char **argv)
{
    return fuse_main(argc, argv, &fs_oper, NULL);
}

static void *fs_init(struct fuse_conn_info *conn)
{
    (void) conn;
    return NULL;
}

/** \brief return the attributes for a single file indicated by path */
static int fs_getattr(const char *path, struct stat *stbuf)
{
    int res = 0;
    memset(stbuf, 0, sizeof(struct stat));

    if (!strcmp(path, "/")) {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 1;
    } else {
        Link *link = path_to_Link(path);
        if (!link) {
            return -ENOENT;
        }
        struct timespec spec;
        spec.tv_sec = link->time;
        stbuf->st_mtim = spec;
        switch (link->type) {
            case LINK_DIR:
                stbuf->st_mode = S_IFDIR | 0755;
                stbuf->st_nlink = 1;
                break;
            case LINK_FILE:
                stbuf->st_mode = S_IFREG | 0444;
                stbuf->st_nlink = 1;
                stbuf->st_size = link->content_length;
                stbuf->st_blksize = 128*1024;
                stbuf->st_blocks = (link->content_length)/512;
                break;
            default:
                return -ENOENT;
        }
    }
    stbuf->st_uid = getuid();
    stbuf->st_gid = getgid();

    return res;
}

/** \brief read a file */
static int fs_read(const char *path, char *buf, size_t size, off_t offset,
                   struct fuse_file_info *fi)
{
    (void) fi;
    long received = path_download(path, buf, size, offset);
    return received;
}

/** \brief open a file indicated by the path */
static int fs_open(const char *path, struct fuse_file_info *fi)
{
    if (!path_to_Link(path)) {
        return -ENOENT;
    }

    if ((fi->flags & 3) != O_RDONLY) {
        return -EACCES;
    }

    return 0;
}

/** \brief read the directory indicated by the path*/
static int fs_readdir(const char *path, void *buf, fuse_fill_dir_t dir_add,
                      off_t offset, struct fuse_file_info *fi)
{
    (void) offset;
    (void) fi;

    Link *link;
    LinkTable *linktbl;

    if (!strcmp(path, "/")) {
        linktbl = ROOT_LINK_TBL;
    } else {
        link = path_to_Link(path);
        if (!link) {
            return -ENOENT;
        }
        linktbl = link->next_table;
        if (!linktbl) {
            linktbl = LinkTable_new(link->f_url);
            if(!linktbl) {
                return -ENOENT;
            }
        }
    }

    /* start adding the links */
    dir_add(buf, ".", NULL, 0);
    dir_add(buf, "..", NULL, 0);
    for (int i = 1; i < linktbl->num; i++) {
        link = linktbl->links[i];
        if (link->type != LINK_INVALID) {
            dir_add(buf, link->p_url, NULL, 0);
        }
    }

    return 0;
}
