#include <SPI.h>
#include <SD.h> 
#include <Adafruit_VS1053.h>
#include <Wire.h>

#define I2C_SLAVE_ADDR 21

volatile byte i2c_req = 0;
volatile byte i2cStatus = 0;
volatile bool cmdStop = false;
char lastLog[30] = "Start";

void Log(const char* txt) {
  strncpy(lastLog, txt, 29);
  lastLog[29] = '\0';
  Serial.println(txt);
}

struct DiagAudio3 {
  uint8_t status;
  uint8_t is_playing;
  uint8_t is_alarm_playing;
  uint8_t padding;
} __attribute__((packed));
DiagAudio3 myTelemetry = {0, 0, 0, 0};

void requestEvent() {
  if (i2c_req == 0x99) {
    Wire.write((byte*)&myTelemetry, sizeof(DiagAudio3));
    i2c_req = 0;
  } else if (i2c_req == 0x98) {
    Wire.write((byte*)lastLog, 30);
    i2c_req = 0;
  } else {
    Wire.write(i2cStatus);
  }
}

void receiveEvent(int howMany) {
  while (Wire.available()) {
    byte c = Wire.read();
    if (c == 0x99 || c == 0x98) i2c_req = c;
    else if (c == 'S') cmdStop = true;
  }
}

// *** KONFIGURACE PINŮ PRO SHIELD ***
#define VS1053_RESET    8     
#define VS1053_CS       6     
#define VS1053_DCS      7     
#define VS1053_DREQ     2     
#define CARD_CS         9     

// *** PINY PRO SENZORY ***
#define PIN_DVERE       10    
#define PIN_SIGNAL_EXT   5    

// *** NASTAVENÍ ČASŮ ***
const unsigned long PRODLEVA_DVERE  = 5000;  // 5 sekund pro dveře
const unsigned long PRODLEVA_SIGNAL = 10000; // 10 sekund pro finální zvuk 0003
const unsigned long CLICK_WINDOW     = 450;  // Okno pro rozpoznání dvojkliku (ms)
const int HLASITOST_HLAVNI = 20;

// *** SKLADBY ***
const char* TRACK_FINAL = "/0003.mp3"; // Hlavní nezastavitelný alarm

Adafruit_VS1053_FilePlayer filePlayer = 
  Adafruit_VS1053_FilePlayer(VS1053_RESET, VS1053_CS, VS1053_DCS, VS1053_DREQ, CARD_CS);

// Proměnné pro Dveře (Pin 10)
bool doorCountdownActive = false;
unsigned long doorTriggerTime = 0;
bool lastDoorState = LOW;

// Proměnné pro Signál (Pin 5)
bool signalCountdownActive = false;
unsigned long signalStartTime = 0;
bool lastSignalState = HIGH;
int clickCount = 0;
unsigned long lastClickTime = 0;
bool waitingForDecision = false;

// NOVÁ PROMĚNNÁ: Příznak, že hraje nezastavitelný alarm
bool isAlarmPlaying = false; 

// Funkce pro úplné zastavení (nyní s pojistkou)
void stopEverything() {
  // Pokud hraje finální alarm, tato funkce už nesmí nic vypnout
  if (isAlarmPlaying) return; 

  doorCountdownActive = false;
  signalCountdownActive = false;
  waitingForDecision = false;
  clickCount = 0;
  if (filePlayer.playingMusic) {
    filePlayer.stopPlaying();
  }
  Log("SYSTEM VYPNUT (Deaktivace)");
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_DVERE, INPUT_PULLUP);
  pinMode(PIN_SIGNAL_EXT, INPUT_PULLUP);

  if (!filePlayer.begin()) { while (1); }
  if (!SD.begin(CARD_CS)) { while (1); }

  filePlayer.setVolume(HLASITOST_HLAVNI, HLASITOST_HLAVNI);

  Wire.begin(I2C_SLAVE_ADDR);
  Wire.onRequest(requestEvent);
  Wire.onReceive(receiveEvent);

  Serial.println(F("Ready. Alarm 0003.mp3 nelze po spuštění přerušit."));
}

void loop() {
  // Aktualizace telemetrie pred returnem
  myTelemetry.status = i2cStatus;
  myTelemetry.is_playing = filePlayer.playingMusic ? 1 : 0;
  myTelemetry.is_alarm_playing = isAlarmPlaying ? 1 : 0;
  
  if (cmdStop) {
    cmdStop = false;
    stopEverything(); // if isAlarmPlaying is true, stopEverything() will ignore it, which is correct
  }

  // --- 0. NEZASTAVITELNÝ ALARM (BLOKOVÁNÍ SYSTÉMU) ---
  if (isAlarmPlaying) {
    if (filePlayer.playingMusic) {
      filePlayer.feedBuffer(); // Udržujeme přehrávání v chodu
    } else {
      // Skladba dohrála až do konce
      isAlarmPlaying = false;
      Serial.println(F("Alarm dohrál do konce. Systém se vrací do hlídacího režimu."));
      
      // Resetujeme stavy senzorů, aby se alarm nespustil okamžitě znovu
      doorCountdownActive = false;
      signalCountdownActive = false;
      waitingForDecision = false;
      clickCount = 0;
      lastDoorState = digitalRead(PIN_DVERE);
      lastSignalState = digitalRead(PIN_SIGNAL_EXT);
    }
    return; // ZÁSADNÍ KROK: Ukončí smyčku loop() zde. Kód níže se vůbec neprovede!
  }

  // --- 1. LOGIKA DVEŘÍ (Pin 10) ---
  bool currentDoorState = digitalRead(PIN_DVERE);
  if (currentDoorState == HIGH && lastDoorState == LOW) {
    if (!doorCountdownActive) {
      Log("Dvere otevreny! Odpocet 5s.");
      doorCountdownActive = true;
      doorTriggerTime = millis();
    }
  }
  lastDoorState = currentDoorState;

  if (doorCountdownActive && (millis() - doorTriggerTime >= PRODLEVA_DVERE)) {
    Log("5s vyprselo. Hraji ALARM!");
    filePlayer.stopPlaying(); 
    filePlayer.startPlayingFile(TRACK_FINAL);
    isAlarmPlaying = true; // Zamykáme systém!
    doorCountdownActive = false; 
  }


  // --- 2. LOGIKA SIGNÁLU (Pin 5) ---
  bool currentSignalState = digitalRead(PIN_SIGNAL_EXT);

  if (currentSignalState == LOW && lastSignalState == HIGH) {
    clickCount++;
    lastClickTime = millis();
    waitingForDecision = true;
    delay(50); // Debounce
  }
  lastSignalState = currentSignalState;

  if (waitingForDecision) {
    // DVOJKLIK -> VYPNOUT VŠE
    if (clickCount >= 2) {
      stopEverything();
    } 
    // POTVRZENÝ 1 KLIK
    else if (millis() - lastClickTime > CLICK_WINDOW) {
      Log("Spatny signal! Startuji 10s.");
      if (filePlayer.playingMusic) {
        filePlayer.stopPlaying();
      }
      signalCountdownActive = true;
      signalStartTime = millis();
      waitingForDecision = false;
      clickCount = 0;
    }
  }

  // Vyhodnocení 10s odpočtu pro signál
  if (signalCountdownActive && (millis() - signalStartTime >= PRODLEVA_SIGNAL)) {
    Log("10s vyprselo. Hraji ALARM!");
    filePlayer.stopPlaying();
    filePlayer.startPlayingFile(TRACK_FINAL); 
    isAlarmPlaying = true; // Zamykáme systém!
    signalCountdownActive = false;
  }

  // Běžné krmení bufferu (pro případ, že bys do kódu v budoucnu přidal jinou hudbu)
  if (filePlayer.playingMusic && !isAlarmPlaying) {
    filePlayer.feedBuffer();
  }
}