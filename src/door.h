#pragma once
#include <Arduino.h>

void    doorInit();
void    doorHandle();
uint8_t doorGetDoorState();
uint8_t doorGetLockState();
uint8_t doorGetTrigState();
void    doorAction(int8_t action);