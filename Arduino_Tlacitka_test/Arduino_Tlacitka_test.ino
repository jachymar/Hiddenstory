/*
 * TESTOVACÍ SKETCH PRO TLAČÍTKA
 * Rychlost Serial Monitoru: 115200 baud
 */

const int tlacitka[] = {2, 3, 4, 5, 6};
const int POCET_TLACITEK = sizeof(tlacitka) / sizeof(tlacitka[0]);

bool minulyStav[POCET_TLACITEK];
unsigned long casPosledniZmeny[POCET_TLACITEK];
const unsigned long DEBOUNCE_MS = 50;

void setup() {
  Serial.begin(115200);
  Serial.println(F("========================================"));
  Serial.println(F("       TESTOVACI REZIM TLACITEK         "));
  Serial.println(F("========================================"));
  Serial.println(F("Mackej tlacitka pro overeni pinu..."));

  for (int i = 0; i < POCET_TLACITEK; i++) {
    pinMode(tlacitka[i], INPUT_PULLUP);
    minulyStav[i] = digitalRead(tlacitka[i]);
    casPosledniZmeny[i] = 0;
  }
}

void loop() {
  unsigned long ted = millis();

  for (int i = 0; i < POCET_TLACITEK; i++) {
    int pin = tlacitka[i];
    bool aktualniCteni = (digitalRead(pin) == LOW); // LOW = stisknuto (proti GND)

    if (aktualniCteni != minulyStav[i] && (ted - casPosledniZmeny[i] >= DEBOUNCE_MS)) {
      casPosledniZmeny[i] = ted;
      minulyStav[i] = aktualniCteni;

      if (aktualniCteni) {
        Serial.print(F("[STISKNUTO] -> Tlacitko "));
        Serial.print(i + 1);
        Serial.print(F(" (Pin D"));
        Serial.print(pin);
        Serial.println(F(")"));
      } else {
        Serial.print(F("[UVOLNENO]  -> Tlacitko "));
        Serial.print(i + 1);
        Serial.print(F(" (Pin D"));
        Serial.print(pin);
        Serial.println(F(")"));
      }
    }
  }
}
