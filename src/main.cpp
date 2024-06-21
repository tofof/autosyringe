

#include <debounce.h>
static void buttonHandler(uint8_t, uint8_t);
static Button touch(0, buttonHandler);

// Stepper (TMC2208)
#define DIR_PIN   15     // Direction
#define STEP_PIN  16     // Step
#define ENDWARD true
#define MOTORWARD false
#define STEPS_PER_MM 320  // with m12 on 00, it's 160 steps per 0.5 mm, 320 steps per 1 mm, or 32 steps per 0.1 mm = 1 dmm
bool direction = ENDWARD;
bool shouldStep = false;
void doStep();

#include "FS.h"
#include <SPI.h>
#include <TFT_eSPI.h>      // Hardware-specific library
#include "Free_Fonts.h" 
TFT_eSPI tft = TFT_eSPI(); 
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
uint16_t t_x = 0, t_y = 0;
#define MENU1_FONT  FSSB12  // Menu item large
bool menuOpen = false;
void drawKeypad();
void drawMenu(int, bool);
void setupMenuButtons();
void touchCalibrate();
void status(const char*);
void setupScreen();


void setup() {
  Serial.begin(115200);
  setupScreen();
  pinMode(STEP_PIN, OUTPUT);
  pinMode(DIR_PIN, OUTPUT);
  digitalWrite(STEP_PIN, LOW);
  digitalWrite(DIR_PIN, ENDWARD);
}



void loop(void) {
  bool pressed = tft.getTouch(&t_x, &t_y); // Pressed will be set true is there is a valid touch on the screen
  touch.update((uint8)pressed);
  if (shouldStep) doStep();
  else delay(5);
}


void doStep() {
  digitalWrite(STEP_PIN, HIGH);
  delayMicroseconds(40);
  digitalWrite(STEP_PIN, LOW);
  delayMicroseconds(40);
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
    tft.drawString("1.1", 210, y-11, GFXFF);
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
    tft.drawString("30", 210, y-11, GFXFF);
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
    tft.drawString("Plunger should read: 2.0 mL", 4, y-17, GFXFF);
    tft.setTextDatum(BR_DATUM);
    tft.setFreeFont(MENU1_FONT);
    tft.drawString("2.5", 210, y-11, GFXFF);
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
    tft.drawString("60", 210, y-11, GFXFF);
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
    tft.drawString("2.0", 210, y-11, GFXFF);
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
    tft.drawString("20", 210, y-11, GFXFF);
    tft.drawString("sec", 235, y-13, FONT2);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
  }
}


static void buttonHandler(uint8_t btnId, uint8_t pressed) {
  if (!menuOpen) {  //only check left side if menu not already open
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
        menuOpen=true;
        drawKeypad();
      }
    }
  }

  
  if (menuOpen) { // only check keypad if menu is open
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
        if (b != 0) key[b].drawButton();     // draw normal, except exit button doesn't get redrawn
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
          hidden = true;
          menuOpen = false;
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
          status("Sent value to serial port");
          Serial.println(numberBuffer);
        }
      }
    }
    
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


void drawKeypad() {
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
    Serial.println("formatting file system");
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

  //touchscreen gives hi not low when engaged, so these functions act swapped
  touch.setPushDebounceInterval(100);
  touch.setReleaseDebounceInterval(0);
}
