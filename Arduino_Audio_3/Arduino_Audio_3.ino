#include <SPI.h>
#include <SD.h> 
#include <Adafruit_VS1053.h>
#include <Wire.h>

#define I2C_SLAVE_ADDR 21

volatile byte i2c_req = 0;
volatile byte i2cStatus = 0;
volatile bool cmdStop = false;
volatile bool cmdAlarm = false; 
volatile bool cmdBeep = false;  
volatile bool cmdSolved = false; // Příkaz pro vyřešené puzzle

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
    else if (c == 'A') cmdAlarm = true; 
    else if (c == 'E') cmdBeep = true;  
    else if (c == 'D') cmdSolved = true; 
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
const char* TRACK_BEEP = "/beep.mp3"; // Pípnutí při chybě / ťuknutí

Adafruit_VS1053_FilePlayer filePlayer = 
  Adafruit_VS1053_FilePlayer(VS1053_RESET, VS1053_CS, VS1053_DCS, VS1053_DREQ, CARD_CS);

// Proměnné pro Dveře (Pin 10) a Hint logiku
bool doorOpen = false;
bool isPlayingHint = false;
unsigned long solvedTime = 0;
const unsigned long SOLVED_COOLDOWN_MS = 60000; // 1 minuta ticha po správném vyřešení
unsigned long doorOpenTime = 0;
unsigned long lastTapTime = 0;
bool lastDoorState = LOW;

// Nové příznaky pro nápovědu (jednorázové přehrání)
bool hint8sPlayed = false;   // Příznak, že už proběhla nápověda po 8s neaktivitě
bool hint30sPlayed = false;  // Příznak, že už proběhla nápověda po 30s aktivního ťukání
unsigned long tapSessionStartTime = 0; // Začátek aktuální série ťukání

// Proměnné pro Signál (Pin 5) - ponecháno pro ruční stop
bool lastSignalState = HIGH;
int clickCount = 0;
unsigned long lastClickTime = 0;
bool waitingForDecision = false;

// Funkce pro úplné zastavení
void stopEverything() {
  waitingForDecision = false;
  clickCount = 0;
  isPlayingHint = false;
  hint8sPlayed = false;
  hint30sPlayed = false;
  tapSessionStartTime = 0;
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

  Serial.println(F("Audio 3 ready. Hint logic active."));
}

void loop() {
  unsigned long now = millis();

  // Aktualizace telemetrie
  myTelemetry.status = i2cStatus;
  myTelemetry.is_playing = filePlayer.playingMusic ? 1 : 0;
  myTelemetry.is_alarm_playing = isPlayingHint ? 1 : 0;
  
  if (cmdStop) {
    cmdStop = false;
    stopEverything(); 
  }
  
  if (cmdAlarm) {
    cmdAlarm = false;
    Log("I2C Povel: ALARM SPUSTEN!");
    filePlayer.stopPlaying();
    filePlayer.startPlayingFile(TRACK_FINAL);
    isPlayingHint = true;
  }

  // Úspěšné naťukání kódu -> stop zvuku a 1 minuta ticha
  if (cmdSolved) {
    cmdSolved = false;
    solvedTime = now;
    hint8sPlayed = true;  // Blokujeme další nápovědy
    hint30sPlayed = true;
    Log("Tukani uspesne: 1min ticho");
    if (isPlayingHint || filePlayer.playingMusic) {
      filePlayer.stopPlaying();
    }
    isPlayingHint = false;
  }

  // Příkaz pípnutí (ťuknutí uživatele)
  if (cmdBeep) {
    cmdBeep = false;
    lastTapTime = now; // Aktualizace posledního ťuknutí
    
    // Pokud je to první ťuknutí v nové sérii (po klidu nebo po resetu)
    if (tapSessionStartTime == 0) {
      tapSessionStartTime = now;
      hint30sPlayed = false; // Reset 30s limitu pro novou sérii
    }
    
    // Nové ťuknutí resetuje 8s nápovědu (aby se po dalším ztišení mohla znovu zahrát)
    hint8sPlayed = false;

    // Zastavíme nápovědu, pokud zrovna hrála
    if (isPlayingHint) {
      filePlayer.stopPlaying();
      isPlayingHint = false;
    }
    
    // Zvuk pípnutí (beep) zahrajeme
    filePlayer.stopPlaying();
    filePlayer.startPlayingFile(TRACK_BEEP);
  }

  // --- 1. ČTENÍ DVEŘÍ (Pin 10) ---
  bool currentDoorState = digitalRead(PIN_DVERE);
  if (currentDoorState == HIGH && lastDoorState == LOW) { // Náběžná hrana - dveře se otevřely
    Log("Dvere otevreny!");
    doorOpen = true;
    doorOpenTime = now;
    lastTapTime = now;
    tapSessionStartTime = 0;
    hint8sPlayed = false;
    hint30sPlayed = false;
    if (filePlayer.playingMusic) filePlayer.stopPlaying();
  } else if (currentDoorState == LOW) {
    doorOpen = false;
  }
  lastDoorState = currentDoorState;

  // --- 2. HINT LOGIKA ---
  bool inSolvedCooldown = (solvedTime > 0 && (now - solvedTime < SOLVED_COOLDOWN_MS));

  if (doorOpen && !inSolvedCooldown) {
    // 1) Pravidlo pro 8s neaktivitu: od posledního ťuknutí / otevření uběhlo 8 s
    //    Přehraje se pouze 1x (dokud hráč znovu neťukne)
    if (!hint8sPlayed && lastTapTime > 0 && (now - lastTapTime >= 8000)) {
      if (!isPlayingHint && !filePlayer.playingMusic) {
        Log("Hint: 8s po tuknuti bez aktivity.");
        filePlayer.stopPlaying();
        filePlayer.startPlayingFile(TRACK_FINAL); // Zvuk rytmu
        isPlayingHint = true;
        hint8sPlayed = true; // Označíme, že už nápověda proběhla a víckrát nehraje
        tapSessionStartTime = 0; // Reset série ťukání
      }
    }

    // 2) Pravidlo pro 30s nepřetržitého ťukání:
    //    Hráči stále ťukají, takže 8s ticho nenastává. Pokud série trvá déle než 30 s, přehrajeme nápovědu 1x.
    if (!hint30sPlayed && tapSessionStartTime > 0 && (now - tapSessionStartTime >= 30000)) {
      if (!isPlayingHint && !filePlayer.playingMusic) {
        Log("Hint: 30s soustavneho tukani.");
        filePlayer.stopPlaying();
        filePlayer.startPlayingFile(TRACK_FINAL); // Zvuk rytmu
        isPlayingHint = true;
        hint30sPlayed = true; // Označíme jako zahrané pro tuto sérii
        hint8sPlayed = true;
      }
    }
  }

  // Když hint dohraje do konce
  if (isPlayingHint && !filePlayer.playingMusic) {
    isPlayingHint = false;
  }

  // --- 3. LOGIKA SIGNÁLU (Pin 5) - ruční stop ---
  bool currentSignalState = digitalRead(PIN_SIGNAL_EXT);
  if (currentSignalState == LOW && lastSignalState == HIGH) {
    clickCount++;
    lastClickTime = now;
    waitingForDecision = true;
    delay(50); // Debounce
  }
  lastSignalState = currentSignalState;

  if (waitingForDecision) {
    if (clickCount >= 2) {
      stopEverything();
    } else if (now - lastClickTime > CLICK_WINDOW) {
      waitingForDecision = false;
      clickCount = 0;
    }
  }

  // Běžné krmení bufferu mp3 čipu
  if (filePlayer.playingMusic) {
    filePlayer.feedBuffer();
  }
}