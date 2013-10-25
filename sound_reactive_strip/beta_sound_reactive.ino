#include <QueueList.h>
#include "LPD8806.h"
#include "SPI.h"

typedef struct RGB {
  byte r;
  byte g;
  byte b;
  
  RGB(byte R=0, byte G=0, byte B=0): r(R), g(G), b(B){}
};

const int nLEDs = 50;
LPD8806 strip = LPD8806(nLEDs);
boolean cells[nLEDs];
boolean newCells[nLEDs];
RGB colors[nLEDs];
RGB rainbow[6] = {RGB(127, 0, 0), RGB(127, 127, 0), RGB(0, 127, 0), RGB(0, 127, 127), RGB(0, 0, 127), RGB(127, 0, 127)};

// min and max values the microphone can send
const float MAX = 1024;
const float MIN = 0;

const int sampleWindow = 50; // Sample window width in mS (50 mS = 20Hz)
unsigned int sample;
int minV = MAX;
int maxV = MIN;
int range = 0;
int maxSamples = 100;
QueueList<int> recentSamples;
int micPin = 5;
int iterations = 0;
int maxIterations = 500;
int colorDuration = 100; // in number of iterations (steps)
int maxColorDuration = 100;
int rainbowDuration = 30;
int maxRainbowDuration = 50;
int curRainbowColor = 0; // start at red
int targetRainbowColor = 1; // proceed to orange
boolean reverseRainbow = false;
RGB curRgb;
RGB nextRgb;
RGB startLeftRgb;
RGB startRightRgb;
RGB curLeftRgb;
RGB curRightRgb;
RGB nextLeftRgb;
RGB nextRightRgb;
int numPatterns = 6;
int curPattern = 0;
int numColorStrats = 7;
int curColorStrat = 0;
boolean reverse110 = false;
boolean reverseFillPercentage = false;
float brightness = 1.0;

void setup() {
  strip.begin();

  strip.show();

  Serial.begin(9600);

  randomSeed(analogRead(0));
  curPattern = random(numPatterns);
  curColorStrat = random(curColorStrat);
}

void randomizeRGB(void* rgbVoid) {
  // workaround based on http://forum.arduino.cc/index.php/topic,41848.0.html
  RGB* rgb = (RGB*) rgbVoid;
  int cap = 128;
  byte red = random(cap);
  cap = cap - red;
  byte green = random(cap);
  cap = cap - green;
  byte blue = cap;
  
  rgb->r = red;
  rgb->b = blue;
  rgb->g = green;
}

void initCellsForRule110() {
  int seed = random(nLEDs);
  for(int i = 0; i < nLEDs; i++) {
    if(i == seed)
      cells[i] = true;
    else
      cells[i] = false;
  }
}

void initCellsForParticle() {
  cells[0] = true;
  newCells[0] = false;
  for(int i = 1; i < nLEDs; i++) {
    cells[i] = false;
    newCells[i] = false;
  }
}

boolean isAliveRule110(boolean left, boolean mid, boolean right) {
  if(reverse110){
    boolean temp = left;
    left = right;
    right = temp;
  }
  return (!mid && right) ||
         ( mid && (!left || !right)); 
}
 
void calculateNextGeneration110() {    
  // take care of the cases where we have to wrap around the strip
  newCells[0] = isAliveRule110(cells[nLEDs - 1], cells[0], cells[1]);
  newCells[nLEDs - 1] = isAliveRule110(cells[nLEDs - 2], cells[nLEDs - 1], cells[0]);  

  // everything else
  for(int i = 1; i < nLEDs - 1; i++) {
    newCells[i] = isAliveRule110(cells[i - 1], cells[i], cells[i + 1]);
  }
}

void loop() {
  if(iterations == 0) {
    curPattern = random(numPatterns);
    switch(curPattern) {
      case 0:
        break;
      case 1:
        initCellsForRule110();
        reverse110 = !reverse110;
        break;
      case 2:
        break;
      case 3:
        reverseFillPercentage = !reverseFillPercentage;
        break;
      case 4:
        initCellsForParticle();
        break;
      case 5:
        break;
    }

    // initializations for the various color strategies
    curColorStrat = random(numColorStrats);
    switch(curColorStrat) {
      case 0:
        break;
      case 1:
        break;
      case 2:
        break;
      case 3:
        randomizeRGB(&curRgb);
        randomizeRGB(&nextRgb);
        colorDuration = 1 + random(maxColorDuration);
        break;
      case 4:
        reverseRainbow = !reverseRainbow;
        curRainbowColor = random(6);
        if(reverseRainbow) {
          targetRainbowColor = curRainbowColor - 1;
          if(targetRainbowColor == -1)
            targetRainbowColor = 5;
        }
        else {
          targetRainbowColor = (curRainbowColor + 1) % 6;
        }
        rainbowDuration = 1 + random(maxRainbowDuration);
        break;
      case 5:
        randomizeRGB(&startLeftRgb);
        curLeftRgb.r = startLeftRgb.r;
        curLeftRgb.g = startLeftRgb.g;
        curLeftRgb.b = startLeftRgb.b;        
        randomizeRGB(&startRightRgb);
        curRightRgb.r = startRightRgb.r;
        curRightRgb.g = startRightRgb.g;
        curRightRgb.b = startRightRgb.b;        
        randomizeRGB(&nextLeftRgb);        
        randomizeRGB(&nextRightRgb);
        colorDuration = 1 + random(maxColorDuration);        
        break;
      case 6:
        randomizeRGB(&startLeftRgb);
        curLeftRgb.r = startLeftRgb.r;
        curLeftRgb.g = startLeftRgb.g;
        curLeftRgb.b = startLeftRgb.b;        
        randomizeRGB(&startRightRgb);
        curRightRgb.r = startRightRgb.r;
        curRightRgb.g = startRightRgb.g;
        curRightRgb.b = startRightRgb.b;        
        randomizeRGB(&nextLeftRgb);        
        randomizeRGB(&nextRightRgb);
        colorDuration = 1 + random(maxColorDuration);        
        break;
    }
  }

  boolean setNewMin = false;
  boolean setNewMax = false;
  boolean minOrMaxSet = false;
  int volts = sampleSound();

  // need to pop if the queue is full
  if(recentSamples.count() == maxSamples) {
    int removedVal = recentSamples.pop();    
    if(removedVal == minV) {
      minV = MAX;      
      setNewMin = true;
    }
    if(removedVal == maxV) {
      maxV = MIN;
      setNewMax = true;
    }
  }

  // always need to push the new value on to the queue
  recentSamples.push(volts);

  if(setNewMin || setNewMax) {
    scanQueueForNewMinAndOrMax();
  }
  else
  {
    minOrMaxSet = checkAndSetMinMax(volts);
  }

  boolean adjustedRange = setNewMin || setNewMax || minOrMaxSet;
  range = maxV - minV;

  if(range > 0) {
    float percent = (volts - minV) / (float) range;
    
    switch(curColorStrat) {
      case 0:
        if(minOrMaxSet) {
          randomizeRGB(&curRgb);
          for(int i = 0; i < nLEDs; i++) {
            colors[i] = curRgb;
          }
        }
        break;
      case 1:
        setColorsToRainbowByPercent(percent);
        break;
      case 2:
        setColorsToRainbowByIterations(iterations);
        break;
      case 3:
        fadeToTargetColorRandom(iterations, colorDuration);
        break;
      case 4:
        fadeThroughRainbow(iterations, rainbowDuration);
        break;
      case 5:
        fadeBetweenTwoColorGradient(iterations, colorDuration);
        break;
      case 6:
        fadeBetweenTwoColorGradient(iterations, colorDuration, percent);
        break;        
    }

    switch(curPattern) {
      case 0:
        lightPercentageOfStripRandom(percent);
        break;
      case 1:
        lightStripByRule110(percent);
        break;
      case 2:
        lightWholeStripAtPercentage(percent);
        break;
      case 3:
        fillPercentageOfStrip(percent, reverseFillPercentage);
        break;
      case 4:
        if(adjustedRange) {
          cells[0] = true;
        }
        moveParticle(percent);
        break;
      case 5:
        if(random(50) == 0)
          reverseFillPercentage = !reverseFillPercentage;
        fillPercentageOfStrip(percent, reverseFillPercentage);
        break;        
    }

    // only increment the iterations if we actually ran a pattern
    iterations++;
    if(iterations >= maxIterations) {
      iterations = 0;
    }
  }
}

void fadeToTargetColorRandom(int iterations, int duration) {
  int t = iterations % duration;
  if(t == 0) {
    curRgb.r = nextRgb.r;
    curRgb.g = nextRgb.g;
    curRgb.b = nextRgb.b;    
    randomizeRGB(&nextRgb);
  }
  for(int i = 0; i < nLEDs; i++) {
    colors[i].r = curRgb.r + t * (nextRgb.r - curRgb.r) / duration;
    colors[i].g = curRgb.g + t * (nextRgb.g - curRgb.g) / duration;
    colors[i].b = curRgb.b + t * (nextRgb.b - curRgb.b) / duration;  
  }
}

void fadeThroughRainbow(int iterations, int duration) {
  int t = iterations % duration;
  if(t == 0) {
    if(reverseRainbow) {
      curRainbowColor--;
      if(curRainbowColor == -1)
        curRainbowColor = 5;   
      targetRainbowColor = curRainbowColor - 1;
      if(targetRainbowColor == -1)
        targetRainbowColor = 5;   
    }
    else {
      curRainbowColor = (curRainbowColor + 1) % 6;
      targetRainbowColor = (curRainbowColor + 1) % 6;
    }
  }
  RGB cur = rainbow[curRainbowColor];
  RGB target = rainbow[targetRainbowColor];
  for(int i = 0; i < nLEDs; i++) {
    colors[i].r = cur.r + t * (target.r - cur.r) / duration;
    colors[i].g = cur.g + t * (target.g - cur.g) / duration;
    colors[i].b = cur.b + t * (target.b - cur.b) / duration;  
  }
}

void fadeBetweenTwoColorGradient(int iterations, int duration) {
  fadeBetweenTwoColorGradient(iterations, duration, 0);
}

void fadeBetweenTwoColorGradient(int iterations, int duration, float percentOffset) {
  // 1. move the end colors one step closer to their next colors
  int t = iterations % duration;
    
  if(t == 0) {
    startLeftRgb.r = nextLeftRgb.r;
    startLeftRgb.g = nextLeftRgb.g;
    startLeftRgb.b = nextLeftRgb.b;
    curLeftRgb.r = startLeftRgb.r;
    curLeftRgb.g = startLeftRgb.g;
    curLeftRgb.b = startLeftRgb.b;    
    startRightRgb.r = nextRightRgb.r;
    startRightRgb.g = nextRightRgb.g;
    startRightRgb.b = nextRightRgb.b;    
    curRightRgb.r = startRightRgb.r;
    curRightRgb.g = startRightRgb.g;
    curRightRgb.b = startRightRgb.b;    
    randomizeRGB(&nextLeftRgb);
    randomizeRGB(&nextRightRgb);    
  }

  curLeftRgb.r = startLeftRgb.r + t * (nextLeftRgb.r - startLeftRgb.r) / duration;
  curLeftRgb.g = startLeftRgb.g + t * (nextLeftRgb.g - startLeftRgb.g) / duration;
  curLeftRgb.b = startLeftRgb.b + t * (nextLeftRgb.b - startLeftRgb.b) / duration;
  curRightRgb.r = startRightRgb.r + t * (nextRightRgb.r - startRightRgb.r) / duration;
  curRightRgb.g = startRightRgb.g + t * (nextRightRgb.g - startRightRgb.g) / duration;
  curRightRgb.b = startRightRgb.b + t * (nextRightRgb.b - startRightRgb.b) / duration;

  // 2. set the gradient between the two colors along the strip
  // based on the LED index
  // here, "t" goes from 0 to nLEDs - 1 (so you lose a little on one of the ends)
  int offset = nLEDs * percentOffset;
  for(int i = 0; i < nLEDs; i++) {
    int factor = (i + offset) % nLEDs;
    colors[i].r = curLeftRgb.r + factor * (curRightRgb.r - curLeftRgb.r) / nLEDs;
    colors[i].g = curLeftRgb.g + factor * (curRightRgb.g - curLeftRgb.g) / nLEDs;
    colors[i].b = curLeftRgb.b + factor * (curRightRgb.b - curLeftRgb.b) / nLEDs;    
  }
}
  

void setRGBFromIndexAndOffset(int i, int offset) {
  int index = (i + offset) % nLEDs;
  int region = (int) floor((index / (float) nLEDs) * 6);
  float regionSize = nLEDs / 6.0;
  float regionPercent = (index - (region * regionSize)) / regionSize;
  switch(region) {
    case 0:
      colors[i].r = 127;
      colors[i].g = (byte) floor(127 * regionPercent);
      colors[i].b = 0;
      break;
    case 1:
      colors[i].r = (byte) floor(127 * (1 - regionPercent));
      colors[i].g = 127;
      colors[i].b = 0;
      break;
    case 2:
      colors[i].r = 0;
      colors[i].g = 127;
      colors[i].b = (byte) floor(127 * regionPercent);
      break;
    case 3:
      colors[i].r = 0;
      colors[i].g = (byte) floor(127 * (1 - regionPercent));
      colors[i].b = 127;    
      break;
    case 4:
      colors[i].r = (byte) floor(127 * regionPercent);
      colors[i].g = 0;
      colors[i].b = 127;    
      break;
    case 5:
      colors[i].r = 127;
      colors[i].g = 0;
      colors[i].b = (byte) floor(127 * (1 - regionPercent));
      break;
  }
}

// percent is the offset
void setColorsToRainbowByPercent(float percent) {
  int offset = percent * nLEDs;
  for(int i = 0; i < nLEDs; i++) {
    setRGBFromIndexAndOffset(i, offset);
  }
}

void setColorsToRainbowByIterations(int iterations) {
  int offset = iterations % nLEDs;
  for(int i = 0; i < nLEDs; i++) {
    setRGBFromIndexAndOffset(i, offset);
  }
}

void scanQueueForNewMinAndOrMax() {
  int count = recentSamples.count();
  for(int i = 0; i < count; i++) {
    float value = recentSamples.pop();
    checkAndSetMinMax(value);
    recentSamples.push(value);
  }
}

int sampleSound() {
  unsigned long startMillis = millis();  // Start of sample window
  unsigned int peakToPeak = 0;   // peak-to-peak level

  unsigned int signalMax = 0;
  unsigned int signalMin = 1024;

  // collect data for 50 mS
  while (millis() - startMillis < sampleWindow)
  {
    sample = analogRead(micPin);
    if (sample < 1024)  // toss out spurious readings
    {
      if (sample > signalMax)
      {
        signalMax = sample;  // save just the max levels
      }
      else if (sample < signalMin)
      {
        signalMin = sample;  // save just the min levels
      }
    }
  }

  return signalMax - signalMin;  // max - min = peak-peak amplitude
}

boolean checkAndSetMinMax(float volts) {
  boolean setMin = checkAndSetMin(volts);
  boolean setMax = checkAndSetMax(volts);

  return setMin || setMax;
}

boolean checkAndSetMin(float volts) {
  if(volts < minV) {
    minV = volts;
    return true;
  }
  return false;
}

boolean checkAndSetMax(float volts) {
  if(volts > maxV) {
    maxV = volts;
    return true;
  }
  return false;
}

void clearStrip() {
  for(int i = 0; i < nLEDs; i++) {
    setPixelColor(i, 0, 0, 0);
  }
  strip.show();
}

void fillPercentageOfStrip(float percent, boolean reverse) {
  int ledsToLight = nLEDs * percent;

  for(int i = 0; i < ledsToLight; i++) {
    int index = i;
    if(reverse) {
      index = nLEDs - i - 1;
    }
    setPixelColor(index, colors[index].r, colors[index].g, colors[index].b);
  }
  for(int i = ledsToLight; i < nLEDs; i++) {
    int index = i;
    if(reverse) {
      index = nLEDs - i - 1;
    }
    setPixelColor(index, 0, 0, 0);
  }
  strip.show();
}

void lightWholeStripAtPercentage(float percent) {
  for(int i = 0; i < nLEDs; i++) {
    setPixelColor(i, percent * colors[i].r, percent * colors[i].g, percent * colors[i].b);
  }
  strip.show();
}

void lightPercentageOfStripRandom(float percent) {
  int numToLight = percent * nLEDs;
  for(int i = 0; i < nLEDs; i++) {
    if(random(nLEDs) < numToLight)
      setPixelColor(i, colors[i].r, colors[i].g, colors[i].b);
    else
      setPixelColor(i, 0, 0, 0);
  }
  strip.show();
}

void lightStripByRule110(float percent) {
  calculateNextGeneration110();
  for(int i = 0; i < nLEDs; i++) {
    if(cells[i]){
      setPixelColor(i, percent * colors[i].r, percent * colors[i].g, percent * colors[i].b);
    }
    else {
      setPixelColor(i, 0, 0, 0);
    }
    cells[i] = newCells[i];
  }
  strip.show();
}

void moveParticle(float percent) {
  boolean particlesMove = random(100) < (100 * percent);
  for(int i = 0; i < nLEDs; i++) {
    if(cells[i]) {
      setPixelColor(i, colors[i].r, colors[i].g, colors[i].b);
      if(particlesMove) {
        if(!newCells[i]) {
          newCells[i] = false;
        }
        newCells[(i + 1) % nLEDs] = true;
      }
    }
    else {
      setPixelColor(i, 0, 0, 0);
    }
  }

  strip.show();
  if(particlesMove) {
    for(int i = 0; i < nLEDs; i++) {
      cells[i] = newCells[i];
      newCells[i] = false;
    }    
  }
}

// fix G and B which are switched on my strip
void setPixelColor(int n, byte r, byte g, byte b) {
  strip.setPixelColor(n, brightness * r, brightness * b, brightness * g);
}
