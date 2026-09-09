/*
 * Laserová závora - Arduino SLAVE
 * I2C adresa: 6
 * Cooldown: 16s (tlačítka ignorována)
 * I2C Alert čas (stav 1): 17s
 * Threshold: 20
 * PŘECHODNÝ STAV: Detekce přerušení paprsku je DOČASNĚ DEAKTIVOVÁNA
 */

#include <Wire.h>

const byte I2C_SLAVE_ADDR = 6;

const int PIN_LASER = 9;   
const int PIN_LDR = A0;    
const int PIN_BTN1 = 2;    
const int PIN_BTN2 = 3;    
const int PIN_SWITCH = 12; 

const int LDR_THRESHOLD = 20;        
const unsigned long COOLDOWN = 16000;      
const unsigned long I2C_ALERT_TIME = 17000; 
const int MAX_FAILURES = 5;         
const int SENSOR_STABILIZE = 100;    

unsigned long cooldownTimer = 0;
unsigned long i2cTimer = 0;      
int failCounter = 0;
bool isCoolingDown = false;
bool alignmentMode = false;
bool buttonsPressed = false;
bool laserIsOn = false;

volatile byte i2cStatus = 0;
byte lastStatusPrint = 255;
int ldrValue = 0;

struct I2CPacket {
  byte status;
  byte d1;
  byte d2;
  byte d3;
};
I2CPacket myTelemetry = {0, 0, 0, 0};

void setup() {
  pinMode(PIN_LASER, OUTPUT);
  pinMode(PIN_BTN1, INPUT_PULLUP);
  pinMode(PIN_BTN2, INPUT_PULLUP);
  pinMode(PIN_SWITCH, INPUT_PULLUP);
  
  Wire.begin(I2C_SLAVE_ADDR);
  Wire.onRequest(requestEvent); 
  
  Serial.begin(115200);
  Serial.println(F("--- SYSTEM AKTIVNI (Prechodny stav - Detekce vypnuta) ---"));
  
  attemptToTurnOn();
}

void loop() {
  // Dvojité čtení pro odstranění šumu
  analogRead(PIN_LDR);
  ldrValue = analogRead(PIN_LDR);
  
  bool anyButton = (digitalRead(PIN_BTN1) == LOW || digitalRead(PIN_BTN2) == LOW);
  bool switchActive = (digitalRead(PIN_SWITCH) == LOW);

  // Aktualizace telemetrie pro ESP32
  myTelemetry.status = i2cStatus;
  myTelemetry.d1 = laserIsOn ? 1 : 0;
  myTelemetry.d2 = alignmentMode ? 1 : 0;
  myTelemetry.d3 = (ldrValue > 255) ? 255 : (byte)ldrValue;

  // --- LOGIKA PÁČKY (Stav 3) ---
  if (switchActive) {
    i2cStatus = 3;
    if (!laserIsOn) {
      digitalWrite(PIN_LASER, HIGH);
      laserIsOn = true;
    }
    checkStatusPrint();
    return; 
  } 
  
  if (i2cStatus == 3 && !switchActive) {
    i2cStatus = (millis() - i2cTimer < I2C_ALERT_TIME) ? 1 : 0;
  }

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
    // PŘECHODNÝ STAV: I v kalibraci ignorujeme, jestli senzor něco vidí
    /*
    if (ldrValue > LDR_THRESHOLD) {
      alignmentMode = false;
      failCounter = 0;
    }
    */
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
    // PŘECHODNÝ STAV: Detekce přerušení je dočasně VYPNUTA (zakomentována)
    /*
    if (ldrValue < LDR_THRESHOLD) {
      triggerAlarm();
    }
    */
  }
}

void requestEvent() {
  Wire.write((byte*)&myTelemetry, sizeof(I2CPacket)); 
}

void checkStatusPrint() {
  if (i2cStatus != lastStatusPrint) {
    Serial.print(F("Zmena I2C stavu: ")); Serial.println(i2cStatus);
    lastStatusPrint = i2cStatus;
  }
}

void triggerAlarm() {
  Serial.println(F("PRERUSENO! Poplach."));
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
  
  // PŘECHODNÝ STAV (Placeholder): Ignorujeme senzor a vždy vyhodnotíme zapnutí jako úspěšné
  /* if (checkLDR > LDR_THRESHOLD) { */
    failCounter = 0;
    laserIsOn = true;
    digitalWrite(PIN_LASER, 255);
  /* } else {
    failCounter++;
    Serial.println(F("Chyba trefeni pri zapnuti!"));
    
    if (failCounter >= MAX_FAILURES) {
      digitalWrite(PIN_LASER, LOW);
      laserIsOn = false;
      alignmentMode = true;
      Serial.println(F("Alignment mode aktivni."));
    } else {
      triggerAlarm(); 
    }
  } */
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