#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Servo.h>
#include <EEPROM.h> 

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Release tracking constant
const String RELEASE_VERSION = "1.0"; // INCREMENTED: Release version bump

// --- GLOBAL FLAGS ---
const bool testRun = false; // Set to true for time compression (1 min = 1 sec), false for production real-time
const int debugLevel = 2;

// --- PIN ASSIGNMENTS ---
const int PIN_BTN_MENU        = 4;
const int PIN_BTN_UP          = 5;
const int PIN_BTN_DOWN        = 6;
const int PIN_BTN_START_RESET = 7; 

const int srvMenu        = 9;   // Purple Wire on bread machine header Pin 7 
const int srvMinus       = 10;  // Yellow Wire on bread machine header pin 4
const int srvRunReset    = 11;  // Orange Wire on bread machine header pin 3
const int srvColour      = 12;  // Grey Wire on bread machine header pin 9

/*  
DonLim DL-T065-K Bread Machine
Solder a 10-pin femal header on the circuit board for the following buttons , GND and 5V.
Pin 1 - ground Black 
Pin 2 - 5V Red
Pin 3 - RunReset button Orange
Pin 4 - Minus button Yellow
Pin 5 - Plus button Green 
Pin 6 - Weight button Blue
Pin 7 - Menu button Purple
Pin 8 - Colour button Grey

SSD1306 i2c
SDA A4
SCL A5
GND GND
VCC 5V
*/

// --- ARRAYS FOR PARAMETER OPTIONS ---
// -1 = 1 Min Test Interval, 0 = Skip Stage Entirely
const int kneadOptions[] = {-1, 0, 25, 50, 75};
const int degasOptions[] = {-1, 0, 25, 50, 75, 100, 125, 150};
const int proofOptions[] = {-1, 0, 1, 2, 3, 4, 5, 6,7,8,9,10,11,12,13,14,15,16,17,18}; // Positive values represent Hours   
const int bakeOptions[]  = {-1, 0, 20, 30, 50, 75, 90, 100, 120};   

int kneadOptionsSize = sizeof(kneadOptions) / sizeof(kneadOptions[0]);
int degasOptionsSize = sizeof(degasOptions) / sizeof(degasOptions[0]);
int proofOptionsSize = sizeof(proofOptions) / sizeof(proofOptions[0]);
int bakeOptionsSize = sizeof(bakeOptions)   / sizeof(bakeOptions[0]);

// Default indices
int idxKnead = 2; // Defaults to 25 Min
int idxDegas = 3; // Defaults to 50 Min
int idxProof = 2; // Defaults to 1 Hour
int idxBake  = 4; // Defaults to 50 Min

// EEPROM Memory Map Addresses
const int EEPROM_ADDR_KNEAD = 0;
const int EEPROM_ADDR_DEGAS = 1;
const int EEPROM_ADDR_PROOF = 2;
const int EEPROM_ADDR_BAKE  = 3;

int currentMenuLine = 0; 
long totalMin = 0;
long remainingMin = 0;
int remainingSec = 0; 

// --- STATE MACHINE ENUMS ---
enum ProcessState { IDLE, KNEADING, DEGAS, PROOF, BAKE, FINISHED };
ProcessState currentState = IDLE;
bool isPaused = false;

// --- TIMERS & RUNTIME COUNTERS ---
unsigned long stateStartTime = 0;
unsigned long stepInterval = 0;
unsigned long oneSecondClock = 0;
int cyclesRemaining = 0;
bool stepInitialized = false;

// --- PAUSE BASELINE SHIFT TRACKERS ---
unsigned long pauseStartTime = 0;

// --- BUTTON DEBOUNCE TRACKING ---
bool lastBtnMenuState = HIGH;
bool lastBtnUpState = HIGH;
bool lastBtnDownState = HIGH;
bool runResetWasPressed = false;
unsigned long runResetPressTime = 0;

// --- REBOOT POINTER ---
void (* resetFunc) (void) = 0;

// --- DYNAMIC SERIAL LOGGER ENGINE WITH HH:MM:SS TIMESTAMPS ---
void serialLog(const __FlashStringHelper* message, long dataValue = -1, long cyclesVal = -1, long cycleIntervalVal = -1) {
  long hh = remainingMin / 60;
  long mm = remainingMin % 60;
  
  Serial.print(F("["));
  if (hh < 10) Serial.print(F("0"));
  Serial.print(hh);
  Serial.print(F(":"));
  if (mm < 10) Serial.print(F("0"));
  Serial.print(mm);
  Serial.print(F(":"));
  if (remainingSec < 10) Serial.print(F("0"));
  Serial.print(remainingSec);
  Serial.print(F("] "));
  
  if (currentState != IDLE && currentState != FINISHED && isPaused) {
    Serial.print(F("(PAUSED) "));
  }
  
  Serial.print(message);
  if (dataValue != -1) {
    Serial.print(F(" "));
    Serial.print(dataValue);
  }
  if (cyclesVal != -1) {
    Serial.print(F(" | Cycles Remaining: "));
    Serial.print(cyclesVal);
  }
  if (cycleIntervalVal != -1) {
    Serial.print(F(" | Cycle Interval: "));
    Serial.print(cycleIntervalVal);
    Serial.print(F(" min"));
  }
  Serial.println();
}

void setup() {
  Serial.begin(74880);
  
  if (debugLevel >= 1) {
    Serial.println(F("========================================"));
    Serial.println(F("Sourdough Controller Program Started"));
    Serial.print(F("Mode: "));
    Serial.println(testRun ? F("TEST RUN (Time Compression Active)") : F("PRODUCTION (Real-time Active)"));
    Serial.println(F("========================================"));
  }

  // --- REUSE OPTIONS AT PROGRAM START (EEPROM READ) ---
  int storedKnead = EEPROM.read(EEPROM_ADDR_KNEAD);
  int storedDegas = EEPROM.read(EEPROM_ADDR_DEGAS);
  int storedProof = EEPROM.read(EEPROM_ADDR_PROOF);
  int storedBake  = EEPROM.read(EEPROM_ADDR_BAKE);

  if (storedKnead < kneadOptionsSize) idxKnead = storedKnead;
  if (storedDegas < degasOptionsSize) idxDegas = storedDegas;
  if (storedProof < proofOptionsSize) idxProof = storedProof;
  if (storedBake  < bakeOptionsSize)  idxBake  = storedBake;

  if (debugLevel >= 1) {
    Serial.print(F("Loaded Saved Configuration -> KneadIdx: ")); Serial.print(idxKnead);
    Serial.print(F(", DegasIdx: ")); Serial.print(idxDegas);
    Serial.print(F(", ProofIdx: ")); Serial.print(idxProof);
    Serial.print(F(", BakeIdx: ")); Serial.println(idxBake);
  }

  pinMode(PIN_BTN_MENU, INPUT_PULLUP);
  pinMode(PIN_BTN_UP, INPUT_PULLUP);
  pinMode(PIN_BTN_DOWN, INPUT_PULLUP);
  pinMode(PIN_BTN_START_RESET, INPUT_PULLUP);

  // Default to High-Impedance Inputs on startup
  pinMode(srvMenu, INPUT);
  pinMode(srvMinus, INPUT);
  pinMode(srvRunReset, INPUT);
  pinMode(srvColour, INPUT);

  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { 
    for(;;); 
  }
  
  display.setTextSize(1); 
  calculateTotalMin();
  updateDisplay();
}

void loop() {
  handleButtons();
  processCountdownClock();
  runSourdoughEngine();
}

// --- SERVO DRIVE PROTOCOLS ---
void shortPress(int pin, int numberOfTimes) {
    if (debugLevel >= 2) {
      Serial.print(F("..shortPress Pin:"));
      Serial.print(pin);
      Serial.print(F(","));
      Serial.print(numberOfTimes);
      Serial.println(F(" times"));
    }  
    for (int i = 0; i < numberOfTimes; i++) {
      digitalWrite(pin, LOW);
      delay(200);
      digitalWrite(pin, HIGH);
      delay(300);
    }
}

void longPress(int pin) {
    if (debugLevel >= 2) {
      Serial.print(F("*longPress Pin:"));
      Serial.println(pin);
    }  
    digitalWrite(pin, LOW);
    delay(2000);
    digitalWrite(pin, HIGH);
    delay(1000);
}

long getStageMinutes(int optionValue, bool isProof) {
  if (optionValue == -1) return 1;
  if (optionValue == 0)  return 0;
  if (isProof)           return (long)optionValue * 60;
  return (long)optionValue;
}

void calculateTotalMin() {
  totalMin = getStageMinutes(kneadOptions[idxKnead], false) + 
             getStageMinutes(degasOptions[idxDegas], false) + 
             getStageMinutes(proofOptions[idxProof], true)  + 
             getStageMinutes(bakeOptions[idxBake], false);
}

void processCountdownClock() {
  if (currentState != IDLE && currentState != FINISHED && !isPaused) {
    unsigned long timeThreshold = testRun ? (1000UL / 60UL) : 1000UL; 
    
    if (millis() - oneSecondClock >= timeThreshold) {
      oneSecondClock += timeThreshold;
      
      if (remainingSec > 0) {
        remainingSec--;
      } else if (remainingMin > 0) {
        remainingMin--;
        remainingSec = 59;
      }
    }
  }
}

void cycleMenu(bool forward) {
  currentMenuLine = forward ? (currentMenuLine + 1) % 4 : (currentMenuLine - 1 + 4) % 4;
}

// --- INPUT & ADJUSTMENT REGISTRATION ---
void handleButtons() {
  unsigned long now = millis();
  
  bool btnMenu = digitalRead(PIN_BTN_MENU);
  if (btnMenu == LOW && lastBtnMenuState == HIGH && currentState == IDLE) {
    cycleMenu(true);
    updateDisplay();
    delay(200);
  }
  lastBtnMenuState = btnMenu;

  bool btnUp = digitalRead(PIN_BTN_UP);
  if (btnUp == LOW && lastBtnUpState == HIGH && currentState == IDLE) {
    if (currentMenuLine == 0) { idxKnead = (idxKnead + 1) % kneadOptionsSize; }
    if (currentMenuLine == 1) { idxDegas = (idxDegas + 1) % degasOptionsSize; }
    if (currentMenuLine == 2) { idxProof = (idxProof + 1) % proofOptionsSize; }
    if (currentMenuLine == 3) { idxBake  = (idxBake + 1) % bakeOptionsSize;   }
    
    calculateTotalMin();
    updateDisplay();
    delay(200);
  }
  lastBtnUpState = btnUp;

  bool btnDown = digitalRead(PIN_BTN_DOWN);
  if (btnDown == LOW && lastBtnDownState == HIGH && currentState == IDLE) {
    if (currentMenuLine == 0) { idxKnead = (idxKnead - 1 + kneadOptionsSize) % kneadOptionsSize; }
    if (currentMenuLine == 1) { idxDegas = (idxDegas - 1 + degasOptionsSize) % degasOptionsSize; }
    if (currentMenuLine == 2) { idxProof = (idxProof - 1 + proofOptionsSize) % proofOptionsSize; }
    if (currentMenuLine == 3) { idxBake  = (idxBake - 1 + bakeOptionsSize) % bakeOptionsSize;   }
    
    calculateTotalMin();
    updateDisplay();
    delay(200);
  }
  lastBtnDownState = btnDown;

  bool btnRunReset = digitalRead(PIN_BTN_START_RESET);
  if (btnRunReset == LOW) {
    if (!runResetWasPressed) {
      runResetPressTime = now;
      runResetWasPressed = true;
    } else if (now - runResetPressTime >= 1000) {
      display.clearDisplay();
      display.setCursor(0, 0);
      display.print(F("System Rebooting..."));
      display.display();
      delay(2000); 
      resetFunc(); 
    }
  } else {
    if (runResetWasPressed) {
      unsigned long pressDuration = now - runResetPressTime;
      if (pressDuration < 500) { 
        if (currentState == IDLE) {
          
          EEPROM.update(EEPROM_ADDR_KNEAD, idxKnead);
          EEPROM.update(EEPROM_ADDR_DEGAS, idxDegas);
          EEPROM.update(EEPROM_ADDR_PROOF, idxProof);
          EEPROM.update(EEPROM_ADDR_BAKE, idxBake);
          
          if (debugLevel >= 1) {
            Serial.println(F("[EEPROM] Configuration saved securely."));
            Serial.println(F("========================================"));
            Serial.println(F("    COOKING STARTED - ACTIVE PROFILE    "));
            Serial.println(F("========================================"));
            Serial.print(F(" Knead Time : ")); Serial.print(getStageMinutes(kneadOptions[idxKnead], false)); Serial.println(F(" min"));
            Serial.print(F(" Degas Time : ")); Serial.print(getStageMinutes(degasOptions[idxDegas], false)); Serial.println(F(" min"));
            Serial.print(F(" Proof Time : ")); Serial.print(getStageMinutes(proofOptions[idxProof], true));  Serial.println(F(" min"));
            Serial.print(F(" Bake Time  : ")); Serial.print(getStageMinutes(bakeOptions[idxBake], false));  Serial.println(F(" min"));
            Serial.print(F(" Total Time : ")); Serial.print(totalMin);                                      Serial.println(F(" min"));
            Serial.println(F("========================================"));
          }

          // --- INITIALIZE ACTUATOR PINS IMMEDIATELY AT COOKING START ---
          pinMode(srvMenu, OUTPUT); 
          pinMode(srvMinus, OUTPUT); 
          pinMode(srvRunReset, OUTPUT); 
          pinMode(srvColour, OUTPUT);
          
          digitalWrite(srvMenu, HIGH); 
          digitalWrite(srvMinus, HIGH); 
          digitalWrite(srvRunReset, HIGH); 
          digitalWrite(srvColour, HIGH);

          // --- EDITED PER SPECIFICATION: ACTIONS REMOVED FROM HERE ---

          remainingMin = totalMin;
          remainingSec = 0;
          oneSecondClock = now;
          currentState = KNEADING;
          
          long kMin = getStageMinutes(kneadOptions[idxKnead], false);
          cyclesRemaining = (kMin == 1) ? 1 : (kMin + 24) / 25; 
          stepInitialized = false;
          currentMenuLine = 0; 
          isPaused = false;
        } else if (currentState == FINISHED) {
          currentState = IDLE;
          calculateTotalMin();
          updateDisplay();
        } else {
          if (!isPaused) {
            isPaused = true;
            pauseStartTime = now;
            if (debugLevel >= 1) serialLog(F("Process PAUSED"));
          } else {
            unsigned long pauseDuration = now - pauseStartTime;
            stateStartTime += pauseDuration;
            oneSecondClock += pauseDuration;
            isPaused = false;
            if (debugLevel >= 1) serialLog(F("Process RESUMED"));
          }
        }
      }
      runResetWasPressed = false;
    }
  }
}

void printFormatTime(long totalMinutes, int totalSeconds, bool displaySeconds) {
  long hh = totalMinutes / 60;
  long mm = totalMinutes % 60;
  if (hh < 10) display.print(F("0"));
  display.print(hh);
  display.print(F(":"));
  if (mm < 10) display.print(F("0"));
  display.print(mm);
  
  if (displaySeconds) {
    display.print(F(":"));
    if (totalSeconds < 10) display.print(F("0"));
    display.print(totalSeconds);
  }
}

void updateDisplay() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  
  if (currentState == FINISHED) {
    display.setTextSize(2); 
    display.print(F("Sourdough\nDone"));
    display.setTextSize(1); 
    display.display();
    return; 
  } else {
    if (currentState == IDLE) {
      display.print(F("Sourdough"));
      display.setCursor(80, 0); 
      printFormatTime(totalMin, 0, false);
    } else {
      display.print(isPaused && ((millis() / 500) % 2 == 0) ? F("PAUSED") : F("Cooking"));
      display.setCursor(70, 0); 
      printFormatTime(remainingMin, remainingSec, true);
    }
  }

  if (currentState != IDLE && currentState != FINISHED) {
    const char spinIcons[] = {'/', '-', '\\', '|'};
    display.setCursor(118, 0);
    display.print(spinIcons[(millis() / 250) % 4]);
  }

  renderMenuRow(12, F("KneadMin: "), kneadOptions[idxKnead], (currentState == KNEADING), 0, false);
  renderMenuRow(24, F("DegasMin: "), degasOptions[idxDegas], (currentState == DEGAS), 1, false);
  renderMenuRow(36, F("ProofHr:  "), proofOptions[idxProof], (currentState == PROOF), 2, true);
  renderMenuRow(48, F("BakeMin:  "), bakeOptions[idxBake], (currentState == BAKE), 3, false);

  display.display();
}

void runSourdoughEngine() {
  if (currentState == IDLE || currentState == FINISHED) return;
  
  if (isPaused) {
    static unsigned long lastPauseRefresh = 0;
    if (millis() - lastPauseRefresh >= 250) {
      updateDisplay();
      lastPauseRefresh = millis();
    }
    return; 
  }

  unsigned long currentMillis = millis();
  static unsigned long lastInterfaceRefresh = 0;
  if (currentMillis - lastInterfaceRefresh >= 100) {
    updateDisplay();
    lastInterfaceRefresh = currentMillis;
  }

  long activeStageRemaining = 0;

  switch (currentState) {
    case KNEADING:
      if (kneadOptions[idxKnead] == 0) {
        if (debugLevel >= 1) serialLog(F("Kneading bypassed (Set to 0)"));
        currentState = DEGAS;
        long dMin = getStageMinutes(degasOptions[idxDegas], false);
        cyclesRemaining = (dMin == 1) ? 1 : (dMin + 24) / 25; 
        stepInitialized = false;
        break;
      }

      if (!stepInitialized) {
        long dMin = getStageMinutes(degasOptions[idxDegas], false);
        long pMin = getStageMinutes(proofOptions[idxProof], true);
        long bMin = getStageMinutes(bakeOptions[idxBake], false);
        activeStageRemaining = remainingMin - (dMin + pMin + bMin);
        
        if (activeStageRemaining <= 0) {
          long targetKnead = getStageMinutes(kneadOptions[idxKnead], false);
          activeStageRemaining = (targetKnead == 1) ? 1 : ((targetKnead % 25 == 0) ? 25 : (targetKnead % 25));
        }
        if (activeStageRemaining > 25) activeStageRemaining = 25;

        if (debugLevel >= 1) serialLog(F("Kneading block initialized"), -1, cyclesRemaining, activeStageRemaining);
        
        updateDisplay();
        
        // --- ADDED SHORTPRESS DIRECTLY BEFORE LONGPRESS ---
        shortPress(srvRunReset, 1);
        longPress(srvRunReset);
        
        shortPress(srvMenu, 7);
        shortPress(srvMinus, 10);
        shortPress(srvRunReset, 1); 
          
        stateStartTime = currentMillis;
        stepInterval = testRun ? ((unsigned long)activeStageRemaining * 1000UL) : ((unsigned long)activeStageRemaining * 60UL * 1000UL); 
        stepInitialized = true;
      }
      
      if (currentMillis - stateStartTime >= stepInterval) {
        longPress(srvRunReset);
        cyclesRemaining--;
        
        long dMin = getStageMinutes(degasOptions[idxDegas], false);
        long pMin = getStageMinutes(proofOptions[idxProof], true);
        long bMin = getStageMinutes(bakeOptions[idxBake], false);
        activeStageRemaining = remainingMin - (dMin + pMin + bMin);

        if (cyclesRemaining > 0 && activeStageRemaining > 0) {
          stepInitialized = false;
        } else {
          currentState = DEGAS;
          long dMinNext = getStageMinutes(degasOptions[idxDegas], false);
          cyclesRemaining = (dMinNext == 1) ? 1 : (dMinNext + 24) / 25; 
          stepInitialized = false;
        }
      }
      break;

    case DEGAS:
      if (degasOptions[idxDegas] == 0) {
        if (debugLevel >= 1) serialLog(F("Degas bypassed (Set to 0)"));
        currentState = PROOF;
        long pMin = getStageMinutes(proofOptions[idxProof], true);
        cyclesRemaining = (pMin == 1) ? 1 : (pMin + 59) / 60;
        stepInitialized = false;
        break;
      }

      if (!stepInitialized) {
        long pMin = getStageMinutes(proofOptions[idxProof], true);
        long bMin = getStageMinutes(bakeOptions[idxBake], false);
        activeStageRemaining = remainingMin - (pMin + bMin);
        
        if (activeStageRemaining <= 0) {
          long targetDegas = getStageMinutes(degasOptions[idxDegas], false);
          activeStageRemaining = (targetDegas == 1) ? 1 : ((targetDegas % 25 == 0) ? 25 : (targetDegas % 25));
        }
        if (activeStageRemaining > 25) activeStageRemaining = 25;

        if (debugLevel >= 1) serialLog(F("Degas block initialized"), -1, cyclesRemaining, activeStageRemaining);
        
        updateDisplay();
        
        // --- ADDED SHORTPRESS DIRECTLY BEFORE LONGPRESS ---
        shortPress(srvRunReset, 1);
        longPress(srvRunReset);
         
        shortPress(srvMenu, 7);
        shortPress(srvMinus, 10);
        shortPress(srvRunReset, 1); 
        delay(10000); 
        longPress(srvRunReset);
        
        stateStartTime = millis(); 
        stepInterval = testRun ? ((unsigned long)activeStageRemaining * 1000UL) : ((unsigned long)activeStageRemaining * 60UL * 1000UL);
        stepInitialized = true;
      }
      
      if (millis() - stateStartTime >= stepInterval) {
        cyclesRemaining--;
        long pMin = getStageMinutes(proofOptions[idxProof], true);
        long bMin = getStageMinutes(bakeOptions[idxBake], false);
        activeStageRemaining = remainingMin - (pMin + bMin);

        if (cyclesRemaining > 0 && activeStageRemaining > 0) {
          stepInitialized = false;
        } else {
          currentState = PROOF;
          long pMinNext = getStageMinutes(proofOptions[idxProof], true);
          cyclesRemaining = (pMinNext == 1) ? 1 : (pMinNext + 59) / 60;
          stepInitialized = false;
        }
      }
      break;

    case PROOF:
      if (proofOptions[idxProof] == 0) {
        if (debugLevel >= 1) serialLog(F("Proof bypassed (Set to 0)"));
        currentState = BAKE;
        long bMin = getStageMinutes(bakeOptions[idxBake], false);
        cyclesRemaining = (bMin == 1) ? 1 : (bMin + 49) / 50; 
        stepInitialized = false;
        break;
      }

      if (!stepInitialized) {
        long bMin = getStageMinutes(bakeOptions[idxBake], false);
        activeStageRemaining = remainingMin - bMin;
        
        if (activeStageRemaining <= 0) {
          long targetProof = getStageMinutes(proofOptions[idxProof], true);
          activeStageRemaining = (targetProof == 1) ? 1 : ((targetProof % 60 == 0) ? 60 : (targetProof % 60));
        }
        if (activeStageRemaining > 60) activeStageRemaining = 60;

        if (debugLevel >= 1) serialLog(F("Proof block initialized"), -1, cyclesRemaining, activeStageRemaining);
        
        updateDisplay();
        
        // --- REMOVED LONGPRESS CONDITION FROM HERE PER SPECIFICATION ---
        
        stateStartTime = currentMillis;
        stepInterval = testRun ? ((unsigned long)activeStageRemaining * 1000UL) : ((unsigned long)activeStageRemaining * 60UL * 1000UL);
        stepInitialized = true;
      }
      
      if (currentMillis - stateStartTime >= stepInterval) {
        cyclesRemaining--;
        long bMin = getStageMinutes(bakeOptions[idxBake], false);
        activeStageRemaining = remainingMin - bMin;

        if (cyclesRemaining > 0 && activeStageRemaining > 0) {
          stepInitialized = false;
        } else {
          currentState = BAKE;
          long bMinNext = getStageMinutes(bakeOptions[idxBake], false);
          cyclesRemaining = (bMinNext == 1) ? 1 : (bMinNext + 49) / 50; 
          stepInitialized = false;
        }
      }
      break;

    case BAKE:
      if (bakeOptions[idxBake] == 0) {
        if (debugLevel >= 1) serialLog(F("Bake bypassed (Set to 0). Cooking Complete."));
        currentState = FINISHED;
        stepInitialized = false;
        break;
      }

      if (!stepInitialized) {
        long currentBakeInterval = remainingMin;
        
        if (currentBakeInterval <= 0) currentBakeInterval = 1;
        if (currentBakeInterval > 50) currentBakeInterval = 50; 
        
        if (debugLevel >= 1) serialLog(F("Bake block initialized"), -1, cyclesRemaining, currentBakeInterval);
        
        updateDisplay();
        
        // --- REMOVED THE FIRST LONGPRESS FROM HERE PER SPECIFICATION ---
        shortPress(srvMenu, 13);
        
        shortPress(srvColour, 1); 
        shortPress(srvMinus, 10);
        
        shortPress(srvRunReset, 1); 
        
        stateStartTime = currentMillis;
        stepInterval = testRun ? ((unsigned long)currentBakeInterval * 1000UL) : ((unsigned long)currentBakeInterval * 60UL * 1000UL);
        stepInitialized = true;
      }
      
      if (currentMillis - stateStartTime >= stepInterval) {
        longPress(srvRunReset);
        cyclesRemaining--;
        
        if (cyclesRemaining > 0 && remainingMin > 0) {
          stepInitialized = false;
        } else {
          currentState = FINISHED;
          stepInitialized = false;
          if (debugLevel >= 1) {
            serialLog(F("--> ALL COOKING STAGES COMPLETE! <--"));
          }
          updateDisplay(); 
        }
      }
      break;
  }
}

void renderMenuRow(int yPos, const __FlashStringHelper* textLabel, int numericValue, bool isActiveStage, int targetRowID, bool isProof) {
  display.setCursor(0, yPos);
  
  if (currentState == IDLE) {
    if (currentMenuLine == targetRowID) {
      display.fillRect(0, yPos - 1, SCREEN_WIDTH, 11, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
    } else {
      display.setTextColor(SSD1306_WHITE);
    }
    display.print(textLabel);
    if (numericValue == -1) display.print(F("1m(Test)"));
    else if (numericValue == 0) display.print(F("SKIP"));
    else display.print(numericValue);
  } else {
    display.setTextColor(SSD1306_WHITE);
    if (isActiveStage) {
      long dynamicCycleMin = 0;
      if (currentState == BAKE) {
        dynamicCycleMin = remainingMin;
        if (remainingSec > 0) dynamicCycleMin++;
      } else {
        long targetVal = isProof ? proofOptions[idxProof] : (currentState == KNEADING ? kneadOptions[idxKnead] : degasOptions[idxDegas]);
        if (targetVal == -1) {
          dynamicCycleMin = 1;
        } else {
          long standardBlockSize = isProof ? 60 : 25; 
          long staticBlocksMin = (long)(cyclesRemaining - 1) * standardBlockSize;
          long remainingModOffset = remainingMin % standardBlockSize;
          if (remainingModOffset == 0 && remainingMin > 0) {
            remainingModOffset = standardBlockSize;
            if (staticBlocksMin >= standardBlockSize) staticBlocksMin -= standardBlockSize;
          }
          dynamicCycleMin = staticBlocksMin + remainingModOffset;
          if (remainingSec > 0 && (remainingMin % standardBlockSize != 0)) dynamicCycleMin++;
        }
      }
      display.print(F("*")); display.print(textLabel); display.print(F("C:")); display.print(cyclesRemaining); display.print(F(" M:")); display.print((int)dynamicCycleMin);
    } else {
      display.print(F(" ")); display.print(textLabel);
      if (numericValue == -1) display.print(F("1m"));
      else if (numericValue == 0) display.print(F("SKIP"));
      else display.print(numericValue);
    }
  }
}