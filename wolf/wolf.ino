/*
 * Generative Ambient Synthesizer
 * Hardware: 4 Potentiometers (A0, A1, A2, A4) + 1 Button (D3)
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


Oscil<SAW8192_NUM_CELLS, AUDIO_RATE> aPad(SAW8192_DATA); 
Oscil<SMOOTHSQUARE8192_NUM_CELLS, AUDIO_RATE> aBass(SMOOTHSQUARE8192_DATA);
LowPassFilter lpf;

Oscil<SIN2048_NUM_CELLS, CONTROL_RATE> lfoBass(SIN2048_DATA);
Oscil<SIN2048_NUM_CELLS, CONTROL_RATE> lfoPad(SIN2048_DATA);

byte currentScale = 0; 
const byte scaleSize = 14;
const byte numScales = 5;

const byte scales[numScales][scaleSize] = {
  const byte scales[numScales][scaleSize] = {
  {33, 35, 36, 38, 40, 42, 43, 45, 47, 48, 50, 52, 54, 55}, // 0: A-Dorian
  {33, 34, 36, 38, 40, 41, 43, 45, 46, 48, 50, 51, 53, 54}, // 1: A-Phrygian
  {36, 38, 40, 42, 43, 45, 47, 50, 52, 54, 55, 57, 59, 62}, // 2: C-Lydian (felülről nyitott)
  {33, 35, 36, 38, 40, 42, 43, 45, 47, 48, 50, 52, 54, 55}, // 0: A-Dorian
  {33, 34, 36, 38, 40, 41, 43, 45, 46, 48, 50, 51, 53, 54}  // 1: A-Phrygian
};

byte melody[16];

int mixPot, lfoTempoPot, noteTempoPot, filterPot;
unsigned int timeCounter = 0;
unsigned int changeThreshold = 300; 
byte noteIndex = 0;

long bassGain = 0;
long padGain = 0;
bool lastButtonState = HIGH;
unsigned long lastButtonDebounceTime = 0;
unsigned long debounceDelay = 50;

byte potIndex = 0; 

void setup() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);     
  pinMode(LED_BUILTIN, OUTPUT);          
  
  randomSeed(analogRead(A4));            
  startMozzi(CONTROL_RATE);
  
  lfoBass.setFreq(0.25f);    
  lfoPad.setFreq(0.15f); 
  
  lpf.setResonance(180); 

  currentScale = 0;
  generateNewNotes();
}



void generateNewNotes() {
  aPad.setFreq(mtof(scales[currentScale][random(scaleSize/2-2, scaleSize)]));
  aBass.setFreq(mtof(scales[currentScale][random(0, scaleSize/2+2)]));
}

generateNewMelody() {
  for (int i = 0; i < 16; i++) {
    if (random(0, 100) < 30) { 
      melody[i] = 0; 
    } else {
    melody[i] = scales[currentScale][random(0, scaleSize)];
  }
}


void nextNote(){
  if (melody[noteIndex] != 0) {
    aPad.setFreq(mtof(melody[noteIndex]));
    aBass.setFreq(mtof(melody[noteIndex] - 12)); 
  }
  noteIndex = (noteIndex + 1) % 16;
}


void updateControl() {
  int currentButtonState = digitalRead(BUTTON_PIN);
  if (currentButtonState != lastButtonState){
    lastButtonState = mozziMicros(); 
  }
  if ((mozziMicros() - lastButtonDebounceTime) > debounceDelay) {
    if (currentButtonState != lastButtonState){
      lastButtonDebounceTime = mozziMicros();
      lastButtonState = currentButtonState;
      if (currentButtonState == LOW){
        currentScale = (currentScale + 1) % numScales; 
        generateNewMelody();
        digitalWrite(LED_BUILTIN, HIGH);
      }
    }
  }
  if (currentButtonState == HIGH) { digitalWrite(LED_BUILTIN, LOW); }

  
  mixPot = mozziAnalogRead(A0);
  lfoTempoPot = mozziAnalogRead(A1);
  noteTempoPot = mozziAnalogRead(A2);
  filterPot = mozziAnalogRead(A4);
  
  byte cutoffValue = map(filterPot, 0, 1023, 10, 245);
  lpf.setCutoffFreq(cutoffValue);

  float lfoSpeed = map(lfoTempoPot, 0, 1023, 1, 400) / 100.0f; 
  lfoBass.setFreq(lfoSpeed);
  lfoPad.setFreq(lfoSpeed * 0.6f); 
  
  timeCounter++;
  changeThreshold = map(noteTempoPot, 0, 1023, 600, 4); 
  
  long bassVolumeMod = map(lfoBass.next(), -127, 128, 40, 255);
  long padVolumeMod = map(lfoPad.next(), -127, 128, 20, 255);
  
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
  return lpf.next((int)mix);
}

void loop() {
  audioHook();
}
