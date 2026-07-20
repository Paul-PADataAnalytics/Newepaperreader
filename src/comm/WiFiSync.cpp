#ifndef NATIVE_TESTING

#include "WiFiSync.h"
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <SD.h>
#include <SPI.h>

static AsyncWebServer server(80);
static bool isSyncRunning = false;

void startWiFiSync() {
    if (isSyncRunning) return;
    
    Serial.println("Starting WiFi SoftAP: EPD-Reader");
    WiFi.softAP("EPD-Reader");
    
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        String html = "<html><body>";
        html += "<h1>EPD-Reader WiFi Sync</h1>";
        html += "<form method='POST' action='/upload' enctype='multipart/form-data'>";
        html += "<input type='file' name='file'>";
        html += "<input type='submit' value='Upload'>";
        html += "</form>";
        html += "</body></html>";
        request->send(200, "text/html", html);
    });

    server.on("/upload", HTTP_POST, [](AsyncWebServerRequest *request){
        request->send(200, "text/plain", "File uploaded successfully. Please reboot or refresh library.");
    }, [](AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final) {
        if(!index){
            String path = "/sd/books/" + filename;
            Serial.printf("Upload Start: %s\n", path.c_str());
            // Open the file on first call
            request->_tempFile = SD.open(path, FILE_WRITE);
        }
        if(request->_tempFile){
            if(len){
                request->_tempFile.write(data, len);
            }
            if(final){
                request->_tempFile.close();
                Serial.printf("Upload End: %s, %u B\n", filename.c_str(), index+len);
            }
        }
    });

    server.begin();
    isSyncRunning = true;
    Serial.println("WiFi Sync Server Started");
}

void stopWiFiSync() {
    if (!isSyncRunning) return;
    server.end();
    WiFi.softAPdisconnect(true);
    isSyncRunning = false;
    Serial.println("WiFi Sync Server Stopped");
}

void processWiFiSync() {
    // Async server handles things in background
}

#endif
