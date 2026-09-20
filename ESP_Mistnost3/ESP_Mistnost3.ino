#include <Wire.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

// --- I2C ADRESY ARDUIN V MISTNOSTI 3 ---
const int ADDR_TUKANI = 20;
const int ADDR_AUDIO3 = 21;

// --- ESP-NOW STRUKTURY ---
typedef struct struct_msg_from_master {
  uint8_t target_i2c;
  char command;
  uint8_t sys_mode;
  uint8_t sys_color;
  uint8_t sys_crystals;
} struct_msg_from_master;

typedef struct struct_msg_to_master {
  // Tukani
  uint8_t tukani_status;
  uint8_t tukani_lock;
  uint8_t tukani_taps;
  // Audio3
  uint8_t audio3_status;
  uint8_t audio3_is_playing;
  uint8_t audio3_is_alarm;
  uint8_t wled3_online;
  // Logy
  char tukani_log[30];
  char audio3_log[30];
} struct_msg_to_master;

struct_msg_to_master diag_data;

// Broadcast a Master MAC
uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
uint8_t master_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}; 
bool master_mac_known = false;

// Proměnné pro naši vlastní smyčku
unsigned long lastPingTime = 0;
bool beepReported = false; 

// --- DIAG STRUKTURY Z ARDUIN ---
struct DiagTukani {
  uint8_t status;
  uint8_t lock_open;
  uint8_t taps;
  uint8_t padding;
} __attribute__((packed));

struct DiagAudio3 {
  uint8_t status;
  uint8_t is_playing;
  uint8_t is_alarm_playing;
  uint8_t padding;
} __attribute__((packed));

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

// Rychlé čtení 1 bajtu (ping) z Arduina
uint8_t readI2CBasic(int adresa) {
  Wire.requestFrom((uint8_t)adresa, (uint8_t)1);
  if (Wire.available()) {
    return Wire.read();
  }
  return 255; // 255 znamená, že zařízení neodpovídá
}

uint8_t currentSysMode = 0;

// Callback pro prijem zprav pres ESP-NOW
void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
  if (len == sizeof(struct_msg_from_master)) {
    if (!master_mac_known) {
      memcpy(master_mac, info->src_addr, 6);
      master_mac_known = true;
      
      esp_now_peer_info_t peerInfo = {};
      memcpy(peerInfo.peer_addr, master_mac, 6);
      peerInfo.channel = 0;
      peerInfo.encrypt = false;
      esp_now_add_peer(&peerInfo);

      Serial.printf("Master ESP32 detekovan! MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                    master_mac[0], master_mac[1], master_mac[2],
                    master_mac[3], master_mac[4], master_mac[5]);
    }

    struct_msg_from_master msg;
    memcpy(&msg, incomingData, sizeof(msg));
    Serial.print("Prijat ESP-NOW prikaz '"); Serial.print(msg.command);
    Serial.print("' pro I2C "); Serial.print(msg.target_i2c);
    Serial.print(" | sys_mode: "); Serial.println(msg.sys_mode);
    
    // Změna módu
    if (msg.sys_mode != currentSysMode) {
      currentSysMode = msg.sys_mode;
      if (currentSysMode == 3) {
        posliPrikazI2C(ADDR_AUDIO3, '3');
      } else if (currentSysMode == 0) {
        posliPrikazI2C(ADDR_AUDIO3, '0');
      }
    }

    // Vyžádání diagnostiky
    if (msg.command == 'R') {
      DiagTukani tukani;
      if (readI2CDiagnostics(ADDR_TUKANI, tukani)) {
        diag_data.tukani_status = tukani.status;
        diag_data.tukani_lock = tukani.lock_open;
        diag_data.tukani_taps = tukani.taps;
      }
      DiagAudio3 audio3;
      if (readI2CDiagnostics(ADDR_AUDIO3, audio3)) {
        diag_data.audio3_status = audio3.status;
        diag_data.audio3_is_playing = audio3.is_playing;
        diag_data.audio3_is_alarm = audio3.is_alarm_playing;
      }
      
      // Vyčtení logů
      String tLog = readI2CLog(ADDR_TUKANI);
      strncpy(diag_data.tukani_log, tLog.c_str(), 29);
      diag_data.tukani_log[29] = '\0';
      
      String aLog = readI2CLog(ADDR_AUDIO3);
      strncpy(diag_data.audio3_log, aLog.c_str(), 29);
      diag_data.audio3_log[29] = '\0';

      const uint8_t *targetAddr = master_mac_known ? master_mac : broadcastAddress;
      esp_now_send(targetAddr, (uint8_t *) &diag_data, sizeof(diag_data));
    } 
    // Standardní I2C propis povelu
    else if (msg.command != 'U') { // 'U' je jen update bez I2C cíle
      posliPrikazI2C(msg.target_i2c, msg.command);
    }
  }
}

void setup() {
  Serial.begin(115200);
  Wire.begin(); // Master na I2C
  
  // ESP-NOW běží na STA
  WiFi.mode(WIFI_STA);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
  
  if (esp_now_init() != ESP_OK) {
    Serial.println("Chyba inicializace ESP-NOW");
    return;
  }
  esp_now_register_recv_cb(OnDataRecv);
  
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Chyba pridani broadcast ESP-NOW peer");
  } else {
    Serial.println("ESP-NOW inicializovano (Broadcast peer pripraven).");
  }
  
  Serial.println("ESP32_3 (Mistnost 3) pripraveno.");
  Serial.print("MAC adresa: ");
  Serial.println(WiFi.macAddress());
}
template <typename T>
bool readI2CDiagnostics(int adresa, T &data) {
  Wire.beginTransmission(adresa);
  Wire.write(0x99); // Cílené vyžádání detailní diagnostiky
  Wire.endTransmission();
  delay(1); 
  
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

// Cteni logu z I2C (0x98)
String readI2CLog(int adresa) {
  Wire.beginTransmission(adresa);
  Wire.write(0x98); 
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

void loop() {
  unsigned long ted = millis();

  // Každých 100 ms čteme základní stavy Arduin a reagujeme na ně
  // Heartbeat do Masteru pošleme jen 1x za 500 ms
  if (ted - lastPingTime > 100) {
    lastPingTime = ted;
    
    // Vyčteme zkrácený stav obou modulů (0, 1, 2... nebo 255 = offline)
    uint8_t rawTukani = readI2CBasic(ADDR_TUKANI);
    uint8_t rawAudio3 = readI2CBasic(ADDR_AUDIO3);

    // Filtrace krátkodobých výpadků I2C (debouncing - min. 10 cyklů = 1 s trvalého výpadku)
    static int tukaniFailCount = 0;
    static int audio3FailCount = 0;
    static uint8_t lastValidTukani = 0;
    static uint8_t lastValidAudio3 = 0;

    uint8_t tukaniBasic;
    if (rawTukani != 255) {
      tukaniFailCount = 0;
      lastValidTukani = rawTukani;
      tukaniBasic = rawTukani;
    } else {
      tukaniFailCount++;
      if (tukaniFailCount >= 10) {
        tukaniBasic = 255;
      } else {
        tukaniBasic = lastValidTukani; // Při krátkém zaškobrtnutí použijeme poslední známý stav
      }
    }

    uint8_t audio3Basic;
    if (rawAudio3 != 255) {
      audio3FailCount = 0;
      lastValidAudio3 = rawAudio3;
      audio3Basic = rawAudio3;
    } else {
      audio3FailCount++;
      if (audio3FailCount >= 10) {
        audio3Basic = 255;
      } else {
        audio3Basic = lastValidAudio3; // Při krátkém zaškobrtnutí použijeme poslední známý stav
      }
    }

    static bool solvedReported = false;

    // Zpracování stavů Tukani a předávání do Audio3 (pouze v herním režimu, ne v pracovním)
    if (currentSysMode != 3 && tukaniBasic != 255) {
      if (tukaniBasic == 1 && !beepReported) { 
        beepReported = true;
        Serial.println("Mistnost 3: Tukani zada o pipnuti (Povel E)");
        posliPrikazI2C(ADDR_AUDIO3, 'E'); 
      } else if (tukaniBasic == 2 && !solvedReported) {
        solvedReported = true;
        Serial.println("Mistnost 3: Tukani vyreseno (Povel D)");
        posliPrikazI2C(ADDR_AUDIO3, 'D');
      } else if (tukaniBasic == 0) {
        beepReported = false; 
        solvedReported = false;
      }
    }

    // Uložíme zjištěné stavy do diag_data
    diag_data.tukani_status = tukaniBasic;
    diag_data.audio3_status = audio3Basic;
    diag_data.wled3_online = 0; // Ponecháno pro kompatibilitu struktury

    // Pošleme Masteru heartbeat pouze občas (každý 5. cyklus = 500 ms)
    static int pings = 0;
    pings++;
    if (pings >= 5) {
      pings = 0;
      const uint8_t *targetAddr = master_mac_known ? master_mac : broadcastAddress;
      esp_now_send(targetAddr, (uint8_t *) &diag_data, sizeof(diag_data));
    }
  }
}
