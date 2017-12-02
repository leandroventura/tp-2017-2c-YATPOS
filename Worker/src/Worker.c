#include "funcionesWorker.h"

int main(int argc, char* argv[]) {
	if(argc == 2) process_node(argv[1]);
	process_init();
	listen_to_master();
	return EXIT_SUCCESS;
}
