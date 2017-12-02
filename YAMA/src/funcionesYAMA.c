#include "funcionesYAMA.h"
#include "YAMA.h"
#include "server.h"
#include <semaphore.h>
#include "mstring.h"

mlist_t * listaCargaPorNodo;

static int contadorBloquesSeguidosNoAsignados = 0;
static bool asigneBloquesDeArchivo = false;

void imprimirListaEstadosCompleta(){
	int i;
	printf("\nJob  Nodo    Bloque    Etapa          Estado\n");
	for(i=0;i< mlist_length(listaEstados);i++){
		void * nuevoEstado = mlist_get(listaEstados, i);
		t_Estado* estado = (t_Estado*) nuevoEstado;
		printf("%d  %s  %d  %s  %s \n", estado->job, estado->nodo, estado->block, estado->etapa, estado->estado);
	}
}

void planificar(t_workerPlanificacion planificador[], int tamaniolistaNodos, mlist_t* listaBloque){

	int posicionArray;
	llenarArrayPlanificador(planificador,tamaniolistaNodos,&posicionArray);
	int bloque = 0;
	while(!asigneBloquesDeArchivo){
		if(posicionArray == tamaniolistaNodos){
			posicionArray = 0;
		}
		verificarCondicion(tamaniolistaNodos, &posicionArray,planificador, &bloque, listaBloque);
	}

	asigneBloquesDeArchivo = false;
}

int availabilityClock(){
	return atoi(config_get("DISP_BASE"));
}

int cargaActual(char* nodo){
	bool condicion(void* cargaNodoRecibida){
		return string_equals_ignore_case(nodo, ((t_cargaPorNodo*) cargaNodoRecibida)->nodo);
	}
	void* cargaNodoObtenida = mlist_find(listaCargaPorNodo,condicion);

	t_cargaPorNodo* cargaNodo = (t_cargaPorNodo*) cargaNodoObtenida;
	return cargaNodo->cargaActual;
}

int Disponibilidad(int cargaMax, char* nodo){

	if(string_equals_ignore_case("CLOCK",algoritmoBalanceo)){
		return availabilityClock();
	}
	else{
		return availabilityClock() + ( cargaMax - cargaActual(nodo) );
	}
}

int obtenerCargaMaxima(){

	int carga(void* carga1){
		return ((t_cargaPorNodo*) carga1)->cargaActual;
	}
	mlist_t* listaDeCargas = mlist_map(listaCargaPorNodo, (void*) carga);
	bool mayor(void* carga1,void* carga2){
		return (int) carga1 >= (int)carga2;
	}

	mlist_sort(listaDeCargas, mayor);
	void* cargaMayorObtenido = mlist_first(listaDeCargas);
	int CargaMayor = (int) cargaMayorObtenido;
	int cargaActual = CargaMayor;
	mlist_destroy(listaDeCargas, NULL);
	return cargaActual;
}



void llenarArrayPlanificador(t_workerPlanificacion planificador[],int tamaniolistaNodos,int *posicion){
	int i,MaximaDisponibilidad = 0, cargaMax = 0;
	int historicoAnterior = 0;
	if(!strcmp("WCLOCK",algoritmoBalanceo)){
		cargaMax = obtenerCargaMaxima();
	}
	for(i=0;i<tamaniolistaNodos;i++){
		t_infoNodo* nodoObtenido = mlist_get(listaNodosActivos,i);
		planificador[i].nombreWorker = malloc(sizeof(char)*6);
		strcpy( planificador[i].nombreWorker, nodoObtenido->nodo);
		planificador[i].bloque = mlist_create();
		planificador[i].disponibilidad = Disponibilidad(cargaMax, planificador[i].nombreWorker);


		if(planificador[i].disponibilidad > MaximaDisponibilidad){
			MaximaDisponibilidad = planificador[i].disponibilidad;
			*posicion = i;
			historicoAnterior = obtenerHistorico(planificador[i].nombreWorker);
		} else if(planificador[i].disponibilidad == MaximaDisponibilidad){
			if(obtenerHistorico(planificador[i].nombreWorker) < historicoAnterior){
				*posicion = i;
				historicoAnterior = obtenerHistorico(planificador[i].nombreWorker);
			}
		}
	}
}


void verificarCondicion(int tamaniolistaNodos, int *posicion,t_workerPlanificacion planificador[],int* bloque,mlist_t* listaBloque){
	t_block* infoArchivo = mlist_get(listaBloque, *bloque);

		if(planificador[*posicion].disponibilidad == 0){
			planificador[*posicion].disponibilidad = atoi(config_get("DISP_BASE"));
			*posicion = *posicion +1;
			contadorBloquesSeguidosNoAsignados++;
		}else if(infoArchivo->copies[0].node == NULL){
			if(!strcmp(infoArchivo->copies[1].node, planificador[*posicion].nombreWorker))
			{
				asignoBloque(planificador,posicion,bloque,mlist_length(listaBloque));

			}
			else{
				avanzoPosicion(posicion,tamaniolistaNodos,planificador);
			}

		}

		else if(infoArchivo->copies[1].node == NULL){
			if(!strcmp(infoArchivo->copies[0].node, planificador[*posicion].nombreWorker))
			{
				asignoBloque(planificador,posicion,bloque,mlist_length(listaBloque));

			}
			else{
				avanzoPosicion(posicion,tamaniolistaNodos,planificador);
			}
		}
		else if(!strcmp(infoArchivo->copies[0].node, planificador[*posicion].nombreWorker) || !strcmp(infoArchivo->copies[1].node, planificador[*posicion].nombreWorker)){
			asignoBloque(planificador,posicion,bloque,mlist_length(listaBloque));
		}
		else{
			avanzoPosicion(posicion,tamaniolistaNodos,planificador);
		}
}

void avanzoPosicion(int *posicion,int tamaniolistaNodos,t_workerPlanificacion planificador[]){
*posicion = *posicion + 1;
		contadorBloquesSeguidosNoAsignados++;
		if(contadorBloquesSeguidosNoAsignados == tamaniolistaNodos){
			int i;
			for(i = 0; i < tamaniolistaNodos; i++){
				planificador[i].disponibilidad += atoi(config_get("DISP_BASE"));
			}
			contadorBloquesSeguidosNoAsignados = 0;
		}
}

void asignoBloque(t_workerPlanificacion planificador[], int *posicion,int* bloque,int tamanioListaBloque){

	planificador[*posicion].disponibilidad--;
	mlist_append(planificador[*posicion].bloque,(void*)*bloque);
	*bloque = *bloque + 1;
	if(tamanioListaBloque == *bloque){
		asigneBloquesDeArchivo = true;
	}
	*posicion = *posicion +1;
	contadorBloquesSeguidosNoAsignados = 0;
}

respuestaOperacionTranf* serial_unpackRespuestaOperacion(t_serial *serial){
	respuestaOperacionTranf* operacion = malloc(sizeof(respuestaOperacionTranf));
		serial_unpack(serial, "isiis",&operacion->idJOB ,&operacion->nodo, &operacion->bloque,
				&operacion->response,&operacion->file);
		return operacion;
}

respuestaOperacion* serial_unpackrespuestaOperacion(t_serial * serial){
	respuestaOperacion* operacion = malloc(sizeof(respuestaOperacion));
	serial_unpack(serial,"isi",&operacion->idJOB,&operacion->nodo,&operacion->response);
	return operacion;
}

t_pedidoTrans* serial_unpackPedido(t_serial* serial){
	t_pedidoTrans* operacion = malloc(sizeof(t_pedidoTrans));
	serial_unpack(serial,"is",&operacion->idJOB,&operacion->file);
	return operacion;
}

void requerirInformacionFilesystem(t_serial *file){
	t_packet packetInfoFs = protocol_packet(OP_REQUEST_FILE_INFO, file);
	protocol_send_packet(packetInfoFs,yama.fs_socket);
	serial_destroy(packetInfoFs.content);
}

void abortarJob(int job, int socketMaster, int codigoError){
	eliminarEstadosMultiples(socketMaster,job, "Error");
	log_report("Job: %d abortado",job);
	avisarErrorMaster(job, socketMaster, codigoError);
}

void avisarErrorMaster(int job, int socketMaster, int codigoError){
	t_packet packetError = protocol_packet(OP_ERROR_JOB, serial_pack("i",codigoError));
	protocol_send_packet(packetError, socketMaster);
	serial_destroy(packetError.content);
}

t_yfile* reciboInformacionSolicitada(int job,int master){
	t_yfile* Datosfile = malloc(sizeof(t_yfile));
	t_packet packetNodosActivos = protocol_receive_packet(yama.fs_socket);
	if(packetNodosActivos.operation == OP_NODES_ACTIVE_INFO){
		listaNodosActivos = nodelist_unpack(packetNodosActivos.content);
	}
	t_packet packetArchivo = protocol_receive_packet(yama.fs_socket);
	if(packetArchivo.operation == OP_ARCHIVO_INEXISTENTE){
		abortarJob(job, master,ARCHIVO_INEXISTENTE);
		log_report("Aborto de job %d por archivo inexistente",job);
		Datosfile->size = 0;
		return Datosfile;
	}

	else if(packetArchivo.operation == OP_ARCHIVO_NODES){
		Datosfile = yfile_unpack(packetArchivo.content);
		return Datosfile;
	}
	else{
		FinalizarEjecucion(master,job);
	}
	return Datosfile; //nunca llega a esta instancia.
}

char* generarNombreArchivoTemporalTransf(int job,int master, int bloque){
	char* nombreArchivoTemporal = malloc(sizeof(char)*21);
	 sprintf(nombreArchivoTemporal,"/tmp/j%dMaster%d-temp%d",job,master,bloque);
	 return nombreArchivoTemporal;
}

void agregarAtablaEstado(int job, char* nodo,int Master,int bloque,char* etapa,char* archivo_temporal,char* estado){
	t_Estado*  nuevoEstado = malloc(sizeof(t_Estado));
	nuevoEstado->job = job;
	nuevoEstado->master = Master;
	nuevoEstado->nodo = mstring_duplicate(nodo);
	nuevoEstado->block = bloque;
	nuevoEstado->etapa = mstring_duplicate(etapa);
	nuevoEstado->archivoTemporal = mstring_duplicate(archivo_temporal);
	nuevoEstado->estado = mstring_duplicate(estado);
	mlist_append(listaEstados,nuevoEstado);
	log_print("Nuevo ingreso de la tabla de estado: JOB:%d|MASTER:%d|NODO:%s|BLOQUE:%d|ETAPA:%s|ARCHIVOTEMPORAL:%s|ESTADO:%s",job,Master,nodo,bloque,etapa,archivo_temporal,estado);

}


void eliminarEstadosMultiples(int socketMaster,int job, char* estadoNuevo){//quite estadoABUSCAR
	bool condicionFiltro(void* unEstado){
			return ((t_Estado*) unEstado)->job == job && ((t_Estado*) unEstado)->master == socketMaster;
	}
	mlist_t * listaFiltrada = mlist_filter(listaEstados, condicionFiltro);
	int i, indiceEncontrado;
	for(i = 0; i < mlist_length(listaFiltrada); i++){
		void * estadoObtenido = mlist_get(listaFiltrada, i);
		t_Estado* estadoEncontrado = (t_Estado *) estadoObtenido;

		actualizarCargaDelNodo(estadoEncontrado->nodo, job, 0, 1);

		bool condicionIndice(void * unEstado){
			return ((t_Estado*) unEstado)->job == job && ((t_Estado*) unEstado)->master == socketMaster && string_equals_ignore_case( ((t_Estado*) unEstado)->nodo, estadoEncontrado->nodo ) && ((t_Estado*) unEstado)->block == estadoEncontrado->block;
		}
		indiceEncontrado = mlist_index(listaEstados, condicionIndice);
		if(!string_equals_ignore_case(estadoEncontrado->estado , "Error" )){
			estadoEncontrado->estado = mstring_duplicate(estadoNuevo);
			log_print("Actualizacion en la tabla de estado: JOB:%d|MASTER:%d|NODO:%s|BLOQUE:%d|ETAPA:%s|ARCHIVOTEMPORAL:%s|ESTADO:%s",job,socketMaster,estadoEncontrado->nodo,estadoEncontrado->block,estadoEncontrado->etapa,estadoEncontrado->archivoTemporal,estadoEncontrado->estado);
			mlist_replace(listaEstados, indiceEncontrado, estadoEncontrado);
		}
		else{
			estadoEncontrado->estado = mstring_duplicate(estadoNuevo);
			mlist_replace(listaEstados, indiceEncontrado, estadoEncontrado);
		}


	}
	log_print("Actualizacion tabla de estado: Aborto de job: %d",job);
	mlist_destroy(listaFiltrada, NULL);
}


void actualizoTablaEstado(char* nodo,int bloque,int socketMaster,int job,char* estado){
	t_Estado* estadoActual;
	bool condition(void* estadoTarea){
	  	return string_equals_ignore_case(((t_Estado *) estadoTarea)->nodo,nodo) && ((t_Estado *) estadoTarea)->block == bloque && ((t_Estado *) estadoTarea)->master == socketMaster && ((t_Estado *) estadoTarea)->job == job ? true : false;
	}
	int posicion = mlist_index(listaEstados,(void*) condition);
	void* estadoActualObtenido = mlist_get(listaEstados,posicion);
	estadoActual = (t_Estado*) estadoActualObtenido;
	estadoActual->estado = mstring_duplicate(estado);
	mlist_replace(listaEstados,posicion,estadoActual);
	log_print("Actualizacion en la tabla de estado: JOB:%d|MASTER:%d|NODO:%s|BLOQUE:%d|ETAPA:%s|ARCHIVOTEMPORAL:%s|ESTADO:%s",job,socketMaster,nodo,bloque,estadoActual->etapa,estadoActual->archivoTemporal,estado);

}

void destruirlista(void* bloqueobtenido) {
		free(bloqueobtenido);
}


void replanificacion(char* nodo, const char* pathArchivo,int master,int job){
	bool aborto = false;
	t_serial* serial_send = serial_pack("s",pathArchivo);
	requerirInformacionFilesystem(serial_send);
	t_yfile* Datosfile = reciboInformacionSolicitada(job, master);
	if(Datosfile->size > 0){

	bool esNodoBuscado(void* estadoTarea){
		return string_equals_ignore_case(((t_Estado *) estadoTarea)->nodo,nodo) && ((t_Estado *) estadoTarea)->master == master && ((t_Estado *) estadoTarea)->job == job && string_equals_ignore_case(((t_Estado *) estadoTarea)->etapa,"Transformacion");
	}

	mlist_t* listaFiltradaEstadosBloquesDelNodo = mlist_filter(listaEstados, (void*)esNodoBuscado);

	int i;
	mlist_t* ListaDeBloquesReplanificar = mlist_create();
	for(i=0; i < mlist_length(listaFiltradaEstadosBloquesDelNodo); i++){
		void* estadoActualBloqueObtenido = mlist_get(listaFiltradaEstadosBloquesDelNodo, i);
				t_Estado *  estadoActualBloque = (t_Estado*) estadoActualBloqueObtenido;
				actualizoTablaEstado(estadoActualBloque->nodo,estadoActualBloque->block,master,job,"Error");

		bool esBloqueBuscado(void* bloqueActual){
			if(((t_block*) bloqueActual)->copies[0].node == NULL){
				return (string_equals_ignore_case( ((t_block*) bloqueActual)->copies[1].node, nodo ) && ((t_block*) bloqueActual)->copies[1].blockno == estadoActualBloque->block );
			}
			else if(((t_block*) bloqueActual)->copies[1].node == NULL){
				return (string_equals_ignore_case( ((t_block*) bloqueActual)->copies[0].node, nodo) && ((t_block*) bloqueActual)->copies[0].blockno == estadoActualBloque->block);
			}
			else{
				return (string_equals_ignore_case( ((t_block*) bloqueActual)->copies[0].node, nodo) && ((t_block*) bloqueActual)->copies[0].blockno == estadoActualBloque->block)|| (string_equals_ignore_case( ((t_block*) bloqueActual)->copies[1].node, nodo ) && ((t_block*) bloqueActual)->copies[1].blockno == estadoActualBloque->block );
			}
		}

		void* bloqueObtenido = mlist_find(Datosfile->blocks,esBloqueBuscado);
		t_block* bloqueEncontrado = (t_block*) bloqueObtenido;
		mlist_append(ListaDeBloquesReplanificar,bloqueEncontrado);
	}

	mlist_t* list_to_send = mlist_create();

	int posicionCargaNodoObtenida = obtenerPosicionCargaNodo(nodo);
	void * cargaNodoObtenida = mlist_get(listaCargaPorNodo, posicionCargaNodoObtenida);
	t_cargaPorNodo * cargaNodo = (t_cargaPorNodo *) cargaNodoObtenida;
	for(i=0; i < mlist_length(ListaDeBloquesReplanificar); i++){
		void* bloqueDeListaObtenido = mlist_get(ListaDeBloquesReplanificar, i);
		t_block* bloqueAReplanificar = (t_block *) bloqueDeListaObtenido;
		if(nodoEstaEnLaCopia(bloqueAReplanificar, 0, nodo) && !aborto){
			generarEtapaTransformacionAEnviarParaCopia(1, bloqueAReplanificar, job, master, list_to_send);

		}
		else if(nodoEstaEnLaCopia(bloqueAReplanificar, 1, nodo) && !aborto){
			generarEtapaTransformacionAEnviarParaCopia(0, bloqueAReplanificar, job, master, list_to_send);
		}
		else{
			aborto = true;
		}
	}
	if(!aborto){

		eliminarCargaJobDelNodo(job, cargaNodo->cargaPorJob);
		mlist_replace(listaCargaPorNodo, posicionCargaNodoObtenida, cargaNodo);

		for(i=0; i< mlist_length(list_to_send);i++){
			void* etapaObtenida = mlist_get(list_to_send,i);
			tEtapaTransformacion* etapa = (tEtapaTransformacion*) etapaObtenida;
			agregarAtablaEstado(job,etapa->nodo,master,etapa->bloque,"Transformacion",etapa->archivo_etapa,"En proceso");
			cargaNodo->cargaActual -= 1;
			actualizarCargaDelNodo(etapa->nodo, job, 1, 1);
		}
		mandar_etapa_transformacion(list_to_send,master);
		log_inform("Envio etapa de transformacion por efecto de la replanificacion %d",job);
	}
	else{
		abortarJob(job, master,ERROR_REPLANIFICACION);
	}
	//mlist_destroy(Datosfile->blocks,free);
	//free(Datosfile);
	mlist_destroy(listaFiltradaEstadosBloquesDelNodo,NULL);
	mlist_destroy(list_to_send,NULL);
	mlist_destroy(ListaDeBloquesReplanificar,NULL);
	}
}


void generarEtapaTransformacionAEnviarParaCopia(int copia, t_block* bloqueAReplanificar, int job, int master, mlist_t* list_to_send){
	bool condicion(void* datosDeUnNodo){
		return string_equals_ignore_case(((t_infoNodo *) datosDeUnNodo)->nodo , bloqueAReplanificar->copies[copia].node) ? true : false;
	}
	t_infoNodo* datosNodoAEnviar = mlist_find(listaNodosActivos, condicion);
	char* nombreArchivoTemporal = malloc(sizeof(char)*21);
	strcpy(nombreArchivoTemporal, generarNombreArchivoTemporalTransf(job, master, bloqueAReplanificar->copies[copia].blockno));
	tEtapaTransformacion* et = new_etapa_transformacion(bloqueAReplanificar->copies[copia].node,datosNodoAEnviar->ip,datosNodoAEnviar->puerto,bloqueAReplanificar->copies[copia].blockno,bloqueAReplanificar->size,nombreArchivoTemporal);
	mlist_append(list_to_send,et);
}

bool nodoEstaEnLaCopia(t_block* bloqueAReplanificar, int copia, char* nodoAReplanificar){
	if(copia == 0){
			if( bloqueAReplanificar->copies[0].node == NULL){
				return false;

			}
			else{
				if(bloqueAReplanificar->copies[1].node == NULL){
					return false;
				}
				else{
					return string_equals_ignore_case(nodoAReplanificar, bloqueAReplanificar->copies[0].node) && NodoConCopia_is_active(bloqueAReplanificar->copies[1].node);
				}
			}
	}else{
			if(bloqueAReplanificar->copies[1].node == NULL){
				return false;
			}
			else{
				if(bloqueAReplanificar->copies[0].node == NULL){
					return false;
				}
				else{
					return string_equals_ignore_case(nodoAReplanificar, bloqueAReplanificar->copies[1].node) && NodoConCopia_is_active(bloqueAReplanificar->copies[0].node);
				}
			}
	}

}

int obtenerPosicionCargaNodo(char * nodoAReplanificar){
	bool condicionIndice(void* cargaNodotraida){
		return string_equals_ignore_case( ((t_cargaPorNodo *) cargaNodotraida)->nodo, nodoAReplanificar);
	}
	return mlist_index(listaCargaPorNodo, condicionIndice);
}

void actualizarCargaDelNodo(char* nodoCopia, int job, int aumentarOQuitar, int cantidadAAumentar){//1 para aumentar, 0 para quitar
	int posicionCargaNodoObtenidaCopia, posicionCargaJobObtenidoCopia;
	bool condicionIndiceCopia1(void* cargaNodotraida){
			return string_equals_ignore_case( ((t_cargaPorNodo *) cargaNodotraida)->nodo, nodoCopia);
	}
	posicionCargaNodoObtenidaCopia = mlist_index(listaCargaPorNodo, condicionIndiceCopia1);
	void * cargaNodoObtenida = mlist_get(listaCargaPorNodo, posicionCargaNodoObtenidaCopia);
	t_cargaPorNodo * cargaNodoCopia = (t_cargaPorNodo *) cargaNodoObtenida;

	posicionCargaJobObtenidoCopia = existeElJobEnLaCopia(job, cargaNodoCopia->cargaPorJob);

	if(aumentarOQuitar == 0){
		if(posicionCargaJobObtenidoCopia != -1){
			void * cargaJobObtenida = mlist_get(cargaNodoCopia->cargaPorJob, posicionCargaJobObtenidoCopia);
			t_cargaPorJob * cargaJob = (t_cargaPorJob *) cargaJobObtenida;

			cargaNodoCopia->cargaActual -= cargaJob->cargaDelJob;
			eliminarCargaJobDelNodo(job, cargaNodoCopia->cargaPorJob);
		}
	}
	else{
		cargaNodoCopia->cargaActual += cantidadAAumentar;
		cargaNodoCopia->cargaHistorica += cantidadAAumentar;
		if(posicionCargaJobObtenidoCopia != -1){
			void * cargaJobObtenida = mlist_get(cargaNodoCopia->cargaPorJob, posicionCargaJobObtenidoCopia);
			t_cargaPorJob * cargaJobCopia = (t_cargaPorJob *) cargaJobObtenida;
			cargaJobCopia->cargaDelJob += cantidadAAumentar;
			mlist_replace(cargaNodoCopia->cargaPorJob, posicionCargaJobObtenidoCopia, cargaJobCopia);
		}else{
			t_cargaPorJob * cargaJobCopia = malloc(sizeof(t_cargaPorJob));
			cargaJobCopia->job = job;
			cargaJobCopia->cargaDelJob = cantidadAAumentar;
			mlist_append(cargaNodoCopia->cargaPorJob, cargaJobCopia);
		}
	}

	mlist_replace(listaCargaPorNodo, posicionCargaNodoObtenidaCopia, cargaNodoCopia);

}

void eliminarCargaJobDelNodo(int job, mlist_t * listaJobsNodo){
	bool esJobAEliminar(void* jobObtenido){
		return ((t_cargaPorJob*) jobObtenido)->job == job;
	}
	void eliminarJob(void* jobObtenido){
		free(jobObtenido);
	}
	mlist_remove(listaJobsNodo, esJobAEliminar, eliminarJob);
}

int existeElJobEnLaCopia(int job, mlist_t * listaJobsCopia){
	bool esLaCargaDelJobBuscado(void* unaCargaJob){
		return ((t_cargaPorJob *) unaCargaJob)->job == job;
	}
	return mlist_index(listaJobsCopia, esLaCargaDelJobBuscado);
}

bool NodoConCopia_is_active(char* nodo){
	bool contieneNodo(void* unNodo){
	  return string_equals_ignore_case(((t_infoNodo*) unNodo)->nodo, nodo);
	 }
	 if( mlist_length(listaNodosActivos) == 0){
	  return false;
	 }
	 else{
	  return mlist_any(listaNodosActivos, (void*) contieneNodo);
	 }
}

void finalizarJobGlobalEnTablaEstado(int socketMaster,int job, char* estadoNuevo){
	bool condicionFiltroGlobal(void* unEstado){
		return ((t_Estado*) unEstado)->job == job && ((t_Estado*) unEstado)->master == socketMaster && string_equals_ignore_case( ((t_Estado*) unEstado)->estado, "En proceso" );
	}

	int indiceReduEncargado = mlist_index(listaEstados, condicionFiltroGlobal);

	void * estadoObtenido = mlist_get(listaEstados, indiceReduEncargado);
	t_Estado* estadoEncontrado = (t_Estado *) estadoObtenido;

	actualizarCargaDelNodo(estadoEncontrado->nodo, job, 0, 1);

	estadoEncontrado->estado = mstring_duplicate(estadoNuevo);
	mlist_replace(listaEstados, indiceReduEncargado, estadoEncontrado);
	if(string_equals_ignore_case(estadoNuevo, "Error")){
		log_report("Actualizacion tabla de estado: aborto de job: %d || nodo: %s",job,estadoEncontrado->nodo);
	} else
	{
		log_print("Actualizacion tabla de estado: finalizacion de job: %d || nodo: %s",job,estadoEncontrado->nodo);
	}

	eliminarCargasReduccionesLocales(estadoEncontrado->nodo,socketMaster,job);

}

void finalizarJobGlobal(int job, int socketMaster, int codigoError, char* estadoNuevo){
	if(string_equals_ignore_case(estadoNuevo, "Error") && codigoError != ERROR_ALMACENAMIENTO_FINAL){
		t_packet packetError = protocol_packet(OP_ERROR_JOB, serial_pack("i",codigoError));
		protocol_send_packet(packetError, socketMaster);
		serial_destroy(packetError.content);
	}
	finalizarJobGlobalEnTablaEstado(socketMaster, job, estadoNuevo);
}


void eliminarCargasReduccionesLocales(char* nodoGlobal,int master,int job){

	bool condicionFiltro(void* unEstado){
				return ((t_Estado*) unEstado)->job == job && ((t_Estado*) unEstado)->master == master && !string_equals_ignore_case( ((t_Estado*) unEstado)->nodo, nodoGlobal ) && string_equals_ignore_case( ((t_Estado*) unEstado)->etapa, "ReduccionLocal" );
		}
		mlist_t * listaNodosLocales_whitoutGlobalNode = mlist_filter(listaEstados, condicionFiltro);

		int i;
		for(i = 0; i < mlist_length(listaNodosLocales_whitoutGlobalNode); i++){
			void * estadoObtenido = mlist_get(listaNodosLocales_whitoutGlobalNode, i);
			t_Estado* estadoEncontrado = (t_Estado *) estadoObtenido;

			actualizarCargaDelNodo(estadoEncontrado->nodo, job, 0, 1);
		}

		mlist_destroy(listaNodosLocales_whitoutGlobalNode, NULL);

}

int obtenerHistorico(char * nodo){
	bool condicionIndice(void* cargaNodotraida){
		return string_equals_ignore_case( ((t_cargaPorNodo *) cargaNodotraida)->nodo, nodo);
	}
	void * cargaNodoObtenida = mlist_find(listaCargaPorNodo, condicionIndice);
	t_cargaPorNodo * cargaNodo = (t_cargaPorNodo *) cargaNodoObtenida;
	return cargaNodo->cargaHistorica;
}

int buscarIdJobParaMasterCaido(int socketMaster){
	if(mlist_length(listaEstados) == 0){
		return -1;
	}
	else{
		bool jobTablaDeEstado(void* estado){
			return ((t_Estado*) estado)->master == socketMaster && string_equals_ignore_case(((t_Estado*) estado)->estado,"En proceso");
		}
		if(mlist_any(listaEstados,jobTablaDeEstado)){
			t_Estado* estadoJob = mlist_find(listaEstados,jobTablaDeEstado);
			return estadoJob->job;
		}
		else{
			return -1;
		}
	}
}


mlist_t* buscarSocketsActivos(){
	bool esSocketActivo(void* unEstado){
		return string_equals_ignore_case(((t_Estado*)unEstado)->estado,"En proceso");
	}
	mlist_t* listaSockets = mlist_filter(listaEstados,esSocketActivo);
	return listaSockets;

}


void FinalizarEjecucion(int socket,int job){

	if(socket == -1){
	if(mlist_length(listaEstados)==0){
			log_report("Filesystem desconectado, se desconecta YAMA");
			exit(0);
	}
	else{
		mlist_t * listaEstadoSocketMasters = buscarSocketsActivos();
		if(mlist_length(listaEstadoSocketMasters) > 0){

			void cerrarMasters(void* unEstadoSocketMaster){
				avisarErrorMaster(((t_Estado*) unEstadoSocketMaster)->job,((t_Estado*) unEstadoSocketMaster)->master,ERROR_FS_DISCONNECTED);
			}
			mlist_traverse(listaEstadoSocketMasters,cerrarMasters);
		}
		log_report("Filesystem desconectado, se desconecta YAMA");
		exit(0);
	}
	}
	else{
		avisarErrorMaster(job,socket,ERROR_FS_DISCONNECTED);
		if(mlist_length(listaEstados)==0){
			log_report("Filesystem desconectado, se desconecta YAMA");
			exit(0);
		}
		else{
		mlist_t * listaEstadoSocketMasters = buscarSocketsActivos();
			if(mlist_length(listaEstadoSocketMasters) > 0){

				void cerrarMasters(void* unEstadoSocketMaster){
					avisarErrorMaster(((t_Estado*) unEstadoSocketMaster)->job,((t_Estado*) unEstadoSocketMaster)->master,ERROR_FS_DISCONNECTED);
				}
				mlist_traverse(listaEstadoSocketMasters,cerrarMasters);
			}
			log_report("Filesystem desconectado, se desconecta YAMA");
			exit(0);
		}

	}
}
