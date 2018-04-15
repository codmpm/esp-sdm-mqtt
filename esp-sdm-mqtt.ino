/*
    esp-sdm-mqtt - patrik.mayer@codm.de

    Work in progress sketch to read power values via modbus from the well know
    SDMXXX (SDM120 / SDM220 / SDM630) series power meters from eastron over modbus
    using an esp8266 and an 3.3V RS485 tranceiver.

    This uses the SDM library by reaper7 https://github.com/reaper7/SDM_Energy_Meter
    mqtt client from https://github.com/knolleary/pubsubclient/
    Tasker from https://github.com/sticilface/Tasker
    
    Arduino OTA from ESP8266 Arduino Core V2.4

    As this is not finished yet, the documentation will get better by time.

*/

#include <Tasker.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <SDM.h>

//--------- Configuration
// WiFi
const char* ssid = "<your-wifi-ssid>";
const char* password = "<your-wifi-key>";

const char* mqttServer = "<mqtt-broker-ip-or-host>";
const char* mqttUser = "<mqtt-user>";
const char* mqttPass = "<mqtt-password>";
const char* mqttClientName = "<mqtt-client-id>"; //will also be used hostname and OTA name
const char* mqttTopicPrefix = "<mqtt-topic-prefix>";

unsigned long measureDelay = 10000; //read every 60s

const int rxPin = 4;
const int txPin = 5;
const int derePin = 14;
const long baud = 9600;

// internal vars
WiFiClient espClient;
PubSubClient mqttClient(espClient);
Tasker tasker;
SDM<baud, rxPin, txPin, derePin> sdm;

char mqttTopicStatus[64];
char mqttTopicIp[64];

char mqttTopicVoltage[64];
char mqttTopicCurrent[64];
char mqttTopicPower[64];
char mqttTopicFreq[64];

long lastReconnectAttempt = 0; //For the non blocking mqtt reconnect (in millis)

void setup() {
  Serial.begin(115200);                                                         //initialize serial
  Serial.println("Starting...");

  sdm.begin();

  //put in mqtt prefix
  sprintf(mqttTopicStatus, "%sstatus", mqttTopicPrefix);
  sprintf(mqttTopicIp, "%sip", mqttTopicPrefix);

  sprintf(mqttTopicVoltage, "%svoltage", mqttTopicPrefix);
  sprintf(mqttTopicCurrent, "%scurrent", mqttTopicPrefix);
  sprintf(mqttTopicPower, "%spower", mqttTopicPrefix);
  sprintf(mqttTopicFreq, "%sfreq", mqttTopicPrefix);

  setup_wifi();
  mqttClient.setServer(mqttServer, 1883);

  tasker.setInterval(meassureSDM, measureDelay);


  //----------- OTA
  ArduinoOTA.setHostname(mqttClientName);

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_SPIFFS
      type = "filesystem";
    }
    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
    Serial.println("Start updating " + type);
  });

  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
    delay(1000);
    ESP.restart();
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
  ArduinoOTA.begin();

}

void loop() {

  //handle mqtt connection, non-blocking
  if (!mqttClient.connected()) {
    long now = millis();
    if (now - lastReconnectAttempt > 5000) {
      lastReconnectAttempt = now;
      // Attempt to reconnect
      if (MqttReconnect()) {
        lastReconnectAttempt = 0;
      }
    }
  }
  mqttClient.loop();

  tasker.loop();

  //handle OTA
  ArduinoOTA.handle();

}

void meassureSDM(int) {

  float v = sdm.readVal(SDM220T_VOLTAGE);                                       //read voltage
  float c = sdm.readVal(SDM220T_CURRENT);                                       //read curren
  float p = sdm.readVal(SDM220T_POWER);                                         //read power
  float f = sdm.readVal(SDM220T_FREQUENCY);                                     //read frequency

  if (v != NAN) {
    mqttClient.publish(mqttTopicVoltage, String(v, 2).c_str(), true);
  }

  if (c != NAN) {
    mqttClient.publish(mqttTopicCurrent, String(c, 2).c_str(), true);
  }

  if (p != NAN) {
    mqttClient.publish(mqttTopicPower, String(p, 2).c_str(), true);
  }

  if (f != NAN) {
    mqttClient.publish(mqttTopicFreq, String(f, 2).c_str(), true);
  }

  Serial.printf("\nVoltage:   %sV\nCurrent:   %sA\nPower:     %sW\nFrequency: %sHz\n", String(v, 2).c_str(), String(c, 2).c_str(), String(p, 2).c_str(), String(f, 2).c_str());
  Serial.println();

}

void setup_wifi() {

  delay(10);

  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA); //disable AP mode, only station
  WiFi.hostname(mqttClientName);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}


bool MqttReconnect() {

  if (!mqttClient.connected()) {

    Serial.print("Attempting MQTT connection...");

    // Attempt to connect with last will retained
    if (mqttClient.connect(mqttClientName, mqttUser, mqttPass, mqttTopicStatus, 1, true, "offline")) {

      Serial.println("connected");

      // Once connected, publish an announcement...
      char curIp[16];
      sprintf(curIp, "%d.%d.%d.%d", WiFi.localIP()[0], WiFi.localIP()[1], WiFi.localIP()[2], WiFi.localIP()[3]);

      mqttClient.publish(mqttTopicStatus, "online", true);
      mqttClient.publish(mqttTopicIp, curIp, true);

    } else {
      Serial.print("failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" try again in 5 seconds");
    }
  }
  return mqttClient.connected();
}
