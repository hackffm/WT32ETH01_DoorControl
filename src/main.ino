/*
 MIT Licencse

 Hackerspace FFM e.V. Door Lock

 Uses WT32-ETH01 module with Ethernet. Ethernet is used for LAN connection, Wifi is only used for ESP-Now.

 GPIO1, GPIO3, GPIO0: For programming only (and serial console)

 GPIO32, GPIO33: SDA, SCL for SSD1306 OLED (optional)

 GPIO5: Neopixel (Inside + Outside, 2 in a row, showing door lock status)
 GPIO36: Input for door switch (AnalogRead(GPIO36) < 150: Door is closed, > 150: Door is open). Add pullup here!
 GPIO39: Input for buttons (AnalogRead(GPIO39). Red+Yellow Button via Resistors.

 DEF_STEPPERDRIVE: 
   GPIO17, GPIO35, GPIO4, GPIO12, GPIO2: Stepper based on TMC5160 (DRV_ENN, SDO, CSN, SCK, SDI)
   GIPO14, GPIO15: SDA, SCL for AS5600 (Magnet encoder, position of Lock)

 DEF_LOCKDRIVE: (4 Lines to Lockdrive, GND, +5V, Open, Close)
   GPIO12, GPIO2: Lockdrive based on Open and Close Button (Open-Button, Close-Button)

*/



#include <MyCredsHackffm.h>      // Define WIFI_SSID and WIFI_PASSWORD here
#include <elapsedMillis.h>
#include <ETH.h>

#include "LL_Lib.h"

#include "network/lan.h"
#ifdef DEF_STEPPERDRIVE
#include "lockdrive/panicdoorstepper.h"
#endif
#include "auth/userdb.h"

#include "ui.h"
#include "door.h"

#include <FS.h>
#include <LittleFS.h>

const char* hostname = DEF_HOSTNAME ;

bool debugUnlocked = false;
extern int32_t anglePosCum;

// LL_Led LedGreen(5, true);

void setup() {
  LL_Log.begin(115200, 2222); // Serial baud, TCP Port
  LL_Log.SSD1306SetAutoUpdate(5000);
  LL_Log.println("Booted.");

  lanInit(hostname);
  uiInit(); 
  doorInit();

  LittleFS.begin();
  UserDB.loadUserData();

  LL_Log.println("A."); delay(5000);

  xTaskCreatePinnedToCore (
    loop2,     // Function to implement the task
    "loop2",   // Name of the task
    8000,      // Stack size in words
    NULL,      // Task input parameter
    0,         // Priority of the task
    NULL,      // Task handle.
    0          // Core where the task should run
  );
  
  LL_Log.println("B."); delay(5000);

  uiBlinkLED(0xffffff, 1000, 0x0000ff, 400, 10);
}

// Core 0
void loop2(void* pvParameters) {
  elapsedMillis blinkTimerL2 = 0;
  static int LastPressedButton = 0;
  int PressedButton;
  uint16_t PressedButtonDuration;
  for(;;) {

    uiHandle();
    doorHandle();

    if(blinkTimerL2 > 60000) {
      static uint8_t blinkseq = 0;
      blinkTimerL2 = 0;
      //LL_Log.printf("Hello world from core %d!\n", xPortGetCoreID() );
    }

    PressedButton = uiGetPressedButton(&PressedButtonDuration);
    if(PressedButton != LastPressedButton) {
      switch(PressedButton) {
        case 0: uiSetLED(0); break;
        case 1: uiSetLED(0xff0000); break;
        case 2: 
          // ## TODO: Duration check is not triggerd again...
          if(PressedButtonDuration > 2000) {
            uiSetLED(0x00ff00);
          } else {
            uiSetLED(0xffff00); 
          }
          break;
        case 3: uiSetLED(0xffff80); break;
      }
      LastPressedButton = PressedButton;
    }

    uint16_t butDur = 0;
    int but = uiGetReleasedButton(&butDur);
    if(but) {
      LL_Log.printf("ButRel %d, %d ms\r\n", but, (int)butDur);
      if((but == 1) && (butDur < 1000) && (butDur > 100)) doorAction(0);
      if((but == 2) && (butDur < 1000) && (butDur > 100)) doorAction(1);
      if((but == 1) && (butDur < 5000) && (butDur > 2000)) doorAction(3);
      if((but == 1) && (butDur < 15000) && (butDur > 10000)) ESP.restart();    
    }

    delay(1);
  }
}

// core 1
void loop() {
  LL_Log.update(eth_connected?1:0); 
  //LedGreen.update();
  lanHandle();

  if(LL_Log.receiveLineAvailable()) {
    // LL_Log.printf("Hello world from core %d!\n", xPortGetCoreID() );
    LL_Log.println(LL_Log.receiveLine);
    if(LL_Log.receiveLine[0]=='?') LL_Log.println("Help: ? l a R | (only after Debug-Unlock): T # ! L W d w c");
    if(LL_Log.receiveLine[0]=='l') lanPrintInfo();
    if(LL_Log.receiveLine[0]=='a') {
      LL_Log.printf("But: %d, Mag: %d, APC: %d\r\n", analogRead(39), analogRead(36), anglePosCum);
      // No But: 4095, Red: 203, Yellow: 555, Both: 112
      // No Mag: around 500, with more than 2k
    }
    if(LL_Log.receiveLine[0]=='R') ESP.restart();
    if(LL_Log.receiveLine[0]=='T') {
      lanEspNowTx((uint8_t*)LL_Log.receiveLine, strlen(LL_Log.receiveLine));
    }
    if(LL_Log.receiveLine[0]=='p') {
      lanPingStart(&LL_Log.receiveLine[1]);
      LL_Log.printf("Ping %s started.\n", &LL_Log.receiveLine[1]);
    }
    // All here only allowed if debug was unlocked before   
    if(debugUnlocked) {
      if(LL_Log.receiveLine[0]=='#') {
        lanEnableWebFileEditor();
        LL_Log.println("Web File Editor enabled, reset to disable!");
      }
      if(LL_Log.receiveLine[0]=='!') {
        //UserDB.saveUserData();
        UserDB.loadUserData();
        LL_Log.println("Userdata reloaded.");
      }
      if(LL_Log.receiveLine[0]=='L') {
        if (LittleFS.begin()){
            File root = LittleFS.open("/", "r");
            File file = root.openNextFile();
            LL_Log.printf("LittleFS Total %d, Used %d\n", LittleFS.totalBytes(), LittleFS.usedBytes());
            while (file){
                LL_Log.printf("FS File: %s, size: %d\n", file.name(), file.size());
                file = root.openNextFile();
            }
            if(LL_Log.receiveLine[1]=='+') {
              UserDB.saveUserData();
            }  
        }
        else {
            LL_Log.println("ERROR on mounting filesystem. It will be reformatted!");
            LittleFS.format();
            //ESP.restart();
        }
      }

      #ifdef DEF_STEPPERDRIVE
      if(LL_Log.receiveLine[0]=='W') lockdrivePrintInfo();
      lockdriveManualCommands();
      #endif
      /*
      if(LL_Log.receiveLine[0]=='d') LL_Os.drawString(&LL_Log.receiveLine[1]);
      if(LL_Log.receiveLine[0]=='w') {
        int addr, data;
        if(sscanf(&LL_Log.receiveLine[1],"%02x %02x",&addr, &data)>=2) {
          u8g2.sendF("ca", (uint8_t)addr, (uint8_t)data);
        }
      }
      if(LL_Log.receiveLine[0]=='c') {
        int ad9, a81;
        if(sscanf(&LL_Log.receiveLine[1],"%02x %02x",&ad9, &a81)>=2) {
          u8g2.sendF("ca", (uint8_t)0x81, (uint8_t)a81);
          u8g2.sendF("ca", (uint8_t)0xd9, (uint8_t)ad9);
        }
      }
      */
    }

  }

  delay(1);

}

