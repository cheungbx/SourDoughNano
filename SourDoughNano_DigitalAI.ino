#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

const int debugLevel = 2;

// --- TEST / PRODUCTION CONFIGURATION ---
// Set to true for 1 min = 1 sec testing. Set to false for 1 min = 1 min actual baking.
const bool testRun = false; 

// --- PIN ASSIGNMENTS ---
const int PIN_BTN_MENU        = 4;
const int PIN_BTN_UP          = 5;
const int PIN_BTN_DOWN        = 6;
const int PIN_BTN_START_RESET = 7;
const int srvMenu             = 9;  // Purple Wire on bread machine
const int srvMinus            = 10; // Yellow Wire on bread machine
const int srvRunReset         = 11; // Orange Wire on bread machine

// --- ARRAYS FOR PARAMETER OPTIONS ---
const int kneadOptions[] = {1, 30, 60, 90};
const int degasOptions[] = {1, 30, 60, 90, 120, 150, 180};
const int proofOptions[] = {1, 3, 6, 9, 12, 15, 18, 21, 24};
const int bakeOptions[]  = {1, 60, 70, 80, 90, 100, 110, 120};    

int kneadOptionsSize = sizeof(kneadOptions) / sizeof(kneadOptions[0]);
int degasOptionsSize = sizeof(degasOptions) / sizeof(degasOptions[0]);
int proofOptionsSize = sizeof(proofOptions) / sizeof(proofOptions[0]);
int bakeOptionsSize = sizeof(bakeOptions)   / sizeof(bakeOptions[0]);

// Default initial state indices (30, 90, 12, 90)
int idxKnead = 1; 
int idxDegas = 3; 
int idxProof = 6;
int idxBake  = 4; 

int currentMenuLine = 0; 
long totalMin = 0;
long remainingMin = 0;

// --- STATE MACHINE ENUMS ---
enum ProcessState { IDLE, KNEADING, DEGAS, PROOF, BAKE_60, BAKE_REMAINING, FINISHED };
ProcessState currentState = IDLE;
bool isPaused = false;

// --- TIMERS & RUNTIME COUNTERS ---
unsigned long stateStartTime = 0;
unsigned long stepInterval = 0;
unsigned long countdownClock = 0;
int cyclesRemaining = 0;
bool stepInitialized = false;

// --- BUTTON DEBOUNCE TRACKING ---
bool lastBtnMenuState = HIGH;
bool lastBtnUpState = HIGH;
bool lastBtnDownState = HIGH;
bool runResetWasPressed = false;
unsigned long runResetPressTime = 0;

// --- REBOOT POINTER ---
void (* resetFunc) (void) = 0;

// --- SERIAL LOGGING ENGINE (MEMORY OPTIMIZED) ---
void serialLog(const __FlashStringHelper* message, long dataValue = -1) {
  if (currentState != IDLE && currentState != FINISHED) {
    long hh = remainingMin / 60;
    long mm = remainingMin % 60;
    
    Serial.print(F("["));
    if (hh < 10) Serial.print(F("0"));
    Serial.print(hh);
    Serial.print(F(":"));
    if (mm < 10) Serial.print(F("0"));
    Serial.print(mm);
    Serial.print(F("] "));
    if (isPaused) {
      Serial.print(F("(PAUSED) "));
    }
  } else {
    Serial.print(F("[--:--] "));
  }
  Serial.print(message);
  if (dataValue != -1) {
    Serial.print(F(" "));
    Serial.print(dataValue);
  }
  Serial.println();
}

void setup() {
  Serial.begin(74880);

  pinMode(PIN_BTN_MENU, INPUT_PULLUP);
  pinMode(PIN_BTN_UP, INPUT_PULLUP);
  pinMode(PIN_BTN_DOWN, INPUT_PULLUP);
  pinMode(PIN_BTN_START_RESET, INPUT_PULLUP);

  pinMode(srvMenu, INPUT);
  pinMode(srvMinus, INPUT);
  pinMode(srvRunReset, INPUT);

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

// --- BUTTON PIN PRESS SIMULATION ENGINE ---
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
    delay(200);
    digitalWrite(pin, HIGH);
    delay(300);
    digitalWrite(pin, LOW);
    delay(2000);
    digitalWrite(pin, HIGH);
    delay(1000);
}

void calculateTotalMin() {
  totalMin = (long)kneadOptions[idxKnead] + (long)degasOptions[idxDegas] + ((long)proofOptions[idxProof] * 60) + (long)bakeOptions[idxBake];
}

void processCountdownClock() {
  // Lock countdown updates while execution is paused
  if (currentState != IDLE && currentState != FINISHED && !isPaused) {
    unsigned long interval = testRun ? 1000UL : 60000UL; 
    if (millis() - countdownClock >= interval) {
      countdownClock += interval;
      if (remainingMin > 0) {
        remainingMin--;
      }
    }
  }
}

int getLockedMenuLine() {
  if (currentState == KNEADING) return 0;
  if (currentState == DEGAS)    return 1;
  if (currentState == PROOF)    return 2;
  if (currentState == BAKE_60 || currentState == BAKE_REMAINING) return 3;
  return -1;
}

void cycleMenu(bool forward) {
  int locked = getLockedMenuLine();
  for (int i = 0; i < 4; i++) {
    if (forward) {
      currentMenuLine = (currentMenuLine + 1) % 4;
    } else {
      currentMenuLine = (currentMenuLine - 1 + 4) % 4;
    }
    if (currentMenuLine != locked) break; 
  }
}

// --- INPUT & ADJUSTMENT REGISTRATION ---
void handleButtons() {
  unsigned long now = millis();
  int locked = getLockedMenuLine();
  
  // Disable menu selections when program is active, whether paused or not
  bool menuLocked = (currentState != IDLE && currentState != FINISHED);

  bool btnMenu = digitalRead(PIN_BTN_MENU);
  if (btnMenu == LOW && lastBtnMenuState == HIGH && !menuLocked) {
    cycleMenu(true);
    updateDisplay();
    delay(200);
  }
  lastBtnMenuState = btnMenu;

  bool btnUp = digitalRead(PIN_BTN_UP);
  if (btnUp == LOW && lastBtnUpState == HIGH && !menuLocked) {
    if (currentMenuLine != locked) {
      long oldVal = 0, newVal = 0;
      if (currentMenuLine == 0) { oldVal = kneadOptions[idxKnead]; idxKnead = (idxKnead + 1) % kneadOptionsSize; newVal = kneadOptions[idxKnead]; }
      if (currentMenuLine == 1) { oldVal = degasOptions[idxDegas]; idxDegas = (idxDegas + 1) % degasOptionsSize; newVal = degasOptions[idxDegas]; }
      if (currentMenuLine == 2) { oldVal = proofOptions[idxProof] * 60; idxProof = (idxProof + 1) % proofOptionsSize; newVal = proofOptions[idxProof] * 60; }
      if (currentMenuLine == 3) { oldVal = bakeOptions[idxBake]; idxBake  = (idxBake + 1) % bakeOptionsSize;  newVal = bakeOptions[idxBake]; }
      
      calculateTotalMin();
      updateDisplay();
      delay(200);
    }
  }
  lastBtnUpState = btnUp;

  bool btnDown = digitalRead(PIN_BTN_DOWN);
  if (btnDown == LOW && lastBtnDownState == HIGH && !menuLocked) {
    if (currentMenuLine != locked) {
      long oldVal = 0, newVal = 0;
      if (currentMenuLine == 0) { oldVal = kneadOptions[idxKnead]; idxKnead = (idxKnead - 1 + kneadOptionsSize) % kneadOptionsSize; newVal = kneadOptions[idxKnead]; }
      if (currentMenuLine == 1) { oldVal = degasOptions[idxDegas]; idxDegas = (idxDegas - 1 + degasOptionsSize) % degasOptionsSize; newVal = degasOptions[idxDegas]; }
      if (currentMenuLine == 2) { oldVal = proofOptions[idxProof] * 60; idxProof = (idxProof - 1 + proofOptionsSize) % proofOptionsSize; newVal = proofOptions[idxProof] * 60; }
      if (currentMenuLine == 3) { oldVal = bakeOptions[idxBake]; idxBake  = (idxBake - 1 + bakeOptionsSize) % bakeOptionsSize;  newVal = bakeOptions[idxBake]; }
      
      calculateTotalMin();
      updateDisplay();
      delay(200);
    }
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
      
      if (pressDuration < 500) { // Short press (Less than 0.5s)
        if (currentState == IDLE) {
          // If starting fresh from IDLE
          remainingMin = totalMin;
          countdownClock = now;
          currentState = KNEADING;
          cyclesRemaining = kneadOptions[idxKnead] / 30;
          stepInitialized = false;
          currentMenuLine = 0; 
          isPaused = false;
          if (debugLevel >= 1) {
            serialLog(F("Cooking started Total Min:"), totalMin);
          }
        } else if (currentState != FINISHED) {
          // Toggle Pause / Resume mid-process
          isPaused = !isPaused;
          if (debugLevel >= 1) {
            if (isPaused) serialLog(F("Process PAUSED"));
            else serialLog(F("Process RESUMED"));
          }
        }
      }
      runResetWasPressed = false;
    }
  }
}

void printFormatTime(long totalMinutes) {
  long hh = totalMinutes / 60;
  long mm = totalMinutes % 60;
  if (hh < 10) display.print(F("0"));
  display.print(hh);
  display.print(F(":"));
  if (mm < 10) display.print(F("0"));
  display.print(mm);
}

// --- DISPLAY GRAPHICS ENGINE ---
void updateDisplay() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  if (currentState == FINISHED) {
    display.print(F("Sourdough Done"));
  } else {
    if (currentState == IDLE) {
      display.print(F("Sourdough"));
      display.setCursor(80, 0);
      printFormatTime(totalMin);
    } else {
      if (isPaused && ((millis() / 500) % 2 == 0)) {
        display.print(F("PAUSED")); // Flash PAUSED banner when paused
      } else {
        display.print(F("Cooking"));
      }
      display.setCursor(80, 0);
      printFormatTime(remainingMin);
    }
  }

  if (currentState != IDLE && currentState != FINISHED && !isPaused) {
    const char spinIcons[] = {'/', '-', '\\', '|'};
    int iconIndex = (millis() / 250) % 4;
    display.setCursor(118, 0);
    display.print(spinIcons[iconIndex]);
  }

  int locked = getLockedMenuLine();
  bool blink = (millis() / 400) % 2 == 0;
  renderMenuRow(12, F("KneadMin: "), kneadOptions[idxKnead], (locked == 0) ? blink : (currentMenuLine == 0));
  renderMenuRow(24, F("DegasMin: "), degasOptions[idxDegas], (locked == 1) ? blink : (currentMenuLine == 1));
  renderMenuRow(36, F("ProofHr:  "), proofOptions[idxProof],  (locked == 2) ? blink : (currentMenuLine == 2));
  renderMenuRow(48, F("BakeMin:  "), bakeOptions[idxBake],   (locked == 3) ? blink : (currentMenuLine == 3));

  display.display();
}

// --- PROCESSING SEQUENCE CONTROL ENGINE ---
void runSourdoughEngine() {
  if (currentState == IDLE || currentState == FINISHED) return;
  
  unsigned long currentMillis = millis();
  
  // If system is paused, continually offset trackers to lock execution time in place
  if (isPaused) {
    if (stepInitialized) {
      stateStartTime++; 
    }
    countdownClock++;
    
    static unsigned long lastPauseRefresh = 0;
    if (currentMillis - lastPauseRefresh >= 250) {
      updateDisplay();
      lastPauseRefresh = currentMillis;
    }
    return; 
  }

  static unsigned long lastInterfaceRefresh = 0;
  if (currentMillis - lastInterfaceRefresh >= 100) {
    updateDisplay();
    lastInterfaceRefresh = currentMillis;
  }

  switch (currentState) {
    case KNEADING:
      if (!stepInitialized) {
        if (debugLevel >= 1) { serialLog(F("Kneading"), kneadOptions[idxKnead]); }
        pinMode(srvMenu, OUTPUT);
        pinMode(srvMinus, OUTPUT);
        pinMode(srvRunReset, OUTPUT);
        digitalWrite(srvMenu, HIGH);
        digitalWrite(srvMinus, HIGH);        
        digitalWrite(srvRunReset, HIGH);

        updateDisplay();
        longPress(srvRunReset);
        shortPress(srvMenu, 7);
        shortPress(srvMinus, 10);
        shortPress(srvRunReset, 1); 
        
        stateStartTime = currentMillis;
        stepInterval = testRun ? (30UL * 1000UL) : (30UL * 60UL * 1000UL); 
        stepInitialized = true;
      }
      
      if (currentMillis - stateStartTime >= stepInterval) {
        longPress(srvRunReset);
        cyclesRemaining--;
        if (cyclesRemaining > 0) {
          stepInitialized = false;
        } else {
          currentState = DEGAS;
          cyclesRemaining = degasOptions[idxDegas] / 30;
          stepInitialized = false;
          cycleMenu(true); 
        }
      }
      break;

    case DEGAS:
      if (!stepInitialized) {
        if (debugLevel >= 1) { serialLog(F("Degas"), degasOptions[idxDegas]); }
        updateDisplay();
        longPress(srvRunReset); 
        shortPress(srvMenu, 7);
        shortPress(srvMinus, 10);
        shortPress(srvRunReset, 1); 
        
        stateStartTime = currentMillis;
        stepInterval = testRun ? (60UL * 1000UL) : (60UL * 60UL * 1000UL);
        stepInitialized = true;
      }
      
      if (currentMillis - stateStartTime >= stepInterval) {
        longPress(srvRunReset);
        cyclesRemaining--;
        if (cyclesRemaining > 0) {
          stepInitialized = false;
        } else {
          currentState = PROOF;
          cyclesRemaining = proofOptions[idxProof] * 2;
          stepInitialized = false;
          cycleMenu(true);
        }
      }
      break;

    case PROOF:
      if (!stepInitialized) {
        if (debugLevel >= 1) { serialLog(F("Proof"), proofOptions[idxProof]); }
        updateDisplay();
        if (cyclesRemaining == (proofOptions[idxProof] * 2)) {
          longPress(srvRunReset);
        }
        stateStartTime = currentMillis;
        stepInterval = testRun ? (60UL * 1000UL) : (30UL * 60UL * 1000UL);
        stepInitialized = true;
      }
      
      if (currentMillis - stateStartTime >= stepInterval) {
        cyclesRemaining--;
        if (cyclesRemaining > 0) {
          stepInitialized = false;
        } else {
          currentState = BAKE_60;
          stepInitialized = false;
          cycleMenu(true);
        }
      }
      break;

    case BAKE_60:
      if (!stepInitialized) {
        if (debugLevel >= 1) { serialLog(F("Bake"), bakeOptions[idxBake]); }
        updateDisplay();
        longPress(srvRunReset); 
        shortPress(srvMenu, 13);
        shortPress(srvMinus, 10);
        shortPress(srvRunReset, 1); 
        
        stateStartTime = currentMillis;
        stepInterval = testRun ? (60UL * 1000UL) : (60UL * 60UL * 1000UL);
        stepInitialized = true;
      }
      
      if (currentMillis - stateStartTime >= stepInterval) {
        int remainingBakeTime = bakeOptions[idxBake] - 60;
        if (remainingBakeTime > 0) {
          currentState = BAKE_REMAINING;
          cyclesRemaining = remainingBakeTime;
          stepInitialized = false;
        } else {
          longPress(srvRunReset); 
          currentState = FINISHED;
          stepInitialized = false;
        }
      }
      break;

    case BAKE_REMAINING:
      if (!stepInitialized) {
        updateDisplay();
        longPress(srvRunReset); 
        shortPress(srvMenu, 13);
        shortPress(srvMinus, 10);
        shortPress(srvRunReset, 1); 
        
        stateStartTime = currentMillis;
        stepInterval = testRun ? ((unsigned long)cyclesRemaining * 1000UL) : ((unsigned long)cyclesRemaining * 60UL * 1000UL);
        stepInitialized = true;
      }
      
      if (currentMillis - stateStartTime >= stepInterval) {
        longPress(srvRunReset);
        currentState = FINISHED;
        stepInitialized = false;
      }
      break;

    default:
      break;
  }

  if (currentState == FINISHED) {
    updateDisplay(); 
    delay(2000); 
    currentState = IDLE;
    calculateTotalMin();
    updateDisplay();
  }
}

void renderMenuRow(int yPos, const __FlashStringHelper* textLabel, int numericValue, bool invertedStyle) {
  if (invertedStyle) {
    display.fillRect(0, yPos - 1, SCREEN_WIDTH, 11, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK);
  } else {
    display.setTextColor(SSD1306_WHITE);
  }
  display.setCursor(0, yPos);
  display.print(textLabel);
  display.print(numericValue);
}