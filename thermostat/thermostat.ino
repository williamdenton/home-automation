#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ArduinoMqttClient.h>
#include <SimpleDHT.h>


#define STASSID "ssid"
#define STAPSK "pass"


const char ssid[] = STASSID;
const char password[] = STAPSK;

const char broker[] = "192.168.1.2";
int        port     = 1883;

char location[] = "child";

const int topicSize = 64;
char topic_current_temperature[topicSize];
char topic_current_humidity[topicSize];
char topic_set_target_temperature[topicSize];
char topic_get_target_temperature[topicSize];
char topic_set_target_mode[topicSize];
char topic_get_target_mode[topicSize];
char topic_online[topicSize];
char topic_heater_on[topicSize];
char topic_get_fault[topicSize];

WiFiClient wifiClient;
MqttClient mqttClient(wifiClient);
SimpleDHT22 dht(D1);

const int minHeaterDutyTime = 1 * 60 * 1000; // 1 minutes off or on, no flapping
long lastHeaterControlTime;

float targetTemperature = 17.0;
float currentTemperature = 0;
bool targetIsHeating = false;
bool isHeating = false;

void CombineIntoBuffer(char *topic, const char* prefix, const char* location ){
  strcpy(topic, prefix);
  strcat(topic, location);
}

void setup()
{
  Serial.begin(115200);
  Serial.println("");
  Serial.println("");
  Serial.println("");
  Serial.println("");
  Serial.println("");
  
  snprintf_P(topic_heater_on,              topicSize, PSTR("xclimate/heater-on/%s"),                 location);
  snprintf_P(topic_current_temperature,    topicSize, PSTR("xclimate/temperature/%s"),               location);
  snprintf_P(topic_current_humidity,       topicSize, PSTR("xclimate/humidity/%s"),                  location);
  snprintf_P(topic_set_target_temperature, topicSize, PSTR("thermostat/set-target-temperature/%s"), location);
  snprintf_P(topic_get_target_temperature, topicSize, PSTR("thermostat/get-target-temperature/%s"), location);
  snprintf_P(topic_set_target_mode,        topicSize, PSTR("thermostat/set-target-mode/%s"),        location);
  snprintf_P(topic_get_target_mode,        topicSize, PSTR("thermostat/get-target-mode/%s"),        location);
  snprintf_P(topic_online,                 topicSize, PSTR("thermostat/get-online/%s"),             location);

  char hostname[128];
  snprintf_P(hostname, sizeof(hostname), PSTR("thermostat-%s"), location);
  
  ResetDutyCycleForImmediateChange();


/// WIFI Setup
  String mac = WiFi.macAddress();
  Serial.print("MAC address: ");
  Serial.println(mac);

  Serial.print("Connecting to wifi...");
  WiFi.setHostname(hostname);
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
  mqttClient.setId(hostname);  
  // mqttClient.setUsernamePassword("username", "password");

  ConfigureLastWillAndTestament();
  EnsureConnected();

  mqttClient.onMessage(onMqttMessage);
  mqttClient.subscribe(topic_set_target_temperature);
  mqttClient.subscribe(topic_set_target_mode);
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

void ConfigureLastWillAndTestament() {
  char willPayload[] = "false";
  bool willRetain = true;
  int willQos = 1;

  mqttClient.beginWill(topic_online, willRetain, sizeof(willPayload), willQos);
  mqttClient.print(willPayload);
  mqttClient.endWill();

}

const long interval = 10000;
unsigned long previousMillis = 0;
void loop()
{
  EnsureConnected();
  mqttClient.poll();

/// only execute once every interval (10 sec ish)
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {
    // save the last time a message was sent
    previousMillis = currentMillis;  
    CollectValues();
  }
///------ 
  ControlHeater();
}

int consecutiveBadReadings = 0;
void CollectValues(){

  float humidity = 0;
  float temperature = 0;

  if (takeReading(dht, humidity, temperature)) {
    currentTemperature = temperature;

    PublishFloatToTopic(topic_current_temperature, temperature);
    PublishFloatToTopic(topic_current_humidity, humidity);
    PublishValueToTopic(topic_online, "true");

    consecutiveBadReadings = 0;
  } else {
    consecutiveBadReadings++;
  }

  if (consecutiveBadReadings > 10 ) {
    PublishValueToTopic(topic_online, "false");
    currentTemperature = 0;
  }

  PublishFloatToTopic(topic_heater_on, isHeating ? 1.0 : 0.0);
}

bool takeReading(SimpleDHT &dht, float &humidity, float &temperature)
{
  int err = SimpleDHTErrSuccess;
  if ((err = dht.read2(&temperature, &humidity, NULL)) != SimpleDHTErrSuccess) {
    humidity = NAN;
    temperature = NAN;
    Serial.print("Read DHT failed, err=");
    Serial.println(err);
    return false;
  }
  return true;
}

void PublishFloatToTopic(const char *topic, float value) {
  char value_buffer[5];
  dtostrf(value, 4, 1, value_buffer);
  PublishValueToTopic(topic, value_buffer);
}

void PublishValueToTopic(const char *topic, char* value) {
  Serial.print("Sending: ");
  Serial.print(topic);
  Serial.print(" = ");
  Serial.println(value);

  mqttClient.beginMessage(topic);
  mqttClient.print(value);
  mqttClient.endMessage();
}


void onMqttMessage(int messageSize) {

  String receivedTopic = mqttClient.messageTopic();

  uint8_t messageBuffer[messageSize];
  mqttClient.read(messageBuffer, messageSize);

  Serial.print("Receive: ");
  Serial.print(receivedTopic);
  Serial.print(" = ");
  Serial.println((char*)messageBuffer);


  if (receivedTopic == topic_set_target_temperature ) {
    HandleSetTemperature((char*)messageBuffer);
  } else if (receivedTopic ==  topic_set_target_mode) {
    HandleSetMode((char*)messageBuffer);
  } else {
      Serial.println("Error: unknown topic.");
      return;
  }
}

void HandleSetMode(char* message) {
  Serial.print("Received mode ");
  Serial.print(message);
  Serial.println(".");

  if (strcmp(message,"HEAT") == 0) {
    Serial.println("Setting Mode to HEAT");
    targetIsHeating = true;
  } else {
    Serial.println("Setting Mode to OFF");
    targetIsHeating = false;
  }
  ResetDutyCycleForImmediateChange();
  PublishValueToTopic(topic_get_target_mode, message);
}

void HandleSetTemperature(char* message) {
  float targetTemp = atof(message);
  Serial.print("Setting temperature to ");
  Serial.print(targetTemp);
  Serial.println(".");

  targetTemperature = targetTemp;
  ResetDutyCycleForImmediateChange();
  PublishFloatToTopic(topic_get_target_temperature, targetTemp);
}


void TurnHeaterOn() {
  isHeating = true;
  RecordDutyTime();
  Serial.println("Turning heater on.");
}

void TurnHeaterOff() {
  isHeating = false;
  RecordDutyTime();
  Serial.println("Turning heater off.");
}

void ResetDutyCycleForImmediateChange()
{
  lastHeaterControlTime = -minHeaterDutyTime;
}

void RecordDutyTime(){
  lastHeaterControlTime = millis();
}
bool IsDutyCycleMet(){
  return (lastHeaterControlTime + minHeaterDutyTime) < millis();
}

void ControlHeater()
{
  if (!targetIsHeating || currentTemperature == 0) {
    if (isHeating) {
      TurnHeaterOff();
    }
    return;
  }

  if (!IsDutyCycleMet()) {
    return;
  }

  bool isCold = targetTemperature > currentTemperature;
  if (isCold && !isHeating) {
    TurnHeaterOn(); 
  } else if (!isCold && isHeating) {
    TurnHeaterOff();
  }

}
