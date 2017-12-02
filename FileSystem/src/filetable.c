#include "filetable.h"

#include <commons/config.h>
#include <data.h>
#include <file.h>
#include <mlist.h>
#include <mstring.h>
#include <path.h>
#include <socket.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <system.h>
#include <thread.h>
#include <yfile.h>
#include <log.h>
#include <number.h>

#include "dirtree.h"
#include "nodelist.h"
#include "server.h"
#include "FileSystem.h"

#define BLOCKS_PATH "metadata/blocks"

static mutex_t *cpmut;

static struct {
	mutex_t *mut;
	int current;
	int total;
	thread_t *th;
} bsent;

static struct {
	mutex_t *mut;
	mlist_t *blocks;
	int current;
	int total;
	t_file *fp;
	void *map;
} bfile;

static mlist_t *files = NULL;

static mlist_t *files_in_path(const char *path);
static void file_traverser(const char *path);
static void dir_traverser(t_directory *dir);
static void load_blocks(t_yfile *file, t_config *config);
static void save_blocks(t_yfile *file, t_config *config);
static t_yfile *create_file_from_config(t_config *config);
static void update_file(t_yfile *file);
static char *real_file_path(const char *path);
static bool add_blocks_from_file(t_yfile *yfile, const char *path);
static int count_file_blocks(t_file *source, t_yfile *yfile);
static int partition_file_into_blocks(void *source, void *target, t_yfile *yfile);
static int partition_text_file(t_file *source, void *target, t_yfile *yfile);
static int partition_bin_file(t_file *source, char *target, t_yfile *yfile);
static int add_block(void *block, int blockno, size_t size, void *target, t_yfile *yfile);
static void reset_block_file(mlist_t *blocks);
static t_file *receive_file(t_yfile *yfile);
static void receive_block(t_block *block);
static t_block_copy *first_available_copy(t_block *block);
static t_file *reconstruct_file(t_yfile *yfile, t_file *blocks);
static bool available_copy(t_block *block);

// ========== Funciones públicas ==========

void filetable_init() {
	if(files != NULL) return;
	files = mlist_create();
	dirtree_traverse(dir_traverser);
	cpmut = thread_mutex_create();
	bsent.mut = thread_mutex_create();
	bfile.mut = thread_mutex_create();
}

int filetable_size() {
	return mlist_length(files);
}

size_t filetable_totalsize() {
	int adder(int nsize, t_yfile *file) {
		return nsize + file->size;
	}
	return mlist_reduce(files, adder);
}

void filetable_add(t_yfile *file) {
	if (filetable_find(file->path) != NULL)
		return;
	mlist_append(files, file);
	path_mkfile(file->path);
	update_file(file);
}

t_yfile *filetable_find(const char *path) {
	char *rpath = real_file_path(path);
	if (mstring_isempty(rpath))
		return NULL;

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
	if (file == NULL)
		return;
	char *npath = path_create(PTYPE_YAMA, new_path);
	if (!mstring_hassuffix(npath, path_name(file->path))) {
		mstring_format(&npath, "%s/%s", npath, path_name(file->path));
	}

	if (filetable_contains(npath)) {
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
	if (file == NULL)
		return;
	char *npath = path_dir(file->path);
	mstring_format(&npath, "%s/%s", npath, new_name);

	if (filetable_contains(npath)) {
		free(npath);
		return;
	}

	path_move(file->path, npath);
	free(file->path);
	file->path = npath;
}

void filetable_remove(const char *path) {
	t_yfile *file = filetable_find(path);
	if (file == NULL) return;

	path_remove(file->path);
	mlist_traverse(file->blocks, nodelist_rmblock);

	bool cond(t_yfile *elem) {
		return mstring_equal(elem->path, file->path);
	}
	mlist_remove(files, cond, yfile_destroy);
}

void filetable_clear() {
	void routine(t_yfile *elem) {
		filetable_remove(elem->path);
	}
	mlist_traverse(files, routine);
}

void filetable_print() {
	int size = filetable_size();
	if (size == 0) {
		printf("0 archivos.\n");
		return;
	}
	if (size == 1)
		printf("Un archivo:\n");
	else
		printf("%i archivos:\n", size);
	mlist_traverse(files, yfile_print);
}

void filetable_info(const char *path) {
	t_yfile *file = filetable_find(path);
	if (file == NULL)
		return;

	int nblk = mlist_length(file->blocks);
	printf("Archivo %s ", file->type == FTYPE_TXT ? "de texto" : "binario");

	if (nblk == 0) {
		printf("vacío.\n");
		return;
	}

	if (nblk == 1)
		printf("de un bloque ");
	else
		printf("de %i bloques ", nblk);

	char *size = mstring_bsize(file->size);
	printf("(%s):\n", size);
	free(size);

	printf("Bloque      Tamaño     Copia 1     Copia 2  \n");
	void block_printer(t_block *block) {
		char *size = mstring_bsize(block->size);
		printf("%6i  %10s  ", block->index, size);
		free(size);

		for (int i = 0; i < 2; i++) {
			char *node = block->copies[i].node;
			char *str = mstring_empty(NULL);
			if (node != NULL) {
				node = mstring_duplicate(node);
				mstring_crop(&node, 6);
				mstring_format(&str, "%s#%02i", node, block->copies[i].blockno);
				free(node);
			}
			printf("%10s  ", mstring_crop(&str, 10));
			free(str);
		}
		printf("\n");
	}
	mlist_traverse(file->blocks, block_printer);
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
	thread_mutex_lock(cpmut);
	t_ftype type = path_istext(path) ? FTYPE_TXT : FTYPE_BIN;
	char *ypath = path_create(PTYPE_YAMA, dir, path_name(path));
	t_yfile *file = yfile_create(NULL, type);

	if(add_blocks_from_file(file, path)) {
		char *ydir = path_dir(ypath);
		dirtree_add(ydir);
		free(ydir);

		file->path = real_file_path(ypath);
		filetable_add(file);
	} else {
		yfile_destroy(file);
	}

	free(ypath);
	thread_mutex_unlock(cpmut);
}

void filetable_cpto(const char *path, const char *dir) {
	t_yfile *yfile = filetable_find(path);
	t_file *file = receive_file(yfile);
	char *target = mstring_create("%s/%s", dir, path_name(yfile->path));
	path_copy(file_path(file), target);
	free(target);
	file_delete(file);
}

void filetable_cat(const char *path) {
	t_yfile *yfile = filetable_find(path);
	t_file *file = receive_file(yfile);
	bool line_handler(const char *line) {
		printf("%s", line);
		return true;
	}
	file_ltraverse(file, line_handler);
	file_delete(file);
}

char *filetable_md5(const char *path) {
	t_yfile *yfile = filetable_find(path);
	t_file *file = receive_file(yfile);
	char *md5 = path_md5(file_path(file));
	file_delete(file);
	return md5;
}

bool filetable_stable() {
	bool available_block(t_yfile* file) {
		return mlist_all(file->blocks, available_copy);
	}
	return (fs.formatted && mlist_all(files, available_block));
}

void filetable_sentblock() {
	thread_mutex_lock(bsent.mut);
	bsent.current++;
	bool done = bsent.current == bsent.total;
	thread_mutex_unlock(bsent.mut);
	if(done) thread_resume(bsent.th);
}

void filetable_writeblock(const char *node, int blockno, void *block) {

	bool block_finder(t_block *block) {
		for(int i = 0; i < 2; i++) {
			if(mstring_isempty(block->copies[i].node)) continue;
			if(mstring_equal(block->copies[i].node, node) && block->copies[i].blockno == blockno)
				return true;
		}
		return false;
	}
	int index = mlist_index(bfile.blocks, block_finder);
	if(index == -1) {
		log_report("Bloque #%d del nodo %s desconocido", blockno, node);
	}

	memcpy(bfile.map + index * BLOCK_SIZE, block, BLOCK_SIZE);

	thread_mutex_lock(bfile.mut);
	bfile.current++;
	bool file_done = bfile.current == bfile.total;
	thread_mutex_unlock(bfile.mut);

	if(file_done) thread_resume(thread_main());
}

void filetable_term() {
	mlist_destroy(files, yfile_destroy);
	thread_mutex_destroy(cpmut);
	thread_mutex_destroy(bsent.mut);
	thread_mutex_destroy(bfile.mut);
}

void filetable_rm_block(t_yfile *file, t_block* block, int copy) {
	t_node* node = nodelist_find(block->copies[copy].node);
	bitmap_unset(node->bitmap, block->copies[copy].blockno);
	node->free_blocks++;
	block->copies[copy].node = NULL;
	block->copies[copy].blockno = -1;
	update_file(file);
}

void filetable_cpblock(t_yfile *file, off_t block_free, t_block* block, t_node* node) {
	t_node* node_original;
	if (block->copies[0].node != NULL) node_original = nodelist_find(block->copies[0].node);
	else node_original = nodelist_find(block->copies[1].node);

	t_nodeop* op = server_nodeop(NODE_RECV_BLOCK, block->index, NULL);
	thread_send(node_original->handler, op);
	op = server_nodeop(NODE_SEND, block_free, thread_receive());

	thread_send(node->handler, op);

	if (block->copies[0].node != NULL){
		block->copies[1].node = mstring_duplicate(node->name);
		block->copies[1].blockno = block_free;
	}else{
		block->copies[0].node = mstring_duplicate(node->name);
		block->copies[0].blockno = block_free;
	}

	bitmap_set(node->bitmap, block_free);
	node->free_blocks--;

	update_file(file);
}

// ========== Funciones privadas ==========

static mlist_t *files_in_path(const char *path) {
	char *rpath = dirtree_rpath(path);
	if (rpath == NULL)
		return mlist_create();
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
	t_config *config = config_create((char*) path);
	t_yfile *file = create_file_from_config(config);

	load_blocks(file, config);
	config_destroy(config);

	mlist_append(files, file);
}

static void dir_traverser(t_directory *dir) {
	char *ypath = dirtree_path(dir);
	char *dpath = dirtree_rpath(ypath);
	free(ypath);
	path_files(dpath, file_traverser);
	free(dpath);
}

static void load_blocks(t_yfile *file, t_config *config) {
	char *key = mstring_empty(NULL);
	for (int blockno = 0; true; blockno++) {
		mstring_format(&key, "BLOQUE%iBYTES", blockno);
		char *sizestr = config_get_string_value(config, key);
		if (mstring_isempty(sizestr))
			break;

		t_block *block = calloc(1, sizeof(t_block));
		block->index = blockno;
		block->size = mstring_toint(sizestr);

		for (int copyno = 0; copyno < 2; copyno++) {
			mstring_format(&key, "BLOQUE%iCOPIA%i", blockno, copyno);
			char **vals = config_get_array_value(config, key);
			if (!mstring_equali(vals[0], "(null)")) {
				block->copies[copyno].node = mstring_duplicate(vals[0]);
				block->copies[copyno].blockno = mstring_toint(vals[1]);
			} else {
				block->copies[copyno].node = NULL;
				block->copies[copyno].blockno = -1;
			}
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
		for (int copyno = 0; copyno < 2; copyno++) {
			mstring_format(&key, "BLOQUE%iCOPIA%i", block->index, copyno);
			mstring_format(&value, "[%s, %d]", block->copies[copyno].node,
					block->copies[copyno].blockno);
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
	file->type =
			mstring_equal(config_get_string_value(config, "TIPO"), "TEXTO") ?
					FTYPE_TXT : FTYPE_BIN;
	file->blocks = mlist_create();
	return file;
}

static void update_file(t_yfile *file) {
	if (!fs.formatted)
		return;

	t_config *config = config_create(file->path);
	config_set_value(config, "TIPO",
			file->type == FTYPE_TXT ? "TEXTO" : "BINARIO");
	char *sizestr = mstring_create("%i", file->size);
	config_set_value(config, "TAMANIO", sizestr);
	free(sizestr);

	save_blocks(file, config);
	config_save(config);
	config_destroy(config);
}

static char *real_file_path(const char *path) {
	if(mstring_hasprefix(path, system_userdir())) {
		return mstring_duplicate(path);
	}

	char *ypath = path_create(PTYPE_YAMA, path);
	char *pdir = path_dir(ypath);
	free(ypath);
	char *rpath = dirtree_rpath(pdir);
	free(pdir);
	if (rpath == NULL)
		return NULL;
	mstring_format(&rpath, "%s/%s", rpath, path_name(path));
	return rpath;
}

static bool add_blocks_from_file(t_yfile *yfile, const char *path) {
	t_file *source = file_open(path);

	int numblocks = count_file_blocks(source, yfile);
	int freeblocks = nodelist_freeblocks();

	bsent.th = thread_self();
	bsent.current = 0;
	bsent.total = number_min(freeblocks, 2 * numblocks);

	if(freeblocks < numblocks) {
		if(bsent.th == thread_main())
			fprintf(stderr, "Error: no hay suficiente espacio libre para guardar este archivo.\n");
		file_close(source);
		return false;
	}

	path_truncate(BLOCKS_PATH, numblocks * BLOCK_SIZE);
	t_file *target = file_open(BLOCKS_PATH);
	void *tmap = file_map(target);
	partition_file_into_blocks(source, tmap, yfile);

	file_close(source);

	int saved_blocks = 0;
	void send_block(t_block *block) {
		saved_blocks += nodelist_addblock(block, tmap + block->index * BLOCK_SIZE) ? 1 : 0;
	}

	mlist_traverse(yfile->blocks, send_block);
	mlist_traverse(yfile->blocks, send_block);

	thread_suspend();

	bool success = saved_blocks == bsent.total && numblocks <= saved_blocks && saved_blocks <= 2 * numblocks;
	log_inform("%s", success ? "Archivo distribuido correctamente" : "Error al distribuir archivo");

	if(!success && bsent.th == thread_main()) {
		fprintf(stderr, "Error: no se pudo guardar el archivo.\n");
	}

	file_unmap(target, tmap);
	file_delete(target);

	return success;
}

static int count_file_blocks(t_file *source, t_yfile *yfile) {
	log_inform("Contando bloques en el archivo");
	int count = 0;
	if(yfile->type == FTYPE_TXT) {
		count = partition_text_file(source, NULL, NULL);
	} else {
		count = number_ceiling(file_size(source) * 1.0 / BLOCK_SIZE);
	}
	return count;
}

static int partition_file_into_blocks(void *source, void *target, t_yfile *yfile) {
	log_inform("Particionando archivo en bloques");
	int count = 0;
	if(yfile->type == FTYPE_TXT) {
		count = partition_text_file(source, target, yfile);
	} else {
		count = partition_bin_file(source, target, yfile);
	}
	return count;
}

static int partition_text_file(t_file *source, void *target, t_yfile *yfile) {
	char *buffer = alloca(BLOCK_SIZE);
	size_t size = 0;
	int count = 0;

	bool line_handler(const char *line) {
		if(size + mstring_length(line) + 1 > BLOCK_SIZE) {
			count += add_block(buffer, count, size, target, yfile);
			size = 0;
		}
		size += sprintf(buffer + size, "%s", line);
		return true;
	}
	file_ltraverse(source, line_handler);
	count += add_block(buffer, count, size, target, yfile);

	return count;
}

static int partition_bin_file(t_file *source, char *target, t_yfile *yfile) {
	char *buffer = alloca(BLOCK_SIZE);
	size_t size = 0;
	int count = 0;

	bool block_handler(const void *block, size_t bsize) {
		if (size + bsize > BLOCK_SIZE) {
			count += add_block(buffer, count, size, target, yfile);
			size = 0;
		}
		memcpy(buffer + size, block, bsize);
		size += bsize;
		return true;
	}
	file_btraverse(source, block_handler);
	count += add_block(buffer, count, size, target, yfile);

	return count;
}

static int add_block(void *block, int blockno, size_t size, void *target, t_yfile *yfile) {
	if(size == 0) return 0;
	if(target != NULL && yfile != NULL) {
		memcpy(target + blockno * BLOCK_SIZE, block, BLOCK_SIZE);
		t_block *block = calloc(1, sizeof(t_block));
		block->size = size;
		yfile_addblock(yfile, block);
	}
	return 1;
}

static void reset_block_file(mlist_t *blocks) {
	bfile.blocks = blocks;
	bfile.total = mlist_length(blocks);
	bfile.current = 0;

	path_truncate(BLOCKS_PATH, bfile.total * BLOCK_SIZE);
	bfile.fp = file_open(BLOCKS_PATH);
	bfile.map = file_map(bfile.fp);
}

static t_file *receive_file(t_yfile *yfile) {
	reset_block_file(yfile->blocks);
	mlist_traverse(yfile->blocks, receive_block);
	thread_suspend();
	file_unmap(bfile.fp, bfile.map);
	t_file *file = reconstruct_file(yfile, bfile.fp);
	file_delete(bfile.fp);
	return file;
}

static void receive_block(t_block *block) {
	t_block_copy *copy = first_available_copy(block);
	t_node *node = nodelist_find(copy->node);

	t_nodeop *op = server_nodeop(NODE_RECV, copy->blockno, NULL);
	thread_send(node->handler, op);
}

static t_block_copy *first_available_copy(t_block *block) {
	t_block_copy *copy = NULL;
	t_node *node0 = nodelist_find(block->copies[0].node);
	t_node *node1 = nodelist_find(block->copies[1].node);
	while (copy == NULL) {
		if (nodelist_active(node1))
			copy = block->copies + 1;
		else if (nodelist_active(node0))
			copy = block->copies;
		else
			thread_sleep(500);
	}
	return copy;
}

static t_file *reconstruct_file(t_yfile *yfile, t_file *blocks) {
	log_inform("Reconstruyendo archivo %s", path_name(yfile->path));
	t_file *file = file_create("metadata/file");
	int index = -1;
	t_block *block = NULL;
	size_t size = BLOCK_SIZE;

	bool next_block() {
		block = mlist_get(yfile->blocks, ++index);
		if (block == NULL)
			return false;
		size = 0;
		return true;
	}

	bool traverser(const void *bcont, size_t bsize) {
		if (size == BLOCK_SIZE && !next_block())
			return false;

		size_t bcur = bsize, bytes = 0;
		for (int offset = 0; offset < bsize; offset += bytes) {
			bytes = number_min(bcur, block->size - size);
			fwrite(bcont + offset, 1, bytes, file_pointer(file));
			size += bytes;
			bcur -= bytes;
			if (size == block->size) {
				offset += BLOCK_SIZE - size;
				if (!next_block())
					return false;
			}
		}

		return true;
	}
	file_btraverse(blocks, traverser);
	file_rewind(file);
	return file;
}

static bool available_copy(t_block *block) {
	t_block_copy *copy = NULL;
	t_node *node0 = nodelist_find(block->copies[0].node);
	t_node *node1 = nodelist_find(block->copies[1].node);

	if (nodelist_active(node0))
		copy = block->copies;
	else if (nodelist_active(node1))
		copy = block->copies + 1;

	return (copy != NULL);
}
