#include "filetable.h"
#include <mlist.h>
#include "dirtree.h"
#include <stdlib.h>
#include <path.h>
#include <commons/config.h>
#include <stdio.h>
#include <system.h>
#include "nodelist.h"

static mlist_t *files = NULL;

static mlist_t *files_in_path(const char *path);
static void file_traverser(const char *path);
static void dir_traverser(t_directory *dir);
static void load_blocks(t_yfile *file, t_config *config);
static void save_blocks(t_yfile *file, t_config *config);
static t_yfile *create_file_from_config(t_config *config);
static void update_file(t_yfile *file);
char *real_file_path(const char *path);
static void add_blocks_from_text_file(t_yfile *yfile, const char *path);
static void add_and_send_block(t_yfile *yfile, char *buffer, size_t size);
static void print_block(t_block *block);

// ========== Funciones públicas ==========

void filetable_init() {
	if(files != NULL) return;
	files = mlist_create();
	dirtree_traverse(dir_traverser);
}

int filetable_size() {
	return mlist_length(files);
}

void filetable_add(t_yfile *file) {
	if(filetable_find(file->path) != NULL) return;
	mlist_append(files, file);
	path_mkfile(file->path);
	update_file(file);
}

t_yfile *filetable_find(const char *path) {
	char *rpath = real_file_path(path);
	if(mstring_isempty(rpath)) return NULL;

	bool cond(t_yfile *file) {
		return mstring_equal(file->path, rpath);
	}
	t_yfile *file = mlist_find(files, cond);
	free(rpath);
	return file;
}

bool filetable_contains(const char *path) {
	return filetable_find(path) != NULL;
}

void filetable_move(const char *path, const char *new_path) {
	t_yfile *file = filetable_find(path);
	if(file == NULL) return;
	char *npath = path_create(PTYPE_YAMA, new_path);
	if(!mstring_hassuffix(npath, path_name(file->path))) {
		mstring_format(&npath, "%s/%s", npath, path_name(file->path));
	}
	
	if(filetable_contains(npath)) {
		free(npath);
		return;
	}

	char *dpath = path_dir(npath);
	dirtree_add(dpath);
	free(dpath);

	char *rpath = real_file_path(npath);
	path_move(file->path, rpath);
	free(file->path);
	file->path = rpath;
}

void filetable_rename(const char *path, const char *new_name) {
	t_yfile *file = filetable_find(path);
	if(file == NULL) return;
	char *npath = path_dir(file->path);
	mstring_format(&npath, "%s/%s", npath, new_name);

	if(filetable_contains(npath)) {
		free(npath);
		return;
	}

	path_move(file->path, npath);
	free(file->path);
	file->path = npath;
}

void filetable_remove(const char *path) {
	t_yfile *file = filetable_find(path);
	if(file == NULL) return;

	bool cond(t_yfile *elem) {
		return mstring_equal(elem->path, file->path);
	}
	mlist_remove(files, cond, yfile_destroy);
	path_remove(file->path);
}

void filetable_clear() {
	void routine(t_yfile *elem) {
		filetable_remove(elem->path);
	}
	mlist_traverse(files, routine);
}

void filetable_print() {
	puts("Tabla de archivos yamafs");
	printf("Cantidad: %d\n", filetable_size());
	void routine(t_yfile *file) {
		yfile_print(file);
	}
	mlist_traverse(files, routine);
}

size_t filetable_count(const char *path) {
	mlist_t *ctfiles = files_in_path(path);
	size_t count = mlist_length(ctfiles);
	mlist_destroy(ctfiles, NULL);
	return count;
}

void filetable_list(const char *path) {
	mlist_t *lsfiles = files_in_path(path);

	bool sorter(t_yfile *file1, t_yfile *file2) {
		return mstring_asc(path_name(file1->path), path_name(file2->path));
	}
	mlist_sort(lsfiles, sorter);

	void printer(t_yfile *file) {
		printf("%s\n", path_name(file->path));
	}
	mlist_traverse(lsfiles, printer);

	mlist_destroy(lsfiles, NULL);
}

void filetable_cpfrom(const char *path, const char *dir) {
	char *upath = path_create(PTYPE_USER, path);
	if(!path_isfile(upath)) return;

	t_ftype type = path_istext(upath) ? FTYPE_TXT : FTYPE_BIN;
	char *ypath = path_create(PTYPE_YAMA, dir);

	dirtree_add(ypath);
	mstring_format(&ypath, "%s/%s", ypath, path_name(upath));

	char *rpath = real_file_path(ypath);
	t_yfile *file = yfile_create(rpath, type);

	printf("upath: %s\n", upath);
	printf("yama: %s\n", ypath);
	printf("real: %s\n", rpath);

	if(type == FTYPE_TXT) {
		add_blocks_from_text_file(file, upath);
	}

	filetable_add(file);

	free(upath);
	free(ypath);
	free(rpath);
}

// ========== Funciones privadas ==========

static mlist_t *files_in_path(const char *path) {
	char *rpath = dirtree_path(path);
	if(rpath == NULL) return mlist_create();
	bool filter(t_yfile *file) {
		char *dir = path_dir(file->path);
		bool equal = path_equal(dir, rpath);
		free(dir);
		return equal;
	}
	mlist_t *list = mlist_filter(files, filter);
	free(rpath);
	return list;
}

static void file_traverser(const char *path) {
	t_config *config = config_create((char*)path);
	t_yfile *file = create_file_from_config(config);

	load_blocks(file, config);
	config_destroy(config);

	mlist_append(files, file);
}

static void dir_traverser(t_directory *dir) {
	char *dpath = dirtree_path(dir->name);
	path_files(dpath, file_traverser);
	free(dpath);
}

static void load_blocks(t_yfile *file, t_config *config) {
	char *key = mstring_empty(NULL);
	for(int blockno = 0; true; blockno++) {
		mstring_format(&key, "BLOQUE%iBYTES", blockno);
		char *sizestr = config_get_string_value(config, key);
		if(mstring_isempty(sizestr)) break;

		t_block *block = calloc(1, sizeof(t_block));
		block->index = blockno;
		block->size = mstring_toint(sizestr);

		for(int copyno = 0; copyno < 2; copyno++) {
			mstring_format(&key, "BLOQUE%iCOPIA%i", blockno, copyno);
			char **vals = config_get_array_value(config, key);
			block->copies[copyno].node = mstring_duplicate(vals[0]);
			block->copies[copyno].blockno = mstring_toint(vals[1]);
		}

		mlist_append(file->blocks, block);
	}
	free(key);
}

static void save_blocks(t_yfile *file, t_config *config) {
	char *key = mstring_empty(NULL);
	char *value = mstring_empty(NULL);
	void save_block(t_block *block) {
		mstring_format(&key, "BLOQUE%iBYTES", block->index);
		mstring_format(&value, "%i", block->size);
		config_set_value(config, key, value);
		for(int copyno = 0; copyno < 2; copyno++) {
			mstring_format(&key, "BLOQUE%iCOPIA%i", block->index, copyno);
			mstring_format(&value, "[%s, %d]", block->copies[copyno].node, block->copies[copyno].blockno);
			config_set_value(config, key, value);
		}
	}
	mlist_traverse(file->blocks, save_block);
	free(key);
	free(value);
}

static t_yfile *create_file_from_config(t_config *config) {
	t_yfile *file = malloc(sizeof(t_yfile));
	file->path = mstring_duplicate(config->path);
	file->size = config_get_long_value(config, "TAMANIO");
	file->type = mstring_equal(config_get_string_value(config, "TIPO"), "TEXTO") ? FTYPE_TXT : FTYPE_BIN;
	file->blocks = mlist_create();
	return file;
}

static void update_file(t_yfile *file) {
	t_config *config = config_create(file->path);
	config_set_value(config, "TIPO", file->type == FTYPE_TXT ? "TEXTO" : "BINARIO");
	char *sizestr = mstring_create("%i", file->size);
	config_set_value(config, "TAMANIO", sizestr);
	free(sizestr);

	save_blocks(file, config);
	config_save(config);
	config_destroy(config);
}

char *real_file_path(const char *path) {
	if(mstring_hasprefix(path, system_userdir())) {
		return mstring_duplicate(path);
	}

	char *ypath = path_create(PTYPE_YAMA, path);
	char *pdir = path_dir(ypath);
	free(ypath);
	char *rpath = dirtree_path(pdir);
	free(pdir);
	if(rpath == NULL) return NULL;
	mstring_format(&rpath, "%s/%s", rpath, path_name(path));
	return rpath;
}

static void add_blocks_from_text_file(t_yfile *yfile, const char *path) {
	t_file *file = file_open(path);

	char buffer[15];
	int size = 0;

	void line_handler(const char *line) {
		printf("copying line: %s\n", line);
		if(size + mstring_length(line) + 1 > 15) {
			add_and_send_block(yfile, buffer, size);
			size = 0;
		}
		size += sprintf(buffer + size, "%s\n", line);
	}
	file_traverse(file, line_handler);
	add_and_send_block(yfile, buffer, size);
	file_close(file);
}

static void add_and_send_block(t_yfile *yfile, char *buffer, size_t size) {
	printf("Sending block:\n%s\n", buffer);
	t_block *block = calloc(1, sizeof(t_block));
	block->size = size;
	nodelist_addblock(block, buffer);
	print_block(block);
	yfile_addblock(yfile, block);
}

static void print_block(t_block *block) {
	printf("Block size: %d\n", block->size);
	printf("First copy: block #%d of node %s\n", block->copies[0].blockno, block->copies[0].node);
	printf("Second copy: block #%d of node %s\n", block->copies[1].blockno, block->copies[1].node);
}
