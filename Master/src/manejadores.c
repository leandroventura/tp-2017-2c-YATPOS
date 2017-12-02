#include "manejadores.h"
#include "mstring.h"

void finalizar_manejador_transf(int response, t_socket socket,
		tEtapaTransformacion* transformacion) {

	if(response == -1){
		thread_send(hilo_node_drop, (void*)mstring_duplicate(transformacion->nodo));
		thread_suspend();
		if(!thread_active()) thread_exit(NULL);
		log_report("Finalización del hilo %d etapa TRANSFORMACION por caída del nodo: %s",
				thread_self(),
				transformacion->nodo);
	}else{
		log_print("Finalización hilo %d TRANSFORMACION realizada", thread_self());
		socket_close(socket);
		log_print("Conexión a Worker en %s:%s por el socket %i cerrada",
				transformacion->ip, transformacion->puerto, socket);
	}

	pthread_mutex_lock(&mutex_hilos);
	actualizar_hilo(response);
	pthread_mutex_unlock(&mutex_hilos);

	log_print("Se manda bloque %d\n", transformacion->bloque);

	t_serial *serial_yama = serial_pack("isiis",
			IDJOB,
			transformacion->nodo,
			transformacion->bloque,
			response,
			job.arch);
	enviar_resultado_yama(OP_TRANSFORMACION_LISTA, serial_yama);
	log_inform("Envío a YAMA socket %d OP_TRANSFORMACION_LISTA",
			yama_socket);
	free(transformacion);
	times.transf_end = mtime_now();
	thread_sem_signal(sem);
	thread_exit(0);
}

void manejador_transformacion(tEtapaTransformacion* transformacion) {
	log_print("Hilo %d creado ETAPA_TRANSFORMACION", thread_self());
	int response;

	t_socket socket = connect_to_worker(transformacion->ip, transformacion->puerto);

	if (socket == -1){
		response = -1;
		//finalizar_manejador_transf(response, socket, transformacion);
	}else{
		t_serial *serial_worker = serial_pack("ssii",
			script.script_transf,
			transformacion->archivo_etapa,
			transformacion->bloque,
			transformacion->bytes_ocupados);
			log_inform("Envío a %s socket %d OP_INICIAR_TRANSFORMACION",
					transformacion->nodo,
					socket);
		if(!enviar_operacion_worker(OP_INICIAR_TRANSFORMACION, socket, serial_worker)){
			response = -1;
			//finalizar_manejador_transf(response, socket, transformacion);
		}else{
			log_inform("Se recibe respuesta de ETAPA_TRANSFORMACION del worker en socket %d",
					socket);
			response = protocol_receive_response(socket);
			if(!thread_active()) thread_exit(NULL);
		}
	}

	finalizar_manejador_transf(response, socket, transformacion);
}

void finalizar_manejador_rl(int response, t_socket socket,
		tEtapaReduccionLocal* etapa_rl) {
	t_serial *serial_yama = serial_pack("isi", IDJOB, etapa_rl->nodo, response);

	if(response == -1){
		thread_send(hilo_node_drop, (void*)mstring_duplicate(etapa_rl->nodo));
		thread_suspend();
		if(!thread_active()) thread_exit(NULL);
		log_report("Finalización del hilo %d etapa REDUCCION_LOCAL por caída del nodo: %s",
				thread_self(),
				etapa_rl->nodo);
	}else{
		log_print("Finalización hilo %d REDUCCION_LOCAL realizada", thread_self());
		socket_close(socket);
		log_print("Conexión a Worker en %s:%s por el socket %i cerrada",
				etapa_rl->ip, etapa_rl->puerto, socket);
	}

	pthread_mutex_lock(&mutex_hilos);
	actualizar_hilo(response);
	pthread_mutex_unlock(&mutex_hilos);

	enviar_resultado_yama(OP_REDUCCION_LOCAL_LISTA, serial_yama);
	log_inform("Envío a YAMA socket %d OP_REDUCCION_LOCAL_LISTA",
			yama_socket);
	free(etapa_rl);
	times.rl_end = mtime_now();
	thread_exit(0);
}

void manejador_rl(tEtapaReduccionLocal * etapa_rl) {
	log_print("Hilo %d creado ETAPA_REDUCCION_LOCAL", thread_self());
	int response;

	t_socket socket = connect_to_worker(etapa_rl->ip, etapa_rl->puerto);

	if (socket == -1){
		response = -1;
		//finalizar_manejador_rl(response, socket, etapa_rl);
	}else{
		t_serial *serial_worker = serial_create(NULL, 0);
		serial_add(serial_worker, "s", script.script_reduc);
		serial_add(serial_worker, "i", mlist_length(etapa_rl->archivos_temporales_de_transformacion));

		void routine(char* archivo_temp){
			serial_add(serial_worker, "s", archivo_temp);
		}
		mlist_traverse(etapa_rl->archivos_temporales_de_transformacion, routine);
		serial_add(serial_worker, "s", etapa_rl->archivo_etapa);

		log_inform("Envío a %s socket %d OP_INICIAR_REDUCCION_LOCAL",
				etapa_rl->nodo,
				socket);

		if(!enviar_operacion_worker(OP_INICIAR_REDUCCION_LOCAL, socket, serial_worker)){
			response = -1;
			//finalizar_manejador_rl(response, socket, etapa_rl);
		}else{
			log_inform("Se recibe respuesta de ETAPA_REDUCCION_LOCAL del worker en socket %d",
					socket);
			response = protocol_receive_response(socket);
			if(!thread_active()) thread_exit(NULL);
		}
	}

	finalizar_manejador_rl(response, socket, etapa_rl);
}

void finalizar_manejador_rg(int response, t_socket socket, mlist_t* list,
		tEtapaReduccionGlobal* worker) {
	t_serial *serial_yama = serial_pack("isi", IDJOB, worker->nodo, response);

	if(response == -1){
		log_report("Finalización del hilo %d etapa REDUCCION_GLOBAL por caída del nodo: %s",
				thread_self(),
				worker->nodo);
	}else{
		log_print("Finalización hilo %d REDUCCION_GLOBAL realizada", thread_self());
		socket_close(socket);
		log_print("Conexión a Worker en %s:%s por el socket %i cerrada",
				worker->ip, worker->puerto, socket);
	}

	actualizar_hilo(response);

	enviar_resultado_yama(OP_REDUCCION_GLOBAL_LISTA, serial_yama);
	log_inform("Envío a YAMA socket %d OP_REDUCCION_GLOBAL_LISTA",
			yama_socket);
	void etapa_rg_destroy(tEtapaReduccionGlobal* rg) {
		free(rg);
	}
	mlist_destroy(list, etapa_rg_destroy);

	times.rg_end = mtime_now();
	thread_exit(0);
}

void manejador_rg(mlist_t* list) {
	log_print("Hilo %d creado ETAPA_REDUCCION_GLOBAL", thread_self());
	int response;

	bool getManager(tEtapaReduccionGlobal* etapa){
		return (string_equals_ignore_case(etapa->encargado, SI));
	}
	tEtapaReduccionGlobal* worker_manager = mlist_find(list,getManager);

	t_socket socket = connect_to_worker(worker_manager->ip, worker_manager->puerto);

	if (socket == -1){
		response = -1;
		//finalizar_manejador_rg(response, socket, list, worker_manager);
	}else{
		t_serial *serial_worker = serial_create(NULL, 0);
		serial_add(serial_worker, "s", script.script_reduc);

		serial_add(serial_worker, "i", mlist_length(list));

		void routine(tEtapaReduccionGlobal* rg_worker){
			serial_add(serial_worker, "sssss",
					rg_worker->nodo,
					rg_worker->ip,
					rg_worker->puerto,
					rg_worker->archivo_temporal_de_rl,
					rg_worker->encargado);
		}

		mlist_traverse(list, routine);

		serial_add(serial_worker, "s", worker_manager->archivo_etapa);

		log_inform("Envío a %s socket %d OP_INICIAR_REDUCCION_LOCAL",
				worker_manager->nodo,
				socket);
		if(!enviar_operacion_worker(OP_INICIAR_REDUCCION_GLOBAL, socket, serial_worker)){
			response = -1;
			//finalizar_manejador_rg(response, socket, list, worker_manager);
		}else{
			log_inform("Se recibe respuesta de ETAPA_REDUCCION_GLOBAL del worker en socket %d",
					socket);
			response = protocol_receive_response(socket);
		}
	}

	finalizar_manejador_rg(response, socket, list, worker_manager);
}

void finalizar_manejador_af(int response, t_socket socket, tAlmacenadoFinal* af) {
	t_serial *serial_yama = serial_pack("isi", IDJOB, af->nodo, response);

	switch (response) {
	case RESPONSE_ERROR:
		log_report("Finalización del hilo %d etapa ALMACENAMIENTO_FINAL por caída del nodo: %s",
				thread_self(),
				af->nodo);
		break;
	case RESPONSE_OK:
		log_print("Finalización hilo %d ALMACENAMIENTO_FINAL realizada", thread_self());
		socket_close(socket);
		log_print("Conexión a Worker en %s:%s por el socket %i cerrada",
				af->ip, af->puerto, socket);
		break;
	default:
		log_report("Finalización hilo %d ALMACENAMIENTO_FINAL no realizada", thread_self());
		socket_close(socket);
		log_print("Conexión a Worker en %s:%s por el socket %i cerrada",
				af->ip, af->puerto, socket);
		response = -1;
		break;
	}

	actualizar_hilo(response);

	times.job_end = mtime_now();

	enviar_resultado_yama(OP_ALMACENAMIENTO_LISTA, serial_yama);
	log_inform("Envío a YAMA socket %d OP_ALMACENAMIENTO_LISTA",
			yama_socket);
	free(af);

	job_active = false;
	thread_exit(0);
}

void manejador_af(tAlmacenadoFinal* af) {
	log_print("Hilo %d creado ETAPA_ALMACENAMIENTO_FINAL", thread_self());
	int response;

	t_socket socket = connect_to_worker(af->ip, af->puerto);
	if (socket == -1){
		response = -1;
		//finalizar_manejador_af(response, socket, af);
	}else{
		t_serial *serial_worker = serial_pack("ss",af->archivo_etapa, job.arch_result);

		log_inform("Envío a %s socket %d OP_INICIAR_REDUCCION_LOCAL",
				af->nodo,
				socket);
		if(!enviar_operacion_worker(OP_INICIAR_ALMACENAMIENTO, socket, serial_worker)){
			response = -1;
			//finalizar_manejador_af(response, socket, af);
		}else{
			log_inform("Se recibe respuesta de ETAPA_ALMACENAMIENTO_FINAL del worker en socket %d",
					socket);
			response = protocol_receive_response(socket);
		}
	}

	finalizar_manejador_af(response, socket, af);
}

void etapa_transformacion(const t_packet* paquete) {
	mlist_t* listTranformacion = list_transformacion_unpack(paquete->content);

	bool getTransformacion(t_hilos* hilo){
		return (hilo->etapa == TRANSFORMACION);
	}
	if(mlist_count(hilos, getTransformacion) == 0){
		times.transf_init = mtime_now();
		times.transf_end = mtime_now();
	}

	for (int i = 0; i < mlist_length(listTranformacion); i++) {
		tEtapaTransformacion* etapa_transformacion = mlist_get(
				listTranformacion, i);
		t_hilos* hilo_transformacion = set_hilo(TRANSFORMACION, etapa_transformacion->nodo);
		thread_sem_wait(sem);
		if ((hilo_transformacion->hilo = thread_create(manejador_transformacion,
				etapa_transformacion)) < 0) {
			log_report("Error al crear hilo en INICIAR_TRANSFORMACION");
			perror("Error al crear el hilo_transformacion");
		}

		pthread_mutex_lock(&mutex_hilos);
		mlist_append(hilos, hilo_transformacion);
		verificarParalelismo(TRANSFORMACION);
		pthread_mutex_unlock(&mutex_hilos);
	}
}

void etapa_reduccion_local(const t_packet* paquete) {
	tEtapaReduccionLocal* etapa_rl = etapa_rl_unpack(paquete->content);
	bool getReduccionLocal(t_hilos* hilo){
		return (hilo->etapa == REDUCCION_LOCAL);
	}
	if(mlist_count(hilos, getReduccionLocal) == 0){
		times.rl_init = mtime_now();
		times.rl_end = mtime_now();
	}

	t_hilos* hilo_rl = set_hilo(REDUCCION_LOCAL, etapa_rl->nodo);

	if ((hilo_rl->hilo = thread_create(manejador_rl, etapa_rl))
			< 0) {
		log_report("Error al crear hilo en INICIAR_REDUCCION_LOCAL");
		perror("Error al crear el hilo_reduccion_local");
	}

	pthread_mutex_lock(&mutex_hilos);
	mlist_append(hilos, hilo_rl);
	verificarParalelismo(REDUCCION_GLOBAL);
	pthread_mutex_unlock(&mutex_hilos);
}

void etapa_reduccion_global(const t_packet* paquete) {
	mlist_t* listReduccionGlobal = list_reduccionGlobal_unpack(
			paquete->content);

	bool getReduccionGlobal(t_hilos* hilo){
		return (hilo->etapa == REDUCCION_GLOBAL);
	}
	if(mlist_count(hilos, getReduccionGlobal) == 0){
		times.rg_init = mtime_now();
		times.rg_end = mtime_now();
	}

	bool getManager(tEtapaReduccionGlobal* etapa){
		return (string_equals_ignore_case(etapa->encargado, SI));
	}
	tEtapaReduccionGlobal* worker_manager = mlist_find(listReduccionGlobal,getManager);

	t_hilos* hilo_rg = set_hilo(REDUCCION_GLOBAL, worker_manager->nodo);

	if ((hilo_rg->hilo = thread_create(manejador_rg, listReduccionGlobal))
			< 0) {
		log_report("Error al crear hilo en INICIAR_REDUCCION_GLOBAL");
		perror("Error al crear el hilo_reduccion_global");
	}

	mlist_append(hilos, hilo_rg);
}

void etapa_almacenamiento(const t_packet* paquete) {
	tAlmacenadoFinal* almacenamiento = etapa_af_unpack(
			paquete->content);

	t_hilos* hilo_af = set_hilo(ALMACENAMIENTO, almacenamiento->nodo);

	if ((hilo_af->hilo = thread_create(manejador_af, almacenamiento))
			< 0) {
		log_report("Error al crear hilo en INICIAR_ALMACENAMIENTO");
		perror("Error al crear el hilo_almacenamiento");
	}

	mlist_append(hilos, hilo_af);
	thread_wait(hilo_af->hilo);
}

void manejador_yama(t_packet paquete) {
	int exit_code;

	switch (paquete.operation) {
	case OP_INICIAR_TRANSFORMACION:
		log_inform("OP_INICIAR_TRANSFORMACION");
		etapa_transformacion(&paquete);
		break;
	case OP_INICIAR_REDUCCION_LOCAL:
		log_inform("OP_INICIAR_REDUCCION_LOCAL");
		etapa_reduccion_local(&paquete);
		break;
	case OP_INICIAR_REDUCCION_GLOBAL:
		log_inform("OP_INICIAR_REDUCCION_GLOBAL");
		etapa_reduccion_global(&paquete);
		break;
	case OP_INICIAR_ALMACENAMIENTO:
		log_inform("OP_INICIAR_ALMACENAMIENTO");
		etapa_almacenamiento(&paquete);
		break;
	case OP_ERROR_JOB:
		thread_term();
		serial_unpack(paquete.content, "i", &exit_code);
		log_report("ERROR_JOB - Código de error: %d", exit_code);
		job_active = false;
		times.job_end = mtime_now();
		break;
	default:
		log_inform("Operación indefinida (desconexión de YAMA)");
		job_active = false;
		break;
	}

}
