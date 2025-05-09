#include "AsyncFsWebServer.h"

bool AsyncFsWebServer::init() {

    m_filesystem_ok = true;


    //////////////////////    BUILT-IN HANDLERS    ////////////////////////////
    using namespace std::placeholders;

    using namespace std::placeholders;
    on("/status", HTTP_GET, std::bind(&AsyncFsWebServer::handleFsStatus, this, _1));
    on("/list", HTTP_GET, std::bind(&AsyncFsWebServer::handleFileList, this, _1));
    on("/edit", HTTP_PUT, std::bind(&AsyncFsWebServer::handleFileCreate, this, _1));
    on("/edit", HTTP_DELETE, std::bind(&AsyncFsWebServer::handleFileDelete, this, _1));
    on("/edit", HTTP_GET, std::bind(&AsyncFsWebServer::handleFileEdit, this, _1));
    on("/edit", HTTP_POST,
        std::bind(&AsyncFsWebServer::sendOK, this, _1),
        std::bind(&AsyncFsWebServer::handleUpload, this, _1, _2, _3, _4, _5, _6)
    );

    //on("/favicon.ico", HTTP_GET, std::bind(&AsyncFsWebServer::sendOK, this, _1));

    on("/getStatus", HTTP_GET, std::bind(&AsyncFsWebServer::getStatus, this, _1));

    on("*", HTTP_HEAD, std::bind(&AsyncFsWebServer::handleFileName, this, _1));

    on("/upload", HTTP_POST,
        std::bind(&AsyncFsWebServer::sendOK, this, _1),
        std::bind(&AsyncFsWebServer::handleUpload, this, _1, _2, _3, _4, _5, _6)
    );

    on("/reset", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/plain", WiFi.localIP().toString());
        delay(500);
        ESP.restart();
    });

    onNotFound( std::bind(&AsyncFsWebServer::notFound, this, _1));
    serveStatic("/", *m_filesystem, "/").setDefaultFile("index.htm");

    //begin();

    return true;
}

void AsyncFsWebServer::printFileList(fs::FS &fs, const char * dirname, uint8_t levels) {
    Serial.printf("\nListing directory: %s\n", dirname);
    File root = fs.open(dirname, "r");
    if (!root) {
        Serial.println("- failed to open directory");
        return;
    }
    if (!root.isDirectory()) {
        Serial.println(" - not a directory");
        return;
    }
    File file = root.openNextFile();
    while (file) {
        if (file.isDirectory()) {
        if (levels) {
            printFileList(fs, file.path(), levels - 1);
        }
        } else {
        Serial.printf("|__ FILE: %s (%d bytes)\n",file.name(), file.size());
        }
        file = root.openNextFile();
    }
}


void AsyncFsWebServer::setTaskWdt(uint32_t timeout) {
    #if ESP_ARDUINO_VERSION_MAJOR > 2
    esp_task_wdt_config_t twdt_config = {
        .timeout_ms = timeout,
        .idle_core_mask = (1 << portNUM_PROCESSORS) - 1,    // Bitmask of all cores
        .trigger_panic = false,
    };
    ESP_ERROR_CHECK(esp_task_wdt_reconfigure(&twdt_config));
    #else
    ESP_ERROR_CHECK(esp_task_wdt_init(timeout/1000, 0));
    #endif
}

void AsyncFsWebServer::setAuthentication(const char* user, const char* pswd) {
    m_pageUser = (char*) malloc(strlen(user)*sizeof(char));
    m_pagePswd = (char*) malloc(strlen(pswd)*sizeof(char));
    strcpy(m_pageUser, user);
    strcpy(m_pagePswd, pswd);
}

void AsyncFsWebServer::handleFileName(AsyncWebServerRequest *request) {
    if (m_filesystem->exists(request->url()))
        request->send(301, "text/plain", "OK");
    request->send(404, "text/plain", "File not found");
}

void AsyncFsWebServer::sendOK(AsyncWebServerRequest *request) {
  request->send(200, "text/plain", "OK");
}

void AsyncFsWebServer::notFound(AsyncWebServerRequest *request) {
    request->send(404, "text/plain", "Not found");
    log_debug("Resource %s not found\n", request->url().c_str());
}

void AsyncFsWebServer::getStatus(AsyncWebServerRequest *request) {
    String reply = "{\"type\":\"LittleFS\", \"isOk\":\"true\", \"ssid\":\"";
        reply += WiFi.SSID();
        reply += "\", \"rssi\":";
        reply += WiFi.RSSI();
        reply += "}";
    request->send(200, "application/json", reply);
}


bool AsyncFsWebServer::createDirFromPath(const String& path) {
    String dir;
    int p1 = 0;  int p2 = 0;
    while (p2 != -1) {
        p2 = path.indexOf("/", p1 + 1);
        dir += path.substring(p1, p2);
        // Check if its a valid dir
        if (dir.indexOf(".") == -1) {
            if (!m_filesystem->exists(dir)) {
                if (m_filesystem->mkdir(dir)) {
                    log_info("Folder %s created\n", dir.c_str());
                } else {
                    log_info("Error. Folder %s not created\n", dir.c_str());
                    return false;
                }
            }
        }
        p1 = p2;
    }
    return true;
}

void AsyncFsWebServer::handleUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {

    // DebugPrintln("Handle upload POST");
    if (!index) {
        // Increase task WDT timeout
        setTaskWdt(m_timeout);

        // Create folder if necessary (up to max 5 sublevels)
        int len = filename.length();
        char path[len+1];
        strcpy(path, filename.c_str());
        createDirFromPath(path);

        // open the file on first call and store the file handle in the request object
        request->_tempFile = m_filesystem->open(filename, "w");
        log_debug("Upload Start: writing file %s", filename.c_str());
    }

    if (len) {
        // stream the incoming chunk to the opened file
        request->_tempFile.write(data, len);
    }

    if (final) {
        // Restore task WDT timeout
        setTaskWdt(8000);
        // close the file handle as the upload is now done
        request->_tempFile.close();
        log_debug("Upload complete: %s, size: %d", filename.c_str(), index + len);
    }
}

// returns edit.htm.gz from header file
void AsyncFsWebServer::handleFileEdit(AsyncWebServerRequest *request) {
    if (m_pageUser != nullptr) {
        if(!request->authenticate(m_pageUser, m_pagePswd))
            return request->requestAuthentication();
    }
    AsyncWebServerResponse *response = request->beginResponse(200, "text/html", (uint8_t*)_acedit_min_htm, sizeof(_acedit_min_htm));
    response->addHeader("Content-Encoding", "gzip");
    request->send(response);
}

/*
    Return the list of files in the directory specified by the "dir" query string parameter.
    Also demonstrates the use of chunked responses.
*/
void AsyncFsWebServer::handleFileList(AsyncWebServerRequest *request)
{
    if (!request->hasArg("dir")) {
        return request->send(400, "DIR ARG MISSING");
    }

    String path = request->arg("dir");
    log_debug("handleFileList: %s", path.c_str());
    if (path != "/" && !m_filesystem->exists(path)) {
        return request->send(400, "BAD PATH");
    }

    File root = m_filesystem->open(path, "r");
    JSON_DOC(1024);
    JsonArray array = doc.to<JsonArray>();
    if (root.isDirectory()) {
        File file = root.openNextFile();
        while (file) {
            #if ARDUINOJSON_VERSION_MAJOR > 6
            JsonObject obj = array.add<JsonObject>();
            #else
            JsonObject obj = array.createNestedObject();
            #endif
            String filename = file.name();
            if (filename.lastIndexOf("/") > -1) {
                filename.remove(0, filename.lastIndexOf("/") + 1);
            }
            obj["type"] = (file.isDirectory()) ? "dir" : "file";
            obj["size"] = file.size();
            obj["name"] = filename;
            file = root.openNextFile();
        }
    }
    String output;
    serializeJson(doc, output);
    request->send(200, "text/json", output);
}

/*
    Handle the creation/rename of a new file
    Operation      | req.responseText
    ---------------+--------------------------------------------------------------
    Create file    | parent of created file
    Create folder  | parent of created folder
    Rename file    | parent of source file
    Move file      | parent of source file, or remaining ancestor
    Rename folder  | parent of source folder
    Move folder    | parent of source folder, or remaining ancestor
*/
void AsyncFsWebServer::handleFileCreate(AsyncWebServerRequest *request)
{
    String path = request->arg("path");
    if (path.isEmpty()) {
        return request->send(400, "PATH ARG MISSING");
    }
    if (path == "/") {
        return request->send(400, "BAD PATH");
    }

    String src = request->arg("src");
    if (src.isEmpty())  {
        // No source specified: creation
        log_debug("handleFileCreate: %s\n", path.c_str());
        if (path.endsWith("/")) {
            // Create a folder
            path.remove(path.length() - 1);
            if (!m_filesystem->mkdir(path)) {
                return request->send(500, "MKDIR FAILED");
            }
        }
        else  {
            // Create a file
            File file = m_filesystem->open(path, "w");
            if (file) {
                file.write(0);
                file.close();
            }
            else {
                return request->send(500, "CREATE FAILED");
            }
        }
        request->send(200,  path.c_str());
    }
    else  {
        // Source specified: rename
        if (src == "/") {
            return request->send(400, "BAD SRC");
        }
        if (!m_filesystem->exists(src)) {
            return request->send(400,  "FILE NOT FOUND");
        }

        log_debug("handleFileCreate: %s from %s\n", path.c_str(), src.c_str());
        if (path.endsWith("/")) {
            path.remove(path.length() - 1);
        }
        if (src.endsWith("/")) {
            src.remove(src.length() - 1);
        }
        if (!m_filesystem->rename(src, path))  {
            return request->send(500, "RENAME FAILED");
        }
        sendOK(request);
    }
}

/*
    Handle a file deletion request
    Operation      | req.responseText
    ---------------+--------------------------------------------------------------
    Delete file    | parent of deleted file, or remaining ancestor
    Delete folder  | parent of deleted folder, or remaining ancestor
*/
void AsyncFsWebServer::deleteContent(String& path) {
  File file = m_filesystem->open(path.c_str(), "r");
  if (!file.isDirectory()) {
    file.close();
    m_filesystem->remove(path.c_str());
    log_info("File %s deleted", path.c_str());
    return;
  }

  file.rewindDirectory();
  while (true) {
    File entry = file.openNextFile();
    if (!entry) {
      break;
    }
    String entryPath = path + "/" + entry.name();
    if (entry.isDirectory()) {
      entry.close();
      deleteContent(entryPath);
    }
    else {
      entry.close();
      m_filesystem->remove(entryPath.c_str());
      log_info("File %s deleted", path.c_str());
    }
    yield();
  }
  m_filesystem->rmdir(path.c_str());
  log_info("Folder %s removed", path.c_str());
  file.close();
}



void AsyncFsWebServer::handleFileDelete(AsyncWebServerRequest *request) {
    String path = request->arg((size_t)0);
    if (path.isEmpty() || path == "/")  {
        return request->send(400, "BAD PATH");
    }
    if (!m_filesystem->exists(path))  {
        return request->send(400, "File Not Found");
    }
    deleteContent(path);
    sendOK(request);
}

/*
    Return the FS type, status and size info
*/
void AsyncFsWebServer::handleFsStatus(AsyncWebServerRequest *request) {
    log_debug("handleStatus");
    String json;
    json = "{\"type\":\"";
    json += "LittleFS";
    json += "\", \"isOk\":";
    if (m_filesystem_ok)  {
        IPAddress ip = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP() : WiFi.softAPIP();
        json += PSTR("\"true\", \"totalBytes\":\"");
        json += LittleFS.totalBytes();
        json += PSTR("\", \"usedBytes\":\"");
        json += LittleFS.usedBytes();
        json += PSTR("\", \"mode\":\"");
        json += WiFi.status() == WL_CONNECTED ? "Station" : "Access Point";
        json += PSTR("\", \"ssid\":\"");
        json += WiFi.SSID();
        json += PSTR("\", \"ip\":\"");
        json += ip.toString();
        json += "\"";
    }
    else
        json += "\"false\"";
    json += PSTR(",\"unsupportedFiles\":\"\"}");
    request->send(200, "application/json", json);
}


