#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Servo.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET); // 

const int debugLevel = 2; // [cite: 2]

// --- TEST / PRODUCTION CONFIGURATION ---
// Set to true for 1 min = 1 sec testing. Set to false for 1 min = 1 min actual baking.
const bool testRun = false; 

// --- PIN ASSIGNMENTS ---
const int PIN_BTN_MENU        = 4; // [cite: 2]
const int PIN_BTN_UP          = 5; // [cite: 3]
const int PIN_BTN_DOWN        = 6; // [cite: 4]
const int PIN_BTN_START_RESET = 7; // [cite: 4]
const int srvMenu             = 9;  // Purple Wire on bread machine [cite: 5, 6]
const int srvMinus            = 10; // Yellow Wire on bread machine [cite: 6, 7]
const int srvRunReset         = 11; // Orange Wire on bread machine [cite: 7, 8]

// --- ARRAYS FOR PARAMETER OPTIONS ---
const int kneadOptions[] = {1, 30, 60, 90}; // [cite: 8]
const int degasOptions[] = {1, 30, 60, 90, 120, 150, 180}; // [cite: 9]
const int proofOptions[] = {1, 3, 6, 9, 12, 15, 18, 21, 24}; // [cite: 10]
const int bakeOptions[]  = {1, 60, 70, 80, 90, 100, 110, 120};    // [cite: 11]

int kneadOptionsSize = sizeof(kneadOptions) / sizeof(kneadOptions[0]); // [cite: 11]
int degasOptionsSize = sizeof(degasOptions) / sizeof(degasOptions[0]); // [cite: 12]
int proofOptionsSize = sizeof(proofOptions) / sizeof(proofOptions[0]); // [cite: 12]
int bakeOptionsSize = sizeof(bakeOptions)   / sizeof(bakeOptions[0]); // [cite: 12]

// Default initial state indices (30, 90, 12, 90)
int idxKnead = 1; // [cite: 13]
int idxDegas = 3; // [cite: 13]
int idxProof = 6; // [cite: 13]
int idxBake  = 4; // [cite: 14]

int currentMenuLine = 0; // [cite: 14]
long totalMin = 0; // [cite: 14]
long remainingMin = 0; // [cite: 14]

// --- STATE MACHINE ENUMS ---
enum ProcessState { IDLE, KNEADING, DEGAS, PROOF, BAKE_60, BAKE_REMAINING, FINISHED }; // [cite: 15]
ProcessState currentState = IDLE; // [cite: 15]

// --- TIMERS & RUNTIME COUNTERS ---
unsigned long stateStartTime = 0; // [cite: 16]
unsigned long stepInterval = 0; // [cite: 16]
unsigned long countdownClock = 0;
int cyclesRemaining = 0; // [cite: 17]
bool stepInitialized = false; // [cite: 17]

// --- BUTTON DEBOUNCE TRACKING ---
bool lastBtnMenuState = HIGH; // [cite: 18]
bool lastBtnUpState = HIGH; // [cite: 18]
bool lastBtnDownState = HIGH; // [cite: 18]
bool runResetWasPressed = false; // [cite: 19]
unsigned long runResetPressTime = 0; // [cite: 19]

// --- REBOOT POINTER ---
void (* resetFunc) (void) = 0; // [cite: 19]

// --- SERIAL LOGGING ENGINE (MEMORY OPTIMIZED) ---
// Prepend [HH:MM] while accepting Flash-based strings to save RAM
void serialLog(const __FlashStringHelper* message, long dataValue = -1) {
  if (currentState != IDLE && currentState != FINISHED) { // [cite: 27]
    long hh = remainingMin / 60; // [cite: 66]
    long mm = remainingMin % 60; // [cite: 66]
    
    Serial.print(F("["));
    if (hh < 10) Serial.print(F("0")); // [cite: 67]
    Serial.print(hh); // [cite: 67]
    Serial.print(F(":")); // [cite: 67]
    if (mm < 10) Serial.print(F("0")); // [cite: 67]
    Serial.print(mm); // [cite: 68]
    Serial.print(F("] "));
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
  Serial.begin(74880); // [cite: 20]

  pinMode(PIN_BTN_MENU, INPUT_PULLUP); // [cite: 20]
  pinMode(PIN_BTN_UP, INPUT_PULLUP); // [cite: 20]
  pinMode(PIN_BTN_DOWN, INPUT_PULLUP); // [cite: 20]
  pinMode(PIN_BTN_START_RESET, INPUT_PULLUP); // [cite: 20]

  pinMode(srvMenu, INPUT); // [cite: 20]
  pinMode(srvMinus, INPUT); // [cite: 20]
  pinMode(srvRunReset, INPUT); // [cite: 20]

  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // [cite: 21]
    for(;;); // [cite: 21]
  }
  
  display.setTextSize(1); // [cite: 21]
  calculateTotalMin(); // [cite: 21]
  updateDisplay(); // [cite: 21]
}

void loop() {
  handleButtons(); // [cite: 21]
  processCountdownClock(); // [cite: 22]
  runSourdoughEngine(); // [cite: 22]
}

// --- SERVO DRIVE PROTOCOLS ---
void shortPress(int pin, int numberOfTimes) {
    if (debugLevel >= 2) { // [cite: 22]
      Serial.print(F("..shortPress Pin:"));
      Serial.print(pin); // [cite: 23]
      Serial.print(F(","));
      Serial.print(numberOfTimes); // [cite: 23]
      Serial.println(F(" times"));
    }  
    for (int i = 0; i < numberOfTimes; i++) {
      digitalWrite(pin, LOW); // [cite: 23]
      delay(200); // [cite: 24]
      digitalWrite(pin, HIGH); // [cite: 24]
      delay(300); // [cite: 24]
    }
}

void longPress(int pin) {
    if (debugLevel >= 2) { // [cite: 24]
      Serial.print(F("*longPress Pin:"));
      Serial.println(pin); // [cite: 25]
    }  
    digitalWrite(pin, LOW); // [cite: 25]
    delay(200); // [cite: 25]
    digitalWrite(pin, HIGH); // [cite: 25]
    delay(300); // [cite: 25]
    digitalWrite(pin, LOW); // [cite: 25]
    delay(2000); // [cite: 25]
    digitalWrite(pin, HIGH); // [cite: 26]
    delay(1000); // [cite: 26]
}

void calculateTotalMin() {
  totalMin = (long)kneadOptions[idxKnead] + (long)degasOptions[idxDegas] + ((long)proofOptions[idxProof] * 60) + (long)bakeOptions[idxBake]; // [cite: 26]
}

// Handles dynamic decrement frequency based on the testRun flag
void processCountdownClock() {
  if (currentState != IDLE && currentState != FINISHED) { // [cite: 27]
    unsigned long interval = testRun ? 1000UL : 60000UL; // 1 second vs 1 minute [cite: 27]
    if (millis() - countdownClock >= interval) { // [cite: 27]
      countdownClock += interval; // [cite: 27]
      if (remainingMin > 0) { // [cite: 28]
        remainingMin--; // [cite: 28]
      }
    }
  }
}

int getLockedMenuLine() {
  if (currentState == KNEADING) return 0; // [cite: 29]
  if (currentState == DEGAS)    return 1; // [cite: 30]
  if (currentState == PROOF)    return 2; // [cite: 30]
  if (currentState == BAKE_60 || currentState == BAKE_REMAINING) return 3; // [cite: 31]
  return -1; // [cite: 31]
}

void cycleMenu(bool forward) {
  int locked = getLockedMenuLine(); // [cite: 32]
  for (int i = 0; i < 4; i++) { // [cite: 32]
    if (forward) { // [cite: 32]
      currentMenuLine = (currentMenuLine + 1) % 4; // [cite: 32]
    } else {
      currentMenuLine = (currentMenuLine - 1 + 4) % 4; // [cite: 33]
    }
    if (currentMenuLine != locked) break; // [cite: 34]
  }
}

// --- INPUT & ADJUSTMENT REGISTRATION ---
void handleButtons() {
  unsigned long now = millis(); // [cite: 34]
  int locked = getLockedMenuLine(); // [cite: 35]
  
  bool btnMenu = digitalRead(PIN_BTN_MENU); // [cite: 35]
  if (btnMenu == LOW && lastBtnMenuState == HIGH) { // [cite: 35]
    cycleMenu(true); // [cite: 35]
    updateDisplay(); // [cite: 36]
    delay(200); // [cite: 36]
  }
  lastBtnMenuState = btnMenu; // [cite: 36]

  bool btnUp = digitalRead(PIN_BTN_UP); // [cite: 36]
  if (btnUp == LOW && lastBtnUpState == HIGH) { // [cite: 37]
    if (currentMenuLine != locked) { // [cite: 37]
      long oldVal = 0, newVal = 0; // [cite: 37]
      if (currentMenuLine == 0) { oldVal = kneadOptions[idxKnead]; idxKnead = (idxKnead + 1) % kneadOptionsSize; newVal = kneadOptions[idxKnead]; } // [cite: 38]
      if (currentMenuLine == 1) { oldVal = degasOptions[idxDegas]; idxDegas = (idxDegas + 1) % degasOptionsSize; newVal = degasOptions[idxDegas]; } // [cite: 39, 40]
      if (currentMenuLine == 2) { oldVal = proofOptions[idxProof] * 60; idxProof = (idxProof + 1) % proofOptionsSize; newVal = proofOptions[idxProof] * 60; } // [cite: 41, 42]
      if (currentMenuLine == 3) { oldVal = bakeOptions[idxBake]; idxBake  = (idxBake + 1) % bakeOptionsSize;  newVal = bakeOptions[idxBake]; } // [cite: 43, 44]
      
      calculateTotalMin(); // [cite: 45]
      if (currentState != IDLE && currentState != FINISHED) { // [cite: 46]
        remainingMin += (newVal - oldVal); // [cite: 46]
        if (remainingMin < 0) remainingMin = 0; // [cite: 47]
      }
      updateDisplay(); // [cite: 47]
      delay(200); // [cite: 47]
    }
  }
  lastBtnUpState = btnUp; // [cite: 48]

  bool btnDown = digitalRead(PIN_BTN_DOWN); // [cite: 48]
  if (btnDown == LOW && lastBtnDownState == HIGH) { // [cite: 49]
    if (currentMenuLine != locked) { // [cite: 49]
      long oldVal = 0, newVal = 0; // [cite: 49]
      if (currentMenuLine == 0) { oldVal = kneadOptions[idxKnead]; idxKnead = (idxKnead - 1 + kneadOptionsSize) % kneadOptionsSize; newVal = kneadOptions[idxKnead]; } // [cite: 50]
      if (currentMenuLine == 1) { oldVal = degasOptions[idxDegas]; idxDegas = (idxDegas - 1 + degasOptionsSize) % degasOptionsSize; newVal = degasOptions[idxDegas]; } // [cite: 51, 52]
      if (currentMenuLine == 2) { oldVal = proofOptions[idxProof] * 60; idxProof = (idxProof - 1 + proofOptionsSize) % proofOptionsSize; newVal = proofOptions[idxProof] * 60; } // [cite: 53, 54]
      if (currentMenuLine == 3) { oldVal = bakeOptions[idxBake]; idxBake  = (idxBake - 1 + bakeOptionsSize) % bakeOptionsSize;  newVal = bakeOptions[idxBake]; } // [cite: 55, 56]
      
      calculateTotalMin(); // [cite: 57]
      if (currentState != IDLE && currentState != FINISHED) { // [cite: 58]
        remainingMin += (newVal - oldVal); // [cite: 58]
        if (remainingMin < 0) remainingMin = 0; // [cite: 59]
      }
      updateDisplay(); // [cite: 59]
      delay(200); // [cite: 59]
    }
  }
  lastBtnDownState = btnDown; // [cite: 60]

  bool btnRunReset = digitalRead(PIN_BTN_START_RESET); // [cite: 60]
  if (btnRunReset == LOW) { // [cite: 61]
    if (!runResetWasPressed) { // [cite: 61]
      runResetPressTime = now; // [cite: 61]
      runResetWasPressed = true; // [cite: 62]
    } else if (now - runResetPressTime >= 1000) { // [cite: 62]
      display.clearDisplay(); // [cite: 62]
      display.setCursor(0, 0); // [cite: 62]
      display.print(F("System Rebooting...")); // [cite: 63]
      display.display(); // [cite: 63]
      delay(2000);  // [cite: 63]
      resetFunc();  // [cite: 63]
    }
  } else {
    if (runResetWasPressed) { // [cite: 63]
      if (now - runResetPressTime < 1000 && currentState == IDLE) { // [cite: 63]
        remainingMin = totalMin; // [cite: 63]
        countdownClock = now;
        currentState = KNEADING; // [cite: 64]
        cyclesRemaining = kneadOptions[idxKnead] / 30; // [cite: 64]
        stepInitialized = false; // [cite: 64]
        currentMenuLine = 0;  // [cite: 64]
        if (debugLevel >= 1) { // [cite: 64]
          serialLog(F("Cooking started Total Min:"), totalMin); // 
        }
      }
      runResetWasPressed = false; // 
    }
  }
}

void printFormatTime(long totalMinutes) {
  long hh = totalMinutes / 60; // [cite: 66]
  long mm = totalMinutes % 60; // [cite: 66]
  if (hh < 10) display.print(F("0")); // [cite: 67]
  display.print(hh); // [cite: 67]
  display.print(F(":")); // [cite: 67]
  if (mm < 10) display.print(F("0")); // [cite: 67]
  display.print(mm); // [cite: 68]
}

// --- DISPLAY GRAPHICS ENGINE ---
void updateDisplay() {
  display.clearDisplay(); // [cite: 68]
  display.setTextColor(SSD1306_WHITE); // [cite: 68]
  display.setCursor(0, 0); // [cite: 68]
  if (currentState == FINISHED) { // [cite: 69]
    display.print(F("Sourdough Done")); // [cite: 69]
  } else { // [cite: 70]
    if (currentState == IDLE) { // [cite: 70]
      display.print(F("Sourdough")); // [cite: 70]
      display.setCursor(80, 0); // [cite: 70]
      printFormatTime(totalMin); // [cite: 70]
    } else { // [cite: 71]
      display.print(F("Cooking")); // [cite: 71]
      display.setCursor(80, 0); // [cite: 71]
      printFormatTime(remainingMin); // [cite: 71]
    }
  }

  // Live status tracking spinning indicator
  if (currentState != IDLE && currentState != FINISHED) { // [cite: 72]
    const char spinIcons[] = {'/', '-', '\\', '|'}; // [cite: 72]
    int iconIndex = (millis() / 250) % 4; // [cite: 73]
    display.setCursor(118, 0); // [cite: 73]
    display.print(spinIcons[iconIndex]); // [cite: 73]
  }

  int locked = getLockedMenuLine(); // [cite: 73]
  bool blink = (millis() / 400) % 2 == 0; // [cite: 74]
  renderMenuRow(12, F("KneadMin: "), kneadOptions[idxKnead], (locked == 0) ? blink : (currentMenuLine == 0)); // [cite: 75]
  renderMenuRow(24, F("DegasMin: "), degasOptions[idxDegas], (locked == 1) ? blink : (currentMenuLine == 1)); // [cite: 76]
  renderMenuRow(36, F("ProofHr:  "), proofOptions[idxProof],  (locked == 2) ? blink : (currentMenuLine == 2)); // [cite: 77]
  renderMenuRow(48, F("BakeMin:  "), bakeOptions[idxBake],   (locked == 3) ? blink : (currentMenuLine == 3)); // [cite: 78]

  display.display(); // [cite: 78]
}

// --- PROCESSING SEQUENCE CONTROL ENGINE ---
void runSourdoughEngine() {
  if (currentState == IDLE || currentState == FINISHED) return; // [cite: 79]
  unsigned long currentMillis = millis(); // [cite: 80]
  static unsigned long lastInterfaceRefresh = 0; // [cite: 80]
  if (currentMillis - lastInterfaceRefresh >= 100) { // [cite: 81]
    updateDisplay(); // [cite: 81]
    lastInterfaceRefresh = currentMillis; // [cite: 82]
  }

  switch (currentState) {
    case KNEADING:
      if (!stepInitialized) {
        if (debugLevel >= 1) { serialLog(F("Kneading"), kneadOptions[idxKnead]); } // [cite: 82]
        pinMode(srvMenu, OUTPUT); // [cite: 82]
        pinMode(srvMinus, OUTPUT); // [cite: 83]
        pinMode(srvRunReset, OUTPUT); // [cite: 83]
        digitalWrite(srvMenu, HIGH); // [cite: 83]
        digitalWrite(srvMinus, HIGH);         // [cite: 83]
        digitalWrite(srvRunReset, HIGH); // [cite: 83]

        updateDisplay(); // [cite: 83]
        longPress(srvRunReset); // [cite: 83]
        shortPress(srvMenu, 7); // [cite: 83]
        shortPress(srvMinus, 10); // [cite: 83]
        shortPress(srvRunReset, 1);  // [cite: 83]
        
        stateStartTime = currentMillis; // [cite: 83]
        // 30 mins -> 30 secs scaled for testing, or full 30 mins for real bake
        stepInterval = testRun ? (30UL * 1000UL) : (30UL * 60UL * 1000UL); // [cite: 84]
        stepInitialized = true; // [cite: 84]
      }
      
      if (currentMillis - stateStartTime >= stepInterval) { // [cite: 85]
        longPress(srvRunReset); // [cite: 85]
        cyclesRemaining--; // [cite: 86]
        if (cyclesRemaining > 0) { // [cite: 86]
          stepInitialized = false; // [cite: 86]
        } else {
          currentState = DEGAS; // [cite: 87]
          cyclesRemaining = degasOptions[idxDegas] / 30; // [cite: 87]
          stepInitialized = false; // [cite: 88]
          cycleMenu(true);  // [cite: 88]
        }
      }
      break;

    case DEGAS:
      if (!stepInitialized) {
        if (debugLevel >= 1) { serialLog(F("Degas"), degasOptions[idxDegas]); } // [cite: 89]
        updateDisplay(); // [cite: 89]
        longPress(srvRunReset);  // [cite: 90]
        shortPress(srvMenu, 7); // [cite: 90]
        shortPress(srvMinus, 10); // [cite: 90]
        shortPress(srvRunReset, 1);  // [cite: 90]
        
        stateStartTime = currentMillis; // [cite: 90]
        // 60 secs scaled for testing, or full 60 mins for real bake
        stepInterval = testRun ? (60UL * 1000UL) : (60UL * 60UL * 1000UL); // [cite: 90]
        stepInitialized = true; // [cite: 91]
      }
      
      if (currentMillis - stateStartTime >= stepInterval) { // [cite: 92]
        longPress(srvRunReset); // [cite: 92]
        cyclesRemaining--; // [cite: 93]
        if (cyclesRemaining > 0) { // [cite: 93]
          stepInitialized = false; // [cite: 93]
        } else {
          currentState = PROOF; // [cite: 94]
          cyclesRemaining = proofOptions[idxProof] * 2; // [cite: 94]
          stepInitialized = false; // [cite: 95]
          cycleMenu(true); // [cite: 95]
        }
      }
      break;

    case PROOF:
      if (!stepInitialized) {
        if (debugLevel >= 1) { serialLog(F("Proof"), proofOptions[idxProof]); } // [cite: 96]
        updateDisplay(); // [cite: 96]
        if (cyclesRemaining == (proofOptions[idxProof] * 2)) { // [cite: 97]
          longPress(srvRunReset); // [cite: 97]
        }
        stateStartTime = currentMillis; // [cite: 98]
        // 1 Hour iteration -> 60 secs scaled for testing, or full 30 mins (since it loops twice per hour)
        stepInterval = testRun ? (60UL * 1000UL) : (30UL * 60UL * 1000UL); // [cite: 98]
        stepInitialized = true; // [cite: 99]
      }
      
      if (currentMillis - stateStartTime >= stepInterval) { // [cite: 100]
        cyclesRemaining--; // [cite: 100]
        if (cyclesRemaining > 0) { // [cite: 101]
          stepInitialized = false; // [cite: 101]
        } else {
          currentState = BAKE_60; // [cite: 102]
          stepInitialized = false; // [cite: 102]
          cycleMenu(true); // [cite: 102]
        }
      }
      break;

    case BAKE_60:
      if (!stepInitialized) {
        if (debugLevel >= 1) { serialLog(F("Bake"), bakeOptions[idxBake]); } // [cite: 104]
        updateDisplay(); // [cite: 104]
        longPress(srvRunReset);  // [cite: 105]
        shortPress(srvMenu, 13); // [cite: 105]
        shortPress(srvMinus, 10); // [cite: 105]
        shortPress(srvRunReset, 1);  // [cite: 105]
        
        stateStartTime = currentMillis; // [cite: 105]
        // 60 minutes trace -> 60 secs scaled for testing, or full 60 mins for real bake
        stepInterval = testRun ? (60UL * 1000UL) : (60UL * 60UL * 1000UL); // [cite: 105]
        stepInitialized = true; // [cite: 106]
      }
      
      if (currentMillis - stateStartTime >= stepInterval) { // [cite: 107]
        int remainingBakeTime = bakeOptions[idxBake] - 60; // [cite: 107]
        if (remainingBakeTime > 0) { // [cite: 108]
          currentState = BAKE_REMAINING; // [cite: 108]
          cyclesRemaining = remainingBakeTime; // [cite: 108]
          stepInitialized = false; // [cite: 109]
        } else {
          longPress(srvRunReset);  // [cite: 109]
          currentState = FINISHED; // [cite: 109]
          stepInitialized = false; // [cite: 110]
        }
      }
      break;

    case BAKE_REMAINING:
      if (!stepInitialized) {
        updateDisplay(); // [cite: 111]
        longPress(srvRunReset);  // [cite: 111]
        shortPress(srvMenu, 13); // [cite: 111]
        shortPress(srvMinus, 10); // [cite: 112]
        shortPress(srvRunReset, 1);  // [cite: 112]
        
        stateStartTime = currentMillis; // [cite: 112]
        // Remaining minutes -> seconds scaled for testing, or full remaining milliseconds for real bake
        stepInterval = testRun ? ((unsigned long)cyclesRemaining * 1000UL) : ((unsigned long)cyclesRemaining * 60UL * 1000UL); // [cite: 112]
        stepInitialized = true; // [cite: 113]
      }
      
      if (currentMillis - stateStartTime >= stepInterval) { // [cite: 114]
        longPress(srvRunReset); // [cite: 114]
        currentState = FINISHED; // [cite: 115]
        stepInitialized = false; // [cite: 115]
      }
      break;

    default:
      break;
  }

  if (currentState == FINISHED) { // [cite: 116]
    updateDisplay();  // [cite: 116]
    delay(2000);  // [cite: 116]
    currentState = IDLE; // [cite: 116]
    calculateTotalMin(); // [cite: 116]
    updateDisplay(); // [cite: 116]
  }
}

void renderMenuRow(int yPos, const __FlashStringHelper* textLabel, int numericValue, bool invertedStyle) {
  if (invertedStyle) { // [cite: 117]
    display.fillRect(0, yPos - 1, SCREEN_WIDTH, 11, SSD1306_WHITE); // [cite: 117]
    display.setTextColor(SSD1306_BLACK); // [cite: 118]
  } else {
    display.setTextColor(SSD1306_WHITE); // [cite: 118]
  }
  display.setCursor(0, yPos); // [cite: 118]
  display.print(textLabel); // [cite: 118]
  display.print(numericValue); // [cite: 118]
}