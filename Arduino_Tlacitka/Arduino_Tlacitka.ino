#include <Servo.h>
#include <Wire.h>

#define SLAVE_ADDR 11 // Adresa prvního Arduina

/* --- KONFIGURACE PINŮ A HESLA --- */
const int tlacitka[] = {2, 3, 4, 5, 6}; // Heslo: 1,4,2,3 | Poslední 4 stisky jsou vyhodnocovány kontinuálně
const int pinServoZamek = 9;   
const int heslo[] = {1, 4, 2, 3}; 

/* --- I2C STAVY --- */
volatile byte i2cStatus = 0; 
unsigned long casZmenyI2C = 0;
const unsigned long DOBA_STAVU = 1000;
volatile bool cmdOpenLock = false; 
const char CMD_OPEN_LOCK = 'B';
volatile byte i2c_req = 0;
char lastLog[30] = "Start";

void Log(const char* txt) {
  strncpy(lastLog, txt, 29);
  lastLog[29] = '\0';
  Serial.println(txt);
}

/* --- PROMĚNNÉ PRO ZÁMEK (Nezávislý Debounce) --- */
int zadaneHeslo[4] = {0, 0, 0, 0};
int pocetZadanych = 0;
int pocetChybek = 0;
int pocetChybnychStisku = 0;
int pocitadloStisku = 0;
const unsigned long CAS_PRO_UVOLNENI = 250; // 250 ms debounce

bool stavTlacitka[5] = {false, false, false, false, false}; 
unsigned long casZmenyTlacitka[5] = {0, 0, 0, 0, 0};

unsigned long posledniAktivitaHesla = 0;  
unsigned long startServoZamek = 0;
bool zamekVakci = false;
bool schrankaOtevrena = false; // Po otevření schránky se modul zablokuje až do pracovního módu / resetu
bool odpocetNeaktivityAktivni = false; // Spouští se pouze po aktivaci magnetického senzoru na Světlech

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
  posledniAktivitaHesla = millis(); 

  Serial.println("==========================================");
  Serial.println("      MODUL 1: KLAVESNICE STARTUJE        ");
  Serial.println("==========================================");
  Serial.println("- Nezávislý debounce: 250ms");
  Serial.println("- I2C Adresa: 11 | Čekám na příkaz 'B'");
}

void loop() {
  unsigned long ted = millis();

  // Reset I2C stavu po 1 sekundě
  if (i2cStatus != 0 && (ted - casZmenyI2C > DOBA_STAVU)) {
    i2cStatus = 0;
  }

  // Příkaz z ESP32 k otevření
  if (cmdOpenLock && !zamekVakci) { 
    Log("I2C: Prikaz k otevreni!");
    otevriZamek(ted); 
    i2cStatus = 5;
    casZmenyI2C = ted;
    cmdOpenLock = false; 
  }

  handleButtons(ted);

  // Fyzický návrat serva do výchozí pozice po 2 sekundách od otevření
  if (zamekVakci && (ted - startServoZamek > 2000)) { 
    servoZamek.write(0); 
    zamekVakci = false; 
    Log("Zamek se mechanicky vraci (zavreno)");
  }

  // Případně web/Master si to může resetovat.
  if (i2cStatus == 99) { // 99 si určíme jako reset z Mastera
    servoZamek.write(0);
    zamekVakci = false;
    schrankaOtevrena = false;
    pocetChybek = 0;
    pocetChybnychStisku = 0;
    pocetZadanych = 0;
    for (int k = 0; k < 4; k++) zadaneHeslo[k] = 0;
    i2cStatus = 0;
  }

  // Aktualizace telemetrie pro ESP32
  myTelemetry.status = i2cStatus;
  myTelemetry.lock_open = schrankaOtevrena ? 1 : 0;
  myTelemetry.presses = (uint8_t)pocitadloStisku;
  unsigned long idle = ted - posledniAktivitaHesla;
  myTelemetry.idle_time = (idle > 65535) ? 65535 : (uint16_t)idle;
}

void handleButtons(unsigned long ted) {
  // Po 30 sekundách bez aktivity se spustí chybová akce (vysypání kuličky servom na Světlech)
  if (!schrankaOtevrena && odpocetNeaktivityAktivni &&
      (ted - posledniAktivitaHesla >= 30000)) {
    i2cStatus = 4; // Signalizace chyby / neaktivity pro Mastera (vysypání kuličky servom)
    casZmenyI2C = ted;
    odpocetNeaktivityAktivni = false;
    pocetChybek = 0;
    pocetChybnychStisku = 0;
    pocetZadanych = 0;
    for (int k = 0; k < 4; k++) zadaneHeslo[k] = 0;
    Log("30s neaktivita: Servo (chyba)");
  }

  for (int i = 0; i < 5; i++) {
    int pin = tlacitka[i];
    bool fyzickyStisknuto = (digitalRead(pin) == LOW);

    // Pokud se stav změnil a uplynulo 250 ms od poslední změny tohoto pinu
    if (fyzickyStisknuto != stavTlacitka[i] && (ted - casZmenyTlacitka[i] >= CAS_PRO_UVOLNENI)) {
      stavTlacitka[i] = fyzickyStisknuto;
      casZmenyTlacitka[i] = ted;

      if (fyzickyStisknuto) {
        posledniAktivitaHesla = ted; // Každý stisk tlačítka resetuje časovač neaktivity
        if (!schrankaOtevrena) {
          odpocetNeaktivityAktivni = true; // Aktivita na tlačítkách spouští hlídání neaktivity
        }
        pocitadloStisku++; // Počítadlo jen pro diagnostiku (telemetrie)
        
        int btnId = i + 1; // Převod na "Tlačítko 1 až 5", bez ohledu na to, jaký je to fyzický pin

        if (!schrankaOtevrena) {
          // Kontinuální buffer: posuneme historii doleva a nový stisk přidáme na konec
          if (pocetZadanych < 4) {
            zadaneHeslo[pocetZadanych] = btnId;
            pocetZadanych++;
          } else {
            for (int j = 0; j < 3; j++) {
              zadaneHeslo[j] = zadaneHeslo[j + 1];
            }
            zadaneHeslo[3] = btnId;
          }

          pocetChybnychStisku++;

          char msg[25];
          sprintf(msg, "Stisk: T%d (%d/4)", btnId, pocetZadanych);
          Log(msg);

          // Kontrola hesla: jako zadané heslo jsou vždy považována poslední 4 ťuknutí
          if (pocetZadanych == 4) {
            bool ok = true;
            for (int j = 0; j < 4; j++) {
              if (zadaneHeslo[j] != heslo[j]) { ok = false; break; }
            }
            
            if (ok) {
              Log(">>> HESLO SPRAVNE <<<");
              otevriZamek(ted);
              pocetChybek = 0;
              pocetChybnychStisku = 0;
              i2cStatus = 5; 
              casZmenyI2C = ted;
              odpocetNeaktivityAktivni = false;
              
              // Vyčistíme historii pro další hru
              pocetZadanych = 0;
              for (int k = 0; k < 4; k++) zadaneHeslo[k] = 0;
            } else {
              if (pocetChybnychStisku >= 4) {
                pocetChybnychStisku = 0;
                pocetChybek++;
                Log(">>> HESLO SPATNE <<<");
                if (pocetChybek >= 3) {
                  Log(">>> 3x SPATNE (12 stisku) <<<");
                  pocetChybek = 0;
                  i2cStatus = 4; // Signalizace chyby pro Mastera (pohyb serva na Světlech)
                  casZmenyI2C = ted;
                  odpocetNeaktivityAktivni = false;
                  pocetZadanych = 0;
                  for (int k = 0; k < 4; k++) zadaneHeslo[k] = 0;
                }
              }
            }
          }
        }
      }
    }
  }
}

// --- I2C FUNKCE ---
void receiveEvent(int howMany) {
  while (Wire.available()) {
    byte c = Wire.read();
    if (c == CMD_OPEN_LOCK) {
      cmdOpenLock = true;
    } else if (c == 0x99 || c == 0x98) {
      i2c_req = c;
    } else if (c == 'M') {
      // Magnetický senzor na Světlech byl aktivován -> spustíme odpočet 30s neaktivity
      odpocetNeaktivityAktivni = true;
      posledniAktivitaHesla = millis();
      Log("I2C: Magnet aktivovan - start 30s");
    } else if (c == 'O') {
      // Pouze nastavení stavu na Otevřeno (blokovat zadávání hesel bez pohybu serva)
      schrankaOtevrena = true;
      odpocetNeaktivityAktivni = false;
      pocetZadanych = 0;
      for (int k = 0; k < 4; k++) zadaneHeslo[k] = 0;
      Log("I2C: Schranka nastavena: Otevreno (blokovat)");
    } else if (c == 'U') {
      // Odblokování schránky (stav Zavřeno) pro novou hru i během herního módu
      schrankaOtevrena = false;
      odpocetNeaktivityAktivni = false;
      pocetChybek = 0;
      pocetChybnychStisku = 0;
      pocetZadanych = 0;
      posledniAktivitaHesla = millis();
      for (int k = 0; k < 4; k++) zadaneHeslo[k] = 0;
      Log("I2C: Schranka nastavena: Zavreno (odblokovano)");
    } else if (c == '3') {
      // Pouze pracovní mód ('3') odblokuje schránku pro další hru
      schrankaOtevrena = false;
      odpocetNeaktivityAktivni = false;
      pocetChybek = 0;
      pocetChybnychStisku = 0;
      pocetZadanych = 0;
      posledniAktivitaHesla = millis();
      for (int k = 0; k < 4; k++) zadaneHeslo[k] = 0;
      Log("Pracovni mod - schranka reset");
    } else if (c == '0' || c == 'R' || c == 99) {
      // Běžný návrat/reset (např. z laser alarmu)
      pocetChybek = 0;
      pocetChybnychStisku = 0;
      pocetZadanych = 0;
      odpocetNeaktivityAktivni = false;
      posledniAktivitaHesla = millis();
      for (int k = 0; k < 4; k++) zadaneHeslo[k] = 0;
      Log("Reset / Herni mod");
    }
  }
}

void requestEvent() { 
  if (i2c_req == 0x99) {
    Wire.write((byte*)&myTelemetry, sizeof(DiagTlacitka)); 
    i2c_req = 0;
  } else if (i2c_req == 0x98) {
    Wire.write((byte*)lastLog, 30);
    i2c_req = 0;
  } else {
    Wire.write(i2cStatus);
  }
}

void otevriZamek(unsigned long ted) {
  // i2cStatus necháváme 0, aby se neodesílala změna stavu na zelenou na světlech
  schrankaOtevrena = true;
  servoZamek.write(150); 
  startServoZamek = ted; 
  zamekVakci = true;
}