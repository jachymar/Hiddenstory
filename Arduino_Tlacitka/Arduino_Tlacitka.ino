#include <Servo.h>
#include <Wire.h>

#define SLAVE_ADDR 11 // Adresa prvního Arduina

/* --- KONFIGURACE PINŮ A HESLA --- */
const int tlacitka[] = {2, 3, 4, 5, 6};
const int pinServoZamek = 9;   
const int heslo[] = {2, 5, 3, 4}; 

/* --- I2C STAVY --- */
volatile byte i2cStatus = 0; 
unsigned long casZmenyI2C = 0;
const unsigned long DOBA_STAVU = 1000;
volatile bool cmdOpenLock = false; 
const char CMD_OPEN_LOCK = 'B';
volatile bool sendDetailed = false;

/* --- PROMĚNNÉ PRO ZÁMEK (Nezávislý Debounce) --- */
int zadaneHeslo[4];
int pocitadloStisku = 0;
const unsigned long CAS_PRO_UVOLNENI = 250; // 250 ms debounce

bool stavTlacitka[5] = {false, false, false, false, false}; 
unsigned long casZmenyTlacitka[5] = {0, 0, 0, 0, 0};

unsigned long posledniAktivitaHesla = 0;  
unsigned long startServoZamek = 0;
bool zamekVakci = false;

Servo servoZamek;

struct DiagTlacitka {
  uint8_t status;
  uint8_t lock_open;
  uint8_t presses;
  uint16_t idle_time;
} __attribute__((packed));
DiagTlacitka myTelemetry = {0, 0, 0, 0};

void setup() {
  Serial.begin(9600);
  Wire.begin(SLAVE_ADDR);
  Wire.onRequest(requestEvent); 
  Wire.onReceive(receiveEvent); 

  // Nastavení tlačítek na vnitřní pull-up rezistory
  for (int i = 0; i < 5; i++) {
    pinMode(tlacitka[i], INPUT_PULLUP);
  }

  servoZamek.attach(pinServoZamek);
  servoZamek.write(0);

  Serial.println("==========================================");
  Serial.println("      MODUL 1: KLAVESNICE STARTUJE        ");
  Serial.println("==========================================");
  Serial.println("- Nezávislý debounce: 250ms");
  Serial.println("- I2C Adresa: 3 | Čekám na příkaz 'B'");
}

void loop() {
  unsigned long ted = millis();

  // Reset I2C stavu po 1 sekundě
  if (i2cStatus != 0 && (ted - casZmenyI2C > DOBA_STAVU)) {
    i2cStatus = 0;
  }

  // Příkaz z ESP32 k otevření
  if (cmdOpenLock && !zamekVakci) { 
    Serial.println("[I2C] Prikaz k otevreni zamku!");
    otevriZamek(ted); 
    cmdOpenLock = false; 
  }

  handleButtons(ted);

  // Fyzický návrat serva po 3 sekundách
  if (zamekVakci && (ted - startServoZamek > 3000)) { 
    servoZamek.write(0); 
    zamekVakci = false; 
    Serial.println("[SERVO 9] Zamek se mechanicky zavira.");
  }

  // Aktualizace telemetrie pro ESP32
  myTelemetry.status = i2cStatus;
  myTelemetry.lock_open = zamekVakci ? 1 : 0;
  myTelemetry.presses = (uint8_t)pocitadloStisku;
  unsigned long idle = ted - posledniAktivitaHesla;
  myTelemetry.idle_time = (idle > 65535) ? 65535 : (uint16_t)idle;
}

void handleButtons(unsigned long ted) {
  // Timeout - resetování rozepsaného hesla po 5s nečinnosti
  if (pocitadloStisku > 0 && (ted - posledniAktivitaHesla > 5000)) {
    Serial.println("[KLAVESNICE] Zadavani trvalo prilis dlouho. Resetuji.");
    pocitadloStisku = 0; 
  }

  for (int i = 0; i < 5; i++) {
    int pin = tlacitka[i];
    bool fyzickyStisknuto = (digitalRead(pin) == LOW);

    // Pokud se stav změnil a uplynulo 250 ms od poslední změny tohoto pinu
    if (fyzickyStisknuto != stavTlacitka[i] && (ted - casZmenyTlacitka[i] >= CAS_PRO_UVOLNENI)) {
      stavTlacitka[i] = fyzickyStisknuto;
      casZmenyTlacitka[i] = ted;

      if (fyzickyStisknuto) {
        posledniAktivitaHesla = ted;
        
        if (pocitadloStisku < 4) {
          zadaneHeslo[pocitadloStisku] = pin;
          pocitadloStisku++;
          Serial.print("[KLAVESNICE] Zmáčknuto: "); Serial.print(pin);
          Serial.print(" (Pozice: "); Serial.print(pocitadloStisku); Serial.println("/4)");
        }

        if (pocitadloStisku == 4) {
          bool ok = true;
          for (int j = 0; j < 4; j++) {
            if (zadaneHeslo[j] != heslo[j]) { ok = false; break; }
          }
          
          if (ok) {
            Serial.println("[VYSLEDEK] >>> HESLO SPRAVNE <<< Oteviram.");
            otevriZamek(ted);
          } else {
            Serial.println("[VYSLEDEK] >>> HESLO NESPRAVNE <<<");
            i2cStatus = 1; casZmenyI2C = ted; 
          }
          pocitadloStisku = 0; 
        }
      }
    }
  }
}

// --- I2C FUNKCE ---
void receiveEvent(int howMany) {
  while (Wire.available()) {
    byte c = Wire.read();
    if (c == CMD_OPEN_LOCK) cmdOpenLock = true;
    else if (c == 0x99) sendDetailed = true;
  }
}

void requestEvent() { 
  if (sendDetailed) {
    Wire.write((byte*)&myTelemetry, sizeof(DiagTlacitka)); 
    sendDetailed = false;
  } else {
    Wire.write(i2cStatus);
  }
}

void otevriZamek(unsigned long ted) {
  i2cStatus = 2; 
  casZmenyI2C = ted;
  servoZamek.write(150); 
  startServoZamek = ted; 
  zamekVakci = true;
}