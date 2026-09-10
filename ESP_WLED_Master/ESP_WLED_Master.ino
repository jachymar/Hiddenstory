#include <Wire.h>
#include <HardwareSerial.h>

// --- I2C ADRESY ARDUIN ---
const int ADDR_TLACITKA       = 11; // Modul tlačítek pro barvy a schránky (B)
const int ADDR_KOLA           = 12; // Modul kol / analogů (C)
const int ADDR_LASER          = 13; // Modul hlavní páčky a laserů
const int ADDR_SVETLA         = 14; // Modul Světla (kaskáda LED)
const int ADDR_LEBKA          = 15; // Modul pro načtení krystalů a schránku Lebka (A)
const int ADDR_AUDIO          = 16; // Modul pro zvuky a hudbu

// --- TELEMETRIE ---
struct DiagLebka {
  uint8_t status;
  uint8_t crystals_mask;
  uint16_t k1_val;
  uint16_t k2_val;
  uint16_t k3_val;
  uint8_t lock_open;
} __attribute__((packed));

struct DiagSvetla {
  uint8_t status;
  uint8_t mode_running;
  uint8_t current_led;
  uint16_t magnet_idle;
  uint16_t magnet_val;
} __attribute__((packed));

struct DiagKola {
  uint8_t status;
  uint8_t active_mask;
  uint16_t a1_val;
  uint16_t a2_val;
  uint16_t a3_val;
} __attribute__((packed));

struct DiagTlacitka {
  uint8_t status;
  uint8_t lock_open;
  uint8_t presses;
  uint16_t idle_time;
} __attribute__((packed));

struct DiagLaser {
  uint8_t status;
  uint8_t laser_on;
  uint16_t ldr_val;
  uint8_t fails;
  uint8_t override_btn;
} __attribute__((packed));

DiagLaser dataLaser = {0,0,0,0,0};
DiagTlacitka dataTlacitka = {0,0,0,0};
DiagKola dataKola = {0,0,0,0,0};
DiagSvetla dataSvetla = {0,0,0,0,0};
DiagLebka dataLebka = {0,0,0,0,0,0};

// --- STAVOVÉ PROMĚNNÉ HERNÍ LOGIKY ---
int lastGameMode = -1; // Režim hry: 0=Herní, 1=Vypnuto/Lasery, 3=Pracovní
int lastColorButton = -1; // Stisknuté tlačítko: 1=Červená, 2=Zelená, 0=Nic
int lastCrystalsState = -1; // Krystaly: 2=Všechny krystaly uvnitř, 0=Nic

int physGameMode = -1;
int physColorButton = -1;
int physCrystalsState = -1;

String posledniStateJson = "{}";
unsigned long posledniStateSendMs = 0;

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

// Funkce pro bezpečné vyčtení struktury z Arduina
template <typename T>
bool readI2CStruct(int adresa, T &data) {
  Wire.requestFrom((uint8_t)adresa, (uint8_t)sizeof(T));
  if (Wire.available() == sizeof(T)) {
    uint8_t* ptr = (uint8_t*)&data;
    for (size_t i = 0; i < sizeof(T); i++) {
      ptr[i] = Wire.read();
    }
    return true;
  }
  while (Wire.available()) Wire.read();
  return false;
}

// Zpracování hlavní systémové změny a informování Audia a Komunikační brány
void processGameModeChange(int mode) {
  // 1. Změna hudby podle nového stavu
  char audioPrikaz = '0' + mode; 
  if (mode == 0 && lastCrystalsState == 2) {
    audioPrikaz = 'K'; // Výjimka: Návrat do hry, ale krystaly už tam jsou
  }
  posliPrikazI2C(ADDR_AUDIO, audioPrikaz);

  // 2. Odeslání pokynu do Komunikační brány (ESP32 č.1)
  // Prefix 'M' znamená, že se mění hlavní Mód
  Serial2.println("M" + String(mode));
  
  Serial.print("Logika: Režim změněn na "); Serial.println(mode);
}

String buildTelemetryState() {
  String json = "{";
  json += "\"mode\":" + String(lastGameMode) + ",";
  json += "\"lebka\":{\"st\":" + String(dataLebka.status) + ",\"c_mask\":" + String(dataLebka.crystals_mask) + ",\"k1\":" + String(dataLebka.k1_val) + ",\"k2\":" + String(dataLebka.k2_val) + ",\"k3\":" + String(dataLebka.k3_val) + ",\"lock\":" + String(dataLebka.lock_open) + "},";
  json += "\"svetla\":{\"st\":" + String(dataSvetla.status) + ",\"run\":" + String(dataSvetla.mode_running) + ",\"led\":" + String(dataSvetla.current_led) + ",\"m_idl\":" + String(dataSvetla.magnet_idle) + ",\"m_val\":" + String(dataSvetla.magnet_val) + "},";
  json += "\"tlacitka\":{\"st\":" + String(dataTlacitka.status) + ",\"lock\":" + String(dataTlacitka.lock_open) + ",\"prs\":" + String(dataTlacitka.presses) + ",\"idl\":" + String(dataTlacitka.idle_time) + "},";
  json += "\"kola\":{\"st\":" + String(dataKola.status) + ",\"mask\":" + String(dataKola.active_mask) + ",\"a1\":" + String(dataKola.a1_val) + ",\"a2\":" + String(dataKola.a2_val) + ",\"a3\":" + String(dataKola.a3_val) + "},";
  json += "\"laser\":{\"st\":" + String(dataLaser.status) + ",\"on\":" + String(dataLaser.laser_on) + ",\"ldr\":" + String(dataLaser.ldr_val) + ",\"fail\":" + String(dataLaser.fails) + ",\"ovr\":" + String(dataLaser.override_btn) + "}";
  json += "}";
  return json;
}

void posliTelemetryState(unsigned long ted) {
  if (ted - posledniStateSendMs < 200) return;

  String json = buildTelemetryState();
  if (json != posledniStateJson) {
    posledniStateJson = json;
    Serial2.println("DIAG|" + json);
  }

  posledniStateSendMs = ted;
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
        int novyMod = cmd - '0';
        lastGameMode = novyMod;
        processGameModeChange(lastGameMode);
        Serial.print("Brana vnutila novy rezim: "); Serial.println(cmd);
      }
      // Povel pro tajnou schránku (A, B, C, D) z webové aplikace
      else if (cmd >= 'A' && cmd <= 'D') {
        Serial.print("Brana žada otevreni schranky: "); Serial.println(cmd);
        if (cmd == 'A') posliPrikazI2C(ADDR_LEBKA, 'A');
        else if (cmd == 'B') posliPrikazI2C(ADDR_TLACITKA, 'B');
        else if (cmd == 'C') posliPrikazI2C(ADDR_KOLA, 'C');
        else if (cmd == 'D') Serial.println("POZOR: Oltar zatim nema I2C adresu!");
      }
    }
  }

  if (!inicializaceHotova && (ted - casStartu >= 7000)) {
    if (readI2CStruct(ADDR_LASER, dataLaser)) physGameMode = dataLaser.status;
    if (readI2CStruct(ADDR_TLACITKA, dataTlacitka)) physColorButton = dataTlacitka.status;
    if (readI2CStruct(ADDR_LEBKA, dataLebka)) physCrystalsState = (dataLebka.crystals_mask == 7) ? 2 : 0;
    readI2CStruct(ADDR_KOLA, dataKola); 
    readI2CStruct(ADDR_SVETLA, dataSvetla);
    
    lastGameMode = physGameMode; 
    lastColorButton = physColorButton; 
    lastCrystalsState = physCrystalsState;
    
    // Oznámí stavy komunikační bráně
    processGameModeChange(lastGameMode);
    Serial2.println("C" + String(lastColorButton));
    Serial2.println("K" + String(lastCrystalsState));
    
    inicializaceHotova = true;
    Serial.println("Kalibrace dokoncena, system bezi!");
  }
  
  if (!inicializaceHotova) return;

  // RYCHLÉ ČTENÍ LASERŮ (každých 30 ms) pro nulový delay zhasnutí
  if (ted - posledniI2C_Lasery >= 30) {
    posledniI2C_Lasery = ted;
    
    if (readI2CStruct(ADDR_LASER, dataLaser)) {
      int s = dataLaser.status;
      if (s != physGameMode) { 
        int staryPhysMode = physGameMode;
        physGameMode = s; 
        
        bool ignoruj = false;
        
        // OCHRANA PRACOVNÍHO MÓDU PŘED LASERY
        if (lastGameMode == 3) {
          if (s == 1) {
            ignoruj = true; 
          } else if (s == 0 && staryPhysMode == 1) {
            ignoruj = true; 
          }
        }

        if (!ignoruj) {
          lastGameMode = s; 
          processGameModeChange(s); 
        }
      }
    }
  }

  // ČTENÍ TLAČÍTEK A KRYSTALŮ (každých 150 ms)
  if (ted - posledniI2C >= 150) {
    posledniI2C = ted;

    // Čteme je pouze, pokud nesvítí čistá tma z laserů (stav 1)
    if (lastGameMode == 0 || lastGameMode == 2 || lastGameMode == 3) { 
      bool zmenaTlacitek = false;
      bool zmenaLebky = false;
      int s3 = physColorButton, s8 = physCrystalsState;

      if (readI2CStruct(ADDR_TLACITKA, dataTlacitka)) {
        int s = dataTlacitka.status;
        if (s != physColorButton && s != 3) { s3 = s; physColorButton = s; zmenaTlacitek = true; }
      }
      
      if (readI2CStruct(ADDR_LEBKA, dataLebka)) {
        int s = (dataLebka.crystals_mask == 7) ? 2 : 0;
        if (s != physCrystalsState) { s8 = s; physCrystalsState = s; zmenaLebky = true; }
      }
      
      readI2CStruct(ADDR_KOLA, dataKola);
      readI2CStruct(ADDR_SVETLA, dataSvetla);
      
      // Byla stisknuta nová kombinace barev
      if (zmenaTlacitek) {
        lastColorButton = s3;
        // Odešleme do komunikační brány s prefixem C (Colors)
        Serial2.println("C" + String(lastColorButton)); 
      }
      
      // Byly změněny krystaly v lebce
      if (zmenaLebky) {
        // Hudební odezva na krystaly (pouze v Herním módu)
        if (lastGameMode == 0) {
          if (s8 == 2 && lastCrystalsState != 2) {
            posliPrikazI2C(ADDR_AUDIO, 'K'); // Krystaly jsou tam, hraj výhru
          } else if (s8 != 2 && lastCrystalsState == 2) {
            posliPrikazI2C(ADDR_AUDIO, '0'); // Vytaženy, vrať hru
          }
        }
        
        lastCrystalsState = s8;
        // Odešleme do komunikační brány s prefixem K (Krystaly)
        Serial2.println("K" + String(lastCrystalsState)); 
      }
    }
  }

  posliTelemetryState(ted);
}