#ifndef NUMBER_H_
#define NUMBER_H_

typedef double t_number;

#include <stdbool.h>

/**
 * Devuelve el valor absoluto de un número.
 * @param x Un número.
 * @return Valor absoluto.
 */
t_number number_abs(t_number x);

/**
 * Redondea un número hacia el entero superior más cercano.
 * @param x Número a redondear hacia arriba.
 * @return Número redondeado.
 */
t_number number_ceiling(t_number x);

/**
 * Determina si un número es igual a otro.
 * @param x Un número.
 * @param y Otro número.
 * @return Valor lógico indicando si los números son iguales.
 */
bool number_equals(t_number x, t_number y);

/**
 * Devuelve el menor valor entre dos números.
 * @param x Un número.
 * @param y Otro número.
 * @return Valor mínimo entre los números.
 */
t_number number_min(t_number x, t_number y);

/**
 * Devuelve el mayor valor entre dos números.
 * @param x Un número.
 * @param y Otro número.
 * @return Valor máximo entre los números.
 */
t_number number_max(t_number x, t_number y);

/**
 * Redondea un número según una determinada precisión.
 * @param number Número a redondear.
 * @param precision Precisión deseada.
 * @return Número redondeado.
 */
t_number number_round(t_number number, int precision);

#endif /* NUMBER_H_ */
