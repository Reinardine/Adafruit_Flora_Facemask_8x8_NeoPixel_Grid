#include <Adafruit_NeoPixel.h>
#ifdef __AVR__
  #include <avr/power.h>
#endif

// Which pin on the Flora is connected to the NeoPixels?
#define LED_PIN    6  // NeoPixels attached to pin 6
#define MIC_PIN   A9  // Microphone is attached to this analog pin
#define DC_OFFSET  0  // DC offset in mic signal - leave 0 fo the moment
#define NOISE     10  // Noise/hum/interference in mic signal
 
#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

// When we setup the NeoPixel library, we tell it how many pixels, and which pin to use to send signals.
Adafruit_NeoPixel pixels = Adafruit_NeoPixel(64, LED_PIN, NEO_GRB + NEO_KHZ800);

// Arduino Setup function
void setup() {
  analogReference(EXTERNAL);
  pixels.begin();
  pixels.setBrightness(40); //To reduce power consumption
  Serial.begin(9600);
}

void clear() {
  for(int i=0;i<64;i++){
    pixels.setPixelColor(i, pixels.Color(0,0,0));
  }
  pixels.show();
}

void setPixelForColor(int row, int col, uint32_t color) {
  if ((row % 2) == 1){
    pixels.setPixelColor(row*8+(7-col), color); 
  } else {
    pixels.setPixelColor(row*8+col, color); 
  }
}



/* shape definitions for the mouth movements and the smile (smile comes last)

  when the arrays below are unfolded properly, the shapes look like these on the 8x8 grid ...
  
  B00000000 B00000000 B00000000 B00111100 B00000000
  B00000000 B00000000 B00111100 B01000010 B00000000
  B00000000 B01111110 B01000010 B10000001 B10000001
  B11111111 B10000001 B10000001 B10000001 B11000011
  B11111111 B10000001 B10000001 B10000001 B01111110
  B00000000 B01111110 B01000010 B10000001 B00111100
  B00000000 B00000000 B00111100 B01000010 B00000000
  B00000000 B00000000 B00000000 B00111100 B00000000
  
*/

byte mouth[][8] = {{B00000000,B00000000,B00000000,B11111111,B11111111,B00000000,B00000000,B00000000},
                   {B00000000,B00000000,B01111110,B10000001,B10000001,B01111110,B00000000,B00000000},
                   {B00000000,B00111100,B01000010,B10000001,B10000001,B01000010,B00111100,B00000000},
                   {B00111100,B01000010,B10000001,B10000001,B10000001,B10000001,B01000010,B00111100},
                   {B00000000,B00000000,B10000001,B11000011,B01111110,B00111100,B00000000,B00000000}};

// define a mask for fast bit detection
byte mask[] = {1,2,4,8,16,32,64,128};

// function to draw an 8x8 shape
void drawShape(byte shape[], uint32_t color) {
    for(int i=0;i<8;i++) {
      for(int j=0; j<8; j++) {
        if(shape[i] & mask[j]) {
          setPixelForColor(i, j, color);
        }
        else {
          setPixelForColor(i, j, 0);
        }
      }
    }
    pixels.show(); // This sends the updated pixel color to the hardware.
}


/* definitions */
#define POP_COUNT_THRESHOLD 30
#define LOW_NOISE_START 150
#define LOW_NOISE_DELAY_AFTER_POP 50
#define SMILE_FOR_A_WHILE 750
#define MAX_POWER_OFF_TIME 500

// these thresholds will need to be tuned for how close the microphone will be
#define VOL_THRESHOLD_1 25
#define VOL_THRESHOLD_2 40
#define VOL_THRESHOLD_3 60

// global data declarations
typedef enum {LOW_NOISE_B4_POP=0, COUNT_POP=1, LOW_NOISE_AFTER_POP=2} pop_state_type;   // state machine state definitions
int countLowNoise=0,countPop=0;   // counters for the pop state machine 
int smileTimer=0;   // timer for the smile when it appears
pop_state_type popState=LOW_NOISE_B4_POP;  // State machine handling 'pop' detection for the smile
int lvl=0;  // Current "dampened" audio level
int powerConserveTimer=MAX_POWER_OFF_TIME;

// State machine function that detects a pop, essentially a period of low noise, a small burst of noise, followed by low noise again
bool handlePopStateChanges(int lvl)
{
    // this boolean returns whether to activate the smile or not
    bool returnSmile=false;

    // check for state changes
    switch(popState) {
      case LOW_NOISE_B4_POP:
        // whilst in this state, count low noise
        if (lvl<VOL_THRESHOLD_1) {
          countLowNoise++;
        }
        else {
          // change to counting pops, if lvl is above VOL_THRESHOLD_1 and low noise count is above threshold
          if ((countLowNoise > LOW_NOISE_START) & (lvl > VOL_THRESHOLD_1)) {
            popState=COUNT_POP;
            countPop=1; // had one count to start
          }

          // reset low noise count
          countLowNoise=0;
        }
        break;

      case COUNT_POP:
        // whilst in this state, increment pop counts
        if (lvl > VOL_THRESHOLD_1) {
          countPop++;
        }
        
        // change state to after pop counting, if 0>popCount<threshold and its low noise again
        if ((countPop>1) & (countPop<POP_COUNT_THRESHOLD) & (lvl<VOL_THRESHOLD_1)) {
          popState=LOW_NOISE_AFTER_POP;
          countPop=0;
        }
        else {
          // if low noise, revert to start
          if (lvl<VOL_THRESHOLD_1) popState=LOW_NOISE_B4_POP;
        }

        // always reset the low noise count
        countLowNoise=0;
        break;

      case LOW_NOISE_AFTER_POP:
        // whilst in this state, count low noise
        if (lvl<VOL_THRESHOLD_1) {
          countLowNoise++;
          if (countLowNoise>LOW_NOISE_DELAY_AFTER_POP) {
            returnSmile=true;
            countLowNoise=0;
            popState=LOW_NOISE_B4_POP;
          }
        }
        else {
          // back to start
          popState=LOW_NOISE_B4_POP;
          countLowNoise=0;
        }
      break;
    }
    return(returnSmile);
}

// Arduino Loop function
void loop() {
  int rawMicIn;

  rawMicIn   = analogRead(MIC_PIN);                        // Raw reading from mic 
  rawMicIn   = abs(rawMicIn - 512 - DC_OFFSET); // Center on zero
  rawMicIn   = (rawMicIn <= NOISE) ? 0 : (rawMicIn - NOISE);             // Remove noise/hum
  lvl = ((lvl * 7) + rawMicIn) >> 3;    // simple dampening of mic input to prevent noise
  //Serial.println(lvl, DEC);

  // look for pop count state changes
  if (handlePopStateChanges(lvl)) smileTimer = SMILE_FOR_A_WHILE;
  
  // look for a pop to make a smile
  if (smileTimer > 0) {
    drawShape(mouth[4], pixels.Color(28,172,247));
    smileTimer = MAX(0, smileTimer-1);
  }
  else {
    // only display a mouth if not conserving power
    if (powerConserveTimer > 0) {
      // otherwise process talking
      if (lvl<VOL_THRESHOLD_1) {
        // draw the closed mouth shape
        drawShape(mouth[0], pixels.Color(28,172,247));

        // and decrement the power conserve timer if no noise detected for a while
        powerConserveTimer = MAX(0, (powerConserveTimer-1)); 
      }
      else {
        // higher noise level, so reset the power conserve timer
        powerConserveTimer = MAX_POWER_OFF_TIME;

        // and display mouth shape
        if (lvl<VOL_THRESHOLD_2) {
             drawShape(mouth[1], pixels.Color(28,172,247)); 
        }
        else {
          if (lvl<VOL_THRESHOLD_3) {
             drawShape(mouth[2], pixels.Color(28,172,247)); 
          }
          else{
             drawShape(mouth[3], pixels.Color(28,172,247)); 
          }
        }
      }
    }
    else {
      // turn off the mouth
      clear();
      
      // look to re-enable the LEDs
      if (lvl > VOL_THRESHOLD_1) {
        powerConserveTimer = MAX_POWER_OFF_TIME;
      }
    }
  }
}
