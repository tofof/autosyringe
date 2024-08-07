#include <arduino.h>


typedef unsigned long millis_t; //unless defined elsewhere
typedef unsigned long micros_t; //unless defined elsewhere

#include <debounce.h>
static void buttonHandler(uint8_t, uint8_t);
static Button touch(0, buttonHandler);

// Screen - see also config in platformio.ini
#include "FS.h"
#include <SPI.h>
#include <TFT_eSPI.h>      // Hardware-specific library
#include "Free_Fonts.h" 
#define CALIBRATION_FILE "/TouchCalData2"
#define REPEAT_CAL false
#define TFT_ROTATION 3 //0-3
#define KEY_X 280 // Centre of key
#define KEY_Y 96
#define KEY_W 62 // Width and height
#define KEY_H 30
#define KEY_SPACING_X 18 // X and Y gap
#define KEY_SPACING_Y 20
#define KEY_TEXTSIZE 1   // Font size multiplier
#define LABEL1_FONT FSSO12  // Keypad words
#define LABEL2_FONT FSSB12  // Keypad numerals
#define STATUS_FONT GLCD       
#define DISP_X 241
#define DISP_Y 10
#define DISP_W 238
#define DISP_H 50
#define STATUS_X 360 // Centered on this
#define STATUS_Y 65
#define DISP_TSIZE 3
#define DISP_TCOLOR TFT_CYAN
#define NUM_LEN 12
TFT_eSPI tft = TFT_eSPI(); 
char numberBuffer[NUM_LEN + 1] = "";
uint8_t numberIndex = 0;
char keyLabel[15][6] = {"Exit", "Clear", "<--", "1", "2", "3", "4", "5", "6", "7", "8", "9", ".", "0", "Send" };
uint16_t keyColor[15] = {TFT_RED, TFT_DARKGREY, TFT_DARKGREY,
                         TFT_BLUE, TFT_BLUE, TFT_BLUE,
                         TFT_BLUE, TFT_BLUE, TFT_BLUE,
                         TFT_BLUE, TFT_BLUE, TFT_BLUE,
                         TFT_BLUE, TFT_BLUE, TFT_DARKGREEN
                        };
TFT_eSPI_Button key[15];
TFT_eSPI_Button menu[6];
TFT_eSPI_Button controls[12];
TFT_eSPI_Button retract;
uint16_t t_x = 0, t_y = 0;
#define MENU1_FONT  FSSB12  // Menu item large
int menuOpen = 0; //1-indexed
void touchCalibrate();
void setupScreen();
void setupMenuButtons();
void drawKeypad();
void drawControls();
void drawMenu(int, bool);
void status(const char*);
void battery();

// Ring meter
#define DARKER_GREY 0x0 //0x18E3
void ringMeter(int, int, int, int);
bool initMeter = true;

// Stepper (TMC2208)
#define DIR_PIN   2    // Direction
#define STEP_PIN  4     // Step
#define ENDWARD false
#define MOTORWARD !ENDWARD
#define STEPS_PER_MM 320  // with m12 on 00, it's 160 steps per 0.5 mm, 320 steps per 1 mm, or 32 steps per 0.1 mm = 1 dmm
int dirMult = 0;
int16_t position = 0;
int16_t currentSteps = 0;
micros_t startTime = 0;
micros_t nextStepTime = 0;
void administerDose();
void doStep();
void doJog(uint16_t);
void jogToPosition(uint16_t);

// Medicine
void setupDosage(/*int*/);
void syringeChange();
#define MAX_SYRINGE 21   // one larger than max syringe size in mL
                         // 0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19  20          Syringe total mL
const float mm_per_mL[21]={ 0, 0, 0,16, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
int steps_per_mL = STEPS_PER_MM * mm_per_mL[3];
int plungerOffset_mL = 0.2; //offset because zero position would require plunger to be inserted more than 100% into syringe
float initialDose_mL = 1.1;
int initialSteps = initialDose_mL * steps_per_mL;
int initialTime_sec = 30;
int initialDelay_us = initialTime_sec * 1e6 / initialSteps;
float totalDose_mL = 2.5;
float plungerDose_mL = totalDose_mL - 0.5;
float steadyDose_mL = plungerDose_mL - initialDose_mL; //infusion dose from first syringe
int steadySteps = steadyDose_mL * steps_per_mL;
int steadyTime_sec = 60; // time per 0.1 mL
int steadyDelay_us = steadyTime_sec * 1e6 / (0.1 * steps_per_mL);
float transitionDose_mL = 0.5; //infusion dose in butterfly, administered by second syringe
int transitionSteps = transitionDose_mL * steps_per_mL;
float salineFlush_mL = 2.0;
int salineSteps = salineFlush_mL * steps_per_mL - transitionSteps; //portion of second syringe that is just saline
int salineTime_sec = 20; // time per 0.1 mL after the first 0.5 mL of saline (which is really the last 0.5 of dose)
int salineDelay_us = salineTime_sec * 1e6 / (0.1 * steps_per_mL);
int steps[4] = {initialSteps, steadySteps, transitionSteps, salineSteps};
int delay_us[4] = {initialDelay_us, steadyDelay_us, steadyDelay_us, salineDelay_us}; // transitionDelay is steadyDelay
int phase = 0;

// Piezo Buzzer
#define PIEZO_PIN   5

// Battery
#include <Battery.h>
#define SENSE_PIN A0
Battery batt = Battery(9900, 12300, SENSE_PIN);

void setup() {
  Serial.begin(115200);
  Serial.println("Setup Start");
  setupScreen();
  
  delay(1000);
  pinMode(PIEZO_PIN, OUTPUT);
  pinMode(STEP_PIN, OUTPUT);
  pinMode(DIR_PIN, OUTPUT);
  pinMode(SENSE_PIN, INPUT);
  digitalWrite(PIEZO_PIN, LOW); 
  digitalWrite(STEP_PIN, LOW);
  digitalWrite(DIR_PIN, MOTORWARD);
  dirMult = digitalRead(DIR_PIN) ? 1 : -1;
  //doJog(20000);
  position=0;
  char temp[10];
  status(itoa(position, temp, 10));
  //setupDosage();
  batt.begin(3300, 3.88); //3.3 v adc = 3300, resistor ratio (22k + 6.8k + 10k) / 10k = 2.88 
  Serial.println("Setup Done");
}

void loop(void) {
  if (tft.getTouchRawZ() > 100) { bool pressed = tft.getTouch(&t_x, &t_y); touch.update((uint8)pressed);} 
  if (startTime) administerDose(); // lock-in on just administering dose, quit doing slow touchscreen reads
  else delay(5);
  battery();
}

// #########################################################################
//  Draw the meter on the screen, returns x coord of right-hand side
// #########################################################################
// x,y is centre of meter, r the radius, val a number in range 0-100
void ringMeter(int x, int y, int r, int val)
{
  static uint16_t last_angle = 30;

  if (initMeter) {
    initMeter = false;
    last_angle = 30;
    tft.fillCircle(x, y, r, DARKER_GREY);
    tft.drawSmoothCircle(x, y, r, TFT_SILVER, DARKER_GREY);
    uint16_t tmp = r - 3;
    tft.drawArc(x, y, tmp, tmp - tmp / 5, last_angle, 330, TFT_BLACK, DARKER_GREY);
  }

  r -= 3;

  // Range here is 0-100 so value is scaled to an angle 30-330
  int val_angle = map(val, 0, 100, 30, 330);


  if (last_angle != val_angle) {
    tft.setTextPadding(100);
    tft.setCursor(STATUS_X, STATUS_Y);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setFreeFont(FSSB24);
    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(1);
    char buffer[5]; 
    sprintf(buffer, "%d%%", val);
    tft.drawString(buffer, x, y);
    tft.setTextPadding(0);

    // Allocate a value to the arc thickness dependant of radius
    uint8_t thickness = r / 5;
    if ( r < 25 ) thickness = r / 3;

    // Update the arc, only the zone between last_angle and new val_angle is updated
    if (val_angle > last_angle) {
      tft.drawArc(x, y, r, r - thickness, last_angle, val_angle, TFT_SKYBLUE, TFT_BLACK); 
    }
    else {
      tft.drawArc(x, y, r, r - thickness, val_angle, last_angle, TFT_BLACK, DARKER_GREY);
    }
    last_angle = val_angle; // Store meter arc position for next redraw
  }
}

void administerDose() {
  Serial.println("Administering Dose");
  micros_t now;
  char buffer[50];
  char timeString[10];
  char phaseString[20];
  if (phase == 0) {
    sprintf(buffer, "Initial push...");
    status(buffer);
  }
  digitalWrite(DIR_PIN, MOTORWARD);

  while (currentSteps < steps[phase]) {
    now = micros();
    if (currentSteps % 10 == 0) {
      int progress = currentSteps * 100 / steps[phase];
      ringMeter(480*3/4, 320*3/5, 480/6, progress); // Draw analogue meter
      battery();
    }
    if (nextStepTime <= now) {
      doStep();
      currentSteps++;
      nextStepTime += delay_us[phase];
    } else yield(); // 8266 will crash if loop is blocked for too long
  }

  float timeTaken = (micros() - startTime) / 1e6;
  switch (phase) {
    case 0: sprintf(phaseString, "Initial push"); break;
    case 1: sprintf(phaseString, "Infusion"); break;
    case 2: sprintf(phaseString, "Infusion tail"); break;
    case 3: sprintf(phaseString, "Saline flush"); break;
  }
  dtostrf(timeTaken, 3, 1, timeString);
  sprintf(buffer, "%s finished in %s sec.", phaseString, timeString);
  status(buffer);

  phase++;
  currentSteps = 0;
  startTime = 0;
  if (phase==1) {   // transition immediately from initial to steady
    startTime = micros();
    nextStepTime = startTime + steps[phase]; 
  }
  if (phase==2) {   // pause at transition to saline so syringe can be swapped
    startTime = 0;
    syringeChange();
  }
  if (phase==3) {   // transition immediately from dose/saline transition to remaining saline flush
    startTime = micros();
    nextStepTime = startTime + steps[phase]; 
  }
} 

void syringeChange() {
    digitalWrite(PIEZO_PIN, HIGH);
    tft.fillRect(480*3/4-480/6, 320*3/5-480/6, 480/3, 480/3, TFT_BLACK); // blank ringmeter
    tft.fillRect(241, 90, 240, 140, TFT_RED); //red warning
    tft.setFreeFont(MENU1_FONT);
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(TFT_YELLOW, TFT_RED);
    tft.drawString("REMOVE SYRINGE", 480*3/4, 165, GFXFF);
    tft.drawString("before retracting", 480*3/4, 195, GFXFF);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    retract.initButton(&tft, 480*3/4, 120, 110, 36, TFT_WHITE, TFT_GOLD, TFT_WHITE, "Retract", 1);
    tft.setFreeFont(FSSB12);
    controls[4].initButton(&tft, 500, 500, 1, 1, TFT_WHITE, TFT_BLUE, TFT_WHITE, "Start", 1);  //disable start
    retract.drawButton();
    delay(1000);
    digitalWrite(PIEZO_PIN, LOW);
}

void drawControls() {
  char label[8];
  sprintf(label, (phase==2) ? "Resume" : "Start");
  controls[0].initButton(&tft, 480*7/12, 32, 54, 54, TFT_WHITE, TFT_RED, TFT_WHITE, "<", 1);
  controls[1].initButton(&tft, 480*67/96, 32, 30, 30, TFT_WHITE, TFT_RED, TFT_WHITE, "<", 1);
  controls[2].initButton(&tft, 480*77/96, 32, 30, 30, TFT_WHITE, TFT_GREEN, TFT_WHITE, ">", 1);
  controls[3].initButton(&tft, 480*11/12, 32, 54, 54, TFT_WHITE, TFT_GREEN, TFT_WHITE, ">", 1);
  controls[4].initButton(&tft, 480*3/4, 200, 200, 54, TFT_WHITE, TFT_BLUE, TFT_WHITE, label, 1);
  
  tft.setFreeFont(FSSB24);
  controls[0].drawButton();
  controls[3].drawButton();
  controls[4].drawButton();
  tft.setFreeFont(FSSB12);
  controls[1].drawButton();
  controls[2].drawButton();
}

void drawMenu(int row=-1, bool invert=false) {
  int y=0;
  tft.setCursor(0,0);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  y+=53;
  tft.drawLine(0, y, 240, y, TFT_DARKGREY);
  if (row == -1 || row == 0) {
    if (row==0 && invert) tft.setTextColor(TFT_BLACK, TFT_LIGHTGREY);
    tft.setFreeFont(MENU1_FONT);
    tft.setTextDatum(BL_DATUM);
    tft.drawString("Initial Push", 4, y-11, GFXFF);
    tft.setTextDatum(BR_DATUM);
    tft.drawFloat(initialDose_mL, 1, 210, y-11, GFXFF);
    tft.drawString("mL", 235, y-13, FONT2);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
  }

  y+=53;
  tft.drawLine(0, y, 240, y, TFT_DARKGREY);
  if (row == -1 || row == 1) {
    if (row==1 && invert) tft.setTextColor(TFT_BLACK, TFT_LIGHTGREY);
    tft.setFreeFont(MENU1_FONT);
    tft.setTextDatum(BL_DATUM);
    tft.drawString("Initial Time", 4, y-11, GFXFF);
    tft.setTextDatum(BR_DATUM);
    tft.drawNumber(initialTime_sec, 210, y-11, GFXFF);
    tft.drawString("sec", 235, y-13, FONT2);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
  }

  y+=53;
  tft.drawLine(0, y, 240, y, TFT_DARKGREY);
  if (row == -1 || row == 2) {
    if (row==2 && invert) tft.setTextColor(TFT_BLACK, TFT_LIGHTGREY);
    tft.setFreeFont(MENU1_FONT);
    tft.setTextDatum(BL_DATUM);
    tft.drawString("Dose", 4, y-18, GFXFF);
    tft.setTextDatum(TL_DATUM);
    tft.setTextFont(GLCD);
    int offset = tft.drawString("plunger should read ", 4, y-17, GFXFF);
    offset += tft.drawFloat(plungerDose_mL, 1, 4+offset, y-17, GFXFF);
    tft.drawString(" mL", 4+offset, y-17, GFXFF);
    tft.setTextDatum(BR_DATUM);
    tft.setFreeFont(MENU1_FONT);
    tft.drawFloat(totalDose_mL, 1, 210, y-11, GFXFF);
    tft.drawString("mL", 235, y-13, FONT2);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
  }

  y+=53;
  tft.drawLine(0, y, 240, y, TFT_DARKGREY);
  if (row == -1 || row == 3) {
    if (row==3 && invert) tft.setTextColor(TFT_BLACK, TFT_LIGHTGREY);
    tft.setFreeFont(MENU1_FONT);
    tft.setTextDatum(BL_DATUM);
    tft.drawString("Infusion Rate", 4, y-18, GFXFF);
    tft.setTextDatum(TL_DATUM);
    tft.setTextFont(GLCD);
    tft.drawString("per 0.1 mL", 4, y-17, GFXFF);
    tft.setTextDatum(BR_DATUM);
    tft.setFreeFont(MENU1_FONT);
    tft.drawNumber(steadyTime_sec, 210, y-11, GFXFF);
    tft.drawString("sec", 235, y-13, FONT2);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
  }

  y+=53;
  tft.drawLine(0, y, 240, y, TFT_DARKGREY);
  if (row == -1 || row == 4) {
    if (row==4 && invert) tft.setTextColor(TFT_BLACK, TFT_LIGHTGREY);
    tft.setFreeFont(MENU1_FONT);
    tft.setTextDatum(BL_DATUM);
    tft.drawString("Saline Flush", 4, y-18, GFXFF);
    tft.setTextDatum(TL_DATUM);
    tft.setTextFont(GLCD);
    tft.drawString("true plunger reading", 4, y-17, GFXFF);
    tft.setTextDatum(BR_DATUM);
    tft.setFreeFont(MENU1_FONT);
    tft.drawFloat(salineFlush_mL, 1, 210, y-11, GFXFF);
    tft.drawString("mL", 235, y-13, FONT2);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
  }

  y+=53;
  tft.drawLine(0, y, 240, y, TFT_DARKGREY);
  if (row == -1 || row == 5) {
    if (row==5 && invert) tft.setTextColor(TFT_BLACK, TFT_LIGHTGREY);
    tft.setFreeFont(MENU1_FONT);
    tft.setTextDatum(BL_DATUM);
    tft.drawString("Saline Rate", 4, y-18, GFXFF);
    tft.setTextDatum(TL_DATUM);
    tft.setTextFont(GLCD);
    tft.drawString("per 0.1 mL", 4, y-17, GFXFF);
    tft.setTextDatum(BR_DATUM);
    tft.setFreeFont(MENU1_FONT);
    tft.drawNumber(salineTime_sec, 210, y-11, GFXFF);
    tft.drawString("sec", 235, y-13, FONT2);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
  }
}

void drawKeypad() {
  tft.fillRect(241, 0, 240, 320, TFT_BLACK);
  // Draw number display area and frame
  tft.fillRect(DISP_X, DISP_Y, DISP_W, DISP_H, TFT_BLACK);
  tft.drawRect(DISP_X, DISP_Y, DISP_W, DISP_H, TFT_WHITE);
  for (uint8_t row = 0; row < 5; row++) {
    for (uint8_t col = 0; col < 3; col++) {
      uint8_t b = col + row * 3;

      if (b < 3 || b == 14) tft.setFreeFont(LABEL1_FONT);
      else tft.setFreeFont(LABEL2_FONT);

      key[b].initButton(&tft, KEY_X + col * (KEY_W + KEY_SPACING_X),
                        KEY_Y + row * (KEY_H + KEY_SPACING_Y), // x, y, w, h, outline, fill, text
                        KEY_W, KEY_H, TFT_WHITE, keyColor[b], TFT_WHITE,
                        keyLabel[b], KEY_TEXTSIZE);
      key[b].drawButton();
    }
  }
}

void setupDosage(/*int syringeSize*/) {
  /*steps_per_mL = STEPS_PER_MM * mm_per_mL[syringeSize];*/
  initialSteps = initialDose_mL * steps_per_mL;
  initialDelay_us = initialTime_sec * 1e6 / initialSteps;
  plungerDose_mL = totalDose_mL - 0.5;
  steadyDose_mL = plungerDose_mL - initialDose_mL;
  steadySteps = steadyDose_mL * steps_per_mL;
  steadyDelay_us = steadyTime_sec * 1e6 / (0.1 * steps_per_mL);
  salineSteps = salineFlush_mL * steps_per_mL - transitionSteps; //portion of second syringe that is just saline
  salineDelay_us = salineTime_sec * 1e6 / (0.1 * steps_per_mL);
  steps[0] = initialSteps;
  steps[1] = steadySteps;
  steps[2] = transitionSteps;
  steps[3] = salineSteps;
  delay_us[0] = initialDelay_us;
  delay_us[1] = steadyDelay_us;
  delay_us[2] = steadyDelay_us; //transition continues to use steadyDelay
  delay_us[3] = salineDelay_us;
  Serial.println("Dosage jog start");
  jogToPosition((plungerDose_mL+plungerOffset_mL) * steps_per_mL);
  Serial.println("Dosage jog done");
}

void setupMenuButtons() {
  for (int y=53; y<320; y+=53) {
    menu[y/53-1].initButton(&tft, 120, y-53/2, 240, 53, TFT_BLACK, TFT_BLACK, TFT_LIGHTGREY, "", 1);
    menu[y/53-1].drawButton();
  } 
}

void touchCalibrate() {
  uint16_t calData[5];
  uint8_t calDataOK = 0;

  // check file system exists
  if (!SPIFFS.begin()) {
    SPIFFS.format();
    SPIFFS.begin();
  }

  // check if calibration file exists and size is correct
  if (SPIFFS.exists(CALIBRATION_FILE)) {
    if (REPEAT_CAL)
    {
      // Delete if we want to re-calibrate
      SPIFFS.remove(CALIBRATION_FILE);
    }
    else
    {
      File f = SPIFFS.open(CALIBRATION_FILE, "r");
      if (f) {
        if (f.readBytes((char *)calData, 14) == 14)
          calDataOK = 1;
        f.close();
      }
    }
  }

  if (calDataOK && !REPEAT_CAL) {
    // calibration data valid
    tft.setTouch(calData);
  } else {
    // data not valid so recalibrate
    tft.fillScreen(TFT_BLACK);
    tft.setCursor(20, 0);
    tft.setTextFont(2);
    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);

    tft.println("Touch corners as indicated");

    tft.setTextFont(1);
    tft.println();

    if (REPEAT_CAL) {
      tft.setTextColor(TFT_RED, TFT_BLACK);
      tft.println("Set REPEAT_CAL to false to stop this running again!");
    }

    tft.calibrateTouch(calData, TFT_MAGENTA, TFT_BLACK, 15);

    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.println("Calibration complete!");

    // store data
    File f = SPIFFS.open(CALIBRATION_FILE, "w");
    if (f) {
      f.write((const unsigned char *)calData, 14);
      f.close();
    }
  }
}

void battery() {
  tft.setTextPadding(30);
  tft.setCursor(STATUS_X, 4);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextFont(STATUS_FONT);
  tft.setTextDatum(TC_DATUM);
  tft.setTextSize(1);
  char buffer[20]; 
  sprintf(buffer, "Battery: %d%%", batt.level());
  tft.drawString(buffer, STATUS_X, 4);
  tft.setTextPadding(0);
}

void status(const char *msg) {
  tft.setTextPadding(240);
  tft.setCursor(STATUS_X, STATUS_Y);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextFont(STATUS_FONT);
  tft.setTextDatum(TC_DATUM);
  tft.setTextSize(1);
  tft.drawString(msg, STATUS_X, STATUS_Y);
  tft.setTextPadding(0);
}

void setupScreen() {
  tft.init();
  tft.setRotation(TFT_ROTATION);
  Serial.println("About to calibrate");
  touchCalibrate();
  Serial.println("Done calibrating");
  tft.fillScreen(TFT_BLACK);
  setupMenuButtons();
  drawMenu();
  drawControls();

  //touchscreen gives hi not low when engaged, so these functions act swapped
  touch.setPushDebounceInterval(100);
  touch.setReleaseDebounceInterval(0);
}

void doStep() {
  digitalWrite(STEP_PIN, HIGH);
  delayMicroseconds(50);
  digitalWrite(STEP_PIN, LOW);
  delayMicroseconds(50);
  position += dirMult;
}

void doJog(u_int16_t steps) {
  for (int i=0; i<steps; i++) {
    digitalWrite(STEP_PIN, HIGH);
    delayMicroseconds(50);
    digitalWrite(STEP_PIN, LOW);
    delayMicroseconds(50);
    if (i % 10000 == 0) yield();
  }
  position += dirMult * steps;
  position = max(0, (int)position);
  position = min(26400, (int)position);
  //status(itoa(position, "", 10));
}

void jogToPosition(uint16_t target) {
  int steps = target - position;
  status(itoa(steps, "", 10));
  digitalWrite(DIR_PIN, (steps > 0) ? ENDWARD : MOTORWARD);
  dirMult = digitalRead(DIR_PIN) ? 1 : -1;
  yield();
  doJog(abs(steps));
}

static void buttonHandler(uint8_t btnId, uint8_t pressed) {
  // MENU & CONTROLS
  if (!menuOpen) {  //only check menu and controls if menu not already open
    // MENU
    for (uint8_t b = 0; b < 6; b++) {
      if (pressed && menu[b].contains(t_x, t_y)) {  // Check if any menu coordinate boxes contain the touch coordinates
        menu[b].press(true);  
      } else {
        menu[b].press(false); 
      }
    }
    for (uint8_t b = 0; b<6; b++) { // Check if menu buttons have changed state
      if (menu[b].justReleased()) {
        menu[b].drawButton();       // draw invert
        drawMenu(b);                // redraw text invert
      }
      if (menu[b].justPressed()) {
        menu[b].drawButton(true);   // draw invert
        drawMenu(b, true);          // redraw text invert
        menuOpen=b+1;               // 1-indexed

        switch (menuOpen) {
          case 1: dtostrf(initialDose_mL, 1, 1, numberBuffer); break;
          case 2: itoa(initialTime_sec, numberBuffer, 10); break;
          case 3: dtostrf(totalDose_mL, 1, 1, numberBuffer); break;
          case 4: itoa(steadyTime_sec, numberBuffer, 10); break;
          case 5: dtostrf(salineFlush_mL, 1, 1, numberBuffer); break;
          case 6: itoa(salineTime_sec, numberBuffer, 10); break;
        }
        numberIndex=0;
        while (numberBuffer[numberIndex]!=0) numberIndex++;

        drawKeypad();
      }
    }

    // CONTROLS
    if (phase == 2) {
      if (pressed && retract.contains(t_x, t_y)) {  // Check if any controls coordinate boxes contain the touch coordinates
        retract.press(true);
      } else {
        retract.press(false); 
      }
      if (retract.justPressed()) {
        jogToPosition(salineFlush_mL * steps_per_mL);
        tft.fillRect(241, 0, 240, 320, TFT_BLACK);
        drawControls();
      }
    }
    for (uint8_t b = 0; b < 5; b++) {
      if (pressed && controls[b].contains(t_x, t_y)) {  // Check if any controls coordinate boxes contain the touch coordinates
        controls[b].press(true);
      } else {
        controls[b].press(false); 
      }
    }
    for (uint8_t b = 0; b < 5; b++) {
      if (b==0 || b==3 || b==4) tft.setFreeFont(FSSB24);
      if (b==1 || b==2) tft.setFreeFont(FSSB12);
      if (controls[b].justReleased()) {
        if (b != 4) controls[b].drawButton();       // draw regular, but start never needs to be redrawn once pressed
      }
      if (controls[b].justPressed()) {
        controls[b].drawButton(true);   // draw invert
        if (b<4) {
          digitalWrite(DIR_PIN, b < 2 ? ENDWARD : MOTORWARD);
          dirMult = digitalRead(DIR_PIN) ? 1 : -1;
        }
        if (b==4) {
          startTime = micros();
          nextStepTime = micros();
          currentSteps = 0;
          tft.fillRect(241, 0, 240, 320, TFT_BLACK);
          if (phase==2) status("Finishing dose...");
        }
      }
      if (controls[b].isPressed()) {
        if (b==0 || b==3) {
          doJog(1000);
        }
        if (b==1 || b==2) {
          doJog(100);
        }
      } 
    }
  }
  
  // KEYPAD & FIELD
  if (menuOpen) { // only check keypad if menu is open
    //KEYPAD
    for (uint8_t b = 0; b < 15; b++) {
      if (pressed && key[b].contains(t_x, t_y)) { // Check if any key coordinate boxes contain the touch coordinates
        key[b].press(true);  // tell the button it is pressed
      } else {
        key[b].press(false);  // tell the button it is NOT pressed
      }
    }
    bool hidden=false;
    for (uint8_t b = 0; b < 15; b++) {      // Check if any keypad has changed state
      if (b < 3 || b==14) tft.setFreeFont(LABEL1_FONT);  //handle these in preparation for redrawing
      else tft.setFreeFont(LABEL2_FONT);

      if (key[b].justReleased()) {
        if (b != 0 && b!=14) key[b].drawButton();     // draw normal, except exit/send buttons doesn't get redrawn
      }

      if (key[b].justPressed()) {
        key[b].drawButton(true);  // draw invert

        if (b >= 3 && b != 14) {
          if (numberIndex < NUM_LEN) {
            numberBuffer[numberIndex] = keyLabel[b][0];   // if a numberpad button, append the relevant # to the numberBuffer
            numberIndex++;
            numberBuffer[numberIndex] = 0; // zero terminate
          }
          status(""); // Clear the old status
        }

        if (b == 0) { //exit, so hide right half
          tft.fillRect(241, 0, 240, 320, TFT_BLACK);
          drawControls();
          hidden = true;
          menuOpen = 0;
        }

        if (b == 1) { // Clear button, so empty number field
          status("Value cleared");
          numberIndex = 0; // Reset index to 0
          numberBuffer[numberIndex] = 0; // Place null in buffer
        }

        if (b == 2) { // Del button, so delete last char
          numberBuffer[numberIndex] = 0;
          if (numberIndex > 0) {          
            numberIndex--;
            numberBuffer[numberIndex] = 0;//' ';
          }
          status(""); // Clear the old status
        }
 
        if (b == 14) {  // Send button
          status("updated value");
          float num=atof(numberBuffer); // no error checking here, worst case it crashes and reboots, nbd
          switch (menuOpen) {
            case 1: initialDose_mL  = num; break;
            case 2: initialTime_sec = num; break;
            case 3: totalDose_mL    = num; break;
            case 4: steadyTime_sec  = num; break;
            case 5: salineFlush_mL  = num; break;
            case 6: salineTime_sec  = num; break;
          }
          setupDosage();
          

          tft.fillRect(241, 0, 240, 320, TFT_BLACK);
          drawControls();
          hidden = true;
          menuOpen = 0;
        }
      }
    }

    // NUMBER FIELD    
    if (!hidden) {
      // Update the number display field
      tft.setTextDatum(TL_DATUM);        // Use top left corner as text coord datum
      tft.setFreeFont(&FreeSans18pt7b);  // Choose a nice font that fits box
      tft.setTextColor(DISP_TCOLOR);     // Set the font colour

      // Draw the string, the value returned is the width in pixels
      int xwidth = tft.drawString(numberBuffer, DISP_X + 4, DISP_Y + 12);

      // Now cover up the rest of the line up by drawing a black rectangle.  No flicker this way
      // but it will not work with italic or oblique fonts due to character overlap.
      tft.fillRect(DISP_X + 4 + xwidth, DISP_Y + 1, DISP_W - xwidth - 5, DISP_H - 2, TFT_BLACK);
    }
  }
}