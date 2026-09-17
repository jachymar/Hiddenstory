#include <Wire.h>
#include <HardwareSerial.h>
#include <WiFi.h>
#include <esp_now.h>
#include <SPI.h>
#include <EthernetENC.h>

// --- ETHERNET (ENC28J60) NASTAVENÍ ---
const int ETH_CS = 5;
// SCK = 18, MISO = 19, MOSI = 23 (výchozí VSPI piny)

byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
IPAddress staticIP(192, 168, 0, 100);
IPAddress gateway(192, 168, 0, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress dnsServer(192, 168, 0, 1);

EthernetServer server(80);
bool eth_connected = false;

// --- I2C ADRESY ARDUIN ---
const int ADDR_TLACITKA       = 11; // Modul tlačítek pro barvy a schránky (B)
const int ADDR_KOLA           = 12; // Modul kol / analogů (C)
const int ADDR_LASER          = 13; // Modul hlavní páčky a laserů
const int ADDR_SVETLA         = 14; // Modul Světla (kaskáda LED)
const int ADDR_LEBKA          = 15; // Modul pro načtení krystalů a schránku Lebka (A)
const int ADDR_AUDIO          = 16; // Modul pro zvuky a hudbu

// --- ESP-NOW STRUKTURY ---
typedef struct struct_msg_to_m3 {
  uint8_t target_i2c;
  char command;
} struct_msg_to_m3;

typedef struct struct_msg_from_m3 {
  uint8_t tukani_status;
  uint8_t tukani_lock;
  uint8_t tukani_taps;
  uint8_t audio3_status;
  uint8_t audio3_is_playing;
  uint8_t audio3_is_alarm;
  char tukani_log[30];
  char audio3_log[30];
} struct_msg_from_m3;

struct_msg_from_m3 m3_data = {0,0,0,0,0,0,0,0};
uint8_t addr_esp32_3[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}; // TODO: MAC ADRESA ESP32_3

void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
  if (len == sizeof(struct_msg_from_m3)) {
    memcpy(&m3_data, incomingData, sizeof(m3_data));
  }
}

void posliPrikazM3(uint8_t target_i2c, char cmd) {
  struct_msg_to_m3 msg;
  msg.target_i2c = target_i2c;
  msg.command = cmd;
  esp_now_send(addr_esp32_3, (uint8_t *) &msg, sizeof(msg));
  Serial.print("ESP-NOW Odeslano '"); Serial.print(cmd); 
  Serial.print("' na I2C "); Serial.println(target_i2c);
}

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

bool devModeActive = false; // Příznak Developer Módu
int activeDiagModule = 0;   // Vybraný modul pro diagnostiku
String diagText = "Zadny modul neni vybran..."; // Textový output hodnot
String diagLogMsg = ""; // Poslední log zpráva
unsigned long posledniDiagTime = 0;

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

// Rychlé čtení 1 bajtu pro běžnou hru
uint8_t readI2CBasic(int adresa) {
  Wire.requestFrom((uint8_t)adresa, (uint8_t)1);
  if (Wire.available()) {
    return Wire.read();
  }
  return 255;
}

// Funkce pro bezpečné vyčtení struktury z Arduina v Developer Módu
template <typename T>
bool readI2CDiagnostics(int adresa, T &data) {
  Wire.beginTransmission(adresa);
  Wire.write(0x99); // Cílené vyžádání detailní diagnostiky
  Wire.endTransmission();
  delay(1); // Krátká prodleva pro slave, aby nastavil flag před čtením
  
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

// Funkce pro vyčtení posledního Log stringu z Arduina
String readI2CLog(int adresa) {
  Wire.beginTransmission(adresa);
  Wire.write(0x98); // Příkaz pro log string
  Wire.endTransmission();
  delay(1);
  
  Wire.requestFrom((uint8_t)adresa, (uint8_t)30);
  String l = "";
  while (Wire.available()) {
    char c = Wire.read();
    if (c != 0 && c != 255) l += c;
  }
  return l;
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
  json += "\"laser\":{\"st\":" + String(dataLaser.status) + ",\"on\":" + String(dataLaser.laser_on) + ",\"ldr\":" + String(dataLaser.ldr_val) + ",\"fail\":" + String(dataLaser.fails) + ",\"ovr\":" + String(dataLaser.override_btn) + "},";
  json += "\"m3_tukani\":{\"st\":" + String(m3_data.tukani_status) + ",\"lock\":" + String(m3_data.tukani_lock) + ",\"taps\":" + String(m3_data.tukani_taps) + "},";
  json += "\"m3_audio\":{\"st\":" + String(m3_data.audio3_status) + ",\"play\":" + String(m3_data.audio3_is_playing) + ",\"alarm\":" + String(m3_data.audio3_is_alarm) + "}";
  
  String safeText = diagText;
  safeText.replace("\n", "\\n");
  safeText.replace("\"", "\\\"");
  json += ",\"diag_text\":\"" + safeText + "\"";

  String safeLog = diagLogMsg;
  safeLog.replace("\n", "\\n");
  safeLog.replace("\"", "\\\"");
  json += ",\"diag_log\":\"" + safeLog + "\"";
  
  json += "}";
  return json;
}

void posliTelemetryState(unsigned long ted) {
  if (ted - posledniStateSendMs < 200) return;

  String json = buildTelemetryState();
  if (json != posledniStateJson) {
    posledniStateJson = json;
    // Odesílání přes BLE odstraněno. Telemetrie se nyní čte na vyžádání přes HTTP API.
  }

  posledniStateSendMs = ted;
}

void processCommand(String msg) {
  msg.trim();
  if (msg.length() > 0) {
    char cmd = msg.charAt(0);
    
    // Změna Módu (0, 1, 2, 3) z webové aplikace
    if (cmd >= '0' && cmd <= '3') {
      int novyMod = cmd - '0';
      lastGameMode = novyMod;
      processGameModeChange(lastGameMode);
      Serial.print("Web vnutil novy rezim: "); Serial.println(cmd);
    }
    // Povel pro tajnou schránku (A, B, C, D, E) z webové aplikace
    else if (cmd >= 'A' && cmd <= 'E') {
      Serial.print("Web žada otevreni schranky: "); Serial.println(cmd);
      if (cmd == 'A') posliPrikazI2C(ADDR_LEBKA, 'A');
      else if (cmd == 'B') posliPrikazI2C(ADDR_TLACITKA, 'B');
      else if (cmd == 'C') posliPrikazI2C(ADDR_KOLA, 'C');
      else if (cmd == 'D') Serial.println("POZOR: Oltar zatim nema I2C adresu!");
      else if (cmd == 'E') posliPrikazM3(20, 'O');
    }
    // Developer Mód (X1 / X0)
    else if (cmd == 'X') {
      int val = msg.substring(1).toInt();
      devModeActive = (val == 1);
      Serial.print("Master: Dev Mod nastaven na: "); Serial.println(devModeActive);
      Serial2.println(msg); // Preposlat do WLED (pro zmenu jeho chovani, pokud by bylo treba)
      if (!devModeActive) { activeDiagModule = 0; diagText = "Zadny modul neni vybran..."; diagLogMsg = ""; }
    }
    // Diagnostický dotaz (D0, D1, D2...)
    else if (cmd == 'D') {
      activeDiagModule = msg.substring(1).toInt();
      Serial.print("Vybran diag modul: "); Serial.println(activeDiagModule);
      diagText = "Nacitam detailni data z modulu " + String(activeDiagModule) + "...";
      diagLogMsg = "";
    }
  }
}

// --- ZPRACOVÁNÍ HTTP POŽADAVKŮ (EthernetServer) ---
void handleHttpClient() {
  EthernetClient client = server.available();
  if (!client) return;

  String currentLine = "";
  String reqMethod = "";
  String reqPath = "";
  int contentLength = 0;
  bool isHeader = true;
  unsigned long startWait = millis();

  while (client.connected() && (millis() - startWait < 1500)) {
    if (client.available()) {
      startWait = millis();
      char c = client.read();

      if (isHeader) {
        if (c == '\n') {
          if (currentLine.length() == 0) {
            // Prázdný řádek značí konec HTTP hlaviček
            isHeader = false;
            
            if (reqMethod == "OPTIONS") {
              client.println("HTTP/1.1 204 No Content");
              client.println("Access-Control-Allow-Origin: *");
              client.println("Access-Control-Allow-Methods: POST, GET, OPTIONS");
              client.println("Access-Control-Allow-Headers: Content-Type");
              client.println("Connection: close");
              client.println();
              break;
            } else if (reqMethod == "GET" && reqPath == "/api/state") {
              client.println("HTTP/1.1 200 OK");
              client.println("Content-Type: application/json");
              client.println("Access-Control-Allow-Origin: *");
              client.println("Connection: close");
              client.println();
              client.println(posledniStateJson);
              break;
            } else if (reqMethod != "POST" || reqPath != "/api/command") {
              client.println("HTTP/1.1 404 Not Found");
              client.println("Connection: close");
              client.println();
              break;
            }
          } else {
            if (reqMethod == "") {
              int sp1 = currentLine.indexOf(' ');
              int sp2 = currentLine.indexOf(' ', sp1 + 1);
              if (sp1 != -1 && sp2 != -1) {
                reqMethod = currentLine.substring(0, sp1);
                reqPath = currentLine.substring(sp1 + 1, sp2);
              }
            }
            if (currentLine.startsWith("Content-Length:") || currentLine.startsWith("content-length:")) {
              contentLength = currentLine.substring(15).toInt();
            }
            currentLine = "";
          }
        } else if (c != '\r') {
          currentLine += c;
        }
      } else {
        // Čtení těla POST požadavku
        String body = "";
        body += c;
        while (client.available() && (contentLength == 0 || (int)body.length() < contentLength)) {
          body += (char)client.read();
        }
        processCommand(body);
        client.println("HTTP/1.1 200 OK");
        client.println("Content-Type: text/plain");
        client.println("Access-Control-Allow-Origin: *");
        client.println("Connection: close");
        client.println();
        client.println("OK");
        break;
      }
    }
  }
  delay(1);
  client.stop();
}

void setup() {
  // Debugování do počítače
  Serial.begin(115200);
  
  // Komunikace s ESP32 č.1 (Komunikační brány) přes Sériovou linku UART2
  // Piny RX=16, TX=17
  Serial2.begin(115200, SERIAL_8N1, 16, 17);
  
  // Inicializace I2C jako Master
  Wire.begin();
  
  // Inicializace ESP-NOW (WiFi mode musí být WIFI_STA)
  WiFi.mode(WIFI_STA);
  
  if (esp_now_init() != ESP_OK) {
    Serial.println("Chyba inicializace ESP-NOW");
  } else {
    esp_now_register_recv_cb(esp_now_recv_cb_t(OnDataRecv));
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, addr_esp32_3, 6);
    peerInfo.channel = 0;  
    peerInfo.encrypt = false;
    if (esp_now_add_peer(&peerInfo) != ESP_OK){
      Serial.println("Chyba pridani ESP-NOW peer");
    }
  }
  
  // Inicializace Ethernetu (ENC28J60) se statickou IP
  SPI.begin();
  Ethernet.init(ETH_CS);
  Serial.println("Inicializuji Ethernet (ENC28J60) se statickou IP...");
  Ethernet.begin(mac, staticIP, dnsServer, gateway, subnet);
  
  Serial.print("Ethernet nastaven na statickou IP: ");
  Serial.println(Ethernet.localIP());
  eth_connected = true;

  // Spuštění HTTP serveru
  server.begin();
  Serial.println("HTTP Server bezi na portu 80");

  casStartu = millis();
  Serial.println("ESP32 (Hlavni Mozek) byl uspesne nastartovan!");
  Serial.println("Cekam 7 vterin na srovnani senzoru...");
}

void loop() {
  handleHttpClient();
  Ethernet.maintain();
  unsigned long ted = millis();

  if (!inicializaceHotova && (ted - casStartu >= 7000)) {
    if (readI2CDiagnostics(ADDR_LASER, dataLaser)) physGameMode = dataLaser.status;
    if (readI2CDiagnostics(ADDR_TLACITKA, dataTlacitka)) physColorButton = dataTlacitka.status;
    if (readI2CDiagnostics(ADDR_LEBKA, dataLebka)) physCrystalsState = (dataLebka.crystals_mask == 7) ? 2 : 0;
    readI2CDiagnostics(ADDR_KOLA, dataKola); 
    readI2CDiagnostics(ADDR_SVETLA, dataSvetla);
    
    lastGameMode = physGameMode; 
    lastColorButton = physColorButton; 
    lastCrystalsState = physCrystalsState;
    
    // Oznámí stavy komunikační brány
    processGameModeChange(lastGameMode);
    Serial2.println("C" + String(lastColorButton));
    Serial2.println("K" + String(lastCrystalsState));
    
    inicializaceHotova = true;
    Serial.println("Kalibrace dokoncena, system bezi!");
  }
  
  if (!inicializaceHotova) return;

  // --- 1. NORMÁLNÍ REŽIM: RYCHLÉ ČTENÍ ZÁKLADNÍCH STAVŮ (1 BAJT) ---
  // Běží vždy, aby fungovala hra (bez zatížení I2C)
  if (ted - posledniI2C_Lasery >= 30) {
    posledniI2C_Lasery = ted;
    uint8_t s = readI2CBasic(ADDR_LASER);
    if (s != 255 && s != physGameMode) { 
      int staryPhysMode = physGameMode;
      physGameMode = s; 
      
      bool ignoruj = false;
      if (lastGameMode == 3) {
        if (s == 1) ignoruj = true; 
        else if (s == 0 && staryPhysMode == 1) ignoruj = true; 
      }

      if (!ignoruj) {
        lastGameMode = s; 
        processGameModeChange(s); 
      }
    }
  }

  if (ted - posledniI2C >= 150) {
    posledniI2C = ted;

    if (lastGameMode == 0 || lastGameMode == 2 || lastGameMode == 3) { 
      bool zmenaTlacitek = false;
      bool zmenaLebky = false;
      int s3 = physColorButton, s8 = physCrystalsState;

      uint8_t prectenoTlacitka = readI2CBasic(ADDR_TLACITKA);
      if (prectenoTlacitka != 255 && prectenoTlacitka != physColorButton && prectenoTlacitka != 3) { 
        s3 = prectenoTlacitka; 
        physColorButton = prectenoTlacitka; 
        zmenaTlacitek = true; 
      }
      
      uint8_t prectenoLebka = readI2CBasic(ADDR_LEBKA);
      if (prectenoLebka != 255 && prectenoLebka != physCrystalsState) { 
        s8 = prectenoLebka; 
        physCrystalsState = prectenoLebka; 
        zmenaLebky = true; 
      }
      
      if (zmenaTlacitek) {
        lastColorButton = s3;
        Serial2.println("C" + String(lastColorButton)); 
      }
      
      if (zmenaLebky) {
        if (lastGameMode == 0) {
          if (s8 == 2 && lastCrystalsState != 2) posliPrikazI2C(ADDR_AUDIO, 'K');
          else if (s8 != 2 && lastCrystalsState == 2) posliPrikazI2C(ADDR_AUDIO, '0');
        }
        lastCrystalsState = s8;
        Serial2.println("K" + String(lastCrystalsState)); 
      }
    }
  }
  
  // --- 2. DIAGNOSTIKA NA VYŽÁDÁNÍ (Pouze v Dev Módu pro vybraný modul) ---
  if (devModeActive && activeDiagModule > 0 && (ted - posledniDiagTime >= 400)) {
    posledniDiagTime = ted;
    
    switch (activeDiagModule) {
      case 1: 
        if (readI2CDiagnostics(ADDR_LEBKA, dataLebka)) {
          diagText = "--- LEBKA (I2C: 15) ---\nStav automatu: " + String(dataLebka.status) +
                    "\nKrystaly (Maska): " + String(dataLebka.crystals_mask) +
                    "\nAnalog K1: " + String(dataLebka.k1_val) +
                    "\nAnalog K2: " + String(dataLebka.k2_val) +
                    "\nAnalog K3: " + String(dataLebka.k3_val) +
                    "\nZamek Otevren: " + String(dataLebka.lock_open ? "ANO" : "NE");
          diagLogMsg = readI2CLog(ADDR_LEBKA);
        } else { diagText = "Chyba I2C komunikace s Lebkou!"; }
        break;
      case 2:
        if (readI2CDiagnostics(ADDR_SVETLA, dataSvetla)) {
          diagText = "--- SVETLA (I2C: 14) ---\nStav I2C: " + String(dataSvetla.status) +
                    "\nCyklus bezi: " + String(dataSvetla.mode_running ? "ANO" : "NE") +
                    "\nAktualni LED faze: " + String(dataSvetla.current_led) +
                    "\nMagnet prumer (klid): " + String(dataSvetla.magnet_idle) +
                    "\nMagnet aktualne: " + String(dataSvetla.magnet_val);
          diagLogMsg = readI2CLog(ADDR_SVETLA);
        } else { diagText = "Chyba I2C komunikace se Svetly!"; }
        break;
      case 3:
        if (readI2CDiagnostics(ADDR_TLACITKA, dataTlacitka)) {
          diagText = "--- TLACITKA (I2C: 11) ---\nStav hesla: " + String(dataTlacitka.status) +
                    "\nStisky: " + String(dataTlacitka.presses) + "/4" +
                    "\nCas v klidu: " + String(dataTlacitka.idle_time) + " ms" +
                    "\nZamek Otevren: " + String(dataTlacitka.lock_open ? "ANO" : "NE");
          diagLogMsg = readI2CLog(ADDR_TLACITKA);
        } else { diagText = "Chyba I2C komunikace s Tlacitky!"; }
        break;
      case 4:
        if (readI2CDiagnostics(ADDR_KOLA, dataKola)) {
          diagText = "--- KOLA (I2C: 12) ---\nStav I2C: " + String(dataKola.status) +
                    "\nAktivni magnety (Maska): " + String(dataKola.active_mask) +
                    "\nAnalog 1: " + String(dataKola.a1_val) +
                    "\nAnalog 2: " + String(dataKola.a2_val) +
                    "\nAnalog 3: " + String(dataKola.a3_val);
          diagLogMsg = readI2CLog(ADDR_KOLA);
        } else { diagText = "Chyba I2C komunikace s Koly!"; }
        break;
      case 5:
        if (readI2CDiagnostics(ADDR_LASER, dataLaser)) {
          diagText = "--- LASER (I2C: 13) ---\nStav I2C (0=OK, 1=Alarm, 3=Packa): " + String(dataLaser.status) +
                    "\nLaser Zapnut: " + String(dataLaser.laser_on ? "ANO" : "NE") +
                    "\nLDR Senzor: " + String(dataLaser.ldr_val) +
                    "\nPocet chyb trefeni: " + String(dataLaser.fails) +
                    "\nPacka Override: " + String(dataLaser.override_btn ? "AKTIVNI" : "NE");
          diagLogMsg = readI2CLog(ADDR_LASER);
        } else { diagText = "Chyba I2C komunikace s Laserem!"; }
        break;
      case 6:
        posliPrikazM3(0, 'R'); // 'R' = vyžádání diagnostiky z Místnosti 3
        diagText = "--- MISTNOST 3 (ESP-NOW) ---\n(Odeslan dotaz, data chodi asynchronne)\n\nTukani stav: " + String(m3_data.tukani_status) +
                  "\nTukani poklepy: " + String(m3_data.tukani_taps) +
                  "\nTukani zamek: " + String(m3_data.tukani_lock ? "OTEVREN" : "ZAVREN") +
                  "\n\nAudio3 stav: " + String(m3_data.audio3_status) +
                  "\nAudio3 hraje: " + String(m3_data.audio3_is_playing ? "ANO" : "NE") +
                  "\nAudio3 alarm: " + String(m3_data.audio3_is_alarm ? "ANO" : "NE");
        diagLogMsg = "Tuk: " + String(m3_data.tukani_log) + " | Aud: " + String(m3_data.audio3_log);
        break;
    }
    posliTelemetryState(ted); // Odeslani na web
  }
}