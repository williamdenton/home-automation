#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <EEPROM.h>
#include <ESP8266WebServer.h>
#include <SimpleDHT.h>
#include <uri/UriBraces.h>

#define STASSID "ssid"
#define STAPSK "pass"
#define DHTPIN D4 // Digital pin connected to the DHT sensor
#define HEATER_PIN D1

// https://github.com/PJCzx/homebridge-thermostat

const char *ssid = STASSID;
const char *password = STAPSK;

#define CHILD1

#if CHILD4
IPAddress ip(192, 168, 1, 200);
const char *LOCATION = "child4-bedroom";
#elif CHILD3
IPAddress ip(192, 168, 1, 201);
const char *LOCATION = "child3-bedroom";
#elif CHILD1
IPAddress ip(192, 168, 1, 202);
const char *LOCATION = "child1-bedroom";
#elif CHILD2
IPAddress ip(192, 168, 1, 203);
const char *LOCATION = "child2-bedroom";
#elif KITCHEN
IPAddress ip(192, 168, 1, 204);
const char *LOCATION = "kitchen";
#endif

IPAddress gateway(192, 168, 1, 1);
IPAddress ipmask(255, 255, 255, 0);
IPAddress dns(192, 168, 1, 2);

ESP8266WebServer server(80);
SimpleDHT22 dht_a(DHTPIN);

enum HeatingCoolingState
{
  Off = 0,
  Heat = 1,
  Cool = 2,
  Auto = 3
};

const float DEFAULT_STARTUP_TEMPERATURE = 17.5;
float _targetTemperature = 0;

unsigned long lastTemperatureReadingTime_a = 0;
unsigned long lastSuccessfulTemperatureReadingTime_a = 0;
float _temperature = NAN;
float _humidity = NAN;

bool _heaterOn = false;
enum HeatingCoolingState _targetHeatingCoolingState = Off;
enum HeatingCoolingState _currentHeatingCoolingState = Off;

const int minHeaterDutyTime = 5 * 60 * 1000; // 5 minutes off or on, no flapping
unsigned long lastHeaterControlTime;

const int responseBufferSize = 300;
char responseBuffer[responseBufferSize];

void setup()
{
  Serial.begin(115200);
  Serial.println("");
  Serial.println("");
  Serial.println("");
  Serial.println("");
  Serial.println("");

  Serial.print("MAC address: ");
  Serial.println(WiFi.macAddress());
  String mac = WiFi.macAddress();

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

  _targetTemperature = DEFAULT_STARTUP_TEMPERATURE;

  resetDutyCycleForImmediateChange();
  delay(2000);

  Serial.println("Starting HTTP Server");

  server.on("/status", HTTP_GET, handleStatus);
  server.on("/metrics", HTTP_GET, handleMetrics);
  server.on(UriBraces("/targetHeatingCoolingState/{}"), HTTP_GET, handleTargetHeatingCoolingState);
  server.on(UriBraces("/targetTemperature/{}"), HTTP_GET, handleTargetTemperature);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("HTTP server started.");
}

void loop()
{
  // put your main code here, to run repeatedly:
  recordTemperature();
  controlHeater();

  if (WiFi.status() != WL_CONNECTED)
  {
    return;
  }

  server.handleClient();
}

void recordTemperature()
{
  if (lastTemperatureReadingTime_a + 2000 > millis())
  {
    //too soon to read sensor again
    return;
  }

  float humidity = 0;
  float temperature = 0;
  bool success = takeReading(dht_a, humidity, temperature);
  lastTemperatureReadingTime_a = millis();

  if (success)
  {
    lastSuccessfulTemperatureReadingTime_a = lastTemperatureReadingTime_a;
    _humidity = humidity;
    _temperature = temperature;
  }

  if (lastSuccessfulTemperatureReadingTime_a + 30000 < millis())
  {
    //destroy cached values
    _humidity = NAN;
    _temperature = NAN;
  }
}

void controlHeater()
{
  bool targetHeaterState = _targetHeatingCoolingState == Heat && _targetTemperature > _temperature;
  bool dutyCycleMet = (lastHeaterControlTime + minHeaterDutyTime) < millis();

  if (isnan(_temperature) && _heaterOn)
  {
    dutyCycleMet = true;
    targetHeaterState = false;
    Serial.println("No reliable reading from temperature guage, turning heater off");
  }

  if (dutyCycleMet == false && _targetHeatingCoolingState != Off)
  {
    return;
  }

  if (_heaterOn != targetHeaterState)
  {
    lastHeaterControlTime = millis();
    Serial.print("Turning heater ");
    Serial.println(targetHeaterState ? "on" : "off");
  }

  _heaterOn = targetHeaterState;
  _currentHeatingCoolingState = _targetHeatingCoolingState;

  if (_heaterOn)
  {
    pinMode(HEATER_PIN, OUTPUT);
  }
  else
  {
    pinMode(HEATER_PIN, INPUT_PULLUP);
  }
}

void resetDutyCycleForImmediateChange()
{
  lastHeaterControlTime = -minHeaterDutyTime;
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

void handleStatus()
{
  Serial.println("handleStatus");

  char target_temperature_str[5];
  char current_temperature_str[5];
  char current_humidity_str[5];
  dtostrf(_targetTemperature, 4, 1, target_temperature_str);
  dtostrf(_temperature, 4, 1, current_temperature_str);
  dtostrf(_humidity, 4, 1, current_humidity_str);

  if(isnan(_temperature) || isnan(_humidity)){
    server.send(500, "text/plain", "temperature or humidity reading error");
    Serial.println("temperature or humidity error, returning 500 for metrics");
    return;
  }

  int written = 0;
  written += snprintf_P(responseBuffer + written, responseBufferSize - written, PSTR("{\n"));
  written += snprintf_P(responseBuffer + written, responseBufferSize - written, PSTR("  \"targetHeatingCoolingState\": %d,\n"), _targetHeatingCoolingState);
  written += snprintf_P(responseBuffer + written, responseBufferSize - written, PSTR("  \"targetTemperature\": %s,\n"), target_temperature_str);
  written += snprintf_P(responseBuffer + written, responseBufferSize - written, PSTR("  \"currentHeatingCoolingState\": %d,\n"), _currentHeatingCoolingState);
  written += snprintf_P(responseBuffer + written, responseBufferSize - written, PSTR("  \"currentTemperature\": %s,\n"), current_temperature_str);
  written += snprintf_P(responseBuffer + written, responseBufferSize - written, PSTR("  \"currentRelativeHumidity\": %s\n"), current_humidity_str);
  written += snprintf_P(responseBuffer + written, responseBufferSize - written, PSTR("}\n"));
  server.send(200, "application/json", responseBuffer);
  Serial.println(responseBuffer);
}

void handleMetrics()
{
  Serial.println("handleMetrics");

  char current_temperature_str[5];
  char current_humidity_str[5];
  dtostrf(_temperature, 4, 1, current_temperature_str);
  dtostrf(_humidity, 4, 1, current_humidity_str);

  if(isnan(_temperature) || isnan(_humidity)){
    server.send(500, "text/plain", "temperature or humidity reading error");
    Serial.println("temperature or humidity error, returning 500 for metrics");
    return;
  }

  int written = 0;
  written += snprintf_P(responseBuffer + written, responseBufferSize - written, PSTR("[\n"));
  written += snprintf_P(responseBuffer + written, responseBufferSize - written, PSTR("  {\"location\": \"%s\", \"name\": \"%s\", \"value\": %s}\n"), LOCATION, "humidity", current_humidity_str);
  written += snprintf_P(responseBuffer + written, responseBufferSize - written, PSTR(" ,{\"location\": \"%s\", \"name\": \"%s\", \"value\": %s}\n"), LOCATION, "temperature", current_temperature_str);
  written += snprintf_P(responseBuffer + written, responseBufferSize - written, PSTR(" ,{\"location\": \"%s\", \"name\": \"%s\", \"value\": %s}\n"), LOCATION, "heater", _heaterOn ? "1" : "0");
  written += snprintf_P(responseBuffer + written, responseBufferSize - written, PSTR("]\n"));
  server.send(200, "application/json", responseBuffer);
  Serial.println(responseBuffer);
}

void handleTargetHeatingCoolingState()
{
  Serial.println("handleTargetHeatingCoolingState");

  int desiredState = server.pathArg(0).toInt();
  bool isChanged = desiredState != _targetHeatingCoolingState;
  _targetHeatingCoolingState = (HeatingCoolingState)desiredState;

  int written = 0;
  written += snprintf_P(responseBuffer + written, responseBufferSize - written, PSTR("Setting heater mode to "));
  switch (_targetHeatingCoolingState)
  {
  case Off:
    written += snprintf_P(responseBuffer + written, responseBufferSize - written, PSTR("off"));
    break;
  case Heat:
    written += snprintf_P(responseBuffer + written, responseBufferSize - written, PSTR("heat"));
    break;
  case Cool:
    written += snprintf_P(responseBuffer + written, responseBufferSize - written, PSTR("cool"));
    break;
  case Auto:
    written += snprintf_P(responseBuffer + written, responseBufferSize - written, PSTR("auto"));
    break;
  };
  if (!isChanged)
  {
    written += snprintf_P(responseBuffer + written, responseBufferSize - written, PSTR(" (no change)"));
  }
  written += snprintf_P(responseBuffer + written, responseBufferSize - written, PSTR("\n"));

  server.send(200, "text/plain", responseBuffer);
  Serial.println(responseBuffer);

  resetDutyCycleForImmediateChange();
}

void handleTargetTemperature()
{
  Serial.println("handleTargetTemperature");

  float desiredTemperature = server.pathArg(0).toFloat();
  _targetTemperature = desiredTemperature;

  int written = 0;
  written += snprintf_P(responseBuffer + written, responseBufferSize - written, PSTR("Setting temperature to %f°C\n"), desiredTemperature);
  server.send(200, "text/plain", responseBuffer);
  Serial.println(responseBuffer);

  resetDutyCycleForImmediateChange();
}

void handleNotFound()
{
  Serial.println("handleNotFound");
  server.send(404, "text/plain", "nope");
}