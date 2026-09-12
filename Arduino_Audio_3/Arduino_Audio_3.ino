#include <SPI.h>
#include <SD.h> 
#include <Adafruit_VS1053.h>

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
  Serial.println(F(">>> SYSTÉM VYPNUT (Deaktivace) <<<"));
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_DVERE, INPUT_PULLUP);
  pinMode(PIN_SIGNAL_EXT, INPUT_PULLUP);

  if (!filePlayer.begin()) { while (1); }
  if (!SD.begin(CARD_CS)) { while (1); }

  filePlayer.setVolume(HLASITOST_HLAVNI, HLASITOST_HLAVNI);
  Serial.println(F("Ready. Alarm 0003.mp3 nelze po spuštění přerušit."));
}

void loop() {
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
      Serial.println(F("Dveře otevřeny! Odpočet 5s pro 0003.mp3"));
      doorCountdownActive = true;
      doorTriggerTime = millis();
    }
  }
  lastDoorState = currentDoorState;

  if (doorCountdownActive && (millis() - doorTriggerTime >= PRODLEVA_DVERE)) {
    Serial.println(F("5s vypršelo (Dveře) -> Hraji NEZASTAVITELNÝ ALARM 0003.mp3"));
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
      Serial.println(F("Špatný signál potvrzen! Startuji tichý 10s odpočet."));
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
    Serial.println(F("10s vypršelo (Signál) -> Hraji NEZASTAVITELNÝ ALARM 0003.mp3"));
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