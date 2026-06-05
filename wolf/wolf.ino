 /*
  * Wolf. Generative Synthesizer
  * 4 Potentiometers (A0, A1, A2, A4) + 1 Button (D3)
  */

#include <MozziGuts.h>
#include <Oscil.h>
#include <LowPassFilter.h>
#include <tables/sin2048_int8.h>
#include <tables/saw8192_int8.h>
#include <tables/smoothsquare8192_int8.h> 
#include <mozzi_midi.h>
#include <EventDelay.h> 

#define CONTROL_RATE 128
#define BUTTON_PIN 3                     
#define PRESSED LOW
#define minAnalogChange 10

Oscil<SAW8192_NUM_CELLS, AUDIO_RATE> aPad(SAW8192_DATA); 
Oscil<SMOOTHSQUARE8192_NUM_CELLS, AUDIO_RATE> aBass(SMOOTHSQUARE8192_DATA);
LowPassFilter lpf;

Oscil<SIN2048_NUM_CELLS, CONTROL_RATE> lfoBass(SIN2048_DATA);
Oscil<SIN2048_NUM_CELLS, CONTROL_RATE> lfoPad(SIN2048_DATA);

byte currentScale = 0; 
const byte scaleSize = 14;
const byte numScales = 5;

const byte scales[numScales][scaleSize] = {
  {33, 35, 36, 38, 40, 42, 43, 45, 47, 48, 50, 52, 54, 55}, // 0: A-Dorian
  {33, 34, 36, 38, 40, 41, 43, 45, 46, 48, 50, 51, 53, 54}, // 1: A-Phrygian
  {36, 38, 40, 42, 43, 45, 47, 50, 52, 54, 55, 57, 59, 62}, // 2: C-Lydian (felülről nyitott)
  {33, 35, 36, 38, 40, 42, 43, 45, 47, 48, 50, 52, 54, 55}, // 0: A-Dorian
  {33, 34, 36, 38, 40, 41, 43, 45, 46, 48, 50, 51, 53, 54}  // 1: A-Phrygian
};

byte melody[16];

int oldA0 = 0, oldA1 = 0, oldA2 = 0, oldA4 = 0;
int mixPot, lfoPot, bpmPo, lpfPot;

byte buttonDebouncer = 0;
bool buttonState = HIGH;
bool lastButtonState = HIGH;
unsigned long lastButtonPressedTime = 0;
unsigned long shortPressThreshold = 800000; // 200 ms

unsigned int timeCounter = 0;
unsigned int changeThreshold = 300; 
byte noteIndex = 0;

long bassGain = 0;
long padGain = 0;

void setup() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);     
  pinMode(LED_BUILTIN, OUTPUT);          
  
  randomSeed(analogRead(A4));            
  startMozzi(CONTROL_RATE);
  
  lfoBass.setFreq(0.25f);    
  lfoPad.setFreq(0.15f); 
  
  lpf.setResonance(180); 

  currentScale = 0;
  generateNewMelody();

}

void generateNewMelody() {
  for (int i = 0; i < 16; i++) {
    if (random(0, 100) < 30) { 
      melody[i] = 0; 
    } else {
      melody[i] = scales[currentScale][random(0, scaleSize)];
    }
  }
}

void nextNote(){
  if (melody[noteIndex] != 0) {
    aPad.setFreq(mtof(melody[noteIndex]));
    aBass.setFreq(mtof(melody[noteIndex] - 12)); 
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
      digitalWrite(LED_BUILTIN, HIGH);
    }
    lastButtonPressedTime = mozziMicros();
  }
  if (buttonState == !PRESSED) { digitalWrite(LED_BUILTIN, LOW); }
  
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
      changeThreshold = map(newA1, 0, 1023, 600, 4) * 2; 
    } else {
      changeThreshold = map(newA1, 0, 1023, 600, 4); 
    }
  }

 // handle A2 with or without button press - only if A2 was moved! (using oldA2)
  int newA2 = mozziAnalogRead(A2);
  if (abs(newA2 - oldA2) > minAnalogChange) {  // Only update if the value has changed significantly
    oldA2 = newA2;  // Update oldA2 to the new value 
    if (buttonState == PRESSED) {
      lfoBass.setFreq(map(newA2, 0, 1023, 1, 400) / 100.0f * 2); 
      lfoPad.setFreq(map(newA2, 0, 1023, 1, 400) / 100.0f * 1.2f); 
    } else {
      lfoBass.setFreq(map(newA2, 0, 1023, 1, 400) / 100.0f); 
      lfoPad.setFreq(map(newA2, 0, 1023, 1, 400) / 100.0f * 0.6f); 
    }
  }

  // handle A4 with or without button press - only if A4 was moved! (using oldA4)
  int newA4 = mozziAnalogRead(A4);
  if (abs(newA4 - oldA4) > minAnalogChange) {  // Only update if the value has changed significantly
    oldA4 = newA4;  // Update oldA4 to the new value 
    if (buttonState ==  PRESSED) {
      lpf.setCutoffFreq(map(newA4, 0, 1023, 10, 245) * 2); 
    } else {
      lpf.setCutoffFreq(map(newA4, 0, 1023, 10, 245)); 
    }
  }
  
 
  // LFO modulation
  long bassVolumeMod = map(lfoBass.next(), -127, 128, 40, 255);
  long padVolumeMod = map(lfoPad.next(), -127, 128, 20, 255);
  
  // Note change timing
  timeCounter++;
  if (timeCounter >= changeThreshold) {
    timeCounter = 0;
    nextNote();
  }
  
  long bassRatio = 1023 - mixPot;
  long padRatio = mixPot;
  
  bassGain = (bassVolumeMod * bassRatio) >> 10;
  padGain = (padVolumeMod * padRatio) >> 10;
}

int updateAudio() {
  long mix = ((aBass.next() * bassGain) + (aPad.next() * padGain)) >> 9;  
  return lpf.next((int)(mix));
}

void loop() {
  audioHook();
}
