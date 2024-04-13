#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFiMulti.h>
#include <WebSocketsClient.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "LiquidCrystal_I2C.h"

SemaphoreHandle_t binarySemaphore = xSemaphoreCreateBinary();

WiFiMulti WiFiMulti;
WebSocketsClient webSocket;

char deviceId[37];
constexpr int pingInterval = 3000;
int lcdColumns = 16;
int lcdRows = 2;

class Roles {
public:
    static constexpr const char* master = "master";
    static constexpr const char* slave = "slave";
};




Roles roles;

namespace JSON {
    class serializer {
    public:
        static String serializeRequestDataRegister(const char* type, const char* role, const char* name);
        static String serializePingRequestData(const char* role, const char* deviceid);
        static String serializeRequestDataSensor(const char* type, int distance);
    };
    class deserializer {
    public:
        static JsonDocument deserializeData(const char* input);
    };
}

JsonDocument JSON::deserializer::deserializeData(const char* input) {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, input);
    if (error) {
        Serial.print("deserializeJson() failed: ");
        Serial.println(error.c_str());
    }
    return doc;
};

String JSON::serializer::serializeRequestDataRegister(const char* type, const char * role, const char* name) {
    JsonDocument doc;
    String JSONData;
    doc["type"] = type;
    doc["role"] = role;
    doc["name"] = name;

    serializeJson(doc, JSONData);
    return JSONData;
}

String JSON::serializer::serializeRequestDataSensor(const char* type, const int distance) {
    JsonDocument doc;
    String JSONData;
    doc["type"] = type;
    doc["distance"] = distance;

    serializeJson(doc, JSONData);
    return JSONData;
}

String JSON::serializer::serializePingRequestData(const char* role, const char* deviceid) {
    JsonDocument doc;
    String JSONData;
    doc["type"] = "ping";
    doc["id"] = deviceid;

    serializeJson(doc, JSONData);
    return JSONData;
}

LiquidCrystal_I2C lcd(0x27, lcdColumns, lcdRows);


//CLIENT & SERVER FUNCTIONALITY

[[noreturn]] void sendToWebsocket(void *parameter) {
    while (true) {
        if (webSocket.isConnected()) {
            if (strlen(deviceId) > 35) {
                String pingPayload = JSON::serializer::serializePingRequestData(roles.master, deviceId);
                webSocket.sendTXT(pingPayload);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(pingInterval));
    }
}


// SERVER & CLIENT FUNCTIONALITY

void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
    switch(type) {
        case WStype_DISCONNECTED:
            Serial.printf("[WS] Disconnected!\n");
            break;
        case WStype_CONNECTED:{
            Serial.printf("[WS] Connected to url: %s\n", payload);
            String registerPayload = JSON::serializer::serializeRequestDataRegister("register", roles.master, "ESP-MASTER");
            webSocket.sendTXT(registerPayload);
        }
            break;
        case WStype_TEXT: {
            const char* Payload = (char *) payload;
            JsonDocument payloadData = JSON::deserializer::deserializeData(Payload);
            if (payloadData["type"]) {
                if (strcmp(payloadData["type"], "PONG") == 0) {
                    Serial.printf("[WS]: Ping/Pong Frame Event: %s\n", payloadData["type"].as<const char*>());
                } else if (strcmp(payloadData["type"], "REGISTER") == 0) {
                    if (strcmp(payloadData["actionCode"], "REGISTERED_SUCCESSFULLY") == 0) {
                        char* deviceIdM = static_cast<char *>(malloc(38 * sizeof(payloadData["id"].as<const char *>())));
                        if (deviceIdM != nullptr) {
                            strcpy(deviceId, payloadData["id"].as<const char *>());
                            free(deviceIdM);
                        }
//                        snprintf(deviceId, sizeof deviceId, "%s", payloadData["id"].as<const char*>());
                        Serial.printf("[REGISTER]: Registered as %s with ID %s\n",payloadData["role"].as<const char *>(), payloadData["id"].as<const char *>());
                    } else if (strcmp(payloadData["actionCode"], "REGISTERED_ALREADY") == 0) {
                        Serial.printf("[REGISTER]: Already registered as master.\n");
                    } else if (strcmp(payloadData["actionCode"], "DEAD") == 0) {
                        Serial.printf("[REGISTER]: Client deemed dead, reconnecting.\n");
                    }
                } else if (strcmp(payloadData["type"], "RECEIVE_SIGNAL") == 0) {
                    const char* sensorName = payloadData["detectedSensorName"].as<const char*>();
                    const char* sensorId = payloadData["sensorId"].as<const char*>();
                    int distance = payloadData["distance"].as<int>();
                    if (sensorName && sensorId && distance) {
                        if (distance <= 10) {
                            Serial.printf("[MASTER] [%s] [%s]: Detected object\n", sensorName, sensorId);
                            lcd.setCursor(0, 0);
                            lcd.print(" STATUS ");
                            lcd.setCursor(0, 1);
                            lcd.print("> DETECTED");
                        } else {
                            lcd.setCursor(0, 0);
                            lcd.print(" STATUS ");
                            lcd.setCursor(0, 1);
                            lcd.print("> UNDETECTED");
                        }
                    }

                }
            }
        }
            break;
        case WStype_BIN:
            break;
        case WStype_ERROR:
            Serial.println("ERROR");
            break;
        case WStype_FRAGMENT_TEXT_START:
        case WStype_FRAGMENT_BIN_START:
        case WStype_FRAGMENT:
        case WStype_FRAGMENT_FIN:
            break;
        case WStype_PING:
            break;
        case WStype_PONG:
            break;
    }

}



void setup(){
    Serial.begin(9600);
    lcd.init();
    lcd.backlight();
    WiFiMulti.addAP("ssid", "pass");

    while(WiFiMulti.run() != WL_CONNECTED) {
        delay(100);
    }
    Serial.println("Connected");


    webSocket.begin("host", 4040, "/");
    webSocket.onEvent(webSocketEvent);
    webSocket.setReconnectInterval(2000);
    lcd.setCursor(0, 0);
    lcd.print("> Set up");
    xTaskCreate(&sendToWebsocket, "Websocket", 5000, nullptr, 1, nullptr);
}
void loop(){
    webSocket.loop();
}