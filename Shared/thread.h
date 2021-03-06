#ifndef thread_h
#define thread_h

#include <signal.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>

typedef struct thread thread_t;
typedef pthread_mutex_t mutex_t;

/**
 * Habilita la funcionalidad de hilos.
 */
void thread_init(void);

/**
 * Crea un hilo de usuario usando la biblioteca de hilos POSIX.
 * @param routine Rutina que ejecutará el hilo.
 * @param arg Argumento pasado a la rutina.
 * @return Identificador del hilo creado.
 */
thread_t *thread_create(void *routine, void *arg);

/**
 * Termina la ejecución del hilo actual.
 * @param retvalue Valor de retorno.
 */
void thread_exit(void *retvalue);

/**
 * Suspende la ejecución del hilo actual por un tiempo determinado.
 * @param time Tiempo de suspensión en milisegundos.
 */
void thread_sleep(unsigned time);

/**
 * Suspende la ejecución del hilo actual.
 */
void thread_suspend(void);

/**
 * Resume la ejecución hilo suspendido.
 * @param thread Hilo a despertar.
 */
void thread_resume(thread_t *thread);

/**
 * Escribe datos en el espacio local de un determinado hilo.
 * @param thread Hilo a enviar datos.
 * @param data Datos a enviar.
 */
void thread_send(thread_t *thread, void *data);

/**
 * Devuelve el último remitente del hilo actual.
 * @return Hilo remitente.
 */
thread_t *thread_sender(void);

/**
 * Lee datos de su espacio local.
 */
void *thread_receive(void);

/**
 * Envía datos al último hilo del cual se recibieron datos.
 * @param data Datos a enviar.
 */
void thread_respond(void *data);

/**
 * Función bloqueante que espera a que termine de ejecutar un hilo.
 * @param thread Hilo a esperar.
 * @return Valor de retorno del hilo.
 */
void *thread_wait(thread_t *thread);

/**
 * Espera a que terminen todos los hilos.
 */
void thread_waitall();

/**
 * Cancela la ejecución de un hilo y todos sus hilos hijos.
 * Espera a que terminen de ejecutar.
 * @param thread Hilo que se va a terminar.
 * @return Valor de retorno del hilo.
 */
void *thread_kill(thread_t *thread);

/**
 * Cancela todos los hijos.
 */
void thread_killall();

/**
 * Indica si fue cancelado un hilo determinado.
 * @param thread Hilo a verificar.
 * @return Valor lógico con el resultado.
 */
bool thread_killed(thread_t *thread);

/**
 * Determina si el hilo actual está activo.
 * @return Valor lógico indicando si el hilo está activo.
 */
bool thread_active(void);

/**
 * Devuelve el identificador del hilo actual.
 * @return Identificador del hilo.
 */
thread_t *thread_self(void);

/**
 * Devuelve el identificador del hilo principal o conductor.
 * @return Identificador del hilo conductor.
 */
thread_t *thread_main();

/**
 * Devuelve el identificador del padre de un hilo.
 * @param thread Identificador de un hilo.
 * @return Padre del hilo.
 */
thread_t *thread_parent(thread_t *thread);

/**
 * Envía una señal a un determinado hilo.
 * @param thread Hilo a enviar la señal.
 * @param signal Señal a enviar.
 */
void thread_signal_send(thread_t *thread, int signal);

/**
 * Establece la rutina que se ejecutará al recibir determinada señal.
 * @param signal Descriptor de la señal.
 * @param routine Rutina que se ejecutará.
 */
void thread_signal_capture(int signal, void *routine);

/**
 * Crea un semáforo de exclusión mutua (mutex).
 * @return Semáforo mutex.
 */
mutex_t *thread_mutex_create(void);

/**
 * Bloquea un semáforo de exclusión mutua.
 * @param mutex Semáforo a bloquear.
 */
void thread_mutex_lock(mutex_t *mutex);

/**
 * Desbloquea un semáforo de exclusión mutua.
 * @param mutex Semáforo a desbloquear.
 */
void thread_mutex_unlock(mutex_t *mutex);

/**
 * Destruye un semáforo de exclusión mutua.
 * @param mutex Semáforo a destruir.
 */
void thread_mutex_destroy(mutex_t *mutex);

/**
 * Crea un semáforo contador.
 * @return Semáforo contador.
 */
sem_t *thread_sem_create(unsigned value);

/**
 * Disminuye el valor de un semáforo contador.
 * Si el valor pasa a ser negativo, se bloquea.
 * @param sem Semáforo contador.
 */
void thread_sem_wait(sem_t *sem);

/**
 * Aumenta el valor de un semáforo contador.
 * @param sem Semáforo contador.
 */
void thread_sem_signal(sem_t *sem);

/**
 * Destruye un semáforo contador.
 * @param mutex Semáforo a destruir.
 */
void thread_sem_destroy(sem_t *sem);

/**
 * Termina la funcionalidad de hilos.
 */
void thread_term(void);

#endif /* thread_h */
