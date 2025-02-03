#include <Arduino.h>
#include <Print.h>
#include <HardwareSerial.h>
#include <Stream.h>
#if defined(ESP8266)
  #include <ESP8266WiFi.h>
#elif defined(ESP32)
  #include <WiFi.h>
#else
  #include <WiFi.h>
#endif

#include <elapsedMillis.h>
#include "LL_Lib.h"

#if defined(U8G2_DISPLAYTYPE)
  #include <U8g2lib.h>
  #include <Wire.h>
  U8G2_DISPLAYTYPE u8g2(U8G2_R1, /* reset=*/ U8X8_PIN_NONE);
  U8G2LOG u8g2log;
  // log size should be adjusted to fit good in log-window size
  // max size inside box 124x60 

  /*
  #define U8LOG_FONT u8g2_font_5x7_tf
  #define U8LOG_FONT_X  5
  #define U8LOG_FONT_Y  7
  #define U8LOG_WIDTH 25
  #define U8LOG_HEIGHT 6
  */

  #define U8G2_DISP_WIDTH  64 /* 128 */
  #define U8G2_DISP_HEIGHT 128
  #define U8LOG_FONT u8g2_font_tom_thumb_4x6_tf
  #define U8LOG_FONT_X  4
  #define U8LOG_FONT_Y  6
  #define U8LOG_WIDTH ((U8G2_DISP_WIDTH - 4) / U8LOG_FONT_X)
  #define U8LOG_HEIGHT 6

  uint8_t u8log_buffer[U8LOG_WIDTH*U8LOG_HEIGHT];
#endif

void LL_Log_c::begin(uint32_t serialRate, uint16_t tcpPort) {
  _xMutex = xSemaphoreCreateMutex(); 
  if(serialRate != 0) {
    Serial.begin(serialRate);
    _ptrlogSerial = &Serial;
  } 
  _logTCPServerPort = tcpPort;
  _PreviousWifiConnectionState = false;

  #if defined(U8G2_DISPLAYTYPE)
    // Check if OLED Display is available
    Wire.begin(32,33);
    bool error;
    Wire.setClock(400000);
    Wire.beginTransmission(0x3c); error = Wire.endTransmission();
    if(error == 0){
      LL_Log.println("OLED Display found.");

      u8g2.begin();
      u8g2.enableUTF8Print();
      
      // dim display down 
      //u8g2.sendF("ca", 0xdb, 0); // not for SH1106 - need af (e3) afterwards
      //u8g2.sendF("ca", 0xd9, 0x2f);
      
      u8g2.sendF("ca", 0xaf); // reactivate if display turns off (SH1106 need it)
      u8g2.setContrast(0x32); // u8g2.sendF("ca", 0x81, 32);
      
      u8g2.clearBuffer();          // clear the internal memory
      u8g2.setFont(u8g2_font_fub14_tf); // choose a suitable font
      u8g2.setCursor(0, 14);
      u8g2.setDrawColor(1);
      //u8g2.drawBox(0,0,127,63);
      //u8g2.setDrawColor(0);
      u8g2.print("LL_Lib V1.0");  // write something to the internal memory
      u8g2.sendBuffer();

      u8g2log.begin(U8LOG_WIDTH, U8LOG_HEIGHT, u8log_buffer);	// assign only buffer, update full manual

      _SSD1306DisplayFound = true;
    } else {
      _SSD1306DisplayFound = false;
      LL_Log.println("OLED Display NOT found.");    
    }
  #endif
}

size_t LL_Log_c::write(uint8_t v) {
  return write(&v, 1);
}

size_t LL_Log_c::write(const uint8_t *buffer, size_t size) {
  if(xSemaphoreTake(_xMutex, 1000 / portTICK_PERIOD_MS)) {
    if(_ptrlogSerial) _ptrlogSerial->write(buffer, size);
    for(int i=0; i<LL_LOG_MAX_CLIENTS; i++) {
      if(_logTCPClients[i]) {
        if(_logTCPClients[i].connected()) _logTCPClients[i].write(buffer, size);
        delay(1);
      }
    }
    #if defined(U8G2_DISPLAYTYPE)
      if(_SSD1306DisplayFound) {
        u8g2log.write(buffer, size);
        _SSD1306DisplayNeedLogUpdate = true;
      }
    #endif
    xSemaphoreGive(_xMutex);
  }
  return size;
}

void LL_Log_c::receiveChar(char c) {
  if(_receiveLineAvailable == false) {
    if(c >= ' ') {
      if(_receiveLineIndex < (RECEIVELINEBUFSIZE - 2)) {
        _receiveLineBuf[_receiveLineIndex++] = c;
      } else {
        _receiveLineBuf[_receiveLineIndex] = 0;
        _receiveLineAvailable = true;
        _receiveLineIndex = 0;
      }
    }
    if((c == 0x0d) || (c == 0x0a)) {
      if(_receiveLineIndex > 0) {
        _receiveLineBuf[_receiveLineIndex] = 0;
        _receiveLineAvailable = true;
        _receiveLineIndex = 0;        
      }
    }
  }
}

bool LL_Log_c::receiveLineAvailable() {
  bool ret = _receiveLineAvailable;
  if(ret) {
    strncpy(receiveLine, _receiveLineBuf, RECEIVELINEBUFSIZE);
    _receiveLineAvailable = false;
  }
  return(ret);
}

void LL_Log_c::update(int8_t isConnected) {
  static uint8_t stillAlive = 0;
  stillAlive++;

  // Check Wifi connection state change
  bool isWifiConnected = WiFi.isConnected();
  // overwrite if isConnected != -1
  if(isConnected != -1) isWifiConnected = isConnected;

  if(_PreviousWifiConnectionState != isWifiConnected) {
    if(isWifiConnected) {
      // Wifi now available after it was disconnected
      if(_ptrlogTCPServer != NULL) {
        _ptrlogTCPServer->close();
        delete _ptrlogTCPServer;
        _ptrlogTCPServer = NULL;
      }
      if(_logTCPServerPort != 0) {
        // Start TCP Server
        WiFiServer *srv = new WiFiServer(_logTCPServerPort);
        srv->begin(_logTCPServerPort);
        srv->setNoDelay(true);
        _ptrlogTCPServer = srv;
        Serial.printf("Log-Server started on port %d.\r\n",_logTCPServerPort);
      }

    } else {
      // Wifi now went offline, stop all clients
       if(_ptrlogTCPServer != NULL) {
        _logTCPClientConnected = false;
        for(int i=0; i<LL_LOG_MAX_CLIENTS; i++) {
          if(_logTCPClients[i]) {
            _logTCPClients[i].stop();
            Serial.printf("Log-Client %d stopped.\r\n", i);
          }
        }
        _logTCPClientConnected = false;
        _ptrlogTCPServer->close();
        delete _ptrlogTCPServer;
        _ptrlogTCPServer = NULL;
      }     
    }
    _PreviousWifiConnectionState = isWifiConnected;
  }

  // Check if WiFi is connected 
  if(isWifiConnected) {
    if(_ptrlogTCPServer != 0) {
      if (_ptrlogTCPServer->hasClient()) {
        bool accepted = false;
        for(int i=0; i<LL_LOG_MAX_CLIENTS; i++) {
          if((!_logTCPClients[i]) || (!_logTCPClients[i].connected())) {
            if(_logTCPClients[i]) _logTCPClients[i].stop();
            _logTCPClients[i] = _ptrlogTCPServer->available();
            Serial.printf("Log-Client %d connected.\r\n", i);
            _logTCPClients[i].println("TCP Log opened.");
            accepted = true;
            _logTCPClientConnected = true;
            break;
          }
        }
        if(!accepted) {
          _ptrlogTCPServer->available().stop();
          Serial.println("Log client connection refused - no free slots.");
        }
      }

      // check all clients for available data or disconnections
      bool clientsleft = false;
      for(int i=0; i<LL_LOG_MAX_CLIENTS; i++) {
          if(_logTCPClients[i]) {
            if(!_logTCPClients[i].connected()) {
              _logTCPClients[i].stop(); 
              Serial.printf("Log-Client %d disconnected.\r\n", i);
              continue;           
            }
            clientsleft = true;
            if(_logTCPClients[i].available()) {
              receiveChar(_logTCPClients[i].read());
            }
          }
      }
      _logTCPClientConnected = clientsleft;
    }
  }

  if(_ptrlogSerial) {
    if(_ptrlogSerial->available()) {
      receiveChar(_ptrlogSerial->read());
    }
  }

  #if defined(U8G2_DISPLAYTYPE)
    if(_SSD1306DisplayFound) {
      if((_SSD1306AutoUpdate > 0) 
         && (_SSD1306UpdateMillis > _SSD1306AutoUpdate)) {
        _SSD1306UpdateMillis = 0;
        _SSD1306DisplayNeedLogUpdate = true;
      }
      if(_SSD1306DisplayNeedLogUpdate) {
         // Align Log-box from lower-left corner
         int logbox_y = u8g2.getDisplayHeight() - (U8LOG_HEIGHT * U8LOG_FONT_Y) - 3;
         u8g2.setFont(U8LOG_FONT);
         u8g2.setFontPosTop();
         u8g2.setFontMode(0);
         u8g2.setDrawColor(1);
         u8g2.drawBox(0,logbox_y,u8g2.getDisplayWidth(),u8g2.getDisplayHeight()-logbox_y);
         u8g2.setDrawColor(0);
         u8g2.drawBox(1,logbox_y+1,u8g2.getDisplayWidth()-2,u8g2.getDisplayHeight()-(logbox_y+2));
         u8g2.drawHLine(stillAlive % u8g2.getDisplayWidth(),logbox_y,4); // interrupt line to show beeing alive
         u8g2.setDrawColor(1);
         u8g2.drawLog(2,logbox_y+1,u8g2log);
         u8g2.sendBuffer();
        _SSD1306DisplayNeedLogUpdate = false;
      }
    }
  #endif
}

/* Special characters:
 * 0x0a \n: new line
 * $: Escape, next char defines action
 *    $: prints $
 *    c,l,r: center/left/right aligned
 *    0..9,a,b,d,e,f,g: set fonts
 *    !: clear display
 */
void LL_Log_c::SSD1306drawString(const char *str, int x, int y, int dy) {
  #if defined(U8G2_DISPLAYTYPE)
  if(!_SSD1306DisplayFound) return;
  char buf[127];
  char c;
  int  idx = 0;
  int w = u8g2.getDisplayWidth();
  int sw = 0; // getUTF8Width(
  int dsFlags = 0;
  int newX = x;

   do {
    c = *str++;
    if(c>=' ') {
      if(dsFlags & 1) {
        dsFlags &= ~(1);
        switch(c) {
          case '$': buf[idx++] = c; break;
          case '!': u8g2.clearBuffer(); break;
          case '+': y++; break;
          case '-': y--; break;
          case 'c':
            dsFlags &= ~(6);
            dsFlags |= 2;
            break;
          case 'l':
            dsFlags &= ~(6);
            break; 
          case 'r':
            dsFlags &= ~(6);
            dsFlags |= 4;
            break;
          // Fonts
          case '0': u8g2.setFont(u8g2_font_tinyunicode_tf); break; // 27  
          case '1': u8g2.setFont(u8g2_font_nokiafc22_tf); break; // 22.2  
          case '2': u8g2.setFont(u8g2_font_busdisplay11x5_te); break;   // 21.5
          case '3': u8g2.setFont(u8g2_font_sonicmania_te); break; // 19.1          nice bold
          case 'E': u8g2.setFont(u8g2_font_prospero_bold_nbp_tf); break; // ?          nice bold
          case '4': u8g2.setFont(u8g2_font_DigitalDiscoThin_tf); break; // 16.2
          case '5': u8g2.setFont(u8g2_font_scrum_tf); break; // 16 a
          case '6': u8g2.setFont(u8g2_font_DigitalDisco_tf); break; // 16 b        nice bold
          case 'G': u8g2.setFont(u8g2_font_profont17_tf); break; // 16 b        nice bold
          case '7': u8g2.setFont(u8g2_font_fur11_tf); break; // 16 c
          case '8': u8g2.setFont(u8g2_font_Pixellari_tf); break; // 15.5           nice bold
          case '9': u8g2.setFont(u8g2_font_fub11_tf); break; // 14.1
          case 'a': u8g2.setFont(u8g2_font_lucasarts_scumm_subtitle_o_tf); break; // 12
          case 'b': u8g2.setFont(u8g2_font_lucasarts_scumm_subtitle_r_tf); break; // 12          
          case 'd': u8g2.setFont(u8g2_font_fur14_tf); break; // 11.5
          case 'e': u8g2.setFont(u8g2_font_fub14_tf); break; // 11.5
          case 'f': u8g2.setFont(u8g2_font_chargen_92_tf); break; //10.9
          case 'i': u8g2.setFont(u8g2_font_m2icon_9_tf); break;
                             
        }
      } else {
        if(c=='$') {
          dsFlags |= 1;
        } else {
          buf[idx++] = c;
        }        
      }
    } else {
      switch(c) {
        case 0:
        case '\n':
          buf[idx] = 0;
          sw = u8g2.getUTF8Width(buf);
          if(dsFlags & 2) {
            // centered (plus offsetted by x)
            newX = (w - sw)/2 + x;
          } else if(dsFlags & 4) {
            // right (minus offsetted by x)
            newX = (w - sw) - x;
          } else {
            newX = x;
          }
          y += u8g2.getAscent()-u8g2.getDescent()+dy;
          u8g2.drawUTF8(newX,y,buf);
          idx = 0;
          break; 
      }
    }
  } while(c);
  _SSD1306DisplayNeedLogUpdate = true;
  // u8g2.sendBuffer();
  
  #endif
}

LL_Log_c LL_Log;
