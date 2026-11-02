#include <fs_malloc.h>
#include <fs_string.h>
#include <solution.h>

#include <errno.h>
#include <fuse.h>
#include <stdio.h>
#include <string.h>

static int hello_getattr(const char *path, struct stat *stbuf,
                         struct fuse_file_info *fi) {
  (void)fi;
  memset(stbuf, 0, sizeof(struct stat));

  if (strcmp(path, "/") == 0) {
    stbuf->st_mode = S_IFDIR | 0775;
    stbuf->st_nlink = 2;
    stbuf->st_uid = getuid();
    stbuf->st_gid = getgid();
    return 0;
  }

  if (strcmp(path, "/hello") == 0) {
    stbuf->st_mode = S_IFREG | 0400;
    stbuf->st_nlink = 1;
    stbuf->st_size = 32;
    stbuf->st_uid = getuid();
    stbuf->st_gid = getgid();
    return 0;
  }

  return -ENOENT;
}

static int hello_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                         off_t offset, struct fuse_file_info *fi,
                         enum fuse_readdir_flags flags) {
  (void)offset;
  (void)fi;
  (void)flags;

  if (strcmp(path, "/") != 0) {
    return -ENOENT;
  }

  filler(buf, ".", NULL, 0, 0);
  filler(buf, "..", NULL, 0, 0);
  filler(buf, "hello", NULL, 0, 0);

  return 0;
}

static int hello_open(const char *path, struct fuse_file_info *fi) {
  if (strcmp(path, "/hello") != 0) {
    return -ENOENT;
  }

  if ((fi->flags & O_ACCMODE) != O_RDONLY) {
    return -EROFS;
  }

  return 0;
}

static int hello_read(const char *path, char *buf, size_t size, off_t offset,
                      struct fuse_file_info *fi) {
  (void)fi;

  if (strcmp(path, "/hello") != 0) {
    return -ENOENT;
  }

  struct fuse_context *ctx = fuse_get_context();
  char *str = fs_xasprintf("hello, %d\n", ctx->pid);
  size_t len = strlen(str);

  if ((size_t)offset >= len) {
    fs_xfree(str);
    return 0; // EOF
  }

  if (offset + size > len) {
    size = len - offset;
  }

  memcpy(buf, str + offset, size);
  fs_xfree(str);

  return size;
}

static int hello_erofs(void) { return -EROFS; }

static const struct fuse_operations hellofs_ops = {
    .getattr = hello_getattr,
    .readdir = hello_readdir,
    .open = hello_open,
    .read = hello_read,
    .write = (void *)hello_erofs,
    .create = (void *)hello_erofs,
    .mknod = (void *)hello_erofs,
    .mkdir = (void *)hello_erofs,
    .unlink = (void *)hello_erofs,
    .rmdir = (void *)hello_erofs,
    .truncate = (void *)hello_erofs,
};

int helloworld(const char *mntp) {
  char *argv[] = {"exercise", "-f", (char *)mntp, NULL};
  return fuse_main(3, argv, &hellofs_ops, NULL);
}
