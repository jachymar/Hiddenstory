#include <WiFi.h>
#include <WiFiUdp.h>
#include <HTTPClient.h>

// --- WLED NASTAVENÍ ---
const char* ssid = "Indy-Wifi";
const char* password = "wled1234";
const char* wled_ips[] = {"192.168.1.2", "192.168.1.3", "192.168.1.4"};
const int WLED_UDP_PORT = 21324; // Nativní UDP JSON port WLED

WiFiUDP udp;

IPAddress local_IP(192,168,1,50); // Nastavime ESP na statickou .50 at je hned pripojene
IPAddress gateway(192,168,1,1);
IPAddress subnet(255,255,255,0);
IPAddress primaryDNS(8,8,8,8); // Bez DNS casto pada DHCP reseni

// --- BAREVNÉ KONSTANTY ---
const int baseR = 193, baseG = 99, baseB = 17;     // Herní oranžová
const int servR = 255, servG = 203, servB = 154;   // Pracovní / Technická barva
const int druhyR = 0, druhyG = 157, druhyB = 255;  // Modrá (2. místnost)

const int jasVysoky1 = 172;
const int jasNizky = 10, jasDruhyStandard = 5;
const int jasVysoky3 = 15;

int currentMode = 0; // Režim po zapnutí: 0=Herní, 1=Vypnuto, 3=Pracovní
int currentColorBtn = 0; // Tlačítka: 1=Červená, 2=Zelená, 0=Nic
int currentCrystalsState = 0; // Krystaly: 2=Modrá, 0=Nic
int currentSvetlaSeq = 0; // 0 = klid/vypnuto, 1 = běží kaskáda na Arduinu (zeslabit WLED)

// --- KONTROLA DOSTUPNOSTI WLED PÁSKŮ ---
// Pingujeme vždy jen JEDNO zařízení (round-robin), aby jediný výpadek nikdy neblokoval
// smyčku déle než PING_TIMEOUT_MS. Každý pásek se tak zkontroluje 1x za ~10 s jako dřív.
bool wledStatus[3] = {false, false, false};
unsigned long lastPingTime = 0;
int pingIndex = 0;
const unsigned long PING_INTERVAL_MS = 3333;
const uint16_t PING_TIMEOUT_MS = 150;

// --- STAV WIFI ---
bool wifiBylaPripojena = false;
unsigned long posledniPokusWifi = 0;
const unsigned long WIFI_RETRY_MS = 30000;

void odesliStavWLED() {
  // Odeslat stav do Master jednotky, formát "WLED:1,0,1"
  String statusMsg = "WLED:";
  statusMsg += wledStatus[0] ? "1" : "0";
  statusMsg += ",";
  statusMsg += wledStatus[1] ? "1" : "0";
  statusMsg += ",";
  statusMsg += wledStatus[2] ? "1" : "0";
  Serial2.println(statusMsg);
}

void nastavVseOffline() {
  bool zmena = false;
  for (int i = 0; i < 3; i++) {
    if (wledStatus[i]) { wledStatus[i] = false; zmena = true; }
  }
  if (zmena) odesliStavWLED();
}

// Zkontroluje dostupnost jednoho WLED.
// POZOR: HTTPClient::setTimeout() omezuje jen čekání na odpověď, timeout NAVAZOVÁNÍ TCP spojení
// je zvlášť (výchozí 5 s!). Bez setConnectTimeout() každý odpojený pásek blokoval loop() na 5 s
// a příkazy od Masteru mezitím čekaly v bufferu Serial2 (světla reagovala se zpožděním).
void pingWLED(int i) {
  HTTPClient http;
  http.setConnectTimeout(PING_TIMEOUT_MS);
  http.setTimeout(PING_TIMEOUT_MS);
  http.begin(String("http://") + wled_ips[i] + "/json/state");
  int httpCode = http.GET();
  http.end();

  bool isOnline = (httpCode > 0);
  if (wledStatus[i] != isOnline) {
    wledStatus[i] = isOnline;
    Serial.printf("WLED %d (%s) je nyni %s\n", i + 1, wled_ips[i], isOnline ? "ONLINE" : "OFFLINE");
    odesliStavWLED();
  }
}

// Bleskové odeslání JSON příkazu přes UDP (odezva < 1 ms bez blokování TCP spojením)
void posliPrikaz(const char* ip, const String& json) {
  udp.beginPacket(ip, WLED_UDP_PORT);
  udp.write((const uint8_t*)json.c_str(), json.length());
  udp.endPacket();
}

String vytvorJson(int r, int g, int b, int jas, int tt, int fx = 102) {
  return "{\"on\":true,\"bri\":" + String(jas) + ",\"transition\":" + String(tt) +
         ",\"seg\":[{\"id\":0,\"fx\":" + String(fx) + ",\"sx\":96,\"ix\":224,\"col\":[[" +
         String(r) + "," + String(g) + "," + String(b) + "]]}]}";
}

void aktualizujSystem(int s3, int s8, bool zmenaZ8, bool vsem = false) {
  // Pracovní mód (3) má absolutní přednost a nemění se herními efekty
  if (currentMode == 3) return;
  // Pokud je mód 1 (alarm/vypnuto) a nejedná se o krystaly (konec hry) ani nucené překreslení, ignorovat
  if (currentMode == 1 && s8 != 2 && !vsem) return;

  int r1, g1, b1, jas1, jas2, jas3;
  int tt13 = 8, tt2  = 8;

  if (s8 == 2) {
    // Modro-oranžový mix při plném jasu (Krystaly v Lebce - KONEC HRY)
    // Má absolutní prioritu před jakýmkoliv jiným herním efektem (laser, kaskáda, tlačítka)
    currentMode = 0;

    r1 = (baseR * 40) / 100; g1 = (baseG * 40) / 100; b1 = (baseB * 40 + 255 * 60) / 100;
    jas1 = 255;
    jas2 = jasDruhyStandard;
    jas3 = 255;
    if (vsem) { tt13 = 0; tt2 = 0; } else if (zmenaZ8) { tt13 = 20; tt2 = 2; }

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

      jas1 = (currentSvetlaSeq == 1) ? jasNizky : 255;
      jas2 = (s8 == 1 || currentSvetlaSeq == 1) ? 0 : jasDruhyStandard;
      jas3 = (currentSvetlaSeq == 1) ? jasNizky : 255;

      tt13 = 6; // Fáze 1: Náběh
      tt2 = 6;

      posliPrikaz(wled_ips[0], vytvorJson(r1, g1, b1, jas1, tt13));
      posliPrikaz(wled_ips[1], vytvorJson(druhyR, druhyG, druhyB, jas2, tt2));
      posliPrikaz(wled_ips[2], vytvorJson(r1, g1, b1, jas3, tt13));

      // Důležité: Nenechávat ESP úplně zastavené pro background procesy sítě
      for (int k = 0; k < 25; k++) {
        delay(10);
      }

      tt13 = 15; // Fáze 2: Pokles
      int klesajiciJas1 = (s8 == 1 || currentSvetlaSeq == 1) ? jasNizky : jasVysoky1;
      int klesajiciJas2 = (s8 == 1 || currentSvetlaSeq == 1) ? 0 : jasDruhyStandard;
      int klesajiciJas3 = (s8 == 1 || currentSvetlaSeq == 1) ? jasNizky : jasVysoky3;

      posliPrikaz(wled_ips[0], vytvorJson(r1, g1, b1, klesajiciJas1, tt13));
      posliPrikaz(wled_ips[1], vytvorJson(druhyR, druhyG, druhyB, klesajiciJas2, tt2));
      posliPrikaz(wled_ips[2], vytvorJson(r1, g1, b1, klesajiciJas3, tt13));

    } else {
      // Návrat do běžné oranžové barvy
      r1 = baseR; g1 = baseG; b1 = baseB;

      jas1 = (s8 == 1 || currentSvetlaSeq == 1) ? jasNizky : jasVysoky1;
      jas2 = (s8 == 1 || currentSvetlaSeq == 1) ? 0 : jasDruhyStandard;
      jas3 = (s8 == 1 || currentSvetlaSeq == 1) ? jasNizky : jasVysoky3;

      if (currentColorBtn == 1 || currentColorBtn == 2) {
        tt13 = 15;
        tt2 = 15;
      } else if (vsem) {
        tt13 = 0; tt2 = 0;
      } else if (zmenaZ8) {
        tt13 = 20; tt2 = 2;
      } else {
        tt13 = 15; tt2 = 15;
      }

      posliPrikaz(wled_ips[0], vytvorJson(r1, g1, b1, jas1, tt13));
      posliPrikaz(wled_ips[1], vytvorJson(druhyR, druhyG, druhyB, jas2, tt2));
      posliPrikaz(wled_ips[2], vytvorJson(r1, g1, b1, jas3, tt13));
    }
  }
}

void applyModeChange(int stav) {
  if (stav == 3) {
    // Pracovní mód
    String servJson = vytvorJson(servR, servG, servB, 255, 0, 0);
    posliPrikaz(wled_ips[0], servJson);
    posliPrikaz(wled_ips[1], servJson);
    posliPrikaz(wled_ips[2], servJson);
  } else if (stav == 1) {
    // Vypnuto / Lasery -> Bleskový snap tmy (pouze pokud neběží konec hry s krystaly)
    if (currentCrystalsState == 2) {
      aktualizujSystem(currentColorBtn, currentCrystalsState, false, true);
      return;
    }
    posliPrikaz(wled_ips[0], "{\"on\":false,\"transition\":0}");
    posliPrikaz(wled_ips[1], "{\"on\":false,\"transition\":0}");
    posliPrikaz(wled_ips[2], "{\"on\":false,\"transition\":0}");
  } else {
    // Návrat do herního módu
    aktualizujSystem(currentColorBtn, currentCrystalsState, false, true);
  }
}

// Hlídá (znovu)připojení k WiFi. Při každém navázání spojení se znovu otevře UDP socket
// a na pásky se pošle aktuální stav, aby po výpadku WiFi/AP nezůstaly rozjeté.
void obsluzWifi() {
  bool pripojeno = (WiFi.status() == WL_CONNECTED);

  if (pripojeno && !wifiBylaPripojena) {
    wifiBylaPripojena = true;
    WiFi.setSleep(false); // Vypne úsporný režim Wi-Fi rádia pro okamžitou odezvu bez latence
    udp.begin(WLED_UDP_PORT);
    Serial.print("\nPripojeno k WiFi! IP Adresa: ");
    Serial.println(WiFi.localIP());
    applyModeChange(currentMode); // Obnoví aktuální stav na páscích
    lastPingTime = 0;             // Hned zkontrolovat dostupnost pásků
    pingIndex = 0;
  } else if (!pripojeno && wifiBylaPripojena) {
    wifiBylaPripojena = false;
    udp.stop();
    Serial.println("WiFi odpojeno - cekam na znovupripojeni...");
    nastavVseOffline();
  }

  // Pojistka, kdyby automatický reconnect nezabral (např. AP nebyla při startu vůbec dostupná)
  if (!pripojeno && (millis() - posledniPokusWifi >= WIFI_RETRY_MS)) {
    posledniPokusWifi = millis();
    Serial.println("WiFi: novy pokus o pripojeni...");
    WiFi.disconnect();
    WiFi.begin(ssid, password);
  }
}

void setup() {
  Serial.begin(115200); // Debug do PC

  // Serial2 bude sloužit pro komunikaci s ESP32 č.2 (Mozkem)
  // Pin 16 = RX2 (přijímá z Mozku), Pin 17 = TX2 (odesílá do Mozku)
  Serial2.begin(115200, SERIAL_8N1, 16, 17);

  // Wi-Fi Stanice (Klient) - pripojeni na existujici AP
  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  if (!WiFi.config(local_IP, gateway, subnet, primaryDNS)) {
    Serial.println("Chyba nastaveni staticke IP");
  }
  WiFi.begin(ssid, password);
  posledniPokusWifi = millis();

  Serial.print("Pripojovani k WiFi ");
  Serial.print(ssid);

  // Krátké čekání při startu; pokud se nepřipojí, dořeší to obsluzWifi() za běhu
  int pokusy = 0;
  while (WiFi.status() != WL_CONNECTED && pokusy < 20) {
    delay(500);
    Serial.print(".");
    pokusy++;
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\nChyba: Nepodarilo se pripojit k WiFi (zkousim dal za behu).");
  }
  obsluzWifi(); // Při úspěchu otevře UDP a aplikuje výchozí režim

  Serial.println("ESP32 WLED Kontroler uspesne nastartoval.");
}

void loop() {
  obsluzWifi();

  // PŘÍJEM POKYNŮ OD HLAVNÍHO MOZKU PŘES SERIAL2
  // Mozek posílá jednoduché textové příkazy (např. "M1" pro režim 1, "C2" pro barvu 2)
  if (Serial2.available()) {
    String msg = Serial2.readStringUntil('\n');
    msg.trim(); // Odstraní neviditelné znaky (entery)

    Serial.print("WLED RX: '"); Serial.print(msg); Serial.println("'");

    if (msg.length() >= 2) {
      char prefix = msg.charAt(0);
      int hodnota = msg.substring(1).toInt();

      if (prefix == 'M') {
        // M = Změna hlavního Módu (0, 1, 3)
        currentMode = hodnota;
        applyModeChange(currentMode);
        Serial.print("Svetla: Aplikovan mod "); Serial.println(hodnota);
      }
      else if (prefix == 'C') {
        // C = Změna tlačítka barev (0, 1, 2)
        currentColorBtn = hodnota;
        aktualizujSystem(currentColorBtn, currentCrystalsState, false);
        Serial.print("Svetla: Barva tlacitka zmenena na "); Serial.println(hodnota);
      }
      else if (prefix == 'K') {
        // K = Změna krystalů (0, 1, 2)
        currentCrystalsState = hodnota;
        if (currentCrystalsState == 2 && currentMode != 3) {
          currentMode = 0;
        }
        aktualizujSystem(currentColorBtn, currentCrystalsState, true); // true = zmenaZ8 pro lepsi prechod
        Serial.print("Svetla: Stav krystalu zmenen na "); Serial.println(hodnota);
      }
      else if (prefix == 'S') {
        // S = Sekvence kaskády světel (0 = klid/konec, 1 = běží animace / zeslabit WLED 1)
        currentSvetlaSeq = hodnota;
        aktualizujSystem(currentColorBtn, currentCrystalsState, false);
        Serial.print("Svetla: Stav sekvence svetel zmenen na "); Serial.println(hodnota);
      }
      else if (prefix == 'X') {
        // X = Developer Mod
        Serial.print("Svetla: Prepnut Dev Mod na "); Serial.println(hodnota);
      }
    }
  }

  // Pravidelná kontrola dostupnosti WLED (jeden pásek za PING_INTERVAL_MS), ne během alarmu (mód 1)
  if (wifiBylaPripojena && currentMode != 1 && (millis() - lastPingTime >= PING_INTERVAL_MS)) {
    lastPingTime = millis();
    pingWLED(pingIndex);
    pingIndex = (pingIndex + 1) % 3;
    if (pingIndex == 0) {
      odesliStavWLED(); // Po každém celém kole (~10 s) pošleme stav i beze změny (heartbeat pro Master/LCD)
    }
  }
}
