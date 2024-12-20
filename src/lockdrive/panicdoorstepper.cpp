#include "panicdoorstepper.h"
#include <elapsedMillis.h>

int32_t anglePosCum = 0;

#ifdef DEF_STEPPERDRIVE

#include "LL_Lib.h"
#include <TMCStepper.h>
#include "AS5600.h"
#include <WiFiUdp.h>

// ESP32 Boot-Strap Pins:  0,2,12,15
// ESP32 Boot-Toggle-Pins: 1,3,5,14,15

const uint8_t triEN = 17;
const uint8_t triSDI = 2;
const uint8_t triSCK = 12;
const uint8_t triCS = 4;
const uint8_t triSDO = 35;

int angleSensStatus = 0;
 
 // TMC5160: f_clk = 12 MHz, 2^18 clocks = 45Hz/22ms
TMC5160Stepper tmc = TMC5160Stepper(triCS, 0.075f, triSDI, triSDO, triSCK);
AS5600 as5600(&Wire1); 

WiFiUDP Udp;

int32_t vmax = 4800;

struct sg_s {
  // 0 nothing, 1 feed enable watchdog, 2 do movement
  int8_t state; 
  elapsedMillis millis; // millis since entering state
  uint16_t wd_en_millis;
  int32_t maxSteps;
  uint32_t moveTimeout;
  uint16_t wait_halt;
  int32_t vmax;

  uint16_t sg_min;
  uint16_t sg_max;

  int32_t angleDiff;

} sg;

void lockdriveInit() {

  pinMode(triEN, OUTPUT); 
  digitalWrite(triEN, HIGH); // Active low
  pinMode(triCS, OUTPUT);
  digitalWrite(triCS, HIGH);
  pinMode(triSDI, OUTPUT);
  digitalWrite(triSDI, LOW);
  pinMode(triSDO, INPUT);
  digitalWrite(triSDO, HIGH);
  pinMode(triSCK, OUTPUT);
  digitalWrite(triSCK, LOW);

  Wire1.begin(14, 15);
  as5600.begin(4);  //  set direction pin.
  as5600.setDirection(AS5600_CLOCK_WISE);  //  default, just be explicit.
  int angleSensStatus = as5600.isConnected();
  LL_Log.print("AS5600 connect: ");
  LL_Log.println(angleSensStatus);

  //set driver config
  tmc.begin();
  tmc.toff(4); //off time
  tmc.blank_time(24); //blank time
  //tmc.en_pwm_mode(1); //enable extremely quiet stepping
  tmc.microsteps(16); //16 microsteps
  
  // 1000mA = ihold:9, irun:19, globsc:133
  // 1200mA = ihold:12, irun:24, globsc:128
  // 800mA: Door does not open completely
  // 1000mA: Door does open, not much power reserve
  // 1200mA: Door does open even if somewhat pulled 
  tmc.rms_current(1200); //400mA RMS (sets ihold and irun)
  //tmc.ihold(5); // if 0 no hold but freewheeling
  tmc.freewheel(1);
  tmc.sgt(4); // w5: lower = more sensitive stall guard
  

  tmc.RAMPMODE(0); // 0: use A,D,V params
  tmc.a1(100);     // accelerations
  tmc.AMAX(250);
  tmc.DMAX(800);   // deaccelerations
  tmc.d1(3000);

  tmc.VSTART(100);  // velocities
  tmc.VSTOP(200);
  tmc.v1(480);
  tmc.VMAX(4300);

  tmc.TCOOLTHRS(4000); // stall-guard start velocity

  // tmc.XTARGET(1000);
  tmc.XACTUAL(0);

  //tmc.en_softstop(0); // hard stop
  //tmc.sg_stop(1); // activate SGT stop

  //tmc.freewheel(1);

  //outputs on (LOW active)
  digitalWrite(triEN, LOW);

  LL_Log.printf("Tri Init: ihold: %d, irun: %d\r\n", (int)tmc.ihold(), (int)tmc.irun());

  sg.state = 0;
  sg.wd_en_millis = 1000;
  sg.wait_halt    = 8000;
  sg.vmax = 4800;

}

//#define TRI_NO_WATCHDOG 1

void lockdriveFeedENWatchdog(uint8_t enable) {
  #ifdef TRI_NO_WATCHDOG
  if(enable == 0) {
    digitalWrite(triEN, HIGH);
  } else {
    digitalWrite(triEN, LOW);
  }
  #else
  static uint8_t enLine;
  if(enable != 0) {
    if(enLine == 0) {
      digitalWrite(triEN, LOW);
      enLine = 1;
    } else {
      digitalWrite(triEN, HIGH);
      enLine = 0;
    }
  } else {
    digitalWrite(triEN, HIGH);
  }
  #endif
}

elapsedMillis refresh = 0;

void lockdriveHandle() {
  static int32_t oldAnglePosCum = 0;
  static elapsedMillis udpTimer;

  switch(sg.state) {
    case 0: // idle, motor not powered
      
      break;
    case 1: // feed watchdog, power motor
      lockdriveFeedENWatchdog(1);
      if(sg.millis > sg.wd_en_millis) {
        sg.millis = 0;
        sg.state++;
        sg.angleDiff = oldAnglePosCum;

        // start movement
        tmc.VMAX(0);
        tmc.RAMPMODE(0);
        tmc.sg_stop(0);
        tmc.XACTUAL(0);
        tmc.sg_stop(1);
        tmc.VMAX(sg.vmax);
        tmc.XTARGET(sg.maxSteps);

        sg.sg_min = 65535;
        sg.sg_max = 0;

      }
      break;
    case 2: // do movement
      // ### wie Stall-Guard-Stop erkennen?
      lockdriveFeedENWatchdog(1);
      if((tmc.XACTUAL() == sg.maxSteps) 
       || (sg.millis > sg.moveTimeout)
       || (tmc.event_stop_sg())) {
        sg.millis = 0;
        sg.state++;
        tmc.VMAX(0);
      } else {
        uint16_t sgr = tmc.sg_result();
        if(sgr > sg.sg_max) sg.sg_max = sgr;
        if(sgr < sg.sg_min) sg.sg_min = sgr;
      }  
      break;  
    case 3: // wait for halt
      // ### am Bewegungsende Strom halten damit man Tür öffnen kann
      lockdriveFeedENWatchdog(1);
      if(sg.millis > sg.wait_halt) {
        sg.angleDiff = oldAnglePosCum - sg.angleDiff;
        sg.millis = 0;
        sg.state++;
      }
      break;
    default: // disable watchdog
      lockdriveFeedENWatchdog(0);
      sg.millis = 0;
      sg.state = 0;
      break;
  }  
  if(sg.state >= 2) {
    if(udpTimer > 20) {
      udpTimer = 0;
      
      // int32_t vel = tmc.VACTUAL();
      // if(vel != 0) {
        Udp.begin(47269);
        //Udp.beginPacket("teleplot.fr", 33370);
        Udp.beginPacket("10.0.0.176", 47269);
        Udp.printf("SGT:%d|g\r\n", tmc.sg_result());
        Udp.printf("v:%d|g\r\n", tmc.VACTUAL()); 
        //Udp.printf("cs:%d|g\r\n", tmc.cs_actual()); 
        Udp.printf("x:%d|g\r\n", tmc.XACTUAL()); 
        Udp.printf("as:%d|g\r\n", anglePosCum); 
        Udp.endPacket();
      // }
      
    }
  }

  anglePosCum = as5600.getCumulativePosition();
  if((sg.state == 0) && (refresh > 200)) {
    refresh = 0;
    u8g2.setDrawColor(0);
    u8g2.drawBox(0,0,128,40);
    char s[128];
    sprintf(s, "$!$9$cAPC:%0.2f", anglePosCum/4096.0);
    u8g2.setDrawColor(1);
    LL_Log.SSD1306drawString(s, 0, -10, 1);


    static bool sgstop = false;
    static bool sgstopevent = false;
    static struct sgflags_s {
      uint8_t diag0;
      uint8_t ot;
      uint8_t otpw;
      uint8_t s2ga;
      uint8_t s2gb;
      uint8_t ola;
      uint8_t olb;
    } sgflags;
    if(tmc.status_sg() != sgstop) {
      sgstop = tmc.status_sg();
      LL_Log.printf("SG_Stop %d\r\n", sgstop?1:0);
    }
    if(tmc.event_stop_sg() != sgstopevent) {
      sgstopevent = tmc.event_stop_sg();
      LL_Log.printf("SG_Stop_Event %d, Pos: %d\r\n", sgstopevent?1:0, tmc.XACTUAL());
    }
    if(tmc.diag0_error()) sgflags.diag0 |= 1; else sgflags.diag0 = 0;  
    if(tmc.ot())   sgflags.ot |= 1;   else sgflags.ot = 0; 
    if(tmc.otpw()) sgflags.otpw |= 1; else sgflags.otpw = 0; 
    if(tmc.s2ga()) sgflags.s2ga |= 1; else sgflags.s2ga = 0; 
    if(tmc.s2gb()) sgflags.s2gb |= 1; else sgflags.s2gb = 0; 
    if(tmc.ola())  sgflags.ola |= 1;  else sgflags.ola = 0; 
    if(tmc.olb())  sgflags.olb |= 1;  else sgflags.olb = 0; 
    if(sgflags.diag0 == 1) { LL_Log.println("DIAG0 error"); sgflags.diag0 |= 2;  }
    if(sgflags.ot == 1)    { LL_Log.println("Overtemp."); sgflags.ot |= 2;     }
    if(sgflags.otpw == 1)  { LL_Log.println("Overtemp. PW"); sgflags.otpw |= 2;  }
    if(sgflags.s2ga == 1)  { LL_Log.println("Short to Gnd A"); sgflags.s2ga |= 2;}
    if(sgflags.s2gb == 1)  { LL_Log.println("Short to Gnd B"); sgflags.s2gb |= 2;}
    if(sgflags.ola == 1)   { LL_Log.println("Open Load A"); sgflags.ola |= 2;   }
    if(sgflags.olb == 1)   { LL_Log.println("Open Load B"); sgflags.olb |= 2;   }

    //LL_Log.println(tmc.sg_result());
  }
  
  if(anglePosCum != oldAnglePosCum) {
    oldAnglePosCum = anglePosCum;
    //LL_Log.printf("APC: %d", oldAnglePosCum);
  }
}

void lockdrivePrintInfo() {
      LL_Log.print("STATUS:\t ");
      LL_Log.println(as5600.readStatus(), HEX);
      LL_Log.print("CONFIG:\t ");
      LL_Log.println(as5600.getConfigure(), HEX);
      LL_Log.print("  GAIN:\t ");
      LL_Log.println(as5600.readAGC(), HEX);
      LL_Log.print("MAGNET:\t ");
      LL_Log.println(as5600.readMagnitude(), HEX);
      LL_Log.print("DETECT:\t ");
      LL_Log.println(as5600.detectMagnet(), HEX);
      LL_Log.print("M HIGH:\t ");
      LL_Log.println(as5600.magnetTooStrong(), HEX);
      LL_Log.print("M  LOW:\t ");
      LL_Log.println(as5600.magnetTooWeak(), HEX);
      LL_Log.println();
} 

// Move the stepper with active stallguard
// Enables and disables the extra EN watchdog before and after
void lockdriveStartMove(int32_t maxSteps, uint32_t moveTimeout) {
  
  sg.millis = 0;
  sg.maxSteps = maxSteps;
  sg.moveTimeout = moveTimeout;
  sg.state = 1;

  tmc.VMAX(0);
  tmc.RAMPMODE(0);  
  tmc.sg_stop(0);   
  tmc.XACTUAL(0);
  tmc.XTARGET(0);
}

bool lockdriveIsMoving() {
  bool ret = false;
  if(sg.state != 0) ret = true;
  return ret;
}

void lockdriveOpen() {
  sg.wait_halt    = 5000;
  lockdriveStartMove(-6000, 5000);
  LL_Log.printf("pos: %d, sens: %d\r\n", tmc.XACTUAL(), sg.angleDiff);
}

void lockdriveClose() {
  sg.wait_halt    = 500;
  lockdriveStartMove(6000, 5000);
  LL_Log.printf("pos: %d, sens: %d\r\n", tmc.XACTUAL(), sg.angleDiff);
}

void lockdriveManualCommands() {
    static int32_t tg = 0;

    if(LL_Log.receiveLine[0]=='e') digitalWrite(triEN, HIGH);
    if(LL_Log.receiveLine[0]=='E') digitalWrite(triEN, LOW);
    if(LL_Log.receiveLine[0]=='s') { tmc.VMAX(vmax); tg += 8000; tmc.XTARGET(tg); }
    if(LL_Log.receiveLine[0]=='S') { tmc.VMAX(vmax); tg -= 8000; tmc.XTARGET(tg); }
    if(LL_Log.receiveLine[0]=='p') {tmc.sg_stop(1);  }
    if(LL_Log.receiveLine[0]=='P') {tmc.sg_stop(0);  }
    if(LL_Log.receiveLine[0]==' ') {
      tmc.VMAX(0);
      tmc.RAMPMODE(0);
      tmc.sg_stop(0);
      tmc.XACTUAL(0);
      tmc.sg_stop(1);
      tg = 0;
      LL_Log.printf("Stop at %d\r\n", tmc.XACTUAL());
    }
    if(LL_Log.receiveLine[0]=='o') {
      lockdriveStartMove(-6000, 5000);
      LL_Log.printf("pos: %d, sens: %d\r\n", tmc.XACTUAL(), sg.angleDiff);
    }
    if(LL_Log.receiveLine[0]=='c') {
      lockdriveStartMove(6000, 5000);
      LL_Log.printf("pos: %d, sens: %d\r\n", tmc.XACTUAL(), sg.angleDiff);
    }
    if(LL_Log.receiveLine[0]=='O') {
      tmc.VMAX(vmax);
      tmc.RAMPMODE(1);
    }
    if(LL_Log.receiveLine[0]=='C') {
      tmc.VMAX(vmax);
      tmc.RAMPMODE(2);
    }
    if(LL_Log.receiveLine[0]=='w') {
      int addr, data;
      if(sscanf(&LL_Log.receiveLine[1],"%d %d",&addr, &data)>=2) {
        LL_Log.printf("Addr: %d, Data: %d\r\n", addr, data);
        switch(addr) {
          case 1:
            tmc.RAMPMODE((uint8_t)data);
            break;

          case 2:
            tmc.rms_current(data);
            LL_Log.printf("ihold: %d, irun: %d, iholddelay: %d, global scaler: %d\r\n", 
              (int)tmc.ihold(), (int)tmc.irun(),(int)tmc.iholddelay(),(int)tmc.GLOBAL_SCALER());
            break;  

          case 3:
            tmc.ihold(data);
            LL_Log.printf("ihold: %d, irun: %d\r\n", (int)tmc.ihold(), (int)tmc.irun());
            break;    

          case 4:
            tmc.en_pwm_mode((bool)data);
            LL_Log.printf("en_pwm_mode %d\r\n", (int)tmc.en_pwm_mode());
            break;   

          case 5:
            tmc.sgt(data);
            LL_Log.printf("SGT: %d\r\n", (int)tmc.sgt());
            break;   

          case 10:
            LL_Log.printf("TPWMTHRS: %d, TCOOLTHRS: %d, THIGH: %d\r\n", 
              tmc.TPWMTHRS(), tmc.TCOOLTHRS(), tmc.THIGH());
            break; 

          case 11:
            tmc.TPWMTHRS(data);
            LL_Log.printf("TPWMTHRS: %d, TCOOLTHRS: %d, THIGH: %d\r\n", 
              tmc.TPWMTHRS(), tmc.TCOOLTHRS(), tmc.THIGH());
            break; 

          case 12:
            tmc.TCOOLTHRS(data);
            LL_Log.printf("TPWMTHRS: %d, TCOOLTHRS: %d, THIGH: %d\r\n", 
              tmc.TPWMTHRS(), tmc.TCOOLTHRS(), tmc.THIGH());
            break; 

          case 13:
            tmc.THIGH(data);
            LL_Log.printf("TPWMTHRS: %d, TCOOLTHRS: %d, THIGH: %d\r\n", 
              tmc.TPWMTHRS(), tmc.TCOOLTHRS(), tmc.THIGH());
            break; 

          case 20:
            vmax = data;
            LL_Log.printf("Set vmax: %d\r\n", vmax);
            break;

        }
      }
    }
}

#endif