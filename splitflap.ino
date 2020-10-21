/*
Fallblattanzeiger Uhr ESP8266

Versions chronology:
version 1   - 18.10.2020 Simon Widmer 

ESP8266 pinup :

D1 => SCL for TSL2561 / DS3231 / SI7021
D2 => SDA for TSL2561 / DS3231 / SI7021

D3 => Rx = RO of MAX485 - pin 1
D4 => Tx = DI of MAX485 - pin 4
D8 => RTS = RE/DE of MAX485 - pins 2&3


Inspiration:

NTP: http://werner.rothschopf.net/201802_arduino_esp8266_ntp.htm


*/
#include <Wire.h>
#include "RTClib.h"
#include <Adafruit_Sensor.h>
#include <Adafruit_TSL2561_U.h>
#include "Adafruit_Si7021.h"
#include <ESP8266WiFi.h>
#include <time.h>

/* Configuration of NTP */
#define MY_NTP_SERVER "ch.pool.ntp.org"                // set the best fitting NTP server (pool) for your location
#define MY_TZ "CET-1CEST,M3.5.0,M10.5.0/3"         // set timezone based on this list https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv

#define RX        D3    // Soft Serial RS485 Receive pin
#define TX        D4    // Soft Serial RS485 Transmit pin
#define RTS       D8    // RS485 Direction control
#define POT       A0    // Potentiometer
#define RS485Transmit    HIGH
#define RS485Receive     LOW
#define RS485_BDRATE     19200

#ifndef STASSID
#define STASSID "XXXXXXXXXXXXXXXXXXXXX"                            // set your SSID
#define STAPSK  "XXXXXXXXXXXXXXXXXXXXX"                        // set your wifi password
#endif

const byte  addr_h = 6;      // Adress Modul Hours
const byte  addr_m = 7;      // Adress Modul Minutes
const byte  addr_stat = 11;  // Adress Modul Stations
const byte  addr_delay = 8;  // Adress Modul Delay

const unsigned int ntp_update_interval = 60000;

byte t_hours = 0x00;
byte t_minutes = 0x00;
byte t_seconds = 0x00;
byte blade_m = 0x00; //Minuteblade has zero point at blade 31, conversion needed
byte stat_blank[] = {0,30,32,34,36,41,42,43,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60,61};
byte error1 = 0x20; //Blade "Ausfall"
byte error2 = 0x21; //Blade "unbestimmte Verspätung"
byte error3 = 0x22; //Blade "Ersatzzug"
bool error = false;

unsigned long lasttick_m = 0; //last time second added
unsigned long lastsync = 0; //last time NTP sync happened
unsigned long lastRandom1 = 0; //last time new station appeared
unsigned long presetRandom1 = 0; //

byte i= 0;

const int   TH_LIGHT = 2; //Threshold light in lux
const int   HYST_LIGHT = 1; //Hysterese for Light Threshold
bool        TH_L_LOW = true; // Variable for Threshold light
const int   FILTER_LENGTH = 60; // Length of SMA-Filter
float       temp_filtered;
float       old_temp_filtered = 0.0;

// Globals
Adafruit_TSL2561_Unified tsl = Adafruit_TSL2561_Unified(TSL2561_ADDR_FLOAT, 12345);
Adafruit_Si7021 si7021 = Adafruit_Si7021();

DateTime t_now;
RTC_DS3231 rtc;
time_t ntp_now;                         // this is the epoch, it will store the "UNIX Timestamp" - the seconds since January 1st 1970, 00:00 UTC
tm ntp_tm;                              // the structure tm holds time information in a more convient way


//
// Methods
//_____________________________________________________________________________________________


//Initialize Timers for random interventions
void init_timers(){
  lastRandom1 = millis();
  presetRandom1 = 1000L+80000L*analogRead(POT);
}


//Random Station Generator
void checkrandom1(){
  if ((millis() - lastRandom1) > presetRandom1){
    sf_setdest();
    lastRandom1 = millis();

    //Generate new interval, potentiometer is multiplier
    // Maximum 22.7h, minimum every Second.
    presetRandom1 = 1000L+80000L*analogRead(POT);   
  }
}

// Send RS485 Break
void sendBreak(unsigned int duration){
  Serial1.flush();
  Serial1.end();
  pinMode(TX, OUTPUT);
  digitalWrite(RTS, RS485Transmit);
  digitalWrite(TX, LOW);
  delay(duration);
  digitalWrite(TX, HIGH);
  //digitalWrite(RTS, RS485Receive);
  Serial1.begin(RS485_BDRATE);
}

// Set Splitflap
void setflap(byte address, byte pos){
  //DEBUG
//  Serial.print("Setze Modul ");
//  Serial.print(address);
//  Serial.print(" auf Position ");
//  Serial.println(pos);
//  Serial.println("");

  sendBreak(60);
  Serial1.write(0xFF);
  Serial1.write(0xC0);
  Serial1.write(address);
  Serial1.write(pos);
  delay(200); // Necessary??
}


// Get Lux Value
int get_amblight(void){
  sensors_event_t event;
  tsl.getEvent(&event);
  return event.light;
}

// Configure Lux sensor
void init_amblight(void)
{
  tsl.enableAutoRange(true);            /* Auto-gain ... switches automatically between 1x and 16x */
  tsl.setIntegrationTime(TSL2561_INTEGRATIONTIME_13MS);      /* fast but low resolution */
}

// Print Lux
void print_amblight(void)
{
  int k= get_amblight();
  Serial.print("Umgebungslicht: ");
  Serial.print(k);
  Serial.println(" lux");
}


// Get Temp Value
float get_temp(void){
  return si7021.readTemperature();
}

// Configure Temp sensor
boolean init_temp(void)
{
  //dht.setup(pin, DHTesp::DHT22);
  if(!si7021.begin()) return false;
  temp_filtered = get_temp();
  print_filtered_temp();
  return true;
}

// Print Temp
void print_temp(void)
{
  float t = get_temp();
  Serial.print("Temperatur: ");
  Serial.print(t);
  Serial.println("°C");
}

// Print filtered Temp
void print_filtered_temp(void)
{
  Serial.print("Temperatur: ");
  Serial.print(temp_filtered);
  Serial.println("°C");
}

// Get time from RTC to local time
void get_rtctime(){
  t_now = rtc.now();
  t_hours = t_now.hour(), DEC;
  t_minutes = t_now.minute(), DEC;  
  t_seconds = t_now.second(), DEC;  
  Serial.print("RTC: ");
  Serial.print(t_hours);
  Serial.print(":");
  Serial.print(t_minutes);
  Serial.print(":");
  Serial.println(t_seconds);
}

void get_ntptime() {
  time(&ntp_now);                       // read the current time
  localtime_r(&ntp_now, &ntp_tm);       // update the structure tm with the current time
  Serial.print("NTP: ");
  Serial.print(ntp_tm.tm_hour);         // hours since midnight  0-23
  Serial.print(":");
  Serial.print(ntp_tm.tm_min);          // minutes after the hour  0-59
  Serial.print(":");
  Serial.println(ntp_tm.tm_sec);          // seconds after the minute  0-61*
}

// Sync from NTP to RTC
void set_rtc(void){
  Serial.println("Setting RTC from NTP");
  time(&ntp_now);                       // read the current time
  localtime_r(&ntp_now, &ntp_tm);       // update the structure tm with the current time
  rtc.adjust(DateTime(ntp_tm.tm_year+1970, ntp_tm.tm_mon, ntp_tm.tm_mday, ntp_tm.tm_hour, ntp_tm.tm_min, ntp_tm.tm_sec));
}


// Nachbildung eines RC-Tiefpass FF = Filterfaktor;  Tau = FF / Aufruffrequenz            
void sma_filter(float &FiltVal, float NewVal, int FF){
  FiltVal= ((FiltVal * (FF - 1)) + NewVal) / ( FF ) ; 
}

//Show blank Flaps
void sf_noshow(void){
  setflap(addr_h, 24);
  setflap(addr_m, 29);
  setflap(addr_delay, 0);
  setflap(addr_stat, 0);
}

//Set Display by RS485 Message
void sf_settime(void){
  t_now = rtc.now();
  t_hours = t_now.hour(), DEC;
  t_minutes = t_now.minute(), DEC;  
  t_seconds = t_now.second(), DEC; 
  if(t_minutes<=30) blade_m=t_minutes+30;
  else blade_m=t_minutes-31;
  setflap(addr_m, blade_m);
  setflap(addr_h, t_hours);
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
  setflap(addr_stat, pos);
}

void sf_settemp(){
    float temperature = get_temp();
    sma_filter(temp_filtered, temperature, FILTER_LENGTH);
    int temp = round(temp_filtered);
    // Setze Temperatur nur, wenn eine Änderung stattgefunden hat.
    if((temp != round(old_temp_filtered)) && (error == false)){
      // Values of flap are 1-20 (flap 1-20),25 (21), 30 (22), 35 (23) etc.
      int pos = temp - 10;
      if(pos < 20){
        setflap(addr_delay, pos);
      }
      else if((pos > 21) && (pos < 25)){
        setflap(addr_delay, 21);      
      }
      else if((pos > 26) && (pos < 30)){
        setflap(addr_delay, 22);      
      }    
      else if((pos > 31) && (pos < 35)){
        setflap(addr_delay, 23);      
      }     
      else if((pos > 36) && (pos < 40)){
        setflap(addr_delay, 24);      
      }   
      else{    
        setflap(addr_delay, 40);      
      }
      old_temp_filtered = temp_filtered;
    }
}

// Setze Flaps jeweils während den ersten 3 Sekunden der neuen Minute. 
boolean ok_toset(void){
  t_now = rtc.now();
  t_seconds = t_now.second(), DEC; 
  if(t_seconds<3){ 
    Serial.print("Setze Flaps bei Sekunde ");
    Serial.println(t_seconds);
    return true;
  }
  else{
    return false;
  }
}

// Checking WiFi Connection
bool check_wifi(void){
  int m = 0;
  do {
    if (WiFi.status() == WL_CONNECTED){
      return true;
    }
    delay (2000);
    Serial.print ( "." );
    m++;
  } while (m < 10);
  // Flap "Verspätung"
  setflap(addr_delay, error2);
  return false;
}

// Check Sensors
void check_sensors(void){
  t_now = rtc.now();
  t_seconds = t_now.second(), DEC; 
  t_minutes = t_now.minute(), DEC; 
  if(t_seconds == 0 && t_minutes == 0){ 
     if (!check_wifi()){
      error = true;
    }
    else{
      error = false;
    }
  }
}



//
// SETUP
//_____________________________________________________________________________________________

void setup() {

  pinMode(RTS, OUTPUT);  
  
  // Start the built-in serial port, for Serial Monitor
  Serial.begin(115200);
  Serial.println(""); 
  Serial.println(""); 
  Serial.println(""); 
  Serial.println(""); 
  Serial.println("Fallblattanzeiger"); 
  Serial.println(""); 

  // Serial for RS485
  Serial1.begin(RS485_BDRATE);  
  delay(100);
  sf_noshow();

  // NTP Config
  configTime(MY_TZ, MY_NTP_SERVER);

  // Connecting to a WiFi network
  Serial.print("Verbinde mit WiFi ");
  Serial.print(STASSID);
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.begin(STASSID, STAPSK);

  while ( WiFi.status() != WL_CONNECTED ) {
    delay ( 500 );
    Serial.print ( "." );
  }
  Serial.println("");
  
  Serial.print("lokale IP: ");
  Serial.println(WiFi.localIP());


  // Init light sensor 
  if(!tsl.begin())
  {
    Serial.print("Lux Sensor nicht erkannt");
    while(1);
  }
  init_amblight();
  print_amblight();

  // Init RTC
  if (!rtc.begin()){
    Serial.println("RTC nicht erkannt");
    while (1);
  }
  
  if (rtc.lostPower()){
    Serial.println("RTC Spannungsversorgung - Reset");
  }
  // Set RTC initially from NTP
  set_rtc();
  get_rtctime();
  
  // Init Temperature Sensor
  if (!init_temp()){
    Serial.println("SI7021 nicht erkannt");
  }

  lasttick_m = millis();

  delay(5000);
}  // end of setup

//
// LOOP
//_____________________________________________________________________________________________

void loop() {
    // DEBUG Help
  if (Serial.available()>0) //Checks for a character in the serial monitor
  {
    i = Serial.parseInt();
    switch(i){
      case 1:
        print_filtered_temp();
        break;
      case 2:
        get_rtctime();
        break;
      case 3:
        print_amblight();
        break;
      case 4:
        Serial.print("Potidelay: ");
        Serial.println(1000L+80000L*analogRead(POT));
        break;
    }
  }

  // Check Lighting every second and set Flaps
  if(get_amblight()>(TH_LIGHT+HYST_LIGHT) && (millis() - lasttick_m) > 1000){
    // If old state was lowlight, reinit Timers and set time immediately
    if(TH_L_LOW == true){
        old_temp_filtered = 0;
        temp_filtered = get_temp();
        // Set RTC from NTP at rewake (normally after a night)
        set_rtc();
        get_rtctime();
        init_timers();
        sf_settime();
        sf_settemp();
        sf_setdest();
    }
    TH_L_LOW = false;

    sf_settemp();
    checkrandom1();
    check_sensors();
    // Set flaps every first 3 seconds of a new minute 
    if(ok_toset()){
      sf_settime();
    }
    lasttick_m = millis();
  }
  
  if(get_amblight()<(TH_LIGHT-HYST_LIGHT) && TH_L_LOW == false && (millis() - lasttick_m) > 1000){
    sf_noshow();
    TH_L_LOW = true;
  }
}     // end of loop
