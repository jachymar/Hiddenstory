#include <SPI.h>
#include <SD.h>
#include <Adafruit_VS1053.h>
#include <Wire.h> // Knihovna pro I2C komunikaci

// --- PINY PRO VS1053 ---
#define VS1053_RESET    8     
#define VS1053_CS       6     
#define VS1053_DCS      7     
#define VS1053_DREQ     2     
#define CARD_CS         9     

// --- PINY PRO SENZORY ---
#define PIN_DVERE       3     

// --- I2C NASTAVENÍ ---
#define I2C_ADRESA_ARDUINA 16

const char CMD_PLAY_MAIN = '0';
const char CMD_PLAY_VICTORY = 'K';
const char CMD_STOP = '3';

// Vytvoření objektu přehrávače
Adafruit_VS1053_FilePlayer filePlayer = 
  Adafruit_VS1053_FilePlayer(VS1053_RESET, VS1053_CS, VS1053_DCS, VS1053_DREQ, CARD_CS);

// --- STAVY SYSTÉMU (State Machine) ---
enum SystemState {
  HRAJE_0001,
  ZESLABUJE_0001_NA_0002,
  CEKA_A_HRAJE_0002_HLASITE,
  ZESLABUJE_0002_NA_TISSI,
  HRAJE_0002_TISE,
  ZESLABUJE_ZPET_NA_0001,
  TICHO,
  // --- Stavy pro Krystaly (0003) ---
  ZESLABUJE_PRED_KRYSTALY, // Krátký fade-out aktuální hudby
  HRAJE_0003_HLASITE,
  ZESLABUJE_0003
};
SystemState aktualniStav = HRAJE_0001;

// --- PROMĚNNÉ PRO HLASITOST A FADING ---
// U VS1053: 0 = absolutní maximum, 254 = úplné ticho
const int MAX_HLASITOST = 2;            // Hlasitější pro skladbu 0001
const int START_HLASITOST_0002 = 40;    // Skladba 0002 začne tišeji
const int CILOVA_HLASITOST_0002 = 55;   // Skladba 0002 klesne na 55
const int MIN_HLASITOST = 254;          // Úplné ticho

// Proměnné pro Krystaly (0003)
const int MAX_HLASITOST_0003 = 0;       // Absolutní maximum! (Hlasitější než 0001)

int aktualniHlasitost = MAX_HLASITOST;
unsigned long posledniKrokZeslabeni = 0;

// Rychlosti zeslabování
const int RYCHLOST_PRECHODU = 15;       // Běžný přechod mezi skladbami
const int RYCHLOST_RYCHLEHO_ZESLABENI = 2; // Velmi rychlý fade-out před krystaly (cca 0.5 sekundy)
const int RYCHLOST_ZTISOVANI_0002 = 30000 / (CILOVA_HLASITOST_0002 - START_HLASITOST_0002); 

// Časovače
const unsigned long CAS_PRED_ZTISENIM_0002 = 30000; // 30s
const unsigned long CAS_PRED_ZTISENIM_0003 = 10000; // 10s hlasitě pro krystaly
unsigned long casZacatku0002 = 0;
unsigned long casZacatku0003 = 0;
unsigned long casZacatkuZeslabovani0003 = 0; // Pro logaritmický výpočet 60s fade-outu

// Počítadlo přehrání 0002
int pocetPrehrani0002 = 0;
const int MAX_PREHRANI_0002 = 3;

// --- PROMĚNNÉ PRO DVEŘE (S 1s FILTREM) ---
bool posledniStavPinu = HIGH; 
bool dvereOtevrenyPotvrzeno = false; 
unsigned long casZacatkuOtevreni = 0;

// --- PROMĚNNÉ PRO I2C KOMUNIKACI ---
volatile bool pozadavekHrat0001 = false; 
volatile bool pozadavekZastavitVse = false;  
volatile bool pozadavekHratKrystaly = false; // Vlajka pro 'K'

// Paměť pro ignorování přechodu zpět ze stavu '1'
volatile char lastMasterCommand = '0'; 

void setup() {
  Serial.begin(115200);
  Serial.println(F("Startuji system (I2C povely: '0', '3', 'K' - povel '1' je ignorovan)..."));

  pinMode(PIN_DVERE, INPUT_PULLUP);

  if (!filePlayer.begin()) { Serial.println(F("Chyba VS1053!")); while(1); }
  if (!SD.begin(CARD_CS)) { Serial.println(F("Chyba SD!")); while(1); }

  filePlayer.setVolume(MAX_HLASITOST, MAX_HLASITOST);

  Wire.begin(I2C_ADRESA_ARDUINA);
  Wire.onReceive(prijemDatI2C); 
  
  Serial.println(F("System pripraven. Hraji /0001.mp3."));
}

void loop() {
  // 1. NEUSTÁLÉ KRMENÍ AUDIA (POLLING)
  if (filePlayer.playingMusic) {
    filePlayer.feedBuffer();
  }

  // 2. ČTENÍ DVEŘÍ (S filtrem 1 sekunda)
  bool aktualniStavPinu = digitalRead(PIN_DVERE);
  
  if (aktualniStavPinu != posledniStavPinu) {
    delay(50); 
    aktualniStavPinu = digitalRead(PIN_DVERE); 
    
    if (aktualniStavPinu == HIGH) {
      casZacatkuOtevreni = millis();
    } else {
      dvereOtevrenyPotvrzeno = false; 
    }
    posledniStavPinu = aktualniStavPinu;
  }

  // Dveře vyvolají 0002 pouze, pokud hraje základní 0001
  if (aktualniStavPinu == HIGH && !dvereOtevrenyPotvrzeno && aktualniStav == HRAJE_0001) {
    if (millis() - casZacatkuOtevreni >= 1000) {
      Serial.println(F("PLATNE OTEVRENI (>1s)! Zacinam rychle zeslabovat 0001."));
      dvereOtevrenyPotvrzeno = true; 
      aktualniStav = ZESLABUJE_0001_NA_0002;
      posledniKrokZeslabeni = millis();
    }
  }

  // 3. VYHODNOCENÍ I2C ZPRÁV

  // Proměnná pro snazší zjištění, zda zrovna řešíme krystaly
  bool hrajeKrystaly = (aktualniStav == ZESLABUJE_PRED_KRYSTALY ||
                        aktualniStav == HRAJE_0003_HLASITE || 
                        aktualniStav == ZESLABUJE_0003);

  if (pozadavekHratKrystaly) {
    pozadavekHratKrystaly = false;
    
    // Pokud zrovna nehraje žádná hudba, přejdeme rovnou ke krystalům a spustíme naplno
    if (aktualniStav == TICHO || aktualniHlasitost >= MIN_HLASITOST) {
      Serial.println(F("I2C POVEL 'K' PRIJAT! Spoustim Vitezstvi (0003) naplno."));
      filePlayer.stopPlaying();
      aktualniHlasitost = MAX_HLASITOST_0003;
      filePlayer.setVolume(aktualniHlasitost, aktualniHlasitost);
      filePlayer.startPlayingFile("/0003.mp3");
      
      casZacatku0003 = millis();
      aktualniStav = HRAJE_0003_HLASITE;
    } 
    // Pokud něco hraje, spustíme bleskový fade-out
    else {
      Serial.println(F("I2C POVEL 'K' PRIJAT! Zacinam kratky fade-out pred krystaly."));
      aktualniStav = ZESLABUJE_PRED_KRYSTALY;
      posledniKrokZeslabeni = millis();
    }
  }

  if (pozadavekZastavitVse) {
    pozadavekZastavitVse = false;
    // Tento povel ('3' - Pracovní mód) jako JEDINÝ dokáže zastavit i krystaly
    Serial.println(F("I2C POVEL '3' PRIJAT! Okamzite zastavuji hudbu (Pracovni mod)."));
    filePlayer.stopPlaying();
    aktualniStav = TICHO;
  }

  if (pozadavekHrat0001) {
    pozadavekHrat0001 = false; 
    if (hrajeKrystaly) {
      Serial.println(F("I2C POVEL '0' IGNOROVAN - Krystaly maji absolutni prioritu!"));
    }
    else if (aktualniStav != HRAJE_0001 && aktualniStav != ZESLABUJE_ZPET_NA_0001) {
      Serial.println(F("I2C POVEL '0' PRIJAT! Zacinam rychle prepinat na 0001."));
      aktualniStav = ZESLABUJE_ZPET_NA_0001;
      posledniKrokZeslabeni = millis();
    }
  }

  // 4. HLAVNÍ LOGIKA STAVŮ
  switch (aktualniStav) {
    
    // --- STAVY PRO 0001 ---
    case HRAJE_0001:
      if (!filePlayer.playingMusic) {
        filePlayer.startPlayingFile("/0001.mp3");
      }
      break;

    case ZESLABUJE_ZPET_NA_0001:
      // Pokud hraje 0002 nebo 0003, neustále se obnovují, kdyby skončily během fade-outu
      if (!filePlayer.playingMusic) {
        if (pocetPrehrani0002 > 0) filePlayer.startPlayingFile("/0002.mp3");
        else filePlayer.startPlayingFile("/0003.mp3"); // fallback pro krystaly
      }

      if (millis() - posledniKrokZeslabeni > RYCHLOST_PRECHODU) {
        posledniKrokZeslabeni = millis();
        aktualniHlasitost++;
        filePlayer.setVolume(aktualniHlasitost, aktualniHlasitost);

        if (aktualniHlasitost >= MIN_HLASITOST) {
          filePlayer.stopPlaying();
          delay(50);
          filePlayer.startPlayingFile("/0001.mp3");
          delay(50);

          aktualniHlasitost = MAX_HLASITOST;
          filePlayer.setVolume(aktualniHlasitost, aktualniHlasitost);
          pocetPrehrani0002 = 0; // Reset dveří
          aktualniStav = HRAJE_0001;
          Serial.println(F("Prepnuto zpet. Hraje 0001 smycka."));
        }
      }
      break;


    // --- STAVY PRO 0002 (DVEŘE) ---
    case ZESLABUJE_0001_NA_0002:
      if (millis() - posledniKrokZeslabeni > RYCHLOST_PRECHODU) {
        posledniKrokZeslabeni = millis();
        aktualniHlasitost++; 
        filePlayer.setVolume(aktualniHlasitost, aktualniHlasitost);

        if (aktualniHlasitost >= MIN_HLASITOST) {
          filePlayer.stopPlaying();
          delay(50); 
          filePlayer.startPlayingFile("/0002.mp3"); 
          delay(50); 
          
          aktualniHlasitost = START_HLASITOST_0002;
          filePlayer.setVolume(aktualniHlasitost, aktualniHlasitost);
          
          pocetPrehrani0002 = 1; 
          casZacatku0002 = millis();
          aktualniStav = CEKA_A_HRAJE_0002_HLASITE;
          Serial.println(F("Hraje 0002 na zakladni hlasitost pro tuto skladbu (30s)."));
        }
      }
      break;

    case CEKA_A_HRAJE_0002_HLASITE:
      if (!filePlayer.playingMusic) {
        if (pocetPrehrani0002 < MAX_PREHRANI_0002) {
          pocetPrehrani0002++;
          aktualniHlasitost = CILOVA_HLASITOST_0002;
          filePlayer.setVolume(aktualniHlasitost, aktualniHlasitost);
          filePlayer.startPlayingFile("/0002.mp3");
          aktualniStav = HRAJE_0002_TISE;
        } else {
          aktualniStav = TICHO;
        }
        break; 
      }
      if (millis() - casZacatku0002 > CAS_PRED_ZTISENIM_0002) {
        aktualniStav = ZESLABUJE_0002_NA_TISSI;
        posledniKrokZeslabeni = millis();
      }
      break;

    case ZESLABUJE_0002_NA_TISSI:
      if (!filePlayer.playingMusic) {
        if (pocetPrehrani0002 < MAX_PREHRANI_0002) {
          pocetPrehrani0002++;
          aktualniHlasitost = CILOVA_HLASITOST_0002;
          filePlayer.setVolume(aktualniHlasitost, aktualniHlasitost);
          filePlayer.startPlayingFile("/0002.mp3");
          aktualniStav = HRAJE_0002_TISE;
        } else {
          aktualniStav = TICHO;
        }
        break;
      }
      if (millis() - posledniKrokZeslabeni > RYCHLOST_ZTISOVANI_0002) {
        posledniKrokZeslabeni = millis();
        aktualniHlasitost++;
        filePlayer.setVolume(aktualniHlasitost, aktualniHlasitost);
        if (aktualniHlasitost >= CILOVA_HLASITOST_0002) {
          aktualniStav = HRAJE_0002_TISE;
        }
      }
      break;

    case HRAJE_0002_TISE:
      if (!filePlayer.playingMusic) {
        if (pocetPrehrani0002 < MAX_PREHRANI_0002) {
          pocetPrehrani0002++;
          filePlayer.setVolume(CILOVA_HLASITOST_0002, CILOVA_HLASITOST_0002);
          filePlayer.startPlayingFile("/0002.mp3");
        } else {
          aktualniStav = TICHO;
        }
      }
      break;


    // --- NOVÉ STAVY PRO KRYSTALY (0003) ---
    
    case ZESLABUJE_PRED_KRYSTALY:
      // Krátký fade-out hudby, která hrála před krystaly
      if (millis() - posledniKrokZeslabeni > RYCHLOST_RYCHLEHO_ZESLABENI) {
        posledniKrokZeslabeni = millis();
        aktualniHlasitost++;
        filePlayer.setVolume(aktualniHlasitost, aktualniHlasitost);

        if (aktualniHlasitost >= MIN_HLASITOST) {
          filePlayer.stopPlaying();
          delay(50);
          
          aktualniHlasitost = MAX_HLASITOST_0003;
          filePlayer.setVolume(aktualniHlasitost, aktualniHlasitost);
          filePlayer.startPlayingFile("/0003.mp3");
          
          casZacatku0003 = millis(); // Startujeme odpočet 10s
          aktualniStav = HRAJE_0003_HLASITE;
          Serial.println(F("Predchozi hudba zeslabena. Startuji 0003 hned naplno na 10s."));
        }
      }
      break;

    case HRAJE_0003_HLASITE:
      if (!filePlayer.playingMusic) {
        // Pokud je skladba kratší než 10s, pustíme ji znovu naplno
        filePlayer.startPlayingFile("/0003.mp3");
      }
      // Po 10 sekundách začne minutový zrychlující se fade-out DO NULY
      if (millis() - casZacatku0003 > CAS_PRED_ZTISENIM_0003) {
        Serial.println(F("10s hlasitych krystalu uplynulo. Zacinam 1 minutu logaritmicky zeslabovat..."));
        aktualniStav = ZESLABUJE_0003;
        casZacatkuZeslabovani0003 = millis(); // Uložíme čas startu křivky
      }
      break;

    case ZESLABUJE_0003:
      if (!filePlayer.playingMusic) {
        filePlayer.startPlayingFile("/0003.mp3"); // Udržení smyčky
      }
      
      {
        unsigned long uplynuloOdZacatkuZeslabovani = millis() - casZacatkuZeslabovani0003;
        
        // Zeslabování postupuje až do úrovně MIN_HLASITOST (254) během 60 000 ms
        if (uplynuloOdZacatkuZeslabovani >= 60000) {
          filePlayer.stopPlaying(); // Fyzicky zastavíme hudbu
          aktualniStav = TICHO;     // Přejdeme do ticha
          Serial.println(F("Skladba 0003 plne zeslabena do nuly. Prestavam hrat."));
        } else {
          // Výpočet exponenciální / křivkové progrese
          float progress = (float)uplynuloOdZacatkuZeslabovani / 60000.0;
          int novaHlasitost = MAX_HLASITOST_0003 + (int)((MIN_HLASITOST - MAX_HLASITOST_0003) * progress * progress * progress);
          
          if (novaHlasitost > 254) novaHlasitost = 254; // Bezpečnostní pojistka maxima

          if (novaHlasitost != aktualniHlasitost) {
            aktualniHlasitost = novaHlasitost;
            filePlayer.setVolume(aktualniHlasitost, aktualniHlasitost);
          }
        }
      }
      break;

    case TICHO:
      // Čekání
      break;
  }
}

// --- FUNKCE PRO ZPRACOVÁNÍ I2C ZPRÁVY ---
void prijemDatI2C(int pocetBytu) {
  while (Wire.available()) {
    char c = Wire.read(); 
    
    if (c == CMD_PLAY_MAIN) {
      // Zabráníme restartování hudby, pokud jsme se sem vrátili ze stavu '1'
      if (lastMasterCommand != '1') {
        pozadavekHrat0001 = true;
      }
      lastMasterCommand = CMD_PLAY_MAIN;
      
    } else if (c == '1') {
      lastMasterCommand = '1';
      // Samotný povel 1 (Lasery) je ignorován
      
    } else if (c == CMD_STOP) {
      pozadavekZastavitVse = true; 
      lastMasterCommand = CMD_STOP;
      
    } else if (c == CMD_PLAY_VICTORY) {
      pozadavekHratKrystaly = true;
      lastMasterCommand = CMD_PLAY_VICTORY;
    }
  }
}