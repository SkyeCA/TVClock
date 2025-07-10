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

// EEPROM Mapping
// 32 bits total
// [8 bits][8 bits][8 bits][8 bits] 
// 00010111 00111011 01100100 00000011  
// 00010111 - 0-23 - Hour - uint8_t
// 00111011 - 0-59 - Minute - uint8_t
// 01100100 - 0-100 - Volume - uint8_t
// 00000111 - bools - Alarm Enabled/Light Enabled - uint8_t

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

  // Restore Settings
  loadSettings();

  // MP3 Player
  player.begin();
  player.setVolume(alarmVolume);
  
  // TVOut
  TV.begin(_NTSC,80,30);
  TV.select_font(font8x8);

  // TV on/off collector pin
  pinMode(TV_COLLECTOR_PIN, OUTPUT);
  toggleTvOnOff();
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

  if(alarmHour < 10){
    sprintf(alarmBuf, "%d%d:", 0, alarmHour);
  } else {
    sprintf(alarmBuf, "%d:", alarmHour);
  }

  if(alarmMinute < 10){
    sprintf(alarmBuf + strlen(alarmBuf), "%d%d", 0, alarmMinute);
  } else {
    sprintf(alarmBuf + strlen(alarmBuf), "%d", alarmMinute);
  }

  if(alarmEnabled){
    sprintf(alarmBuf + strlen(alarmBuf), " ON");
  } else {
    sprintf(alarmBuf + strlen(alarmBuf), " OFF");
  }

  TV.print(1,10,alarmBuf);
  return;
}

void displayTime(){
  uint8_t second = rtc.second();
  uint8_t hour = rtc.hour();
  uint8_t minute = rtc.minute();
  char timeBuf[20];

  if(hour < 10){
    sprintf(timeBuf, "%d%d:", 0, hour);
  } else {
    sprintf(timeBuf, "%d:", hour);
  }

  if(minute < 10){
    sprintf(timeBuf + strlen(timeBuf), "%d%d:", 0, minute);
  } else {
    sprintf(timeBuf + strlen(timeBuf), "%d:", minute);
  }

  if(second < 10){
    sprintf(timeBuf + strlen(timeBuf), "%d%d", 0, second);
  } else {
    sprintf(timeBuf + strlen(timeBuf), "%d", second);
  }

  TV.print(1,1,timeBuf);
  return;
}

uint8_t incOrDecOnButtonPress(uint8_t val){
  if (digitalRead(UP_BUTTON_PIN) == HIGH) {
    return val + 1;
  }

  if (digitalRead(DOWN_BUTTON_PIN) == HIGH) {
    return val - 1;
  }

  return val;
}

uint8_t getBoundedTimeValueInput(char* type, uint8_t min, uint8_t max, uint8_t current){
  uint8_t value = current != NULL ? current : min;
  char* str = "Set ";
  char settingText[9];
  char setingValue[4];

  strcpy( settingText, str );
  strcat( settingText, type );
  itoa(value, setingValue, 10);

  while(true){
    TV.clear_screen();
    TV.print(1,1,settingText);
    TV.print(10,10,setingValue);
    value = incOrDecOnButtonPress(value);

    if(value > max){
      value = min;
    }
    if(value < min){
      value = max;
    }

    itoa(value, setingValue, 10);
    TV.delay(200);
    
    if (digitalRead(SET_BUTTON_PIN) == HIGH){
      break;
    }
  }

  return value;
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

  //Sanity check incase this is the first run
  if(alarmRawSettings != 255){
    EEPROM.get(ALARM_HOUR_ADDR, alarmHour);
    EEPROM.get(ALARM_MIN_ADDR, alarmMinute);
    EEPROM.get(ALARM_VOL_ADDR, alarmVolume);
    alarmEnabled = bitRead(alarmRawSettings, 1);
    alarmLightEnabled = bitRead(alarmRawSettings, 0);
    return;
  }

  // If check failed we'll write the defaults
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

// Special alarm loop that doesn't update the screen while the alarm is playing.
// Screen updates while playing cause audio interference and I don't know why.
void alarmSoundingLoop(){
  player.playSpecifiedDevicePath(DY::Device::Flash, path);
  alarmOn = true;

  if(!lampStateOn && alarmLightEnabled){
    toggleLampOnOff();
  }

  if(!tvStateOn){
    toggleTvOnOff();
  }

  TV.clear_screen();
  TV.print(1,1,"ALARM!");

  while(alarmOn && !alarmSilenced){
    if(rtc.minute() != alarmMinute && player.checkPlayState() != DY::PlayState::Playing){
      alarmOn = false;
      alarmSilenced = false;
    }

    if(digitalRead(TV_BUTTON_PIN) == HIGH) {
      player.stop();
      alarmOn = false;
      alarmSilenced = true;
    }

    TV.delay(1000);
  }

  if(lampStateOn && alarmLightEnabled){
    toggleLampOnOff();
  }

  return;
}

void loop() {
  rtc.refresh();

  // If the alarm is enabled, not on, not silenced, and it's the set alarm time, start the alarm loop
  if(alarmEnabled && !alarmOn && !alarmSilenced && rtc.hour() == alarmHour && rtc.minute() == alarmMinute){
    alarmSoundingLoop();
  }

  // If alarm is silenced we need to detect that after the alarm time has passed or else it won't sound
  // the next time it should occur.
  if(alarmSilenced && rtc.minute() != alarmMinute){
    alarmSilenced = false;
  }

  // Turn TV on
  if (digitalRead(TV_BUTTON_PIN) == HIGH) {
    toggleTvOnOff();
  }

  // Start Settings UI Loop
  if (digitalRead(SET_BUTTON_PIN) == HIGH) {
    if(!tvStateOn){
      toggleTvOnOff();
    }
    settingsModeLoop();
  }

  // Turn lamp on
  if (digitalRead(DOWN_BUTTON_PIN) == HIGH) {
    toggleLampOnOff();
  }

  // Turn TV off automatically to preserve tube life
  if (tvStateOn && TV.millis() - tvStateChangedAt >= 30000) {
    toggleTvOnOff();
  }

  // Only upadte TV display if the TV is actually turned on
  if(shouldUpdateTime() && tvStateOn){
    displayTime();
    displayAlarm();
  }

  TV.delay(300);
}