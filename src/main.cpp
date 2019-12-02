//datum posledn9 zmeny 5.10.2018 prvni uprava
//definitivni verze k 24.12.2018
//zmena  10.3.2019  zapadlo.local -> zapadlo.test
//pridána sila signalu, verze 102

#include "pwm-new.h"
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <OneWire.h>
#include <EEPROM.h>
//#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <Ticker.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

//skript pro ukladani teplot a stavu
//#define SKRIPTV "sonda/xxxxx.php"
#define VERSION 102



//definice pinu
#define T1    D4
#define T2    D2
#define PWMW1  D5
#define PWMW2  D1
#define PWMR  D6
#define PWMG  D7
#define PWMB  D8
#define TEPLOMER  D3  //pin pro DS18B20
#define POCET_DS 1    //maximalni ocekavany pocet DS18B20
#define TEPLOMER_TIME 10 //interval prevodu teplromeru
//prideleni pameti EEPROM, rastr 4byte
#define WJAS 0    //adresa v eeprom pro ulozeni jasu bile
#define RJAS 4  //adresa v eeprom pro ulozeni jasu R
#define GJAS 8   //adresa v eeprom pro ulozeni jasu G
#define BJAS 12 //adresa v eeprom pro ulozeni jasu B
#define TIMEZONE 24 //1Byte na casovou zonu
#define BUDIK_TIME 25 //4B na cas budiku
#define BUDIK_STATE 29 // 1B na stav budiku
#define BUDIK_JAS 30 // 4B na jas budiku
#define TEST 40 //testovaci adresa

#define konstanta 1000 //pocet ms na s
#define DEBOUNC 200 //protizakmitova konstanta

#define BUDIK_START 900 //delka rozedneni
#define BUDIK_SVIT 1800 //delka svitu na budiku

#define OVERTEMP 50 //hranice prehrati

#define PWM_CHANNELS 5
#define PWM_MULTIPLY  4
const uint32_t period = 1023*PWM_MULTIPLY; // * 200ns ^= 5 kHz
uint32 io_info[PWM_CHANNELS][3] = {
	// MUX, FUNC, PIN
  {PERIPHS_IO_MUX_MTMS_U,  FUNC_GPIO14, 14}, //D5
	{PERIPHS_IO_MUX_MTDI_U,  FUNC_GPIO12, 12}, //D6
	{PERIPHS_IO_MUX_MTCK_U,  FUNC_GPIO13, 13}, //D7
  {PERIPHS_IO_MUX_MTDO_U,  FUNC_GPIO15, 15}, //D8
	{PERIPHS_IO_MUX_GPIO5_U, FUNC_GPIO5 ,  5}, //D1
  };
  // initial duty: all off
  uint32 pwm_duty_init[PWM_CHANNELS] = {0,0,0,0};

//hlavičky funkci
OneWire temp(TEPLOMER);
ESP8266WebServer server(80);
Ticker temper;
Ticker synchro;
Ticker minuta;
Ticker budik;
WiFiUDP udp;

void prevod_teplomeru();


void handleRoot();
void handleNotFound();
void handleSubmit();
void handleWlon();
void handleWloff();
void handleRgbon();
void handleRgboff();

void index();
void zobraz();
//void odesli();
void sendpacket(IPAddress& address);
uint8_t zjisti();
void syncenable();
void set_minuta();
void write_to_eeprom(uint32_t integer, uint8_t adresa);
uint32_t read_from_eeprom(uint8_t adresa);
void set_pwm (void);
void budik_ramp(void);


//promenne
#include "password.h"
//const char* ssid     = "xxxxxx";
// const char* password = "yyyyyy";
//promene pro odesli()
//const char* host = "zapadlo.name";
//const uint8_t host_IP[]={109,69,211,132};

//promenne NTP
unsigned int localPort = 2390;
const int _PACKET_SIZE = 48;
byte packetBuffer[ _PACKET_SIZE];
IPAddress timeServerIP;
//unsigned long epoch; //unix time
const char* host_="tik.cesnet.cz";
//promenne pro web server
const int httpPort = 80;

//promenne pro teplotni cidla
byte addrT[POCET_DS][8];    //adresy teplotnich cidel
byte pocetT = 0;      //pocet teplotnich cidel
float teploty[POCET_DS];    //hodnoty teplotnich cidel
byte hash[POCET_DS];    //identifikace teplotnich cidel (hashe)
uint8_t valid_temp_read[POCET_DS]; //pocet spravnych cteni na teplotnich cidlech
boolean ds_flag=false;

uint8_t overtemp=0; //indikator prehrati


//promenne pro cas
uint32_t testtime=0;
uint32_t temptime=0;
uint32_t time_z=0;
uint8_t syncnow=1;
uint8_t sync=0;
uint8_t timezone=0;
uint8_t flag_minuta=0;
uint32_t budik_time=0;
uint8_t budik_state=0;
//jas budiku
uint16_t budik_jas=0;
//budik je aktivovan
uint8_t budik_aktiv=0;
//prubezny stav budiku
uint16_t budik_up=0;


//promene pro svetla
uint16_t w_jas=0; //jas bile pri manualnim zapnuti
uint16_t w_jas_docas=0; //jas bile
uint16_t r_jas=0;
uint16_t g_jas=0;
uint16_t b_jas=0;
//stav svetel
uint8_t w_state=0;
uint8_t rgb_state=0;


//promenne pro ladeni
uint32_t start_unixtime=0;
uint32_t prubezny_unixtime=0;

//aktualni ip adresa
IPAddress tst;
//promene pro tlacitka
uint8_t t1_last=1;
uint8_t t2_last=1;
uint32_t t1_time=0;
uint32_t t2_time=0;
//string pro Web
String INDEX_HTML;
String INDEX_HTML_1 =
"<!DOCTYPE HTML>\n"
"<html>\n"
"<head>\n"
"<meta http-equiv=\"Content-Type\" content=\"text/html; charset=UTF-8\">\n"
"<meta name = \"viewport\" content = \"width = device-width\">\n"
"<title>ESP8266 Web Form Settings</title>\n"
"<link rel=\"stylesheet\" type=\"text/css\" href=\"http://pi.zapadlo.test/svetla/style.css\">\n"
"</head>\n"
"<body>\n"
"<div class=\"wrapper\">"
"<h1>Světla Ondra</h1><br>\n";

String INDEX_HTML_3 =
//"<tr><td></td><td><INPUT type=\"submit\" value=\"Uložit\"></td></tr>"
//"</table>\n"
"</form>\n"
"</div>\n"
"</body>\n"
"</html>";



void setup() {

  Serial.begin(115200);
  delay(10);
  pinMode(T1, INPUT_PULLUP);
  pinMode(T2, INPUT_PULLUP);
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(PWMW1, OUTPUT);
  pinMode(PWMW2, OUTPUT);
  pinMode(PWMR, OUTPUT);
  pinMode(PWMG, OUTPUT);
  pinMode(PWMB, OUTPUT);
  pwm_init(period, pwm_duty_init, PWM_CHANNELS, io_info);
  Serial.println("start1");
  pwm_start();
  Serial.println("start2");
  set_pwm();
  Serial.print("Verze: ");
  Serial.println(VERSION);
  //inicializace EEPROM
  EEPROM.begin(512);

  // We start by connecting to a WiFi network


  Serial.println();
  Serial.println();
  Serial.print("Connecting to: ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);

  while (WiFi.status() != WL_CONNECTED) {
    digitalWrite(LED_BUILTIN,0);
    delay(50);
    Serial.print(".");
    digitalWrite(LED_BUILTIN,1);
    delay(350);
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  tst = WiFi.localIP();

  //inicializace teplotnich cidel

  for (int i = 0; i < POCET_DS; i++ ) {
    if ( !temp.search(addrT[i])) {
      addrT[i][0] = 0;
      addrT[i][1] = 0;
      addrT[i][2] = 0;
      addrT[i][3] = 0;
      addrT[i][4] = 0;
      addrT[i][5] = 0;
      addrT[i][6] = 0;
      addrT[i][7] = 0;
      break;
    }
    else {
      pocetT++;
      hash[i] = addrT[i][0] ^ addrT[i][1] ^ addrT[i][2] ^ addrT[i][3] ^ addrT[i][4] ^ addrT[i][5] ^ addrT[i][6] ^ addrT[i][7];
      Serial.print("Hash cidla ");
      Serial.print(hash[i]);
      Serial.println();

    }
    delay(700);
  }
  Serial.print("Nalezeno cidel teploty: ");
  Serial.println(pocetT);

//nacteni hodnot z eeprom
timezone=EEPROM.read(TIMEZONE);
Serial.print("Timezone: ");
Serial.println(timezone);
if ((timezone>2)||(timezone<1)){
  timezone=1; //zimni cas
  EEPROM.write(TIMEZONE,timezone);
  EEPROM.commit();
}
//nacteni jas
w_jas=read_from_eeprom(WJAS);
Serial.print("Wjas: ");
Serial.println(w_jas);
if (w_jas> 1023) {
  w_jas=0;
  write_to_eeprom(w_jas, WJAS);
}
w_jas_docas=w_jas;
r_jas=read_from_eeprom(RJAS);
Serial.print("Rjas: ");
Serial.println(r_jas);

if (r_jas> 1023) {
  r_jas=0;
  write_to_eeprom(r_jas, RJAS);
}
g_jas=read_from_eeprom(GJAS);
Serial.print("Gjas: ");
Serial.println(g_jas);

if (g_jas> 1023) {
  g_jas=0;
  write_to_eeprom(g_jas, GJAS);
}
b_jas=read_from_eeprom(BJAS);
Serial.print("Bjas: ");
Serial.println(b_jas);

if (b_jas> 1023) {
  b_jas=0;
  write_to_eeprom(b_jas, BJAS);
}
budik_time=read_from_eeprom(BUDIK_TIME);
Serial.print("Budik time: ");
Serial.println(budik_time);

if (budik_time> 86400) {
  budik_time=0;
  write_to_eeprom(budik_time, BUDIK_TIME);
}

budik_state=EEPROM.read(BUDIK_STATE);
Serial.print("Budik state: ");
Serial.println(budik_state);

if (budik_state>1){
  budik_state=0; //budik vypnut
  EEPROM.write(BUDIK_STATE,budik_state);
  EEPROM.commit();
}
budik_jas=read_from_eeprom(BUDIK_JAS);
Serial.print("Budik jas: ");
Serial.println(budik_jas);

if (budik_jas> 1023) {
  budik_jas=0;
  write_to_eeprom(budik_jas, BUDIK_JAS);
}

//start web server
if (MDNS.begin("ondra")) {
   Serial.println("MDNS responder started");
 }

 server.on("/", handleRoot);
 server.on("/wlon", handleWlon);
 server.on("/wloff", handleWloff);
 server.on("/rgbon", handleRgbon);
 server.on("/rgboff", handleRgboff);
 server.onNotFound(handleNotFound);
 server.begin();
 Serial.println("HTTP server started");
 //zjisteni  casu
 udp.begin(localPort);
 Serial.println("UDP Started");
 Serial.print("Local port: ");
 Serial.println(udp.localPort());
 for (uint8_t i=0;i<3; i++){
   if (zjisti()==0){
     break;
   }
 }
 start_unixtime=prubezny_unixtime;
 if (sync==0) {
      synchro.attach (120, syncenable);
 } else {
      synchro.attach (43000, syncenable);
 }
//start prevod teplot z cidel DS18B20
temper.attach(TEPLOMER_TIME, prevod_teplomeru);
//start minutovych intervalu
minuta.attach(60, set_minuta);
//OTA

ArduinoOTA.onStart([]() {
  Serial.println("Start");
});
ArduinoOTA.onEnd([]() {
  Serial.println("\nEnd");
});
ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
  Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
});
ArduinoOTA.onError([](ota_error_t error) {
  Serial.printf("Error[%u]: ", error);
  if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
  else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
  else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
  else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
  else if (error == OTA_END_ERROR) Serial.println("End Failed");
});

ArduinoOTA.setHostname("ondra");
ArduinoOTA.begin();
}



void loop() {
  //obsluha OTA
  ArduinoOTA.handle();
  //obsluha web clientu
  server.handleClient();
  //vterinove intervaly
  testtime = millis ();
  if ((testtime - temptime) > konstanta)
    {
      for (uint8_t i = 0; i < ((testtime - temptime) / konstanta); i++)
	     {
	        time_z++;
	        temptime = temptime + konstanta;
	       }
    if (time_z >= 86400)
	     {			//vynulovani dne
	        time_z = time_z - 86400;
	     }
    //obsluha  budíku
    if ((budik_aktiv==0)&&(budik_time < time_z)&&((budik_time+4)> time_z)&&(w_state==0)&&(budik_state==1)){
        //budik poprve aktivovan
        budik_aktiv=1;
        Serial.println("Budik aktivovan");
        Serial.println(BUDIK_START);
        Serial.println(budik_jas);
        budik.attach_ms((BUDIK_START*1000)/budik_jas, budik_ramp);
        Serial.println((BUDIK_START*1000)/budik_jas);
        budik_up=0;
        w_state=1;
    }
    //budik vypneme
    if ((budik_aktiv==1)&&((budik_time+BUDIK_SVIT)<time_z)){
        budik_aktiv=0;
        w_state=0;
        set_pwm();
    }
  }

    //synchronizace s
    if (syncnow == 0) {
      for (uint8_t i=0;i<3; i++){
        if (zjisti()==0){
          break;
        }
      }
      syncnow = 1;
      if (sync==0) {
         synchro.attach (120, syncenable);
      } else {
          synchro.attach (43000, syncenable);
      }
    //Serial.println (" ");
    //Serial.println (syncnow);
    //Serial.println (sync);
    }
    //minutovy interval / odeslani, zobrazeni
    if (flag_minuta==1){
      flag_minuta=0;
      zobraz();
      //test na prehrati
      if (teploty[0] > OVERTEMP){
        w_state=0;
        rgb_state=0;
        set_pwm();
        overtemp=1;
      }
     //odesli();
    }
    //obsluha tlacitek
    if ((digitalRead(T1)==0)&&(t1_last==1)&&((t1_time+DEBOUNC)<millis())){
      //mame poprve zmackle
      t1_time=millis();
      t1_last=0;
      //zmacknute poprve
      Serial.println("zmacknuto T1");
      if (w_state==0){
        w_state=1;
      } else {
        w_state=0;
        budik.detach();
        budik_aktiv=0;
      }
      w_jas_docas=w_jas;
      set_pwm();
    } else{
      if ((digitalRead(T1)==1)&& (t1_last==0)){
        t1_last=1;
      }
    }
    if ((digitalRead(T2)==0)&&(t2_last==1)&&((t2_time+DEBOUNC)<millis())){
      //mame poprve zmackle
      t2_time=millis();
      t2_last=0;
      //zmacknute poprve
      Serial.println("zmacknuto T2");
      if (rgb_state==0){
        rgb_state=1;
      } else {
        rgb_state=0;
      }
      set_pwm();
    } else{
      if ((digitalRead(T2)==1)&& (t2_last==0)){
        t2_last=1;
      }
    }
}






//------------------------podprogramy--------------------


//prevod teplomeru
void prevod_teplomeru() {
  //nacteni teplot  z cidel
  #define MAXTEMP 90
  #define MINTEMP -30
  //byte present = 0;
  byte data[12];

  for (uint8_t i = 0; i < (pocetT); i++) {

    if (ds_flag== false) {
      temp.reset();
      temp.select(addrT[i]);
      temp.write(0x44, 1);       // start conversion, with parasite power on at the end
    } else {
      temp.reset();
      temp.select(addrT[i]);
      temp.write(0xBE);
      for (int j = 0; j < 9; j++) {           // we need 9 bytes
        data[j] = temp.read();
      }
      if ( temp.crc8( data, 8) != data[8]) {
        Serial.print(i);
        Serial.print(" CRC is not valid!  ");
        Serial.print( temp.crc8( data, 8), HEX);
        Serial.print(" ");
        Serial.println (data[8], HEX);
      } else {
        int16_t raw = (data[1] << 8) | data[0];
        byte cfg = (data[4] & 0x60);
        // at lower res, the low bits are undefined, so let's zero them
        if (cfg == 0x00) raw = raw & ~7;  // 9 bit resolution, 93.75 ms
        else if (cfg == 0x20) raw = raw & ~3; // 10 bit res, 187.5 ms
        else if (cfg == 0x40) raw = raw & ~1; // 11 bit res, 375 ms
        //// default is 12 bit resolution, 750 ms conversion time
        //test zda teplota je v ocekavanem rozsahu
        if ((MAXTEMP > (float)raw / 16.0)&&(MINTEMP< (float)raw / 16.0)){
          teploty[i] = (float)raw / 16.0;
          valid_temp_read[i]++;
          Serial.print("Teplota ");
          Serial.print(i);
          Serial.print(" ");
          Serial.println(teploty[i]);
        } else {
          Serial.println("Teplota mimo rozsah");
        }
      }
    }
  }
  if (ds_flag==false) {
    ds_flag=true;
  } else {
    ds_flag=false;
  }
}


//obsluha startu budiku
void budik_ramp (void){
  w_jas_docas=budik_up;
  set_pwm();
  budik_up++;
  if (budik_up >= budik_jas){
      budik.detach();
  }
}

void handleRoot() {
  digitalWrite(LED_BUILTIN, 0);
  if (server.hasArg("tz")) {
    handleSubmit();
  } else {
  index();
  server.send(200, "text/html", INDEX_HTML);
  }
  digitalWrite(LED_BUILTIN, 1);
}
void handleNotFound(){
  digitalWrite(LED_BUILTIN, 0);
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET)?"GET":"POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i=0; i<server.args(); i++){
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
  digitalWrite(LED_BUILTIN, 1);
}

void handleSubmit() {
  String argument;

  uint16_t w_jas_temp, r_jas_temp, g_jas_temp, b_jas_temp, budik_jas_temp, budik_state_temp;
  uint32_t budik_time_temp;
  argument = server.arg("wjas");
  w_jas_temp = argument.toInt();
  char char_temp[2];
  argument = server.arg("color");
  Serial.println(argument);
  String arg_temp=argument.substring(1,3);
  arg_temp.toCharArray(char_temp,3);
  r_jas_temp=map(strtol(char_temp,0,16),0,255,0,1023);
  arg_temp=argument.substring(3,5);
  arg_temp.toCharArray(char_temp,3);
  g_jas_temp=map(strtol(char_temp,0,16),0,255,0,1023);
  arg_temp=argument.substring(5,7);
  arg_temp.toCharArray(char_temp,3);
  b_jas_temp=map(strtol(char_temp,0,16),0,255,0,1023);
  argument = server.arg("budikjas");
  budik_jas_temp=argument.toInt();
  if (server.arg("BUDIK") == "ano"){
    budik_state_temp=1;
  } else {budik_state_temp=0;}
  argument = server.arg("HH");
  budik_time_temp = argument.toInt()*3600;
  argument = server.arg("MM");
  budik_time_temp += argument.toInt()*60;
//kotrola jasu
if (w_jas_temp < 1024) {
  if (w_jas != w_jas_temp){
    w_jas=w_jas_temp;
    write_to_eeprom(w_jas, WJAS);
  }
}
if (r_jas_temp < 1024) {
  if (r_jas != r_jas_temp){
    r_jas=r_jas_temp;
    write_to_eeprom(r_jas, RJAS);
  }
}
if (g_jas_temp < 1024) {
  if (g_jas != g_jas_temp){
    g_jas=g_jas_temp;
    write_to_eeprom(g_jas, GJAS);
  }
}
if (b_jas_temp < 1024) {
  if (b_jas != b_jas_temp){
    b_jas=b_jas_temp;
    write_to_eeprom(b_jas, BJAS);
  }
}
  argument = server.arg("tz");
  uint8_t tz_temp = argument.toInt();


  //kontrola casove zony
  if ((tz_temp > 0)&&(tz_temp <3)){
    if (tz_temp != timezone){
      timezone=tz_temp;
      EEPROM.write(TIMEZONE,timezone);
      EEPROM.commit();
      zjisti();
    }
  }
  if (budik_jas_temp < 1024) {
    if (budik_jas != budik_jas_temp){
      budik_jas=budik_jas_temp;
      write_to_eeprom(budik_jas, BUDIK_JAS);
    }
  }
  if (budik_time_temp < 86400) {
    if (budik_time != budik_time_temp){
      budik_time=budik_time_temp;
      write_to_eeprom(budik_time, BUDIK_TIME);
      budik_aktiv=0; //zmema casu - zapnuti budiku
    }
  }
  if (budik_state_temp < 2) {
    if (budik_state != budik_state_temp){
      budik_state=budik_state_temp;
      EEPROM.write(BUDIK_STATE,budik_state);
      EEPROM.commit();
      Serial.print("budik_state: ");
      Serial.println(budik_state);
    }
  }
  server.sendHeader("Connection", "close");
  server.sendHeader("Access-Control-Allow-Origin", "*");
  index();
  server.send(200, "text/html", INDEX_HTML);
  w_jas_docas=w_jas;
  set_pwm();
}

//vytvoreni souboru index.html
void index(){
INDEX_HTML = INDEX_HTML_1;
//test zda neni prehrato
if (overtemp > 0) {
  INDEX_HTML += "<H1> Přehřáto</H1>\n";
}
//zapnuti svetla
INDEX_HTML += "<a href=\"";
if (w_state==0){
  INDEX_HTML += "./wlon\" class=\"light-icon-on\"></a>\n";
} else {
  INDEX_HTML += "./wloff\" class=\"light-icon-off\"></a>\n";
}
INDEX_HTML += "<a href=\"";
if (rgb_state==0){
  INDEX_HTML += "./rgbon\" class=\"rgb-icon-on\"></a>\n";
} else {
  INDEX_HTML += "./rgboff\" class=\"rgb-icon-off\"></a>\n";
}
INDEX_HTML +="<form action=\"/\" method=\"post\" class=\"form\">\n";
INDEX_HTML +="<div class=\"slidecontainer\">";
//slider na jas
INDEX_HTML +="<p class=\"slider-placeholder noselect\">Hlavní světlo</p>";
INDEX_HTML += "<input type=range name=wjas min=0 max=1023 value=";
INDEX_HTML += w_jas;
INDEX_HTML += " onchange=\"this.form.submit()\" class=\"slider\">\n";
INDEX_HTML += "<input type=color name=color value=\"#";
//doplneni uvodnich null
if (map(r_jas, 0, 1023, 0, 255)<16) {
  INDEX_HTML += "0";
}
INDEX_HTML +=String(map(r_jas, 0, 1023, 0, 255), HEX);
if (map(g_jas, 0, 1023, 0, 255)<16) {
  INDEX_HTML += "0";
}
INDEX_HTML +=String(map(g_jas, 0, 1023, 0, 255), HEX);
if (map(b_jas, 0, 1023, 0, 255)<16) {
  INDEX_HTML += "0";
}
INDEX_HTML +=String(map(b_jas, 0, 1023, 0, 255), HEX);
INDEX_HTML +="\" onchange=\"this.form.submit()\" class=\"slider-color\">\n";
INDEX_HTML += "<table>\n<tbody>\n";
//cas
INDEX_HTML += "<tr><td>Aktuální čas:</td><td>";
INDEX_HTML += uint8_t(time_z/3600);
INDEX_HTML +=":";
if (((time_z%3600)/60) < 10){
  INDEX_HTML +="0";
}
INDEX_HTML += uint8_t((time_z%3600)/60);
INDEX_HTML += "</td></tr>\n";
//zobrazni teploty
if (pocetT > 0){
  INDEX_HTML += "<tr><td>Teplota uvnitř</td><td>";
  INDEX_HTML += teploty[0];
  INDEX_HTML += "</td></tr>\n";
}
//tisk sily signálu
INDEX_HTML += "<tr><td>Síla Wifi signálu</td><td>";
INDEX_HTML += WiFi.RSSI();
INDEX_HTML += "dBm</td></tr>\n";

//tisk Verze
INDEX_HTML += "<tr><td>Použitá verze</td><td>";
INDEX_HTML += VERSION;
INDEX_HTML += "</td></tr>\n";
//nastaveni casove zony
INDEX_HTML += "<tr><td>Časová zóna UTC + </td><td><select name=\"tz\" size=\"1\"  onchange=\"this.form.submit()\">\n";
for (uint8_t i = 1; i < 3; i++){
    INDEX_HTML += "<option ";
    if (i == timezone) {
      INDEX_HTML += "selected";
    }
    INDEX_HTML += ">";
    INDEX_HTML += i;
    INDEX_HTML += "</option>  \n";
  }
INDEX_HTML +="</select></td></tr>\n";
INDEX_HTML +="<tr><td>Čas budíku:</td><td><input type=\"text\" size=2 name=\"HH\" value=";
INDEX_HTML +=uint8_t(budik_time/3600);
INDEX_HTML += " onchange=\"this.form.submit()\">:";
INDEX_HTML +="<input type=\"text\" size=2 name=\"MM\" value=";
INDEX_HTML += uint8_t((budik_time%3600)/60);
INDEX_HTML += " onchange=\"this.form.submit()\"></td><td></td></tr>\n";
INDEX_HTML +="<tr><td>Budík aktivní:</td><td><label class=\"container\">\n<input type=checkbox name=\"BUDIK\" value=ano";
if (budik_state==1) {
  INDEX_HTML += " checked";
}
INDEX_HTML +=" onchange=\"this.form.submit()\"><span class=\"checkmark\"></span></label></td></tr>\n";
INDEX_HTML +="</tbody>\n</table>\n";
INDEX_HTML +="<div class=\"slidecontainer\">\n";
INDEX_HTML += "<input type=range name=budikjas min=0 max=1023 value=";
INDEX_HTML += budik_jas;
INDEX_HTML += " onchange=\"this.form.submit()\" class=\"slider\" id=\"myRange\">\n";
INDEX_HTML +="</div>\n";


//dokonceni formulare, tlacitko submit
INDEX_HTML += INDEX_HTML_3;
}


//odeslani dat na PI
/*
void odesli (){
      pocitadlo_odeslani++;
      long rssi = WiFi.RSSI();
      WiFiClient client;
      err=client.connect(host_IP, httpPort);
      if (err != 1) {
        Serial.print("connection failed error:");
        Serial.println(err);
        return;
      }
      String url = "http://";
      url += host;
      url += "/";
      url += SKRIPTV;
      url += "?rssi=";
      url += rssi;
      for (uint8_t i=0; i< pocetT; i++){
        url +="&hash";
        url +=i;
        url +="=";
        url +=hash[i];
        url += "&temp";
        url +=i;
        url +="=";
        url +=int (teploty[i]*10);
        url +="&valid";
        url +=i;
        url +="=";
        url +=valid_temp_read[i];
        valid_temp_read[i]=0;
      }
      url += "&jas=";
      url += pocitadlo_mezi;
      url += "&sync=";
      url += sync;
      url += "&pocitadlo=";
      url += pocitadlo_odeslani;
      url += "&unixtime=";
      url += start_unixtime;
      url += "&topeni=";

      if (digitalRead(TOP_PIN)==0){
        url +="1";
      } else {
        url +="0";
      }
      url += "&ip=";
      tst = WiFi.localIP();
      url += tst[3];
      url += "&reg=";
      url += topeni_temp;

      Serial.println (url);
      client.print(String("GET ") + url + " HTTP/1.1\r\n" +
                   "Host: " + host + "\r\n" +
                   "Connection: close\r\n\r\n");
}
*/

//pprg pro zjisteni casu startu
void sendpacket(IPAddress& address)
{
  Serial.println("sending  packet...");
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, _PACKET_SIZE);
  // Initialize values needed to form  request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;

  // all  fields have been given values, now
  // you can send a packet requesting a timestamp:
  udp.beginPacket(address, 123); // requests are to port 123
  udp.write(packetBuffer, _PACKET_SIZE);
  udp.endPacket();
}



uint8_t zjisti(){
  unsigned long epoch; //unix time
  WiFi.hostByName(host_, timeServerIP);
  Serial.print("NTP from: ");
  Serial.print(host_);
  Serial.print("");
  Serial.println(timeServerIP);
  sendpacket(timeServerIP); // send an  packet to a time server
  // wait to see if a reply is available
  delay(1000);
  int cb = udp.parsePacket();
  if (!cb) {
    Serial.println("no packet yet");
    sync = 0;
    return 1;
  }
  else {
    Serial.print("packet received, length=");
    Serial.println(cb);
    // We've received a packet, read the data from it
    udp.read(packetBuffer, _PACKET_SIZE); // read the packet into the buffer
    //the timestamp starts at byte 40 of the received packet and is four bytes,
    // or two words, long. First, esxtract the two words:
    unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
    unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
    // combine the four bytes (two words) into a long integer
    // this is  time (seconds since Jan 1 1900):
    unsigned long secsSince1900 = highWord << 16 | lowWord;
    Serial.print("Seconds since Jan 1 1900 = " );
    Serial.println(secsSince1900);
    // now convert  time into everyday time:
    Serial.print("Unix time = ");
    // Unix time starts on Jan 1 1970. In seconds, that's 2208988800:
    const unsigned long seventyYears = 2208988800UL;
    // subtract seventy years:
    epoch = secsSince1900 - seventyYears;
    // print Unix time:
    Serial.println(epoch);
    //ulozeni unixtime
    prubezny_unixtime=epoch;
    // print the hour, minute and second:
    Serial.print("The UTC time is ");       // UTC is the time at Greenwich Meridian (GMT)
    Serial.print((epoch  % 86400L) / 3600); // print the hour (86400 equals secs per day)
    uint32_t hod = (epoch  % 86400L)/3600;
    Serial.print(':');
    if ( ((epoch % 3600) / 60) < 10 ) {
      // In the first 10 minutes of each hour, we'll want a leading '0'
      Serial.print('0');
    }
    Serial.print((epoch  % 3600) / 60); // print the minute (3600 equals secs per minute)
    uint32_t min =  (epoch  % 3600)/60;
    uint16_t sec = (epoch % 60);
    uint32_t timenow = hod*3600 + min*60 + sec ;
    Serial.print(':');
    if ( (epoch % 60) < 10 ) {
      // In the first 10 seconds of each minute, we'll want a leading '0'
      Serial.print('0');
    }
    Serial.println(epoch % 60); // print the second
    Serial.print("interni cas (UTC):");
    Serial.println(timenow);
    Serial.print(hod);
    Serial.print(":");
    Serial.print(min);
    Serial.print(":");
    Serial.println(sec);
    time_z=timenow+timezone*3600;
    if (time_z >= 86400){
      time_z=time_z-86400;
    }
    sync = 1;
    return 0;
  }
}

//funkce pro  ticker nastaveni flagu pro synchro z
void syncenable(){
  syncnow=0;
}


//funkce vypisu casu
void zobraz() {
  uint8_t hod=time_z/3600;
  uint8_t min=(time_z -hod*3600)/60;
  uint8_t sec=(time_z -hod*3600 - min*60);
  Serial.print(hod);
  Serial.print(":");
  Serial.print(min);
  Serial.print(":");
  Serial.println(sec);
}

//minutovy interval
void set_minuta(){
  flag_minuta=1;
}

//zapis uint32_t do EEPROM
void write_to_eeprom(uint32_t integer, uint8_t adresa){
  for (uint8_t i=0; i<4;i++){
    EEPROM.write((adresa+i), (uint8_t) integer);
    EEPROM.commit();
    //Serial.println((uint8_t)integer);
    integer= integer >> 8;
  }
}

//nacteni uint32_t z EEPROM
uint32_t read_from_eeprom(uint8_t adresa){
  uint8_t pole[4];
  for (uint8_t i=0; i<4;i++){
    pole[i]=EEPROM.read(adresa+i);
  }
  /*for (uint8_t test=0; test<4; test++){
    Serial.println(pole[test]);
  }*/
return ((uint32_t)((pole[3] << 24) | (pole[2] << 16) | (pole[1] << 8) | pole[0]));
}

//nastevni pwm
void set_pwm (void){
  Serial.print("Jas:");
  Serial.println(w_jas_docas);
  if (w_state ==0) {
    pwm_set_duty(0, 0); //D5
    pwm_set_duty(0, 4); //D1
  } else {
    pwm_set_duty(w_jas_docas*PWM_MULTIPLY, 0); //D5
    pwm_set_duty(w_jas_docas*PWM_MULTIPLY, 4); //D1
  }
  if (rgb_state==0){
    pwm_set_duty(0, 1); //D6
    pwm_set_duty(0, 2); //D7
    pwm_set_duty(0, 3); //D8
  } else {
    pwm_set_duty(r_jas*PWM_MULTIPLY, 1); //D6
    pwm_set_duty(g_jas*PWM_MULTIPLY, 2); //D7
    pwm_set_duty(b_jas*PWM_MULTIPLY, 3); //D8
  }
  pwm_start();
}

//obsluha webu
void handleWlon(){
  Serial.println("Wlon");
  digitalWrite(LED_BUILTIN, 0);
  w_state=1;
  index();
  server.send(200, "text/html", INDEX_HTML);
  w_jas_docas=w_jas;
  set_pwm();
  digitalWrite(LED_BUILTIN, 1);
  overtemp=0;
}
void handleWloff(){
  Serial.println("Wloff");
  digitalWrite(LED_BUILTIN, 0);
  w_state=0;
  index();
  server.send(200, "text/html", INDEX_HTML);
  set_pwm();
  digitalWrite(LED_BUILTIN, 1);
  //vypnuti budik_up
  budik.detach();
  budik_aktiv=0;
}
void handleRgbon(){
  Serial.println("RGBon");
  digitalWrite(LED_BUILTIN, 0);
  rgb_state=1;
  index();
  server.send(200, "text/html", INDEX_HTML);
  set_pwm();
  digitalWrite(LED_BUILTIN, 1);
  overtemp=0;
}
void handleRgboff(){
  Serial.println("RGBoff");
  digitalWrite(LED_BUILTIN, 0);
  rgb_state=0;
  index();
  server.send(200, "text/html", INDEX_HTML);
  set_pwm();
  digitalWrite(LED_BUILTIN, 1);
}
