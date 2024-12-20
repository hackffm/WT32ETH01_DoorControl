
#pragma once

#include <Arduino.h>
#include <Print.h>
#include <HardwareSerial.h>
#include <Stream.h>
#include <elapsedMillis.h>

#if defined(ESP8266)
  #include <ESP8266WiFi.h>
#elif defined(ESP32)
  #include <WiFi.h>
#else
  #include <WiFi.h>  
#endif

// Activate none if no display is needed
//#define U8G2_DISPLAYTYPE U8G2_SSD1306_128X64_NONAME_F_HW_I2C  /* 0.96 inch */
 #define U8G2_DISPLAYTYPE U8G2_SH1106_128X64_NONAME_F_HW_I2C   /* 1.3 inch */
//#define U8G2_DISPLAYTYPE U8G2_SSD1309_128X64_NONAME0_F_HW_I2C /* 2.42 inch */

#if defined(U8G2_DISPLAYTYPE)
  #include <U8g2lib.h>
  #include <Wire.h>
  extern U8G2_DISPLAYTYPE u8g2; 
#endif


#define RECEIVELINEBUFSIZE 200
#define LL_LOG_MAX_CLIENTS 3

class LL_Log_c : public Print {
  public:
    LL_Log_c() { receiveLine[0] = 0; }

    void begin(uint32_t serialRate = 115200 /*74880*/, uint16_t tcpPort = 2222);
    void setSerial(Stream *ptrSer) { _ptrlogSerial = ptrSer; }

    void update(int8_t isConnected = -1);

    size_t write(const uint8_t *buffer, size_t size); 
    size_t write(uint8_t v);

    void receiveChar(char c);

    char receiveLine[RECEIVELINEBUFSIZE];
    bool receiveLineAvailable();

    bool SSD1306Found() {return _SSD1306DisplayFound; }
    void SSD1306SetAutoUpdate(uint16_t updateMillis) { _SSD1306AutoUpdate = updateMillis; }
    
   /* Special characters:
    * 0x0a \n: new line
    * $: Escape, next char defines action
    *    $: prints $
    *    c,l,r: center/left/right aligned
    *    0..9,a,b,d,e,f,g: set fonts
    *    !: clear display
    */
    void SSD1306drawString(const char *str, int x, int y, int dy); 

  protected:
    Stream *_ptrlogSerial = NULL;
    WiFiServer *_ptrlogTCPServer = NULL;
    WiFiClient _logTCPClients[LL_LOG_MAX_CLIENTS];
    bool _logTCPClientConnected = false;
    uint16_t _logTCPServerPort = 0;
    bool _PreviousWifiConnectionState = false;
    char _receiveLineBuf[RECEIVELINEBUFSIZE];
    uint16_t _receiveLineIndex = 0;
    bool _receiveLineAvailable = false;
    bool _SSD1306DisplayFound = false; 
    bool _SSD1306DisplayNeedLogUpdate = false; 
    uint16_t _SSD1306AutoUpdate = 500;
    elapsedMillis _SSD1306UpdateMillis = 0;
    SemaphoreHandle_t _xMutex = NULL;  // Create a mutex object

};

extern LL_Log_c LL_Log;

class LL_Led {
  public:
    LL_Led(int pin, bool lowActive = false) {
      _pin = pin;
      _lowActive = lowActive;
      /*
      pinMode(_pin, OUTPUT);
      if(_lowActive) 
        digitalWrite(_pin, HIGH);
      else
        digitalWrite(_pin, LOW);
      */
      _state = 0;  
    }
    void set(int8_t newstate, uint32_t autoOff = 0) {
      int8_t outval;
      if(newstate == 0) _state = 0;
      if(newstate == 1) _state = 1;
      if(newstate == 2) _state = _state?0:1;
      if(_lowActive) outval = _state?0:1; else outval = _state;
      pinMode(_pin, OUTPUT);
      digitalWrite(_pin, outval?HIGH:LOW);
      if(autoOff != 0) _autoOff = millis() + autoOff; else _autoOff = 0;
    }
    uint8_t get() {
      return(_state);
    }
    void update() {
      if(_autoOff != 0) {
        if((int32_t)(millis() - _autoOff) > 0L) {
          _autoOff = 0;
          this->set(0);
        }
      }
    }
  protected:
    int _pin = -1;
    bool _lowActive = false;
    int8_t _state = 0;
    uint32_t _autoOff = 0;
};
