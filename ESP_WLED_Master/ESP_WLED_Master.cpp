#include <Wire.h>
#include <HardwareSerial.h>

// --- I2C ADRESY ARDUIN ---
const int ADDR_LASERY         = 6;  // Modul hlavní páčky a laserů
const int ADDR_TLACITKA       = 3;  // Modul tlačítek pro barvy a schránky (B, C)
const int ADDR_SVETLA_LEBKA   = 8;  // Modul pro načtení krystalů a schránku Lebka (A)
const int ADDR_AUDIO          = 10; // Modul pro zvuky a hudbu

// --- STAVOVÉ PROMĚNNÉ HERNÍ LOGIKY ---
int posledniS6 = -1; // Režim: 0=Herní, 1=Vypnuto/Lasery, 3=Pracovní
int posledniS3 = -1; // Tlačítka: 1=Červená, 2=Zelená, 0=Nic
int posledniS8 = -1; // Krystaly: 2=Všechny krystaly uvnitř, 0=Nic

int fyzickeS6 = -1;
int fyzickeS3 = -1;
int fyzickeS8 = -1; 

bool inicializaceHotova = false;
unsigned long casStartu = 0;
unsigned long posledniI2C = 0;
unsigned long posledniI2C_Lasery = 0;

// Funkce pro odeslání 1 znaku (povelu) podřízenému Arduinu přes I2C
void posliPrikazI2C(int adresa, char prikaz) {
  Wire.beginTransmission(adresa);
  Wire.write(prikaz);
  byte error = Wire.endTransmission();
  
  if (error == 0) {
    Serial.print("I2C Odeslano '"); Serial.print(prikaz); 
    Serial.print("' na adresu "); Serial.println(adresa);
  } else {
    Serial.print("Chyba I2C komunikace s adresou "); Serial.println(adresa);
  }
}

// Zpracování hlavní systémové změny a informování Audia a Komunikační brány
void zpracujZmenuS6(int stav) {
  // 1. Změna hudby podle nového stavu
  char audioPrikaz = '0' + stav; 
  if (stav == 0 && posledniS8 == 2) {
    audioPrikaz = 'K'; // Výjimka: Návrat do hry, ale krystaly už tam jsou
  }
  posliPrikazI2C(ADDR_AUDIO, audioPrikaz);

  // 2. Odeslání pokynu do Komunikační brány (ESP32 č.1)
  // Prefix 'M' znamená, že se mění hlavní Mód
  Serial2.println("M" + String(stav));
  
  Serial.print("Logika: Režim změněn na "); Serial.println(stav);
}

void setup() {
  // Debugování do počítače
  Serial.begin(115200);
  
  // Komunikace s ESP32 č.1 (Komunikační bránou) přes Sériovou linku UART2
  // Piny RX=16, TX=17
  Serial2.begin(115200, SERIAL_8N1, 16, 17);
  
  // Inicializace I2C jako Master
  Wire.begin();
  
  casStartu = millis();
  Serial.println("ESP32 (Hlavni Mozek) byl uspesne nastartovan!");
  Serial.println("Cekam 7 vterin na srovnani senzoru...");
}

void loop() {
  unsigned long ted = millis();

  // --- ZPRACOVÁNÍ PŘÍCHOZÍCH ZPRÁV Z KOMUNIKAČNÍ BRÁNY (PŘES BLUETOOTH) ---
  if (Serial2.available()) {
    String msg = Serial2.readStringUntil('\n');
    msg.trim();
    
    if (msg.length() > 0) {
      char cmd = msg.charAt(0);
      
      // Změna Módu (0, 1, 2, 3) z webové aplikace
      if (cmd >= '0' && cmd <= '3') {
        int novyS6 = cmd - '0';
        posledniS6 = novyS6;
        zpracujZmenuS6(posledniS6);
        Serial.print("Brana vnutila novy rezim: "); Serial.println(cmd);
      }
      // Povel pro tajnou schránku (A, B, C, D) z webové aplikace
      else if (cmd >= 'A' && cmd <= 'D') {
        Serial.print("Brana žada otevreni schranky: "); Serial.println(cmd);
        if (cmd == 'A') posliPrikazI2C(ADDR_SVETLA_LEBKA, 'A');
        else if (cmd == 'B') posliPrikazI2C(ADDR_TLACITKA, 'B');
        else if (cmd == 'C') posliPrikazI2C(ADDR_TLACITKA, 'C');
        else if (cmd == 'D') Serial.println("POZOR: Oltar zatim nema I2C adresu!");
      }
    }
  }

  if (!inicializaceHotova && (ted - casStartu >= 7000)) {
    Wire.requestFrom(ADDR_LASERY, 1); if (Wire.available()) fyzickeS6 = Wire.read();
    Wire.requestFrom(ADDR_TLACITKA, 1); if (Wire.available()) fyzickeS3 = Wire.read();
    Wire.requestFrom(ADDR_SVETLA_LEBKA, 1); if (Wire.available()) fyzickeS8 = Wire.read();
    
    posledniS6 = fyzickeS6; 
    posledniS3 = fyzickeS3; 
    posledniS8 = fyzickeS8;
    
    // Oznámí stavy komunikační bráně
    zpracujZmenuS6(posledniS6);
    Serial2.println("C" + String(posledniS3));
    Serial2.println("K" + String(posledniS8));
    
    inicializaceHotova = true;
    Serial.println("Kalibrace dokoncena, system bezi!");
  }
  
  if (!inicializaceHotova) return;

  // RYCHLÉ ČTENÍ LASERŮ (každých 30 ms) pro nulový delay zhasnutí
  if (ted - posledniI2C_Lasery >= 30) {
    posledniI2C_Lasery = ted;
    
    Wire.requestFrom(ADDR_LASERY, 1);
    if (Wire.available()) {
      int s = Wire.read();
      if (s != fyzickeS6) { 
        int staryFyzickeS6 = fyzickeS6;
        fyzickeS6 = s; 
        
        bool ignoruj = false;
        
        // OCHRANA PRACOVNÍHO MÓDU PŘED LASERY
        if (posledniS6 == 3) {
          if (s == 1) {
            ignoruj = true; 
          } else if (s == 0 && staryFyzickeS6 == 1) {
            ignoruj = true; 
          }
        }

        if (!ignoruj) {
          posledniS6 = s; 
          zpracujZmenuS6(s); 
        }
      }
    }
  }

  // ČTENÍ TLAČÍTEK A KRYSTALŮ (každých 150 ms)
  if (ted - posledniI2C >= 150) {
    posledniI2C = ted;

    // Čteme je pouze, pokud nesvítí čistá tma z laserů (stav 1)
    if (posledniS6 == 0 || posledniS6 == 2 || posledniS6 == 3) { 
      bool zmenaTlacitek = false;
      bool zmenaLebky = false;
      int s3 = fyzickeS3, s8 = fyzickeS8;

      Wire.requestFrom(ADDR_TLACITKA, 1);
      if (Wire.available()) {
        int s = Wire.read();
        if (s != fyzickeS3 && s != 3) { s3 = s; fyzickeS3 = s; zmenaTlacitek = true; }
      }
      
      Wire.requestFrom(ADDR_SVETLA_LEBKA, 1);
      if (Wire.available()) {
        int s = Wire.read();
        if (s != fyzickeS8) { s8 = s; fyzickeS8 = s; zmenaLebky = true; }
      }
      
      // Byla stisknuta nová kombinace barev
      if (zmenaTlacitek) {
        posledniS3 = s3;
        // Odešleme do komunikační brány s prefixem C (Colors)
        Serial2.println("C" + String(posledniS3)); 
      }
      
      // Byly změněny krystaly v lebce
      if (zmenaLebky) {
        // Hudební odezva na krystaly (pouze v Herním módu)
        if (posledniS6 == 0) {
          if (s8 == 2 && posledniS8 != 2) {
            posliPrikazI2C(ADDR_AUDIO, 'K'); // Krystaly jsou tam, hraj výhru
          } else if (s8 != 2 && posledniS8 == 2) {
            posliPrikazI2C(ADDR_AUDIO, '0'); // Vytaženy, vrať hru
          }
        }
        
        posledniS8 = s8;
        // Odešleme do komunikační brány s prefixem K (Krystaly)
        Serial2.println("K" + String(posledniS8)); 
      }
    }
  }
}