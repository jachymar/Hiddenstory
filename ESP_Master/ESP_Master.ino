    #include <Wire.h>
    #include <HardwareSerial.h>
    #include <WiFi.h>
    #include <esp_now.h>
    #include <esp_wifi.h>
    #include <SPI.h>
    #include <EthernetENC.h>
    #include <LiquidCrystal_I2C.h> // Knihovna pro LCD

    // --- LCD DISPLEJ (I2C Adresa obvykle 0x27) ---
    LiquidCrystal_I2C lcd(0x27, 16, 2);

    // Vlastní znaky pro invertovaná čísla (offline WLED)
    byte customInverted1[8] = { B11111, B11011, B10011, B11011, B11011, B11011, B10001, B11111 };
    byte customInverted2[8] = { B11111, B10001, B11101, B11101, B10001, B10111, B10001, B11111 };
    byte customInverted3[8] = { B11111, B10001, B11101, B10001, B11101, B11101, B10001, B11111 };

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
    uint8_t sys_mode;
    uint8_t sys_color;
    uint8_t sys_crystals;
  } struct_msg_to_m3;

  typedef struct struct_msg_from_m3 {
    uint8_t tukani_status;
    uint8_t tukani_lock;
    uint8_t tukani_taps;
    uint8_t audio3_status;
    uint8_t audio3_is_playing;
    uint8_t audio3_is_alarm;
    uint8_t wled3_online;
    char tukani_log[30];
    char audio3_log[30];
  } struct_msg_from_m3;

  struct_msg_from_m3 m3_data = {255,0,0,255,0,0,0,"",""};
  uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  uint8_t addr_esp32_3[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  bool addr_esp32_3_known = false;

  unsigned long lastM3Heartbeat = 0; // Kdy naposledy přišla data z M3
  bool m3_connected = false;

  // --- STAVOVÉ PROMĚNNÉ HERNÍ LOGIKY (dopředná deklarace) ---
  int lastGameMode = 0; // Výchozí mód po startu je 0 (Herní)
  int lastColorButton = -1;
  int lastCrystalsState = -1;

  void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
    if (len == sizeof(struct_msg_from_m3)) {
      if (!addr_esp32_3_known) {
        memcpy(addr_esp32_3, info->src_addr, 6);
        addr_esp32_3_known = true;

        esp_now_peer_info_t peerInfo = {};
        memcpy(peerInfo.peer_addr, addr_esp32_3, 6);
        peerInfo.channel = 0;
        peerInfo.encrypt = false;
        esp_now_add_peer(&peerInfo);

        Serial.printf("ESP32 M3 detekovano! MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                      addr_esp32_3[0], addr_esp32_3[1], addr_esp32_3[2],
                      addr_esp32_3[3], addr_esp32_3[4], addr_esp32_3[5]);
      }

      memcpy(&m3_data, incomingData, sizeof(m3_data));
      lastM3Heartbeat = millis(); // Záznam, že přijímáme data
    }
  }

  void posliPrikazM3(uint8_t target_i2c, char cmd) {
    struct_msg_to_m3 msg;
    msg.target_i2c = target_i2c;
    msg.command = cmd;
    msg.sys_mode = (uint8_t)(lastGameMode >= 0 ? lastGameMode : 0);
    msg.sys_color = (uint8_t)(lastColorButton >= 0 ? lastColorButton : 0);
    msg.sys_crystals = (uint8_t)(lastCrystalsState >= 0 ? lastCrystalsState : 0);
    
    const uint8_t *targetAddr = addr_esp32_3_known ? addr_esp32_3 : broadcastAddress;
    esp_err_t res = esp_now_send(targetAddr, (uint8_t *) &msg, sizeof(msg));
    if (cmd != 'U') {
      Serial.print("ESP-NOW Odeslano '"); Serial.print(cmd); 
      Serial.print("' na I2C "); Serial.print(target_i2c);
      Serial.println(res == ESP_OK ? " [OK]" : " [CHYBA]");
    }
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
    } __attribute__((packed));

    DiagLaser dataLaser = {0,0,0,0};
    DiagTlacitka dataTlacitka = {0,0,0,0};
    DiagKola dataKola = {0,0,0,0,0};
    DiagSvetla dataSvetla = {0,0,0,0,0};
    DiagLebka dataLebka = {0,0,0,0,0,0};

    const int PIN_MODE_BTN = 14;     // Pin pro tlačítko změny módu
    unsigned long lastModeBtnTime = 0;
    bool lastModeBtnState = HIGH;

    int physGameMode = 0; // Výchozí mód po startu je 0 (Herní)
    int physColorButton = -1;
    int physCrystalsState = -1;
    int physSvetlaState = -1;
    int lastSvetlaState = -1;

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

    bool wled1_status = false;
    bool wled2_status = false;
    bool wled3_status = false;

    bool laser_online = false;
    bool tlacitka_online = false;
    bool lebka_online = false;
    bool kola_online = false;
    bool svetla_online = false;
    bool audio_online = false;

    unsigned long lastLcdUpdate = 0;
    bool blinkState = false;
    int scrollIndex = 0;

    uint8_t physAudio = 255;
    unsigned long casOtevreniDveri1 = 0;
    bool laserAktivovanDvermi = false;
    uint8_t minulyStavDveri1 = 255;

    void updateLCD() {
      unsigned long ted = millis();
      if (ted - lastLcdUpdate < 400) return; // Obnovování každých 400ms (kvůli blikání a scrollování)
      lastLcdUpdate = ted;
      blinkState = !blinkState;

      // Statické časovače pro filtraci falešných výpadků TUKANI a AUDIO3 (Místnost 3)
      static unsigned long lastTukaniOk = ted;
      static unsigned long lastAudio3Ok = ted;

      if (m3_data.tukani_status != 255) lastTukaniOk = ted;
      if (m3_data.audio3_status != 255) lastAudio3Ok = ted;

      lcd.setCursor(0, 0);
      // Pouze dva módy. 1 (Alarm) spadá do HERNÍ.
      if (lastGameMode == 3) {
        lcd.print("PRACOVNI    ");
      } else {
        lcd.print("HERNI       "); // Pro 0, 1 a vše ostatní to považujeme za Herní
      }

      lcd.setCursor(13, 0); // Vpravo nahoře pro 3 znaky WLED
      
      // Logika prohozená:
      // ZAPNUTÝ = normální číslo "1", Vypnutý = blikající invertovaný blok (custom znak)
      if (wled1_status) {
        lcd.print("1");
      } else {
        if (blinkState) lcd.write(byte(1)); else lcd.print(" ");
      }
      
      if (wled2_status) {
        lcd.print("2");
      } else {
        if (blinkState) lcd.write(byte(2)); else lcd.print(" ");
      }
      
      if (wled3_status) {
        lcd.print("3");
      } else {
        if (blinkState) lcd.write(byte(3)); else lcd.print(" ");
      }

      // Sestavení seznamu chyb (chyba se zařadí až po nepřerušeném trvání výpadku)
      String errors[16]; // Zvětšeno z 12 na 16 pro absolutní bezpečnost proti zamrznutí (Stack Overflow)
      int errCount = 0;
      
      if (!eth_connected) errors[errCount++] = "LAN ODPOJENA";

      if (!laser_online) errors[errCount++] = "LASER";
      if (!tlacitka_online) errors[errCount++] = "TLACITKA";
      if (!lebka_online) errors[errCount++] = "LEBKA";
      if (!kola_online) errors[errCount++] = "KOLA";
      if (!svetla_online) errors[errCount++] = "SVETLA";
      if (!audio_online) errors[errCount++] = "AUDIO 1/2";
      
      // M3 závislosti
      bool is_m3 = (millis() - lastM3Heartbeat <= 30000);
      if (!is_m3) {
        errors[errCount++] = "ESP MISTNOST 3";
      } else {
        if (m3_data.tukani_status == 255) errors[errCount++] = "TUKANI";
        if (m3_data.audio3_status == 255) errors[errCount++] = "AUDIO 3";
      }
      
      if (!wled1_status) errors[errCount++] = "WLED 1";
      if (!wled2_status) errors[errCount++] = "WLED 2";
      if (!wled3_status) errors[errCount++] = "WLED 3";
      
      lcd.setCursor(0, 1);
      if (errCount == 0) {
        lcd.print("Vse OK          ");
      } else {
        // Přepínání chybových hlášek každé 2 sekundy (2000 ms)
        int dispIndex = (ted / 2000) % errCount;
        String toShow = errors[dispIndex];
        while(toShow.length() < 16) toShow += " "; // Doplnění mezer do konce řádku
        lcd.print(toShow.substring(0, 16));
      }
    }

    // Funkce pro odeslání 1 znaku (povelu) podřízenému Arduinu přes I2C
    void posliPrikazI2C(int adresa, char prikaz) {
      if (adresa == ADDR_LEBKA) {
        Wire.beginTransmission(ADDR_LASER);
        Wire.write(prikaz);
        byte error = Wire.endTransmission();

        if (error == 0) {
          Serial.print("UART bridge -> Laser fwd '"); Serial.print(prikaz);
          Serial.print("' pro lebku"); Serial.println(ADDR_LEBKA);
        } else {
          Serial.print("Chyba bridge komunikace na laser pro lebku");
        }
        return;
      }

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
      if (adresa == ADDR_LEBKA) {
        Wire.beginTransmission(ADDR_LASER);
        Wire.write('S');
        byte error = Wire.endTransmission();
        if (error != 0) return 255;
        delay(2);

        Wire.requestFrom((uint8_t)ADDR_LASER, (uint8_t)1);
        if (Wire.available()) {
          uint8_t v = Wire.read();
          if (v == 'X') return 255; // Offline příznak z Laseru
          if (v >= '0' && v <= '9') return (uint8_t)(v - '0');
          return v;
        }
        return 255;
      }

      Wire.beginTransmission(adresa);
      if (Wire.endTransmission() != 0) return 255;

      Wire.requestFrom((uint8_t)adresa, (uint8_t)1);
      if (Wire.available()) {
        return Wire.read();
      }
      return 255;
    }

    // Funkce pro bezpečné vyčtení struktury z Arduina v Developer Módu
    template <typename T>
    bool readI2CDiagnostics(int adresa, T &data) {
      if (adresa == ADDR_LEBKA) {
        Wire.beginTransmission(ADDR_LASER);
        Wire.write('D');
        byte error = Wire.endTransmission();
        if (error != 0) return false;
        delay(2);

        Wire.requestFrom((uint8_t)ADDR_LASER, (uint8_t)30);
        String payload = "";
        while (Wire.available()) {
          char c = Wire.read();
          if (c != 0 && c != 255) payload += c;
        }
        if (payload.length() == 0) return false;

        int values[6] = {0,0,0,0,0,0};
        int count = 0;
        String token = "";
        for (size_t i = 0; i < payload.length(); i++) {
          char ch = payload[i];
          if (ch == ',') {
            if (count < 6) values[count++] = token.toInt();
            token = "";
          } else {
            token += ch;
          }
        }
        if (token.length() > 0 && count < 6) values[count++] = token.toInt();
        if (count < 6) return false;

        DiagLebka *lebka = (DiagLebka*)&data;
        lebka->status = (uint8_t)values[0];
        lebka->crystals_mask = (uint8_t)values[1];
        lebka->k1_val = (uint16_t)values[2];
        lebka->k2_val = (uint16_t)values[3];
        lebka->k3_val = (uint16_t)values[4];
        lebka->lock_open = (uint8_t)values[5];
        return true;
      }

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
      if (adresa == ADDR_LEBKA) {
        Wire.beginTransmission(ADDR_LASER);
        Wire.write('L');
        byte error = Wire.endTransmission();
        if (error != 0) return "";
        delay(2);

        Wire.requestFrom((uint8_t)ADDR_LASER, (uint8_t)30);
        String l = "";
        while (Wire.available()) {
          char c = Wire.read();
          if (c != 0 && c != 255) l += c;
        }
        return l;
      }

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

    // Funkce I2C Skeneru (Prohledá adresy 1-127)
    String scanI2C() {
      byte error, address;
      int nDevices = 0;
      String result = "--- I2C SKENER ---\nNalezena zarizeni na adrese:\n";
      for(address = 1; address < 127; address++ ) {
        Wire.beginTransmission(address);
        error = Wire.endTransmission();
        if (error == 0) {
          result += "- 0x";
          if (address < 16) result += "0";
          result += String(address, HEX) + " (" + String(address) + ")\n";
          nDevices++;
        } else if (error == 4) {
          result += "- Neznama chyba na 0x";
          if (address < 16) result += "0";
          result += String(address, HEX) + "\n";
        }
      }
      if (nDevices == 0) result += "Zadna I2C zarizeni NENALEZENA! Zkontroluj kabely (SDA, SCL) a napajeni.\n";
      else result += "Celkem nalezeno: " + String(nDevices) + "\n";
      return result;
    }

    // Rychlá kontrola, zda je zařízení na I2C adrese dostupné
    bool checkI2COnline(int adresa) {
      Wire.beginTransmission(adresa);
      byte error = Wire.endTransmission();
      return (error == 0);
    }

    // Zpracování hlavní systémové změny a informování Audia a Komunikační brány
    void processGameModeChange(int mode) {
      // 1. Změna hudby podle nového stavu
      char audioPrikaz = '0' + mode; 
      if (mode == 0 && lastCrystalsState == 2) {
        audioPrikaz = 'K'; // Výjimka: Návrat do hry, ale krystaly už tam jsou
      }
      posliPrikazI2C(ADDR_AUDIO, audioPrikaz);

      // 1b. Informování Laseru o změně módu (aby věděl, jestli je pracovní mód - 3)
      char laserPrikaz = (mode == 3) ? '3' : '0';
      posliPrikazI2C(ADDR_LASER, laserPrikaz);

      // 1c. Informování Kol o změně módu
      posliPrikazI2C(ADDR_KOLA, (mode == 3) ? '3' : '0');

      if (mode == 3) {
        posliPrikazI2C(ADDR_SVETLA, 'E');   // Vysypání kuličky při přechodu do pracovního módu
        posliPrikazI2C(ADDR_SVETLA, '3');   // Reset stavu světel pro pracovní mód
        posliPrikazI2C(ADDR_TLACITKA, '3'); // Reset tlačítek pro novou hru / pracovní mód
      } else if (mode == 0) {
        posliPrikazI2C(ADDR_SVETLA, 'R');   // Reset pro novou hru
        posliPrikazI2C(ADDR_TLACITKA, '0');
        casOtevreniDveri1 = 0;
        laserAktivovanDvermi = false;
        minulyStavDveri1 = 255; // Vynutí novou kontrolu stavu dveří v loop()
      }

      // 2. Odeslání pokynu do Komunikační brány (ESP32 č.1)
      // Prefix 'M' znamená, že se mění hlavní Mód
      Serial2.println("M" + String(mode));
      
      // 3. Aktualizace LCD Displeje (kompaktní zobrazení)
      updateLCD();  posliPrikazM3(0, 'U'); // Informujeme Místnost 3 o změně módu pro její WLED
      Serial.print("Logika: Režim změněn na "); Serial.println(mode);
    }

    String sanitizeJsonString(const String& input) {
      String out = "";
      out.reserve(input.length() + 16);
      for (size_t i = 0; i < input.length(); i++) {
        char c = input[i];
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') { /* přeskočit */ }
        else if (c == '\t') out += "\\t";
        else if ((uint8_t)c >= 32) out += c;
      }
      return out;
    }

    String buildTelemetryState() {
      String json = "";
      json.reserve(640);
      json += "{";
      json += "\"mode\":" + String(lastGameMode) + ",";
      json += "\"lebka\":{\"st\":" + String(dataLebka.status) + ",\"c_mask\":" + String(dataLebka.crystals_mask) + ",\"k1\":" + String(dataLebka.k1_val) + ",\"k2\":" + String(dataLebka.k2_val) + ",\"k3\":" + String(dataLebka.k3_val) + ",\"lock\":" + String(dataLebka.lock_open) + "},";
      json += "\"svetla\":{\"st\":" + String(dataSvetla.status) + ",\"run\":" + String(dataSvetla.mode_running) + ",\"led\":" + String(dataSvetla.current_led) + ",\"m_idl\":" + String(dataSvetla.magnet_idle) + ",\"m_val\":" + String(dataSvetla.magnet_val) + "},";
      json += "\"tlacitka\":{\"st\":" + String(dataTlacitka.status) + ",\"lock\":" + String(dataTlacitka.lock_open) + ",\"prs\":" + String(dataTlacitka.presses) + ",\"idl\":" + String(dataTlacitka.idle_time) + "},";
      json += "\"kola\":{\"st\":" + String(dataKola.status) + ",\"mask\":" + String(dataKola.active_mask) + ",\"a1\":" + String(dataKola.a1_val) + ",\"a2\":" + String(dataKola.a2_val) + ",\"a3\":" + String(dataKola.a3_val) + "},";
      json += "\"laser\":{\"st\":" + String(dataLaser.status) + ",\"on\":" + String(dataLaser.laser_on) + ",\"ldr\":" + String(dataLaser.ldr_val) + ",\"fail\":" + String(dataLaser.fails) + "},";
      json += "\"m3_tukani\":{\"st\":" + String(m3_data.tukani_status) + ",\"lock\":" + String(m3_data.tukani_lock) + ",\"taps\":" + String(m3_data.tukani_taps) + "},";
      json += "\"m3_audio\":{\"st\":" + String(m3_data.audio3_status) + ",\"play\":" + String(m3_data.audio3_is_playing) + ",\"alarm\":" + String(m3_data.audio3_is_alarm) + "},";
      
      // Tvrdé online stavy pro diagnostiku Dashboardu
      bool lcd_on = true;
      
      json += "\"online\":{";
      json += "\"laser\":" + String(laser_online ? "1" : "0") + ",";
      json += "\"tlacitka\":" + String(tlacitka_online ? "1" : "0") + ",";
      json += "\"lebka\":" + String(lebka_online ? "1" : "0") + ",";
      json += "\"kola\":" + String(kola_online ? "1" : "0") + ",";
      json += "\"svetla\":" + String(svetla_online ? "1" : "0") + ",";
      json += "\"audio\":" + String(audio_online ? "1" : "0") + ",";
      json += "\"lcd\":" + String(lcd_on ? "1" : "0") + ",";
      
      bool is_m3 = (millis() - lastM3Heartbeat <= 30000);
      json += "\"m3\":" + String(is_m3 ? "1" : "0") + ",";
      json += "\"m3_tukani\":" + String((is_m3 && m3_data.tukani_status != 255) ? "1" : "0") + ",";
      json += "\"m3_audio3\":" + String((is_m3 && m3_data.audio3_status != 255) ? "1" : "0");
      json += "}";

      json += ",\"diag_text\":\"" + sanitizeJsonString(diagText) + "\"";
      json += ",\"diag_log\":\"" + sanitizeJsonString(diagLogMsg) + "\"";
      
      json += "}";
      return json;
    }

    void posliTelemetryState(unsigned long ted) {
      if (ted - posledniStateSendMs < 200) return;

      String json = buildTelemetryState();
      if (json != posledniStateJson) {
        posledniStateJson = json;
        // Odesílání přes BLE odstraněno. Telemetrie se nyní čte na vyžádání přes HTTP API.
        Serial.println("TELEMETRY:" + json); // Zabaleno s prefixem pro Web Serial API (USB)
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
          physGameMode = novyMod;
          processGameModeChange(lastGameMode);
          Serial.print("Web vnutil novy rezim: "); Serial.println(cmd);
        }
        // Povel pro tajnou schránku (A, B, C, D, E) z webové aplikace
        else if (cmd >= 'A' && cmd <= 'E') {
          Serial.print("Web žada otevreni schranky: "); Serial.println(cmd);
          if (cmd == 'A') posliPrikazI2C(ADDR_LEBKA, 'A');
          else if (cmd == 'B') {
            posliPrikazI2C(ADDR_TLACITKA, 'B');
            posliPrikazI2C(ADDR_SVETLA, 'O');
          }
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
        // Nástroje (I2C skener)
        else if (cmd == 'I') {
          activeDiagModule = 99;
          diagText = scanI2C();
          diagLogMsg = "I2C Skener dokoncil praci.";
          Serial.println(diagText);
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
      String postBody = "";

      while (client.connected() && (millis() - startWait < 300)) {
        if (client.available()) {
          startWait = millis();
          char c = client.read();

          if (isHeader) {
            if (c == '\n') {
              if (currentLine.length() == 0) {
                isHeader = false;
                if (reqMethod != "POST" || contentLength == 0) {
                  break; // Pro GET a OPTIONS není tělo potřeba číst
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
                String lowerLine = currentLine;
                lowerLine.toLowerCase();
                if (lowerLine.startsWith("content-length:")) {
                  contentLength = lowerLine.substring(15).toInt();
                }
                currentLine = "";
              }
            } else if (c != '\r') {
              currentLine += c;
            }
          } else {
            postBody += c;
            if (contentLength > 0 && (int)postBody.length() >= contentLength) {
              break;
            }
          }
        } else {
          delay(1);
        }
      }

      // Odpověď odesíláme jako jeden ucelený paket (šetří paměť a sockety ENC28J60)
      if (reqMethod == "OPTIONS") {
        String resp = "HTTP/1.1 204 No Content\r\n"
                      "Access-Control-Allow-Origin: *\r\n"
                      "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
                      "Access-Control-Allow-Headers: *\r\n"
                      "Access-Control-Max-Age: 86400\r\n"
                      "Connection: close\r\n\r\n";
        client.print(resp);
      } else if (reqMethod == "GET" && (reqPath == "/api/state" || reqPath == "/")) {
        String payload = buildTelemetryState();
        String resp = "HTTP/1.1 200 OK\r\n"
                      "Access-Control-Allow-Origin: *\r\n"
                      "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
                      "Access-Control-Allow-Headers: *\r\n"
                      "Content-Type: application/json; charset=utf-8\r\n"
                      "Cache-Control: no-cache, no-store, must-revalidate\r\n"
                      "Content-Length: " + String(payload.length()) + "\r\n"
                      "Connection: close\r\n\r\n" + payload;
        client.print(resp);
      } else if (reqMethod == "POST" && reqPath == "/api/command") {
        String resp = "HTTP/1.1 200 OK\r\n"
                      "Access-Control-Allow-Origin: *\r\n"
                      "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
                      "Access-Control-Allow-Headers: *\r\n"
                      "Content-Type: text/plain; charset=utf-8\r\n"
                      "Content-Length: 2\r\n"
                      "Connection: close\r\n\r\nOK";
        client.print(resp);
      } else {
        String resp = "HTTP/1.1 404 Not Found\r\n"
                      "Access-Control-Allow-Origin: *\r\n"
                      "Content-Length: 0\r\n"
                      "Connection: close\r\n\r\n";
        client.print(resp);
      }

      client.flush();
      delay(2);
      client.stop();

      // Příkaz provedeme AŽ PO uzavření a uvolnění síťového socketu
      if (reqMethod == "POST" && reqPath == "/api/command" && postBody.length() > 0) {
        processCommand(postBody);
      }
    }

    void setup() {
      // Debugování do počítače
      Serial.begin(115200);
      
      // Komunikace s ESP32 č.1 (Komunikační brány) přes Sériovou linku UART2
      // Piny RX=16, TX=17
      Serial2.begin(115200, SERIAL_8N1, 16, 17);
      
      // Inicializace I2C jako Master
      Wire.begin();
      Wire.setTimeOut(25); // Timeout pro zamezeni zablokovani smycky pri vypadku I2C modulu
      
      // Inicializace tlacitka a LCD displeje
      pinMode(PIN_MODE_BTN, INPUT_PULLUP);
      lcd.init();
      lcd.backlight();
      
      // Nahrání vlastních znaků do paměti LCD
      lcd.createChar(1, customInverted1);
      lcd.createChar(2, customInverted2);
      lcd.createChar(3, customInverted3);
      
      lcd.setCursor(0, 0);
      lcd.print("System Start...");
      
      // Provedeme okamžitý sken při startu
      Serial.println(scanI2C());
      
      // Inicializace ESP-NOW (WiFi mode musí být WIFI_STA)
      WiFi.mode(WIFI_STA);
      esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
      
      if (esp_now_init() != ESP_OK) {
        Serial.println("Chyba inicializace ESP-NOW");
      } else {
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
      }
      
      // Inicializace Ethernetu s tvrdým zpožděním pro stabilitu PHY čipu
      delay(500);
      SPI.begin();
      Ethernet.init(ETH_CS);
      Serial.println("Inicializuji Ethernet (ENC28J60) se statickou IP...");
      
      // Nové agresivnější zahájení, které zajistí stabilní IP a Link Status i pro zarušený čip
      Ethernet.begin(mac, staticIP, dnsServer, gateway, subnet);
      delay(200);

      Serial.print("Ethernet nastaven na statickou IP: ");
      Serial.println(Ethernet.localIP());
      eth_connected = true;

      // Spuštění HTTP serveru
      server.begin();
      Serial.println("HTTP Server bezi na portu 80");

      eth_connected = (Ethernet.linkStatus() != LinkOFF);

      // Aby na začátku neházela Místnost 3 offline, dáme jí do začátku čas 15 sekund k dobru
      lastM3Heartbeat = millis();

      updateLCD(); // Zobrazí základní údaje hned po spuštění sítě

      casStartu = millis();
      Serial.println("ESP32 (Hlavni Mozek) byl uspesne nastartovan!");
      Serial.println("Cekam 7 vterin na srovnani senzoru...");
    }

    void loop() {
      // Nutno udržovat síťovou komunikaci u ENC28J60 (ačkoliv máme statickou IP)
      Ethernet.maintain();
      
      handleHttpClient();
      
      // Kontrola linku na LAN
      static unsigned long lastEthCheck = 0;
      if (millis() - lastEthCheck > 2000) {
        lastEthCheck = millis();
        eth_connected = (Ethernet.linkStatus() == LinkON);
      }
      
      unsigned long ted = millis();

      // --- 0. Zpracování příkazů z USB (Web Serial API) ---
      static String usbBuffer = "";
      while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n') {
          String msg = usbBuffer;
          usbBuffer = "";
          msg.trim();
          if (msg == "GET_STATE") {
            Serial.println("TELEMETRY:" + posledniStateJson);
          } else if (msg.length() > 0) {
            processCommand(msg);
          }
        } else if (c != '\r' && usbBuffer.length() < 100) {
          usbBuffer += c;
        }
      }

      // --- 0b. Zpracování zpětné vazby z WLED brány (Serial2) ---
      static String wledBuffer = "";
      while (Serial2.available()) {
        char c = Serial2.read();
        if (c == '\n') {
          String msg2 = wledBuffer;
          wledBuffer = "";
          msg2.trim();
          if (msg2.startsWith("WLED:")) {
            // Formát zprávy: "WLED:1,0,1"
            int indexCarky1 = msg2.indexOf(',');
            int indexCarky2 = msg2.indexOf(',', indexCarky1 + 1);
            if (indexCarky1 != -1 && indexCarky2 != -1 && msg2.length() > indexCarky2 + 1) {
              wled1_status = (msg2.charAt(indexCarky1 - 1) == '1');
              wled2_status = (msg2.charAt(indexCarky2 - 1) == '1');
              wled3_status = (msg2.charAt(indexCarky2 + 1) == '1');
            }
          }
        } else if (c != '\r' && wledBuffer.length() < 100) {
          wledBuffer += c;
        }
      }

    if (!inicializaceHotova && (ted - casStartu >= 7000)) {
        lebka_online = readI2CDiagnostics(ADDR_LEBKA, dataLebka);
        if (lebka_online) physCrystalsState = (dataLebka.crystals_mask == 7) ? 2 : 0;

        kola_online = readI2CDiagnostics(ADDR_KOLA, dataKola); 
        svetla_online = readI2CDiagnostics(ADDR_SVETLA, dataSvetla);
        if (svetla_online) physSvetlaState = dataSvetla.mode_running;

        physAudio = readI2CBasic(ADDR_AUDIO);
        audio_online = (physAudio != 255);
        
        lastGameMode = physGameMode; 
        lastColorButton = physColorButton; 
        lastCrystalsState = physCrystalsState;
        lastSvetlaState = physSvetlaState;
        
        // Oznámí stavy komunikační brány
        processGameModeChange(lastGameMode);
        Serial2.println("C" + String(lastColorButton));
        Serial2.println("K" + String(lastCrystalsState));
        Serial2.println("S" + String(lastSvetlaState >= 0 ? lastSvetlaState : 0));
        
        inicializaceHotova = true;
        Serial.println("Kalibrace dokoncena, system bezi!");
      }
      
      if (!inicializaceHotova) return;

      // --- 0. OBSLUHA TLAČÍTKA NA PINU 14 (Změna módu) ---
      bool currentModeBtn = digitalRead(PIN_MODE_BTN);
      if (currentModeBtn == LOW && lastModeBtnState == HIGH && (ted - lastModeBtnTime > 250)) {
        lastModeBtnTime = ted;
        // Přepíná mezi 0 (Herní) a 3 (Pracovní)
        int novyMod = (lastGameMode == 3) ? 0 : 3;
        lastGameMode = novyMod;
        physGameMode = novyMod;
        processGameModeChange(novyMod);
      }
      lastModeBtnState = currentModeBtn;

      // --- 1. NORMÁLNÍ REŽIM: RYCHLÉ ČTENÍ ZÁKLADNÍCH STAVŮ (1 BAJT) ---
      // Běží vždy, aby fungovala hra (bez zatížení I2C)
      if (ted - posledniI2C_Lasery >= 30) {
        uint8_t s = readI2CBasic(ADDR_LASER);
        laser_online = (s != 255);
        if (laser_online) { 
          // Laser posílá už jen 1 (Alarm) nebo 0 (Klid)
          // V PRACOVNÍM MÓDU (lastGameMode == 3) LASER NIKDY NEMĚNÍ MÓD (mód 3 může vypnout pouze tlačítko nebo aplikace)
          if (lastGameMode != 3) {
            if (s == 1 && lastGameMode != 1) { // Pokud dojde k přerušení laseru
              lastGameMode = 1;
              physGameMode = 1;
              processGameModeChange(1);
            } else if (s == 0 && lastGameMode == 1) {
              // Návrat z Alarmu do Herního módu
              lastGameMode = 0;
              physGameMode = 0;
              processGameModeChange(0);
            }
          }
        }
        posledniI2C_Lasery = millis();
      }

      if (ted - posledniI2C >= 100) {
        // Vyčteme stavy ze všech modulů pro telemetrii a LCD
        physAudio = readI2CBasic(ADDR_AUDIO); // Průběžná kontrola připojení Audio I2C (vrací 0 = zavřeno, 1 = otevřeno)
        audio_online = (physAudio != 255);

        // Řízení laseru podle dveří v 1. místnosti (kontakt na Audio 1-2)
        if (audio_online && lastGameMode != 3) {
          if (physAudio == 0) {
            // Dveře v 1. místnosti jsou ZAVŘENÉ -> Laser musí být vypnutý
            casOtevreniDveri1 = 0;
            if (laserAktivovanDvermi || minulyStavDveri1 != 0) {
              posliPrikazI2C(ADDR_LASER, 'F'); // Vypnout laser
              laserAktivovanDvermi = false;
              minulyStavDveri1 = 0;
              Serial.println("Dvere 1 zavreny: Laser vypnut.");
            }
          } else if (physAudio == 1) {
            // Dveře v 1. místnosti jsou OTEVŘENÉ -> Laser se zapne po 3 sekundách od otevření
            if (minulyStavDveri1 != 1) {
              casOtevreniDveri1 = ted;
              minulyStavDveri1 = 1;
              Serial.println("Dvere 1 otevreny: Odpocitavam 3s do zapnuti laseru...");
            }
            if (!laserAktivovanDvermi && casOtevreniDveri1 > 0 && (ted - casOtevreniDveri1 >= 3000)) {
              posliPrikazI2C(ADDR_LASER, 'N'); // Zapnout laser
              laserAktivovanDvermi = true;
              Serial.println("Dvere 1 otevreny > 3s: Laser zapnut.");
            }
          }
        }
        
        uint8_t prectenoTlacitka = readI2CBasic(ADDR_TLACITKA);
        tlacitka_online = (prectenoTlacitka != 255);

        uint8_t prectenoLebka = readI2CBasic(ADDR_LEBKA);
        lebka_online = (prectenoLebka != 255);

        uint8_t prectenoSvetla = readI2CBasic(ADDR_SVETLA);
        svetla_online = (prectenoSvetla != 255);

        // Herní akce a přepínání světel se vyhodnocují pouze v herním módu (0) nebo po dohrání (2), v pracovním módu (3) ani v alarmu (1) se neprovádí
        if (lastGameMode == 0 || lastGameMode == 2) { 
          bool zmenaTlacitek = false;
          bool zmenaLebky = false;
          bool zmenaSvetel = false;
          int s3 = physColorButton, s8 = physCrystalsState, s14 = physSvetlaState;

          if (tlacitka_online) {
            static bool chybaOdeslana = false;
            static bool otevrenoOdeslano = false;
            if (prectenoTlacitka == 4) {
              // Bylo zadáno špatné heslo 3x
              if (!chybaOdeslana) {
                posliPrikazI2C(ADDR_SVETLA, 'E');
                Serial.println("Odesilan prikaz E (Chyba) na Svetla.");
                chybaOdeslana = true;
              }
            } else if (prectenoTlacitka == 5) {
              // Schránka otevřena tlačítky!
              if (!otevrenoOdeslano) {
                posliPrikazI2C(ADDR_SVETLA, 'O');
                Serial.println("Odesilan prikaz O (Schranka otevrena) na Svetla.");
                otevrenoOdeslano = true;
              }
            } else {
              chybaOdeslana = false;
              if (prectenoTlacitka != 5) {
                otevrenoOdeslano = false;
              }
              if (prectenoTlacitka != physColorButton && prectenoTlacitka != 3 && prectenoTlacitka != 5) { 
                s3 = prectenoTlacitka; 
                physColorButton = prectenoTlacitka; 
                zmenaTlacitek = true; 
              }
            }
          }
          
          if (lebka_online && prectenoLebka != physCrystalsState) { 
            s8 = prectenoLebka; 
            physCrystalsState = prectenoLebka; 
            zmenaLebky = true; 
          }

          if (svetla_online && prectenoSvetla != physSvetlaState) {
            s14 = prectenoSvetla;
            physSvetlaState = prectenoSvetla;
            zmenaSvetel = true;
          }
          
          if (zmenaTlacitek) {
            lastColorButton = s3;
            Serial2.println("C" + String(lastColorButton)); 
          }
          
          if (zmenaLebky) {
            if (lastGameMode == 0) {
              if (s8 == 2 && lastCrystalsState != 2) {
                posliPrikazI2C(ADDR_AUDIO, 'K');
                posliPrikazI2C(ADDR_LEBKA, 'A'); // Automaticky otevrit schranku
              }
              else if (s8 != 2 && lastCrystalsState == 2) posliPrikazI2C(ADDR_AUDIO, '0');
            }
            lastCrystalsState = s8;
            Serial2.println("K" + String(lastCrystalsState)); 
          }

          if (zmenaSvetel) {
            lastSvetlaState = s14;
            Serial2.println("S" + String(lastSvetlaState));
            Serial.print("Master: Stav animace svetel zmenen na "); Serial.println(lastSvetlaState);
          }
        }
        posledniI2C = millis();
      }

      // --- 1.B: Pomalejší kontrola pro Kola (která normálně I2C nezatěžují) ---
      static unsigned long posledniI2C_Pomaly = 0;
      if (ted - posledniI2C_Pomaly >= 500) {
        kola_online = (readI2CBasic(ADDR_KOLA) != 255);
        posledniI2C_Pomaly = millis();
      }
      
      // --- 2. DIAGNOSTIKA NA VYŽÁDÁNÍ (Pouze v Dev Módu pro vybraný modul) ---
      if (devModeActive && activeDiagModule > 0 && (ted - posledniDiagTime >= 400)) {
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
              diagText = "--- LASER (I2C: 13) ---\nStav I2C (0=OK, 1=Alarm): " + String(dataLaser.status) +
                        "\nLaser Zapnut: " + String(dataLaser.laser_on ? "ANO" : "NE") +
                        "\nLDR Senzor: " + String(dataLaser.ldr_val) +
                        "\nPocet chyb trefeni: " + String(dataLaser.fails);
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
          case 99:
            // I2C Skener - text uz je vygenerovany v diagText, nic neobnovujeme
            break;
        }
        posliTelemetryState(millis()); // Odeslani na web
        posledniDiagTime = millis();
      }

      // Voláme updateLCD pořád dokola kvůli blikání a rolování spodního textu
      updateLCD();
    }