
#include <SimpleDHT.h>

#ifdef ESP8266

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>

#define STASSID ""
#define STAPSK  ""
#define METRIC_SERVER "192.168.1.2"
#define ARRAY_SIZE 100
#define METRIC_URL "http://192.168.1.2:8081/telegraf"
#define DHTPIN D4     // Digital pin connected to the DHT sensor 

#else
#include <UIPEthernet.h>
byte mac[] = { 0x90, 0xA2, 0xDA, 0x0D, 0x78, 0xEE  };
EthernetClient client;
IPAddress METRIC_SERVER(192, 168, 1, 2);
#define ARRAY_SIZE 20
#define METRIC_PORT 8081
#define METRIC_PATH "/telegraf"
#define DHTPIN 7     // Digital pin connected to the DHT sensor 

#endif


SimpleDHT22 dht22(DHTPIN);



#define REPORT_METRIC_INTERVAL 60000 //ms

const char* LOCATION = "kitchen";

#define DHTTYPE    DHT11     // DHT 11
//#define DHTTYPE    DHT22     // DHT 22 (AM2302)
#define DHTTYPE    DHT21     // DHT 21 (AM2301)


uint32_t min_delay;

float temperatureReadings[ARRAY_SIZE];
float humidityReadings[ARRAY_SIZE];
int readCount;

unsigned long lastReadTime = 0;           // last time the sensor was read, in milliseconds
unsigned long nextMetricSendTime = 0;


struct EnvironmentReading {
  float humidity;
  float temperature;
};


void setup() {
  Serial.begin(115200);

  Serial.println();
  Serial.println();
  Serial.println();

  Serial.println(F("Starting up..."));



#ifdef ESP8266
  WiFi.begin(STASSID, STAPSK);

  Serial.print(F("connecting to Wifi "));
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print(F("Connected! IP address: "));
  Serial.println(WiFi.localIP());

#else
  // start the Ethernet connection and the server:
  Ethernet.begin(mac);
  Serial.print(F("IP Address: "));
  Serial.println(Ethernet.localIP());
#endif

  min_delay = 2000;
  lastReadTime = millis();
  nextMetricSendTime = GetNextSendTime(millis());

  Serial.println(F("Startup complete."));
}

void loop() {
  struct EnvironmentReading reading;

  reading = TakeReading();

  if (isnan(reading.humidity) || isnan(reading.temperature)) {
    return;
  }

  StoreReading(reading);

  if (ShouldSendMetric()) {
    reading = GetAverageReading();

    SendMetric(reading);
  }
}

bool ShouldSendMetric() {
  return millis() >  nextMetricSendTime;
}

long GetNextSendTime(long lastSendTime) {
  return lastSendTime + REPORT_METRIC_INTERVAL;
}

void SendMetric(EnvironmentReading reading) {

  if (isnan(reading.humidity) || isnan(reading.temperature)) {
    nextMetricSendTime = GetNextSendTime(nextMetricSendTime);
    Serial.println(F("Damn, no metrics to report..."));
    return;
  }

  const int bufferSize = 200;
  char jsonBuffer[bufferSize];
  int length = GenerateMetricJson(jsonBuffer, bufferSize, reading);

  Serial.println(jsonBuffer);

  SendJsonMetricOnWire(jsonBuffer, length);

  nextMetricSendTime = GetNextSendTime(nextMetricSendTime);
}

int GenerateMetricJson(char buffer[], int bufferLength, EnvironmentReading reading) {
  char temperature[6]; //12.45<null>
  char humidity[6];
  dtostrf(reading.temperature, 5, 2, temperature);
  dtostrf(reading.humidity, 5, 2, humidity);

  int written = 0;
  written += snprintf_P(buffer + written, bufferLength - written, PSTR("["));
  written += snprintf_P(buffer + written, bufferLength - written, PSTR("{\"location\": \"%s\", \"name\": \"%s\", \"value\": %s}"), LOCATION, "humidity", humidity);
  written += snprintf_P(buffer + written, bufferLength - written, PSTR(","));
  written += snprintf_P(buffer + written, bufferLength - written, PSTR("{\"location\": \"%s\", \"name\": \"%s\", \"value\": %s}"), LOCATION, "temperature", temperature);
  written += snprintf_P(buffer + written, bufferLength - written, PSTR("]"));
  return written;
}

struct EnvironmentReading GetAverageReading() {
  struct EnvironmentReading reading;
  reading.humidity = CalculateAverage(humidityReadings, readCount);
  reading.temperature = CalculateAverage(temperatureReadings, readCount);
  readCount = 0;
  return reading;
}

void StoreReading(EnvironmentReading reading) {
  //on smaller chips the memory is constrained, start writing from the middle of the array for new data
  if (readCount >= ARRAY_SIZE) {
    readCount = ARRAY_SIZE / 2;
    Serial.print(F("Uhoh, wraping array back to half way @"));
    Serial.println(readCount);
  }
  
  temperatureReadings[readCount] = reading.temperature;
  humidityReadings[readCount] = reading.humidity;
  readCount++;
}

//get the average reading, excluding the max and min values which might be outliers
float CalculateAverage(float reading[], int length) {
  float min = 1000.0;
  float max = -1000.0;
  for (int i = 0; i < length; i++) {
    if (reading[i] > max) max = reading[i];
    if (reading[i] < min) min = reading[i];
  }

  float sum = 0;
  int count = 0;
  bool minExcluded = false, maxExcluded = false;
  for (int i = 0; i < length; i++) {
    Serial.print(reading[i]);

    if ((reading[i] == min) && !minExcluded) {
      minExcluded = true;
      Serial.print(F("(*min) "));
      continue;
    }

    if ((reading[i] == max) && !maxExcluded) {
      maxExcluded = true;
      Serial.print(F("(*max) "));
      continue;
    }

    Serial.print(F(" "));
    sum += reading[i];
    count++;
  }

  float avg = sum / count;
  Serial.print(F("Avg = "));
  Serial.println(avg);
  return avg;
}


struct EnvironmentReading TakeReading() {
  int waitTime = min_delay - (millis() - lastReadTime);
  if (waitTime > 0) {
    //    Serial.print(F("Delaying "));
    //    Serial.print(waitTime);
    //    Serial.println(F("ms before taking environment reading"));
    delay(waitTime);
  }

  struct EnvironmentReading reading;

  int err = SimpleDHTErrSuccess;
  if ((err = dht22.read2(&reading.temperature, &reading.humidity, NULL)) != SimpleDHTErrSuccess) {
    reading.humidity = NAN;
    reading.temperature = NAN;
    Serial.print("Read DHT22 failed, err="); Serial.println(err);
    delay(2000);
  }

  //  if (reading.humidity != NaN && reading.temperature != NaN) {
  //    Serial.print(F("Humidity: "));
  //    Serial.print(reading.humidity);
  //    Serial.print(F("%"));
  //    Serial.print(F(" Temperature: "));
  //    Serial.print(reading.temperature);
  //    Serial.println(F("°C"));
  //  }

  lastReadTime = millis();
  return reading;
}




#ifdef ESP8266
void SendJsonMetricOnWire(char* json, int length) {

  WiFiClient client;
  HTTPClient http;

  http.begin(client, METRIC_URL);
  http.addHeader("Content-Type", "application/json");
  int httpCode = http.POST(String(json));


  if (httpCode >= 200 && httpCode <= 299) {
    Serial.println("Metrics uploaded");
  } else {
    Serial.printf("[HTTP] POST... failed, error: %s\n", http.errorToString(httpCode).c_str());
    const String& payload = http.getString();
    Serial.println("received payload:\n<<");
    Serial.println(payload);
    Serial.println(">>");
  }

  http.end();
}

#else

// this method makes a HTTP connection to the server:
void SendJsonMetricOnWire(char* json, int length) {

  // if there's a successful connection:
  if (client.connect(METRIC_SERVER, METRIC_PORT)) {
    Serial.println("sending stat... ");
    // send the HTTP request:
    client.println("POST "METRIC_PATH" HTTP/1.0");
    client.println("Connection: close");
    client.println("Content-Type: application/json");
    client.print("Content-Length: ");
    client.println(length);
    client.println();
    client.write(json, length);
    client.println();
    client.flush();

    delay(500);

    if (client.available()) {
      client.read();
    }

    client.stop();

  } else {
    // if you couldn't make a connection:
    Serial.println("connection failed");
  }
}


#endif
