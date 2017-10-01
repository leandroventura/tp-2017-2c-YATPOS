#define _XOPEN_SOURCE 500
#include <asm-generic/errno-base.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <mstring.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <system.h>
#include <time.h>
#include <file.h>
#include <unistd.h>
#ifndef __USE_XOPEN_EXTENDED
#define __USE_XOPEN_EXTENDED
#endif
#include <ftw.h>
#include "path.h"

#define FILE_PERMS 0664

static void show_error_and_exit(const char *path, const char *action);
static int remove_routine(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf);
static size_t size_of_file(const char *path);

// ========== Funciones públicas ==========

bool path_exists(const char *path) {
	char *upath = system_upath(path);
	bool exists = access(upath, F_OK) != -1;
	free(upath);
	return exists;
}

bool path_isdir(const char *path) {
	char *upath = system_upath(path);
	bool exists = access(upath, F_OK) != -1;
	struct stat s;
	stat(upath, &s);
	bool isdir = s.st_mode & S_IFDIR;
	free(upath);
	return exists && isdir;
}

bool path_isfile(const char *path) {
	return path_exists(path) && !path_isdir(path);
}

bool path_istext(const char *path) {
	if(!path_isfile(path)) return false;

	char *upath = system_upath(path);
	FILE *fd = fopen(upath, "r");
	size_t size = path_size(path);
	int scansize = 64;
	if(size < scansize) scansize = size;

	bool istext = true;
	while(scansize-- && istext) {
		if(fgetc(fd) == '\0') istext = false;
	}

	if(!istext) goto istext_end;

	char *cmd = mstring_create("file %s", upath);
	FILE *pd = popen(cmd, "r");
	free(cmd);
	if(pd == NULL) show_error_and_exit(upath, "verificar");
	char *output = mstring_buffer();
	fgets(output, mstring_maxsize(), pd);
	pclose(pd);

	istext = mstring_contains(output, "text");
	istext_end:
	fclose(fd);
	free(upath);
	return istext;
}

bool path_isbin(const char *path) {
	return path_isfile(path) && !path_istext(path);
}

void path_mkdir(const char *path) {
	char *upath = system_upath(path);
	if(access(upath, F_OK) != -1) {
		free(upath);
		return;
	}
	mode_t perms = S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH;
	for(char *p = upath + 1; *p; p++) {
		if(*p == '/') {
			*p = '\0';
			mkdir(upath, perms);
			*p = '/';
		}
	}
	mkdir(upath, perms);
	free(upath);
}

size_t path_size(const char *path) {
	char *upath = system_upath(path);
	size_t size = 0;

	if(!path_isdir(path)) {
		size = size_of_file(upath);
		free(upath);
		return size;
	}

	int routine(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf) {
		size += size_of_file(fpath);
		return 0;
	}

	nftw(upath, routine, 64, FTW_PHYS);
	return size;
}

char *path_sizep(const char *path) {
	static const char *pref[] = {"", "K", "M", "G", "T"};
	const size_t size = path_size(path);
	const unsigned short mult = 1024;
	const char *x = size < mult ? "" : "i";
	float s = size;
	int i = 0;
	while(s >= mult) {
		i++;
		s /= mult;
	}
	return mstring_create("%.1f %s%sB", s, pref[i], x);
}

char *path_dir(const char *path) {
	char *upath = system_upath(path);
	char *slash = strrchr(upath, '/');
	if(slash) *slash = '\0';
	return upath;
}

const char *path_name(const char *path) {
	char *slash = strrchr(path, '/');
	return slash ? slash + 1 : path;
}

char *path_temp() {
	static bool seed = false;
	if(!seed) {
		srand(time(NULL));
		seed = true;
	}
	char *tmp = NULL, c;
	int r;
	do {
		mstring_empty(&tmp);
		while(strlen(tmp) < 32) {
			r = rand();
			c = r % 3 == 2 ? '0' + r % 10 : (r % 3 ? 'A' : 'a') + r % 26;
			mstring_format(&tmp, "%s%c", tmp, c);
		}
		mstring_format(&tmp, "tmp/%s", tmp);
	} while(path_exists(tmp));
	return tmp;
}

void path_create(const char *path) {
	char *upath = system_upath(path);
	char *dir = path_dir(upath);
	path_mkdir(dir);
	free(dir);

	int fd = open(upath, O_CREAT, FILE_PERMS);
	if(fd == -1) {
		fprintf(stderr, "Error al crear archivo %s: %s\n", upath, strerror(errno));
		free(upath);
		exit(EXIT_FAILURE);
	}
	free(upath);
	close(fd);
}

void path_copy(const char *source, const char *target) {
	char *usource = system_upath(source);
	int fd_from = open(source, O_RDONLY);
	if(fd_from == -1) {
		free(usource);
		return;
	}

	char *utarget = system_upath(target);
	if(path_isdir(utarget)) {
		mstring_format(&utarget, "%s/%s", utarget, path_name(source));
	}

	int fd_to = open(utarget, O_WRONLY | O_CREAT | O_TRUNC, FILE_PERMS);
	if(fd_to == -1) goto exit;

	char buffer[4096];
	ssize_t nread;
	while(nread = read(fd_from, buffer, sizeof buffer), nread > 0) {
		char *p = buffer;
		ssize_t nwritten;
		do {
			nwritten = write(fd_to, p, nread);
			if(nwritten == -1 && errno != EINTR) goto exit;
			nread -= nwritten;
			p += nwritten;
		} while(nread > 0);
	}

	exit:
	free(usource);
	free(utarget);
	close(fd_from);
	if(fd_to != -1) close(fd_to);
}

void path_move(const char *source, const char *target) {
	path_copy(source, target);
	path_remove(source);
}

void path_remove(const char *path) {
	if(!path_exists(path)) return;
	char *upath = system_upath(path);
	if(path_isdir(path)) {
		int r = nftw(upath, remove_routine, 64, FTW_DEPTH | FTW_PHYS);
		if(r == -1) show_error_and_exit(upath, "remover");
	} else {
		unlink(upath);
	}
	free(upath);
}

void path_truncate(const char *path, size_t size) {
	char *upath = system_upath(path);
	int fd = open(upath, O_CREAT | O_WRONLY, FILE_PERMS);
	free(upath);
	ftruncate(fd, size);
	close(fd);
}

void path_sort(const char *path) {
	if(!path_istext(path)) return;
	t_file *file = file_open(path);

	mlist_t *lines = mlist_create();
	void reader(const char *line) {
		mlist_append(lines, mstring_create("%s\n", line));
	}
	file_traverse(file, reader);

	mlist_sort(lines, mstring_asc);
	file_clear(file);

	void writer(const char *line) {
		file_writeline(file, line);
	}
	mlist_traverse(lines, writer);
	mlist_destroy(lines, free);
}

void path_merge(mlist_t *sources, const char *target) {
	typedef struct {
		t_file *file;
		char *line;
	} t_cont;
	t_cont *map_cont(const char *source) {
		t_cont *cont = malloc(sizeof(cont));
		cont->file = file_open(source);
		cont->line = NULL;
		return cont;
	}
	bool line_set(t_cont *cont) {
		return cont->line != NULL;
	}
	void read_file(t_cont *cont) {
		if(!line_set(cont)) {
			char *aux = file_readline(cont->file);
			free(cont->line);
			cont->line = aux;
		}
	}
	bool compare_lines(t_cont *cont1, t_cont *cont2) {
		return mstring_asc(cont1->line, cont2->line);
	}
	mlist_t *files = mlist_map(sources, map_cont);

	t_file *output = file_create(target);
	mlist_t *list = mlist_copy(files);
	while(true) {
		mlist_traverse(list, read_file);
		mlist_t *aux = mlist_filter(list, line_set);
		mlist_destroy(list, NULL);
		list = aux;
		if(mlist_length(list) == 0) break;
		mlist_sort(list, compare_lines);
		t_cont *cont = mlist_first(list);
		file_writeline(output, cont->line);
		free(cont->line);
		cont->line = NULL;
	}
	void free_cont(t_cont *cont) {
		file_close(cont->file);
		free(cont);
	}
	mlist_destroy(files, free_cont);
}

// ========== Funciones privadas ==========

static void show_error_and_exit(const char *path, const char *action) {
	system_exit("Error al %s ruta %s: %s", action, path, strerror(errno));
}

static int remove_routine(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf) {
	int r = remove(fpath);
	if(r == -1) show_error_and_exit(fpath, "remover");
	return r;
}

static size_t size_of_file(const char *path) {
	struct stat s;
	stat(path, &s);
	return s.st_size;
}
