#include "TVout.h"
#include "fontALL.h"
#include "uRTCLib.h"
#include "DYPlayerArduino.h"
#include "EEPROM.h"

// Pin assignments
#define TV_BUTTON_PIN  10
#define SET_BUTTON_PIN  16
#define DOWN_BUTTON_PIN  14
#define UP_BUTTON_PIN  15
#define LMAP_COLLECTOR_PIN  5
#define TV_COLLECTOR_PIN 4

// EEPROM memory locations
#define ALARM_HOUR_ADDR 0
#define ALARM_MIN_ADDR 8
#define ALARM_VOL_ADDR 16
#define ALARM_SET_ADDR 24

struct ButtonState {
  uint8_t pin;
  bool stableState;
  bool lastReading;
  unsigned long lastDebounceTime;
};

ButtonState tvBtn = {TV_BUTTON_PIN, LOW, LOW, 0};
ButtonState setBtn = {SET_BUTTON_PIN, LOW, LOW, 0};
ButtonState downBtn = {DOWN_BUTTON_PIN, LOW, LOW, 0};
ButtonState upBtn = {UP_BUTTON_PIN, LOW, LOW, 0};

TVout TV;
uRTCLib rtc(0x68);
DY::Player player(&Serial1);

bool tvStateOn = false;
bool lampStateOn = false;
unsigned long tvStateChangedAt;
uint8_t lastTimeChangeSec = -1;
uint8_t alarmHour = 0;
uint8_t alarmMinute = 0;
uint8_t alarmVolume = 50;
uint8_t alarmRawSettings = 0;
bool alarmEnabled = false;
bool alarmLightEnabled = false;
bool alarmOn = false;
bool alarmSilenced = false;
char path[] = "/00001.mp3";

void setup() {
  Serial.begin(9600);
  URTCLIB_WIRE.begin();
  rtc.set_12hour_mode(false);

  pinMode(TV_BUTTON_PIN, INPUT);
  pinMode(SET_BUTTON_PIN, INPUT);
  pinMode(DOWN_BUTTON_PIN, INPUT);
  pinMode(UP_BUTTON_PIN, INPUT);
  
  pinMode(TV_COLLECTOR_PIN, OUTPUT);
  pinMode(LMAP_COLLECTOR_PIN, OUTPUT);

  // Restore Settings
  loadSettings();

  // MP3 Player
  player.begin();
  player.setVolume(alarmVolume);
  
  // TVOut
  TV.begin(_NTSC,80,30);
  TV.select_font(font8x8);

  toggleTvOnOff();
}

bool isButtonPressed(ButtonState &btn) {
  bool currentReading = digitalRead(btn.pin);
  bool triggered = false;

  if (currentReading != btn.lastReading) {
    btn.lastDebounceTime = TV.millis();
  }

  if ((TV.millis() - btn.lastDebounceTime) > 50) {
    if (currentReading != btn.stableState) {
      btn.stableState = currentReading;
      if (btn.stableState == HIGH) {
        triggered = true;
      }
    }
  }
  
  btn.lastReading = currentReading;
  return triggered;
}

void toggleTvOnOff(){
  tvStateChangedAt = TV.millis();

  if(tvStateOn){
    digitalWrite(TV_COLLECTOR_PIN, LOW);
  } else {
    digitalWrite(TV_COLLECTOR_PIN, HIGH);
  }

  tvStateOn = !tvStateOn;
  TV.delay(500);
  return;
}

void toggleLampOnOff(){
  if(lampStateOn){
    digitalWrite(LMAP_COLLECTOR_PIN, LOW);
  } else {
    digitalWrite(LMAP_COLLECTOR_PIN, HIGH);
  }

  lampStateOn = !lampStateOn;
  TV.delay(500);
  return;
}

bool shouldUpdateTime(){
  uint8_t second = rtc.second();

  if(second == lastTimeChangeSec){
    return false;
  } 

  lastTimeChangeSec = second;
  return true;
}

void displayAlarm(){
  char alarmBuf[20];
  sprintf(alarmBuf, "%02d:%02d %s", alarmHour, alarmMinute, alarmEnabled ? "ON" : "OFF");
  TV.print(1, 10, alarmBuf);
}

void displayTime(){
  char timeBuf[20];
  sprintf(timeBuf, "%02d:%02d:%02d", rtc.hour(), rtc.minute(), rtc.second());
  TV.print(1, 1, timeBuf);
}

uint8_t getBoundedTimeValueInput(const char* type, int minVal, int maxVal, int current){
  int value = current;
  char settingText[12];
  char settingValue[4];

  strcpy(settingText, "Set ");
  strcat(settingText, type);

  TV.clear_screen();

  while(true){
    TV.print(1, 1, settingText);
    
    itoa(value, settingValue, 10);
    TV.print(10, 10, settingValue);
    TV.print("  ");

    if (isButtonPressed(upBtn)) {
      value++;
      if (value > maxVal) value = minVal;
      TV.clear_screen();
    }

    if (isButtonPressed(downBtn)) {
      value--;
      if (value < minVal) value = maxVal;
      TV.clear_screen();
    }

    if (isButtonPressed(setBtn)) {
      break;
    }
  }

  return (uint8_t)value;
}

void storeSettings(){
  EEPROM.put(ALARM_HOUR_ADDR, alarmHour);
  EEPROM.put(ALARM_MIN_ADDR, alarmMinute);
  EEPROM.put(ALARM_VOL_ADDR, alarmVolume);
  alarmEnabled ? bitSet(alarmRawSettings, 1) : bitClear(alarmRawSettings, 1);
  alarmLightEnabled ? bitSet(alarmRawSettings, 0) : bitClear(alarmRawSettings, 0);
  EEPROM.put(ALARM_SET_ADDR, alarmRawSettings);
}

void loadSettings(){
  EEPROM.get(ALARM_SET_ADDR, alarmRawSettings);

  if(alarmRawSettings != 255){
    EEPROM.get(ALARM_HOUR_ADDR, alarmHour);
    EEPROM.get(ALARM_MIN_ADDR, alarmMinute);
    EEPROM.get(ALARM_VOL_ADDR, alarmVolume);
    alarmEnabled = bitRead(alarmRawSettings, 1);
    alarmLightEnabled = bitRead(alarmRawSettings, 0);
    return;
  }

  alarmRawSettings = 0;
  storeSettings();
}

void settingsModeLoop(){
  uint8_t newHour = getBoundedTimeValueInput("TH", 0, 23, rtc.hour());
  uint8_t newMinute = getBoundedTimeValueInput("TM", 0, 59, rtc.minute());
  uint8_t newSecond = getBoundedTimeValueInput("TS", 0, 59, rtc.second());
  rtc.set(newSecond, newMinute, newHour, 1, 1, 1, 25);

  alarmHour = getBoundedTimeValueInput("AH", 0, 23, alarmHour);
  alarmMinute = getBoundedTimeValueInput("AM", 0, 59, alarmMinute);
  alarmVolume = getBoundedTimeValueInput("VL", 1, 100, alarmVolume);
  uint8_t alarmEnableNum = getBoundedTimeValueInput("AEN", 0, 1, alarmEnabled ? 1 : 0);
  uint8_t alarmLightEnableNum = getBoundedTimeValueInput("LEN", 0, 1, alarmLightEnabled ? 1 : 0);
 
  alarmEnabled = alarmEnableNum == 1 ? true : false;
  alarmLightEnabled = alarmLightEnableNum == 1 ? true : false;
  player.setVolume(alarmVolume);
  storeSettings();
  
  TV.delay(300);
  TV.clear_screen();
}

void alarmSoundingLoop(){
  player.playSpecifiedDevicePath(DY::Device::Flash, path);
  alarmOn = true;
  unsigned long lastAlarmCheckTime = TV.millis();

  if(!lampStateOn && alarmLightEnabled){
    toggleLampOnOff();
  }

  if(!tvStateOn){
    toggleTvOnOff();
  }

  TV.clear_screen();
  TV.print(1, 1, "ALARM!");

  while(alarmOn && !alarmSilenced){
    if ((TV.millis() - lastAlarmCheckTime) > 1000) {
      lastAlarmCheckTime = TV.millis();
      rtc.refresh();

      if(rtc.minute() != alarmMinute){
         if(player.checkPlayState() != DY::PlayState::Playing){
            alarmOn = false;
            alarmSilenced = false;
         }
      }
    }

    if(isButtonPressed(tvBtn)) {
      player.stop();
      alarmOn = false;
      alarmSilenced = true;
    }
  }

  if(lampStateOn && alarmLightEnabled){
    toggleLampOnOff();
  }

  TV.clear_screen();
  return;
}

void loop() {
  rtc.refresh();

  if(alarmEnabled && !alarmOn && !alarmSilenced && rtc.hour() == alarmHour && rtc.minute() == alarmMinute){
    alarmSoundingLoop();
  }

  if(alarmSilenced && rtc.minute() != alarmMinute){
    alarmSilenced = false;
  }

  if (isButtonPressed(tvBtn)) {
    toggleTvOnOff();
  }

  if (isButtonPressed(setBtn)) {
    if(!tvStateOn){
      toggleTvOnOff();
    }
    settingsModeLoop();
  }

  if (isButtonPressed(downBtn)) {
    toggleLampOnOff();
  }

  if (tvStateOn && TV.millis() - tvStateChangedAt >= 30000) {
    toggleTvOnOff();
  }

  if(shouldUpdateTime() && tvStateOn){
    displayTime();
    displayAlarm();
  }
}