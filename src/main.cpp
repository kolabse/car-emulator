#define PROJECT_NAME "Car emulator"

#include <Arduino.h>

#include <GyverDBFile.h>
#include <SettingsGyver.h>
#include <LittleFS.h>
#include <WiFiConnector.h>

#include <SPI.h>
#include <mcp2515.h>


struct can_frame canMsg;
struct MCP2515 mcp2515(15);

enum class MesageIds {
    x036,
    x0B6,
    x0E6,
    x0F6,
    x128,
    x1D0,
    Count
};

DB_KEYS(
    kk,
    wifi_ssid,
    wifi_pass,
    close_ap
);

struct State {
    bool egnRnn = false;
};


GyverDBFile db(&LittleFS, "/data.db");
SettingsGyver sett(PROJECT_NAME, &db);
State state;

unsigned long previousMillis = 0;
const long interval = 99; 


// ========== build ==========
static void build(sets::Builder& b) {
    {   // WiFi settings, admin access only
        sets::Group g(b, "WiFi");
        b.Input(kk::wifi_ssid, "SSID");
        b.Pass(kk::wifi_pass, "Pass", "");
        if (b.Switch(kk::close_ap, "Закрывать AP")) {
            WiFiConnector.closeAP(db[kk::close_ap]);
        }
        if (b.Button("Подключить")) {
            WiFiConnector.connect(db[kk::wifi_ssid], db[kk::wifi_pass]);
        }
    }

    if (b.beginGroup("Control")) {
        sets::GuestAccess g(b);

        b.Switch("Запуск двигателя", &state.egnRnn);

        if (b.Button("Перезагрузить")) {
            ESP.restart();
        }
        b.endGroup();
    }
}

// ========== update ==========
static void update(sets::Updater& u) {
}

// ========== begin ==========
void sett_begin() {
    // fs
#ifdef ESP32
    LittleFS.begin(true);
#else
    LittleFS.begin();
#endif

    // database
    db.begin();
    db.init(kk::wifi_ssid, "");
    db.init(kk::wifi_pass, "");
    db.init(kk::close_ap, true);

    // wifi
    WiFiConnector.onConnect([]() {
        Serial.print("Connected: ");
        Serial.println(WiFi.localIP());
    });
    WiFiConnector.onError([]() {
        Serial.print("Error. Start AP: ");
        Serial.println(WiFi.softAPIP());
    });

    WiFiConnector.setName(PROJECT_NAME);
    WiFiConnector.closeAP(db[kk::close_ap]);
    WiFiConnector.connect(db[kk::wifi_ssid], db[kk::wifi_pass]);

    // settings
    sett.begin();
    sett.onBuild(build);
    sett.onUpdate(update);
}

// ========== loop ==========
void sett_loop() {
    WiFiConnector.tick();
    sett.tick();
}


void setup() {
    Serial.begin(115200);
    Serial.println();
    SPI.begin();
    mcp2515.reset();
    mcp2515.setBitrate(CAN_125KBPS, MCP_8MHZ);
    mcp2515.setNormalMode();

    sett.setPass("1234");

    sett_begin();

    // из settings.h доступны db и ключи
    Serial.println(db[kk::wifi_ssid]);

    canMsg.can_id = 0x036;
    canMsg.can_dlc = 8;
    canMsg.data[0] = 0x00;
    canMsg.data[1] = 0x00;
    canMsg.data[2] = 0x00;
    canMsg.data[3] = 0x00;
    canMsg.data[4] = 0x00;
    canMsg.data[5] = 0x00;
    canMsg.data[6] = 0x00;
    canMsg.data[7] = 0x00;


}

void loop() {
    sett_loop();

    unsigned long currentMillis = millis();

    if (currentMillis - previousMillis >= interval) {
        canMsg.data[4] = state.egnRnn ? 0x01 : 0x00;

        mcp2515.sendMessage(&canMsg);
        Serial.print("Message sent: ");
        Serial.print(canMsg.data[4],HEX);
        Serial.println();

        previousMillis = currentMillis;
    }
}
