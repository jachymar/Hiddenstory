#include <Wire.h>

/* --- KONFIGURACE PINŮ --- */
const int pocetSvetel = 4;
int poradpinu[pocetSvetel] = {9, 6, 3, 5}; 
const int pinSenzoru = A0;                

// OneWire komunikace
const int pinOneWire = 4; 
OneWire ds(pinOneWire);

/* --- ČASOVÁNÍ ANIMACE --- */
const int preDelay = 2500;           // 2,5s předstih pro ESP32
const int pauzaPredDalsimSvetlem = 400; // Krátká pauza před zapnutím dalšího světla (400ms)
const int pauzaMeziVypnutim = 650;   // 0.65s (650ms) interval konečného vypínání
const int dobaPlnehoSvitu = 5000;    // 5s společného svícení v DIM stavu (než se začne vypínat)
const int jasDim = 15;               // Hodnota nízkého jasu ("skoro vypnuté")
const unsigned long dobaSvituJedneLED = 1500; // Doba, po kterou svítí LED naplno

/* --- ČASOVÁNÍ KRYSTALŮ A MAGNETU --- */
const unsigned long ochrannaLhutaKrystaly = 7000; // 7s musí být krystaly OFF pro nový start
const unsigned long maxDobaStavu2 = 12000;        // 12s limit pro odesílání stavu 2
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
volatile byte systemovyStav = 0; 
volatile bool prikazOtevritLebku = false; 

/* --- STAVOVÉ PROMĚNNÉ SYSTÉMU --- */
bool cyklusBezi = false;
bool pripravenoKActivaci = false;
unsigned long casVstupuDoOkna = 0; 
unsigned long casStartuPauzy = 0;  // Hlídá pauzu mezi dozhasnutím a startem další LED

// Proměnné pro OneWire (Krystaly)
bool krystalySplneny = false;
byte posledniOneWirePIO = 0xFF; // Uložení surové telemetrie z Lebky

unsigned long casPoslednihoCteni1W = 0;
bool externiAktivni = false;
bool pripravenoKExterniActivaci = false; 
unsigned long casVypnutiKrystalu = 0;    
unsigned long casStartuStavu2 = 0;

unsigned long casAktivaceMagnetem = 0;
unsigned long casStartuSekvence = 0;
unsigned long casZacatkuSviceni = 0;
unsigned long casVypisu = 0;

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
  
  for (int i = 0; i < pocetSvetel; i++) {
    pinMode(poradpinu[i], OUTPUT);
    digitalWrite(poradpinu[i], LOW);
    nastavUnikatniParametry(i);
  }
  
  randomSeed(analogRead(1)); 
  
  Wire.begin(8);
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
  
  // Zabezpečení, aby výchozí klidová hodnota nebyla mimo povolené meze
  if (klidovaHodnota < 520) klidovaHodnota = 520;
  if (klidovaHodnota > 532) klidovaHodnota = 532;
  
  // Vložení první hodnoty do historie kalibrace
  historieHodnot[0] = klidovaHodnota;
  pocetVzorku = 1;
  indexHistorie = 1;
  
  Serial.print("SYSTEM PRIPRAVEN. Vychozi klidova hodnota: ");
  Serial.println(klidovaHodnota);
}

void loop() {
  unsigned long ted = millis();
  
  // --- 0. OBSLUHA PŘÍKAZU LEBKA ---
  if (prikazOtevritLebku) {
    zapisOneWirePIO(0xFD); // Odeslání příkazu na otevření
    prikazOtevritLebku = false;       
  }

  // --- 1. NEBLOKUJÍCÍ ČTENÍ ONEWIRE (Krystaly) ---
  ctiOneWire();

  // --- 2. LOGIKA KRYSTALŮ (STAV 2) S 7s POJISTKOU ---
  if (!krystalySplneny) {
    if (casVypnutiKrystalu == 0) casVypnutiKrystalu = ted;
    if (ted - casVypnutiKrystalu >= ochrannaLhutaKrystaly) {
      pripravenoKExterniActivaci = true; 
    }
  } else {
    if (pripravenoKExterniActivaci && !cyklusBezi && !externiAktivni) {
      externiAktivni = true;
      pripravenoKExterniActivaci = false;
      casStartuStavu2 = ted;
      Serial.println(">>> STAV 2: Krystaly aktivovany.");
    }
    casVypnutiKrystalu = 0; 
  }
  
  // Omezení trvání Stavu 2 na 12 sekund
  if (externiAktivni && (ted - casStartuStavu2 > maxDobaStavu2)) {
    externiAktivni = false;
  }
  
  if (cyklusBezi) externiAktivni = false;

  // --- 3. URČENÍ STAVU PRO I2C ---
  if (cyklusBezi) systemovyStav = 1;
  else systemovyStav = 0;

  // --- PRŮBĚŽNÁ AUTO-KALIBRACE (Průměr za poslední 2 minuty) ---
  // Každou sekundu zkusíme zapsat novou hodnotu
  if (ted - casPoslednihoVzorku >= 1000) {
    casPoslednihoVzorku = ted;
    int aktualniCteni = analogRead(pinSenzoru);
    
    // Hodnota se započítá do průměru POUZE pokud je v bezpečném rozmezí (např. bez kuličky)
    if (aktualniCteni >= 520 && aktualniCteni <= 532) {
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

  // --- 4. LOGIKA MAGNETU (DELTA AUTO-KALIBRACE) ---
  int h = analogRead(pinSenzoru);
  int odchylka = abs(h - klidovaHodnota);
  
  bool senzorVKlidu = (odchylka <= (deltaThreshold / 2));
  bool senzorAktivni = (odchylka > deltaThreshold);

  // DEBUG VÝPIS
  if (ted - casVypisu > 500) {
    Serial.print("Akt: "); Serial.print(h);
    Serial.print(" | Klid(prumer): "); Serial.print(klidovaHodnota);
    Serial.print(" | Odchylka: "); Serial.print(odchylka);
    Serial.print(" | I2C Stav: "); Serial.println(systemovyStav);
    casVypisu = ted;
  }

  // Ochranná lhůta před dalším spuštěním (0.8s musí být v absolutním klidu)
  if (senzorVKlidu && !cyklusBezi) {
    if (casVstupuDoOkna == 0) casVstupuDoOkna = ted;
    if (ted - casVstupuDoOkna > ochrannaLhuta) pripravenoKActivaci = true;
  } else if (!senzorVKlidu) {
    casVstupuDoOkna = 0; // Magnet se hýbe, resetujeme klidovou lhůtu
  }

  // SPUŠTĚNÍ SHOW (Kulička je přiložena)
  if (senzorAktivni && pripravenoKActivaci && !cyklusBezi) {
    Serial.println(">>> SPUSTENI SHOW! Kulicka prilozena.");
    cyklusBezi = true;
    pripravenoKActivaci = false;
    
    vsechnaRozsvicena = false;
    aktualniRozsvicena = 0;
    aktualniVypnuta = 0;
    casZacatkuSviceni = 0;
    casStartuSekvence = 0;
    casStartuPauzy = 0;
    casAktivaceMagnetem = ted; 
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
        cyklusBezi = false; systemovyStav = 0; casZacatkuSviceni = 0; casVstupuDoOkna = 0; 
        casStartuPauzy = 0;
        for(int i=0; i<pocetSvetel; i++) nastavUnikatniParametry(i);
      }
    }
  }

  // Aktualizace telemetrie pro ESP32
  myTelemetry.status = systemovyStav;
  myTelemetry.mode_running = cyklusBezi ? 1 : 0;
  myTelemetry.current_led = (uint8_t)aktualniRozsvicena;
  myTelemetry.magnet_idle = klidovaHodnota;
  myTelemetry.magnet_val = h;
}

// --- FUNKCE PRO ZÁPIS NA ONEWIRE (Odeslání k Lebce) ---
void zapisOneWirePIO(byte pioData) {
  if (ds.reset()) {
    ds.skip(); ds.write(0x5A); ds.write(pioData); ds.write(~pioData);
    byte ack = ds.read(); 
    if (ack == 0xAA) Serial.println("OneWire: Prikaz potvrzen (0xAA)");
  }
}

// --- FUNKCE PRO ČTENÍ ONEWIRE (Stav krystalů z P0) ---
void ctiOneWire() {
  if (millis() - casPoslednihoCteni1W >= 500) {
    casPoslednihoCteni1W = millis();
    if (ds.reset()) {
      ds.skip(); ds.write(0xF0); ds.write(0x88); ds.write(0x00);
      byte pioStav = ds.read(); 
      posledniOneWirePIO = pioStav; // Uložení celého stavu pro I2C paket
      if ((pioStav & 0x01) == 0) krystalySplneny = true;
      else krystalySplneny = false;
    } else { 
      krystalySplneny = false; 
      posledniOneWirePIO = 0xFF; 
    }
  }
}

// --- I2C FUNKCE ---
void requestEvent() { 
  Wire.write((byte*)&myTelemetry, sizeof(DiagSvetla)); 
}

void receiveEvent(int howMany) {
  while (Wire.available()) {
    Wire.read(); 
  }
}

// --- POMOCNÁ FUNKCE ---
void nastavUnikatniParametry(int i) {
  delkaZazehu[i] = random(600, 1300); 
  silaKolisani[i] = random(190, 230); 
  rychlostNabehu[i] = random(3, 7);
}