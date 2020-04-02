/*
Fallblattanzeiger Uhr

2020 Simon Widmer
RTC:      DS3231
RS485:    ADM485
DCF77:    Reichelt+Transistorschaltung  
SI7021:   Temperature (and Humidity) Sensor
TSL2561:  Lux Sensor

Fehlerausgabe über LED
Einstellung der Änderungsintervalle Stationen via Poti
keine Buttons
*/

//##########################################################################
// Includes and defines
//##########################################################################
 
#include <Adafruit_TSL2561_U.h>
#include "Adafruit_Si7021.h"
#include <ArduinoRS485.h>
#include "RTClib.h"
#include "DCF77.h"
#include <jled.h>

#define RS485_BDRATE      19200
#define DE_PIN            9
#define RE_PIN            8

#define DCF_INTERRUPT     0
#define DCF_PIN           2       
#define LED_ERROR         6

#define ERR_INIT          -1    
#define ERR_NO            0    
#define ERR_DCF1          1  
#define ERR_DCF2          2    
#define ERR_RTC           3    
#define ERR_LUX           4    
#define ERR_TEMP          5    

//##########################################################################
// Variables
//##########################################################################

const byte  addr_h = 6;      // Adress Modul Hours
const byte  addr_m = 7;      // Adress Modul Minutes
const byte  addr_stat = 11;  // Adress Modul Stations
const byte  addr_delay = 8;  // Adress Modul Delay

int         POT_FUN  = A3;   // Delay between random interventions

const int   TH_LIGHT = 30; //Threshold light in lux
bool        TH_L_LOW = true; // Variable for Threshold light

DCF77 DCF = DCF77(DCF_PIN,DCF_INTERRUPT);

DateTime t_now;
RTC_DS3231 rtc;

Adafruit_TSL2561_Unified tsl = Adafruit_TSL2561_Unified(TSL2561_ADDR_FLOAT, 12345);
Adafruit_Si7021 sensor = Adafruit_Si7021();

byte t_hours = 0x00;
byte t_minutes = 0x00;
byte t_seconds = 0x00;
byte blade_m = 0x00; //Minuteblade has zero point at blade 31, conversion needed
byte stat_blank[] = {0,30,32,34,36,41,42,43,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60,61};


unsigned long lasttick_m = 0; //last time second added
unsigned long lastsync = 0; //last time DCF sync happened
unsigned long lastRandom1 = 0; //last time new station appeared
unsigned long presetRandom1 = 0; //

// Error Handling
auto led = JLed(LED_ERROR).Off().Forever();
int error_state = 0;

//##########################################################################
// Functions
//##########################################################################

// Configure Lux sensor
void configureSensor(void)
{
  tsl.enableAutoRange(true);            /* Auto-gain ... switches automatically between 1x and 16x */
  tsl.setIntegrationTime(TSL2561_INTEGRATIONTIME_13MS);      /* fast but low resolution */
}

// Get Lux Value
int lux_getlight(){
  sensors_event_t event;
  tsl.getEvent(&event);
  return event.light;
}

// Get time from RTC to local buffer
void rtc_gettime(){
  t_now = rtc.now();
  t_hours = t_now.hour(), DEC;
  t_minutes = t_now.minute(), DEC;  
  t_seconds = t_now.second(), DEC;  
}

//Initialize Timers for random interventions
void init_timers(){
  lastRandom1 = millis();
  presetRandom1 = 1000L+80000L*analogRead(POT_FUN);
}

 
//Check for last DCF Sync
void checksync(){
  //24h away
  if ((millis() - lastsync) > 86400000){
    state_error(ERR_DCF1);
  }
  //48h away
  if ((millis() - lastsync) > 172800000){
    state_error(ERR_DCF2);
  }
}

// Sync from DCF to RTC
void sync_dcf(){
  time_t DCFtime = DCF.getTime();
  if (DCFtime!=0) {
    tmElements_t tm;   
    breakTime(DCFtime, tm);
    rtc.adjust(DateTime(tm.Year+1970, tm.Month, tm.Day, tm.Hour, tm.Minute, tm.Second));
    rtc_gettime();
    lastsync = millis();
    state_error(ERR_NO);
  }   
}

//Random Station Generator
void checkrandom1(){
  if ((millis() - lastRandom1) > presetRandom1){
    sf_setdest();
    lastRandom1 = millis();

    //Generate new interval, potentiometer is multiplier
    // Maximum 22.7h, minimum every Second.
    presetRandom1 = 1000L+80000L*analogRead(POT_FUN);   
  }
}

//Show blank Flaps
void sf_noshow(){
  setpos(addr_m, 29);
  setpos(addr_h, 24);
  setpos(addr_stat, 0);
  setpos(addr_delay, 0);
}

//Set Display by RS485 Message
void sf_settime(){
  if(t_minutes<=30) blade_m=t_minutes+30;
  else blade_m=t_minutes-31;
  setpos(addr_m, blade_m);
  setpos(addr_h, t_hours);
}

void sf_setdest(){
  //Generate Number between 1 and 61 which is not blanked
  bool result=false;
  byte pos = 0;
  do {
    result = true;
    pos = random(1,61);
    // Check Blanking Array
    for (byte i = 0; i < (sizeof(stat_blank) / sizeof(stat_blank[0])); i++) {
      if (pos == stat_blank[i]){
        result = false;
      }
    } 
  }while(result == false);
  setpos(addr_stat, pos);
}

void sf_settemp(){
    float temperature = sensor.readTemperature();
    int temp = round(temperature);

    // Values of flap are 1-20 (flap 1-20),25 (21), 30 (22), 35 (23) etc.
    int pos = temp - 10;
    if(pos < 20){
      setpos(addr_delay, pos);
    }
    else if(pos > 21 && pos < 25){
      setpos(addr_delay, 21);      
    }
    else if(pos > 26 && pos < 30){
      setpos(addr_delay, 22);      
    }    
    else if(pos > 31 && pos < 35){
      setpos(addr_delay, 23);      
    }     
    else if(pos > 36 && pos < 40){
      setpos(addr_delay, 24);      
    }   
    else{    
      setpos(addr_delay, 40);      
    }
}

void setpos(byte address, byte pos){
  RS485.begin(RS485_BDRATE);
  RS485.setPins(1, DE_PIN, RE_PIN); //(int txPin, int dePin, int rePin)   
  RS485.beginTransmission();
  RS485.sendBreak(50);
  RS485.write(0xFF);
  RS485.write(0xC0);
  RS485.write(address);
  RS485.write(pos);
  RS485.endTransmission();
  RS485.end();
  //delay(500);
}

void state_error(int error){
  if(error != error_state){
    error_state = error;
    switch(error){
      case ERR_INIT:
        led = JLed(LED_ERROR).Off().Forever();    // System initializing
        break;
      case ERR_NO:
        led = JLed(LED_ERROR).Breathe(5000).Forever();    // Clock synced, runnig normal
        break;
      case ERR_DCF1:
        led = JLed(LED_ERROR).Blink(3000, 500).Forever(); // No DCF Signal since 24h
        break;
      case ERR_DCF2:
        led = JLed(LED_ERROR).Blink(2000, 500).Forever(); // No DCF Signal since 48h
        break;
      case ERR_RTC:
        led = JLed(LED_ERROR).Blink(5000, 1000).Forever(); // RTC not detected
        break;
      case ERR_LUX:
        led = JLed(LED_ERROR).Blink(5000, 2000).Forever(); // Lux Sensor not detected
        break;
      case ERR_TEMP:
        led = JLed(LED_ERROR).Blink(5000, 3000).Forever(); // Lux Sensor not detected
        break;
      default:
        break;
    }
  }
}

//##########################################################################
// Setup
//##########################################################################

 void setup() {
  pinMode(DE_PIN, OUTPUT);
  pinMode(RE_PIN, OUTPUT);
  digitalWrite(DE_PIN, LOW);
  digitalWrite(RE_PIN, HIGH);

  //Serial.begin(115200);
  
  DCF.Start();
  sf_noshow();
  delay(2000);
  
  // Init RTC
  if (!rtc.begin()){
    state_error(ERR_RTC);
    while (1);
  }
  state_error(ERR_INIT);
  
  if (rtc.lostPower()){
    rtc.adjust(DateTime(2000, 1, 1, 0, 0, 0));
    rtc_gettime();
  }

  // Init lux sensor
  if(!tsl.begin()){
    state_error(ERR_LUX);
    while(1);
  }
  state_error(ERR_INIT);
  
  configureSensor();

  // Init Temp Sensor
  if (!sensor.begin()) {
    state_error(ERR_TEMP);
    while (1);
  }
  
  randomSeed(analogRead(1));

  //Read Time from RTC to local buffer and set initial flap Position
  rtc_gettime();

  lasttick_m = millis();
}


//##########################################################################
// Loop
//##########################################################################

 void loop() {

  // Check Lighting every second (cheap "watch" increment)
  if(lux_getlight()>TH_LIGHT && (millis() - lasttick_m) > 1000){
    // If old state was lowlight, reinit Timers
    if(TH_L_LOW == true){
      init_timers();
      sf_settime();
      sf_settemp();
      sf_setdest();
    }
    TH_L_LOW = false;

    t_seconds += 1;
    if (t_seconds > 59) {
      t_seconds = 0;
      t_minutes += 1;
      // Check for new sync DCF every Minute
      sync_dcf();
      if (t_minutes > 59) {
        t_minutes = 0;
        t_hours += 1;
        // Sync from RTC every hour
        rtc_gettime();
        if (t_hours > 23) {
          t_hours = 0;
        }
      }
      sf_settime();
      sf_settemp();
    }
    lasttick_m = millis();
  }
  
  if(lux_getlight()<TH_LIGHT && TH_L_LOW == false && (millis() - lasttick_m) > 1000){
    sf_noshow();
    TH_L_LOW = true;
  }

  led.Update();
  checksync();
  checkrandom1();
}
