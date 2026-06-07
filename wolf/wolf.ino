 /*
  * Wolf. 
  * A Generative Synthesizer
  * 
  * 4 Potentiometers (A0, A1, A2, A4) + 1 Button (D3)
  * 
  * A0-Normal: Mix ( Bass <==> Pad )
  * A0-Button: Swing (0 - 50% of step length)
  * 
  * A1-Normal: Note change speed (200 - 4)
  * A1-Button: Shift melody 
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
#include <tables/sin2048_int8.h>
#include <tables/saw8192_int8.h>
#include <tables/smoothsquare8192_int8.h> 
//#include <tables/sin8192_int8.h>
#include <LowPassFilter.h>
#include <mozzi_midi.h>


// Configuration constants
//=============================================================================
#define CONTROL_RATE 64
#define MAX_NOTE_LENGTH CONTROL_RATE * 16 // 1024 ms at CONTROL_RATE of 64
#define MAX_STEP_LENGTH CONTROL_RATE * 16 // 1024 ms at CONTROL_RATE of 64
#define BUTTON_PIN 3                     
#define PRESSED LOW
#define minAnalogChange 3
#define REST 99
#define MELODY_LENGTH 16
#define BUTTON_DEBOUCER_BYTE 0x1F // 


// Mozzi oscillators + low pass filter
//=============================================================================
Oscil<SAW8192_NUM_CELLS, AUDIO_RATE> aPad(SAW8192_DATA); 
Oscil<SMOOTHSQUARE8192_NUM_CELLS, AUDIO_RATE> aBass(SMOOTHSQUARE8192_DATA);
Oscil<SIN2048_NUM_CELLS, CONTROL_RATE> lfoBass(SIN2048_DATA);
Oscil<SIN2048_NUM_CELLS, CONTROL_RATE> lfoPad(SIN2048_DATA);
//Oscil<SIN8192_NUM_CELLS, AUDIO_RATE> aBass(SIN8192_DATA);
LowPassFilter lpf;


// Scales with MIDI notes
//=============================================================================
const byte scaleSize = 14;
const byte numScales = 6;
const byte scales[numScales][scaleSize] = {
  {33, 36, 38, 40, 43, 45, 48, 50, 52, 55, 57, 60, 62, 64}, // A-pentaton
  {33, 35, 36, 38, 40, 41, 43, 45, 47, 48, 50, 51, 53, 55}, // A-moll
  {33, 36, 38, 39, 40, 43, 45, 48, 50, 51, 52, 55, 57, 60}, // A-blues
  {36, 38, 40, 42, 43, 45, 47, 50, 52, 54, 55, 57, 59, 62}, // C-lid
  {33, 34, 36, 38, 40, 41, 43, 45, 46, 48, 50, 51, 53, 54}, // Szintetikus
  {33, 35, 36, 38, 40, 42, 43, 45, 47, 48, 50, 52, 54, 55}  // G-dur / A-dor
};

// 
//=============================================================================
byte stepsSize = 9;
const int steps[] = {-2, 2, -1, -1, 1, 1, 0, REST, REST};


// Variables
//=============================================================================
byte melody[MELODY_LENGTH];

byte currentScale = 0; 
int step;

int oldA0 = 0, oldA1 = 0, oldA2 = 0, oldA4 = 0;
int mixPot;

byte buttonDebouncer = 0;
bool buttonState = HIGH;
bool lastButtonState = HIGH;
unsigned long lastButtonPressedTime = 0;
unsigned long shortPressThreshold = 800000; // 200 ms

unsigned int ticks = 0;
unsigned int stepLength = 300; 
byte swing = 0;
byte noteIndex = 0;

long bassGain = 200;
long padGain = 200;
int bassNotelength = 200;
int padNotelength = 200;

byte mutationSpeed = 20; // in ticks


// Functions
//=============================================================================

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

byte generateNewNoteIndex(byte idx) {
  if (idx == REST) {
    return random(0, scaleSize); 
  }
  int step = steps[random(0, stepsSize)];
  if (step == REST) {
    return REST; 
  } else {
    int newIndex = idx + step;
    if (newIndex < 0) {
      newIndex = 0;    
    }
    if (newIndex >= scaleSize){
      newIndex = scaleSize-1;
    }
    return newIndex;
  }
}


void generateNewMelody() {
  byte noteIdx = random(0, scaleSize);
  for (int i = 0; i < MELODY_LENGTH; i++) {
    noteIdx = generateNewNoteIndex(noteIdx);
    melody[i] = noteIdx;
  }
}


void mutateMelody() {
  byte pos = random(0, MELODY_LENGTH);
  byte idx = melody[pos];
  melody[pos] = generateNewNoteIndex(idx);
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
      swing = map(newA0, 0, 1023, 0, 128);
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

  int swingOffset = (swing * stepLength) >> 8; // Calculate swing offset based on the swing value
  int finaltick = stepLength + ((noteIndex % 2 == 0) ? swingOffset : -swingOffset); // Adjust step length based on swing and note index
  if (ticks >= finaltick) {
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
