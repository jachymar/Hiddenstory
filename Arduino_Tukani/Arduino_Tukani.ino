// --- KONFIGURACE ---
const int PIEZO_PIN = A1;            // Senzor na pinu A1
const int OUTPUT_PIN = 8;            // Hlavní akční pin (2 s při úspěchu)
const int STATUS_PIN = 9;            // Stavový pin (bzučák/LED)

const int THRESHOLD = 50;            // Citlivost senzoru
const unsigned long DEBOUNCE_TIME_MS = 150;  // Debounce mezi jednotlivými ťuknutími
const unsigned long RESET_TIMEOUT_MS = 3000;  // Reset paměti po 3 s nečinnosti
const unsigned long LOCK_TIME_MS = 2000;      // Doba, po kterou zůstane výstup pin 8 aktivní
const unsigned long BEEP_TIME_MS = 100;       // Délka jednorázového pípnutí

const float RATIO_TOLERANCE = 0.30;  // Tolerance +/- 30 % od očekávaného tempa

// RYTMUS: 1200, 350, 350, 350, 1200 (5 intervalů = 6 ťuknutí)
const long RHYTHM_PATTERN[] = {1200, 350, 350, 350, 1200};
const int RHYTHM_LENGTH = sizeof(RHYTHM_PATTERN) / sizeof(RHYTHM_PATTERN[0]);

// --- PROMĚNNÉ PRO KONTINUÁLNÍ MĚŘENÍ ---
int intervalCount = 0;
unsigned long lastTapTime = 0;
unsigned long lastTapEventTime = 0;
long measuredIntervals[RHYTHM_LENGTH];

// --- PROMĚNNÉ PRO NEBLOKUJÍCÍ PÍPÁNÍ A ZÁMEK ---
unsigned long beepEndTime = 0;
bool isBeeping = false;
unsigned long lockEndTime = 0;
bool isUnlocked = false;
bool piezoWasHigh = false;

void setup() {
  Serial.begin(115200);

  pinMode(OUTPUT_PIN, OUTPUT);
  pinMode(STATUS_PIN, OUTPUT);

  digitalWrite(OUTPUT_PIN, LOW);
  digitalWrite(STATUS_PIN, LOW);

  Serial.println("--- System Start (Kontinualni, Neblokujici rezim) ---");
  Serial.println("Posloucham... Kdykoliv vyklepej spravnou sekvenci.");
}

void loop() {
  unsigned long currentMillis = millis();

  // 1. NEBLOKUJÍCÍ OBSLUHA HARDWARU
  if (isBeeping && currentMillis >= beepEndTime) {
    digitalWrite(STATUS_PIN, LOW);
    isBeeping = false;
  }

  if (isUnlocked && currentMillis >= lockEndTime) {
    digitalWrite(OUTPUT_PIN, LOW);
    isUnlocked = false;
    resetRhythm();
  }

  // 2. DETEKCE KLEPNUTÍ (jen na náběžné hraně)
  int sensorReading = analogRead(PIEZO_PIN);
  bool sensorHigh = sensorReading > THRESHOLD;

  if (sensorHigh && !piezoWasHigh && !isUnlocked) {
    if (currentMillis - lastTapEventTime >= DEBOUNCE_TIME_MS) {
      handleTap(currentMillis);
      lastTapEventTime = currentMillis;
    }
  }

  piezoWasHigh = sensorHigh;

  if (isUnlocked) return;

  // 3. Timeout reset po 3 sekundách nečinnosti
  if (lastTapTime > 0 && (currentMillis - lastTapTime) > RESET_TIMEOUT_MS) {
    Serial.println("\n❌ Dlouha pauza. Mazu pamet (Timeout).");
    triggerBeep(BEEP_TIME_MS);
    resetRhythm();
  }
}

// --- HLAVNÍ KONTINUÁLNÍ LOGIKA ---
void handleTap(unsigned long currentTime) {
  // První ťuknutí po resetu
  if (lastTapTime == 0) {
    Serial.println("\n✅ Prvni t'uknuti zaznamenano...");
    lastTapTime = currentTime;
    triggerBeep(BEEP_TIME_MS);
    return;
  }

  // Spočítáme čas od posledního ťuknutí
  long elapsedTime = currentTime - lastTapTime;

  // Posuneme staré intervaly v poli o jedno místo doleva
  for (int i = 0; i < RHYTHM_LENGTH - 1; i++) {
    measuredIntervals[i] = measuredIntervals[i + 1];
  }

  // Zapíšeme nový interval na konec pole
  measuredIntervals[RHYTHM_LENGTH - 1] = elapsedTime;

  // Zvýšíme počítadlo uložených intervalů
  if (intervalCount < RHYTHM_LENGTH) {
    intervalCount++;
  }

  Serial.print("✅ T'uknuti (interval: ");
  Serial.print(elapsedTime);
  Serial.println(" ms)");

  lastTapTime = currentTime;

  bool isSuccess = false;

  // Pokud máme plný počet intervalů, zkusíme rytmus vyhodnotit
  if (intervalCount == RHYTHM_LENGTH) {
    isSuccess = evaluateRhythm();
  }

  // Pokud rytmus neodpovídá, pípněme na pozadí
  if (!isSuccess) {
    triggerBeep(BEEP_TIME_MS);
  }
}

bool evaluateRhythm() {
  float sumOfPatternTimes = 0.0;
  float sumOfMeasuredTimes = 0.0;

  for (int i = 0; i < RHYTHM_LENGTH; i++) {
    sumOfPatternTimes += RHYTHM_PATTERN[i];
    sumOfMeasuredTimes += measuredIntervals[i];
  }

  float finalScaleFactor = sumOfMeasuredTimes / sumOfPatternTimes;
  bool rhythmSuccess = true;

  Serial.print("--- KONTROLA POSLEDNICH 5 INTERVALU (Scale: x");
  Serial.print(finalScaleFactor);
  Serial.println(") ---");

  for (int i = 0; i < RHYTHM_LENGTH; i++) {
    long expectedScaledTime = RHYTHM_PATTERN[i] * finalScaleFactor;
    long minTime = expectedScaledTime * (1.0 - RATIO_TOLERANCE);
    long maxTime = expectedScaledTime * (1.0 + RATIO_TOLERANCE);

    Serial.print("Int ");
    Serial.print(i + 1);
    Serial.print(": ");
    Serial.print(measuredIntervals[i]);

    if (measuredIntervals[i] < minTime || measuredIntervals[i] > maxTime) {
      Serial.print(" ms ❌ (Cil: ");
      Serial.print(expectedScaledTime);
      Serial.println(")");
      rhythmSuccess = false;
    } else {
      Serial.print(" ms ✅ (Cil: ");
      Serial.print(expectedScaledTime);
      Serial.println(")");
    }
  }

  if (rhythmSuccess) {
    Serial.println("\n🎉🎉🎉 RYTMUS USPESNY! Aktivuji pin 8.");

    digitalWrite(OUTPUT_PIN, HIGH);
    lockEndTime = millis() + LOCK_TIME_MS;
    isUnlocked = true;

    return true;
  } else {
    Serial.println("❌ Sekvence neodpovida. Cekam na dalsi t'uknuti...\n");
    return false;
  }
}

void triggerBeep(unsigned long duration) {
  digitalWrite(STATUS_PIN, HIGH);
  beepEndTime = millis() + duration;
  isBeeping = true;
}

void resetRhythm() {
  intervalCount = 0;
  lastTapTime = 0;

  for (int i = 0; i < RHYTHM_LENGTH; i++) {
    measuredIntervals[i] = 0;
  }

  Serial.println("\n--- Pripraven na novou sekvenci ---");
}