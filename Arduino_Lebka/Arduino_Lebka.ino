#include <SoftwareSerial.h>
#include <Servo.h>

const int RX_PIN = 2;    // Arduino Lebka <- Laser TX
const int TX_PIN = 3;    // Arduino Lebka -> Laser RX
SoftwareSerial bridgeSerial(RX_PIN, TX_PIN);
const int pinServoSchranky = 9;
Servo servoSchranky;

struct DiagLebka {
  uint8_t status;
  uint8_t crystals_mask;
  uint16_t k1_val;
  uint16_t k2_val;
  uint16_t k3_val;
  uint8_t lock_open;
} __attribute__((packed));
DiagLebka myTelemetry = {0, 0, 0, 0, 0, 0};

volatile bool cmdOpenLock = false;
const char CMD_OPEN_LOCK = 'A';
char lastLog[30] = "Start";
bool i2cOpenActive = false;
unsigned long casStartuOtevreni = 0;

void Log(const char* txt) {
  strncpy(lastLog, txt, 29);
  lastLog[29] = '\0';
  Serial.println(txt);
}

const int pinySenzoru[] = {A0, A1, A2};
const int pinLED_PWM = 6;
const int pinZamek = 11;
const int pinReleLebka = 7;

const int dobaOtevreniI2C = 1000;
const int dobaPredServem = 220;
const int dobaOtevreniServa = 1200;
const int casDoOtevreniKrystaly = 7000;
const int dobaCekaniAkceKrystaly = 1500;
const int rychlostFading = 24;
const int servoZavreno = 180;
const int servoOtevreno = 30;

const int limitAktivace = 560;
const int limitDeaktivace = 545;
const int dobaPotvrzeniCile = 200;

bool krystalAktivni[3] = {false, false, false};
unsigned long casAktivaceKrystalu[3] = {0, 0, 0};
unsigned long casDeaktivaceKrystalu[3] = {0, 0, 0};

enum HerniStav {
  CEKANI_NA_KRYSTALY,
  ODPOCET,
  ODEMYKANI,
  HOTOVO_CEKANI_NA_VYNDANI
};
HerniStav stavHry = CEKANI_NA_KRYSTALY;

unsigned long casZmenyStavu = 0;
unsigned long casZtratyKrystalu = 0;

enum SchrankovaSekvence {
  SCHRANKA_IDLE,
  SCHRANKA_ZAMEK,
  SCHRANKA_SERVO,
  SCHRANKA_ZAVIRANI
};

SchrankovaSekvence schrankovaSekvence = SCHRANKA_IDLE;
unsigned long casSekvenceSchranky = 0;

void handleBridgeCommand(char cmd) {
  if (cmd == CMD_OPEN_LOCK) {
    cmdOpenLock = true;
    bridgeSerial.println("OK");
  } else if (cmd == 'S') {
    bridgeSerial.print((int)myTelemetry.crystals_mask);
    bridgeSerial.print('\n');
  } else if (cmd == 'D') {
    bridgeSerial.print((int)stavHry);
    bridgeSerial.print(',');
    bridgeSerial.print((int)myTelemetry.crystals_mask);
    bridgeSerial.print(',');
    bridgeSerial.print(myTelemetry.k1_val);
    bridgeSerial.print(',');
    bridgeSerial.print(myTelemetry.k2_val);
    bridgeSerial.print(',');
    bridgeSerial.print(myTelemetry.k3_val);
    bridgeSerial.print(',');
    bridgeSerial.print((int)myTelemetry.lock_open);
    bridgeSerial.print('\n');
  } else if (cmd == 'L') {
    bridgeSerial.println(lastLog);
  }
}

void setup() {
  Serial.begin(115200);
  bridgeSerial.begin(9600);

  pinMode(pinLED_PWM, OUTPUT);
  pinMode(pinZamek, OUTPUT);
  pinMode(pinReleLebka, OUTPUT);

  servoSchranky.attach(pinServoSchranky);
  servoSchranky.write(servoZavreno);

  digitalWrite(pinZamek, LOW);
  digitalWrite(pinReleLebka, LOW);
}

void loop() {
  unsigned long ted = millis();

  if (bridgeSerial.available()) {
    char c = bridgeSerial.read();
    if (c == 'A' || c == 'S' || c == 'D' || c == 'L') {
      handleBridgeCommand(c);
    }
  }

  if (cmdOpenLock && schrankovaSekvence == SCHRANKA_IDLE) {
    Log("UART: Prikaz k otevreni");
    i2cOpenActive = true;
    casStartuOtevreni = ted;
    casSekvenceSchranky = ted;
    schrankovaSekvence = SCHRANKA_ZAMEK;
    digitalWrite(pinZamek, HIGH);
    digitalWrite(pinReleLebka, HIGH);
    cmdOpenLock = false;
  }

  switch (schrankovaSekvence) {
    case SCHRANKA_ZAMEK:
      if (ted - casSekvenceSchranky >= dobaPredServem) {
        servoSchranky.write(servoOtevreno);
        casSekvenceSchranky = ted;
        schrankovaSekvence = SCHRANKA_SERVO;
      }
      break;

    case SCHRANKA_SERVO:
      if (ted - casSekvenceSchranky >= dobaOtevreniServa) {
        servoSchranky.write(servoZavreno);
        digitalWrite(pinZamek, LOW);
        digitalWrite(pinReleLebka, LOW);
        casSekvenceSchranky = ted;
        schrankovaSekvence = SCHRANKA_ZAVIRANI;
      }
      break;

    case SCHRANKA_ZAVIRANI:
      if (ted - casSekvenceSchranky >= 250) {
        servoSchranky.write(servoZavreno);
        schrankovaSekvence = SCHRANKA_IDLE;
        i2cOpenActive = false;
      }
      break;

    case SCHRANKA_IDLE:
      break;
  }

  if (i2cOpenActive && (ted - casStartuOtevreni >= dobaOtevreniI2C) && schrankovaSekvence == SCHRANKA_IDLE) {
    i2cOpenActive = false;
    digitalWrite(pinReleLebka, LOW);
  }

  bool vsechnyKrystalyOk = true;
  for (int i = 0; i < 3; i++) {
    int akt = analogRead(pinySenzoru[i]);

    if (akt >= limitAktivace) {
      casDeaktivaceKrystalu[i] = 0;
      if (!krystalAktivni[i]) {
        if (casAktivaceKrystalu[i] == 0) casAktivaceKrystalu[i] = ted;
        if (ted - casAktivaceKrystalu[i] >= dobaPotvrzeniCile) {
          krystalAktivni[i] = true;
        }
      }
    } else if (akt <= limitDeaktivace) {
      casAktivaceKrystalu[i] = 0;
      if (krystalAktivni[i]) {
        if (casDeaktivaceKrystalu[i] == 0) casDeaktivaceKrystalu[i] = ted;
        if (ted - casDeaktivaceKrystalu[i] >= dobaPotvrzeniCile) {
          krystalAktivni[i] = false;
        }
      }
    }

    if (krystalAktivni[i] == false) {
      vsechnyKrystalyOk = false;
    }
  }

  switch (stavHry) {
    case CEKANI_NA_KRYSTALY:
      if (vsechnyKrystalyOk && krystalAktivni[0] && krystalAktivni[1] && krystalAktivni[2]) {
        Log("Vsechny krystaly na miste!");
        stavHry = ODPOCET;
        casZmenyStavu = ted;
      }
      break;

    case ODPOCET:
      if (vsechnyKrystalyOk == false) {
        if (casZtratyKrystalu == 0) casZtratyKrystalu = ted;
        if (ted - casZtratyKrystalu > 300) {
          Log("Krystal ztracen behem odpoctu");
          stavHry = CEKANI_NA_KRYSTALY;
          casZtratyKrystalu = 0;
        }
      } else {
        casZtratyKrystalu = 0;
        if (ted - casZmenyStavu >= casDoOtevreniKrystaly) {
          Log("Odpocet hotov. Oteviram!");
          stavHry = ODEMYKANI;
          casZmenyStavu = ted;
        }
      }
      break;

    case ODEMYKANI:
      if (ted - casZmenyStavu >= dobaCekaniAkceKrystaly) {
        stavHry = HOTOVO_CEKANI_NA_VYNDANI;
      }
      break;

    case HOTOVO_CEKANI_NA_VYNDANI:
      if (vsechnyKrystalyOk == false) {
        Log("Krystaly odebrany. Reset.");
        stavHry = CEKANI_NA_KRYSTALY;
      }
      break;
  }

  if (i2cOpenActive || (stavHry == ODEMYKANI)) {
    digitalWrite(pinZamek, HIGH);
  } else {
    digitalWrite(pinZamek, LOW);
  }

  static int jas = 0;
  static unsigned long lastF = 0;

  bool efektSvetla = (stavHry != CEKANI_NA_KRYSTALY);

  if (ted - lastF >= rychlostFading) {
    if (efektSvetla && jas < 255) jas++;
    else if (!efektSvetla && jas > 0) jas--;
    analogWrite(pinLED_PWM, jas);
    lastF = ted;
  }

  myTelemetry.status = (uint8_t)stavHry;
  myTelemetry.crystals_mask = (krystalAktivni[0] ? 1 : 0) | (krystalAktivni[1] ? 2 : 0) | (krystalAktivni[2] ? 4 : 0);
  myTelemetry.k1_val = analogRead(pinySenzoru[0]);
  myTelemetry.k2_val = analogRead(pinySenzoru[1]);
  myTelemetry.k3_val = analogRead(pinySenzoru[2]);
  myTelemetry.lock_open = (i2cOpenActive || schrankovaSekvence != SCHRANKA_IDLE || stavHry == ODEMYKANI) ? 1 : 0;
}