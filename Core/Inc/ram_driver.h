/*
 * ram_driver.h
 *
 *  Created on: 21 сент. 2026 г.
 *      Author: Nikita
 */

#ifndef INC_RAM_DRIVER_H_
#define INC_RAM_DRIVER_H_

#define SSIZE 512
#define BLOCKS 80

#include "inttypes.h"
#include "string.h"

extern uint8_t ramdrive[BLOCKS][SSIZE];

void ramdrive_init(void);

#endif /* INC_RAM_DRIVER_H_ */
