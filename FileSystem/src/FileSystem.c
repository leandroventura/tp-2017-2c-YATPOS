#include "FileSystem.h"

#include <path.h>
#include <process.h>
#include <stdbool.h>
#include <stdlib.h>
#include <thread.h>
#include <log.h>

#include "console.h"
#include "dirtree.h"
#include "filetable.h"
#include "nodelist.h"
#include "server.h"

static void init(void);
static void clear_previous_state(void);
static void term(void);

// ========== Funciones públicas ==========

int main(int argc, char *argv[]) {
	process_init();

	if(argc == 2 && mstring_equal(argv[1], "--clean")) {
		clear_previous_state();
	}

	init();
	server();
	console();
	term();

	return EXIT_SUCCESS;
}

// ========== Funciones privadas ==========

static void init() {
	dirtree_init();
	filetable_init();
	nodelist_init();
	fs.formatted = nodelist_length() > 0;
	fs.yama_connected = false;
	log_inform("Inicializado %s", fs.formatted ? "con estado anterior" : "sin formato");
}

static void clear_previous_state() {
	path_remove("metadata");
	fs.formatted = false;
	log_inform("Estado anterior eliminado");
}

static void term() {
	thread_killall();
	nodelist_term();
	filetable_term();
	dirtree_term();
	process_term();
}
