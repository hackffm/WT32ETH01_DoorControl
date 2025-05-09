#pragma once

//#include <QuickEspNow.h>

extern bool eth_connected;
extern tm lanCLT; // Current local time from type tm

void lanUpdateCLT();
void lanInit(const char *hostname);
void lanHandle();
void lanPrintInfo();
void lanEnableWebFileEditor();
void lanPingStart(const char *host = NULL);
int  lanPingStatus(); 

void lanEspNowTx(const uint8_t *data, int len);