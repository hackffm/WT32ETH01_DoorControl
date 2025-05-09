#include <Arduino.h>
#include <ETH.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <esp_task_wdt.h>

//30 seconds WDT
#define WDT_TIMEOUT 30

#include "time.h"
#include "esp_sntp.h"

#include <esp_wifi.h>
#include <esp_now.h>
#include <esp_log.h>
//#include <QuickEspNow.h>

#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

#include "ping/ping_sock.h"
#include "lwip/netdb.h"

#include "lan.h"

#include "LL_Lib.h"

#include <elapsedMillis.h>

#include <FS.h>
#include <LittleFS.h>
#include <AsyncFsWebServer.h>

#include "auth/protocol.h"

#define ESPNOW_CHANNEL 13
#define ESPNOW_PREFIX "D00"

//#define ETH_ADDR 1
//#define ETH_POWER_PIN 16//-1 //16 // Do not use it, it can cause conflict during the software reset.
//#define ETH_POWER_PIN_ALTERNATIVE 16 //17
//#define ETH_MDC_PIN 23
//#define ETH_MDIO_PIN 18
//#define ETH_TYPE ETH_PHY_LAN8720
//#define ETH_CLK_MODE ETH_CLOCK_GPIO17_OUT // ETH_CLOCK_GPIO0_IN

bool eth_connected = false;
const char *lan_hostname = NULL;
int ota_in_progress = 0;
const char upload_auth[] = DEF_UPLOAD_AUTH;

const char TIME_ZONE[] = "CET-1CEST,M3.5.0,M10.5.0/3";
const char NTP_SERVER_POOL[] = "de.pool.ntp.org";
bool TimeUpdated = false;
time_t now;
tm lanCLT; // Current local time from type tm

// AsyncWebServer web_server(80);
AsyncFsWebServer web_server(80);
AsyncWebSocket ws("/ws");
bool ledState = 0;

// const char index_html[] PROGMEM = R"rawliteral(
// <!DOCTYPE HTML><html>
// </html>
// )rawliteral";

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
  AwsFrameInfo *info = (AwsFrameInfo*)arg;
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
    data[len] = 0;
    if (strcmp((char*)data, "toggle") == 0) {
      ledState = !ledState;
      ws.textAll(String(ledState));
    }
  }
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
             void *arg, uint8_t *data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      LL_Log.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
      break;
    case WS_EVT_DISCONNECT:
      LL_Log.printf("WebSocket client #%u disconnected\n", client->id());
      break;
    case WS_EVT_DATA:
      {
        AwsFrameInfo *info = (AwsFrameInfo*)arg;
        String msg = "";
        if(info->final && info->index == 0 && info->len == len){
            //the whole message is in a single frame and we got all of it's data
            LL_Log.printf("ws[%s][%lu] %s-message[%llu] from %s\n", 
              server->url(), client->id(), (info->opcode == WS_TEXT)?"text":"binary", 
              info->len, client->remoteIP().toString().c_str());
            /*  
            if (info->opcode == WS_TEXT){
                for(size_t i=0; i < info->len; i++) {
                    msg += (char) data[i];
                }
            }
            else {
                char buff[4];
                for(size_t i=0; i < info->len; i++) {
                    sprintf(buff, "%02x ", (uint8_t) data[i]);
                    msg += buff ;
                }
            }
            LL_Log.printf("%s\n",msg.c_str());
            */
            if(info->opcode == WS_BINARY) {
              uint8_t ansbuf[250];
              size_t anslen = 250;
              bool protOK = protocolInput(data, info->len, ansbuf, &anslen, 1);
              if(protOK) {
                ws.binary(client->id(), ansbuf, anslen);
              }
            }
        } 
      }   
      handleWebSocketMessage(arg, data, len);
      break;
    case WS_EVT_PONG:
    case WS_EVT_ERROR:
      break;
  }
}

String processor(const String& var){
  Serial.println(var);
  if(var == "STATE"){
    if (ledState){
      return "ON";
    }
    else{
      return "OFF";
    }
  }
  if(var == "HOSTNAME"){
      return String(lan_hostname);
  }
  return String("%" + var + "%");
}


// Callback function (get's called when time adjusts via NTP)
void timeavailable(struct timeval *t)
{
  lanUpdateCLT();
  LL_Log.printf("Got NTP time: %02d:%02d\r\n", lanCLT.tm_hour, lanCLT.tm_min);
  TimeUpdated = true;
}

void lanUpdateCLT() {
  time(&now); 
  localtime_r(&now, &lanCLT);
}  

void WiFiEvent(WiFiEvent_t event)
{
  switch (event) {
    case ARDUINO_EVENT_ETH_START:
      LL_Log.print("ETH Started");
      //set eth hostname here
      if(lan_hostname) {
        ETH.setHostname(lan_hostname);
        LL_Log.printf(": %s\r\n", lan_hostname);
      } else {
        LL_Log.println(".");
      }
      break;
    case ARDUINO_EVENT_ETH_CONNECTED:
      LL_Log.println("ETH Connected.");
      break;
    case ARDUINO_EVENT_ETH_GOT_IP:
      LL_Log.print("ETH MAC: ");
      LL_Log.print(ETH.macAddress());
      LL_Log.print(", IPv4: ");
      LL_Log.print(ETH.localIP());
      if (ETH.fullDuplex()) {
        LL_Log.print(", FULL_DUPLEX");
      }
      LL_Log.print(", ");
      LL_Log.print(ETH.linkSpeed());
      LL_Log.println("Mbps");
      sntp_restart();
      eth_connected = true;
      break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
      LL_Log.println("ETH Disconnected");
      eth_connected = false;
      break;
    case ARDUINO_EVENT_ETH_STOP:
      LL_Log.println("ETH Stopped");
      eth_connected = false;
      break;
    default:
      break;
  }
}

void espNowdataReceived (uint8_t* address, uint8_t* data, uint8_t len, signed int rssi, bool broadcast) {
    /*
    LL_Log.print ("Received: ");
    LL_Log.printf ("%.*s\n", len, data);
    LL_Log.printf ("RSSI: %d dBm\n", rssi);
    LL_Log.printf ("From: " MACSTR "\n", MAC2STR (address));
    LL_Log.printf ("%s\n", broadcast ? "Broadcast" : "Unicast");
    */
    uint8_t ansbuf[250];
    size_t anslen = 250;
    int sourceid = 2;
    LL_Log.printf("\nESP Now Rx w RSSI: %d dBm\n", rssi);
    if(rssi > -70) sourceid = 0;
    bool protOK = protocolInput(data, len, ansbuf, &anslen, sourceid);
    if(protOK) {
      lanEspNowTx(ansbuf, anslen);
    }    
}

// Callback receive
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
void on_data_recv(const esp_now_recv_info_t *esp_now_info, const uint8_t *data_orig, int len) {
  const uint8_t *mac_addr  = esp_now_info->src_addr;
  #else
void on_data_recv(const uint8_t *mac_addr, const uint8_t *data_orig, int len) {
#endif  
    uint8_t data[255];
    if(len < 1) return;
    if(len > 251) return;
    memcpy(data, data_orig, len); data[len] = 0;
    signed int rssi = -1;
    rssi = esp_now_info->rx_ctrl->rssi;
    /*
    LL_Log.print ("Received: ");
    LL_Log.printf ("%.*s\n", len, data);
    LL_Log.printf ("RSSI: %d dBm\n", rssi);
    LL_Log.printf ("From: " MACSTR "\n", MAC2STR (address));
    LL_Log.printf ("%s\n", broadcast ? "Broadcast" : "Unicast");
    */
   uint8_t ansbuf[250];
   size_t anslen = 250;
   int sourceid = 2;
   LL_Log.printf("\nESP Now Rx w RSSI: %d dBm\n", rssi);
   if(rssi > -76) sourceid = 0;
   bool protOK = protocolInput(data, len, ansbuf, &anslen, sourceid);
   if(protOK) {
     lanEspNowTx(ansbuf, anslen);
   } 
}

// Callback transmit
void on_data_sent(const uint8_t *mac, esp_now_send_status_t status) {
  //LL_Log.printf("Tx: %s\n", status == ESP_NOW_SEND_SUCCESS ? "Ok" : "Failed");
  if(status != ESP_NOW_SEND_SUCCESS) LL_Log.printf("EspNow Tx failed!\n");
}

void lanEspNowTx(const uint8_t *data, int len) {
  if(len > 0) {
    //quickEspNow.send(mac, data, len);
    esp_now_peer_info_t peer = {
      .peer_addr = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
      .channel = ESPNOW_CHANNEL,
      .encrypt = false
    };
    ESP_ERROR_CHECK(esp_now_send(peer.peer_addr, data, len));
  }
}

elapsedMillis pingRunningTime = 0;
int pingRunStatus = 0;
esp_ping_handle_t pingHandle;
char pingResultString[200];

static void cb_on_ping_success(esp_ping_handle_t hdl, void *args)
{
    // optionally, get callback arguments
    // const char* str = (const char*) args;
    // printf("%s\r\n", str); // "foo"
    uint8_t ttl;
    uint16_t seqno;
    uint32_t elapsed_time, recv_len;
    ip_addr_t target_addr;
    esp_ping_get_profile(hdl, ESP_PING_PROF_SEQNO, &seqno, sizeof(seqno));
    esp_ping_get_profile(hdl, ESP_PING_PROF_TTL, &ttl, sizeof(ttl));
    esp_ping_get_profile(hdl, ESP_PING_PROF_IPADDR, &target_addr, sizeof(target_addr));
    esp_ping_get_profile(hdl, ESP_PING_PROF_SIZE, &recv_len, sizeof(recv_len));
    esp_ping_get_profile(hdl, ESP_PING_PROF_TIMEGAP, &elapsed_time, sizeof(elapsed_time));
    snprintf(pingResultString, sizeof(pingResultString), "%d bytes from %s icmp_seq=%d ttl=%d time=%d ms\n",
           recv_len, inet_ntoa(target_addr.u_addr.ip4), seqno, ttl, elapsed_time);
    // LL_Log.printf("%s", pingResultString);
    pingRunStatus = 2;
}

static void cb_on_ping_timeout(esp_ping_handle_t hdl, void *args)
{
    uint16_t seqno;
    ip_addr_t target_addr;
    esp_ping_get_profile(hdl, ESP_PING_PROF_SEQNO, &seqno, sizeof(seqno));
    esp_ping_get_profile(hdl, ESP_PING_PROF_IPADDR, &target_addr, sizeof(target_addr));
    snprintf(pingResultString, sizeof(pingResultString), "From %s icmp_seq=%d timeout\n", inet_ntoa(target_addr.u_addr.ip4), seqno);
    LL_Log.printf("%s", pingResultString);
    pingRunStatus = 3;
}

static void cb_on_ping_end(esp_ping_handle_t hdl, void *args)
{
    uint32_t transmitted;
    uint32_t received;
    uint32_t total_time_ms;

    esp_ping_get_profile(hdl, ESP_PING_PROF_REQUEST, &transmitted, sizeof(transmitted));
    esp_ping_get_profile(hdl, ESP_PING_PROF_REPLY, &received, sizeof(received));
    esp_ping_get_profile(hdl, ESP_PING_PROF_DURATION, &total_time_ms, sizeof(total_time_ms));
    snprintf(pingResultString, sizeof(pingResultString), "%d packets transmitted, %d received, time %dms\n", transmitted, received, total_time_ms);
    // LL_Log.printf("%s", pingResultString);
    pingRunStatus = 11;
    if(received > 0) pingRunStatus = 20;
}

// Pings gateway if no host is given
void lanPingStart(const char *host) {

  if(pingRunStatus > 0) {
    esp_ping_stop(pingHandle);
    esp_ping_delete_session(pingHandle);
  }

  /* convert URL to IP address */
  ip_addr_t target_addr;

  if((host == NULL) || (strlen(host) == 0)) {
    IPAddress ip = ETH.gatewayIP();
    ip.to_ip_addr_t(&target_addr);
  } else {
    addrinfo hint;
    addrinfo *res = NULL;
    memset(&hint, 0, sizeof(hint));
    memset(&target_addr, 0, sizeof(target_addr));
    int ga_ret = getaddrinfo(host, NULL, &hint, &res);
    struct in_addr addr4 = ((struct sockaddr_in *) (res->ai_addr))->sin_addr;
    inet_addr_to_ip4addr(ip_2_ip4(&target_addr), &addr4);
    freeaddrinfo(res);
    if(ga_ret != 0) {
      LL_Log.printf("getaddrinfo failed: %d\n", ga_ret);
      pingRunStatus = -1;
      return;
    }
  }

  esp_ping_config_t ping_config = ESP_PING_DEFAULT_CONFIG();
  ping_config.target_addr = target_addr;          // target IP address
  ping_config.count = 1;   

  /* set callback functions */
  esp_ping_callbacks_t cbs;
  cbs.on_ping_success = cb_on_ping_success;
  cbs.on_ping_timeout = cb_on_ping_timeout;
  cbs.on_ping_end = cb_on_ping_end;
  cbs.cb_args = NULL;  // arguments that feeds to all callback functions, can be NULL

  esp_ping_new_session(&ping_config, &cbs, &pingHandle);
  esp_ping_start(pingHandle);
  pingRunStatus = 1;
  pingRunningTime = 0;
}

// <=0 = not started, 1...3 = running, 10...11 = timeout, 20 = success
int lanPingStatus() {
  if((pingRunStatus > 0) && (pingRunStatus < 10)) {
    if(pingRunningTime > 5000) {
      pingRunStatus = 10;
    }
  }
  return pingRunStatus;
}

void lanInit(const char *hostname) {
  lan_hostname = hostname;

  //pinMode(ETH_POWER_PIN_ALTERNATIVE, OUTPUT);
  //digitalWrite(ETH_POWER_PIN_ALTERNATIVE, HIGH);
  //ETH.begin(ETH_ADDR, ETH_POWER_PIN, ETH_MDC_PIN, ETH_MDIO_PIN, ETH_TYPE, ETH_CLK_MODE); // Enable ETH DHCP

  WiFi.onEvent(WiFiEvent);
  ETH.begin();

  // No Wifi, but ESP-Now on channel x
    // WiFi-Init
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE));
  
    // ESP-NOW-Init
    ESP_ERROR_CHECK(esp_now_init());
    esp_now_register_recv_cb(on_data_recv);
    esp_now_register_send_cb(on_data_sent);
  
    // Peer-Info konfigurieren (Broadcast)
    esp_now_peer_info_t peer = {
        .peer_addr = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
        .channel = ESPNOW_CHANNEL,
        .encrypt = false
    };
    ESP_ERROR_CHECK(esp_now_add_peer(&peer));
  
    wifi_phy_rate_t wifi_rate = WIFI_PHY_RATE_48M; // WIFI_PHY_RATE_MCS5_SGI;
   // wifi_rate = WIFI_PHY_RATE_LORA_250K;
   // wifi_rate = WIFI_PHY_RATE_1M_L;
    wifi_rate = WIFI_PHY_RATE_24M;
   // wifi_rate = WIFI_PHY_RATE_MCS5_LGI;
   
    esp_wifi_set_max_tx_power(44); // 11 dbm
    
    esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B|WIFI_PROTOCOL_11G|WIFI_PROTOCOL_11N /* |WIFI_PROTOCOL_LR */);
    #if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
    esp_now_rate_config_t en_rateconfig = {
      .phymode = WIFI_PHY_MODE_HT20, // WIFI_PHY_MODE_11G,
      .rate = WIFI_PHY_RATE_MCS0_LGI, //WIFI_PHY_RATE_MCS1_LGI, // WIFI_PHY_RATE_MCS4_SGI,
      .ersu = false,   
      .dcm = false,
    };
    esp_now_set_peer_rate_config(peer.peer_addr, &en_rateconfig);
    #else
    esp_wifi_config_espnow_rate(WIFI_IF_STA,  wifi_rate);
    #endif

  /* OLD:
  WiFi.mode(WIFI_MODE_STA);
  WiFi.disconnect(false, true);
  quickEspNow.onDataRcvd(espNowdataReceived);
  quickEspNow.setWiFiBandwidth (WIFI_IF_STA, WIFI_BW_HT20); // Only needed for ESP32 in case you need coexistence with ESP8266 in the same network
  quickEspNow.begin(ESPNOW_CHANNEL); // If you use no connected WiFi channel needs to be specified
  
  wifi_phy_rate_t wifi_rate = WIFI_PHY_RATE_48M; // WIFI_PHY_RATE_MCS5_SGI;
 // wifi_rate = WIFI_PHY_RATE_LORA_250K;
 // wifi_rate = WIFI_PHY_RATE_1M_L;
  wifi_rate = WIFI_PHY_RATE_24M;
 // wifi_rate = WIFI_PHY_RATE_MCS5_LGI;
 
  esp_wifi_set_max_tx_power(60);

 // Serial.println(esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B|WIFI_PROTOCOL_11G|WIFI_PROTOCOL_11N|WIFI_PROTOCOL_LR));
  LL_Log.println(esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B|WIFI_PROTOCOL_11G|WIFI_PROTOCOL_11N ));
  
  #if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  // Peer-Info konfigurieren (Broadcast)
  esp_now_peer_info_t peer = {
      .peer_addr = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
      .channel = ESPNOW_CHANNEL,
      .encrypt = false
  };
  ESP_ERROR_CHECK(esp_now_add_peer(&peer));
  esp_now_rate_config_t en_rateconfig = {
    .phymode = WIFI_PHY_MODE_HT20, // WIFI_PHY_MODE_11G,
    .rate = WIFI_PHY_RATE_MCS4_LGI, // WIFI_PHY_RATE_MCS1_LGI, // WIFI_PHY_RATE_MCS4_SGI,
    .ersu = false,   
    .dcm = false,
  };
  esp_now_set_peer_rate_config(peer.peer_addr, &en_rateconfig);
  #else
  LL_Log.println(esp_wifi_config_espnow_rate(WIFI_IF_STA,  wifi_rate ));
  #endif
  */

  // Port defaults to 3232
  // ArduinoOTA.setPort(3232);

  // Hostname defaults to esp3232-[MAC]
  // ArduinoOTA.setHostname(lan_hostname);

  // No authentication by default
  ArduinoOTA.setPassword(upload_auth);

  // Password can be set with it's md5 value as well
  // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
  // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");
  ArduinoOTA
        .onStart([]() {
            char updatetype[16];
            if (ArduinoOTA.getCommand() == U_FLASH)
                strcpy(updatetype, "sketch");
            else // U_SPIFFS
                strcpy(updatetype, "filesystem");

            // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
            LL_Log.printf("Start updating %s...\r\n", updatetype);
            LL_Log.update(eth_connected?1:0); 
            ota_in_progress = 1;
        })
        .onEnd([]() {
            Serial.println("\nEnd");
            ota_in_progress = 0;
        })
        .onProgress([](unsigned int progress, unsigned int total) {
            esp_task_wdt_reset();
            //Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
        })
        .onError([](ota_error_t error) {
            Serial.printf("Error[%u]: ", error);
            if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
            else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
            else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
            else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
            else if (error == OTA_END_ERROR) Serial.println("End Failed");
        });

    ArduinoOTA.begin();

    sntp_set_time_sync_notification_cb( timeavailable );
    configTzTime(TIME_ZONE, NTP_SERVER_POOL);
    sntp_set_sync_interval(120*60UL*1000UL);

    if (!MDNS.begin(lan_hostname)) {
        Serial.println("Error setting up MDNS responder!");
    }
    MDNS.addService("debug", "tcp", 2222);
    MDNS.addService("http", "tcp", 80);
    MDNS.setInstanceName("door-fs-webserver");

    ws.onEvent(onEvent);
    web_server.addHandler(&ws);

    // Route for root / web page
    //web_server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
      // request->send_P(200, "text/html", index_html, processor);
    //  request->send(LittleFS, "/www/door.htm");
    //});

    web_server.serveStatic("/", LittleFS, "/www/").setDefaultFile("door.htm"); // .setTemplateProcessor(processor);

    // Start server

    // Enable ACE FS file web editor and add FS info callback function
    //web_server.init(); 
    web_server.begin();

    #if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
      // If the TWDT was not initialized automatically on startup, manually intialize it now
      esp_task_wdt_config_t twdt_config = {
        .timeout_ms = (WDT_TIMEOUT * 1000UL),
        .idle_core_mask = (1 << CONFIG_FREERTOS_NUMBER_OF_CORES) - 1,    // Bitmask of all cores
        .trigger_panic = true,
      };
      ESP_ERROR_CHECK(esp_task_wdt_reconfigure(&twdt_config));
      ESP_ERROR_CHECK(esp_task_wdt_add(NULL)); //add current thread to WDT watch
      ESP_ERROR_CHECK(esp_task_wdt_reset());
    #else
      esp_task_wdt_init(WDT_TIMEOUT, true); //enable panic so ESP32 restarts
    #endif
    

}

void lanPrintInfo() {
  LL_Log.printf("Connected to %s in channel %d\n", WiFi.SSID ().c_str (), WiFi.channel ());
  LL_Log.printf("IP address: %s\n", WiFi.localIP ().toString ().c_str ());
  LL_Log.printf("MAC address: %s\n", WiFi.macAddress ().c_str ());
  LL_Log.printf("LAN IP address: %s\n", ETH.localIP ().toString ().c_str ());
  LL_Log.printf("Gateway IP address: %s\n", ETH.gatewayIP().toString().c_str());
  // unsafe if uncommented!  LL_Log.printf("Upload auth: %s\n", upload_auth);
}

// System is not safe anymore until reset when this is called as Web File Editor is enabled
void lanEnableWebFileEditor() {
  web_server.init(); 
}

elapsedMillis pingTimer = 0;
int pingFailCount = 0;

void lanHandle() {
  ArduinoOTA.handle();
  ws.cleanupClients();

  // All power to firmware-update once triggered
  if(ota_in_progress) {
    for(int i=0; i<10; i++) {
      ArduinoOTA.handle();
      delay(1);
      yield();
    }
  }

  if(pingTimer > 10000) {
    pingTimer = 0;

    if(lanPingStatus() >= 20) {
      pingFailCount = 0;
    } else if(lanPingStatus() >= 10) {
      LL_Log.printf("Ping failed: %s\n", pingResultString);
      pingFailCount++;
    } 
    if((lanPingStatus() <= 0) || (lanPingStatus() >= 10)) {
      lanPingStart();
    }

    // workaround to keep EspNow going...
    lanEspNowTx((const uint8_t*)"dpng\0", 5);
  }

  if(pingFailCount < 3) esp_task_wdt_reset();


}