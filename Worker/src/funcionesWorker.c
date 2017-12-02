#include "funcionesWorker.h"

t_file* crearScript(char * bufferScript, int etapa,int nroSocket) {
	int aux, auxChmod;
	char mode[] = "0777";
	t_file*script;
	aux = string_length(bufferScript);
	const char * nombreScript = path_temp();
	if (etapa == OP_INICIAR_TRANSFORMACION){
		script = file_create(nombreScript);
		log_print("SCRIPT DE TRANSFORMACION: %s CREADO PARA SOCKET: %d",nombreScript,nroSocket);
	}
	else if (etapa == OP_INICIAR_REDUCCION_LOCAL){
		script = file_create(nombreScript);
		log_print("SCRIPT DE REDUCCION LOCAL: %s CREADO PARA SOCKET: %d",nombreScript,nroSocket);
	}
	else{
		script = file_create(nombreScript);
		log_print("SCRIPT DE REDUCCION Global: %s CREADO PARA SOCKET: %d",nombreScript,nroSocket);
	}
	//file_open(file_path(script));
	fwrite(bufferScript, sizeof(char), aux, file_pointer(script));
	auxChmod = strtol(mode, 0, 8);
	if (chmod(file_path(script), auxChmod) < 0) {
		log_report("NO SE PUDO DAR PERMISOS DE EJECUCION AL ARCHIVO");
	}

	fclose(file_pointer(script));
	return script;

}

t_file * crearArchivo(char * bufferArchivo, int size, char*nombreArchivo) {
	t_file * archivo = file_create(nombreArchivo);
	file_open(file_path(archivo));
	fwrite(bufferArchivo, sizeof(char), size, file_pointer(archivo));
	fclose(file_pointer(archivo));
	return archivo;
}

tEtapaReduccionLocalWorker * etapa_rl_unpack_bis(t_serial * serial) {
	tEtapaReduccionLocalWorker * rl = malloc(
			sizeof(tEtapaReduccionLocalWorker));
	char * elemento;
	rl->archivosTemporales = mlist_create();
	serial_remove(serial, "s", &rl->script); //En realidad no es nodo, pero como nodo es (char*) aprovecho para almacenar el script
	serial_remove(serial, "i", &rl->lenLista);
	for (int i = 0; i < rl->lenLista; i++) {
		serial_remove(serial, "s", &elemento);
		char * aux = mstring_create("%s%s", system_userdir(), elemento);
		log_print("ARCHIVO RECIBIDO: %s",aux);
		mlist_append(rl->archivosTemporales, aux);
	}
	log_print("CANTIDAD DE ARCHIVOS A REDUCIR: %d",mlist_length(rl->archivosTemporales));
	serial_remove(serial, "s", &rl->archivoTemporal);
	log_print("ARCHIVO TEMPORAL:%s",rl->archivoTemporal);
	return rl;
}

tEtapaReduccionGlobalWorker * rg_unpack(t_serial * serial) {
	log_print("DESENPACANDO RG");
	tEtapaReduccionGlobalWorker * rg_w = malloc(
			sizeof(tEtapaReduccionGlobalWorker));
	rg_w->datosWorker = mlist_create();

	serial_remove(serial, "s", &rg_w->scriptReduccion);
	serial_remove(serial, "i", &rg_w->lenLista);

	for (int i = 0; i < rg_w->lenLista; i++) {
		tEtapaReduccionGlobal *rg = malloc(sizeof(tEtapaReduccionGlobal));
		serial_remove(serial, "sssss", &rg->nodo, &rg->ip, &rg->puerto,
				&rg->archivo_temporal_de_rl, &rg->encargado);
		printf("Archivo temporal_de_rl: %s\n",rg->archivo_temporal_de_rl);
		mlist_append(rg_w->datosWorker, rg);
	}

	serial_remove(serial, "s", &rg_w->archivoEtapa);

	return rg_w;
}

t_socket connect_to_worker(const char *ip, const char *port) { // La ip y el puerto son obtenidos mediante YAMA
	t_socket socket = socket_connect(ip, port);
	if (socket == -1) {
		log_report("Worker no está corriendo en %s:%s", ip, port);
		exit(EXIT_FAILURE);
	}

	protocol_send_handshake(socket);
	log_inform("Conectado a Worker en %s:%s por el socket %i", ip, port,
			socket);
	return socket;
}

void ejecutarComando(char * command, int socketAceptado) {
	int status;
	printf("COMANDO:%s\n", command);
	system(command);
	if ((status = system(command)) < 0) {
		log_report(
				"NO SE PUDO EJECTUAR EL COMANDO EN SYSTEM, FALLA REDUCCION LOCAL");
		free(command);
		protocol_send_response(socketAceptado, -1);
		exit(1);
	} else {
		log_print("Se manda respuesta correcta a master");
		protocol_send_response(socketAceptado, 1);
	}
}

char * crearListaParaReducir(tEtapaReduccionGlobalWorker * rg) {
	log_print("CREANDO RUTA");
	mlist_t * archivosAReducir = mlist_create();
	mlist_t * socketsAWorker = mlist_create();
	int socket;
	int size;
	char * bufferArchivoTemporal;
	char * archivoAReducir ;
	if (rg->lenLista > 1) {
		printf("tamanio lista:%d\n",rg->lenLista);
		archivoAReducir = mstring_create("%s/%s",system_userdir(),path_temp());
		for (int i = 0; i < rg->lenLista; i++) {
			rg->rg = mlist_get(rg->datosWorker, i);
			if (!string_equals_ignore_case(rg->rg->encargado, SI)) {
				t_socket socketWorker = connect_to_worker(rg->rg->ip,
						rg->rg->puerto);
				socket = socketWorker;
				mlist_append(socketsAWorker, &socket);
				t_packet paquete;
				paquete.content = serial_pack("s",
						rg->rg->archivo_temporal_de_rl);
				paquete.operation = OP_MANDAR_ARCHIVO;
				protocol_send_packet(paquete, socketWorker);
				paquete = protocol_receive_packet(socketWorker);
				serial_unpack(paquete.content, "si", &bufferArchivoTemporal, &size);
				t_file * archivo = crearArchivo(bufferArchivoTemporal, size,
						rg->rg->archivo_temporal_de_rl);
				mlist_append(archivosAReducir, (char *)file_path(archivo));

			}else{
				log_print("SOY ENCARGADO para generar el archivo: %s",rg->archivoEtapa);
				char * aux = mstring_create("%s%s",system_userdir(),rg->rg->archivo_temporal_de_rl);
				mlist_append(archivosAReducir, aux);
			}
		}
		path_merge(archivosAReducir, archivoAReducir);
	}else{
		log_print("ESTO SIGNIFICA QUE SOLO HAY 1 NODO LABURANDO");
		rg->rg = mlist_get(rg->datosWorker,0);
		archivoAReducir = mstring_create("%s%s",system_userdir(),rg->rg->archivo_temporal_de_rl);
		log_print("Archivo a reducir: %s\n",archivoAReducir);
		return archivoAReducir;
	}
	printf("Archivo a reducir: %s\n",archivoAReducir);
	free(bufferArchivoTemporal);
	return archivoAReducir;
}

tEtapaAlmacenamientoWorker * af_unpack(t_serial * serial) {
	log_print("DESAMPAQUETANDO AF");
	tEtapaAlmacenamientoWorker * af = malloc(sizeof af);
	serial_remove(serial, "ss", &af->archivoReduccion, &af->archivoFinal);
	log_print("Archivo de reduccion: %s , archivoFinal: %s \n",af->archivoReduccion,af->archivoFinal);
	return af;
}

int connect_to_filesystem() {
	t_socket socket = socket_connect(config_get("IP_FILESYSTEM"),
			config_get("PUERTO_FILESYSTEM"));
	protocol_send_handshake(socket);
	int response = protocol_receive_response(socket);
	if (response == RESPONSE_ERROR) {
		log_print("Conexión rechazada. El FileSystem no se encuentra estable.");
		socket_close(socket);
	} else {
		log_inform("Conectado a proceso FileSystem por socket %i", socket);
		socketFileSystem = socket;
	}
	return response;
}

//void signal_handler(){
//	pid_t pid;
//	int status;
//	pid = waitpid(-1,&status,WNOHANG);
//	log_inform("proceso hijo de pid: %d",pid);
//}
void listen_to_master() {
	log_print("Escuchando puertos");
	socketEscuchaMaster = socket_init(NULL, config_get("PUERTO_WORKER"));
	t_socket socketAceptado;

	while (true) {
		pid_t pid;
		socketAceptado = socket_accept(socketEscuchaMaster);
		printf("Socket Aceptado:%d\n", socketAceptado);
		if (protocol_receive_handshake(socketAceptado, PROC_MASTER)) {
			log_print("HANDSHAKE CON PROC_MASTER");
			if ((pid = fork()) == 0) {
				log_print("PROCESO_HIJO:%d", pid);
				t_packet packet = protocol_receive_packet(socketAceptado);
				switch (packet.operation) {
				case OP_INICIAR_TRANSFORMACION:
					log_print("OP_INICIAR_TRANSFORMACION");
					tEtapaTransformacionWorker * trans = malloc(sizeof(tEtapaTransformacionWorker));
					char * bufferScript, *archivoEtapa;
					t_file*scriptTransformacion;
					serial_unpack(packet.content, "ssii", &bufferScript,
							&archivoEtapa, &trans->bloque,
							&trans->bytesOcupados);
					scriptTransformacion = crearScript(bufferScript,
							OP_INICIAR_TRANSFORMACION,socketAceptado);

					bool tt_ok = block_transform(trans->bloque, trans->bytesOcupados, file_path(scriptTransformacion), archivoEtapa,trans->bytesOcupados);
					if (tt_ok) {
						log_print("MANDANDO RESPUESTA CORRECTA A MASTER");
						protocol_send_response(socketAceptado,RESPONSE_OK );
					} else {
						log_report("MANDANDO RESPUESTA INCORRECTA A MASTER");
						protocol_send_response(socketAceptado, RESPONSE_ERROR);
					}
					path_remove(file_path(scriptTransformacion));
					//serial_destroy(packet.content); //agrego esto
					free(bufferScript); //lo movi, originalmente estaba en linea 208
					exit(1);
					break;
				case OP_INICIAR_REDUCCION_LOCAL:
					log_print("OP_INICIAR_REDUCCION_LOCAL");
					char* archivoPreReduccion = path_temp();
					tEtapaReduccionLocalWorker* rl;
					rl = etapa_rl_unpack_bis(packet.content);
					t_file *scriptReduccion = crearScript(rl->script,
							OP_INICIAR_REDUCCION_LOCAL,socketAceptado);
					char * aux = mstring_create("%s/%s", system_userdir(),
							archivoPreReduccion);
					t_file * archivo = file_create(aux);
					log_print("archivoPreReduccion: %s,aux:%s, archivo:%s\n",archivoPreReduccion,aux,file_path(archivo));
					path_merge(rl->archivosTemporales, file_path(archivo));
					bool lr_ok = reducir_path(file_path(archivo), file_path(scriptReduccion), rl->archivoTemporal);
					if(lr_ok){
						log_print("MANDANDO RESPUESTA CORRECTA A MASTER");
					protocol_send_response(socketAceptado, lr_ok ? RESPONSE_OK : RESPONSE_ERROR);
					}else{
						log_report("MANDANDO RESPUESTA INCORRECTA A MASTER");
											protocol_send_response(socketAceptado, lr_ok ? RESPONSE_OK : RESPONSE_ERROR);
					}
					path_remove(file_path(scriptReduccion));
					path_remove(archivoPreReduccion);
					exit(1);
					break;
				case OP_INICIAR_REDUCCION_GLOBAL:
					log_print("OP_INICIAR_REDUCCION_GLOBAL");
					char *archivoAReducir;
					tEtapaReduccionGlobalWorker * rg;
					rg = rg_unpack(packet.content);
					t_file *scriptReduccionGlobal;
					break;
					scriptReduccionGlobal = crearScript(rg->scriptReduccion,
							OP_INICIAR_REDUCCION_GLOBAL,socketAceptado);
					archivoAReducir = crearListaParaReducir(rg);
					bool gr_ok = reducir_path(archivoAReducir, file_path(scriptReduccionGlobal), rg->archivoEtapa);
					protocol_send_response(socketAceptado, gr_ok ? RESPONSE_OK : RESPONSE_ERROR);
					path_remove(file_path(scriptReduccionGlobal));
					path_remove(archivoAReducir);
					exit(1);
					break;
				case OP_INICIAR_ALMACENAMIENTO:
					log_print("OP_INICIAR_ALMACENAMIENTO");
					tEtapaAlmacenamientoWorker * af;
					af = af_unpack(packet.content);
					t_file * archivoReduccion;
					char * bufferArchivoReduccion;
					char * aux2 = mstring_create("%s%s",system_userdir(),af->archivoReduccion);
					printf("Archivo reduccion:%s\n",aux2);
					archivoReduccion = file_open(aux2);
					bufferArchivoReduccion = file_map(archivoReduccion);
					t_serial *serialFileSystem = serial_pack("ssi", bufferArchivoReduccion, af->archivoFinal, file_size(archivoReduccion));
					printf("file_size: %d\n", file_size(archivoReduccion));
					t_packet paquete = protocol_packet(
							OP_INICIAR_ALMACENAMIENTO, serialFileSystem);
					int response = connect_to_filesystem();
					if (response < 0) {
						log_report("No se pudo conectar al filesystem");
						protocol_send_response(socketAceptado, -1);
					} else {
						protocol_send_packet(paquete, socketFileSystem);
						int estado = protocol_receive_response(
								socketFileSystem);
						if (estado == RESPONSE_OK) {
							log_print("Se informa a Master el termino del job");
							protocol_send_response(socketAceptado, RESPONSE_OK);
						} else {
							log_print("Se informa a Master la falla del job");
							protocol_send_response(socketAceptado, -2);
						}
					}
					exit(1);
					break;
				default:
					break;
				}
			} else if (pid > 0) {
				log_print("PROCESO_PADRE:%d", pid);
			} else if (pid < 0) {
				log_report("NO SE PUDO HACER EL FORK");
			}

		} else {
			log_print("Handshake de Worker Homologo");
			if ((pid = fork()) == 0) {
				log_print("Proceso hijo de worker homologo de pid: #%d", pid);
				t_packet paquete = protocol_receive_packet(socketAceptado);
				t_file * archivo;
				char * nombreDelArchivo;
				char *bufferArchivo;
				switch (paquete.operation) {
				case (OP_MANDAR_ARCHIVO): //OP_MANDAR_ARCHIVO
					serial_unpack(paquete.content, "s", &nombreDelArchivo);
					char * aux2 = mstring_create("%s%s",system_userdir(),nombreDelArchivo);
					log_print("NOMBRE DEL ARCHIVO A MANDAR A ENCARGADO: %s",nombreDelArchivo);
					archivo = file_open(aux2);
					bufferArchivo = file_map(archivo);
					paquete.content = serial_pack("si", bufferArchivo, file_size(archivo));
					paquete.operation = OP_MANDAR_ARCHIVO;
					protocol_send_packet(paquete, socketAceptado);
					file_unmap(archivo, bufferArchivo);
					exit(1);
					break;
				default:
					log_report("OP_UNDIFINED");
					exit(1);
					break;
				}
			} else if (pid > 0) {
				log_print("PROCESO PADRE HOMOLOG");
			} else if (pid < 0) {
				log_report("NO SE PUDO CREAR EL PROCESO HIJO HOMOLOGO");
			}
		}
	}
}

bool block_transform(int blockno, size_t size, const char *script, const char *output,int bytesOcupados) {
	char *scrpath = path_create(PTYPE_USER, script);
	if(!path_exists(scrpath)) {
		free(scrpath);
		return false;
	}
//
//	size_t start = blockno * BLOCK_SIZE + 1;
	char *datapath = path_create(PTYPE_YATPOS, config_get("RUTA_DATABIN"));
	char *outpath = path_create(PTYPE_YATPOS, output);
	//char *command = mstring_create("cat %s | tail -c %zi | head -c %zi | %s | sort > %s", datapath, start, size, scrpath, outpath);
	char * command; int offset;
	log_print("Transformo en el BLOQUE: %d, la cantidad de: %d bytes\n",bytesOcupados,blockno);
	if (blockno == 0) {
		offset = bytesOcupados;
		command = mstring_create("head -c %d < %s | %s | sort > %s%s",
				offset, datapath, scrpath, system_userdir(), outpath);
	} else {
		offset = blockno * BLOCK_SIZE + bytesOcupados;
		command = mstring_create(
				"head -c %d < %s | tail -c %d | %s | sort > %s%s", offset,
				datapath, bytesOcupados, scrpath, system_userdir(), outpath);
	}
	log_print("COMMAND: %s",command);


	int r = system(command);
	if (r < 0) {
		log_report("FALLO AL FORKEAR EN SYSTEM");

		//break;
	} else if (r == 127) {
		log_print("NO SE PUDO EJECTUAR EL COMMANDO");

		//break;
	}
	free(command);
	free(datapath);
	free(scrpath);
	free(outpath);
	printf("EL RESULTADO DE SYSTEM %d \n", r);
	return r ==0;
}
bool reducir_path(const char *input, const char *script, const char *output) {
	char *scrpath = path_create(PTYPE_USER, script);
	if(!path_exists(scrpath)) {
		free(scrpath);
		return false;
	}

	char *inpath = path_create(PTYPE_YATPOS, input);
	char *outpath = path_create(PTYPE_YATPOS, output);

	char *command = mstring_create("cat %s | %s > %s%s", inpath, scrpath, system_userdir(),outpath);

	int r = system(command);
	if(r<0){
		log_report("FALLO AL FORKEAR EN SYSTEM");
		free(command);
				//break;
	}else if(r == 127){
		log_print("NO SE PUDO EJECTUAR EL COMMANDO");
		free(command);
				//break;
	}
	log_print("COMMAND: %s",command);
	free(inpath);
	free(outpath);
	free(command);
	free(scrpath);
	return r == 0 ;
}
