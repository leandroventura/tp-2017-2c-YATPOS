#include <config.h>
#include <process.h>
#include "console.h"
#include "server.h"

#include <file.h>
#include <stdio.h>

int main() {
	process_init(FILESYS);
	config_load();
	escucharPuertosDataNodeYYama();
	console();
	return 0;
}
