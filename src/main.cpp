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

struct Emulator {
    bool common = false;
    bool dashboard = false;
    bool climate = false;
    bool lighting = false;
    bool multimedia = false;
    bool doors = false;
};

struct Doors {
    bool frontLeft = false;
    bool frontRight = false;
    bool rearLeft = false;
    bool rearRight = false;
    bool trunk = false;
};

struct State {
    bool egnRnn = false;        // Зажигание
    bool econMode = false;      // Экономичный режим
    bool parkBrake = false;     // Стояночный тормоз
    uint8_t brightness = 0;     // Яркость подсветки приборной панели
    int8_t otdrTemp = 0;        // Наружная температура
    int8_t clntTemp = 0;        // Температура охлаждающей жидкости
    int8_t speed = 0;           // Текущая скорость
    int16_t rpm = 0;            // Обороты двигателя
    uint32_t mileage = 0;       // Пробег с момента запуска
    float voltage = 0;          // Напряжение
    Doors doors;
};


GyverDBFile db(&LittleFS, "/data.db");
SettingsGyver sett(PROJECT_NAME, &db);
State state;
Emulator emulator;

GTimerCb<millis> tmr50ms, tmr100ms, tmr200ms, tmr500ms, tmr1000ms;

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
        if (b.Switch("Отправка", &emulator.common)) {
            // Включение/выключение всех параметров
        }

        if (b.Switch("Зажигание", &state.egnRnn)) {
            // Формула Значение параметра в CAN-сообщении = Значение параметра ? 0x91 : 0x92
            canMsg[MessageIds::x036].data[4] = state.egnRnn ? 0x91 : 0x92;
            canMsg[MessageIds::x0F6].data[0] = state.egnRnn ? 0x8E : 0x86;
        }

        if (b.Switch("Экономичный режим", &state.econMode)) {
            // Формула Значение параметра в CAN-сообщении = Значение параметра ? 0x80 : 0x00
            canMsg[MessageIds::x036].data[2] = state.econMode ? 0x80 : 0x00;
        }

        if (b.Slider("otdrT"_h, "Наружная температура", -40, 85, 1, " °C", &state.otdrTemp)) {
            // Формула Значение параметра в CAN-сообщении = (Значение параметра + 40) * 2
            canMsg[MessageIds::x0F6].data[6] = (state.otdrTemp + 40u) * 2u;
        }

        if (b.Slider("volts"_h, "Напряжение", 10, 15, 0.1, " В", &state.voltage)) {
            // Формула Значение параметра в CAN-сообщении = Значение параметра * 20 - 144
            canMsg[MessageIds::x0E6].data[6] = state.voltage * 20 - 144;
        }
    

        b.endGroup();
    }

    if (b.beginGroup("Устройства")) { 
        sets::GuestAccess g(b);

        {
            sets::Menu m(b, "Панель приборов");
            if (b.beginGroup("Панель приборов")) {
                if (b.Switch("Отправка", &emulator.dashboard)) {
                    // Включение/выключение всех параметров
                }
        
                // Температура охлаждающей жидкости
                if (b.Slider("clntT"_h, "Температура двигателя", -39, 120, 1, " °C", &state.clntTemp)) {
                    // Формула Значение параметра в CAN-сообщении = Значение параметра + 39
                    canMsg[MessageIds::x0F6].data[1] = state.clntTemp + 39;
                }
                // Обороты двигателя
                if (b.Slider("rpm"_h, "Обороты двигателя", 0, 8000, 100, " об/мин", &state.rpm)) {
                    canMsg[MessageIds::x0B6].data[0] = ((state.rpm << 3) & 0xFF00) >> 8;
                    canMsg[MessageIds::x0B6].data[1] = (state.rpm << 3) & 0x00FF;
                    
                }
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
                if (b.ButtonHold()) {
                    // Отправка сообщения
                    bitWrite(canMsg[MessageIds::x128].data[5], 7, b.build.pressed());
                }
                // Передние стеклоочистители
                // Ремни безопасности, мигающий сигнал
                // Ремни безопасности
                // Яркость подсветки приборной панели
                if (b.Slider("brightness"_h, "Яркость подсветки приборной панели", 0, 15, 1, "", &state.brightness)) {
                    canMsg[MessageIds::x036].data[3] = (canMsg[MessageIds::x036].data[3] & 0xF0) | state.brightness;
                    canMsg[MessageIds::x128].data[7] = (canMsg[MessageIds::x128].data[7] & 0xF0) | state.brightness;
                }
        
                b.endGroup();
            }

        }

        {
            sets::Menu m(b, "Освещение");
            if (b.beginGroup("Освещение")) {
                if (b.Switch("Отправка", &emulator.lighting)) {
                    // Включение/выключение всех параметров
                }
                // Подсветка приборной панели
                // Яркость подсветки приборной панели
                // Задний ход
                // Поворотники
                b.endGroup();
            }
        }

        {
            sets::Menu m(b, "Мультимедиа");
            if (b.beginGroup("Мультимедиа")) {
                if (b.Switch("Отправка", &emulator.multimedia)) {
                    // Включение/выключение всех параметров
                }
        
                b.endGroup();
            }
        }

        {
            sets::Menu m(b, "Климат-контроль");
            if (b.beginGroup("Климат-контроль")) {
                if (b.Switch("Отправка", &emulator.climate)) {
                    // Включение/выключение всех параметров
                }

                b.endGroup();
            }
        }

        {
            sets::Menu m(b, "Двери"); 
            if (b.beginGroup("Двери")) {
                if (b.Switch("Отправка", &emulator.doors)) {
                    // Включение/выключение всех параметров
                }
                // Передняя левая дверь
                if(b.Switch("Передняя левая дверь", &state.doors.frontLeft)){
                    bitWrite(canMsg[MessageIds::x220].data[0], 7, state.doors.frontLeft); ;
                }
                // Передняя правая дверь
                if(b.Switch("Передняя правая дверь", &state.doors.frontRight)){
                    bitWrite(canMsg[MessageIds::x220].data[0], 6, state.doors.frontRight); ;
                }
                // Задняя левая дверь
                if(b.Switch("Задняя левая дверь", &state.doors.rearLeft)){
                    bitWrite(canMsg[MessageIds::x220].data[0], 5, state.doors.rearLeft); ;
                }
                // Задняя правая дверь
                if(b.Switch("Задняя правая дверь", &state.doors.rearRight)){
                    bitWrite(canMsg[MessageIds::x220].data[0], 4, state.doors.rearRight); ;
                }
                // Багажник
                if(b.Switch("Багажник", &state.doors.trunk)){
                    bitWrite(canMsg[MessageIds::x220].data[0], 3, state.doors.trunk); ;
                }
                b.endGroup();
            }
        }
        b.endGroup();
    }
    if (b.Button("Перезагрузить")) {
        ESP.restart();
    }
}

// ========== update ==========
static void update(sets::Updater& upd) {
    // upd.update("otdrT"_h, state.otdrTemp);
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

void tmr50() {
    if (emulator.dashboard){
        mcp2515.sendMessage(&canMsg[MessageIds::x0B6]);
    } 
}

void tmr100() {
    if (emulator.common || emulator.dashboard){
        mcp2515.sendMessage(&canMsg[MessageIds::x036]);
    }
}

void tmr200() {
    if (emulator.common){
        mcp2515.sendMessage(&canMsg[MessageIds::x0E6]);
    }
    if (emulator.multimedia){
        mcp2515.sendMessage(&canMsg[MessageIds::x122]);
    }
    if(emulator.dashboard || emulator.lighting){
        mcp2515.sendMessage(&canMsg[MessageIds::x128]);
    }
}

void tmr500() {
    if (emulator.common || emulator.dashboard || emulator.lighting){ 
        mcp2515.sendMessage(&canMsg[MessageIds::x0F6]);
    }
    
    if (emulator.climate){
        mcp2515.sendMessage(&canMsg[MessageIds::x1D0]);
    }
    if (emulator.doors){
        mcp2515.sendMessage(&canMsg[MessageIds::x220]);
    }
    if (emulator.multimedia){
        mcp2515.sendMessage(&canMsg[MessageIds::x260]);
    }
    
}

void tmr1000() {
    if (emulator.dashboard){
        mcp2515.sendMessage(&canMsg[MessageIds::x221]);
    }
    
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
    __u8 _0F6_1 = (__u8)(state.clntTemp + 39);
    __u8 _0F6_6 = (__u8)((state.otdrTemp + 40) * 2);

    canMsg[MessageIds::x036] = {0x036, 8, {0x0E, 0x00, 0x00, 0x30, 0x92, 0x00, 0x00, 0xA0}};
    canMsg[MessageIds::x0B6] = {0x0B6, 8, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};
    canMsg[MessageIds::x0E6] = {0x0E6, 8, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x45}};
    canMsg[MessageIds::x0F6] = {0x0F6, 8, {0x86, _0F6_1, 0x00, 0x00, 0x00, 0x00, _0F6_6, 0x00}};
    canMsg[MessageIds::x122] = {0x122, 8, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};
    canMsg[MessageIds::x128] = {0x128, 8, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};
    canMsg[MessageIds::x1D0] = {0x1D0, 7, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};
    canMsg[MessageIds::x21F] = {0x21F, 3, {0x00, 0x00, 0x00}};
    canMsg[MessageIds::x220] = {0x220, 2, {0x00, 0x00}};
    canMsg[MessageIds::x221] = {0x221, 7, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};
    canMsg[MessageIds::x260] = {0x260, 7, {0xB8, 0x34, 0x8F, 0x30, 0xD5, 0x41, 0x00}};


// ========== Инициализация таймеров ==========
    tmr50ms.startInterval(50, tmr50);
    tmr100ms.startInterval(100, tmr100);
    tmr200ms.startInterval(200, tmr200);
    tmr500ms.startInterval(500, tmr500);
    tmr1000ms.startInterval(1000, tmr1000);
}

void loop() {
    sett_loop();

    tmr50ms.tick();
    tmr100ms.tick();
    tmr200ms.tick();
    tmr500ms.tick();
    tmr1000ms.tick();
}
