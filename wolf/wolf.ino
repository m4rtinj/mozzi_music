/*
  * Wolf. 
  * A Generative Synthesizer
  * 
  * 4 Potentiometers (A0, A1, A2, A4) + 1 Button (D3)
  * 
  * A0-Normal: Mix between bass and pad
  * A0-Button: Swing amount
  *
  * A1-Normal: Note length
  * A1-Button: Note change speed
  * 
  * A2-Normal: Base note
  * A2-Button: ADSR release time
  *
  * A4-Normal: Low pass filter cutoff
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
#include <LowPassFilter.h>
#include <mozzi_midi.h>
#include <ADSR.h>

// Configuration constants
//=============================================================================
#define CONTROL_RATE 64
#define MAX_NOTE_LENGTH 64 // in ticks; 4096 ms at CONTROL_RATE=64
#define MAX_STEP_LENGTH 64 // in ticks; 4096 ms at CONTROL_RATE=64
#define BUTTON_PIN 3                     
#define PRESSED LOW
#define MIN_ANALOG_CHANGE 3
#define REST 99
#define MELODY_LENGTH 16  // must be a power of 2 for easier modulo operation
#define BUTTON_DEBOUCER_BYTE 0x1F // 


// Mozzi oscillators + low pass filter
//=============================================================================
Oscil<SAW8192_NUM_CELLS, AUDIO_RATE> aPad(SAW8192_DATA); 
Oscil<SMOOTHSQUARE8192_NUM_CELLS, AUDIO_RATE> aBass(SMOOTHSQUARE8192_DATA);
//Oscil<SIN2048_NUM_CELLS, CONTROL_RATE> lfo(SIN2048_DATA);
//Oscil<SIN8192_NUM_CELLS, AUDIO_RATE> aBass(SIN8192_DATA);
LowPassFilter lpf;
ADSR<CONTROL_RATE, CONTROL_RATE> envelope;


// Scales with MIDI notes
//=============================================================================
const byte scaleLength = 12;
const byte numScales = 6;
const byte scales[numScales][scaleLength] = {
  {33, 36, 38, 40, 43, 45, 48, 50, 52, 55, 57, 60}, //
  {33, 35, 36, 38, 40, 41, 43, 45, 47, 48, 50, 51}, // 
  {33, 36, 38, 39, 40, 43, 45, 48, 50, 51, 52, 55}, // 
  {36, 38, 40, 42, 43, 45, 47, 50, 52, 54, 55, 57}, // 
  {33, 34, 36, 38, 40, 41, 43, 45, 46, 48, 50, 51}, // 
  {33, 35, 36, 38, 40, 42, 43, 45, 47, 48, 50, 52}  // 
};

// Steps for melody generation
//=============================================================================
const byte numOffsets = 9;
const int offsets[numOffsets] = {-2, 2, -1, -1, 1, 1, 0, REST, REST};


// Variables
//=============================================================================

// Read digital and analog inputs
int oldA0 = 0, oldA1 = 0, oldA2 = 0, oldA4 = 0;
byte buttonDebouncer = 0;
bool buttonState = HIGH;
bool lastButtonState = HIGH;
unsigned long lastButtonPressedTime = 0;
unsigned long shortPressThreshold = 800000; // 800 ms in microseconds

// Melody playback
unsigned int ticks = 0;
byte currentScale = 0; 
byte melody[MELODY_LENGTH];
byte melodyStep = 0;

// Configurable parameters
unsigned long mixPot = 512; // 0: only bass, 1023: only pad
unsigned int stepLength = 300; // in ticks
byte swing = 0; // 0: no swing, 128: 50% swing (max)
unsigned long env = 0;
unsigned long bassGain = 200; // 0: silent, 1023: full volume
unsigned long padGain = 200; // 0: silent, 1023: full volume
int noteLength = 100; // in ticks
byte noteOffset = 0; // MIDI note offset for the scale (0..20)
byte mutationSpeed = 20; // in ticks
bool noteActive = false;


// Functions
//=============================================================================

void setup() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);     
  pinMode(LED_BUILTIN, OUTPUT);          
  randomSeed(analogRead(A5));            
  startMozzi(CONTROL_RATE);
  lpf.setResonance(140); 
  envelope.setLevels(255, 255, 255, 0);
  envelope.setTimes(120, 10, 65535, 500);
  generateNewMelody();
 //  Serial.begin(9600);
}


byte generateNewNoteIndex(byte idx) {
  if (idx == REST) {
    return random(0, scaleLength); 
  }
  int step = offsets[random(0, numOffsets)];
  if (step == REST) {
    return REST; 
  } else {
    int newIndex = idx + step;
    if (newIndex < 0) {
      newIndex = 0;    
    }
    if (newIndex >= scaleLength){
      newIndex = scaleLength-1;
    }
    return newIndex;
  }
}


void generateNewMelody() {
  byte noteIdx = random(0, scaleLength);
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
  if (melody[melodyStep] != REST) {
    aPad.setFreq(mtof(noteOffset scales[currentScale][melody[melodyStep]]));
    aBass.setFreq(mtof(noteOffset + scales[currentScale][melody[melodyStep]])); 
    envelope.noteOn();
    noteActive = true;
  } else {
  //  envelope.noteOff();
  //  noteActive = false;
  }
  melodyStep = (melodyStep + 1) % MELODY_LENGTH;
}


void updateControl() {
  
  // Button handling
  buttonDebouncer = ((buttonDebouncer << 1) | (uint8_t)(!digitalRead(BUTTON_PIN))) & BUTTON_DEBOUCER_BYTE;
  lastButtonState = buttonState;
  if (buttonDebouncer ==  BUTTON_DEBOUCER_BYTE) {
    buttonState = PRESSED;
  } else if (buttonDebouncer == 0) {
    buttonState = !PRESSED;
  }  
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
  if (abs(newA0 - oldA0) > MIN_ANALOG_CHANGE) {
    oldA0 = newA0;
    if (buttonState == PRESSED) {
      swing = map(newA0, 0, 1023, 0, 128);
    } else {
      mixPot = newA0; 
    } 
  }

  // handle A1 with or without button press - only if A1 was moved! (using oldA1)
  int newA1 = mozziAnalogRead(A1);
  if (abs(newA1 - oldA1) > MIN_ANALOG_CHANGE) {
    oldA1 = newA1;
    if (buttonState == PRESSED) {
      stepLength = map(newA1, 0, 1023, 5, MAX_STEP_LENGTH);
    } else {
      noteLength = map(newA1, 0, 1023, 5, MAX_NOTE_LENGTH);
    }
  }

  // handle A2 with or without button press - only if A2 was moved! (using oldA2)
  int newA2 = mozziAnalogRead(A2);
  if (abs(newA2 - oldA2) > MIN_ANALOG_CHANGE) {
    oldA2 = newA2;
    if (buttonState == PRESSED) {
      envelope.setReleaseTime( newA2 ); 
    } else {
      noteOffset = map( newA2, 0, 1023, 0, 20 );  //0..255 
    }
  }

  // handle A4 with or without button press - only if A4 was moved! (using oldA4)
  int newA4 = mozziAnalogRead(A4);
  if (abs(newA4 - oldA4) > MIN_ANALOG_CHANGE) {
    oldA4 = newA4;
    if (buttonState ==  PRESSED) {
      mutationSpeed = map(newA4, 0, 1023, 0, 255); // in ticks
    } else {
      lpf.setCutoffFreq(map(newA4, 0, 1023, 10, 245)); 
    }
  }


  envelope.update();

  // Note change timing
  ticks++;

  int swingOffset = (swing * stepLength) >> 8;
  int finaltick = stepLength + ((melodyStep % 2 == 0) ? swingOffset : -swingOffset);
  
  if (noteActive && (ticks >= noteLength)) {
    envelope.noteOff();
    noteActive = false;
    //Serial.println(9999);
  }

  if (ticks >= finaltick) {
    ticks = 0;
    if (random(5, 250) < mutationSpeed) {
      mutateMelody();
    }
    nextNote();
  }
  env = envelope.next(); 
  bassGain = ((1023 - mixPot) * env) >> 10;
  padGain = (env * mixPot) >> 10;
  // Serial.print(env);
  // Serial.print("  ");
  // Serial.print(mixPot);
  // Serial.print("  ");
  // Serial.print(bassGain);
  // Serial.println("  ");

}

int updateAudio() {
  long mix = ((aBass.next() * bassGain) + (aPad.next() * padGain)) >> 9;  
  return lpf.next((int)mix);
}

void loop() {
  audioHook();
}
