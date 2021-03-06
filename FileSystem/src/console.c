#include <mstring.h>
#include <readline/history.h>
#include <readline/readline.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <system.h>
#include <protocol.h>
#include <path.h>
#include <stdarg.h>
#include <data.h>

#include "nodelist.h"
#include "dirtree.h"
#include "FileSystem.h"
#include "filetable.h"

#define show_usage() {show_usage_for(current_cmd); return;}

// ========== Estructuras ==========

typedef struct {
	char *name;
	void (*func)(void);
	char *info;
	char *usage;
} t_command;

static int current_cmd = -1;
static char *current_args = NULL;

// ========== Declaraciones ==========

static void cmd_cat(void);
static void cmd_clear(void);
static void cmd_cpblock(void);
static void cmd_cpfrom(void);
static void cmd_cpto(void);
static void cmd_format(void);
static void cmd_help(void);
static void cmd_info(void);
static void cmd_ls(void);
static void cmd_md5(void);
static void cmd_mkdir(void);
static void cmd_mv(void);
static void cmd_nodes(void);
static void cmd_quit(void);
static void cmd_rename(void);
static void cmd_rm(void);
static void cmd_status(void);
static void cmd_tree(void);

static void init_console();
static void term_console();
static void show_usage_for(int cmd);
static char *extract_arg(int no);
static int num_args(void);
static void print_error(const char *format, ...);
static void execute_line(const char *);
static t_command *find_command(const char *);
static void command_not_found(const char *);
static char **rl_completion(const char*, int, int);
static char *rl_generator(const char*, int);

// ========== Variables globales ==========

static t_command commands[] = {
		{"cat", cmd_cat, "Muestra el contenido de un archivo", "archivo"},
		{"clear", cmd_clear, "", ""},
		{"cpblock", cmd_cpblock, "Copia un bloque de un archivo en un nodo", "archivo bloque nodo"},
		{"cpfrom", cmd_cpfrom, "Copia un archivo local a yamafs", "archivo_local directorio_yamafs"},
		{"cpto", cmd_cpto, "Copia un archivo yamafs al sistema local", "archivo_yamafs directorio_local"},
		{"exit", cmd_quit, "", ""},
		{"format", cmd_format, "Da formato al sistema de archivos", ""},
		{"help", cmd_help, "Lista los comandos disponibles", "[comando]"},
		{"info", cmd_info, "Muestra información de un archivo", "archivo"},
		{"ls", cmd_ls, "Lista los archivos de un directorio", "directorio"},
		{"md5", cmd_md5, "Muestra el MD5 de un archivo", "archivo"},
		{"mkdir", cmd_mkdir, "Crea un directorio", "directorio"},
		{"mv", cmd_mv, "Mueve un archivo o directorio", "ruta_original ruta_final"},
		{"nodes", cmd_nodes, "Muestra información sobre los nodos de datos", ""},
		{"quit", cmd_quit, "Termina la ejecución del proceso", ""},
		{"rename", cmd_rename, "Renombra un archivo o directorio", "ruta_original nombre_final"},
		{"rm", cmd_rm, "Elimina un archivo, directorio o bloque", "archivo | -d directorio | -b archivo bloque copia"},
		{"status", cmd_status, "Muestra información sobre el estado del sistema", ""},
		{"tree", cmd_tree, "Muestra la estructura de árbol de los directorios", ""},
		{NULL, NULL, NULL, NULL}
};

static bool should_quit = false;
static char *history_file = NULL;

// ========== Funciones públicas ==========

void console() {
	char *line;
	init_console();

	while(!should_quit && (line = readline("> "))) {
		mstring_trim(line);
		fflush(stdout);

		if(*line) {
			add_history(line);
			write_history(history_file);
			execute_line(line);
		}

		free(line);
	}

	term_console();
}

// ========== Funciones privadas ==========

static void init_console() {
	history_file = mstring_create("%s/.history", system_userdir());
	read_history(history_file);
	rl_attempted_completion_function = rl_completion;
}

static void term_console() {
	free(history_file);
}

static void show_usage_for(int cmd) {
	printf("Uso: %s %s\n", commands[cmd].name, commands[cmd].usage);
}

static char *extract_arg(int no) {
	if(mstring_isempty(current_args)) return NULL;
	int i = 0;
	char *p = current_args;
	int start = 0;
	int end = 0;
	bool q = false;
	bool s = false;
	do {
		while(!q && isspace(*p)) { s = true; p++; }
		if(*p == '"' || *p == '\'') q = !q;
		if(s || *p == '\0') {
			s = false;
			i++;
			end = p - current_args;
			if(no == i) {
				char *copy = mstring_trim(mstring_copy(current_args, start, end));
				char *a = copy, *b = mstring_end(copy);
				if(*a == '"' || *a == '\'') *a = ' ';
				if(*b == '"' || *b == '\'') *b = ' ';
				return mstring_trim(copy);
			}
			start = end;
		}
	} while(*p++ != '\0');
	return NULL;
}

static int num_args() {
	char *arg = NULL;
	int count = 0;
	int i = 1;
	while(arg = extract_arg(i++), arg != NULL) {
		if(!mstring_isempty(arg)) {
			count++;
		}
		free(arg);
	}
	return count;
}

static void print_error(const char *format, ...) {
	va_list args;
	char *err = mstring_create("Error: %s.\n", format);
	va_start(args, format);
	vfprintf(stderr, err, args);
	va_end(args);
	free(err);
}

// ========== Funciones de comandos ==========

static void cmd_cat() {
	if(num_args() != 1) show_usage();
	char *path = extract_arg(1);
	if(filetable_contains(path)) {
		filetable_cat(path);
	} else {
		print_error("archivo inexistente");
	}
	free(path);
}

static void cmd_clear() {
	printf("\033c");
}

static void cmd_cpblock() {
	int nargs = num_args();
	if(nargs != 3) show_usage();
	char *path = extract_arg(1);

	if(!filetable_contains(path)) {
		print_error("archivo inexistente");
	}else{
		t_yfile *yfile = filetable_find(path);
		bool getBlock (t_block *bloque){
			return mstring_toint(extract_arg(2)) == bloque->index;
		}
		t_block *block = mlist_find(yfile->blocks, getBlock);
		if(block == NULL){
			print_error("bloque inexistente");
		}else if(block->copies[0].node != NULL && block->copies[1].node != NULL){
			print_error("ya existen dos copias - máximo soportado");
		}else{
			t_node *node = nodelist_find(extract_arg(3));
			if (node == NULL){
				print_error("nodo inexistente");
			}else if((block->copies[0].node != NULL && mstring_equali(node->name, block->copies[0].node)) ||
				(block->copies[1].node != NULL && mstring_equali(node->name, block->copies[1].node))){
				print_error("no pueden haber dos copias en el mismo nodo");
			}else{
				off_t block_free = bitmap_firstzero(node->bitmap);
				if (block_free == -1){
					print_error("no hay bloques libres en %s", node->name);
				}else if(!nodelist_active(node)){
					print_error("nodo inactivo");
				}else{
					filetable_cpblock(yfile, block_free, block, node);
				}
			}
		}
	}
}

static void cmd_cpfrom() {
	if(num_args() != 2) show_usage();
	char *path = extract_arg(1);
	char *dir = extract_arg(2);

	char *upath = path_create(PTYPE_USER, path);
	free(path);

	if(path_isfile(upath)) {
		filetable_cpfrom(upath, dir);
	} else {
		print_error("archivo inexistente");
	}

	free(upath);
	free(dir);
}

static void cmd_cpto() {
	if(num_args() != 2) show_usage();
	char *path = extract_arg(1);
	char *dir = extract_arg(2);
	char *udir = path_create(PTYPE_USER, dir);
	free(dir);

	if(!filetable_contains(path)) {
		print_error("archivo inexistente");
	} else if(!path_isdir(udir)) {
		print_error("directorio inexistente");
	} else {
		filetable_cpto(path, udir);
	}

	free(path);
	free(udir);
}

static void cmd_format() {
	if(nodelist_length() == 0) {
		print_error("ningún nodo conectado");
		return;
	}

	bool proceed = mstring_equal(current_args, "-f");
	if(!proceed) {
		printf("!: El filesystem se va a formatear."
				" Continuar? \n"
				"[S]í. [N]o. \n");
		char *scan = readline("> ");
		proceed = mstring_equali(scan, "S");
		free(scan);
	}

	if(!proceed) return;

	dirtree_clear();
	filetable_clear();
	nodelist_format();

	printf("\nFilesystem formateado.\n");
	fs.formatted = true;
}

static void cmd_help() {
	int printed = 0;

	for(int i = 0; commands[i].name; i++) {
		if((!*current_args || mstring_equal(current_args, commands[i].name))
				&& !mstring_isempty(commands[i].info)) {
			printf("%s\t\t%s.\n", commands[i].name, commands[i].info);
			printed++;
			if(*current_args) {
				show_usage_for(i);
				break;
			}
		}
	}

	if(!printed) command_not_found(current_args);
}

static void cmd_info() {
	if(num_args() != 1) show_usage();
	char *path = extract_arg(1);
	if(filetable_contains(path)) {
		filetable_info(path);

	} else {
		print_error("archivo inexistente");
	}
	free(path);
}

static void cmd_ls() {
	if(num_args() != 1) show_usage();
	char *path = extract_arg(1);
	if(!dirtree_contains(path)) {
		puts("Directorio inexistente.");
		free(path);
		return;
	}

	size_t nd = dirtree_count(path);
	size_t nf = filetable_count(path);
	printf("%zi directorio%s, %zi archivo%s%s\n", nd, nd == 1 ? "" : "s",
			nf, nf == 1 ? "" : "s", nf + nd == 0 ? "." : ":");

	dirtree_list(path);
	filetable_list(path);
	free(path);
}

static void cmd_md5() {
	if(num_args() != 1) show_usage();
	char *path = extract_arg(1);
	if(!filetable_contains(path)) {
		print_error("archivo inexistente");
	} else {
		char *md5 = filetable_md5(path);
		printf("%s\n", md5);
		free(md5);
	}
	free(path);
}

static void cmd_mkdir() {
	if(num_args() != 1) show_usage();
	char *path = extract_arg(1);
	if(dirtree_contains(path)) {
		print_error("ya existe el directorio");
	} else if(dirtree_size() == 100) {
		print_error("límite de directorios alcanzado");
	} else {
		dirtree_add(path);
	}
	free(path);
}

static void cmd_mv() {
	if(num_args() != 2) show_usage();
	char *path = extract_arg(1);
	char *new_path = extract_arg(2);

	if(dirtree_contains(path)) {
		dirtree_move(path, new_path);
	} else if(filetable_contains(path)) {
		filetable_move(path, new_path);
	} else {
		print_error("ruta inexistente");
	}

	free(path);
	free(new_path);
}

static void cmd_nodes() {
	nodelist_print();
}

static void cmd_quit() {
	should_quit = true;
}

static void cmd_rename() {
	if(num_args() != 2) show_usage();
	char *path = extract_arg(1);
	char *new_name = extract_arg(2);

	if(dirtree_contains(path)) {
		dirtree_rename(path, new_name);
	} else if(filetable_contains(path)) {
		filetable_rename(path, new_name);
	} else {
		print_error("ruta inexistente");
	}

	free(path);
	free(new_name);
}

static void cmd_rm() {
	int nargs = num_args();
	if(nargs == 0) show_usage();
	char *path = extract_arg(1);

	char mode = 'f';
	if(*path == '-' && mstring_length(path) == 2) {
		mode = *(path + 1);
	}

	switch(mode) {
	case 'f':
		if(nargs != 1) show_usage();
		if(filetable_contains(path)) {
			filetable_remove(path);
		} else {
			print_error("archivo inexistente");
		}
		break;
	case 'd':
		if(nargs != 2) show_usage();
		mstring_format(&path, "%s", extract_arg(2));
		if(!dirtree_contains(path)) {
			print_error("directorio inexistente");
		} else if(dirtree_count(path) + filetable_count(path) > 0) {
			print_error("directorio no vacío");
		} else {
			dirtree_remove(path);
		}
		break;
	case 'b':
		if(nargs != 4) show_usage();
		if (mstring_toint(extract_arg(4)) > 1 || mstring_toint(extract_arg(4)) < 0){
			print_error("copia inválida - 0 ó 1");
		}else{
			mstring_format(&path, "%s", extract_arg(2));
			if(!filetable_contains(path)) {
				print_error("archivo inexistente");
			}else{
				t_yfile *yfile = filetable_find(path);
				bool getBlock (t_block *bloque){
					return mstring_toint(extract_arg(3)) == bloque->index;
				}
				t_block *block = mlist_find(yfile->blocks, getBlock);
				if(block == NULL){
					print_error("bloque inexistente");
				}else if(block->copies[mstring_toint(extract_arg(4))].node == NULL){
					print_error("copia inexistente");
					}else if(block->copies[0].node == NULL || block->copies[1].node == NULL){
						print_error("última copia");
					}else{
						filetable_rm_block(yfile, block, mstring_toint(extract_arg(4)));
						printf("copia %d del bloque %d del archivo %s eliminada.\n",
								mstring_toint(extract_arg(4)),
								mstring_toint(extract_arg(3)),
								path);
					}
			}
		}
		break;
	default:
		show_usage();
	}

	free(path);
}

static void cmd_status() {
	int nnodes = nodelist_length();
	int nfiles = filetable_size();
	int ndirs = dirtree_size();

	size_t ntotal = nodelist_blocks() * BLOCK_SIZE;
	size_t nusing = filetable_totalsize();
	size_t nfree = ntotal - nusing;

	float fusing = ntotal == 0 ? ntotal : nusing * 100.0 / ntotal;
	float ffree = 100.0 - fusing;

	char *btotal = mstring_bsize(ntotal);
	char *busing = mstring_bsize(nusing);
	char *bfree = mstring_bsize(nfree);

	printf("  %s Sistema formateado  %3i nodo%s        %10s usado (%5.1f%%)\n", fs.formatted ? "✓" : "×", nnodes, nnodes == 1 ? " " : "s", busing, fusing);
	printf("  %s Sistema estable     %3i archivo%s     %10s libre (%5.1f%%)\n", filetable_stable() ? "✓" : "×", nfiles, nfiles == 1 ? " " : "s", bfree, ffree);
	printf("  %s YAMA conectado      %3i directorio%s  %10s total\n", fs.yama_connected ? "✓" : "×", ndirs, ndirs == 1 ? " " : "s", btotal);

	free(btotal);
	free(busing);
	free(bfree);
}

static void cmd_tree() {
	dirtree_print();
}

// ========== Funciones para libreadline ==========

static void execute_line(const char *line) {
	char *cmd = (char*) line;
	char zero = '\0';
	char *args = &zero;

	char *space = strchr(line, ' ');
	if(space) {
		*space = '\0';
		args = space + 1;
		mstring_trim(args);
	}

	t_command *command = find_command(cmd);
	if(!command) {
		command_not_found(cmd);
		return;
	}

	if(!fs.formatted
			&& !mstring_equal(command->name, "format")
			&& !mstring_equal(command->name, "help")
			&& !mstring_equal(command->name, "nodes")
			&& !mstring_equal(command->name, "status")
			&& !mstring_equal(command->name, "clear")
			&& !mstring_equal(command->name, "quit")) {
		printf("\nEl Filesystem no se encuentra formateado.\n"
				"Para poder operar proceda a formatear con el comando <<format>>\n");
		return;
	}

	current_cmd = command - commands;
	current_args = args;
	(*(command->func))();
}

static t_command *find_command(const char *name) {
	for(int i = 0; commands[i].name; i++) {
		if(strcmp(name, commands[i].name) == 0) {
			return &commands[i];
		}
	}

	return NULL;
}

static void command_not_found(const char *name) {
	printf("No existe el comando '%s'. Comandos posibles:\n", name);

	for(int i = 0; commands[i].name; i++) {
		if(i > 0 && i % 6 == 0) printf("\n");
		printf("%s\t", commands[i].name);
	}

	printf("\n");
}

static char **rl_completion(const char *text, int start, int end) {
	return rl_completion_matches(text, &rl_generator);
}

static char *rl_generator(const char *text, int state) {
	static int list_index, len;
	char *name;

	if(!state) {
		list_index = 0;
		len = strlen(text);
	}

	while((name = commands[list_index++].name)) {
		if(strncmp(name, text, len) == 0)
			return strdup(name);
	}

	return NULL;
}
