#pragma once
#include <Arduino.h>

#ifdef DEF_STEPPERDRIVE
void lockdriveInit();
void lockdriveHandle();
void lockdrivePrintInfo();
void lockdriveManualCommands();
void lockdriveStartMove(int32_t maxSteps, uint32_t moveTimeout);

bool lockdriveIsMoving();
void lockdriveOpen();
void lockdriveClose();
#endif