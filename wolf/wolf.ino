 /*
  * Wolf. 
  * A Generative Synthesizer
  * 
  * 4 Potentiometers (A0, A1, A2, A4) + 1 Button (D3)
  * 
  * A0-Normal: Mix ( Bass <==>Pad )
  * A0-Button: Change scale
  * 
  * A1-Normal: Note change speed (200 - 4)
  * A1-Button: Shift melody )
  * 
  * A2-Normal: Pad note length (5 - 250 ms)
  * A2-Button: Bass note length (5 - 250 ms) * 2
  * 
  * A4-Normal: LPF cutoff (10 - 245)
  * A4-Button: Mutation speed
  * 
  * Button double press: generate new melody + change scale
  *  
  */

#include <MozziGuts.h>
#include <Oscil.h>
#include <LowPassFilter.h>
#include <tables/sin2048_int8.h>
#include <tables/saw8192_int8.h>
//#include <tables/sin8192_int8.h>
#include <tables/smoothsquare8192_int8.h> 
#include <mozzi_midi.h>
//#include <EventDelay.h> 

#define CONTROL_RATE 64

#define MAX_NOTE_LENGTH CONTROL_RATE * 16 // 1024 ms at CONTROL_RATE of 64
#define MAX_STEP_LENGTH CONTROL_RATE * 16 // 1024 ms at CONTROL_RATE of 64

#define BUTTON_PIN 3                     
#define PRESSED LOW
#define minAnalogChange 3
#define REST 99
#define MELODY_LENGTH 16

Oscil<SAW8192_NUM_CELLS, AUDIO_RATE> aPad(SAW8192_DATA); 
//Oscil<SIN8192_NUM_CELLS, AUDIO_RATE> aBass(SIN8192_DATA);
Oscil<SMOOTHSQUARE8192_NUM_CELLS, AUDIO_RATE> aBass(SMOOTHSQUARE8192_DATA);
LowPassFilter lpf;

Oscil<SIN2048_NUM_CELLS, CONTROL_RATE> lfoBass(SIN2048_DATA);
Oscil<SIN2048_NUM_CELLS, CONTROL_RATE> lfoPad(SIN2048_DATA);

byte currentScale = 0; 
const byte scaleSize = 14;
const byte numScales = 6;

const byte scales[numScales][scaleSize] = {
    {33, 35, 36, 38, 40, 42, 43, 45, 47, 48, 50, 52, 54, 55}, // G-dur / A-dor
    {33, 34, 36, 38, 40, 41, 43, 45, 46, 48, 50, 51, 53, 54}, // Szintetikus
    {36, 38, 40, 42, 43, 45, 47, 50, 52, 54, 55, 57, 59, 62}, // C-lid
    {33, 35, 36, 38, 40, 41, 43, 45, 47, 48, 50, 51, 53, 55}, // A-moll
    {33, 36, 38, 40, 43, 45, 48, 50, 52, 55, 57, 60, 62, 64}, // A-pentaton
    {33, 36, 38, 39, 40, 43, 45, 48, 50, 51, 52, 55, 57, 60}  // A-blues
};

// Step probability array for melody generation and mutation
byte stepsSize = 9;
const int steps[] = {-2, 2, -1, -1, 1, 1, 0, REST, REST};
int step;

// Melody array includes note indexes and not the notes! 
byte melody[MELODY_LENGTH];

int oldA0 = 0, oldA1 = 0, oldA2 = 0, oldA4 = 0;
int mixPot, lfoPot, bpmPo, lpfPot;

byte buttonDebouncer = 0;
bool buttonState = HIGH;
bool lastButtonState = HIGH;
unsigned long lastButtonPressedTime = 0;
unsigned long shortPressThreshold = 800000; // 200 ms

unsigned int ticks = 0;
unsigned int stepLength = 300; 
byte noteIndex = 0;

long bassGain = 200;
long padGain = 200;
int bassNotelength = 200;
int padNotelength = 200;

byte mutationSpeed = 20; // in ticks

void setup() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);     
  pinMode(LED_BUILTIN, OUTPUT);          
  
  randomSeed(analogRead(A5));            
  startMozzi(CONTROL_RATE);
  
  lfoBass.setFreq(0.25f);    
  lfoPad.setFreq(0.15f); 
  
  lpf.setResonance(180); 

  currentScale = 0;
  generateNewMelody();

}

void generateNewMelody() {
  byte idx = 0; //random(0, scaleSize);
  for (int i = 0; i < MELODY_LENGTH; i++) {
    step = steps[random(0, stepsSize)];
    if (step == REST) {
      melody[i] = REST; 
    } else {
      idx = (idx + step);
      if (idx < 0) {
        idx = 0;
      }
      if (idx >= scaleSize){
        idx = scaleSize-1;
      }
      melody[i] = idx;
    }
  }
}

void mutateMelody() {
  byte idx = random(0, MELODY_LENGTH);
  step = steps[random(0, stepsSize)];
  if (step == REST) {
    melody[idx] = REST; 
  } else {
    if (melody[idx] = REST) { 
      melody[idx] = random(0, scaleSize); // If it's currently a pause, change it to a random note
    } else {
      int newIdx = melody[idx] + step;
      if (newIdx < 0) {
        newIdx = 0;    
      }
      if (newIdx >= scaleSize){
        newIdx = scaleSize-1;
      }
      melody[idx] = newIdx;
    }
  }
}

void nextNote(){
  if (melody[noteIndex] != REST) {
    aPad.setFreq(mtof(scales[currentScale][melody[noteIndex]]));
    aBass.setFreq(mtof(scales[currentScale][melody[noteIndex]])); 
  }
  noteIndex = (noteIndex + 1) % 16;
}

void updateButtonState(){
  buttonDebouncer = ((buttonDebouncer << 1) | (uint8_t)(!digitalRead(BUTTON_PIN))) & 0xFF;
  lastButtonState = buttonState;
  if (buttonDebouncer == 0xFF) {
    buttonState = PRESSED;
  } else if (buttonDebouncer == 0x00) {
    buttonState = !PRESSED;
  }
}

void updateControl() {
  
  // Button handling
  updateButtonState();
  if (buttonState == PRESSED && lastButtonState == !PRESSED) {
    if (mozziMicros() - lastButtonPressedTime < shortPressThreshold) { 
      currentScale = (currentScale + 1) % numScales;
      generateNewMelody(); 
    }
    lastButtonPressedTime = mozziMicros();
  }
  digitalWrite(LED_BUILTIN, buttonState==PRESSED);


  // handle A0 with or without button press - only if A0 was moved! (using oldA0)
  int newA0 = mozziAnalogRead(A0);
  if (abs(newA0 - oldA0) > minAnalogChange) {  // Only update if the value has changed significantly
    oldA0 = newA0;  // Update oldA0 to the new value 
    if (buttonState == PRESSED) {
      mixPot = newA0 * 2; 
    } else {
      mixPot = newA0; 
    } 
  }

  // handle A1 with or without button press - only if A1 was moved! (using oldA1)
  int newA1 = mozziAnalogRead(A1);
  if (abs(newA1 - oldA1) > minAnalogChange) {  // Only update if the value has changed significantly
    oldA1 = newA1;  // Update oldA1 to the new value 
    if (buttonState == PRESSED) {
      stepLength = map(newA1, 0, 1023, 200, 4) * 2; 
    } else {
      stepLength = map(newA1, 0, 1023, 200, 4); 
    }
  }

  // handle A2 with or without button press - only if A2 was moved! (using oldA2)
  int newA2 = mozziAnalogRead(A2);
  if (abs(newA2 - oldA2) > minAnalogChange) {  // Only update if the value has changed significantly
    oldA2 = newA2;  // Update oldA2 to the new value 
    if (buttonState == PRESSED) {
      bassNotelength = map(newA2, 0, 1023, 5, 250);
      padNotelength = map(newA2, 0, 1023, 5, 250); 
    } else {
      bassNotelength = map(newA2, 0, 1023, 5, 250);
      padNotelength = map(newA2, 0, 1023, 5, 250); 
    }
  }

  // handle A4 with or without button press - only if A4 was moved! (using oldA4)
  int newA4 = mozziAnalogRead(A4);
  if (abs(newA4 - oldA4) > minAnalogChange) {  // Only update if the value has changed significantly
    oldA4 = newA4;  // Update oldA4 to the new value 
    if (buttonState ==  PRESSED) {
      mutationSpeed = map(newA4, 0, 1023, 0, 255); // in ticks
    } else {
      lpf.setCutoffFreq(map(newA4, 0, 1023, 10, 245)); 
    }
  }
  
 
  // LFO modulation
  // long bassVolumeMod = map(lfoBass.next(), -127, 128, 40, 255);
  // long padVolumeMod = map(lfoPad.next(), -127, 128, 20, 255);
  
  // Note change timing
  ticks++;
  if (ticks >= stepLength) {
    ticks = 0;
    if (random(5, 250) < mutationSpeed) {
      mutateMelody();
    }
    nextNote();
  }

  bassGain = (1023 - mixPot) >> 2;
  padGain = mixPot >> 2;
  
  if (ticks > bassNotelength) {
    bassGain = 0; 
  }
  if (ticks > padNotelength) {
    padGain = 0; 
  }
  if (melody[noteIndex] == REST) {
    //bassGain = 0; 
    padGain = 0; 
  }
}

int updateAudio() {
  long mix = ((aBass.next() * bassGain) + (aPad.next() * padGain)) >> 9;  
  return lpf.next((int)(mix));
}

void loop() {
  audioHook();
}
