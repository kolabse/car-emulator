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
    x036, // BSI Ignition, Dashboard lightning
    x0B6, // Tachometer, Actual speed, Odometer from start, Fuel consumtion
    x0E6, // Wheels rotation, voltage
    x0F6, // Ignition, Coolant temperature, Odometer, External temperature, Reverse gear, Turn signals
    x122, // Universal multiplexed panel (Multimedia control)
    x128, // Dashboard lights
    x1D0, // Climate control information
    x21F, // Radio remote control under the steering wheel
    x220, // Door status
    x221, // Trip computer
    x260, // ****, Language
    x261, // Average speed, Fuel consumption
    Count
};

struct can_frame canMsg[MessageIds::Count];
struct MCP2515 mcp2515(15);

DB_KEYS(
    kk,
    wifi_ssid,
    wifi_pass,
    close_ap
);

struct State {
    bool egnRnn = false;
    bool econMode = false;
    bool parkBrake = false;
    int8_t otdrTemp = 0;
};


GyverDBFile db(&LittleFS, "/data.db");
SettingsGyver sett(PROJECT_NAME, &db);
State state;

GTimerCb<millis> tmr100ms, tmr500ms;


__u8 outdorTempToCan(int8_t temp) {
    return (temp + 40) * 2;
}

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
    if (b.beginGroup("Общие")) {
        sets::GuestAccess g(b);

        if (b.Switch("Зажигание", &state.egnRnn)) {
            // обработка изменения значения
            // Формула Значение параметра в CAN-сообщении = Значение параметра ? 0x91 : 0x92
            canMsg[MessageIds::x036].data[4] = state.egnRnn ? 0x91 : 0x92;
            canMsg[MessageIds::x0F6].data[0] = state.egnRnn ? 0x8E : 0x86;
        }

        if (b.Switch("Экономичный режим", &state.econMode)) {
            // обработка изменения значения
            // Формула Значение параметра в CAN-сообщении = Значение параметра ? 0x80 : 0x00
            canMsg[MessageIds::x036].data[2] = state.econMode ? 0x80 : 0x00;
        }
        if (b.beginRow("Наружная температура")) {

            if (b.Button("UP")) {
                state.otdrTemp++;
                if (state.otdrTemp > 85) state.otdrTemp = 85;
                canMsg[MessageIds::x0F6].data[6] = outdorTempToCan(state.otdrTemp);
            }

            if (b.Number("otdrT"_h, "", &state.otdrTemp, -40, 85)) {
                // обработка изменения значения 
                // Формула Значение параметра в CAN-сообщении = (Значение параметра + 40) * 2
                canMsg[MessageIds::x0F6].data[6] = outdorTempToCan(state.otdrTemp);
            }

            if (b.Button("DOWN")) {
                state.otdrTemp--;
                if (state.otdrTemp < -40) state.otdrTemp = -40;
                canMsg[MessageIds::x0F6].data[6] = outdorTempToCan(state.otdrTemp);
            }


            b.endRow();
        }
    

        b.endGroup();
    }

    if (b.beginGroup("Панель приборов")) {
        sets::GuestAccess g(b);
        // Температура охлаждающей жидкости
        // Обороты двигателя
        // Текущая скорость
        // Пробег с момента запуска
        // Расход топлива
        // Ремень безопасности водителя
        // Стояночный тормоз
        // Открыта одна из дверей
        // Габариты
        // Ближний свет
        // Дальний свет
        // Передние противотуманные фары
        // Задние противотуманные фары
        // Поворотник правый
        // Поворотник левый
        // Низкий уровень топлива
        // Ремни безопасности, мигающий сигнал
        // Ремни безопасности
        // Яркость подсветки приборной панели

        b.endGroup();
    }

    if (b.beginGroup("Освещение")) {
        sets::GuestAccess g(b);
        // Подсветка приборной панели
        // Яркость подсветки приборной панели
        // Задний ход
        // Поворотники
        b.endGroup();
    }

    if (b.beginGroup("Мультимедиа")) {
        sets::GuestAccess g(b);

        b.endGroup();
    }

    if (b.beginGroup("Климат-контроль")) {
        sets::GuestAccess g(b);

        b.endGroup();
    }

    if (b.beginGroup("Двери")) {
        sets::GuestAccess g(b);

        b.endGroup();
    }

    if (b.beginGroup("Освещение")) {
        sets::GuestAccess g(b);

        b.endGroup();
    }

    if (b.Button("Перезагрузить")) {
        ESP.restart();
    }
}

// ========== update ==========
static void update(sets::Updater& upd) {
    upd.update("otdrT"_h, state.otdrTemp);
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
    canMsg[MessageIds::x036] = {0x036, 8, {0xE, 0, 0, 0x2F, 0x92, 0, 0, 0xA0}};
    canMsg[MessageIds::x0F6] = {0x0F6, 8, {0x86, 0, 0, 0, 0, 0, (__u8)((state.otdrTemp + 40) * 2), 0}};

// ========== Инициализация таймеров ==========
    tmr100ms.startInterval(100, tmr100);
    tmr500ms.startInterval(500, tmr500);

}

void loop() {
    sett_loop();

    tmr100ms.tick();
    tmr500ms.tick();

}
