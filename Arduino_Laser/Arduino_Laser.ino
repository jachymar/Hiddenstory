/*
 * Laserová závora - Arduino SLAVE
 * I2C adresa: 13
 * Cooldown: 16s (tlačítka ignorována)
 * I2C Alert čas (stav 1): 17s
 * Threshold: 20
 */

#include <Wire.h>
#include <SoftwareSerial.h>

const byte I2C_SLAVE_ADDR = 13;
const int LEBKA_RX_PIN = 10; // Laser RX <- Lebka TX
const int LEBKA_TX_PIN = 11; // Laser TX -> Lebka RX
SoftwareSerial lebkaSerial(LEBKA_RX_PIN, LEBKA_TX_PIN);

const int PIN_LASER = 9;   
const int PIN_LDR = A0;    
const int PIN_BTN1 = 2;    
const int PIN_BTN2 = 3;    
// Tlačítko (páčka) módu odstraněno

const int LDR_THRESHOLD = 20;        
const unsigned long COOLDOWN = 16000;      
const unsigned long I2C_ALERT_TIME = 17000; 
const int MAX_FAILURES = 5;         
const int SENSOR_STABILIZE = 100;    

struct DiagLaser {
  uint8_t status;
  uint8_t laser_on;
  uint16_t ldr_val;
  uint8_t fails;
} __attribute__((packed));

DiagLaser myTelemetry;

unsigned long cooldownTimer = 0;
unsigned long i2cTimer = 0;      
int failCounter = 0;
bool isCoolingDown = false;
bool alignmentMode = false;
bool buttonsPressed = false;
bool laserIsOn = false;
bool pracovniMod = false; // Příznak pro pracovní mód

volatile byte i2cStatus = 0;
byte lastStatusPrint = 255;
int ldrValue = 0;
volatile byte i2c_req = 0;
char lastLog[30] = "Start";
String lebkaData = "0";
String lebkaLog = "Start";
char bridgeCommand = 0;

void pollLebkaBridge() {
  while (lebkaSerial.available()) {
    String msg = lebkaSerial.readStringUntil('\n');
    msg.trim();
    if (msg.length() == 0) continue;

    if (bridgeCommand == 'S') {
      lebkaData = msg;
    } else if (bridgeCommand == 'D') {
      lebkaData = msg;
    } else if (bridgeCommand == 'L') {
      lebkaLog = msg;
    }

    bridgeCommand = 0;
  }
}

void setup() {
  pinMode(PIN_LASER, OUTPUT);
  pinMode(PIN_BTN1, INPUT_PULLUP);
  pinMode(PIN_BTN2, INPUT_PULLUP);
  
  Wire.begin(I2C_SLAVE_ADDR);
  Wire.onRequest(requestEvent); 
  Wire.onReceive(receiveEvent);

  lebkaSerial.begin(9600);
  
  Serial.begin(115200);
  Serial.println(F("--- SYSTEM AKTIVNI ---"));
  
  attemptToTurnOn();
}

void loop() {
  pollLebkaBridge();

  // Dvojité čtení pro odstranění šumu
  analogRead(PIN_LDR);
  ldrValue = analogRead(PIN_LDR);
  
  bool anyButton = (digitalRead(PIN_BTN1) == LOW || digitalRead(PIN_BTN2) == LOW);

  // Aktualizace telemetrie pro ESP32
  myTelemetry.status = i2cStatus;
  myTelemetry.laser_on = laserIsOn ? 1 : 0;
  myTelemetry.ldr_val = ldrValue;
  myTelemetry.fails = failCounter;

  // --- LOGIKA I2C ALARMU ---
  if (i2cStatus == 1 && (millis() - i2cTimer >= I2C_ALERT_TIME)) {
    i2cStatus = 0;
  }

  checkStatusPrint();

  // --- PRIORITA 1: TLAČÍTKA (Povolena pouze pokud NEBĚŽÍ cooldown) ---
  if (!isCoolingDown) {
    if (anyButton) {
      if (!buttonsPressed && laserIsOn) {
        effectFadeOut();
        laserIsOn = false;
      }
      digitalWrite(PIN_LASER, LOW);
      buttonsPressed = true;
      return;
    }

    if (buttonsPressed && !anyButton) {
      buttonsPressed = false;
      attemptToTurnOn();
      return;
    }
  }

  // --- PRIORITA 2: ALIGNMENT MÓD ---
  if (alignmentMode) {
    digitalWrite(PIN_LASER, 255);
    laserIsOn = true;
    if (ldrValue > LDR_THRESHOLD) {
      alignmentMode = false;
      failCounter = 0;
      Log("Laser srovnan.");
    }
    return;
  }

  // --- PRIORITA 3: COOLDOWN ---
  if (isCoolingDown) {
    if (millis() - cooldownTimer >= COOLDOWN) {
      isCoolingDown = false;
      attemptToTurnOn();
    }
    return;
  }

  // --- NORMÁLNÍ PROVOZ ---
  if (laserIsOn) {
    if (ldrValue < LDR_THRESHOLD) {
      if (!pracovniMod) {
        triggerAlarm();
      }
    }
  }
}

void requestEvent() {
  if (i2c_req == 0x99) {
    Wire.write((byte*)&myTelemetry, sizeof(DiagLaser)); 
    i2c_req = 0;
  } else if (i2c_req == 0x98) {
    Wire.write((byte*)lastLog, 30);
    i2c_req = 0;
  } else if (i2c_req == 'S' || i2c_req == 'D' || i2c_req == 'L' || i2c_req == 'A') {
    if (i2c_req == 'S') {
      Wire.write((const uint8_t*)lebkaData.c_str(), lebkaData.length());
    } else if (i2c_req == 'D') {
      Wire.write((const uint8_t*)lebkaData.c_str(), lebkaData.length());
    } else if (i2c_req == 'L') {
      Wire.write((const uint8_t*)lebkaLog.c_str(), lebkaLog.length());
    } else if (i2c_req == 'A') {
      const char *ack = "OK";
      Wire.write((const uint8_t*)ack, 2);
    }
    i2c_req = 0;
  } else {
    Wire.write(i2cStatus);
  }
}

void receiveEvent(int howMany) {
  while (Wire.available()) {
    byte c = Wire.read();
    if (c == 0x99 || c == 0x98) {
      i2c_req = c;
    } else if (c == 'A' || c == 'S' || c == 'D' || c == 'L') {
      bridgeCommand = (char)c;
      i2c_req = c;
      lebkaSerial.write((char)c);
      delay(10);
      pollLebkaBridge();
    } else if (c == '0') {
      pracovniMod = false;
      Log("Herni mod - detekce zapnuta");
    } else if (c == '3') {
      pracovniMod = true;
      Log("Pracovni mod - laser ignoruje preruseni");
    }
  }
}

void checkStatusPrint() {
  if (i2cStatus != lastStatusPrint) {
    char msg[25];
    sprintf(msg, "Zmena I2C stavu: %d", i2cStatus);
    Log(msg);
    lastStatusPrint = i2cStatus;
  }
}

void triggerAlarm() {
  Log("PRERUSENO! Poplach.");
  i2cStatus = 1;      
  i2cTimer = millis(); 
  digitalWrite(PIN_LASER, LOW);
  laserIsOn = false;
  isCoolingDown = true;
  cooldownTimer = millis();
}

void attemptToTurnOn() {
  effectOldBulb(); 
  delay(SENSOR_STABILIZE); 
  
  // Dvojité čtení po zapnutí
  analogRead(PIN_LDR);
  int checkLDR = analogRead(PIN_LDR);
  
  if (checkLDR > LDR_THRESHOLD) {
    failCounter = 0;
    laserIsOn = true;
    digitalWrite(PIN_LASER, 255);
  } else {
    failCounter++;
    Log("Chyba trefeni pri zapnuti!");
    
    if (failCounter >= MAX_FAILURES) {
      digitalWrite(PIN_LASER, LOW);
      laserIsOn = false;
      alignmentMode = true;
      Log("Alignment mode aktivni.");
    } else {
      triggerAlarm(); 
    }
  }
}

// --- EFEKTY ---

void effectOldBulb() {
  for(int i=0; i<2; i++) {
    analogWrite(PIN_LASER, 60);
    delay(20);
    analogWrite(PIN_LASER, 0);
    delay(10);
  }
  for(int i = 0; i <= 255; i += 25) {
    analogWrite(PIN_LASER, i);
    delay(5);
  }
  digitalWrite(PIN_LASER, 255);
}

void effectFadeOut() {
  for (int i = 255; i >= 0; i -= 15) {
    analogWrite(PIN_LASER, i);
    delay(5);
  }
  digitalWrite(PIN_LASER, LOW);
}

void Log(const char* txt) {
  strncpy(lastLog, txt, 29);
  lastLog[29] = '\0';
  Serial.println(txt);
}