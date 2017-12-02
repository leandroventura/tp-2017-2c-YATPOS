#include "server.h"

#include <config.h>
#include <log.h>
#include <mstring.h>
#include <process.h>
#include <protocol.h>
#include <serial.h>
#include <socket.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <struct.h>
#include <thread.h>
#include "funcionesYAMA.h"
#include "YAMA.h"
#include "mstring.h"

static bool esLaPrimeraVezQueReciboLosNodos;

void listen_to_master() {
	esLaPrimeraVezQueReciboLosNodos = true;
	log_inform("Escuchando puertos de master");
	t_socket sv_sock = socket_init(NULL, config_get("MASTER_PUERTO"));
	t_fdset sockets = socket_set_create();
	socket_set_add(sv_sock, &sockets);

	socket_set_add(yama.fs_socket,&sockets);

	while(true) {
		t_fdset selected = socket_select(sockets);

		for(t_socket sock = 3; sock <= sockets.max; sock++) {
			if(!socket_set_contains(sock, &selected)) continue;
			if(sock == sv_sock) {
				t_socket cli_sock = socket_accept(sv_sock);
				t_packet packet = protocol_receive_packet(cli_sock);
				if(packet.operation == OP_HANDSHAKE && packet.sender == PROC_MASTER) {
					socket_set_add(cli_sock, &sockets);
					log_inform("Conectado proceso Master por socket %i", cli_sock);
					log_inform("IdCliente otorgado: %d ",numeroJob);
					t_packet packetID = protocol_packet(OP_IDJOB, serial_pack("i",numeroJob));
					protocol_send_packet(packetID,cli_sock);
					numeroJob++;
				} else {
					socket_close(cli_sock);
				}
				serial_destroy(packet.content);
			}
			else if(sock == yama.fs_socket){
				FinalizarEjecucion(-1,-1);
			}
			else {
				t_packet packetOperacion = protocol_receive_packet(sock);
				if(packetOperacion.operation == OP_UNDEFINED) {
					log_report("Desconexion del master: %d", sock);
					int idJob = buscarIdJobParaMasterCaido(sock);
					if(idJob > 0){
						eliminarEstadosMultiples(sock,idJob, "Error");
					}
					socket_close(sock);
					socket_set_remove(sock, &sockets);
					continue;
				}

				switch(packetOperacion.operation) {
				case OP_INIT_JOB:
					{
						t_pedidoTrans* pedidoInicio = serial_unpackPedido(packetOperacion.content);
						log_inform("Inicio de job nuevo :%d",pedidoInicio->idJOB);
						t_serial* file_serial = serial_pack("s",pedidoInicio->file);
						requerirInformacionFilesystem(file_serial);
						t_yfile* Datosfile = reciboInformacionSolicitada(pedidoInicio->idJOB,sock);
						if(Datosfile->size>0){
							int tamaniolistaNodos = mlist_length(listaNodosActivos);
							if(tamaniolistaNodos == 0){
								log_report("Job: %d abortado",pedidoInicio->idJOB);
								avisarErrorMaster(pedidoInicio->idJOB, sock,ERROR_PLANIFICACION);
							}
							else{

								completarPrimeraVez();
								t_workerPlanificacion planificador[tamaniolistaNodos];
								entreAPlanificar = true;
								thread_sleep(retardoPlanificacion);
								planificar(planificador, tamaniolistaNodos,Datosfile->blocks);
								if(recibiSenial){
									 config_reload();
									 retardoPlanificacion = atoi(config_get("RETARDO_PLANIFICACION"));
									 strcpy(algoritmoBalanceo,config_get("ALGORITMO_BALANCEO"));
									 log_inform("Modificacion del retardo a :%d || Modificacion del algoritmo a:%s",retardoPlanificacion,algoritmoBalanceo);
									 recibiSenial = false;
								}
								entreAPlanificar = false;
								agregarCargaNodoSegunLoPlanificado(pedidoInicio->idJOB, planificador, tamaniolistaNodos);
								enviarEtapa_transformacion_Master(pedidoInicio->idJOB,tamaniolistaNodos,planificador,Datosfile->blocks,sock);
							}

						}
					}
					break;
				case OP_TRANSFORMACION_LISTA :
					{respuestaOperacionTranf* finalizoOperacion = serial_unpackRespuestaOperacion(packetOperacion.content);

					if(finalizoOperacion->response == -1){
						entreAPlanificar = true;
						thread_sleep(retardoPlanificacion);
						replanificacion(finalizoOperacion->nodo,finalizoOperacion->file,sock,finalizoOperacion->idJOB);
						if(recibiSenial){
							 config_reload();
							 retardoPlanificacion = atoi(config_get("RETARDO_PLANIFICACION"));
							 strcpy(algoritmoBalanceo,config_get("ALGORITMO_BALANCEO"));
							 log_inform("Modificacion del retardo a :%d || Modificacion del algoritmo a:%s",retardoPlanificacion,algoritmoBalanceo);
							 recibiSenial = false;
						}
						entreAPlanificar = false;
					}
					else{
						log_inform("Transformacion terminada para :%d bloque: %d",finalizoOperacion->idJOB,finalizoOperacion->bloque);
						actualizoTablaEstado(finalizoOperacion->nodo,finalizoOperacion->bloque,sock,finalizoOperacion->idJOB,"FinalizadoOK");
						if(verificoFinalizacionTransformacion(finalizoOperacion->nodo,sock,finalizoOperacion->idJOB)){
							t_infoNodo* IP_PUERTOnodo = BuscoIP_PUERTO(finalizoOperacion->nodo);
							mlist_t* archivosTemporales_Transf = BuscoArchivosTemporales(finalizoOperacion->nodo,sock,finalizoOperacion->idJOB);
							char* temporal_local = generarNombreTemporal_local(finalizoOperacion->nodo,sock,finalizoOperacion->idJOB);
							mandarEtapaReduccionLocal(finalizoOperacion->idJOB,sock,finalizoOperacion->nodo,IP_PUERTOnodo,archivosTemporales_Transf,temporal_local);
						}
					}
					}
				break;
				case OP_REDUCCION_LOCAL_LISTA:
					{respuestaOperacion* finalizoRL = serial_unpackrespuestaOperacion(packetOperacion.content);

					if(finalizoRL->response == -1){
						abortarJob(finalizoRL->idJOB, sock,ERROR_REDUCCION_LOCAL);

					}
					else{

						log_inform("Reduccion local terminada para :%d nodo: %s",finalizoRL->idJOB,finalizoRL->nodo);
						actualizoTablaEstado(finalizoRL->nodo,-1,sock,finalizoRL->idJOB,"FinalizadoOK");
						if(verificoFinalizacionRl(finalizoRL->idJOB,sock)){
							mandarEtapaReduccionGL(sock,finalizoRL->idJOB);
						}
					}
					}
				break;
				case OP_REDUCCION_GLOBAL_LISTA:
					{respuestaOperacion* finalizoRG = serial_unpackrespuestaOperacion(packetOperacion.content);

					if(finalizoRG->response == -1){
						finalizarJobGlobal(finalizoRG->idJOB,sock,ERROR_REDUCCION_GLOBAL,"Error");

					}
					else{
						log_inform("Etapa de reduccion global terminada para job: %d",finalizoRG->idJOB);
						actualizoTablaEstado(finalizoRG->nodo,-2,sock,finalizoRG->idJOB,"FinalizadoOK");
						mandarEtapaAlmacenadoFinal(finalizoRG->nodo,sock,finalizoRG->idJOB);

					}
					}
				break;
				case OP_ALMACENAMIENTO_LISTA:
					{respuestaOperacion* finalizoAF = serial_unpackrespuestaOperacion(packetOperacion.content);
					if(finalizoAF->response == 0){
						finalizarJobGlobal(finalizoAF->idJOB,sock,ERROR_ALMACENAMIENTO_FINAL,"FinalizadoOK");
						log_inform("Almacenamiento final terminada para :%d",finalizoAF->idJOB);
					}
					else{
						finalizarJobGlobal(finalizoAF->idJOB,sock,ERROR_ALMACENAMIENTO_FINAL,"Error");
					}
					}
				break;
				default:
					log_report("Operación desconocida: %d de master: %d", packetOperacion.operation,sock);
				break;

				}
			}
		}
	}
}


void completarPrimeraVez(){
	if(esLaPrimeraVezQueReciboLosNodos){
			listaCargaPorNodo = mlist_create();
			int i;
			for(i = 0; i < mlist_length(listaNodosActivos); i++){
				t_infoNodo* datosNodo = mlist_get(listaNodosActivos, i);
				crearCargaPorNodo(datosNodo->nodo);
				esLaPrimeraVezQueReciboLosNodos = false;
			}
	}
	else if(mlist_length(listaNodosActivos)>mlist_length(listaCargaPorNodo)){
		int i;
		for(i = 0; i < mlist_length(listaNodosActivos); i++){
			t_infoNodo* datosNodo = mlist_get(listaNodosActivos, i);

			bool esNodoBuscado(void * unaCargaNodo){
			 return string_equals_ignore_case( ((t_cargaPorNodo *) unaCargaNodo)->nodo , datosNodo->nodo);
			}

			if(!mlist_any(listaCargaPorNodo,esNodoBuscado)){
				crearCargaPorNodo(datosNodo->nodo);

			}
		}
	}
}

void crearCargaPorNodo(char* nombreNodo){
 t_cargaPorNodo* cargaPorNodo = malloc(sizeof(t_cargaPorNodo));
 cargaPorNodo->nodo = malloc(sizeof(nombreNodo) + 1);
 cargaPorNodo->nodo = strcpy(cargaPorNodo->nodo, nombreNodo);
 cargaPorNodo->cargaActual = 0;
 cargaPorNodo->cargaHistorica = 0;
 cargaPorNodo->cargaPorJob = mlist_create();
 mlist_append(listaCargaPorNodo, cargaPorNodo);
}


void agregarCargaNodoSegunLoPlanificado(int job, t_workerPlanificacion planificador[], int tamanioPlanificador){
	int i, posicionObtenida;
	for(i = 0; i < tamanioPlanificador ; i++){
		bool condicion(void* cargaNodotraida){
			return string_equals_ignore_case( ((t_cargaPorNodo *) cargaNodotraida)->nodo, planificador[i].nombreWorker );
		}
		posicionObtenida = mlist_index(listaCargaPorNodo, condicion);
		void * cargaNodoObtenida = mlist_get(listaCargaPorNodo, posicionObtenida);

		t_cargaPorNodo * cargaNodo = (t_cargaPorNodo *) cargaNodoObtenida;
		cargaNodo->cargaActual += mlist_length(planificador[i].bloque);
		cargaNodo->cargaHistorica += mlist_length(planificador[i].bloque);

		log_inform("Planificado Nodo: %s, Carga Actual: %d, Carga Historica: %d", cargaNodo->nodo, cargaNodo->cargaActual, cargaNodo->cargaHistorica);

		t_cargaPorJob * cargaPorJob = malloc(sizeof(t_cargaPorJob));
		cargaPorJob->job = job;
		cargaPorJob->cargaDelJob = mlist_length(planificador[i].bloque);
		mlist_append(cargaNodo->cargaPorJob,cargaPorJob);
		mlist_replace(listaCargaPorNodo, posicionObtenida, cargaNodo);

	}
}


void enviarEtapa_transformacion_Master(int job, int tamaniolistaNodos,t_workerPlanificacion planificador[],mlist_t* listabloques,int sock){
	mlist_t* lista = mlist_create();
	int i, j = 0;
	int nroIndex;
	for(i = 0; i < tamaniolistaNodos; i++){
		for(j = 0; j < mlist_length(planificador[i].bloque); j++){
			void* nroIndexOBtenido = mlist_get(planificador[i].bloque, j);
			nroIndex = (int)nroIndexOBtenido;
			t_infoNodo* datosNodoAEnviar = mlist_get(listaNodosActivos, i);
			bool condition(void* datosDeUnBloque){
				return ((t_block *) datosDeUnBloque)->index == nroIndex ? true : false;
			}
			void* datosDeUnBloqueObtenido = mlist_find(listabloques,(void*) condition);
			t_block* datosDeUnBloque = (t_block*)datosDeUnBloqueObtenido;
			int nroBloque;
			if(datosDeUnBloque->copies[0].node != NULL){
				if(!strcmp(datosDeUnBloque->copies[0].node, datosNodoAEnviar->nodo)){
					nroBloque = datosDeUnBloque->copies[0].blockno;
				} else{
					nroBloque = datosDeUnBloque->copies[1].blockno;
				}
			}else{
				nroBloque = datosDeUnBloque->copies[1].blockno;
			}

			char* nombreArchivoTemporal = malloc(sizeof(char)*21);

			strcpy(nombreArchivoTemporal, generarNombreArchivoTemporalTransf(job,sock, nroBloque));

			tEtapaTransformacion* et = new_etapa_transformacion(datosNodoAEnviar->nodo,datosNodoAEnviar->ip,datosNodoAEnviar->puerto,nroBloque, datosDeUnBloque->size,nombreArchivoTemporal); //ROMPE EN ESTA funcion
			mlist_append(lista,et);
			agregarAtablaEstado(job,datosNodoAEnviar->nodo,sock,nroBloque,"Transformacion",nombreArchivoTemporal,"En proceso");
		}
	}
	mandar_etapa_transformacion(lista,sock);
	log_inform("Etapa de transformacion iniciada para job: %d",job);

}

bool verificoFinalizacionTransformacion(char* nodo,int socket,int job){
	bool esNodoBuscado(void* estadoTarea){
		printf("comparacion \n");
		return string_equals_ignore_case(((t_Estado *) estadoTarea)->nodo,nodo) && ((t_Estado *) estadoTarea)->master == socket && !string_equals_ignore_case(((t_Estado *) estadoTarea)->estado, "Error") && ((t_Estado *) estadoTarea)->job == job;
	}

	mlist_t* listaFiltradaDelNodo = mlist_filter(listaEstados, (void*)esNodoBuscado);
	printf("filtre la lista \n");
	bool FinalizacionDeTransf_Nodo(void* estadoTarea){
			  	return string_equals_ignore_case(((t_Estado *) estadoTarea)->estado,"FinalizadoOK");
	}
	printf("termine de comparar \n");
	bool terminarontodos = mlist_all(listaFiltradaDelNodo, (void*) FinalizacionDeTransf_Nodo);
	mlist_destroy(listaFiltradaDelNodo,NULL);
	return terminarontodos;

}

void mandarEtapaReduccionLocal(int job, int socket,char* nodo,t_infoNodo* nodo_worker,mlist_t* archivos_transf,char* archivoTemporal_local){
	tEtapaReduccionLocal* etapaRL = new_etapa_rl(nodo,nodo_worker->ip,nodo_worker->puerto,archivos_transf,archivoTemporal_local);
	agregarAtablaEstado(job,nodo,socket,-1,"ReduccionLocal",archivoTemporal_local,"En proceso");
	mandar_etapa_rl(etapaRL,socket);
	log_inform("Etapa de reduccion local iniciada para job: %d|| Nodo: %s",job, nodo);

}

t_infoNodo* BuscoIP_PUERTO(char* nodo){
	t_infoNodo* Nodo;
	bool esNodoBuscado(void* unNodoConectado){
			  	return mstring_equali(((t_infoNodo *) unNodoConectado)->nodo,nodo);
	}

	 void* NodoObtenido = mlist_find(listaNodosActivos, (void*) esNodoBuscado);
	 Nodo= (t_infoNodo*) NodoObtenido;
	 return Nodo;
}

mlist_t* BuscoArchivosTemporales(char* nodo,int socket,int job){

	bool esNodoBuscado(void* estadoTarea){
		  	return string_equals_ignore_case(((t_Estado *) estadoTarea)->nodo,nodo) && ((t_Estado *) estadoTarea)->master == socket && ((t_Estado *) estadoTarea)->job == job && string_equals_ignore_case(((t_Estado *) estadoTarea)->estado, "FinalizadoOK");
	}

	mlist_t* listaFiltradaDelNodo = mlist_filter(listaEstados, (void*)esNodoBuscado);

	 char* getArchivoTemporal(void* unEstadoObtenido){
		t_Estado* Estado;
		Estado = (t_Estado*) unEstadoObtenido;
		return Estado->archivoTemporal;
	}

	mlist_t* archivosTemporales = mlist_map(listaFiltradaDelNodo, (void*) getArchivoTemporal);
	return archivosTemporales;
}


char* generarNombreTemporal_local(char* nodo,int master,int job){
	char* nombreArchivoTemporal = malloc(sizeof(char)*21);
	sprintf(nombreArchivoTemporal,"/tmp/J%dMaster%d-%s",job,master,nodo);
	return nombreArchivoTemporal;

}




bool verificoFinalizacionRl(int job, int master){
		bool esJobBuscado(void* estadoTarea){
				  	return ((t_Estado *) estadoTarea)->master == master &&
				  			((t_Estado *) estadoTarea)->job == job
							&& ( string_equals_ignore_case(((t_Estado *) estadoTarea)->etapa, "ReduccionLocal")
							|| string_equals_ignore_case(((t_Estado *) estadoTarea)->etapa, "Transformacion"))
							&& !string_equals_ignore_case(((t_Estado *) estadoTarea)->estado,"Error");
		}

		mlist_t* listaFiltradaDelNodo = mlist_filter(listaEstados, (void*)esJobBuscado);

		bool FinalizacionDeRl_Job(void* estadoTarea){
					  	return string_equals_ignore_case(((t_Estado *) estadoTarea)->estado,"FinalizadoOK");
		}

			return mlist_all(listaFiltradaDelNodo, (void*) FinalizacionDeRl_Job);

}

void mandarEtapaReduccionGL(int master,int job){
	mlist_t* listaRG = mlist_create();
	mlist_t* NodosRL = BuscoNodos(master,job);
	char* nodo = seleccionarEncargado(NodosRL, job);
	char* nombreRG = generarArchivoRG(master,job);
	int i = 0;
	for(i=0; i < mlist_length(NodosRL); i++){
		void* unNodo_sendObtenido	= mlist_get(NodosRL,i);
		t_nodotemporal* unNodo_send = (t_nodotemporal*) unNodo_sendObtenido;
		t_infoNodo* infoNodo = BuscoIP_PUERTO(unNodo_send->nodo);
		if(string_equals_ignore_case(unNodo_send->nodo,nodo)){
			tEtapaReduccionGlobal* etapaRG = new_etapa_rg(unNodo_send->nodo,infoNodo->ip,infoNodo->puerto,unNodo_send->archivoTemporal,nombreRG,"SI");
			mlist_append(listaRG,etapaRG);
		}
		else{
			tEtapaReduccionGlobal* etapaRG = new_etapa_rg(unNodo_send->nodo,infoNodo->ip,infoNodo->puerto,unNodo_send->archivoTemporal,"-1","NO");
			mlist_append(listaRG,etapaRG);
		}
	}
	agregarAtablaEstado(job,nodo,master,-2,"ReduccionGlobal",nombreRG,"En proceso");
	mandar_etapa_rg(listaRG,master);
	log_inform("Etapa de reduccion global iniciada para job: %d",job);

}

mlist_t* BuscoNodos(int master, int job){

	bool NodosReadyRL(void* unEstadoObtenido){
		t_Estado* Estado = (t_Estado*) unEstadoObtenido;
		return Estado->master == master && Estado->job == job && string_equals_ignore_case(Estado->etapa, "ReduccionLocal") ? true : false;
	}

	mlist_t* listaFiltrada = mlist_filter(listaEstados,(void*) NodosReadyRL);

	t_nodotemporal* getNodoTemporal(void* unEstadoObtenido){
			t_Estado* Estado;
			t_nodotemporal* info_enviar_toRG = malloc(sizeof(t_nodotemporal));
			Estado = (t_Estado*) unEstadoObtenido;
			info_enviar_toRG->nodo = Estado->nodo;
			info_enviar_toRG->archivoTemporal = Estado->archivoTemporal;
			return info_enviar_toRG;
		}

	mlist_t* Nodos = mlist_map(listaFiltrada, (void*) getNodoTemporal);
	return Nodos;
}


char* seleccionarEncargado(mlist_t* NodosRL, int job){

	char* nombreNodo(void* nodoRL){
		return ((t_nodotemporal*) nodoRL)->nodo;
	}

	mlist_t* listaDeNombreNodos = mlist_map(NodosRL, (void*) nombreNodo);

	bool condicionAComparar(void * nodo1, void * nodo2){
		return cargaActual((char*) nodo1) <= cargaActual((char*) nodo2);
	}

	mlist_sort(listaDeNombreNodos, condicionAComparar);
	void * nodoObtenido = mlist_first(listaDeNombreNodos);
	char* nodoConMenorCarga = (char*) nodoObtenido;

	int cantidadAAumentar = 1 + ((mlist_length(NodosRL) - 1) / 2);

	actualizarCargaDelNodo(nodoConMenorCarga, job, 1, cantidadAAumentar);
	return nodoConMenorCarga;

}


char* generarArchivoRG(int master, int job){
	char* nombreArchivoTemporal = malloc(sizeof(char)*21);
	sprintf(nombreArchivoTemporal,"/tmp/J%dMaster%d-final",job,master);
	return nombreArchivoTemporal;

}

void mandarEtapaAlmacenadoFinal(char* nodo,int master,int idJOB){
	t_infoNodo* nodo_send = BuscoIP_PUERTO(nodo);
	char* archivo_AF = BuscoNodoEncargado(idJOB);
	tAlmacenadoFinal* af = new_etapa_af(nodo,nodo_send->ip,nodo_send->puerto,archivo_AF);
	agregarAtablaEstado(idJOB,nodo,master,-3,"AlmacenamientoFinal",archivo_AF,"En proceso");
	mandar_etapa_af(af,master);
	log_inform("Etapa de almacenamiento final iniciado para job: %d",idJOB);

}

char* BuscoNodoEncargado(int job){

	bool esEncargado(void* unEstadoObtenido){
			t_Estado* Estado = (t_Estado*) unEstadoObtenido;
			return Estado->job == job && string_equals_ignore_case(Estado->etapa, "ReduccionGlobal") ? true : false;
	}


	void* nodoEncargadoObtenido = mlist_find(listaEstados, (void*) esEncargado);
	t_Estado* nodoEncargado = (t_Estado*) nodoEncargadoObtenido;
	return nodoEncargado->archivoTemporal;
}

