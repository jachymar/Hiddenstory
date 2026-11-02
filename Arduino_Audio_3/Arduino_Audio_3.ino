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
volatile bool cmdSolved = false; // Prikaz pro vyreseni puzzle

bool pracovniMod = false;
char lastLog[30] = "Start";
unsigned long lastTapTime = 0;

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
    else if (c == 'S' || c == '3') {
      cmdStop = true;
      pracovniMod = true;
    }
    else if (c == '0') {
      pracovniMod = false;
      lastTapTime = millis(); // Ochrana proti okamzitemu spusteni hintu po zapnuti hry
    }
    else if (c == 'A') {
      cmdAlarm = true; // Rytmus lze spustit i v pracovnim modu
    }
    else if (c == 'E') {
      if (!pracovniMod) cmdBeep = true;
    }
    else if (c == 'D') {
      if (!pracovniMod) cmdSolved = true;
    }
  }
}

// *** KONFIGURACE PINU PRO SHIELD ***
#define VS1053_RESET    8      
#define VS1053_CS       6      
#define VS1053_DCS      7      
#define VS1053_DREQ     2      
#define CARD_CS         9      

// *** PINY PRO SENZORY ***
#define PIN_DVERE       10     
#define PIN_SIGNAL_EXT   5     

// *** NASTAVENI ***
const unsigned long CLICK_WINDOW     = 450;  // Okno pro rozpoznani dvojkliku (ms)
const int HLASITOST_HLAVNI = 20; 

// *** SKLADBY ***
const char* TRACK_FINAL = "/0003.mp3"; // Hlavni nezastavitelny alarm / rytmus

Adafruit_VS1053_FilePlayer filePlayer = 
  Adafruit_VS1053_FilePlayer(VS1053_RESET, VS1053_CS, VS1053_DCS, VS1053_DREQ, CARD_CS);

// Promenne pro Dvere (Pin 10) a Hint logiku
bool doorOpen = false;
bool isPlayingHint = false;
unsigned long solvedTime = 0;

// Casovani
const unsigned long SURPRISE_DELAY_MS = 3500;   // 3.5s prekvapeni po otevreni dveri
const unsigned long SOLVED_COOLDOWN_MS = 60000; // 1 minuta ticha po spravnem vyreseni
const unsigned long GRACE_PERIOD_MS = 90000;    // 90s ochrana pote, co dohraje prekvapeni
const unsigned long HINT_COOLDOWN_MS = 45000;   // 45s pauza mezi napovedami

bool surprisePending = false;
unsigned long gracePeriodStartTime = 0;

unsigned long doorOpenTime = 0;
unsigned long lastHintPlayedTime = 0;
bool lastDoorState = LOW;

bool hint8sPlayed = false;   
bool hint30sPlayed = false;  
unsigned long tapSessionStartTime = 0; 

// Promenne pro Signal (Pin 5) - rucni stop
bool lastSignalState = HIGH;
int clickCount = 0;
unsigned long lastClickTime = 0;
bool waitingForDecision = false;

// Funkce pro totalni zastaveni
void stopEverything() {
  waitingForDecision = false;
  clickCount = 0;
  isPlayingHint = false;
  hint8sPlayed = false;
  hint30sPlayed = false;
  tapSessionStartTime = 0;
  lastHintPlayedTime = 0;
  surprisePending = false;
  gracePeriodStartTime = 0;
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
  
  Serial.println(F("Audio 3 ready. Surprise & Hint logic active."));
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
    Log("I2C Povel: Rytmus spusten!");
    filePlayer.stopPlaying();
    filePlayer.startPlayingFile(TRACK_FINAL);
    isPlayingHint = true;
    surprisePending = false; // Zrusit pripadne prekvapeni v pozadi
  }

  // Uspesne vyreseni -> stop zvuku a 1 minuta ticha
  if (cmdSolved) {
    cmdSolved = false;
    solvedTime = now;
    hint8sPlayed = true;  
    hint30sPlayed = true;
    surprisePending = false;
    Log("Tukani uspesne: 1min ticho");
    if (isPlayingHint || filePlayer.playingMusic) {
      filePlayer.stopPlaying();
    }
    isPlayingHint = false;
  }

  // Prikaz pipnuti (tuknuti hrace) slouzi pouze k aktualizaci casovace
  if (cmdBeep) {
    cmdBeep = false;
    lastTapTime = now; 
    
    // Pokud je to prvni tuknuti v nove serii (po klidu nebo po resetu)
    if (tapSessionStartTime == 0) {
      tapSessionStartTime = now;
      hint30sPlayed = false; 
    }
    
    // Nove tuknuti resetuje 8s napovedu
    hint8sPlayed = false;
    
    // Zastavime napovedu, pokud zrovna hraje
    if (isPlayingHint) {
      filePlayer.stopPlaying();
      isPlayingHint = false;
      lastHintPlayedTime = now; // Zacatek cooldownu po rucnim preruseni
    }
  }
  
  // --- 1. CTENI DVERI (Pin 10) ---
  bool currentDoorState = digitalRead(PIN_DVERE);
  if (currentDoorState == HIGH && lastDoorState == LOW) { // Nabezna hrana - dvere se otevrou
    Log("Dvere otevreny! Za 3.5s hraje prekvapeni.");
    doorOpen = true;
    doorOpenTime = now;
    
    surprisePending = true;
    gracePeriodStartTime = 0; // Grace period zacne az po dohrani surprise
    
    lastTapTime = now;
    tapSessionStartTime = 0;
    hint8sPlayed = false;
    hint30sPlayed = false;
    if (filePlayer.playingMusic) filePlayer.stopPlaying();
  } else if (currentDoorState == LOW) {
    doorOpen = false;
    surprisePending = false;
  }
  lastDoorState = currentDoorState;

  // --- 2. HINT LOGIKA ---
  
  // 0) Moment prekvapeni (Surprise)
  if (doorOpen && surprisePending && (now - doorOpenTime >= SURPRISE_DELAY_MS)) {
    Log("Surprise! Hraji prvni rytmus po otevreni.");
    filePlayer.stopPlaying();
    filePlayer.startPlayingFile(TRACK_FINAL);
    isPlayingHint = true;
    surprisePending = false;
  }

  // Kdyz hint nebo prekvapeni dohraje do konce
  if (isPlayingHint && !filePlayer.playingMusic) {
    isPlayingHint = false;
    lastHintPlayedTime = now; // Start cooldownu
    
    // Pokud jeste nezacala Grace Period (tzn. prave dohral moment prekvapeni)
    if (gracePeriodStartTime == 0) {
      gracePeriodStartTime = now;
      lastTapTime = now; // Odsud se pocita pripadna necinnost
      Log("Grace Period (90s) odstartovana.");
    }
  }

  bool inSolvedCooldown = (solvedTime > 0 && (now - solvedTime < SOLVED_COOLDOWN_MS));
  // Dokud gracePeriodStartTime == 0, jeste nezaznelo (nebo prave zni) prekvapeni, takze blokujeme bezne hinty
  bool inGracePeriod = (gracePeriodStartTime > 0 && (now - gracePeriodStartTime < GRACE_PERIOD_MS)) || (gracePeriodStartTime == 0);
  bool inHintCooldown = (lastHintPlayedTime > 0 && (now - lastHintPlayedTime < HINT_COOLDOWN_MS));

  if (doorOpen && !inSolvedCooldown && !pracovniMod && !inGracePeriod && !inHintCooldown && !surprisePending) {
    
    // 1) Pravidlo pro 8s neaktivitu (prehraje se 1x, dokud hrac znovu netukne)
    if (!hint8sPlayed && lastTapTime > 0 && (now - lastTapTime >= 8000)) {
      if (!isPlayingHint && !filePlayer.playingMusic) {
        Log("Hint: 8s po tuknuti bez aktivity.");
        filePlayer.stopPlaying();
        filePlayer.startPlayingFile(TRACK_FINAL);
        isPlayingHint = true;
        hint8sPlayed = true; 
        tapSessionStartTime = 0; 
      }
    }

    // 2) Pravidlo pro dlouhe soustavne tukani (60s) s 2s pauzou na oddech
    // Abychom hrace neprerusili uprostred vytukavani, cekame na kratkou 2s pauzu
    if (!hint30sPlayed && tapSessionStartTime > 0 && (now - tapSessionStartTime >= 60000)) {
      if (!isPlayingHint && !filePlayer.playingMusic && (now - lastTapTime >= 2000)) {
        Log("Hint: 60s tukani a pauza.");
        filePlayer.stopPlaying();
        filePlayer.startPlayingFile(TRACK_FINAL); 
        isPlayingHint = true;
        hint30sPlayed = true; 
        hint8sPlayed = true;
      }
    }
  }

  // --- 3. LOGIKA SIGNALU (Pin 5) - rucni stop ---
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

  // Bezne krmeni bufferu mp3 
  if (filePlayer.playingMusic) {
    filePlayer.feedBuffer();
  }
}