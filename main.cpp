#include <Arduino.h>


// Buttons (5-way joystick)
#include <debounce.h>
static void buttonHandler(uint8_t, uint8_t);
static void handlerC(uint8_t, uint8_t);
static void pollButtons();
#define JOY_U 0
#define JOY_D 14
#define JOY_L 12
#define JOY_R 13
#define JOY_C 2
static Button buttonU(JOY_U, buttonHandler);    //button id doesn't have to be pin, but ensures uniqueness and simplifies use
static Button buttonD(JOY_D, buttonHandler);
static Button buttonL(JOY_L, buttonHandler);
static Button buttonR(JOY_R, buttonHandler);
static Button buttonC(JOY_C, handlerC);

// Stepper (TMC2208)
#define DIR_PIN   15     // Direction
#define STEP_PIN  16     // Step
#define ENDWARD true
#define MOTORWARD false
#define STEPS_PER_MM 320  // with m12 on 00, it's 160 steps per 0.5 mm, 320 steps per 1 mm, or 32 steps per 0.1 mm = 1 dmm
bool direction = ENDWARD;
bool shouldStep = false;
void doStep();

// Medicine
void setupDosage(int);
#define MAX_SYRINGE 21   // one larger than max syringe size in mL
//                          0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19  20          Syringe total mL
const float mm_per_mL[21]={ 0, 0, 0,16, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

void setupDosage(int syringeSize) {
  int steps_per_mL = STEPS_PER_MM * mm_per_mL[syringeSize];
  float bolusDose_mL = 1.1;
  float bolusSteps = bolusDose_mL * steps_per_mL;
  int bolusTime_sec = 45;
  int bolusDelay_us = bolusTime_sec * 5e5 / bolusSteps;    // 2 delays per step, so seconds * 1e6 / 2 = seconds * 5e5     THIS IS ASSUMING VARIABLE DELAYS IN 2 PART
  // bolusDelay_us is on the order of 4000, ie 4ms, so we should probably do a 'nextStepTime' in micros that we schedule and check against
  // largest value you can use with delayMicroseconds is 16383
  // and of course we will be fighting other execution times like drawing the screen :(
  // might need https://github.com/DrDiettrich/ALib0/blob/master/examples/MultipleTasks/MultipleTasks.ino or something like it to deal with the problem
  // or might need to calculate moment of go, keep it stored, count steps, and calculate current next step time so we can take more than one step in a row to 'catch up'
  // like `while (micros() > curStep * bolusDelay_us + start_us) {step(); curStep++;} and do FIXED NOT VARIABLE 40us between the two step pieces

}

void setup() {
  Serial.begin(115200);
  pinMode(JOY_U, INPUT_PULLUP);
  pinMode(JOY_D, INPUT_PULLUP);
  pinMode(JOY_L, INPUT_PULLUP);
  pinMode(JOY_R, INPUT_PULLUP);
  pinMode(JOY_C, INPUT_PULLUP);
  pinMode(STEP_PIN, OUTPUT);
  pinMode(DIR_PIN, OUTPUT);
  digitalWrite(STEP_PIN, LOW);
  digitalWrite(DIR_PIN, ENDWARD);

}

void loop() {
  pollButtons();
  if (shouldStep) doStep();
  else delay(5);
}

static void pollButtons() {
  // update() will call buttonHandler() if PIN transitions to a new state and stays there
  // for multiple reads over 25+ ms.
  // Serial.print(digitalRead(JOY_U));
  // Serial.print(digitalRead(JOY_D));
  // Serial.print(digitalRead(JOY_L));
  // Serial.print(digitalRead(JOY_R));
  // Serial.print(digitalRead(JOY_C));
  // Serial.println();
  buttonU.update(digitalRead(JOY_U));
  buttonD.update(digitalRead(JOY_D));
  buttonL.update(digitalRead(JOY_L));
  buttonR.update(digitalRead(JOY_R));
  buttonC.update(digitalRead(JOY_C));
}

static void buttonHandler(uint8_t btnId, uint8_t btnState) {
  if (btnState == BTN_PRESSED) {
    Serial.print("Pushed button"); Serial.println(btnId);
    if (btnId == JOY_L || btnId == JOY_R) {
      digitalWrite(DIR_PIN, btnId == JOY_L ? ENDWARD : MOTORWARD);
      shouldStep = true;
      Serial.print("shouldStep"); Serial.println(shouldStep);
    }
    if (btnId == JOY_U) { 
      direction = MOTORWARD;
      Serial.println("Setting direction to motorward");
    }
    if (btnId == JOY_D) { 
      direction = ENDWARD;
      Serial.println("Setting direction to endward");
    }
  } else {
    // btnState == BTN_OPEN.
    Serial.print("Released button"); Serial.println(btnId);
    if (btnId == JOY_L || btnId == JOY_R) {
      shouldStep = false;
    }
  }
}

static void handlerC(uint8_t btnId, uint8_t btnState) {
  if (btnState == BTN_PRESSED) {
    // if (btnId == JOY_C) {                                     //ms 12- (there is no ms3 on a tmc2208 so this dip should do nothing?)
    //   const int steps = 24000;                                //  00: 20000 -> 62.47 millimeters, or .0031235 mm/step, or 160 steps per 0.5mm thread at 1/8 so 20 full steps per rev
    //   Serial.print("Stepping "); Serial.println(steps);       //   11: 40000 -> 62.91 millimeters, or .00157275 mm/step, or 318 steps per 0.5mm thread at 1/16 so 19.9 full steps per rev
    //   digitalWrite(DIR_PIN, direction);                       //   11: 48000 -> 75.60 millimeters, or .001575 mm/step, or 317.4 steps per 0.5mm thread
    //   delay(10);
    //   for (int i = 0; i<steps; i++) {
    //     if (i%1000==0) Serial.println(i);
    //     digitalWrite(STEP_PIN, HIGH);
    //     delayMicroseconds(40);
    //     digitalWrite(STEP_PIN, LOW);
    //     delayMicroseconds(40);
    //   }
    //   Serial.print("Done stepping "); Serial.println(steps);
    //}



  } else {
    Serial.print("Released button"); Serial.println(btnId);
  }
}


void doStep() {
  digitalWrite(STEP_PIN, HIGH);
  delayMicroseconds(40);
  digitalWrite(STEP_PIN, LOW);
  delayMicroseconds(40);
}