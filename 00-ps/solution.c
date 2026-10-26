#include <solution.h>   // ps(), report_process(), report_error()
#include <fs_malloc.h>  // fs_xrealloc(), fs_xfree()
#include <fs_string.h>  // fs_xasprintf(), fs_xstrdup()

#include <stdio.h>      // FILE, fopen(), fclose(), getdelim()
#include <stdlib.h>     // atoi(), free()
#include <stdbool.h>    // bool, true, false
#include <ctype.h>      // isdigit()
#include <errno.h>      // errno

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

char **read_proc_strings(const char *path)
{
        FILE *f = fopen(path, "r");
        if (!f) {
                report_error(path, errno);
                return NULL;
        }

        char **list = NULL;
        size_t count = 0;
        char *line = NULL;
        size_t line_cap = 0;

        while (getdelim(&line, &line_cap, '\0', f) != -1) {
                list = fs_xrealloc(list, sizeof(char *) * (count + 1));
                list[count++] = fs_xstrdup(line);
        }

        free(line);
        fclose(f);

        list = fs_xrealloc(list, sizeof(char *) * (count + 1));
        list[count] = NULL;

        return list;
}

void free_proc_strings(char **list)
{
        if (!list) {
                return;
        }
        for (size_t i = 0; list[i] != NULL; ++i) {
                fs_xfree(list[i]);
        }
        fs_xfree(list);
}

void ps(void)
{
        DIR *dir = opendir(PROC_DIR);
        if (dir == NULL) {
                report_error(PROC_DIR, errno);
                return;
        }

        int proc_dfd = dirfd(dir);
        struct dirent *entry;
        char *empty_list[] = { NULL };

        while ((entry = readdir(dir)) != NULL) {
                if (!is_pid_dir(proc_dfd, entry)) {
                        continue;
                }
                pid_t pid = (pid_t)atoi(entry->d_name);

                char *exe_path = fs_xasprintf("/proc/%s/exe", entry->d_name);
                char *argv_path = fs_xasprintf("/proc/%s/cmdline", entry->d_name);
                char *envp_path = fs_xasprintf("/proc/%s/environ", entry->d_name);

                char exe_buf[PATH_MAX];
                ssize_t len = readlink(exe_path, exe_buf, sizeof(exe_buf) - 1);
                const char *exe_str;
                if (len < 0) {
                        report_error(exe_path, errno);
                } else {
                        exe_buf[len] = '\0';
                        exe_str = exe_buf;
                }

                char **argv = read_proc_strings(argv_path);
                char **envp = read_proc_strings(envp_path);

                report_process(pid, exe_str, argv ? argv : empty_list, envp ? envp : empty_list);

                free_proc_strings(argv);
                free_proc_strings(envp);

                fs_xfree(exe_path);
                fs_xfree(argv_path);
                fs_xfree(envp_path);
        }

        closedir(dir);
}
