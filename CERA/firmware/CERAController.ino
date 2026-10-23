#include <DHT.h>

// CERA controller adaptation based on the supplied project sketch.
// Test only with a low-voltage prototype. Never use as a safety controller.

#define DHT_PIN 2
#define DHT_TYPE DHT11
#define DUST_LED_PIN 3
#define DUST_SENSOR_PIN A0
#define RELAY_motor 11
#define RELAY_pump 12
#define BUZZER 6

#define LOW_DUST 20
#define HIGH_DUST 70
#define RELAY_ON LOW
#define RELAY_OFF HIGH
#define HOT_TEMP 32.0
#define DRY_HUMIDITY 45.0

const unsigned long SAMPLE_INTERVAL = 1000;
const unsigned long DHT_INTERVAL = 2000;
const unsigned long NORMAL_RESPONSE_MS = 6000;
const unsigned long DRY_RESPONSE_MS = 4000;
const int DUST_CHANGE_MARGIN = 3;
const float REQUIRED_IMPROVEMENT = 0.10;

DHT dht(DHT_PIN, DHT_TYPE);

enum OperatingMode { MODE_STANDBY, MODE_AUTO, MODE_MANUAL };
OperatingMode operatingMode = MODE_STANDBY;

float temperature = NAN, humidity = NAN;
int dustRaw = 0, previousDust = -1, blowerStartDust = -1;
bool blowerOn = false, pumpOn = false, sensorError = false;
bool manualBlower = false, manualPump = false;
int risingCount = 0, lowDustCount = 0;
unsigned long lastSample = 0, lastDHTRead = 0, blowerStartTime = 0;
String dustTrend = "STARTING", statusText = "INITIALIZING";

void setBlower(bool on) {
  blowerOn = on;
  digitalWrite(RELAY_motor, on ? RELAY_ON : RELAY_OFF);
}
void setPump(bool on) {
  pumpOn = on;
  digitalWrite(RELAY_pump, on ? RELAY_ON : RELAY_OFF);
}
void equipmentOff() { setBlower(false); setPump(false); }
void startBlower() {
  if (!blowerOn) {
    setBlower(true); blowerStartTime = millis(); blowerStartDust = dustRaw; risingCount = 0;
    Serial.println("EVENT:BLOWER_STARTED");
  }
}

int readDust() {
  digitalWrite(DUST_LED_PIN, LOW);
  delayMicroseconds(280);
  int value = analogRead(DUST_SENSOR_PIN);
  delayMicroseconds(40);
  digitalWrite(DUST_LED_PIN, HIGH);
  delayMicroseconds(968);
  return value;
}

void readSensors() {
  dustRaw = readDust();
  if (millis() - lastDHTRead >= DHT_INTERVAL || lastDHTRead == 0) {
    float newTemp = dht.readTemperature();
    float newHumidity = dht.readHumidity();
    if (!isnan(newTemp) && !isnan(newHumidity)) {
      temperature = newTemp; humidity = newHumidity;
    } else { temperature = NAN; humidity = NAN; }
    lastDHTRead = millis();
  }
  bool dhtError = isnan(temperature) || isnan(humidity);
  bool dustError = (dustRaw <= 0 || dustRaw >= 1023);
  sensorError = dhtError || dustError;
}

void updateTrend() {
  if (previousDust < 0) { dustTrend = "STARTING"; previousDust = dustRaw; return; }
  if (dustRaw > previousDust + DUST_CHANGE_MARGIN) { dustTrend = "INCREASING"; risingCount++; }
  else if (dustRaw < previousDust - DUST_CHANGE_MARGIN) { dustTrend = "DECREASING"; risingCount = 0; }
  else { dustTrend = "STABLE"; risingCount = 0; }
  previousDust = dustRaw;
}

void controlSystem() {
  if (sensorError) {
    equipmentOff(); digitalWrite(BUZZER, HIGH); statusText = "SENSOR_ERROR"; return;
  }
  digitalWrite(BUZZER, LOW);
  bool hotAndDry = temperature >= HOT_TEMP && humidity <= DRY_HUMIDITY;
  unsigned long responseTime = hotAndDry ? DRY_RESPONSE_MS : NORMAL_RESPONSE_MS;

  if (dustRaw < LOW_DUST) {
    lowDustCount++;
    if (lowDustCount >= 3) { equipmentOff(); statusText = "LOW_DUST_EQUIPMENT_OFF"; }
    else statusText = "VERIFYING_LOW_DUST";
    return;
  }
  lowDustCount = 0;
  if (!blowerOn) { startBlower(); statusText = "BLOWER_STARTING"; return; }
  unsigned long elapsed = millis() - blowerStartTime;
  if (risingCount >= 2 && dustRaw >= HIGH_DUST) {
    setPump(true); statusText = "RISING_DUST_PUMP_ON"; return;
  }
  if (elapsed >= responseTime) {
    float improvement = 0.0;
    if (blowerStartDust > 0) improvement = (float)(blowerStartDust - dustRaw) / (float)blowerStartDust;
    bool ineffective = dustRaw >= HIGH_DUST && improvement < REQUIRED_IMPROVEMENT;
    if (ineffective) { setPump(true); statusText = "BLOWER_INEFFECTIVE_PUMP_ON"; }
    else if (pumpOn) { statusText = "PUMP_ASSISTING"; }
    else statusText = hotAndDry ? "BLOWER_RUNNING_HOT_DRY" : "BLOWER_EFFECTIVE_OR_MONITORING";
  } else statusText = hotAndDry ? "BLOWER_TEST_HOT_DRY" : "BLOWER_TESTING";
}

void printData() {
  Serial.print("DATA,");
  if (isnan(temperature)) Serial.print("NA,"); else { Serial.print(temperature, 1); Serial.print(","); }
  if (isnan(humidity)) Serial.print("NA,"); else { Serial.print(humidity, 1); Serial.print(","); }
  Serial.print(dustRaw); Serial.print(","); Serial.print(dustTrend); Serial.print(",");
  Serial.print(blowerOn ? 1 : 0); Serial.print(","); Serial.print(pumpOn ? 1 : 0); Serial.print(",");
  Serial.print(statusText); Serial.print(","); Serial.println(sensorError ? 1 : 0);
  // Human-readable logs are ignored by the backend; retained for Serial Monitor debugging.
  Serial.print("Temperature: "); if (isnan(temperature)) Serial.println("ERROR"); else { Serial.print(temperature); Serial.println(" C"); }
  Serial.print("Humidity: "); if (isnan(humidity)) Serial.println("ERROR"); else { Serial.print(humidity); Serial.println(" %"); }
  Serial.print("Dust RAW: "); Serial.println(dustRaw);
  Serial.print("Dust trend: "); Serial.println(dustTrend);
  Serial.print("Blower: "); Serial.println(blowerOn ? "ON" : "OFF");
  Serial.print("Pump: "); Serial.println(pumpOn ? "ON" : "OFF");
  Serial.print("Status: "); Serial.println(statusText); Serial.println("--------------------------------------");
}

void sendAck(const String &command, const String &result) {
  Serial.print("ACK,"); Serial.print(command); Serial.print(","); Serial.println(result);
}

void handleSerialCommands() {
  if (!Serial.available()) return;
  String cmd = Serial.readStringUntil('\n');
  cmd.trim(); cmd.toUpperCase();
  if (cmd == "MODE,AUTO") {
    operatingMode = MODE_AUTO; sendAck("MODE_AUTO", "ACCEPTED");
  } else if (cmd == "MODE,STANDBY") {
    operatingMode = MODE_STANDBY; equipmentOff(); statusText = "STANDBY"; sendAck("MODE_STANDBY", "ACCEPTED");
  } else if (cmd == "MODE,MANUAL") {
    operatingMode = MODE_MANUAL; sendAck("MODE_MANUAL", "ACCEPTED");
  } else if (cmd == "BLOWER,ON" && operatingMode == MODE_MANUAL) {
    manualBlower = true; setBlower(true); statusText = "MANUAL_BLOWER_ON"; sendAck("BLOWER_ON", "ACCEPTED");
  } else if (cmd == "BLOWER,OFF" && operatingMode == MODE_MANUAL) {
    manualBlower = false; setBlower(false); statusText = "MANUAL_BLOWER_OFF"; sendAck("BLOWER_OFF", "ACCEPTED");
  } else if (cmd == "PUMP,ON" && operatingMode == MODE_MANUAL) {
    manualPump = true; setPump(true); statusText = "MANUAL_PUMP_ON"; sendAck("PUMP_ON", "ACCEPTED");
  } else if (cmd == "PUMP,OFF" && operatingMode == MODE_MANUAL) {
    manualPump = false; setPump(false); statusText = "MANUAL_PUMP_OFF"; sendAck("PUMP_OFF", "ACCEPTED");
  } else {
    sendAck("UNKNOWN_OR_BLOCKED", "REJECTED");
  }
}

void setup() {
  Serial.begin(9600);
  Serial.setTimeout(30);
  digitalWrite(RELAY_motor, RELAY_OFF); digitalWrite(RELAY_pump, RELAY_OFF);
  pinMode(RELAY_motor, OUTPUT); pinMode(RELAY_pump, OUTPUT);
  pinMode(DHT_PIN, INPUT); pinMode(DUST_LED_PIN, OUTPUT); pinMode(DUST_SENSOR_PIN, INPUT); pinMode(BUZZER, OUTPUT);
  digitalWrite(DUST_LED_PIN, HIGH); digitalWrite(BUZZER, LOW);
  equipmentOff(); dht.begin();
  Serial.println("CERA Controller Ready");
  Serial.println("CSV: DATA,temp,humidity,dust,trend,blower,pump,status,error");
  Serial.println("MODE: STANDBY");
}

void loop() {
  handleSerialCommands();
  if (millis() - lastSample < SAMPLE_INTERVAL) return;
  lastSample = millis();
  readSensors(); updateTrend();

  if (operatingMode == MODE_STANDBY) {
    equipmentOff(); digitalWrite(BUZZER, LOW); statusText = "STANDBY";
  } else if (operatingMode == MODE_AUTO) {
    controlSystem();
  } else { // Manual mode: sensor fault forces outputs off as a conservative prototype policy.
    if (sensorError) { equipmentOff(); digitalWrite(BUZZER, HIGH); statusText = "SENSOR_ERROR"; }
    else { digitalWrite(BUZZER, LOW); statusText = "MANUAL_MODE"; }
  }
  printData();
}