/*
 * Laserová závora - Arduino SLAVE
 * I2C adresa: 13
 * Cooldown alarmu: 16s
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
bool laserEnabled = false; // Laser je povolen Masterem (dveře otevřeny > 3s nebo pracovní mód)
bool pracovniMod = false; // Příznak pro pracovní mód

volatile byte i2cStatus = 0;
byte lastStatusPrint = 255;
int ldrValue = 0;
volatile byte i2c_req = 0;
volatile char pendingSerialCmd = 0;
char lastLog[30] = "Start";

// Přidáme bezpečné buffery pro I2C přenos bez Stringů uvnitř přerušení
volatile char safeLebkaStatus[4] = "0";
volatile char safeLebkaData[40] = "0,0,0,0,0,0";
volatile char safeLebkaLog[35] = "Start";

unsigned long lastLebkaPoll = 0;
unsigned long lastLebkaResponseTime = 0;

void pollLebkaBridge() {
  // Odeslání požadavku z fronty z I2C
  if (pendingSerialCmd != 0) {
    lebkaSerial.write(pendingSerialCmd);
    pendingSerialCmd = 0;
  }

  // Příjem dat z Lebky
  static String recvBuffer = "";
  while (lebkaSerial.available()) {
    char c = lebkaSerial.read();
    if (c == '\n') {
      String msg = recvBuffer;
      recvBuffer = "";
      msg.trim();
      if (msg.length() == 0) continue;

      lastLebkaResponseTime = millis();

      if (msg.indexOf(',') != -1) {
        // Telemetrie: stavHry,crystals_mask,k1,k2,k3,lock
        int c1 = msg.indexOf(',');
        int c2 = msg.indexOf(',', c1 + 1);
        int mask = (c2 != -1) ? msg.substring(c1 + 1, c2).toInt() : 0;
        
        noInterrupts();
        strncpy((char*)safeLebkaData, msg.c_str(), 39);
        safeLebkaData[39] = '\0';
        strcpy((char*)safeLebkaStatus, (mask == 7) ? "2" : "0");
        interrupts();
      } else if (msg.startsWith("LOG:")) {
        noInterrupts();
        strncpy((char*)safeLebkaLog, msg.substring(4).c_str(), 34);
        safeLebkaLog[34] = '\0';
        interrupts();
      } else if (msg == "OK") {
        // Potvrzení příkazu
      } else {
        noInterrupts();
        strncpy((char*)safeLebkaStatus, msg.c_str(), 3);
        safeLebkaStatus[3] = '\0';
        interrupts();
      }
    } else {
      if (recvBuffer.length() < 50) {
        recvBuffer += c;
      }
    }
  }

  // Pravidelné cyklické vyčítání z Lebky každých 150 ms
  if (millis() - lastLebkaPoll >= 150) {
    lastLebkaPoll = millis();
    lebkaSerial.write('D');
  }

  // Timeout detekce Lebky (pokud neodpoví déle než 2.5 sekundy, označí se offline)
  if (millis() - lastLebkaResponseTime > 2500 && lastLebkaResponseTime > 0) {
    noInterrupts();
    strcpy((char*)safeLebkaData, "X");
    strcpy((char*)safeLebkaStatus, "X");
    interrupts();
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
  
  // Laser startuje vypnutý, dokud Master nepotvrdí otevření dveří nebo pracovní mód
  digitalWrite(PIN_LASER, LOW);
  laserIsOn = false;
  laserEnabled = false;
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

  // V pracovním módu nebo když je laser vypnutý nesmí být aktivní žádný alarm
  if (pracovniMod || !laserEnabled) {
    i2cStatus = 0;
  }

  checkStatusPrint();

  // Pokud není laser povolen (dveře zavřeny) a nejsme v pracovním módu, laser zůstává vypnutý
  if (!laserEnabled && !pracovniMod) {
    if (laserIsOn) {
      digitalWrite(PIN_LASER, LOW);
      laserIsOn = false;
    }
    return;
  }

  // --- PRIORITA 1: TLAČÍTKA (Fungují VŽDY pro chvilkové vypnutí laseru) ---
  if (anyButton) {
    if (!buttonsPressed) {
      if (laserIsOn) {
        effectFadeOut();
      }
      digitalWrite(PIN_LASER, LOW);
      laserIsOn = false;
      isCoolingDown = false;
      alignmentMode = false;
      buttonsPressed = true;
    }
    digitalWrite(PIN_LASER, LOW);
    return;
  }

  if (buttonsPressed && !anyButton) {
    buttonsPressed = false;
    if (laserEnabled || pracovniMod) {
      attemptToTurnOn();
    }
    return;
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
      if (laserEnabled || pracovniMod) {
        attemptToTurnOn();
      }
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
  } else if (laserEnabled || pracovniMod) {
    attemptToTurnOn();
  }
}

void requestEvent() {
  if (i2c_req == 0x99) {
    Wire.write((const byte*)&myTelemetry, sizeof(DiagLaser)); 
    i2c_req = 0;
  } else if (i2c_req == 0x98) {
    Wire.write((const byte*)lastLog, 30);
    i2c_req = 0;
  } else if (i2c_req == 'S') {
    Wire.write((const uint8_t*)safeLebkaStatus, strlen((char*)safeLebkaStatus));
    i2c_req = 0;
  } else if (i2c_req == 'D') {
    Wire.write((const uint8_t*)safeLebkaData, strlen((char*)safeLebkaData));
    i2c_req = 0;
  } else if (i2c_req == 'L') {
    Wire.write((const uint8_t*)safeLebkaLog, strlen((char*)safeLebkaLog));
    i2c_req = 0;
  } else if (i2c_req == 'A') {
    const char *ack = "OK";
    Wire.write((const uint8_t*)ack, 2);
    i2c_req = 0;
  } else {
    Wire.write(pracovniMod ? (byte)0 : i2cStatus);
  }
}

void receiveEvent(int howMany) {
  while (Wire.available()) {
    byte c = Wire.read();
    if (c == 0x99 || c == 0x98 || c == 'S' || c == 'D' || c == 'L') {
      i2c_req = c;
    } else if (c == 'A') {
      i2c_req = c;
      pendingSerialCmd = 'A';
    } else if (c == '0') {
      pracovniMod = false;
      pendingSerialCmd = '0';
      Log("Herni mod - detekce zapnuta");
    } else if (c == '3') {
      pracovniMod = true;
      i2cStatus = 0;
      laserEnabled = true;
      pendingSerialCmd = '3';
      Log("Pracovni mod - laser zapnut, ignoruje preruseni");
      attemptToTurnOn();
    } else if (c == 'N') { // Zapnout laser (dveře otevřeny > 3s)
      laserEnabled = true;
      if (!laserIsOn && !isCoolingDown && !alignmentMode && !buttonsPressed) {
        attemptToTurnOn();
      }
      Log("Povel: Laser ZAPNUT (dvere otevreny)");
    } else if (c == 'F') { // Vypnout laser (dveře zavřeny)
      laserEnabled = false;
      if (laserIsOn) {
        effectFadeOut();
      }
      digitalWrite(PIN_LASER, LOW);
      laserIsOn = false;
      i2cStatus = 0;
      isCoolingDown = false;
      alignmentMode = false;
      Log("Povel: Laser VYPNUT (dvere zavreny)");
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
  if (pracovniMod) return; // V pracovním módu nikdy nespouštět alarm
  Log("PRERUSENO! Poplach.");
  i2cStatus = 1;      
  i2cTimer = millis(); 
  digitalWrite(PIN_LASER, LOW);
  laserIsOn = false;
  isCoolingDown = true;
  cooldownTimer = millis();
}

void attemptToTurnOn() {
  if (!laserEnabled && !pracovniMod) return; // Pojistka - laser se nezapne, pokud neni povolen

  effectOldBulb(); 
  delay(SENSOR_STABILIZE); 
  
  // Dvojité čtení po zapnutí
  analogRead(PIN_LDR);
  int checkLDR = analogRead(PIN_LDR);
  
  if (checkLDR > LDR_THRESHOLD || pracovniMod) {
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
      if (!pracovniMod) {
        triggerAlarm(); 
      }
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