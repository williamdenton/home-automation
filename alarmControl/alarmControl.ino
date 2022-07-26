#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <EEPROM.h>
#include <ArduinoMqttClient.h>


#define STASSID "ssidt"
#define STAPSK "pass"


#define ALARM_PANEL_ARMED D1


//https://github.com/arachnetech/homebridge-mqttthing/blob/HEAD/docs/Accessories.md#garage-door-opener


const char *ssid = STASSID;
const char *password = STAPSK;

const long interval = 1000;
unsigned long previousMillis = 0;

IPAddress ip(192, 168, 1, 209);
IPAddress gateway(192, 168, 1, 1);
IPAddress ipmask(255, 255, 255, 0);
IPAddress dns(192, 168, 1, 2);

WiFiClient wifiClient;
MqttClient mqttClient(wifiClient);

const char broker[] = "192.168.1.2";
int        port     = 1883;
const char topic_set_target_state[]  = "alarm/set-target-state";
const char topic_get_target_state[]  = "alarm/get-target-state";
const char topic_get_current_state[]  = "alarm/get-current-state";
const char topic_current_state[]  = "alarm/current-state";

const char ARM_AWAY[] = "AA";
const char DISARMED[] = "D";
const char DISARM_AWAY[] = "DA";
const char TRIGGERED[] = "T";


int alarmPanelArmed = 0;
int alarmPanelTriggered = 0;
//alarm outputs
//1 siren
//2 = custom - armed
//3 flasher
//relay = sirens running

//zone 7 - keyswitch stay
//zone 8 - keyswitch disarm

enum OnOffState {
  unknown,
  on,
  off
};


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
  mqttClient.setId("alarmduino");  
  // mqttClient.setUsernamePassword("username", "password");

  if (!mqttClient.connect(broker, port)) {
    Serial.print("MQTT connection failed! Error code = ");
    Serial.println(mqttClient.connectError());

    while (1);
  }

  mqttClient.onMessage(onMqttMessage);
  mqttClient.subscribe(topic_set_target_state);

  pinMode(ALARM_PANEL_ARMED, INPUT);

}

OnOffState currentArmedState = unknown;
OnOffState currentSirenState = unknown;

void loop()
{
  if (WiFi.status() != WL_CONNECTED) { return; }

  mqttClient.poll();

/// only execute once every interval (1 sec ish)
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {
    // save the last time a message was sent
    previousMillis = currentMillis;  
    ProcessAlarmState();
  }
///------ 
}

const char* previousState = "?";

void ProcessAlarmState() {

  const char* currentState = GetAlarmState();

  if (previousState != currentState){
    PublishGetTargetState(currentState);
    previousState = currentState;
  }

  PublishCurrentState(currentState);
}

const char* GetAlarmState() {
  
  OnOffState armedState = ReadArmedState();
  OnOffState sirenState = ReadSirenState();

  if (sirenState == on ) {
    //set triggered
    return TRIGGERED;
  } else if (armedState == on) {
    //set armed   
    return ARM_AWAY;
  } else if (armedState == off) {
    //set disarmed   
    return DISARMED;
  } else {
    //uh oh
    return "?";
  }

}

void PublishGetTargetState(const char *state){
  PublishState(state, topic_get_target_state);
}

void PublishSetTargetState(const char *state){
  PublishState(state, topic_set_target_state);
}

void PublishCurrentState(const char *state){
  PublishState(state, topic_current_state);
}

void PublishState(const char *state, const char *topic) {

  Serial.print("Sending message to topic: ");
  Serial.println(topic);
  Serial.println(state);
  Serial.println("-------");

  mqttClient.beginMessage(topic);
  mqttClient.print(state);
  mqttClient.endMessage();
}

OnOffState ReadArmedState() {
  int val = digitalRead(ALARM_PANEL_ARMED);

  Serial.print("Reading state: ");
  Serial.println(val);


  return val==0
    ? off
    : on;
}

OnOffState ReadSirenState() {
  return off;
}

void ArmAlarmPanel(){
  alarmPanelArmed = 1;
}

void DisarmAlarmPanel() {
  alarmPanelArmed = 0;
}

const int messageBufferSize = 10;
uint8_t messageBuffer[messageBufferSize];

void onMqttMessage(int messageSize) {

  String topic = mqttClient.messageTopic();

  if(topic != topic_set_target_state){
    Serial.println("Message received for unknown topic!");
    return;
  }

  int bytesToReadIntoBuffer = messageSize > messageBufferSize
    ? messageBufferSize
    : messageSize;

  mqttClient.read(messageBuffer, bytesToReadIntoBuffer);

  if (strcmp((char *) messageBuffer, ARM_AWAY) == 0) {
    Serial.println("Arming...");
    ArmAlarmPanel();
  } else if (strcmp((char *) messageBuffer, DISARM_AWAY) == 0) {
    Serial.println("Disarming...");
    DisarmAlarmPanel();
  } else {
    Serial.print("Unknown command! ");
    Serial.println((char *) messageBuffer);
    //unknown command, do nothingz
  }
}