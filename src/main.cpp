#include <arduino.h>

typedef unsigned long millis_t; //unless defined elsewhere
typedef unsigned long micros_t; //unless defined elsewhere

#include <debounce.h>
static void buttonHandler(uint8_t, uint8_t);
static Button touch(0, buttonHandler);

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
#define STATUS_X 375 // Centered on this
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

// Stepper (TMC2208)
#define DIR_PIN   4    // Direction
#define STEP_PIN  5     // Step
#define ENDWARD true
#define MOTORWARD false
#define STEPS_PER_MM 320  // with m12 on 00, it's 160 steps per 0.5 mm, 320 steps per 1 mm, or 32 steps per 0.1 mm = 1 dmm
int dirMult = 0;
int16_t position = 0;
int16_t currentSteps = 0;
micros_t startTime = 0;
micros_t nextStepTime = 0;
void administerDose();
void doStep();
void doJog(int);

// Medicine
void setupDosage(/*int*/);
#define MAX_SYRINGE 21   // one larger than max syringe size in mL
                         // 0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19  20          Syringe total mL
const float mm_per_mL[21]={ 0, 0, 0,16, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
int steps_per_mL = STEPS_PER_MM * mm_per_mL[3];
float initialDose_mL = 1.1;
int initialSteps = initialDose_mL * steps_per_mL;
int initialTime_sec = 30;
int initialDelay_us = initialTime_sec * 1e6 / initialSteps;
float totalDose_mL = 2.5;
float plungerDose_mL = totalDose_mL - 0.5;
float steadyDose_mL = plungerDose_mL - initialDose_mL;
int steadySteps = steadyDose_mL * steps_per_mL;
int steadyTime_sec = 60; // time per 0.1 mL
int steadyDelay_us = steadyTime_sec * 1e6 / (0.1 * steps_per_mL);
float salineFlush_mL = 2.0;
int salineSteps = salineFlush_mL * steps_per_mL;
int salineTime_sec = 20; // time per 0.1 mL after the first 0.5 mL of saline (which is really the last 0.5 of dose)
int salineDelay_us = salineTime_sec * 1e6 / (0.1 * steps_per_mL);
int steps[3] = {initialSteps, steadySteps, salineSteps};
int delay_us[3] = {initialDelay_us, steadyDelay_us, salineDelay_us};
int phase = 0;


// initialDelay_us is on the order of 4000, ie 4ms, so we should probably do a 'nextStepTime' in micros that we schedule and check against
// largest value you can use with delayMicroseconds is 16383
// and of course we will be fighting other execution times like drawing the screen :(
// might need https://github.com/DrDiettrich/ALib0/blob/master/examples/MultipleTasks/MultipleTasks.ino or something like it to deal with the problem
// or might need to calculate moment of go, keep it stored, count steps, and calculate current next step time so we can take more than one step in a row to 'catch up'
// like `while (micros() > curStep * initialDelay_us + start_us) {step(); curStep++;} and do FIXED NOT VARIABLE 40us between the two step pieces


void setup() {
  Serial.begin(115200);
  setupScreen();
  delay(1000);
  pinMode(STEP_PIN, OUTPUT);
  pinMode(DIR_PIN, OUTPUT);
  digitalWrite(STEP_PIN, LOW);
  digitalWrite(DIR_PIN, MOTORWARD);
  setupDosage();
  doJog(20000);
  position=0;
  digitalWrite(DIR_PIN, ENDWARD);
  dirMult = digitalRead(DIR_PIN) ? 1 : -1;
  char temp[10];
  status(itoa(position, temp, 10));
}

void loop(void) {
  if (tft.getTouchRawZ() > 100) { bool pressed = tft.getTouch(&t_x, &t_y); touch.update((uint8)pressed);} 
  if (startTime) administerDose(); // lock-in on just administering dose, quit doing slow touchscreen reads
  else delay(5);
}

void administerDose() {
  micros_t now;
  while (currentSteps < steps[phase]) {
    now = micros();
    if (nextStepTime <= now) {
      Serial.print(">");
      doStep();
      currentSteps++;
      nextStepTime += delay_us[phase];
    }
  }
  float timeTaken = (micros() - startTime) / 1e6;
  char buffer[20];
  char timeString[10];
  dtostrf(timeTaken, 3, 1, timeString);
  sprintf(buffer, "Finished in %s sec.", timeString);
  status(buffer);

  phase++;
  currentSteps = 0;
  startTime = 0;
  // if (phase==1) {   // transition immediately from initial to steady
  //   startTime = micros();
  //   nextStepTime = startTime + steps[phase]; 
  // }
  // if (phase==2) {   // pause at transition to saline so syringe can be swapped
  //   startTime = 0;
  // }
} 

void drawControls() {
  controls[0].initButton(&tft, 480*7/12, 32, 54, 54, TFT_WHITE, TFT_RED, TFT_WHITE, "<", 1);
  controls[1].initButton(&tft, 480*67/96, 32, 30, 30, TFT_WHITE, TFT_RED, TFT_WHITE, "<", 1);
  controls[2].initButton(&tft, 480*77/96, 32, 30, 30, TFT_WHITE, TFT_GREEN, TFT_WHITE, ">", 1);
  controls[3].initButton(&tft, 480*11/12, 32, 54, 54, TFT_WHITE, TFT_GREEN, TFT_WHITE, ">", 1);
  controls[4].initButton(&tft, 480*3/4, 200, 200, 54, TFT_WHITE, TFT_BLUE, TFT_WHITE, "Start", 1);
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
    int offset = tft.drawString("Plunger should read: ", 4, y-17, GFXFF);
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
    tft.drawString("(true plunger reading)", 4, y-17, GFXFF);
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
  salineSteps = salineFlush_mL * steps_per_mL;
  salineDelay_us = salineTime_sec * 1e6 / (0.1 * steps_per_mL);
  steps[0] = initialSteps;
  steps[1] = steadySteps;
  steps[2] = salineSteps;
  delay_us[0] = initialDelay_us;
  delay_us[1] = steadyDelay_us;
  delay_us[2] = salineDelay_us;
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
  touchCalibrate();
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

void doJog(int steps) {
  for (int i=0; i<steps; i++) {
    digitalWrite(STEP_PIN, HIGH);
    delayMicroseconds(50);
    digitalWrite(STEP_PIN, LOW);
    delayMicroseconds(50);
  }
  position += dirMult * steps;
  position = max(0, (int)position);
  position = min(26400, (int)position);
  status(itoa(position, "", 10));
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
        controls[b].drawButton();       // draw invert
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
          phase = 0;
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