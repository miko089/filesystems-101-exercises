#include <solution.h>   // abspath(), report_path(), report_error()

#include <stdio.h>      // snprintf(), NULL
#include <stdbool.h>    // bool, true, false
#include <string.h>     // strlen(), strcmp(), strchr(), strrchr(), strcpy(), memcpy()
#include <errno.h>      // errno, ENOENT, ENOTDIR, ELOOP, ENAMETOOLONG
#include <unistd.h>     // readlink(), ssize_t
#include <sys/stat.h>   // struct stat, lstat(), S_ISLNK(), S_ISDIR()
#include <limits.h>     // PATH_MAX, NAME_MAX

#define MAX_SYMLINKS 40

void abspath(const char *path)
{
	if (!path || !*path) {
		report_error("/", "", ENOENT);
		return;
	}

	char resolved[PATH_MAX] = "/";
	char left[PATH_MAX * 2];
	if (strlen(path) >= sizeof(left)) {
		report_error("/", "", ENAMETOOLONG);
		return;
	}
	strcpy(left, path);

	char *cur = left;
	int symlinks = 0;

	while (true) {
		while (*cur == '/')
			cur++;

		if (!*cur) {
			report_path(resolved);
			return;
		}

		char comp[NAME_MAX + 1];
		bool has_next = false;

		char *slash = strchr(cur, '/');
		size_t clen = slash ? (size_t)(slash - cur) : strlen(cur);
		if (clen > NAME_MAX) {
			report_error(resolved, "", ENAMETOOLONG);
			return;
		}

		memcpy(comp, cur, clen);
		comp[clen] = '\0';

		if (slash) {
			has_next = true;
			cur = slash;
		} else {
			cur += clen;
		}

		if (strcmp(comp, ".") == 0)
			continue;

		if (strcmp(comp, "..") == 0) {
			if (strcmp(resolved, "/") != 0) {
				resolved[strlen(resolved) - 1] = '\0';
				char *last = strrchr(resolved, '/');
				last[1] = '\0';
			}
			continue;
		}

		char child_path[PATH_MAX * 2];
		snprintf(child_path, sizeof(child_path), "%s%s", resolved, comp);

		struct stat st;
		if (lstat(child_path, &st) != 0) {
			report_error(resolved, comp, errno);
			return;
		}

		if (S_ISLNK(st.st_mode)) {
			if (++symlinks > MAX_SYMLINKS) {
				report_error(resolved, comp, ELOOP);
				return;
			}

			char target[PATH_MAX];
			ssize_t len = readlink(child_path, target, sizeof(target) - 1);
			if (len <= 0) {
				report_error(resolved, comp, len < 0 ? errno : ENOENT);
				return;
			}
			target[len] = '\0';

			if (target[0] == '/')
				strcpy(resolved, "/");

			char new_left[PATH_MAX * 2];
			bool need_slash = (*cur && target[len - 1] != '/' && *cur != '/');
			if (snprintf(new_left, sizeof(new_left), "%s%s%s",
				     target, need_slash ? "/" : "", cur) >= (int)sizeof(new_left)) {
				report_error(resolved, comp, ENAMETOOLONG);
				return;
			}

			strcpy(left, new_left);
			cur = left;
			continue;
		}

		if (S_ISDIR(st.st_mode)) {
			size_t rlen = strlen(resolved);
			if (snprintf(resolved + rlen, sizeof(resolved) - rlen, "%s/", comp) >= (int)(sizeof(resolved) - rlen)) {
				report_error(resolved, comp, ENAMETOOLONG);
				return;
			}
			continue;
		}

		if (has_next)
			report_error(resolved, comp, ENOTDIR);
		else
			report_path(child_path);
		return;
	}
}
