#ifndef FUNCIONESWORKER_H
#define FUNCIONESWORKER_H

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stddef.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <netdb.h>
#include <string.h>
#include <pthread.h>

#include <sys/select.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <config.h>
#include <data.h>
#include <file.h>
#include <log.h>
#include <mstring.h>
#include <path.h>
#include <process.h>
#include <protocol.h>
#include <serial.h>
#include <socket.h>
#include <struct.h>
#include <system.h>
#include <data.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <commons/config.h>
#include <commons/log.h>
#include <commons/string.h>

#define MAX_IP_LEN 16   // aaa.bbb.ccc.ddd -> son 15 caracteres, 16 contando un '\0'
#define MAX_PORT_LEN 6  // 65535 -> 5 digitos, 6 contando un '\0'
#define MAX_NOMBRE_NODO 5
#define MAX_RUTA 25
#define MAXIMO_TAMANIO_DATOS 256 //definiendo el tamanio maximo
#define MAXCONEXIONESLISTEN 10

#define SIZE 1024

#define LECTURA_HIJO pipe_hijoAPadre[0]
#define ESCRITURA_HIJO pipe_hijoAPadre[1]
#define LECTURA_PADRE pipe_padreAHijo[0]
#define ESCRITURA_PADRE pipe_padreAHijo[1]


typedef struct{
		int bloque;
		int bytesOcupados;
	}tEtapaTransformacionWorker;

typedef struct {
	char * script;
	int lenLista;
	mlist_t * archivosTemporales;
	char * archivoTemporal;
} tEtapaReduccionLocalWorker;

typedef struct{
			char * scriptReduccion;
			int  lenLista;
			mlist_t * datosWorker;
			char * archivoEtapa;
			tEtapaReduccionGlobal * rg;
}tEtapaReduccionGlobalWorker;

typedef struct {
	char * archivoReduccion;
	char * archivoFinal;
} tEtapaAlmacenamientoWorker;

t_socket socketEscuchaMaster,socketEscuchaWorker, socketFileSystem,socketWorker;


void listen_to_master();
t_file* crearScript(char * bufferScript,int,int);
tEtapaReduccionLocalWorker * etapa_rl_unpack_bis(t_serial * serial);
tEtapaReduccionGlobalWorker * rg_unpack(t_serial*);
void manejador_master(t_packet *packet,int socket);
void manejador_worker(t_packet * packet,int socket);
t_socket connect_to_worker(const char *ip, const char *port);
void mandarDatosAWorkerHomologo(tEtapaReduccionGlobal * rg,int);
void asignarOffset(int * offset,int bloque,int bytesOcuapdos);
void ejecutarComando(char * command, int socketAceptado);
char*  crearListaParaReducir(tEtapaReduccionGlobalWorker * rg);
tEtapaAlmacenamientoWorker * af_unpack(t_serial * serial);
int connect_to_filesystem();
bool block_transform(int blockno, size_t size, const char *script, const char *output,int);
bool reducir_path(const char *input, const char *script, const char *output);

#endif
