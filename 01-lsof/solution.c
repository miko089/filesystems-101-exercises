#include <solution.h>   // lsof(), report_file(), report_error()
#include <fs_malloc.h>  // fs_xfree()
#include <fs_string.h>  // fs_xasprintf()

#include <stdio.h>      // NULL
#include <stdlib.h>     // atoi()
#include <stdbool.h>    // bool, true, false
#include <ctype.h>      // isdigit()
#include <errno.h>      // errno
#include <string.h>     // strcmp()

#include <dirent.h>     // DIR, struct dirent, opendir(), readdir(), closedir(), dirfd(), DT_UNKNOWN, DT_DIR
#include <unistd.h>     // readlink(), pid_t
#include <limits.h>     // PATH_MAX
#include <fcntl.h>      // AT_SYMLINK_NOFOLLOW
#include <sys/stat.h>   // struct stat, fstatat(), S_ISDIR()

#define PROC_DIR "/proc"

bool is_pid_dir(int proc_dfd, const struct dirent *entry) 
{
        const char *p = entry->d_name;
        if (*p == '\0') {
            return false;
        }
        for (; *p; ++p) {
            if (!isdigit((unsigned char)*p)) {
                return false;
            }
        }

        if (entry->d_type != DT_UNKNOWN) {
            return entry->d_type == DT_DIR;
        }

        struct stat st;
        if (fstatat(proc_dfd, entry->d_name, &st, AT_SYMLINK_NOFOLLOW) != 0) {
                return false;
        }

        return S_ISDIR(st.st_mode);
}

void lsof(void)
{
        DIR *proc_dir = opendir(PROC_DIR);
        if (proc_dir == NULL) {
                report_error(PROC_DIR, errno);
                return;
        }

        int proc_dfd = dirfd(proc_dir);
        struct dirent *proc_entry;

        while ((proc_entry = readdir(proc_dir)) != NULL) {
                if (!is_pid_dir(proc_dfd, proc_entry)) {
                        continue;
                }

                char *fd_dir_path = fs_xasprintf("/proc/%s/fd", proc_entry->d_name);
                DIR *fd_dir = opendir(fd_dir_path);
                if (fd_dir == NULL) {
                        report_error(fd_dir_path, errno);
                        fs_xfree(fd_dir_path);
                        continue;
                }

                struct dirent *fd_entry;
                while ((fd_entry = readdir(fd_dir)) != NULL) {
                        if (strcmp(fd_entry->d_name, ".") == 0 || strcmp(fd_entry->d_name, "..") == 0) {
                                continue;
                        }

                        char *file_link_path = fs_xasprintf("/proc/%s/fd/%s", proc_entry->d_name, fd_entry->d_name);
                        char target_buf[PATH_MAX];
                        ssize_t len = readlink(file_link_path, target_buf, sizeof(target_buf) - 1);

                        if (len < 0) {
                                report_error(file_link_path, errno);
                        } else {
                                target_buf[len] = '\0';
                                report_file(target_buf);
                        }

                        fs_xfree(file_link_path);
                }

                closedir(fd_dir);
                fs_xfree(fd_dir_path);
        }
        closedir(proc_dir);
}
