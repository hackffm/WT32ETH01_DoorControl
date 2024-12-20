#ifndef ASYNC_FS_WEBSERVER_H
#define ASYNC_FS_WEBSERVER_H

#include <FS.h>
#include <LittleFS.h>

#include <ESPAsyncWebServer.h>

  #include <Update.h>

  #include "esp_wifi.h"
  #include "esp_task_wdt.h"
  #include "sys/stat.h"


#define DBG_OUTPUT_PORT     Serial
#define LOG_LEVEL           3         // (0 disable, 1 error, 2 info, 3 debug)

#ifndef ESP_FS_WS_EDIT
    #define ESP_FS_WS_EDIT              1   //has edit methods
    #ifndef ESP_FS_WS_EDIT_HTM
        #define ESP_FS_WS_EDIT_HTM      1   //included from progmem
    #endif
#endif

#if ESP_FS_WS_EDIT_HTM
    #include "edit_htm.h"
#endif

    #define ARDUINOJSON_USE_LONG_LONG 1
    #include <ArduinoJson.h>
#if ARDUINOJSON_VERSION_MAJOR > 6
    #define JSON_DOC(x) JsonDocument doc
#else
    #define JSON_DOC(x) DynamicJsonDocument doc((size_t)x)
#endif

#include "SerialLog.h"


class AsyncFsWebServer : public AsyncWebServer
{
  protected:
    //AsyncWebSocket* m_ws = nullptr;
    // AsyncWebHandler *m_captive = nullptr;
    //DNSServer* m_dnsServer = nullptr;

    //void handleWebSocket(AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t * data, size_t len);
    void handleUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);
    
    void notFound(AsyncWebServerRequest *request);

    void getStatus(AsyncWebServerRequest *request);
    
    void handleFileName(AsyncWebServerRequest *request);

    // edit page, in useful in some situation, but if you need to provide only a web interface, you can disable
    void deleteContent(String& path) ;
    void handleFileDelete(AsyncWebServerRequest *request);
    void handleFileCreate(AsyncWebServerRequest *request);
    void handleFsStatus(AsyncWebServerRequest *request);
    void handleFileList(AsyncWebServerRequest *request);
    void handleFileEdit(AsyncWebServerRequest *request);

  void setTaskWdt(uint32_t timeout);

  //  Create a dir if not exist on uploading files
  bool createDirFromPath( const String& path) ;

  private:
    char* m_pageUser = nullptr;
    char* m_pagePswd = nullptr;

    uint16_t m_port;
    uint32_t m_timeout = 10000;
    size_t m_contentLen = 0;

    bool m_filesystem_ok = false;

    fs::FS* m_filesystem = nullptr;

  public:
    AsyncFsWebServer(uint16_t port, fs::FS &fs = LittleFS) :
    AsyncWebServer(port),
    m_filesystem(&fs)
    {
      m_port = port;
    }

    ~AsyncFsWebServer() {
      reset();
      end();
      if(_catchAllHandler) delete _catchAllHandler;
    }

    inline TaskHandle_t getTaskHandler() {
      return xTaskGetCurrentTaskHandle();
    }

    //  Start webserver and bind a websocket event handler (optional)
    bool init();

    //  Enable authenticate for /setup webpage
    void setAuthentication(const char* user, const char* pswd);

    //  List FS content
    void printFileList(fs::FS &fs, const char * dirname, uint8_t levels);

    //  Send a default "OK" reply to client
    void sendOK(AsyncWebServerRequest *request);
};

#endif
