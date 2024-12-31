#include "door.h"
#include "ui.h"

#include <elapsedMillis.h>

#include "LL_Lib.h"

#ifdef DEF_STEPPERDRIVE
#include "lockdrive/panicdoorstepper.h"
#endif

#ifdef DEF_LOCKDRIVE
#define DOOR_SIG_OPEN_PIN   12
#define DOOR_SIG_CLOSE_PIN  2
#define LOCKDRIVE_SHORTPRESS_MS 120
#define LOCKDRIVE_OPENING_MS 7000
#define LOCKDRIVE_CLOSING_MS 7000
uint8_t lockdriveState = 0; // 0=nothing, 1=opening, 2=closing
elapsedMillis lockdriveMovetime;

bool lockdriveIsMoving() {
  if(lockdriveState != 0) return true;
  return false;
}

void lockdriveInit() {
  digitalWrite(DOOR_SIG_OPEN_PIN, LOW);
  digitalWrite(DOOR_SIG_CLOSE_PIN, LOW);
  pinMode(DOOR_SIG_OPEN_PIN, OUTPUT);
  pinMode(DOOR_SIG_CLOSE_PIN, OUTPUT);
  digitalWrite(DOOR_SIG_OPEN_PIN, LOW);
  digitalWrite(DOOR_SIG_CLOSE_PIN, LOW);
  lockdriveState = 0;
}

void lockdriveHandle() {
  switch(lockdriveState) {
    case 1:
      if(lockdriveMovetime > LOCKDRIVE_OPENING_MS) {
        lockdriveState = 0;
        LL_Log.println("Now open.");
      }
      break;
    case 2:
      if(lockdriveMovetime > LOCKDRIVE_CLOSING_MS) {
        lockdriveState = 0;
        LL_Log.println("Now closed.");
      }
      break;
  }
}

void lockdriveOpen() {
  lockdriveMovetime = 0;
  digitalWrite(DOOR_SIG_OPEN_PIN, HIGH);  // press button for some time
  delay(LOCKDRIVE_SHORTPRESS_MS);
  digitalWrite(DOOR_SIG_OPEN_PIN, LOW);
  LL_Log.println("Simulate pressing open button, waiting...");
  lockdriveState = 1;
}

void lockdriveClose() {
  lockdriveMovetime = 0;
  digitalWrite(DOOR_SIG_CLOSE_PIN, HIGH);  // press button for some time
  delay(LOCKDRIVE_SHORTPRESS_MS);
  digitalWrite(DOOR_SIG_CLOSE_PIN, LOW);
  LL_Log.println("Simulate pressing close button, waiting...");
  lockdriveState = 2;
}
#endif

/*
Zustände
    Tür: steht auf / ist zu (unabhängig ob abgeschlossen)
    Schloss: ist abgeschlossen / ist aufgeschlossen / ist offen (Türfalle auch offen) / unbekannt (nach Reset)
        Bei Innentüren: Aufgeschlossen und Offen sind vermutlich das gleiche
        Bei Aussentüren: Aufgeschlossen = Falle ist noch zu, man kann nicht rein (oder war nur kurz auf)
    Getriggert: Nichts / Schloss in Bewegung / Warte darauf das Tür zu gemacht wird um abzuschliessen

Aktionen
    Wechsel: Öffnen oder Schliessen je nach vorherigen Zustand. Wenn Zustand unbekannt dann zuerst aufschliessen
    Öffnen: Schliesst auf (wird nur ignoriert wenn Schloss in Bewegung ist). Bei Aussentür wird Falle für einige Sekunden geöffnet.
    Schliessen: Schliesst zu. Wird ignoriert wenn Schloss in Bewegung ist. Triggert warte auf Tür zu wenn Tür offen steht.
    Geöffnet: Nur bei Aussentür: Falle wird dauerhaft zurück gehalten.
    Sofort schliessen. Schliesst zu egal ob Tür als offen oder zu erkannt wurde.
*/

uint8_t doorState = 0; // 0=unknown, 1=closed, 2=open
uint8_t lockState = 0; // 0=unknown, 1=locked, 2=unlocked, 3=open
uint8_t trigState = 0; // 0=nothing, 1=moving, 2=waiting that door is closed, 3=delaying from door is closed to locking

uint16_t TimeOutForDoorClosing = 10000;
elapsedMillis WaitForDoorClosing;
uint16_t DelayClosingToLocking = 1000;
elapsedMillis WaitForDoorClosingDelay;

uint8_t doorGetDoorState() { return doorState; }
uint8_t doorGetLockState() { return lockState; }
uint8_t doorGetTrigState() { return trigState; }


void doorInit() {
  doorState = 0;
  lockState = 0;
  trigState = 0;

  lockdriveInit();
}

void doorHandle() {

  lockdriveHandle();

  #ifdef DEF_STEPPERDRIVE
  // ## TODO: Averaging and debouncing
  if(analogRead(36) < 150) doorState = 1; else doorState = 2;
  #endif

  // Reset trigState when moving is finished
  if(trigState == 1) {
    if(lockdriveIsMoving() == false) {
      trigState = 0;
      LL_Log.println("Move finished.");
      uiSetLED(0x000000); 
    }
  }

  // Wait with timeout that door is beeing closed
  if(trigState == 2) {
    if(WaitForDoorClosing > TimeOutForDoorClosing) {
      trigState = 0; // Abort
      LL_Log.println("Aborted.");
      uiSetLED(0x000000);
    } else {
      if(doorState == 1) {
        // Kick-off closing delay
        trigState = 3;
        WaitForDoorClosingDelay = 0;
      }
    }
  }

  // Wait that closing delay is passed
  if(trigState == 3) {
    // Abort if door open again
    if(doorState != 1) {
      trigState = 2;
    } else {
      if(WaitForDoorClosingDelay > DelayClosingToLocking) {
        trigState = 1;
        LL_Log.println("Locking door...");
        lockdriveClose();
        lockState = 1;
        uiBlinkLED(0xff0000,200,0x000000,200,0);
      }
    }
  }
}

/**
 *  Action:
 * -1 = close immediately
 *  0 = lock close (after door is closed)
 *  1 = unlock open (open trap for a few seconds, then release trap)
 *  2 = change (close -> open, open -> close (after door is closed)
 *  3 = open trap and keep trap open
 */
// ##TODO: Need MUtex or so!!!
void doorAction(int8_t action) {
  if(action == 2) {
    switch(lockState) {
      case 0: // unknown -> open
        action = 1;
        break;
      
      case 1: // locked -> open
        action = 1;
        break;

      case 2: // unlocked -> close
        action = 0;
        break;

      case 3: // trap open -> close
        action = 0;
        break;

      default: // wrong state -> set to unknown state
        lockState = 0;
        break;
    }
  }
  // ignore action if lock is moving
  if(trigState != 1) {
    switch(action) {
      case -1: // close immediately
        LL_Log.println("Locking door...");        
        lockdriveClose();
        trigState = 1;
        lockState = 1;
        uiBlinkLED(0xff0000,200,0x000000,200,0);
        break;

      case 0: // wait for closing, then close (in Handle)
        LL_Log.println("Waiting for door close...");
        WaitForDoorClosing = 0;
        trigState = 2;
        uiBlinkLED(0xffff00,200,0xff0000,200,0);
        break;

      case 1: // unlock open
        LL_Log.println("Unlock door...");
        lockdriveOpen();
        trigState = 1;
        lockState = 2;
        uiBlinkLED(0x00ff00,200,0x000000,200,0);
        break;

    #ifdef DEF_STEPPERDRIVE
      case 3: // open trap and keep trap open
        break;


    #endif

    }
  }
}

