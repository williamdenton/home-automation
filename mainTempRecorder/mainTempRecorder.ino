#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <EEPROM.h>
#include <ArduinoMqttClient.h>
#include <SimpleDHT.h>


#define STASSID "ssid"
#define STAPSK "pass"


const char *ssid = STASSID;
const char *password = STAPSK;

const long interval = 10000;
unsigned long previousMillis = 0;

IPAddress ip(192, 168, 1, 208);
IPAddress gateway(192, 168, 1, 1);
IPAddress ipmask(255, 255, 255, 0);
IPAddress dns(192, 168, 1, 2);

WiFiClient wifiClient;
MqttClient mqttClient(wifiClient);

const char broker[] = "192.168.1.2";
int        port     = 1883;

const char topic_current_temperature_prefix[]  = "climate/temperature/";
const char topic_current_humidity_prefix[]  = "climate/humidity/";

SimpleDHT22 dht_1(D1);
SimpleDHT22 dht_2(D2);
SimpleDHT22 dht_3(D3);

const char location_1[] = "outside";
const char location_2[] = "kitchen";
const char location_3[] = "roof";


void setup()
{
  Serial.begin(115200);
  Serial.println("");
  Serial.println("");
  Serial.println("");
  Serial.println("");
  Serial.println("");


/// WIFI Setup
  String mac = WiFi.macAddress();
  Serial.print("MAC address: ");
  Serial.println(mac);

  Serial.print("Connecting to wifi...");

  WiFi.config(ip, gateway, ipmask, dns);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println(" Connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());


/// MQTT Setup
  Serial.print("Attempting to connect to the MQTT broker: ");
  Serial.println(broker);
  mqttClient.setId("mainTempRecorder");  
  // mqttClient.setUsernamePassword("username", "password");

  EnsureConnected();
}

void EnsureConnected(){
  while (WiFi.status() != WL_CONNECTED);

  if (!mqttClient.connected()) {
    if (!mqttClient.connect(broker, port)) {
      Serial.print("MQTT connection failed! Error code = ");
      Serial.println(mqttClient.connectError());
      while (1);
    }
  }
}

void loop()
{
  EnsureConnected();
  mqttClient.poll();

/// only execute once every interval (1 sec ish)
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {
    // save the last time a message was sent
    previousMillis = currentMillis;  
    CollectValues();
  }
///------ 
}

void CollectValues(){

  float humidity = 0;
  float temperature = 0;

  if(takeReading(dht_1, humidity, temperature)){
    PublishValueToTopic(topic_current_temperature_prefix, location_1, temperature);
    PublishValueToTopic(topic_current_humidity_prefix, location_1, humidity);
  }

  if(takeReading(dht_2, humidity, temperature)){
    PublishValueToTopic(topic_current_temperature_prefix, location_2, temperature);
    PublishValueToTopic(topic_current_humidity_prefix, location_2, humidity);
  }

  if(takeReading(dht_3, humidity, temperature)){
    PublishValueToTopic(topic_current_temperature_prefix, location_3, temperature);
    PublishValueToTopic(topic_current_humidity_prefix, location_3, humidity);
  }
}

bool takeReading(SimpleDHT &dht, float &humidity, float &temperature)
{
  int err = SimpleDHTErrSuccess;
  if ((err = dht.read2(&temperature, &humidity, NULL)) != SimpleDHTErrSuccess)
  {
    humidity = NAN;
    temperature = NAN;
    Serial.print("Read DHT failed, err=");
    Serial.println(err);
    return false;
  }
  return true;
}

void PublishValueToTopic(const char *topicPrefix, const char *location, float value) {
  char topic_buffer[128];
  strcpy(topic_buffer, topicPrefix);
  strcat(topic_buffer, location);   

  char value_buffer[5];
  dtostrf(value, 4, 1, value_buffer);


  Serial.print("Sending message to topic: ");
  Serial.println(topic_buffer);
  Serial.println(value_buffer);
  Serial.println("-------");

  mqttClient.beginMessage(topic_buffer);
  mqttClient.print(value_buffer);
  mqttClient.endMessage();
}
