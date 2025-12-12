#include <LedControl.h>
#include <LiquidCrystal_I2C.h>

// --------------------------------------------------------------------------
// PIN DEFINITIONS
// --------------------------------------------------------------------------
#define PIN_BTN_UP 2
#define PIN_BTN_DOWN 3
#define PIN_BTN_LEFT 4
#define PIN_BTN_RIGHT 5
#define PIN_BTN_FIRE 6
#define PIN_BUZZER 9

#define PIN_MTX_DIN 11
#define PIN_MTX_CLK 13
#define PIN_MTX_CS 10

// --------------------------------------------------------------------------
// GAME CONSTANTS
// --------------------------------------------------------------------------
#define GRID_SIZE 8
#define SHIPS_PER_PLAYER 3

// Grid Cell States
#define CELL_EMPTY 0
#define CELL_SHIP 1  // Hidden ship
#define CELL_MISS 2
#define CELL_HIT 3

// Game State Machine
enum GameState {
  STATE_INTRO,
  STATE_SETUP_P1,
  STATE_HANDOVER_SETUP,  // Message: P1 Done -> Pass to P2
  STATE_SETUP_P2,
  STATE_HANDOVER_GAME,  // Message: P2 Done -> Pass to P1
  STATE_AIM,
  STATE_IMPACT,
  STATE_SWAP,  // Message: Px Done -> Pass to Py
  STATE_GAMEOVER
};

// --------------------------------------------------------------------------
// GLOBAL VARIABLES
// --------------------------------------------------------------------------

// Hardware Objects
LiquidCrystal_I2C lcd(0x27, 16, 2);
LedControl lc = LedControl(PIN_MTX_DIN, PIN_MTX_CLK, PIN_MTX_CS, 1);

// Game Data
byte gridP1[GRID_SIZE][GRID_SIZE];
byte gridP2[GRID_SIZE][GRID_SIZE];

GameState currentState = STATE_INTRO;
int activePlayer = 1;  // 1 or 2
int cursorX = 0;
int cursorY = 0;
int p1ShipCount = 0;
int p2ShipCount = 0;
int p1HitsRemaining = SHIPS_PER_PLAYER;
int p2HitsRemaining = SHIPS_PER_PLAYER;

// Timing & Blinking
unsigned long lastBlinkFast = 0;
unsigned long lastBlinkSlow = 0;
bool blinkFastState = false;  // Toggles approx 4Hz
bool blinkSlowState = false;  // Toggles approx 1Hz

// Input Handling
unsigned long lastDebounceTime = 0;
unsigned long firePressStartTime = 0;
bool fireButtonHeld = false;
bool ignoreInputUntilRelease = false;  // Safety flag for long presses
const int debounceDelay = 50;

// --------------------------------------------------------------------------
// SETUP
// --------------------------------------------------------------------------
void setup() {
  // Init Pins
  pinMode(PIN_BTN_UP, INPUT);
  pinMode(PIN_BTN_DOWN, INPUT);
  pinMode(PIN_BTN_LEFT, INPUT);
  pinMode(PIN_BTN_RIGHT, INPUT);
  pinMode(PIN_BTN_FIRE, INPUT);
  pinMode(PIN_BUZZER, OUTPUT);

  Serial.begin(9600);

  // Init LCD
  lcd.init();
  lcd.backlight();

  // Init Matrix
  lc.shutdown(0, false);  // Wake up
  lc.setIntensity(0, 8);  // Brightness (0-15)
  lc.clearDisplay(0);

  // Random Seed
  randomSeed(analogRead(0));

  resetGame();

  // Initial LCD Message
  lcd.setCursor(0, 0);
  lcd.print("   BATTLESHIP   ");
  lcd.setCursor(0, 1);
  lcd.print("  Press FIRE!   ");
}

// --------------------------------------------------------------------------
// MAIN LOOP
// --------------------------------------------------------------------------
void loop() {
  updateTimers();  // Handle blinking logic
  handleInput();   // Read buttons
  renderMatrix();  // Update LED display based on state
}

// --------------------------------------------------------------------------
// LOGIC HELPERS
// --------------------------------------------------------------------------

void updateTimers() {
  unsigned long currentMillis = millis();

  // Fast Blink (Cursor) - 4Hz -> 125ms toggle
  if (currentMillis - lastBlinkFast >= 125) {
    lastBlinkFast = currentMillis;
    blinkFastState = !blinkFastState;
  }

  // Slow Blink (Misses) - 1Hz -> 500ms toggle
  if (currentMillis - lastBlinkSlow >= 500) {
    lastBlinkSlow = currentMillis;
    blinkSlowState = !blinkSlowState;
  }
}

void resetGame() {
  memset(gridP1, 0, sizeof(gridP1));
  memset(gridP2, 0, sizeof(gridP2));
  p1ShipCount = 0;
  p2ShipCount = 0;
  p1HitsRemaining = SHIPS_PER_PLAYER;
  p2HitsRemaining = SHIPS_PER_PLAYER;
  activePlayer = 1;
  cursorX = 0;
  cursorY = 0;
  ignoreInputUntilRelease = false;
}

// --------------------------------------------------------------------------
// INPUT HANDLING & STATE MACHINE
// --------------------------------------------------------------------------

void handleInput() {
  // Read Buttons (Low = Pressed)
  bool bUp = digitalRead(PIN_BTN_UP);
  bool bDown = digitalRead(PIN_BTN_DOWN);
  bool bLeft = digitalRead(PIN_BTN_LEFT);
  bool bRight = digitalRead(PIN_BTN_RIGHT);
  bool bFire = digitalRead(PIN_BTN_FIRE);

  Serial.print("U:");
  Serial.print(bUp);
  Serial.print(" D:");
  Serial.print(bDown);
  Serial.print(" L:");
  Serial.print(bLeft);
  Serial.print(" R:");
  Serial.print(bRight);
  Serial.print(" F:");
  Serial.println(bFire);

  // --- CRITICAL FIX: SAFETY LOCK ---
  // If we just transitioned from a Long Press, we MUST wait for the user
  // to physically release the button before accepting any new input.
  if (ignoreInputUntilRelease) {
    if (bFire) {
      // User is still holding the button. Do nothing.
      return;
    } else {
      // User has released the button. Resume normal operation.
      ignoreInputUntilRelease = false;
      fireButtonHeld = false;  // Ensure logic resets
    }
  }

  // Simple Debounce throttle
  if (millis() - lastDebounceTime < 150) return;

  // Navigation Logic (Only valid in Setup and Aim states)
  if (currentState == STATE_SETUP_P1 || currentState == STATE_SETUP_P2 || currentState == STATE_AIM) {
    if (bUp) {
      cursorY = (cursorY - 1 + 8) % 8;
      playTone(400, 50);
      lastDebounceTime = millis();
      updateLCD();
    }
    if (bDown) {
      cursorY = (cursorY + 1) % 8;
      playTone(400, 50);
      lastDebounceTime = millis();
      updateLCD();
    }
    if (bLeft) {
      cursorX = (cursorX - 1 + 8) % 8;
      playTone(400, 50);
      lastDebounceTime = millis();
      updateLCD();
    }
    if (bRight) {
      cursorX = (cursorX + 1) % 8;
      playTone(400, 50);
      lastDebounceTime = millis();
      updateLCD();
    }
  }

  // State Machine Logic for Action Button (FIRE)

  // 1. INTRO
  if (currentState == STATE_INTRO) {
    if (bFire) {
      lastDebounceTime = millis();
      currentState = STATE_SETUP_P1;
      cursorX = 0;
      cursorY = 0;
      ignoreInputUntilRelease = true;  // Safety lock
      updateLCD();
    }
  }

  // 2. SETUP P1
  else if (currentState == STATE_SETUP_P1) {
    handleSetupInput(bFire, gridP1, p1ShipCount, STATE_HANDOVER_SETUP);
  }

  // 3. HANDOVER SETUP (P1 -> P2)
  else if (currentState == STATE_HANDOVER_SETUP) {
    if (bFire) {
      lastDebounceTime = millis();
      currentState = STATE_SETUP_P2;
      cursorX = 0;
      cursorY = 0;
      ignoreInputUntilRelease = true;  // Safety lock
      updateLCD();
    }
  }

  // 4. SETUP P2
  else if (currentState == STATE_SETUP_P2) {
    handleSetupInput(bFire, gridP2, p2ShipCount, STATE_HANDOVER_GAME);
  }

  // 5. HANDOVER GAME (P2 -> P1)
  else if (currentState == STATE_HANDOVER_GAME) {
    if (bFire) {
      lastDebounceTime = millis();
      activePlayer = 1;  // P1 starts
      currentState = STATE_AIM;
      cursorX = 0;
      cursorY = 0;
      ignoreInputUntilRelease = true;  // Safety lock to prevent accidental fire
      updateLCD();
    }
  }

  // 6. AIM
  else if (currentState == STATE_AIM) {
    if (bFire) {
      lastDebounceTime = millis();
      // Determine target grid
      byte(*targetGrid)[8] = (activePlayer == 1) ? gridP2 : gridP1;

      byte cell = targetGrid[cursorY][cursorX];

      // Only allow fire if cell hasn't been hit/missed yet
      if (cell == CELL_EMPTY || cell == CELL_SHIP) {
        currentState = STATE_IMPACT;
        handleImpact();
      } else {
        // Invalid shot buzzer
        playTone(100, 150);
      }
    }
  }

  // 7. SWAP
  else if (currentState == STATE_SWAP) {
    if (bFire) {
      lastDebounceTime = millis();
      activePlayer = (activePlayer == 1) ? 2 : 1;
      currentState = STATE_AIM;
      cursorX = 0;
      cursorY = 0;
      ignoreInputUntilRelease = true;  // Safety lock to prevent accidental fire
      updateLCD();
    }
  }

  // 8. GAMEOVER
  else if (currentState == STATE_GAMEOVER) {
    if (bFire) {
      lastDebounceTime = millis();
      resetGame();
      currentState = STATE_INTRO;
      ignoreInputUntilRelease = true;  // Safety lock
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("   BATTLESHIP   ");
      lcd.setCursor(0, 1);
      lcd.print("  Press FIRE!   ");
    }
  }
}

// Helper to handle Setup Phase Logic (Placement & Long Press Confirm)
void handleSetupInput(bool bFire, byte grid[8][8], int& shipCount, GameState nextState) {
  if (bFire) {
    if (!fireButtonHeld) {
      fireButtonHeld = true;
      firePressStartTime = millis();
    }

    // Check for Long Press (Confirm) - Must have 10 ships
    if ((millis() - firePressStartTime > 1000) && shipCount == SHIPS_PER_PLAYER) {
      playTone(1000, 100);
      delay(100);
      playTone(1500, 300);  // Confirm sound

      currentState = nextState;

      // TRIGGER THE SAFETY LOCK
      // Prevents the current button hold from triggering the next state immediately
      // The state machine will effectively PAUSE here until button is released.
      ignoreInputUntilRelease = true;

      updateLCD();
    }
  } else {
    // Button Released
    if (fireButtonHeld) {
      // Short Press (Toggle Ship)
      if (millis() - firePressStartTime < 1000) {
        if (grid[cursorY][cursorX] == CELL_SHIP) {
          grid[cursorY][cursorX] = CELL_EMPTY;
          shipCount--;
          playTone(300, 50);
        } else if (grid[cursorY][cursorX] == CELL_EMPTY && shipCount < SHIPS_PER_PLAYER) {
          grid[cursorY][cursorX] = CELL_SHIP;
          shipCount++;
          playTone(800, 50);
        }
        updateLCD();
      }
      fireButtonHeld = false;
      lastDebounceTime = millis();
    }
  }
}

// Handle the shooting logic
void handleImpact() {
  byte(*targetGrid)[8] = (activePlayer == 1) ? gridP2 : gridP1;
  byte cell = targetGrid[cursorY][cursorX];
  bool hit = (cell == CELL_SHIP);

  // Update Grid
  if (hit) {
    targetGrid[cursorY][cursorX] = CELL_HIT;
    if (activePlayer == 1)
      p2HitsRemaining--;
    else
      p1HitsRemaining--;

    // Impact Visuals/Audio
    lcd.clear();
    lcd.setCursor(5, 0);
    lcd.print("HIT!!");
    // Update Matrix immediately for impact reveal
    lc.setLed(0, cursorY, cursorX, true);
    playTone(1500, 100);
    delay(100);
    playTone(2000, 400);
  } else {
    targetGrid[cursorY][cursorX] = CELL_MISS;

    lcd.clear();
    lcd.setCursor(5, 0);
    lcd.print("MISS");
    playTone(200, 400);
  }

  // FREEZE for 2000ms as requested
  // During this time, main loop is paused, but we want the matrix to show the result
  // The matrix retains the state set just before this delay
  delay(2000);

  // Check Win
  if (p1HitsRemaining == 0 || p2HitsRemaining == 0) {
    currentState = STATE_GAMEOVER;
    playWinMelody();
    updateLCD();
  }
  // NEW RULE: If Hit, stay in AIM state (Bonus Turn)
  else if (hit) {
    currentState = STATE_AIM;
    // Don't reset cursor - keep it at hit location for easier followup
    ignoreInputUntilRelease = true;  // Safety Lock
    updateLCD();
    // Overwrite the bottom line to give feedback
    lcd.setCursor(0, 1);
    lcd.print("Shoot Again!    ");
  }
  // If Miss, Switch Players
  else {
    currentState = STATE_SWAP;
    updateLCD();
  }
}

// --------------------------------------------------------------------------
// VISUAL OUTPUT
// --------------------------------------------------------------------------

void updateLCD() {
  lcd.clear();

  if (currentState == STATE_SETUP_P1) {
    lcd.setCursor(0, 0);
    lcd.print("P1 Setup Ships");
    lcd.setCursor(0, 1);
    lcd.print("Count: ");
    lcd.print(p1ShipCount);
    lcd.print("/");
    lcd.print(SHIPS_PER_PLAYER);
    if (p1ShipCount == SHIPS_PER_PLAYER) lcd.print(" OK?");
  } else if (currentState == STATE_SETUP_P2) {
    lcd.setCursor(0, 0);
    lcd.print("P2 Setup Ships");
    lcd.setCursor(0, 1);
    lcd.print("Count: ");
    lcd.print(p2ShipCount);
    lcd.print("/");
    lcd.print(SHIPS_PER_PLAYER);
    if (p2ShipCount == SHIPS_PER_PLAYER) lcd.print(" OK?");
  } else if (currentState == STATE_HANDOVER_SETUP) {
    // Explicit handover text for Setup Phase
    lcd.setCursor(0, 0);
    lcd.print("P1 Done.");
    lcd.setCursor(0, 1);
    lcd.print("Pass to P2");
  } else if (currentState == STATE_HANDOVER_GAME) {
    // Explicit handover text for Game Start
    lcd.setCursor(0, 0);
    lcd.print("P2 Done.");
    lcd.setCursor(0, 1);
    lcd.print("Pass to P1");
  } else if (currentState == STATE_AIM) {
    lcd.setCursor(0, 0);
    lcd.print("P");
    lcd.print(activePlayer);
    lcd.print(" Aiming");
    lcd.setCursor(0, 1);
    // Convert Coords to Battleship notation (e.g., A5)
    lcd.print("Target: ");
    lcd.print((char)('A' + cursorX));
    lcd.print(cursorY + 1);
  } else if (currentState == STATE_SWAP) {
    lcd.setCursor(0, 0);
    lcd.print("P");
    lcd.print(activePlayer);
    lcd.print(" Done.");
    lcd.setCursor(0, 1);
    lcd.print("Pass to P");
    lcd.print(activePlayer == 1 ? 2 : 1);
  } else if (currentState == STATE_GAMEOVER) {
    lcd.setCursor(0, 0);
    lcd.print("GAME OVER!");
    lcd.setCursor(0, 1);
    if (p2HitsRemaining == 0)
      lcd.print("P1 WINS!");
    else
      lcd.print("P2 WINS!");
  }
}

void renderMatrix() {
  // Clear buffer logic (LedControl doesn't have a buffer, so we set pixel by pixel)
  // Optimization: Only update changed LEDs or refresh entire grid

  if (currentState == STATE_INTRO || currentState == STATE_HANDOVER_SETUP || currentState == STATE_HANDOVER_GAME ||
      currentState == STATE_GAMEOVER) {
    lc.clearDisplay(0);
    return;
  }

  // Determine which grid to show
  byte(*gridToShow)[8] = NULL;
  bool showShips = false;
  bool showCursor = false;

  if (currentState == STATE_SETUP_P1) {
    gridToShow = gridP1;
    showShips = true;
    showCursor = true;
  } else if (currentState == STATE_SETUP_P2) {
    gridToShow = gridP2;
    showShips = true;
    showCursor = true;
  } else if (currentState == STATE_AIM) {
    // Show OPPONENT grid, but hide ships
    gridToShow = (activePlayer == 1) ? gridP2 : gridP1;
    showShips = false;
    showCursor = true;
  } else if (currentState == STATE_SWAP) {
    // Show OPPONENT grid (result of last shot), no cursor
    gridToShow = (activePlayer == 1) ? gridP2 : gridP1;
    showShips = false;
    showCursor = false;
  }

  // Render Logic
  if (gridToShow != NULL) {
    for (int r = 0; r < 8; r++) {
      for (int c = 0; c < 8; c++) {
        byte cell = gridToShow[r][c];
        bool ledState = false;

        // 1. Base State
        if (cell == CELL_HIT) {
          ledState = true;  // Solid ON
        } else if (cell == CELL_MISS) {
          ledState = blinkSlowState;  // Blink Slow (1Hz)
        } else if (cell == CELL_SHIP && showShips) {
          ledState = true;  // Solid ON (Only in Setup)
        }

        // 2. Cursor Overlay (Priority)
        if (showCursor && r == cursorY && c == cursorX) {
          ledState = blinkFastState;  // Blink Fast (4Hz)
        }

        lc.setLed(0, r, c, ledState);
      }
    }
  }
}

// --------------------------------------------------------------------------
// AUDIO UTILS
// --------------------------------------------------------------------------

void playTone(int freq, int duration) {
  tone(PIN_BUZZER, freq, duration);
}

void playWinMelody() {
  int melody[] = {262, 294, 330, 349, 392, 440, 494, 523};
  for (int i = 0; i < 8; i++) {
    tone(PIN_BUZZER, melody[i], 100);
    delay(100);
  }
  noTone(PIN_BUZZER);
}