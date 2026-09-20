#include <Wire.h>

/* --- KONFIGURACE PINŮ --- */
const int pocetSvetel = 4;
int poradpinu[pocetSvetel] = {9, 6, 3, 5}; 
const int pinSenzoru = A0;                
const int pinServo = 10;
int cilovyUhelServa = 0;

/* --- ČASOVÁNÍ ANIMACE --- */
const int preDelay = 2500;           // 2,5s předstih pro ESP32
const int pauzaPredDalsimSvetlem = 400; // Krátká pauza před zapnutím dalšího světla (400ms)
const int pauzaMeziVypnutim = 650;   // 0.65s (650ms) interval konečného vypínání
const int dobaPlnehoSvitu = 5000;    // 5s společného svícení v DIM stavu (než se začne vypínat)
const int jasDim = 15;               // Hodnota nízkého jasu ("skoro vypnuté")
const unsigned long dobaSvituJedneLED = 1500; // Doba, po kterou svítí LED naplno

/* --- ČASOVÁNÍ MAGNETU --- */
const unsigned long ochrannaLhuta = 800;          // 0,8s (800ms) v klidu pro reset magnetu

/* --- AUTO-KALIBRACE MAGNETU --- */
int klidovaHodnota = 0;
const int deltaThreshold = 10; // O kolik se musí hodnota změnit pro aktivaci (citlivost)

// Proměnné pro 2 minutový průměr
const int velikostBufferu = 120; // 120 vteřin = 2 minuty
int historieHodnot[velikostBufferu];
int indexHistorie = 0;
int pocetVzorku = 0;
unsigned long casPoslednihoVzorku = 0;

/* --- PROMĚNNÉ PRO I2C A LEBKU --- */
volatile byte i2cStatus = 0; 
const byte I2C_SLAVE_ADDR = 14;
volatile byte i2c_req = 0;
char lastLog[30] = "Start";

void Log(const char* txt) {
  strncpy(lastLog, txt, 29);
  lastLog[29] = '\0';
  Serial.println(txt);
}

/* --- STAVOVÉ PROMĚNNÉ SYSTÉMU --- */
bool cyklusBezi = false;
bool pripravenoKActivaci = false;
bool pracovniMod = false;
unsigned long casVstupuDoOkna = 0; 
unsigned long casStartuPauzy = 0;  // Hlídá pauzu mezi dozhasnutím a startem další LED

/* --- NOUZOVÝ REŽIM (BEZ SERVA) - OPAKOVÁNÍ SEKVENCE --- */
bool opakovatSekvenci = false;
bool schrankaOtevrena = false;
unsigned long casPoslednihoSpusteni = 0;
const unsigned long INTERVAL_OPAKOVANI = 40000; // Opakování každých 40 sekund

unsigned long casAktivaceMagnetem = 0;
unsigned long casStartuSekvence = 0;
unsigned long casZacatkuSviceni = 0;
unsigned long casVypisu = 0;

// Proměnné pro debounce aktivace senzoru
unsigned long casZacatkuAktivitySenzoru = 0;
bool senzorTrvaleAktivni = false;
const unsigned long DOBA_POTVRZENI_MAGNETU = 300; // Senzor musí být aktivní 300ms v kuse

// Proměnné pro servo a chybovou sekvenci
volatile bool chybovaSekvenceAktivni = false;
unsigned long casZacatkuChyby = 0;
bool servoVPohybu = false;

int stavSvetla[pocetSvetel] = {0, 0, 0, 0};
int aktualniJas[pocetSvetel] = {0, 0, 0, 0};
unsigned long casPosledniZmeny[pocetSvetel] = {0, 0, 0, 0};

int aktualniRozsvicena = 0;
int aktualniVypnuta = 0;
bool vsechnaRozsvicena = false;

unsigned long casStartuFaze[pocetSvetel] = {0, 0, 0, 0}; 
int delkaZazehu[pocetSvetel], silaKolisani[pocetSvetel], rychlostNabehu[pocetSvetel];

struct DiagSvetla {
  uint8_t status;
  uint8_t mode_running;
  uint8_t current_led;
  uint16_t magnet_idle;
  uint16_t magnet_val;
} __attribute__((packed));
DiagSvetla myTelemetry = {0, 0, 0, 0, 0};

void setup() {
  Serial.begin(115200); 
  
  pinMode(pinServo, OUTPUT);
  digitalWrite(pinServo, LOW);
  cilovyUhelServa = 5;
  
  for (int i = 0; i < pocetSvetel; i++) {
    pinMode(poradpinu[i], OUTPUT);
    digitalWrite(poradpinu[i], LOW);
    nastavUnikatniParametry(i);
  }
  
  randomSeed(analogRead(1)); 
  
  Wire.begin(I2C_SLAVE_ADDR);
  Wire.onRequest(requestEvent);
  Wire.onReceive(receiveEvent); 
  
  Serial.println("--- INICIALIZACE A KALIBRACE SENZORU ---");
  long suma = 0;
  // Přečteme senzor 30x pro stabilní počáteční hodnotu
  for(int i = 0; i < 30; i++) { 
    suma += analogRead(pinSenzoru); 
    delay(20); 
  }
  klidovaHodnota = suma / 30;
  
  // Zabezpečení proti extrémním chybám senzoru (např. odpojený pin 0 nebo 1023)
  if (klidovaHodnota < 200 || klidovaHodnota > 850) {
    klidovaHodnota = 512;
  }
  
  // Vložení první hodnoty do historie kalibrace
  for (int i = 0; i < velikostBufferu; i++) {
    historieHodnot[i] = klidovaHodnota;
  }
  pocetVzorku = 1;
  indexHistorie = 1;
  pripravenoKActivaci = true;
  opakovatSekvenci = false;
  schrankaOtevrena = false;
  casVstupuDoOkna = millis();
  
  char m[30];
  sprintf(m, "Klid: %d", klidovaHodnota);
  Log(m);
}

void spustSekvenci(unsigned long ted) {
  cyklusBezi = true;
  vsechnaRozsvicena = false;
  aktualniRozsvicena = 0;
  aktualniVypnuta = 0;
  casZacatkuSviceni = 0;
  casStartuSekvence = 0;
  casStartuPauzy = 0;
  casAktivaceMagnetem = ted;
  casPoslednihoSpusteni = ted;
  for (int i = 0; i < pocetSvetel; i++) {
    stavSvetla[i] = 0;
    aktualniJas[i] = 0;
    digitalWrite(poradpinu[i], LOW);
    nastavUnikatniParametry(i);
  }
}

void vypniVsechnaSvetla() {
  cyklusBezi = false;
  i2cStatus = 0;
  vsechnaRozsvicena = false;
  aktualniRozsvicena = 0;
  aktualniVypnuta = 0;
  casZacatkuSviceni = 0;
  casStartuSekvence = 0;
  casStartuPauzy = 0;
  for (int i = 0; i < pocetSvetel; i++) {
    stavSvetla[i] = 0;
    aktualniJas[i] = 0;
    digitalWrite(poradpinu[i], LOW);
    nastavUnikatniParametry(i);
  }
}

void loop() {
  unsigned long ted = millis();
  
  // --- 1. URČENÍ STAVU PRO I2C ---
  if (cyklusBezi) i2cStatus = 1;
  else i2cStatus = 0;

  // --- PRŮBĚŽNÁ AUTO-KALIBRACE (Průměr za poslední 2 minuty) ---
  // Každou sekundu zkusíme zapsat novou hodnotu
  if (ted - casPoslednihoVzorku >= 1000) {
    casPoslednihoVzorku = ted;
    int aktualniCteni = analogRead(pinSenzoru);
    
    // Hodnota se započítá do průměru POUZE pokud je v klidovém stavu (bez magnetu)
    if (abs(aktualniCteni - klidovaHodnota) <= (deltaThreshold / 2)) {
      historieHodnot[indexHistorie] = aktualniCteni;
      indexHistorie = (indexHistorie + 1) % velikostBufferu; 
      if (pocetVzorku < velikostBufferu) pocetVzorku++;
      
      // Výpočet nového průměru z historie
      long sumaHistorie = 0;
      for (int i = 0; i < pocetVzorku; i++) {
        sumaHistorie += historieHodnot[i];
      }
      klidovaHodnota = sumaHistorie / pocetVzorku;
    }
  }

  // --- 4. LOGIKA MAGNETU (DELTA AUTO-KALIBRACE A DEBOUNCE) ---
  int h = analogRead(pinSenzoru);
  int odchylka = abs(h - klidovaHodnota);
  
  bool senzorVKlidu = (odchylka <= (deltaThreshold / 2));
  bool senzorAktivni = (odchylka >= deltaThreshold);

  // Ochranná lhůta před dalším spuštěním (0.8s musí být v absolutním klidu)
  if (senzorVKlidu && !cyklusBezi) {
    if (casVstupuDoOkna == 0) casVstupuDoOkna = ted;
    if (ted - casVstupuDoOkna >= ochrannaLhuta) pripravenoKActivaci = true;
    casZacatkuAktivitySenzoru = 0;
    senzorTrvaleAktivni = false;
  } else if (!senzorVKlidu) {
    casVstupuDoOkna = 0; // Magnet se hýbe, resetujeme klidovou lhůtu
    
    // Debounce filtrace - ignoruje výkyvy, magnet tam musí být držen trvale
    if (senzorAktivni) {
      if (casZacatkuAktivitySenzoru == 0) {
        casZacatkuAktivitySenzoru = ted;
      } else if (ted - casZacatkuAktivitySenzoru >= DOBA_POTVRZENI_MAGNETU) {
        senzorTrvaleAktivni = true;
      }
    } else {
      casZacatkuAktivitySenzoru = 0;
      senzorTrvaleAktivni = false;
    }
  }

  // SPUŠTĚNÍ SHOW (Kulička je přiložena a potvrzena filtrací) - v pracovním módu se nespouští
  if (!pracovniMod && senzorTrvaleAktivni && pripravenoKActivaci && !cyklusBezi && !schrankaOtevrena) {
    Log("SHOW START! Magnet detekovan");
    opakovatSekvenci = true;
    pripravenoKActivaci = false;
    spustSekvenci(ted);
  }

  // --- OPAKOVÁNÍ SEKVENCE KAŽDÝCH 40s (DOKUD NENÍ SCHRÁNKA OTEVŘENA) ---
  if (!pracovniMod && opakovatSekvenci && !schrankaOtevrena) {
    if (!cyklusBezi && (ted - casPoslednihoSpusteni >= INTERVAL_OPAKOVANI)) {
      Log("SHOW REPEAT (40s)");
      spustSekvenci(ted);
    }
  }

  // --- 5. ANIMACE LED (Kaskáda) ---
  if (cyklusBezi) {
    if (casStartuSekvence == 0 && (ted - casAktivaceMagnetem >= preDelay)) casStartuSekvence = ted;
    
    if (casStartuSekvence > 0) {
      if (!vsechnaRozsvicena) {
        
        // ZAPÍNÁNÍ JEDNOHO PO DRUHÉM S PAUZOU
        if (aktualniRozsvicena == 0) {
          stavSvetla[0] = 1; 
          casStartuFaze[0] = ted; 
          aktualniRozsvicena = 1;
        } else if (aktualniRozsvicena < pocetSvetel) {
          int minulaLED = aktualniRozsvicena - 1;
          if (stavSvetla[minulaLED] == 5 && casStartuPauzy > 0 && (ted - casStartuPauzy >= pauzaPredDalsimSvetlem)) {
            stavSvetla[aktualniRozsvicena] = 1;
            casStartuFaze[aktualniRozsvicena] = ted; 
            aktualniRozsvicena++;
            casStartuPauzy = 0; 
          }
        }
        
        if (aktualniRozsvicena == pocetSvetel && stavSvetla[pocetSvetel-1] == 5 && casZacatkuSviceni == 0) {
          casZacatkuSviceni = ted;
        }
        
        if (casZacatkuSviceni > 0 && ted - casZacatkuSviceni > dobaPlnehoSvitu) {
          vsechnaRozsvicena = true;
          casStartuSekvence = ted; 
        }
      }
      
      // FINÁLNÍ VYPÍNÁNÍ V INTERVALECH
      if (vsechnaRozsvicena) {
        if (ted - casStartuSekvence > (unsigned long)(aktualniVypnuta * pauzaMeziVypnutim) && aktualniVypnuta < pocetSvetel) {
          stavSvetla[aktualniVypnuta] = 6; 
          aktualniVypnuta++;
        }
      }
    }
    
    for (int i = 0; i < pocetSvetel; i++) {
      switch (stavSvetla[i]) {
        case 1: // ZÁŽEH
          if (ted - casPosledniZmeny[i] > (unsigned long)random(40, 110)) {
            aktualniJas[i] = random(20, 90); analogWrite(poradpinu[i], aktualniJas[i]);
            casPosledniZmeny[i] = ted;
            if (ted - casStartuFaze[i] > (unsigned long)delkaZazehu[i]) stavSvetla[i] = 2;
          }
          break;
        case 2: // NÁBĚH
          if (ted - casPosledniZmeny[i] > 20) {
            aktualniJas[i] += rychlostNabehu[i] * 2; 
            if (aktualniJas[i] >= 240) { 
              aktualniJas[i] = 250; 
              stavSvetla[i] = 3; 
              casStartuFaze[i] = ted; 
            }
            analogWrite(poradpinu[i], aktualniJas[i]); casPosledniZmeny[i] = ted;
          }
          break;
        case 3: // PLNNÝ SVIT
          if (ted - casPosledniZmeny[i] > 50) {
            analogWrite(poradpinu[i], random(220, 255)); 
            casPosledniZmeny[i] = ted;
          }
          if (ted - casStartuFaze[i] > dobaSvituJedneLED) {
            stavSvetla[i] = 4; 
          }
          break;
        case 4: // POHASÍNÁNÍ NA DIM (ROZTŘESENÉ)
          if (ted - casPosledniZmeny[i] > (unsigned long)random(15, 45)) { 
            aktualniJas[i] -= random(2, 8); 
            
            if (aktualniJas[i] <= jasDim) { 
              aktualniJas[i] = jasDim; 
              stavSvetla[i] = 5; 
              casStartuPauzy = ted; // SVĚTLO DOSÁHLO DIM STAVU A STARTUJE PAUZA
            }
            
            int zobrazenyJas = aktualniJas[i] + random(-15, 10);
            if (zobrazenyJas < 0) zobrazenyJas = 0;
            if (zobrazenyJas > 255) zobrazenyJas = 255;
            
            analogWrite(poradpinu[i], zobrazenyJas);
            casPosledniZmeny[i] = ted;
          }
          break;
        case 5: // DIM SVIT (Čekání)
          if (ted - casPosledniZmeny[i] > 100) {
            analogWrite(poradpinu[i], random(jasDim - 5, jasDim + 5)); 
            casPosledniZmeny[i] = ted;
          }
          break;
        case 6: // ÚPLNÉ VYPÍNÁNÍ (MÍRNĚ ROZTŘESENÉ)
          if (ted - casPosledniZmeny[i] > (unsigned long)random(15, 35)) { 
            if (aktualniJas[i] > 0) {
              aktualniJas[i] -= random(1, 4); 
              if (aktualniJas[i] < 0) aktualniJas[i] = 0;
              
              int zobrazenyJas = aktualniJas[i] + random(-5, 5); 
              if (zobrazenyJas < 0) zobrazenyJas = 0;
              
              analogWrite(poradpinu[i], zobrazenyJas);
            } else { 
              stavSvetla[i] = 0; 
              digitalWrite(poradpinu[i], LOW); 
            }
            casPosledniZmeny[i] = ted;
          }
          break;
      }
    }
    
    // Kompletní reset a příprava na další cyklus
    if (vsechnaRozsvicena && aktualniVypnuta == pocetSvetel) {
      bool hotovo = true;
      for(int i=0; i<pocetSvetel; i++) if(stavSvetla[i] != 0) hotovo = false;
      if (hotovo) {
        cyklusBezi = false; i2cStatus = 0; casZacatkuSviceni = 0; casVstupuDoOkna = 0; 
        casStartuPauzy = 0;
        Log("Sekvence dokoncena.");
        for(int i=0; i<pocetSvetel; i++) nastavUnikatniParametry(i);
      }
    }
  }

  // --- 6. LOGIKA SERVA (Chybová sekvence hesel z tlačítek) ---
  if (chybovaSekvenceAktivni) {
    if (!servoVPohybu) {
      cilovyUhelServa = 30;
      casZacatkuChyby = ted;
      servoVPohybu = true;
      Log("Servo: 30 deg");
    } else if (ted - casZacatkuChyby > 1000) {
      cilovyUhelServa = 5;
      chybovaSekvenceAktivni = false;
      servoVPohybu = false;
      Log("Servo: 5 deg");
    }
  }

  // Softwarové generování PWM pro Servo (bez přerušení, aby nedošlo k ovlivnění pinu 9)
  static unsigned long lastServoPulse = 0;
  if (ted - lastServoPulse >= 20) {
    lastServoPulse = ted;
    int pulseWidth = map(cilovyUhelServa, 0, 180, 544, 2400);
    digitalWrite(pinServo, HIGH);
    delayMicroseconds(pulseWidth);
    digitalWrite(pinServo, LOW);
  }

  // Aktualizace telemetrie pro ESP32
  myTelemetry.status = i2cStatus;
  myTelemetry.mode_running = cyklusBezi ? 1 : 0;
  myTelemetry.current_led = (uint8_t)aktualniRozsvicena;
  myTelemetry.magnet_idle = klidovaHodnota;
  myTelemetry.magnet_val = h;
}

// --- I2C FUNKCE ---
void requestEvent() { 
  if (i2c_req == 0x99) {
    Wire.write((byte*)&myTelemetry, sizeof(DiagSvetla)); 
    i2c_req = 0;
  } else if (i2c_req == 0x98) {
    Wire.write((byte*)lastLog, 30);
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
    } else if (c == 'E') {
      chybovaSekvenceAktivni = true;
    } else if (c == 'O' || c == 'B') {
      // Schránka otevřena tlačítky nebo webem -> okamžitě zastavit světla a zablokovat další spouštění
      schrankaOtevrena = true;
      opakovatSekvenci = false;
      vypniVsechnaSvetla();
      Log("I2C: Schranka otevrena - STOP");
    } else if (c == '3') {
      // POUZE PRACOVNÍ MÓD ('3') odblokuje schránku a uvede modul do pracovního klidu
      pracovniMod = true;
      schrankaOtevrena = false;
      opakovatSekvenci = false;
      vypniVsechnaSvetla();
      pripravenoKActivaci = false;
      Log("I2C: Pracovni mod - reset");
    } else if (c == 'R' || c == '0' || c == 99) {
      // Běžný návrat/reset (např. z laser alarmu) - pokud již byla schránka otevřena, zůstává zablokovaná až do pracovního módu!
      pracovniMod = false;
      if (!schrankaOtevrena) {
        opakovatSekvenci = false;
        vypniVsechnaSvetla();
        pripravenoKActivaci = true;
        casVstupuDoOkna = millis();
        Log("I2C: Reset");
      }
    }
  }
}

// --- POMOCNÁ FUNKCE ---
void nastavUnikatniParametry(int i) {
  delkaZazehu[i] = random(600, 1300); 
  silaKolisani[i] = random(190, 230); 
  rychlostNabehu[i] = random(3, 7);
}