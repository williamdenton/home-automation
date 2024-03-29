#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <EEPROM.h>
#include <ArduinoMqttClient.h>


#define STASSID "ssidt"
#define STAPSK "pass"


#define ALARM_PANEL_ARMED D1
#define ALARM_PANEL_SIREN D2
#define ALARM_PANEL_ARM D7
#define ALARM_PANEL_DISARM D6



//https://github.com/arachnetech/homebridge-mqttthing/blob/HEAD/docs/Accessories.md#garage-door-opener


const char *ssid = STASSID;
const char *password = STAPSK;


WiFiClient wifiClient;
MqttClient mqttClient(wifiClient);

const char broker[] = "192.168.1.2";
int port = 1883;
const char topic_set_target_state[] = "alarm/set-target-state";
const char topic_get_target_state[] = "alarm/get-target-state";
const char topic_get_current_state[] = "alarm/get-current-state";
const char topic_current_state[] = "alarm/current-state";

const char ARM_AWAY[] = "AA";
const char DISARMED[] = "D";
const char DISARM[] = "D";
const char DISARM_AWAY[] = "DA";
const char TRIGGERED[] = "T";


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


void setup() {
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

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(" Connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());


  /// MQTT Setup
  Serial.print("Connecting to MQTT broker: ");
  Serial.println(broker);
  mqttClient.setId("alarmduino");
  // mqttClient.setUsernamePassword("username", "password");

  if (!mqttClient.connect(broker, port)) {
    Serial.print("MQTT connection failed! Error code = ");
    Serial.println(mqttClient.connectError());
    while (1)
      ;
  }

  mqttClient.onMessage(onMqttMessage);
  mqttClient.subscribe(topic_set_target_state);


  pinMode(ALARM_PANEL_ARM, OUTPUT);
  pinMode(ALARM_PANEL_DISARM, OUTPUT);


  pinMode(ALARM_PANEL_ARMED, INPUT_PULLDOWN_16);
  pinMode(ALARM_PANEL_SIREN, INPUT_PULLDOWN_16);

  attachInterrupt(digitalPinToInterrupt(ALARM_PANEL_ARMED), onAlarmStateChanged, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ALARM_PANEL_SIREN), onAlarmStateChanged, CHANGE);

  ProcessAlarmState();
}

ICACHE_RAM_ATTR void onAlarmStateChanged() {
  Serial.println("Alarm State changed!");
  ProcessAlarmState();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED){
    Serial.println(wl_status_to_string(WiFi.status()));
    return;
  }

  ensureMqttConnection();

  //end the momentary push of the keyswitch after a delay (if they are on)
  EndDisarmAlarmPanel();
  EndArmAlarmPanel();

  mqttClient.poll();
}

void ensureMqttConnection(){
  //dont bother to try and reconnect, just force the reboot
  if (!mqttClient.connected()){
    Serial.println("NO MQTT");
    Serial.println("triggering watchdog reboot...");
    while(true);
  }
}


const char* wl_status_to_string(wl_status_t status) {
  switch (status) {
    case WL_NO_SHIELD: return "WL_NO_SHIELD";
    case WL_IDLE_STATUS: return "WL_IDLE_STATUS";
    case WL_NO_SSID_AVAIL: return "WL_NO_SSID_AVAIL";
    case WL_SCAN_COMPLETED: return "WL_SCAN_COMPLETED";
    case WL_CONNECTED: return "WL_CONNECTED";
    case WL_CONNECT_FAILED: return "WL_CONNECT_FAILED";
    case WL_CONNECTION_LOST: return "WL_CONNECTION_LOST";
    case WL_DISCONNECTED: return "WL_DISCONNECTED";
  }
  return "WL?? Unknown";
}


const char *previousState = "?";

void ProcessAlarmState() {

  const char *currentState = GetAlarmState();

  if (previousState != currentState) {
    PublishGetTargetState(currentState);
    previousState = currentState;
  }

  PublishCurrentState(currentState);
}

const char *GetAlarmState() {

  OnOffState armedState = ReadArmedState();
  OnOffState sirenState = ReadSirenState();

  if (sirenState == on) {
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

void PublishGetTargetState(const char *state) {
  PublishState(state, topic_get_target_state);
}

void PublishSetTargetState(const char *state) {
  PublishState(state, topic_set_target_state);
}

void PublishCurrentState(const char *state) {
  PublishState(state, topic_current_state);
}

void PublishState(const char *state, const char *topic) {

  Serial.print("Sending message to topic: ");
  Serial.print(topic);
  Serial.print(" = ");
  Serial.println(state);

  mqttClient.beginMessage(topic);
  mqttClient.print(state);
  mqttClient.endMessage();
}

OnOffState ReadArmedState() {
  int val = digitalRead(ALARM_PANEL_ARMED);

  Serial.print("Reading Armed state: ");
  Serial.println(val);

  return val == 0
           ? off
           : on;
}

OnOffState ReadSirenState() {
  int val = digitalRead(ALARM_PANEL_SIREN);

  Serial.print("Reading Siren state: ");
  Serial.println(val);

  return val == 0
           ? off
           : on;
}

const long keySwitchPushDuration = 500;
long timeArmStarted = 0;
void BeginArmAlarmPanel() {
  Serial.println("Sending Keyswitch ARM monentary signal (ON)");
  timeArmStarted = millis();
  digitalWrite(ALARM_PANEL_ARM, 1);
}
void EndArmAlarmPanel() {
  if (timeArmStarted == 0) { return; }
  if ((millis() - timeArmStarted) > keySwitchPushDuration) {
    Serial.println("Sending Keyswitch ARM monentary signal (OFF)");
    digitalWrite(ALARM_PANEL_ARM, 0);
    timeArmStarted = 0;
  }
}

long timeDisarmStarted = 0;
void BeginDisarmAlarmPanel() {
  Serial.println("Sending Keyswitch DISARM monentary signal (ON)");
  timeDisarmStarted = millis();
  digitalWrite(ALARM_PANEL_DISARM, 1);
}
void EndDisarmAlarmPanel() {
  if (timeDisarmStarted == 0) { return; }
  if ((millis() - timeDisarmStarted) > keySwitchPushDuration) {
    digitalWrite(ALARM_PANEL_DISARM, 0);
    timeDisarmStarted = 0;
    Serial.println("Sending Keyswitch DISARM monentary signal (OFF)");
  }
}

const int messageBufferSize = 10;
uint8_t messageBuffer[messageBufferSize];

void onMqttMessage(int messageSize) {

  String topic = mqttClient.messageTopic();

  if (topic != topic_set_target_state) {
    Serial.println("Message received for unknown topic!");
    return;
  }

  int bytesToReadIntoBuffer = messageSize > messageBufferSize
                                ? messageBufferSize
                                : messageSize;

  mqttClient.read(messageBuffer, bytesToReadIntoBuffer);

  if (strcmp((char *)messageBuffer, ARM_AWAY) == 0) {
    Serial.println("Arming...");
    BeginArmAlarmPanel();
  } else if (strcmp((char *)messageBuffer, DISARM_AWAY) == 0) {
    Serial.println("Disarming...");
    BeginDisarmAlarmPanel();
  } else if (strcmp((char *)messageBuffer, DISARM) == 0) {
    Serial.println("Disarming...");
    BeginDisarmAlarmPanel();
  } else {
    Serial.print("Unknown command! ");
    Serial.println((char *)messageBuffer);
    //unknown command, do nothingz
  }
}