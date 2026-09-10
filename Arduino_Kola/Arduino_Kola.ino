#include <Servo.h>
#include <Wire.h>

#define SLAVE_ADDR 12 // Adresa druhého Arduina (Kola)

/* --- KONFIGURACE PINŮ A HODNOT --- */
const int pinServoAnalog = 10; 
const int analogPiny[] = {A1, A2, A3};

// Tvé přesně naměřené klidové hodnoty
const int referencniHodnoty[] = {528, 493, 497}; 
const int THRESHOLD = 40; // Minimální změna nutná k aktivaci

/* --- I2C STAVY --- */
volatile byte i2cStatus = 0; 
unsigned long casZmenyI2C = 0;
const unsigned long DOBA_STAVU = 1000;
volatile bool cmdOpenLock = false; 
const char CMD_OPEN_LOCK = 'C';

/* --- PROMĚNNÉ PRO ANALOGOVOU LOGIKU --- */
bool stavPinuAktivni[3] = {false, false, false}; 
unsigned long startCasPodminkyAnalog = 0; 
unsigned long startCasResetuAnalog = 0;   
unsigned long startServoAnalog = 0;
bool analogServoVakci = false;
bool aktivaceDokoncena = false; 

Servo servoAnalog;

struct DiagKola {
  uint8_t status;
  uint8_t active_mask;
  uint16_t a1_val;
  uint16_t a2_val;
  uint16_t a3_val;
} __attribute__((packed));
DiagKola myTelemetry = {0, 0, 0, 0, 0};

void setup() {
  Serial.begin(9600);
  Wire.begin(SLAVE_ADDR);
  Wire.onRequest(requestEvent); 
  Wire.onReceive(receiveEvent); 

  servoAnalog.attach(pinServoAnalog);
  servoAnalog.write(0);

  Serial.println("==========================================");
  Serial.println("       MODUL 2: ANALOGY STARTUJÍ          ");
  Serial.println("==========================================");
  Serial.println("- Pevná kalibrace: A1=528, A2=493, A3=497");
  Serial.println("- Threshold: Změna o 40 jednotek");
  Serial.println("- Aktivace: 1.5s | Reset: 1.0s");
  Serial.println("- I2C Adresa: 12 | Čekám na příkaz 'C'");
}

void loop() {
  unsigned long ted = millis();

  // Reset I2C stavu po 1 sekundě
  if (i2cStatus != 0 && (ted - casZmenyI2C > DOBA_STAVU)) {
    i2cStatus = 0;
  }

  // Příkaz z ESP32 k otevření
  if (cmdOpenLock && !analogServoVakci) { 
    Serial.println("[I2C] Prikaz k aktivaci analogu!");
    aktivujAnalogServo(ted); 
    cmdOpenLock = false; 
  }

  handleAnalog(ted);

  // Fyzický návrat serva po 2 sekundách
  if (analogServoVakci && (ted - startServoAnalog > 2000)) { 
    servoAnalog.write(0); 
    analogServoVakci = false; 
    Serial.println("[SERVO 10] Analogove servo se vraci.");
  }

  // Aktualizace telemetrie pro ESP32
  myTelemetry.status = i2cStatus;
  myTelemetry.active_mask = (stavPinuAktivni[0] ? 1 : 0) | (stavPinuAktivni[1] ? 2 : 0) | (stavPinuAktivni[2] ? 4 : 0);
}

// Vyhlazení analogového signálu (Oversampling)
int getSmoothedAnalog(int pin) {
  long sum = 0;
  for (int i = 0; i < 10; i++) {
    sum += analogRead(pin);
  }
  return sum / 10;
}

void handleAnalog(unsigned long ted) {
  bool vsechnySplneny = true;

  for (int i = 0; i < 3; i++) {
    int hodnota = getSmoothedAnalog(analogPiny[i]);
    if(i==0) myTelemetry.a1_val = hodnota;
    if(i==1) myTelemetry.a2_val = hodnota;
    if(i==2) myTelemetry.a3_val = hodnota;
    
    // Výpočet absolutní odchylky od tvé pevné kalibrační hodnoty
    int odchylka = abs(hodnota - referencniHodnoty[i]);
    bool aktualneAktivni = (odchylka >= THRESHOLD);
    
    // Výpisy do Serial Monitoru pouze při změně stavu
    if (aktualneAktivni && !stavPinuAktivni[i]) {
      Serial.print("[ANALOG] A"); Serial.print(i + 1); Serial.println(" AKTIVNI [!]");
      stavPinuAktivni[i] = true;
    } 
    else if (!aktualneAktivni && stavPinuAktivni[i]) {
      Serial.print("[ANALOG] A"); Serial.print(i + 1); Serial.println(" UVOLNEN [ ]");
      stavPinuAktivni[i] = false;
    }

    if (!aktualneAktivni) vsechnySplneny = false;
  }

  // --- LOGIKA AKTIVACE A RESETU ---
  if (vsechnySplneny) {
    startCasResetuAnalog = 0; // Vynulujeme časovač resetu

    if (startCasPodminkyAnalog == 0 && !aktivaceDokoncena) {
      startCasPodminkyAnalog = ted;
      Serial.println("[SYSTEM] Magnety detekovany. Startuji odpocet 1.5s...");
    }
    
    if (startCasPodminkyAnalog != 0 && (ted - startCasPodminkyAnalog >= 1500) && !aktivaceDokoncena) {
      Serial.println("[AKCE] 1.5s ubehlo! Aktivuji analogove servo.");
      aktivujAnalogServo(ted);
    }
  } 
  else {
    startCasPodminkyAnalog = 0; // Vynulujeme časovač aktivace

    if (startCasResetuAnalog == 0) {
      startCasResetuAnalog = ted; 
    }

    if (ted - startCasResetuAnalog >= 1000) {
      if (aktivaceDokoncena) {
        aktivaceDokoncena = false;
        Serial.println("[SYSTEM] 1s stabilita v klidu. System pripraven.");
      }
      startCasResetuAnalog = 0; 
    }
  }
}

// --- I2C FUNKCE ---
void receiveEvent(int howMany) {
  while (Wire.available()) {
    char c = Wire.read();
    if (c == CMD_OPEN_LOCK) cmdOpenLock = true;
  }
}

void requestEvent() { 
  Wire.write((byte*)&myTelemetry, sizeof(DiagKola)); 
}

void aktivujAnalogServo(unsigned long ted) {
  i2cStatus = 3; 
  casZmenyI2C = ted;
  servoAnalog.write(90); 
  startServoAnalog = ted;
  analogServoVakci = true; 
  aktivaceDokoncena = true;
}