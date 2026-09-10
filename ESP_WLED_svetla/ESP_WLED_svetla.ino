#include <WiFi.h>
#include <HTTPClient.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>

// --- BLE NASTAVENÍ ---
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define STATE_CHARACTERISTIC_UUID "c4d2a8aa-7c45-4e4f-a999-f7dffb74c1a2"

BLEServer* pServer = NULL;
BLECharacteristic* pStateCharacteristic = NULL;
bool deviceConnected = false;
bool oldDeviceConnected = false;
bool cekaNaReklamu = false;
unsigned long casOdpojeni = 0;

// --- WLED NASTAVENÍ ---
const char* ssid = "LED-AP";
const char* password = "wled1234";
const char* wled_ips[] = {"5.3.2.2", "5.3.2.3", "5.3.2.4"};

IPAddress local_IP(5,3,2,1);
IPAddress gateway(5,3,2,1);
IPAddress subnet(255,255,255,0);

// --- BAREVNÉ KONSTANTY ---
const int baseR = 193, baseG = 99, baseB = 17;     // Herní oranžová
const int servR = 255, servG = 203, servB = 154;   // Pracovní / Technická barva
const int druhyR = 0, druhyG = 157, druhyB = 255;  // Modrá (2. místnost)

const int jasVysoky1 = 172, jasVysoky3 = 15;       
const int jasNizky = 10, jasDruhyStandard = 5;    

int posledniS6 = 0; // Režim: 0=Herní, 1=Vypnuto, 3=Pracovní
int posledniS3 = 0; // Tlačítka: 1=Červená, 2=Zelená, 0=Nic
int posledniS8 = 0; // Krystaly: 2=Modrá, 0=Nic

String globalDiagnosticJson = "{}";

void posliPrikaz(const char* ip, String json) {
  HTTPClient http;
  http.setTimeout(150); 
  http.begin("http://" + String(ip) + "/json/state");
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Connection", "close");
  http.POST(json);
  http.end();
}

String vytvorJson(int r, int g, int b, int jas, int tt, int fx = 102) {
  return "{\"on\":true,\"bri\":" + String(jas) + ",\"transition\":" + String(tt) + 
         ",\"seg\":[{\"id\":0,\"fx\":" + String(fx) + ",\"sx\":96,\"ix\":224,\"col\":[[" + 
         String(r) + "," + String(g) + "," + String(b) + "]]}]}";
}
void posliStateNaBle(String json) {
  if (pStateCharacteristic == NULL) return;
  pStateCharacteristic->setValue(json.c_str());
  pStateCharacteristic->notify();
}
void aktualizujSystem(int s3, int s8, bool zmenaZ8, bool vsem = false) {
  // Ochrana před změnou barvy, pokud není herní mód (0) nebo to není globální překreslení
  if (s3 == 3) return; 
  if (!vsem && (posledniS6 == 3 || posledniS6 == 1)) return;

  int r1, g1, b1, jas1, jas2, jas3;
  int tt13 = 8, tt2  = 8; 

  if (s8 == 2) { 
    // Modro-oranžový mix při plném jasu (Krystaly v Lebce)
    r1 = (baseR * 40) / 100; g1 = (baseG * 40) / 100; b1 = (baseB * 40 + 255 * 60) / 100;
    jas1 = 255; jas2 = jasDruhyStandard; jas3 = 255;
    if (vsem || zmenaZ8) { tt13 = 20; tt2 = 2; }
    
    posliPrikaz(wled_ips[0], vytvorJson(r1, g1, b1, jas1, tt13));
    posliPrikaz(wled_ips[1], vytvorJson(druhyR, druhyG, druhyB, jas2, tt2));
    posliPrikaz(wled_ips[2], vytvorJson(r1, g1, b1, jas3, tt13));
  } else { 
    // TLAČÍTKA - DVOJFÁZOVÁ FLASHBANG ANIMACE
    if (s3 == 1 || s3 == 2) {
      int tr = (s3 == 1) ? 255 : 0;
      int tg = (s3 == 2) ? 255 : 0;
      int tb = 0;
      
      r1 = ((2 * baseR) + tr) / 3;
      g1 = ((2 * baseG) + tg) / 3;
      b1 = ((2 * baseB) + tb) / 3;
      
      jas1 = 255; 
      jas2 = (s8 == 1) ? 3 : jasDruhyStandard;
      jas3 = 255; 
      
      tt13 = 6; // Fáze 1: Náběh
      tt2 = 6;
      
      posliPrikaz(wled_ips[0], vytvorJson(r1, g1, b1, jas1, tt13));
      posliPrikaz(wled_ips[1], vytvorJson(druhyR, druhyG, druhyB, jas2, tt2));
      posliPrikaz(wled_ips[2], vytvorJson(r1, g1, b1, jas3, tt13));

      delay(250);

      tt13 = 15; // Fáze 2: Pokles
      int klesajiciJas1 = (s8 == 1) ? jasNizky : jasVysoky1;
      int klesajiciJas3 = (s8 == 1) ? jasNizky : jasVysoky3;

      posliPrikaz(wled_ips[0], vytvorJson(r1, g1, b1, klesajiciJas1, tt13));
      posliPrikaz(wled_ips[2], vytvorJson(r1, g1, b1, klesajiciJas3, tt13));

    } else {
      // Návrat do běžné oranžové barvy
      r1 = baseR; g1 = baseG; b1 = baseB;
      
      jas1 = (s8 == 1) ? jasNizky : jasVysoky1;
      jas2 = (s8 == 1) ? 3 : jasDruhyStandard;
      jas3 = (s8 == 1) ? jasNizky : jasVysoky3;

      if (posledniS3 == 1 || posledniS3 == 2) {
        tt13 = 15; 
        tt2 = 15;
      } else if (vsem || zmenaZ8) {
        tt13 = 20; tt2 = 2;
      }

      posliPrikaz(wled_ips[0], vytvorJson(r1, g1, b1, jas1, tt13));
      posliPrikaz(wled_ips[1], vytvorJson(druhyR, druhyG, druhyB, jas2, tt2));
      posliPrikaz(wled_ips[2], vytvorJson(r1, g1, b1, jas3, tt13));
    }
  }
}

void zpracujZmenuS6(int stav) {
  if (stav == 3) { 
    // Pracovní mód
    String servJsonFast = vytvorJson(servR, servG, servB, 255, 5, 0);
    String servJsonSlow = vytvorJson(servR, servG, servB, 255, 20, 0);
    posliPrikaz(wled_ips[0], servJsonSlow);
    posliPrikaz(wled_ips[1], servJsonFast);
    posliPrikaz(wled_ips[2], servJsonSlow);
  } else if (stav == 1) { 
    // Vypnuto / Lasery -> Bleskový snap tmy
    for (int i = 0; i < 3; i++) posliPrikaz(wled_ips[i], "{\"on\":false,\"transition\":0}");
  } else {
    // Návrat do herního módu
    aktualizujSystem(posledniS3, posledniS8, false, true);
  }
}

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
      Serial.println("BLE PŘIPOJENO.");
    }
    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
      Serial.println("BLE ODPOJENO.");
    }
};

class MyCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      String rxValue = pCharacteristic->getValue();
      if (rxValue.length() > 0) {
        char cmd = rxValue[0];
        
        // Cokoliv přijde z mobilu, okamžitě pošleme UARTem do "Hlavního Mozku" (ESP32 č.2)
        // Mozek se pak sám rozhodne, co s tím udělá.
        Serial2.println(cmd);
        
        Serial.print("BLE zprava odeslana do Mozku: ");
        Serial.println(cmd);
      }
    }
};

void setup() {
  Serial.begin(115200); // Debug do PC
  
  // Serial2 bude sloužit pro komunikaci s ESP32 č.2 (Mozkem)
  // Pin 16 = RX2 (přijímá z Mozku), Pin 17 = TX2 (odesílá do Mozku)
  Serial2.begin(115200, SERIAL_8N1, 16, 17);
  
  // Wi-Fi Access Point
  WiFi.softAPConfig(local_IP, gateway, subnet);
  WiFi.softAP(ssid, password);
  
  // BLE Inicializace
  BLEDevice::init("ESP32_WLED_Ovladac");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID);
  BLECharacteristic *pCharacteristic = pService->createCharacteristic(
                                         CHARACTERISTIC_UUID,
                                         BLECharacteristic::PROPERTY_WRITE
                                       );
  pCharacteristic->setCallbacks(new MyCallbacks());

  pStateCharacteristic = pService->createCharacteristic(
                            STATE_CHARACTERISTIC_UUID,
                            BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
                          );
  pStateCharacteristic->setValue("{}");

  pService->start();
  
  BLEAdvertising *pAdvertising = pServer->getAdvertising(); 
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06); 
  pAdvertising->setMinPreferred(0x12);
  pAdvertising->start();

  Serial.println("ESP32 Komunikacni Brana uspesne nastartovala.");
}

void loop() {
  unsigned long ted = millis();

  // BLE Opětovné připojení
  if (!deviceConnected && oldDeviceConnected) {
      casOdpojeni = ted;
      cekaNaReklamu = true;
      oldDeviceConnected = deviceConnected;
  }
  if (cekaNaReklamu && (ted - casOdpojeni > 500)) {
      cekaNaReklamu = false;
      pServer->getAdvertising()->start(); 
  }
  if (deviceConnected && !oldDeviceConnected) {
      oldDeviceConnected = deviceConnected;
  }

  // PŘÍJEM POKYNŮ OD HLAVNÍHO MOZKU PŘES SERIAL2
  // Mozek posílá jednoduché textové příkazy (např. "M1" pro režim 1, "C2" pro barvu 2)
  if (Serial2.available()) {
    String msg = Serial2.readStringUntil('\n');
    msg.trim(); // Odstraní neviditelné znaky (entery)

    if (msg.startsWith("DIAG|")) {
      globalDiagnosticJson = msg.substring(5);
      return;
    }

    if (msg.startsWith("STATE|")) {
      String json = msg.substring(6);
      posliStateNaBle(json);
      return;
    }

    if (msg.length() >= 2) {
      char prefix = msg.charAt(0);
      int hodnota = msg.substring(1).toInt();

      if (prefix == 'M') {
        // M = Změna hlavního Módu (0, 1, 3)
        if (posledniS6 != hodnota) {
          posledniS6 = hodnota;
          zpracujZmenuS6(posledniS6);
          Serial.print("Svetla: Zmenen mod na "); Serial.println(hodnota);
        }
      } 
      else if (prefix == 'C') {
        // C = Změna tlačítka barev (0, 1, 2)
        if (posledniS3 != hodnota) {
          posledniS3 = hodnota;
          aktualizujSystem(posledniS3, posledniS8, false);
          Serial.print("Svetla: Barva tlacitka zmenena na "); Serial.println(hodnota);
        }
      } 
      else if (prefix == 'K') {
        // K = Změna krystalů (0, 1, 2)
        if (posledniS8 != hodnota) {
          posledniS8 = hodnota;
          aktualizujSystem(posledniS3, posledniS8, true); // true = zmenaZ8 pro lepsi prechod
          Serial.print("Svetla: Stav krystalu zmenen na "); Serial.println(hodnota);
        }
      }
    }
  }
}