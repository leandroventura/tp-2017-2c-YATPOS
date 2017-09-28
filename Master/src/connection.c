#include "connection.h"

#include <config.h>
#include <log.h>
#include <protocol.h>
#include <socket.h>
#include <stdlib.h>

#include "Master.h"

void connect_to_yama() {
	const char *ip = config_get("YAMA_IP");
	const char *port = config_get("YAMA_PUERTO");

	t_socket socket = socket_connect(ip, port);
	if(socket == -1) {
		log_report("YAMA no está corriendo en %s:%s", ip, port);
		exit(EXIT_FAILURE);
	}

	protocol_handshake(socket);
	log_inform("Conectado a YAMA en %s:%s por el socket %i", ip, port, socket);
	master.yama_socket = socket;
}

void request_job_for_file(const char *file) {
	t_serial content = serial_pack("s", file);
	t_packet packet = protocol_packet(OP_INIT_JOB, content);
	protocol_send(packet, master.yama_socket);
	free(content.data);
}

void connect_to_worker(const char *ip, const char *port) { // La ip y el puerto son obtenidos mediante YAMA
	t_socket socket = socket_connect(ip, port);
	if(socket == -1) {
		log_report("Worker no está corriendo en %s:%s", ip, port);
		exit(EXIT_FAILURE);
	}

	protocol_handshake(socket);
	log_inform("Conectado a Worker en %s:%s por el socket %i", ip, port, socket);
	master.worker_socket = socket;
}