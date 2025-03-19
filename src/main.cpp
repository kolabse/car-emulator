#define PROJECT_NAME "Car emulator"

#include <Arduino.h>

#include <GyverDBFile.h>
#include <GTimer.h>
#include <SettingsGyver.h>
#include <LittleFS.h>
#include <WiFiConnector.h>

#include <SPI.h>
#include <mcp2515.h>

enum MessageIds {
    x036, //
    x0B6,
    x0E6,
    x0F6,
    x128,
    x1D0,
    Count
};

struct can_frame canMsg[Count];
struct MCP2515 mcp2515(15);

DB_KEYS(
    kk,
    wifi_ssid,
    wifi_pass,
    close_ap
);

struct State {
    bool egnRnn = false;
    __u8 otdrTemp = 0;
};


GyverDBFile db(&LittleFS, "/data.db");
SettingsGyver sett(PROJECT_NAME, &db);
State state;

GTimerCb<millis> tmr100ms, tmr500ms;

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


// ========== Основной интерфейс ==========
    if (b.beginGroup("Control")) {
        sets::GuestAccess g(b);

        if (b.Switch("Запуск двигателя", &state.egnRnn)) {
            // обработка изменения значения
            // Формула Значение параметра в CAN-сообщении = Значение параметра ? 0x01 : 0x00
            canMsg[MessageIds::x036].data[4] = state.egnRnn ? 0x91 : 0x92;
        }

        if (b.Number("Наружная температура", &state.otdrTemp, -40, 85)) {
            // обработка изменения значения 
            // Формула Значение параметра в CAN-сообщении = (Значение параметра + 40) * 2

            canMsg[MessageIds::x0F6].data[6] = (state.otdrTemp + 40) * 2;
        }
        b.endGroup();
    }

    if (b.Button("Перезагрузить")) {
        ESP.restart();
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

// ========== Обработчики таймеров ==========

void tmr100() {
    mcp2515.sendMessage(&canMsg[MessageIds::x036]);
}

void tmr500() {
    mcp2515.sendMessage(&canMsg[MessageIds::x0F6]);
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

    Serial.println(db[kk::wifi_ssid]);

// ========== Начальные значения ==========    
    canMsg[MessageIds::x036] = {0x036, 8, {0xE, 0, 0, 0, 0x92, 0, 0, 0xA0}};
    canMsg[MessageIds::x0F6] = {0x0F6, 8, {0, 0, 0, 0, 0, 0, (__u8)((state.otdrTemp + 40) * 2), 0}};

// ========== Инициализация таймеров ==========
    tmr100ms.startInterval(100, tmr100);
    tmr500ms.startInterval(500, tmr500);

}

void loop() {
    sett_loop();

    tmr100ms.tick();
    tmr500ms.tick();

}
