#include <Wire.h>
#include <WiFi.h>
#include <esp_now.h>

// --- I2C ADRESY ARDUIN V MISTNOSTI 3 ---
const int ADDR_TUKANI = 20;
const int ADDR_AUDIO3 = 21;

// --- ESP-NOW STRUKTURY ---
typedef struct struct_msg_from_master {
  uint8_t target_i2c;
  char command;
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
  // Logy
  char tukani_log[30];
  char audio3_log[30];
} struct_msg_to_master;

struct_msg_to_master diag_data;

// Odkud nam prisla data (Master)
uint8_t master_mac[6] = {0}; 
bool master_mac_known = false;

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
  }
}

// Callback pro prijem zprav pres ESP-NOW
void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
  if (len == sizeof(struct_msg_from_master)) {
    if (!master_mac_known) {
      memcpy(master_mac, mac, 6);
      master_mac_known = true;
      
      esp_now_peer_info_t peerInfo = {};
      memcpy(peerInfo.peer_addr, master_mac, 6);
      peerInfo.channel = 0;
      peerInfo.encrypt = false;
      esp_now_add_peer(&peerInfo);
    }

    struct_msg_from_master msg;
    memcpy(&msg, incomingData, sizeof(msg));
    Serial.print("Prijat ESP-NOW prikaz '"); Serial.print(msg.command);
    Serial.print("' pro I2C "); Serial.println(msg.target_i2c);
    
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

      if (master_mac_known) {
        esp_now_send(master_mac, (uint8_t *) &diag_data, sizeof(diag_data));
      }
    } 
    // Standardní I2C propis povelu
    else {
      posliPrikazI2C(msg.target_i2c, msg.command);
    }
  }
}

void setup() {
  Serial.begin(115200);
  Wire.begin(); // Master na I2C
  
  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) {
    Serial.println("Chyba inicializace ESP-NOW");
    return;
  }
  esp_now_register_recv_cb(esp_now_recv_cb_t(OnDataRecv));
  
  Serial.println("ESP32_3 (Mistnost 3) pripraveno.");
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
  // Diagnostika probíhá asynchronně v OnDataRecv na základě povelu 'R'.
  // V loopu momentálně ESP32_3 nemá žádnou povinnost, čeká na příkazy z ESP-NOW.
  delay(10);
}
