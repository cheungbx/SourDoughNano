#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Servo.h>
#include <EEPROM.h> // Included for persistent option storage

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// --- GLOBAL FLAGS ---
const bool testRun = false; // Set to true for time compression (1 min = 1 sec), false for production real-time
const int debugLevel = 2;

// --- PIN ASSIGNMENTS ---
const int PIN_BTN_MENU        = 4;
const int PIN_BTN_UP          = 5;
const int PIN_BTN_DOWN        = 6;
const int PIN_BTN_START_RESET = 7; 

const int srvMenu        = 9;   // Purple Wire on bread machine
const int srvMinus       = 10;  // Yellow Wire on bread machine
const int srvRunReset    = 11;  // Orange Wire on bread machine

// --- ARRAYS FOR PARAMETER OPTIONS ---
const int kneadOptions[] = {1, 30, 60, 90};
const int degasOptions[] = {1, 30, 60, 90, 120, 150, 180};
const int proofOptions[] = {0, 1, 3, 6, 9, 12, 15, 18, 21, 24}; // 0 = 1 min, others are Hours   
const int bakeOptions[]  = {1, 20, 30, 60, 70, 80, 90, 100, 110, 120};   

int kneadOptionsSize = sizeof(kneadOptions) / sizeof(kneadOptions[0]);
int degasOptionsSize = sizeof(degasOptions) / sizeof(degasOptions[0]);
int proofOptionsSize = sizeof(proofOptions) / sizeof(proofOptions[0]);
int bakeOptionsSize = sizeof(bakeOptions)   / sizeof(bakeOptions[0]);

// Default indices (will be overwritten if valid data exists in EEPROM)
int idxKnead = 1; 
int idxDegas = 3; 
int idxProof = 1; // Defaults to 1 Hour
int idxBake  = 4; 

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

// --- DYNAMIC SERIAL LOGGER ENGINE WITH HH:MM:SS TIMESTAMPS & CYCLE INTERVALS ---
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

  // Validate bounds before applying to prevent errors on fresh/blank EEPROM hardware
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
    delay(200);
    digitalWrite(pin, HIGH);
    delay(300);
    digitalWrite(pin, LOW);
    delay(2000);
    digitalWrite(pin, HIGH);
    delay(1000);
}

void calculateTotalMin() {
  long proofMinutes = 0;
  if (proofOptions[idxProof] == 0) {
    proofMinutes = 1; // Option 0 means 1 minute for quick validation testing
  } else {
    proofMinutes = (long)proofOptions[idxProof] * 60; // Others translated to Hours
  }
  totalMin = (long)kneadOptions[idxKnead] + (long)degasOptions[idxDegas] + proofMinutes + (long)bakeOptions[idxBake];
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
  for (int i = 0; i < 4; i++) {
    if (forward) {
      currentMenuLine = (currentMenuLine + 1) % 4;
    } else {
      currentMenuLine = (currentMenuLine - 1 + 4) % 4;
    }
    break; 
  }
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
          
          // --- STORE OPTIONS TO EEPROM WHEN COOKING STARTS ---
          EEPROM.update(EEPROM_ADDR_KNEAD, idxKnead);
          EEPROM.update(EEPROM_ADDR_DEGAS, idxDegas);
          EEPROM.update(EEPROM_ADDR_PROOF, idxProof);
          EEPROM.update(EEPROM_ADDR_BAKE, idxBake);
          
          if (debugLevel >= 1) {
            Serial.println(F("[EEPROM] Current configuration indexes saved securely to flash storage."));
            
            // --- LOG ALL SELECTED COOKING OPTIONS ---
            Serial.println(F("========================================"));
            Serial.println(F("    COOKING STARTED - ACTIVE PROFILE    "));
            Serial.println(F("========================================"));
            Serial.print(F(" Knead Time : ")); Serial.print(kneadOptions[idxKnead]); Serial.println(F(" min"));
            Serial.print(F(" Degas Time : ")); Serial.print(degasOptions[idxDegas]); Serial.println(F(" min"));
            
            Serial.print(F(" Proof Time : ")); 
            if (proofOptions[idxProof] == 0) {
              Serial.println(F("1 min (Special Selection)"));
            } else {
              Serial.print(proofOptions[idxProof]); Serial.println(F(" hours"));
            }
            
            Serial.print(F(" Bake Time  : ")); Serial.print(bakeOptions[idxBake]);  Serial.println(F(" min"));
            Serial.print(F(" Total Time : ")); Serial.print(totalMin);               Serial.println(F(" min"));
            Serial.println(F("========================================"));
          }

          remainingMin = totalMin;
          remainingSec = 0;
          oneSecondClock = now;
          currentState = KNEADING;
          
          cyclesRemaining = (kneadOptions[idxKnead] + 29) / 30;
          stepInitialized = false;
          currentMenuLine = 0; 
          isPaused = false;
          if (debugLevel >= 1) {
            Serial.println();
            Serial.println();
            serialLog(F("Cooking started Total Min:"), totalMin);
          }
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

// --- DISPLAY GRAPHICS ENGINE ---
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
      if (isPaused && ((millis() / 500) % 2 == 0)) {
        display.print(F("PAUSED"));
      } else {
        display.print(F("Cooking"));
      }
      display.setCursor(70, 0); 
      printFormatTime(remainingMin, remainingSec, true);
    }
  }

  if (currentState != IDLE && currentState != FINISHED) {
    const char spinIcons[] = {'/', '-', '\\', '|'};
    int iconIndex = (millis() / 250) % 4;
    display.setCursor(118, 0);
    display.print(spinIcons[iconIndex]);
  }

  renderMenuRow(12, F("KneadMin: "), kneadOptions[idxKnead], (currentState == KNEADING), 0);
  renderMenuRow(24, F("DegasMin: "), degasOptions[idxDegas], (currentState == DEGAS), 1);
  
  // Dynamic switch text header depending on proof testing selection styles
  if (proofOptions[idxProof] == 0) {
    renderMenuRow(36, F("ProofMin: "), 1, (currentState == PROOF), 2);
  } else {
    renderMenuRow(36, F("ProofHr:  "), proofOptions[idxProof], (currentState == PROOF), 2);
  }
  
  renderMenuRow(48, F("BakeMin:  "), bakeOptions[idxBake], (currentState == BAKE), 3);

  display.display();
}

// --- PROCESSING SEQUENCE CONTROL ENGINE ---
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
      if (!stepInitialized) {
        long totalProofMinutes = (proofOptions[idxProof] == 0) ? 1 : ((long)proofOptions[idxProof] * 60);
        activeStageRemaining = remainingMin - ((long)degasOptions[idxDegas] + totalProofMinutes + (long)bakeOptions[idxBake]);
        
        // Anti-race condition protection floor code
        if (activeStageRemaining <= 0) {
          activeStageRemaining = (kneadOptions[idxKnead] % 30 == 0) ? 30 : (kneadOptions[idxKnead] % 30);
        }
        if (activeStageRemaining > 30) {
          activeStageRemaining = 30;
        }

        if (debugLevel >= 1) {
          serialLog(F("Kneading loop cycle initialized"), kneadOptions[idxKnead], cyclesRemaining, activeStageRemaining);
        }
        
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
        stepInterval = testRun ? ((unsigned long)activeStageRemaining * 1000UL) : ((unsigned long)activeStageRemaining * 60UL * 1000UL); 
        stepInitialized = true;
      }
      
      if (currentMillis - stateStartTime >= stepInterval) {
        longPress(srvRunReset);
        cyclesRemaining--;
        
        long totalProofMinutes = (proofOptions[idxProof] == 0) ? 1 : ((long)proofOptions[idxProof] * 60);
        activeStageRemaining = remainingMin - ((long)degasOptions[idxDegas] + totalProofMinutes + (long)bakeOptions[idxBake]);
        if (cyclesRemaining > 0 && activeStageRemaining > 0) {
          stepInitialized = false;
          if (debugLevel >= 1) {
            serialLog(F("Kneading loop cycle continuous shift"), -1, cyclesRemaining, (activeStageRemaining > 30 ? 30 : activeStageRemaining));
          }
        } else {
          currentState = DEGAS;
          cyclesRemaining = (degasOptions[idxDegas] + 29) / 30;
          stepInitialized = false;
        }
      }
      break;

    case DEGAS:
      if (!stepInitialized) {
        long totalProofMinutes = (proofOptions[idxProof] == 0) ? 1 : ((long)proofOptions[idxProof] * 60);
        activeStageRemaining = remainingMin - (totalProofMinutes + (long)bakeOptions[idxBake]);
        
        // Anti-race condition protection floor code
        if (activeStageRemaining <= 0) {
          activeStageRemaining = (degasOptions[idxDegas] % 30 == 0) ? 30 : (degasOptions[idxDegas] % 30);
        }
        if (activeStageRemaining > 30) {
          activeStageRemaining = 30;
        }

        if (debugLevel >= 1) {
          serialLog(F("Degas loop cycle initialized"), degasOptions[idxDegas], cyclesRemaining, activeStageRemaining);
        }
        
        updateDisplay();
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
        
        long totalProofMinutes = (proofOptions[idxProof] == 0) ? 1 : ((long)proofOptions[idxProof] * 60);
        activeStageRemaining = remainingMin - (totalProofMinutes + (long)bakeOptions[idxBake]);
        if (cyclesRemaining > 0 && activeStageRemaining > 0) {
          stepInitialized = false;
          if (debugLevel >= 1) {
            serialLog(F("Degas loop cycle continuous shift"), -1, cyclesRemaining, (activeStageRemaining > 30 ? 30 : activeStageRemaining));
          }
        } else {
          currentState = PROOF;
          if (proofOptions[idxProof] == 0) {
            cyclesRemaining = 1;
          } else {
            cyclesRemaining = (((long)proofOptions[idxProof] * 60) + 59) / 60;
          }
          stepInitialized = false;
        }
      }
      break;

    case PROOF:
      if (!stepInitialized) {
        activeStageRemaining = remainingMin - (long)bakeOptions[idxBake];
        
        // Anti-race condition protection floor code
        if (activeStageRemaining <= 0) {
          if (proofOptions[idxProof] == 0) {
            activeStageRemaining = 1;
          } else {
            long totalProofMinutes = (long)proofOptions[idxProof] * 60;
            activeStageRemaining = (totalProofMinutes % 60 == 0) ? 60 : (totalProofMinutes % 60);
          }
        }
        if (activeStageRemaining > 60) {
          activeStageRemaining = 60;
        }

        if (debugLevel >= 1) {
          long displayLabelValue = (proofOptions[idxProof] == 0) ? 1 : ((long)proofOptions[idxProof] * 60);
          serialLog(F("Proof loop cycle initialized"), displayLabelValue, cyclesRemaining, activeStageRemaining);
        }
        
        updateDisplay();
        long totalProofMinutes = (proofOptions[idxProof] == 0) ? 1 : ((long)proofOptions[idxProof] * 60);
        long proofTimeElapsed = totalProofMinutes - activeStageRemaining;
        if (proofTimeElapsed == 0) {
          longPress(srvRunReset);
        }
        
        stateStartTime = currentMillis;
        stepInterval = testRun ? ((unsigned long)activeStageRemaining * 1000UL) : ((unsigned long)activeStageRemaining * 60UL * 1000UL);
        stepInitialized = true;
      }
      
      if (currentMillis - stateStartTime >= stepInterval) {
        cyclesRemaining--;
        
        activeStageRemaining = remainingMin - (long)bakeOptions[idxBake];
        if (cyclesRemaining > 0 && activeStageRemaining > 0) {
          stepInitialized = false;
          if (debugLevel >= 1) {
            serialLog(F("Proof loop cycle continuous shift"), -1, cyclesRemaining, (activeStageRemaining > 60 ? 60 : activeStageRemaining));
          }
        } else {
          currentState = BAKE;
          cyclesRemaining = (bakeOptions[idxBake] + 59) / 60; 
          stepInitialized = false;
        }
      }
      break;

    case BAKE:
      if (!stepInitialized) {
        long currentBakeInterval = remainingMin;
        
        // Anti-race condition protection floor code
        if (currentBakeInterval <= 0) {
          currentBakeInterval = 1;
        }
        if (currentBakeInterval > 60) {
          currentBakeInterval = 60;
        }
        
        if (debugLevel >= 1) {
          serialLog(F("Bake loop cycle initialized"), bakeOptions[idxBake], cyclesRemaining, currentBakeInterval);
        }
        
        updateDisplay();
        longPress(srvRunReset); 
        shortPress(srvMenu, 13);
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
          if (debugLevel >= 1) {
            serialLog(F("Bake loop cycle continuous shift"), -1, cyclesRemaining, (remainingMin > 60 ? 60 : remainingMin));
          }
        } else {
          currentState = FINISHED;
          stepInitialized = false;
          if (debugLevel >= 1) {
            serialLog(F("Cooking finished completely! Freezing screen layout..."));
          }
          updateDisplay(); 
        }
      }
      break;

    default:
      break;
  }
}

void renderMenuRow(int yPos, const __FlashStringHelper* textLabel, int numericValue, bool isActiveStage, int targetRowID) {
  display.setCursor(0, yPos);
  
  if (currentState == IDLE) {
    if (currentMenuLine == targetRowID) {
      display.fillRect(0, yPos - 1, SCREEN_WIDTH, 11, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
    } else {
      display.setTextColor(SSD1306_WHITE);
    }
    display.print(textLabel);
    display.print(numericValue);
  } else {
    display.setTextColor(SSD1306_WHITE);
    
    if (isActiveStage) {
      long dynamicCycleMin = 0;
      
      if (currentState == KNEADING || currentState == DEGAS || currentState == PROOF) {
        long standardBlockSize = (currentState == PROOF) ? 60 : 30;
        
        if (currentState == PROOF && proofOptions[idxProof] == 0) {
          standardBlockSize = 1;
        }
        
        long staticBlocksMin = (long)(cyclesRemaining - 1) * standardBlockSize;
        long remainingModOffset = remainingMin % standardBlockSize;
        
        if (remainingModOffset == 0 && remainingMin > 0) {
          remainingModOffset = standardBlockSize;
          if (staticBlocksMin >= standardBlockSize) {
             staticBlocksMin -= standardBlockSize;
          }
        }
        
        dynamicCycleMin = staticBlocksMin + remainingModOffset;
        
        if (remainingSec > 0 && (remainingMin % standardBlockSize != 0)) {
          dynamicCycleMin++;
        }
        
        long maxCeiling = 0;
        if (currentState == KNEADING) maxCeiling = kneadOptions[idxKnead];
        if (currentState == DEGAS)    maxCeiling = degasOptions[idxDegas];
        if (currentState == PROOF)    maxCeiling = (proofOptions[idxProof] == 0) ? 1 : ((long)proofOptions[idxProof] * 60);
        if (dynamicCycleMin > maxCeiling) dynamicCycleMin = maxCeiling;
        
      } else if (currentState == BAKE) {
        dynamicCycleMin = remainingMin;
        if (remainingSec > 0) dynamicCycleMin++;
      }
      
      if (dynamicCycleMin < 0) dynamicCycleMin = 0;

      display.print(F("*"));
      display.print(textLabel);
      display.print(F("C:"));
      display.print(cyclesRemaining);
      display.print(F(" M:"));
      display.print((int)dynamicCycleMin);
    } else {
      display.print(F(" "));
      display.print(textLabel);
      display.print(numericValue);
    }
  }
}