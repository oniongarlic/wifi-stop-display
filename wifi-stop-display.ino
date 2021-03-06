/*
WiFi-KotiBussi - Wireless, realtime, buss stop display
Copyright (C) 2014-2015  Kaj-Michael Lang

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>

#include <Wire.h> 
#include <LiquidCrystal_I2C.h>
#include <PubSubClient.h>

#include <ArduinoJson.h>
#include <Time.h>
#include <DS1307RTC.h> 

LiquidCrystal_I2C lcd(0x27,2,1,0,4,5,6,7);

// Adjust if needed
#define LCD_COLS (20)
#define LCD_ROWS (4)
#define BACKLIGHT_PIN     3

#include "wifisetup.h"

// Web interface data
#ifdef USE_CDN
#define JQUERY_JS "<script src=\"//code.jquery.com/jquery-1.11.3.min.js\"></script>"
#define BOOTSTRAP_CSS "<link rel=\"stylesheet\" href=\"https://maxcdn.bootstrapcdn.com/bootstrap/3.3.5/css/bootstrap.min.css\" integrity=\"sha512-dTfge/zgoMYpP7QbHy4gWMEGsbsdZeCXz7irItjcC3sPUFtf0kuFbDz/ixG7ArTxmDjLXDmezHubeNikyKGVyQ==\" crossorigin=\"anonymous\">"
#define BOOTSTRAP_JS "<script src=\"https://maxcdn.bootstrapcdn.com/bootstrap/3.3.5/js/bootstrap.min.js\" integrity=\"sha512-K1qjQ+NcF2TYO/eI3M6v8EiNYZfA95pQumfvcVrTHtwQVDG+aHRqLi/ETn2uB+1JqwYqVG3LIvdm9lj6imS/pQ==\" crossorigin=\"anonymous\"></script>"
#else 
#define JQUERY_JS "http://api.tal.org/jquery/2.1.4/jquery-2.1.4.min.js"
#define BOOTSTRAP_CSS "<link rel=\"stylesheet\" href=\"http://api.tal.org/bootstrap/3.3.5/css/bootstrap-theme.min.css\">"
#define BOOTSTRAP_JS "<script src=\"http://api.tal.org/bootstrap/3.3.5/js/bootstrap.min.js\"></script>"
#endif

const char* hdr1="<html><head><meta name=\"viewport\" content=\"maximum-scale=1,width=device-width,initial-scale=1,user-scalable=0\"><title>KotiBussi V2</title>";
const char* hdr2="</head><body>";
const char* js=JQUERY_JS BOOTSTRAP_CSS BOOTSTRAP_JS;
const char* ftr="</body></html>";

// WiFi configuration
const char* ssid = WIFI_SSID;
const char* password = WIFI_KEY;

// Current stop area and id information
String ctopic;
String area="Turku";
String stop_id="1170";

// Max amount of data to store
#define DATA_MAX (8)

struct StationData {
  String ref;
  String dest;
  int eta;
  bool m;
};

int dataCount=0;
StationData data[DATA_MAX];

bool refresh=true;

MDNSResponder mdns;
ESP8266WebServer server(80);

WiFiClient wifiClient;
PubSubClient client(wifiClient);

// Heartbeat
unsigned long previousMillis = 0;
const long interval = 1000;
int ledState = LOW;     

void printZeroPadded(int n) {
  if (n<10)
    lcd.print("0");
  lcd.print(n);
}

void updateLines() {
  int i,s,e,r;

  lcd.clear();
  lcd.setCursor(LCD_COLS-stop_id.length(), 0);
  lcd.print(stop_id);

  printRTC();

  if (dataCount==0) {
    lcd.setCursor(5,2);
    lcd.print("Waiting");
    return;
  }

  // XXX: Scroll trough the lines, but check if 0 is arriwing soon (under 1 min?) then display only 0-3
  s=0;
  if (dataCount>4)
    e=4;
  else
    e=dataCount;
  r=1;
  
  for (i=s;i<e;i++) {
    lcd.setCursor(0, r);
    lcd.print(data[i].ref);
    lcd.print(" ");
    lcd.print(data[i].dest);
    if (data[i].eta<(60*60))
      printTimeAt(15, r, data[i].eta);
    r++;
    if (r>3)
      break;
  }
}

void printTimeAt(int c, int r, int t) {
  lcd.setCursor(c, r);
  if (t<0) {
    lcd.print("--:--");
    return;
  }  
  
  int m=t/60;
  int s=t-(m*60);

  printZeroPadded(m);
  lcd.print(":");
  printZeroPadded(s);
}

void callback(char* topic, byte* payload, unsigned int length) {
  String t(topic);

#if 0
  Serial.println(t);
  Serial.println((char *)payload);
#endif
  
  if (length>2048) {
    Serial.println("OSP!");
    return;
  }
  
  payload[length] = '\0';
  //String d=(char *)payload;
  
  // siri/turku/sm/<stop-id>/<offset>/<data>
  //               ^
  int pos_id=t.indexOf(stop_id);
  if (pos_id==-1)
    return;

  // XXX Check topic!

#if 0    
  int pos_ofs=t.indexOf('/', pos_id);
  if (pos_ofs==-1)
    return;
  int pos_ofe=t.indexOf('/', pos_ofs);
  if (pos_ofe==-1)
    return;

  // Get the data offset
  int offset=t.substring(pos_ofs, pos_ofe).toInt();
  if  (offset<0 || offset>15) {
    Serial.println("InvOfs");
    return;
  }
  
  int pos_data=t.lastIndexOf('/');
  if (pos_data==-1)
    return;
#endif

  // Our data is an array of objects, in order
  StaticJsonBuffer<2048> jsonBuffer;
  JsonArray& root = jsonBuffer.parseArray((char *)payload);

  if (!root.success()) {
    Serial.println("*");
    return;
  }

  int cnt=root.size();
  Serial.println(cnt);

  if (cnt>DATA_MAX)
    cnt=DATA_MAX;

  String tmp;

  dataCount=cnt;

  for (int i=0;i<cnt;i++) {
    JsonObject& a = root[i].asObject();

    const char *ref = a["r"];
    const char *dest = a["d"];
    const char *eta = a["eta"];

    tmp=eta;
    data[i].ref=ref;
    data[i].dest=dest;
    data[i].eta=tmp.toInt();
  }

  refresh=true;
}

void updateETA()
{
  for (int i=0;i<dataCount;i++) {
    data[i].eta--;
    if (data[i].eta<0)
      data[i].eta=0;
  }
}

void sendHeader(int c=200) {
  server.send(c, "text/html", hdr1);
  server.sendContent(js);
  server.sendContent(hdr2);
}

void handleRoot() {
  sendHeader();
  server.sendContent("<h1>KotiBussi V2 - Config</h1><p>");
  server.sendContent("<form method='post' action='/stop'><label>Stop ID:</label>");
  server.sendContent("<input name='stopid' type='text' value='' required='required' placeholder='Enter a stop id' /><br/><input type='submit' value='Set' />");
  server.sendContent("</form></p><h3>Current config:</h3><p>");
  server.sendContent("Area: " + area + "<br/>Stop: "+stop_id+"</p>");
  server.sendContent("Device ID:"+ESP.getChipId());
  server.sendContent(ftr);
}

void handleTime()
{
  if (server.hasArg("h")==true) {
    tmElements_t tm;
    tm.Hour = server.arg("h").toInt();
    tm.Minute = server.arg("mi").toInt();
    tm.Second = 0;
    tm.Day = server.arg("h").toInt();
    tm.Month = server.arg("mo").toInt();
    tm.Year = server.arg("y").toInt();

    if (RTC.write(tm))
      server.send(200, "text/html", "<h1>Error</h1>Time set");
    else
      server.send(200, "text/html", "<h1>Error</h1>Failed to set time");
    return;
  }

  sendHeader();
  server.sendContent( "<h1>Time</h1><p>");
  server.sendContent("<form method='post' action='/time'><label>Time:</label>");
  server.sendContent("<input name='h' type='text' value='' required='required' size='2' placeholder='hh' />:");
  server.sendContent("<input name='mi' type='text' value='' required='required' size='2' placeholder='mm' /> ");
  server.sendContent("<input name='d' type='text' value='' required='required' size='2' placeholder='dd' />-");
  server.sendContent("<input name='mo' type='text' value='' required='required' size='2' placeholder='mm' />-");
  server.sendContent("<input name='y' type='text' value='' required='required' size='4' placeholder='yyyy' /> ");
  server.sendContent("<br/><input type='submit' value='Set' />");
  server.sendContent("</form></p>");
  server.sendContent(ftr);
}

void handleSetStop()
{
  if (server.hasArg("stopid")==false) {
    server.send(200, "text/html", "<h1>Error</h1>Stop ID not set");
    return;
  }

  stop_id=server.arg("stopid");
  // XXX validate
  subscribeCurrentStop();

  sendHeader();
  
  server.sendContent("<h1>KotiBussi V2 - Config</h1><p>Stop set to: "+stop_id);
  server.sendContent("<br/><a href='/'>Back</a>");
  server.sendContent(ftr);
  lcd.clear();
  lcd.print("New stop: ");
  lcd.print(stop_id);
}

void handleNotFound() {
  server.send(404, "text/plain", "Not Found");
}

void setSubscribeTopic()
{
  ctopic="siri/"+area+"/sm/"+stop_id+"/0/json";
}

/**
 * Subscribe to new stop, we subscribe to the whole sub-tree of the stop so 
 * we automatically will get all upcoming busses.
 * If we had a previous valid topic, umsubscribe it
 */
void subscribeCurrentStop()
{
  if (ctopic.length()>0) {
    client.unsubscribe(ctopic.c_str());
  }

  dataCount=0;
  refresh=true;
 
  setSubscribeTopic();
  client.subscribe(ctopic.c_str());
}

void connectWiFi()
{
    // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    lcd.print(".");
  }
  
  Serial.println(ssid);
  Serial.println(WiFi.localIP());

  lcd.clear();
  lcd.print("WiFi OK");
  lcd.setCursor(0, 1);
  lcd.print(WiFi.localIP());

  lcd.setCursor(0, 2);
  if (mdns.begin("kotibussi", WiFi.localIP())) {
    lcd.print(" - MDNS OK");
  } else {
    lcd.print(" - MDNS ER");
  }
}

void setup(void){
  pinMode(BUILTIN_LED, OUTPUT);

  // Note: Use 0.2 on ESP-01
  Wire.begin(5, 4);
  
  Serial.begin(115200);
  Serial.println("KotiBussi starting");
  Serial.print("ID: ");
  Serial.print(ESP.getChipId());
  delay(1000);

  setSyncProvider(RTC.get);

  lcd.setBacklightPin(BACKLIGHT_PIN, POSITIVE);
  lcd.setBacklight(BACKLIGHT_ON); 
  lcd.begin(LCD_COLS, LCD_ROWS);
  lcd.clear();
  lcd.print("WKB");
  
  WiFi.begin(ssid, password);
  Serial.println("CW");
  
  lcd.setCursor(0, 1);
  lcd.print("Connecting");

  connectWiFi();

  delay(500);
  
  server.on("/", handleRoot);
  server.on("/stop", handleSetStop);
  server.on("/time", handleTime);

  server.on("/reconnect", [](){
    server.send(200, "text/plain", "mqttdis");
    client.disconnect();
  });

  server.onNotFound(handleNotFound);
  
  server.begin();
  Serial.println("HTTP server started");

  client.setServer(MQTT_SERVER, 1883);
  client.setCallback(callback);
}

int mqttreconnect() {
  int c=0;
  lcd.print("MQTT");
  Serial.print("MQTT");
  
  while (!client.connected()) {
    c++;  
    // Attempt to connect
    if (client.connect("talorg-kb")) {
      lcd.print(" OK");
      Serial.println("connected");          
      subscribeCurrentStop();
      refresh=true;
      return 0;
    }
    lcd.setCursor(0,3);
    lcd.print(c);
    lcd.print(" ER=");
    lcd.print(client.state());
    
    Serial.print(" ER=");
    Serial.println(client.state());    
    
    return 1;
  }
}

void networkRefresh(void) {
  server.handleClient();
  if (!client.connected()) {
    mqttreconnect();
  }
  client.loop();
  mdns.update();
}

void printRTC()
{
    lcd.setCursor(0, 0);
    lcd.print(hour());
    lcd.print(":");
    printZeroPadded(minute());
    lcd.print(":");
    printZeroPadded(second());
}

void blink()
{
  unsigned long currentMillis = millis();
  if(currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;   
    if (ledState == LOW)
      ledState = HIGH;  // Note that this switches the LED *off*
    else
      ledState = LOW;   // Note that this switches the LED *on*

    printRTC();
    updateETA();
    refresh=true;
 
    digitalWrite(BUILTIN_LED, ledState);
  }
}

void loop(void){
  networkRefresh();
  if (refresh==true) {
    updateLines();
    refresh=false;
  }
  blink();
}
