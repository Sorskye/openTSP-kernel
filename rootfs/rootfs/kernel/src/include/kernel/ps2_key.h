
#pragma once
#ifndef PS2_KEY
#define PS2_KEY

#include "task.h"
#define PS2_NEW_INPUT 60




void ps2keyboard_init();
//tmp (should register irq handler instead)
void ps2keyboard_irq();


#endif