#include <OneWireHub.h>
#include <DS2408.h>

const int ONEWIRE_PIN = 3; 
OneWireHub hub(ONEWIRE_PIN);
DS2408 ds2408(0x29, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00); 

const int pinySenzoru[] = {A0, A1, A2};
const int pinLED_PWM = 6;              
const int pinZamek = 11;               
const int pinReleLebka = 7;            

const int dobaOtevreniOneWire = 1000;      
const int casDoOtevreniKrystaly = 7000;    
const int dobaCekaniAkceKrystaly = 1500;   
const int rychlostFading = 24;             

// LIMITY PRO PLOCHÉ MAGNETY
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

bool oneWireAktivniPraveTed = false; 
unsigned long casStartuOneWire = 0;
unsigned long casZmenyStavu = 0;
unsigned long casZtratyKrystalu = 0; 

void setup() {
  Serial.begin(115200); 
  
  pinMode(pinLED_PWM, OUTPUT);
  pinMode(pinZamek, OUTPUT);
  pinMode(pinReleLebka, OUTPUT);
  
  digitalWrite(pinZamek, LOW);
  digitalWrite(pinReleLebka, LOW);

  hub.attach(ds2408);
  ds2408.setPinState(0, true); 
  ds2408.setPinState(1, true); 
}

void loop() {
  unsigned long ted = millis();
  hub.poll();

  // 1. OBSLUHA ONEWIRE (OD MASTERA)
  if (ds2408.getPinState(1) == false && !oneWireAktivniPraveTed) {
    oneWireAktivniPraveTed = true;
    casStartuOneWire = ted;
    digitalWrite(pinZamek, HIGH);      
    digitalWrite(pinReleLebka, HIGH);  
    ds2408.setPinState(1, true); 
  }

  if (oneWireAktivniPraveTed && (ted - casStartuOneWire >= dobaOtevreniOneWire)) {
    oneWireAktivniPraveTed = false;
    digitalWrite(pinReleLebka, LOW);
  }

  // 2. ČTENÍ SENZORŮ KRYSTALŮ
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
    } 
    else if (akt <= limitDeaktivace) {
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

  // ZPOŽDĚNÍ ZNĚLKY: Pin 0 hlásí hotovo až ve stavu ODPOCET nebo dále
  ds2408.setPinState(0, (stavHry == ODPOCET || stavHry == ODEMYKANI) ? false : true);

  // --- ONEWIRE TELEMETRIE ---
  // Využíváme zbylé virtuální PIO piny DS2408 pro přenos stavu jednotlivých krystalů
  // a stavu hry směrem k Arduino_Svetla, které je předá dál do ESP32.
  ds2408.setPinState(2, krystalAktivni[0]);
  ds2408.setPinState(3, krystalAktivni[1]);
  ds2408.setPinState(4, krystalAktivni[2]);
  
  ds2408.setPinState(5, (stavHry == CEKANI_NA_KRYSTALY));
  ds2408.setPinState(6, (stavHry == ODPOCET));
  ds2408.setPinState(7, (stavHry == ODEMYKANI || stavHry == HOTOVO_CEKANI_NA_VYNDANI));

  // 3. LOGIKA AUTOMATU
  switch (stavHry) {
    case CEKANI_NA_KRYSTALY:
      if (vsechnyKrystalyOk && krystalAktivni[0] && krystalAktivni[1] && krystalAktivni[2]) {
        stavHry = ODPOCET;
        casZmenyStavu = ted;
      }
      break;

    case ODPOCET:
      if (vsechnyKrystalyOk == false) {
        if (casZtratyKrystalu == 0) casZtratyKrystalu = ted;
        if (ted - casZtratyKrystalu > 300) { 
          stavHry = CEKANI_NA_KRYSTALY;
          casZtratyKrystalu = 0;
        }
      } else {
        casZtratyKrystalu = 0; 
        if (ted - casZmenyStavu >= casDoOtevreniKrystaly) {
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
        stavHry = CEKANI_NA_KRYSTALY;
      }
      break;
  }

  if (oneWireAktivniPraveTed || (stavHry == ODEMYKANI)) {
    digitalWrite(pinZamek, HIGH);
  } else {
    digitalWrite(pinZamek, LOW);
  }

  // 4. EFEKT SVĚTLA
  static int jas = 0;
  static unsigned long lastF = 0;
  
  bool efektSvetla = (stavHry != CEKANI_NA_KRYSTALY);
  
  if (ted - lastF >= rychlostFading) {
    if (efektSvetla && jas < 255) jas++;
    else if (!efektSvetla && jas > 0) jas--;
    analogWrite(pinLED_PWM, jas);
    lastF = ted;
  }
}