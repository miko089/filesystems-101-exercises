#include <solution.h>   // abspath(), report_path(), report_error()

#include <stdio.h>      // snprintf(), NULL
#include <stdbool.h>    // bool, true, false
#include <string.h>     // strlen(), strcmp(), strchr(), strrchr(), memcpy()
#include <errno.h>      // errno, ENOENT, ENOTDIR, ELOOP, ENAMETOOLONG
#include <fcntl.h>      // open(), openat(), O_PATH, O_DIRECTORY, O_NOFOLLOW
#include <unistd.h>     // readlink(), ssize_t
#include <sys/stat.h>   // struct stat, lstat(), S_ISLNK(), S_ISDIR()
#include <limits.h>     // PATH_MAX, NAME_MAX

#define MAX_SYMLINKS 40
#define MAX_DEPTH 2048

struct dir_level {
        char name[NAME_MAX + 1];
        int fd;
};

bool build_parent_path(struct dir_level *stack, int top, char *buf, size_t size)
{
        if (top == 0) {
                if (size < 2) return false;
                buf[0] = '/';
                buf[1] = '\0';
                return true;
        }

        size_t off = 0;
        for (int i = 1; i <= top; i++) {
                size_t nlen = strlen(stack[i].name);
                if (off + 1 + nlen + 1 >= size)
                        return false;
                buf[off++] = '/';
                memcpy(buf + off, stack[i].name, nlen);
                off += nlen;
        }

        if (off + 1 >= size)
                return false;
        buf[off++] = '/';
        buf[off] = '\0';
        return true;
}

void cleanup_stack(struct dir_level *stack, int top)
{
        for (int i = 0; i <= top; i++) {
                if (stack[i].fd >= 0) {
                        close(stack[i].fd);
                        stack[i].fd = -1;
                }
        }
}

void abspath(const char *path)
{
        if (!path || !*path) {
            report_error("/", "", ENOENT);
            return;
        }

        struct dir_level stack[MAX_DEPTH];
        int top = 0;

        int root_fd = open("/", O_PATH | O_DIRECTORY);
        if (root_fd < 0) {
            report_error("/", "", errno);
            return;
        }
        stack[0].fd = root_fd;
        stack[0].name[0] = '\0';

        char left[PATH_MAX * 2];
        size_t plen = strlen(path);
        if (plen >= sizeof(left)) {
            report_error("/", "", ENAMETOOLONG);
            close(root_fd);
            return;
        }
        memcpy(left, path, plen+1);

        char *cur = left;
        int symlink_count = 0;

        while (true) {
                while (*cur == '/')
                        cur++;
                if (!*cur) {
                        char resolved[PATH_MAX];
                        if (!build_parent_path(stack, top, resolved, sizeof(resolved))) {
                                report_error("/", "", ENAMETOOLONG);
                        } else {
                                report_path(resolved);
                        }
                        cleanup_stack(stack, top);
                        return;
                }

                char *slash = strchr(cur, '/');
                size_t clen = slash ? (size_t)(slash - cur) : strlen(cur);
                if (clen > NAME_MAX) {
                        char parent[PATH_MAX];
                        build_parent_path(stack, top, parent, sizeof(parent));
                        report_error(parent, "", ENAMETOOLONG);
                        cleanup_stack(stack, top);
                        return;
                }

                char comp[NAME_MAX + 1];
                memcpy(comp, cur, clen);
                comp[clen] = '\0';

                bool has_next = false;
                if (slash) {
                        cur = slash;
                        char *p = slash;
                        while (*p == '/')
                                p++;
                        if (*p != '\0')
                                has_next = true;
                } else {
                        cur += clen;
                }

                if (strcmp(comp, ".") == 0) {
                        continue;
                }

                if (strcmp(comp, "..") == 0) {
                        if (top > 0) {
                                close(stack[top].fd);
                                top--;
                        }
                        continue;
                }

                int child_fd = openat(stack[top].fd, comp, O_PATH | O_NOFOLLOW);
                if (child_fd < 0) {
                        char parent[PATH_MAX];
                        build_parent_path(stack, top, parent, sizeof(parent));
                        report_error(parent, comp, errno);
                        cleanup_stack(stack, top);
                        return;
                }

                struct stat st;
                if (fstat(child_fd, &st) < 0) {
                        char parent[PATH_MAX];
                        build_parent_path(stack, top, parent, sizeof(parent));
                        report_error(parent, comp, errno);
                        close(child_fd);
                        cleanup_stack(stack, top);
                        return;
                }

                if (S_ISLNK(st.st_mode)) {
                        close(child_fd);
                        if (++symlink_count > MAX_SYMLINKS) {
                                char parent[PATH_MAX];
                                build_parent_path(stack, top, parent, sizeof(parent));
                                report_error(parent, comp, ELOOP);
                                cleanup_stack(stack, top);
                                return;
                        }

                        char target[PATH_MAX];
                        ssize_t tlen = readlinkat(stack[top].fd, comp, target, sizeof(target) - 1);
                        if (tlen < 0) {
                                char parent[PATH_MAX];
                                build_parent_path(stack, top, parent, sizeof(parent));
                                report_error(parent, comp, errno);
                                cleanup_stack(stack, top);
                                return;
                        }
                        target[tlen] = '\0';

                        if (target[0] == '/') {
                                while (top > 0) {
                                        close(stack[top].fd);
                                        top--;
                                }
                        }

                        char new_left[PATH_MAX * 2];
                        bool need_slash = (*cur && target[tlen - 1] != '/' && *cur != '/');
                        int nlen = snprintf(new_left, sizeof(new_left), "%s%s%s",
                                                target, need_slash ? "/" : "", cur);
                        if (nlen < 0 || (size_t)nlen >= sizeof(new_left) || (size_t)nlen >= sizeof(left)) {
                                char parent[PATH_MAX];
                                build_parent_path(stack, top, parent, sizeof(parent));
                                report_error(parent, comp, ENAMETOOLONG);
                                cleanup_stack(stack, top);
                                return;
                        }

                        memcpy(left, new_left, (size_t)nlen + 1);
                        cur = left;
                        continue;
                }

                if (S_ISDIR(st.st_mode)) {
                        if (top + 1 >= MAX_DEPTH) {
                                char parent[PATH_MAX];
                                build_parent_path(stack, top, parent, sizeof(parent));
                                report_error(parent, comp, ENAMETOOLONG);
                                close(child_fd);
                                cleanup_stack(stack, top);
                                return;
                        }
                        top++;
                        stack[top].fd = child_fd;
                        strncpy(stack[top].name, comp, NAME_MAX);
                        stack[top].name[NAME_MAX] = '\0';
                        continue;
                }

                if (slash != NULL || has_next) {
                        char parent[PATH_MAX];
                        build_parent_path(stack, top, parent, sizeof(parent));
                        report_error(parent, comp, ENOTDIR);
                        close(child_fd);
                        cleanup_stack(stack, top);
                        return;
                }

                char parent[PATH_MAX];
                build_parent_path(stack, top, parent, sizeof(parent));
                char result[PATH_MAX * 2];
                if (top == 0) {
                        snprintf(result, sizeof(result), "/%s", comp);
                } else {
                        snprintf(result, sizeof(result), "%s%s", parent, comp);
                }

                if (strlen(result) >= PATH_MAX) {
                        report_error(parent, comp, ENAMETOOLONG);
                } else {
                        report_path(result);
                }

                close(child_fd);
                cleanup_stack(stack, top);
                return;
        }
}

