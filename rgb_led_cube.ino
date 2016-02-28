#include <SPI.h>

#define latch_pin 4   // BIT of PortD for latch - UNO pin 2, MEGA pin 4
#define blank_pin 5   // BIT of PortD for blank - UNO pin 3, MEGA pin 5
#define data_pin 51   // used by SPI - UNO pin 11, MEGA pin 51 (MOSI)
#define clock_pin 52  // used by SPI - UNO pin 13, MEGA pin 52 (SCK)

#define layer1 4
#define layer2 5
#define layer3 6
#define layer4 7
#define layer5 8
#define layer6 9
#define layer7 10
#define layer8 11

int layerArray[8] = {
  layer1,
  layer2,
  layer3,
  layer4,
  layer5,
  layer6,
  layer7,
  layer8
};
int lastAnode;

int shift_out;
byte anode[8];  // byte to write to the anode shift register, 8 of them,
                // shifting the ON level in each byte in the array

// This is how the brightness for every LED is stored,
// Each LED only needs a 'bit' to know if it should be ON or OFF, so 64 Bytes
// gives you 512 bits= 512 LEDs. Since we are modulating the LEDs, using 4 bit
// resolution, each color has 4 arrays containing 64 bits each. Notice how more
// resolution will eat up more of your precious RAM.
byte red0[64], red1[64], red2[64], red3[64];
byte blue0[64], blue1[64], blue2[64], blue3[64];
byte green0[64], green1[64], green2[64], green3[64];

int level = 0;                // Track level we shift data to
int anodeLevel = 0;           // Increments through the anode levels
int BAM_Bit, BAM_Counter = 0; // Keeps track of Bit Angle Modulation

unsigned long start; // Millisecond timer to cycle through the animations


const int CUBE_SIZE = 8;
const int TEMPO = 126; // bpm

const float DIVISOR_4 = 0.25;
const float DIVISOR_8 = 0.125;
const float DIVISOR_16 = 0.0625;
const float DIVISOR_32 = 0.03125;
const float THIRD_DIVISOR = 0.333333333;
const float SIXTH_DIVISOR = 0.166666667;


int getBeat() {
  return int(60000 / TEMPO);
}

int getBeatDivision(float multiplier = 1.00) {
  return int(getBeat() * multiplier);
}

int getBeatPerLayer() {
  return int(getBeatDivision(1.00 / CUBE_SIZE));
}

int getBeatDivisionPerLayer(float multiplier = 1.00) {
  return int(getBeatPerLayer() * multiplier);
}

void setup() {
  SPI.setBitOrder(MSBFIRST);            // Most Significant Bit First
  SPI.setDataMode(SPI_MODE0);           // Mode 0 Rising edge of data, keep clock low
  SPI.setClockDivider(SPI_CLOCK_DIV2);  // Run the data in at 16MHz/2 - 8MHz

  // Serial.begin(115200); // Start serial if required
  noInterrupts();       // kill interrupts until everybody is set up

  // We use Timer 1 to refresh the cube
  TCCR1A = B00000000; // Register A all 0's since we're not toggling any pins
  TCCR1B = B00001011; // bit 3 set to place in CTC mode, will call an interrupt
                      // on a counter match
  // bits 0 and 1 are set to divide the clock by 64, so 16MHz/64=250kHz
  TIMSK1 = B00000010; // bit 1 set to call the interrupt on an OCR1A match
  OCR1A = 45;         // you can play with this, but I set it to 30, which means:
                      // our clock runs at 250kHz, which is 1/250kHz = 4us
                      // with OCR1A set to 30, this means the interrupt will be
                      // called every (30+1)x4us=124us, which gives a multiplex
                      // frequency of about 8kHz

  // Setup anode array, this is written to anode
  // shift register, to enable each level
  anode[0] = B11111110;
  anode[7] = B11111101;
  anode[6] = B11111011;
  anode[5] = B11110111;
  anode[4] = B11101111;
  anode[3] = B11011111;
  anode[2] = B10111111;
  anode[1] = B01111111;

  // Set up Outputs
  // pinMode(latch_pin, OUTPUT); //Latch
  pinMode(2, OUTPUT);         // turn off PWM and set PortD bit 4 as output
  pinMode(3, OUTPUT);         // turn off PWM and set PortD bit 5 as output
  pinMode(data_pin, OUTPUT);  // MOSI DATA
  pinMode(clock_pin, OUTPUT); // SPI Clock
  // pinMode(blank_pin, OUTPUT); // Output Enable - Do this last, so LEDs don't flash on boot

  pinMode(layer1, OUTPUT);
  pinMode(layer2, OUTPUT);
  pinMode(layer3, OUTPUT);
  pinMode(layer4, OUTPUT);
  pinMode(layer5, OUTPUT);
  pinMode(layer6, OUTPUT);
  pinMode(layer7, OUTPUT);
  pinMode(layer8, OUTPUT);

  SPI.begin();
  interrupts(); // Start multiplexing
}

void setLED(int level, int row, int column, byte red, byte green, byte blue) {
  // Args mean:
  // setLED(
  //  level 0-7,
  //  row 0-7,
  //  column 0-7,
  //  red brightness 0-15,
  //  green brightness 0-15,
  //  blue brightness 0-15
  // );

  // Clamp location between 0 or 7
  // Clamp brightness between 0 or 15
  if (level<0)   level=0;
  if (level>7)   level=7;
  if (row<0)     row=0;
  if (row>7)     row=7;
  if (column<0)  column=0;
  if (column>7)  column=7;
  if (red<0)     red=0;
  if (red>15)    red=15;
  if (green<0)   green=0;
  if (green>15)  green=15;
  if (blue<0)    blue=0;
  if (blue>15)   blue=15;

  // Translate level, row and column 0 to 511
  int whichbyte = int(((level * (CUBE_SIZE * CUBE_SIZE)) + (row * CUBE_SIZE) + column) / CUBE_SIZE);

  // The first level LEDs are first in the sequence, then 2nd level, then third,
  // and so on the (level*64) is what indexes the level's starting place, so
  // level 0 are LEDs 0-63, level 1 are LEDs 64-127, and so on.

  // The column counts left to right 0-7 and the row is back to front 0-7. This
  // means that if you had level 0, row 0, the bottom back row would count from
  // 0-7,

  // Eg. If you look down on the cube, and only looked at the bottom level
  // 00 01 02 03 04 05 06 07
  // 08 09 10 11 12 13 14 15
  // 16 17 18 19 20 21 22 23
  // 24 25 26 27 28 29 30 31
  // 32 33 34 35 36 37 38 39
  // 40 41 42 43 44 45 46 47
  // 48 49 50 51 52 53 54 55
  // 56 57 58 59 60 61 62 63

  // Then, if you incremented the level, the top right of the grid above would
  // start at 64. The reason for doing this, is so you don't have to memorize a
  // number for each LED, allowing you to use level, row, column.

  // Now, what about the divide by 8 in there?
  // ...well, we have 8 bits per byte, and we have 64 bytes in memory for all
  // 512 bits needed for each LED, so we divide the number we just found by 8,
  // and take the integ7er of it, so we know which byte, that bit is located
  // confused? that's ok, let's take an example, if we wanted to write to the
  // LED to the last LED in the cube, we would write a 7, 7, 7 giving
  // (7 * 64) + (7 * 8) = 7 = 511, which is right, but now let's divide it by 8,
  // 511 / 8 = 63.875, and take the int of it so, we get 63, this is the last
  // byte in the array, which is right since this is the last LED.

  // This next variable is the same thing as before, but here we don't divide
  // by 8, so we get the LED number 0-511
  int wholebyte=(level * (CUBE_SIZE * CUBE_SIZE)) + (row * CUBE_SIZE) + column;

  //This is 4 bit color resolution, so each color contains x4 64 byte arrays,
  // explanation below:
  bitWrite(red0[whichbyte],   wholebyte - (CUBE_SIZE * whichbyte), bitRead(red,   0));
  bitWrite(red1[whichbyte],   wholebyte - (CUBE_SIZE * whichbyte), bitRead(red,   1));
  bitWrite(red2[whichbyte],   wholebyte - (CUBE_SIZE * whichbyte), bitRead(red,   2));
  bitWrite(red3[whichbyte],   wholebyte - (CUBE_SIZE * whichbyte), bitRead(red,   3));

  bitWrite(green0[whichbyte], wholebyte - (CUBE_SIZE * whichbyte), bitRead(green, 0));
  bitWrite(green1[whichbyte], wholebyte - (CUBE_SIZE * whichbyte), bitRead(green, 1));
  bitWrite(green2[whichbyte], wholebyte - (CUBE_SIZE * whichbyte), bitRead(green, 2));
  bitWrite(green3[whichbyte], wholebyte - (CUBE_SIZE * whichbyte), bitRead(green, 3));

  bitWrite(blue0[whichbyte],  wholebyte - (CUBE_SIZE * whichbyte), bitRead(blue,  0));
  bitWrite(blue1[whichbyte],  wholebyte - (CUBE_SIZE * whichbyte), bitRead(blue,  1));
  bitWrite(blue2[whichbyte],  wholebyte - (CUBE_SIZE * whichbyte), bitRead(blue,  2));
  bitWrite(blue3[whichbyte],  wholebyte - (CUBE_SIZE * whichbyte), bitRead(blue,  3));

  // Are you now more confused?  You shouldn't be!  It's starting to make sense
  // now.  Notice how each line is a bitWrite, which is, bitWrite(the byte you
  // want to write to, the bit of the byte to write, and the 0 or 1 you want to
  // write). This means that the 'whichbyte' is the byte from 0-63 in which the
  // bit corresponding to the LED from 0-511. Is making sense now why we did
  // that? taking a value from 0-511 and converting it to a value from 0-63,
  // since each LED represents a bit in an array of 64 bytes.
  // Then next line is which bit 'wholebyte-(8*whichbyte)'. This is simply
  // taking the LED's value of 0-511 and subracting it from the BYTE its bit
  // was located in times 8. Think about it, byte 63 will contain LEDs from
  // 504 to 511, so if you took 505-(8*63), you get a 1, meaning that, LED
  // number 505 is is located in bit 1 of byte 63 in the array

  // is that it?  No, you still have to do the bitRead of the brightness 0-15
  // you are trying to write, if you wrote a 15 to RED, all 4 arrays for that
  // LED would have a 1 for that bit, meaning it will be on 100%. This is why
  // the four arrays read 0-4 of the value entered in for RED, GREEN, and BLUE
  // hopefully this all makes some sense?
}

ISR(TIMER1_COMPA_vect) {
  // This routine is called in the background automatically at frequency set by
  // OCR1A. In this code, I set OCR1A to 30, so this is called every 124us,
  // giving each level in the cube 124us of ON time. There are 8 levels, so we
  // have a maximum brightness of 1/8, since the level must turn off before the
  // next level is turned on. The frequency of the multiplexing is then
  // 124us*8=992us, or 1/992us= about 1kHz


  PORTE |= 1<<blank_pin;  // The first thing we do is turn all of the LEDs OFF,
                          // by writing a 1 to the blank pin
                          //
  // Note, in my bread-boarded version, I was able to move this way down in the
  // cube, meaning that the OFF time was minimized due to signal integrity and
  // parasitic capcitance, my rise/fall times, required all of the LEDs to first
  // turn off, before updating otherwise you get a ghosting effect on the
  // previous level

  // This is 4 bit 'Bit angle Modulation' or BAM, There are 8 levels, so when a
  // '1' is written to the color brightness, each level will have a chance to
  // light up for 1 cycle, the BAM bit keeps track of which bit we are
  // modulating out of the 4 bits. Bam counter is the cycle count, meaning as we
  // light up each level, we increment the BAM_Counter
  if      (BAM_Counter==8)  BAM_Bit++;
  else if (BAM_Counter==24) BAM_Bit++;
  else if (BAM_Counter==56) BAM_Bit++;

  BAM_Counter++; // Increment the BAM counter

  // The BAM bit will be a value from 0-3, and only shift out the arrays
  // corresponding to that bit, 0-3. Here's how this works, each case is the bit
  // in the Bit angle modulation from 0-4. Next, it depends on which level we're
  // on, so the byte in the array to be written depends on which level, but
  // since each level contains 64 LED, we only shift out 8 bytes for each color
  switch (BAM_Bit) {
    case 0:
      for (shift_out=level; shift_out<level+8; shift_out++) {
        SPI.transfer(red0[shift_out]);
      }
      for (shift_out=level; shift_out<level+8; shift_out++) {
        SPI.transfer(green0[shift_out]);
      }
      for (shift_out=level; shift_out<level+8; shift_out++) {
        SPI.transfer(blue0[shift_out]);
      }
      break;
    case 1:
      for (shift_out=level; shift_out<level+8; shift_out++) {
        SPI.transfer(red1[shift_out]);
      }
      for (shift_out=level; shift_out<level+8; shift_out++) {
        SPI.transfer(green1[shift_out]);
      }
      for (shift_out=level; shift_out<level+8; shift_out++) {
        SPI.transfer(blue1[shift_out]);
      }
      break;
    case 2:
      for (shift_out=level; shift_out<level+8; shift_out++) {
        SPI.transfer(red2[shift_out]);
      }
      for (shift_out=level; shift_out<level+8; shift_out++) {
        SPI.transfer(green2[shift_out]);
      }
      for (shift_out=level; shift_out<level+8; shift_out++) {
        SPI.transfer(blue2[shift_out]);
      }
      break;
    case 3:
      for (shift_out=level; shift_out<level+8; shift_out++) {
        SPI.transfer(red3[shift_out]);
      }
      for (shift_out=level; shift_out<level+8; shift_out++) {
        SPI.transfer(green3[shift_out]);
      }
      for (shift_out=level; shift_out<level+8; shift_out++) {
        SPI.transfer(blue3[shift_out]);
      }

      // Here is where the BAM_Counter is reset back to 0, it's only 4 bit, but
      // since each cycle takes 8 counts, it goes 0 8 16 32, and when
      // BAM_counter hits 64 we reset the BAM
      if (BAM_Counter==120) {
        BAM_Counter=0;
        BAM_Bit=0;
      }
      break;
  }

  //SPI.transfer(anode[anodeLevel]); // Send out the anode level byte

  // ** This routine selects layer without shift register.
  lastAnode = (anodeLevel-1);
  if (anodeLevel == 0) { lastAnode = 7; } // if at bottom, last layer was top
  digitalWrite(layerArray[lastAnode], HIGH);  // turn off the previous layer
  digitalWrite(layerArray[anodeLevel], LOW);  // turn on the current layer


  PORTE |= 1<<latch_pin;    // Latch pin HIGH
  PORTE &= ~(1<<latch_pin); // Latch pin LOW
  PORTE &= ~(1<<blank_pin); // Blank pin LOW to turn on the LEDs with the new
                            // data. Blank is the same as the OE or ENABLE pin

  anodeLevel++;       // inrement the anode level
  level = level + 8;  // increment the level variable by 8, which is used to
                      // shift out data, since the next level woudl be the next
                      // 8 bytes in the arrays

  if (anodeLevel==8) anodeLevel=0; // go back to 0 if max is reached
  if (level==64) level=0;          // if you hit 64 on level, this means you just
                                   // sent out all 63 bytes, so go back
  pinMode(blank_pin, OUTPUT); // moved down here so outputs are all off until
                              // the first call of this function
}



// ANIMATIONS ==================================================================
// =============================================================================
// =============================================================================

void fireworks (int iterations, int n, int delayx) {
  clean;

  int i, f, e, x;

  float origin_x = 3;
  float origin_y = 3;
  float origin_z = 3;

  int rand_y, rand_x, rand_z;

  float slowrate, gravity;

  // Particles and their position, x,y,z and their movement, dx, dy, dz
  float particles[n][6];
  float lastpart[n][3];

  for (i=0; i < iterations; i++) {
    origin_x = rand() % 4;
    origin_y = rand() % 4;
    origin_z = rand() % 2;
    origin_z +=5;
    origin_x +=2;
    origin_y +=2;

    // shoot a particle up in the air
    for (e=0; e < origin_z; e++) {
      setLED(e, origin_x, origin_y, (random(16)), (random(16)), (random(16)));
      x = (50 * e);
      delay(30);
      clean();
    }

    // Fill particle array
    for (f=0; f < n; f++) {
      // Position
      particles[f][0] = origin_x;
      particles[f][1] = origin_y;
      particles[f][2] = origin_z;

      rand_x = rand() % 200;
      rand_y = rand() % 200;
      rand_z = rand() % 200;

      // Movement
      particles[f][3] = 1 - (float)rand_x / 100; // dx
      particles[f][4] = 1 - (float)rand_y / 100; // dy
      particles[f][5] = 1 - (float)rand_z / 100; // dz
    }

    // explode
    for (e=0; e < 25; e++) {
      slowrate = 1 + tan((e + 0.1) / 20) * 10;
      gravity = tan((e + 0.1) / 20) / 2;


      for (f=0; f < n; f++) {
        particles[f][0] += particles[f][3] / slowrate;
        particles[f][1] += particles[f][4] / slowrate;
        particles[f][2] += particles[f][5] / slowrate;
        particles[f][2] -= gravity;

        setLED(particles[f][2],particles[f][0],particles[f][1],(random(16)),(random(16)),(random(16)));
        lastpart[f][2]=particles[f][2];
        lastpart[f][0]=particles[f][0];
        lastpart[f][1]=particles[f][1];
      }

      delay(40);
      for (f=0; f < n; f++) {
        setLED(lastpart[f][2], lastpart[f][0], lastpart[f][1], 0, 0, 0);
      }
    }
  }
}

void wipeOut() {
  int xxx = 0;
  int yyy = 0;
  int zzz = 0;
  int fx = random(8);
  int fy = random(8);
  int fz = random(8);
  int fxm = 1;
  int fym = 1;
  int fzm = 1;
  int fxo = 0;
  int fyo = 0;
  int fzo = 0;
  int ftx = random(8);
  int fty = random(8);
  int ftz = random(8);
  int ftxm = 1;
  int ftym = 1;
  int ftzm = 1;
  int ftxo = 0;
  int ftyo = 0;
  int ftzo = 0;

  int direct, select, rr, gg, bb, rrt, ggt, bbt;

  for (xxx=0; xxx < 8; xxx++) {
    for (yyy=0; yyy < 8; yyy++) {
      for (zzz=0; zzz < 8; zzz++) {
        setLED(xxx, yyy, zzz, 0, 0, 0);
      }
    }
  }

  select = random(3);
  if (select == 0) {
    rr = random(1, 16);
    gg = random(1, 16);
    bb = 0;
  }
  if (select == 1) {
    rr = random(1, 16);
    gg = 0;
    bb = random(1, 16);
  }
  if (select==2) {
    rr = 0;
    gg = random(1, 16);
    bb = random(1, 16);
  }

  select = random(3);
  if (select == 0) {
    rrt = random(1, 16);
    ggt = random(1, 16);
    bbt = 0;
  }
  if (select==1) {
    rrt = random(1, 16);
    ggt = 0;
    bbt = random(1, 16);
  }
  if (select==2) {
    rrt = 0;
    ggt = random(1, 16);
    bbt = random(1, 16);
  }

  start = millis();
  while (millis() - start < 10000) {
    setLED(fxo,      fyo,      fzo,      0,    0,    0);
    setLED(fxo,      fyo,      fzo + 1,  0,    0,    0);
    setLED(fxo,      fyo,      fzo - 1,  0,    0,    0);
    setLED(fxo + 1,  fyo,      fzo,      0,    0,    0);
    setLED(fxo - 1,  fyo,      fzo,      0,    0,    0);
    setLED(fxo,      fyo + 1,  fzo,      0,    0,    0);
    setLED(fxo,      fyo - 1,  fzo,      0,    0,    0);

    setLED(ftxo,     ftyo,     ftzo,     0,    0,    0);
    setLED(ftxo,     ftyo,     ftzo + 1, 0,    0,    0);
    setLED(ftxo,     ftyo,     ftzo - 1, 0,    0,    0);
    setLED(ftxo + 1, ftyo,     ftzo,     0,    0,    0);
    setLED(ftxo - 1, ftyo,     ftzo,     0,    0,    0);
    setLED(ftxo,     ftyo + 1, ftzo,     0,    0,    0);
    setLED(ftxo,     ftyo - 1, ftzo,     0,    0,    0);

    setLED(ftx,      fty,      ftz,      rr,   gg,   bb);
    setLED(ftx,      fty,      ftz + 1,  rr,   gg,   bb);
    setLED(ftx,      fty,      ftz - 1,  rr,   gg,   bb);
    setLED(ftx + 1,  fty,      ftz,      rr,   gg,   bb);
    setLED(ftx - 1,  fty,      ftz,      rr,   gg,   bb);
    setLED(ftx,      fty + 1,  ftz,      rr,   gg,   bb);
    setLED(ftx,      fty - 1,  ftz,      rr,   gg,   bb);

    setLED(fx,       fy,       fz,       rrt,  ggt,  bbt);
    setLED(fx,       fy,       fz + 1,   rrt,  ggt,  bbt);
    setLED(fx,       fy,       fz - 1,   rrt,  ggt,  bbt);
    setLED(fx + 1,   fy,       fz,       rrt,  ggt,  bbt);
    setLED(fx - 1,   fy,       fz,       rrt,  ggt,  bbt);
    setLED(fx,       fy + 1,   fz,       rrt,  ggt,  bbt);
    setLED(fx,       fy - 1,   fz,       rrt,  ggt,  bbt);

    delay(10);

    fxo = fx;
    fyo = fy;
    fzo = fz;

    ftxo = ftx;
    ftyo = fty;
    ftzo = ftz;

    direct = random(3);
    if (direct == 0) { fx = fx + fxm; }
    if (direct == 1) { fy = fy + fym; }
    if (direct == 2) { fz = fz + fzm; }
    if (fx < 0) { fx = 0; fxm = 1;  }
    if (fx > 7) { fx = 7; fxm = -1; }
    if (fy < 0) { fy = 0; fym = 1;  }
    if (fy > 7) { fy = 7; fym = -1; }
    if (fz < 0) { fz = 0; fzm = 1;  }
    if (fz > 7) { fz = 7; fzm = -1; }

    direct=random(3);
    if (direct == 0) { ftx = ftx + ftxm; }
    if (direct == 1) { fty = fty + ftym; }
    if (direct == 2) { ftz = ftz + ftzm; }
    if (ftx < 0) { ftx = 0; ftxm = 1;  }
    if (ftx > 7) { ftx = 7; ftxm = -1; }
    if (fty < 0) { fty = 0; ftym = 1;  }
    if (fty > 7) { fty = 7; ftym = -1; }
    if (ftz < 0) { ftz = 0; ftzm = 1;  }
    if (ftz > 7) { ftz = 7; ftzm = -1; }
  }

  for (xxx=0; xxx < 8; xxx++) {
    for (yyy=0; yyy < 8; yyy++) {
      for (zzz=0; zzz < 8; zzz++) {
        setLED(xxx, yyy, zzz, 0, 0, 0);
      }
    }
  }
}

void rainVersionTwo() {
  int x[64],    y[64],    z[64];
  int xx[64],   yy[64],   zz[64];
  int xold[64], yold[64], zold[64];
  int addr, ledcolor, colowheel, slowdown;
  int leds = 64;
  int bright = 1;

  for (addr=0; addr < 64; addr++) {
    x[addr]   = random(8);
    y[addr]   = random(8);
    z[addr]   = random(8);
    xx[addr]  = random(16);
    yy[addr]  = random(16);
    zz[addr]  = random(16);
  }
  start = millis();
  while (millis() - start < 20000) {
    //wipe_out();
    //for (addr=0; addr<leds; addr++)
    //setLED(zold[addr], xold[addr], yold[addr], 0, 0, 0);

    if (ledcolor < 200) {
      for (addr=0; addr < leds; addr++) {
        setLED(zold[addr], xold[addr], yold[addr], 0, 0, 0);
        if (z[addr] >= 7) { setLED(z[addr], x[addr], y[addr], 0,  5, 15); }
        if (z[addr] == 6) { setLED(z[addr], x[addr], y[addr], 0,  1, 9);  }
        if (z[addr] == 5) { setLED(z[addr], x[addr], y[addr], 0,  0, 10); }
        if (z[addr] == 4) { setLED(z[addr], x[addr], y[addr], 1,  0, 11); }
        if (z[addr] == 3) { setLED(z[addr], x[addr], y[addr], 3,  0, 12); }
        if (z[addr] == 2) { setLED(z[addr], x[addr], y[addr], 10, 0, 15); }
        if (z[addr] == 1) { setLED(z[addr], x[addr], y[addr], 10, 0, 10); }
        if (z[addr] <= 0) { setLED(z[addr], x[addr], y[addr], 10, 0, 1);  }
      }
    }

    if (ledcolor >= 200 && ledcolor < 300) {
      for (addr=0; addr < leds; addr++) {
        setLED(zold[addr], xold[addr], yold[addr], 0, 0, 0);
        if (z[addr] >= 7) { setLED(z[addr], x[addr], y[addr], 15, 15, 0); }
        if (z[addr] == 6) { setLED(z[addr], x[addr], y[addr], 10, 10, 0); }
        if (z[addr] == 5) { setLED(z[addr], x[addr], y[addr], 15, 5,  0); }
        if (z[addr] == 4) { setLED(z[addr], x[addr], y[addr], 15, 2,  0); }
        if (z[addr] == 3) { setLED(z[addr], x[addr], y[addr], 15, 1,  0); }
        if (z[addr] == 2) { setLED(z[addr], x[addr], y[addr], 15, 0,  0); }
        if (z[addr] == 1) { setLED(z[addr], x[addr], y[addr], 12, 0,  0); }
        if (z[addr] <= 0) { setLED(z[addr], x[addr], y[addr], 10, 0,  0); }
      }
    }

    if (ledcolor >= 300 && ledcolor < 400) {}
    if (ledcolor >= 500 && ledcolor < 600) {}

    ledcolor++;
    if (ledcolor >= 300) { ledcolor=0; }

    for (addr=0; addr < leds; addr++) {
      xold[addr] = x[addr];
      yold[addr] = y[addr];
      zold[addr] = z[addr];
    }

    delay(15);

    for (addr=0; addr < leds; addr++) {
      //slowdown = random(2);
      //if (bitRead(z[addr],0))
      z[addr] = z[addr]-1;

      // x[addr] = x[addr]+1;
      // y[addr] = y[addr]+1;
      if (z[addr] < random(-100, 0)) {
        x[addr] = random(8);
        y[addr] = random(8);

        int select = random(3);
        if (select == 0) {
          xx[addr] = 0;
          zz[addr] = random(16);
          yy[addr] = random(16);
          //zz[addr] = 0;
        }
        if (select == 1) {
          xx[addr] = random(16);
          zz[addr] = 0;
          yy[addr] = random(16);
          //yy[addr] = 0;
        }
        if (select == 2) {
          xx[addr] = random(16);
          zz[addr] = random(16);
          yy[addr] = 0;
        }
        z[addr] = 7;
      }
    }
  }
}

void folder(int duration, float speedMultiplier = 1.0) {
  int xx, yy, zz;
  int pullback[16], state = 0, backorfront = 7; // backorfront 7 for back 0 for front

  int folderaddr[16], LED_Old[16], oldpullback[16];
  int ranx = random(16), rany = random(16), ranz = random(16),
      ranselect;
  int bot = 0, top = 1, right = 0, left = 0, back = 0, front = 0, side = 0,
      side_select;

  folderaddr[0] = -7;
  folderaddr[1] = -6;
  folderaddr[2] = -5;
  folderaddr[3] = -4;
  folderaddr[4] = -3;
  folderaddr[5] = -2;
  folderaddr[6] = -1;
  folderaddr[7] = 0;

  for (xx=0; xx < 8; xx++) {
    oldpullback[xx] = 0;
    pullback[xx] = 0;
  }

  start = millis();
  while (millis() - start < duration) {
    if (top == 1) {
      if (side == 0) {
        //top to left-side
        for (yy=0; yy < 8; yy++) {
          for (xx=0; xx < 8; xx++) {
            setLED(7 - LED_Old[yy],    yy - oldpullback[yy], xx, 0,    0,    0);
            setLED(7 - folderaddr[yy], yy - pullback[yy],    xx, ranx, rany, ranz);
          }
        }
        delay(getBeatDivisionPerLayer(speedMultiplier));
      }
      if (side == 2) {
        //top to back-side
        for (yy=0; yy < 8; yy++) {
          for (xx=0; xx < 8; xx++) {
            setLED(7 - LED_Old[yy],    xx, yy - oldpullback[yy], 0,    0,    0);
            setLED(7 - folderaddr[yy], xx, yy - pullback[yy],    ranx, rany, ranz);
          }
        }
        delay(getBeatDivisionPerLayer(speedMultiplier));
      }
      if (side == 3) {
        //top-side to front-side
        for (yy=0; yy < 8; yy++) {
          for (xx=0; xx < 8; xx++) {
            setLED(7 - LED_Old[7-yy],    xx, yy + oldpullback[yy], 0,    0,    0);
            setLED(7 - folderaddr[7-yy], xx, yy + pullback[yy],    ranx, rany, ranz);
          }
        }
        delay(getBeatDivisionPerLayer(speedMultiplier));
      }
      if (side == 1) {
        //top-side to right
        for (yy=0; yy < 8; yy++) {
          for (xx=0; xx < 8; xx++) {
            setLED(7 - LED_Old[7-yy],    yy + oldpullback[yy], xx, 0,    0,    0);
            setLED(7 - folderaddr[7-yy], yy + pullback[yy],    xx, ranx, rany, ranz);
          }
        }
        delay(getBeatDivisionPerLayer(speedMultiplier));
      }
    }
    if (right == 1) {
      if (side == 4) {
        //right-side to top
        for (yy=0; yy < 8; yy++) {
          for (xx=0; xx < 8; xx++) {
            setLED(yy + oldpullback[7-yy], 7 - LED_Old[7-yy],    xx, 0,    0,    0);
            setLED(yy + pullback[7-yy],    7 - folderaddr[7-yy], xx, ranx, rany, ranz);
          }
        }
        delay(getBeatDivisionPerLayer(speedMultiplier));
      }
      if (side == 3) {
        //right-side to front-side
        for (yy=0; yy < 8; yy++) {
          for (xx=0; xx < 8; xx++) {
            setLED(xx, 7 - LED_Old[7-yy],    yy + oldpullback[yy], 0,    0,    0);
            setLED(xx, 7 - folderaddr[7-yy], yy + pullback[yy],    ranx, rany, ranz);
          }
        }
        delay(getBeatDivisionPerLayer(speedMultiplier));
      }
      if (side == 2) {
        //right-side to back-side
        for (yy=0; yy < 8; yy++) {
          for (xx=0; xx < 8; xx++) {
            setLED(xx, 7 - LED_Old[yy],    yy - oldpullback[yy], 0,    0,    0);
            setLED(xx, 7 - folderaddr[yy], yy - pullback[yy],    ranx, rany, ranz);
          }
        }
        delay(getBeatDivisionPerLayer(speedMultiplier));
      }
      if (side == 5) {
        //right-side to bottom
        for (yy=0; yy < 8; yy++) {
          for (xx=0; xx < 8; xx++) {
            setLED(yy - oldpullback[yy], 7 - LED_Old[yy],    xx, 0,    0,    0);
            setLED(yy - pullback[yy],    7 - folderaddr[yy], xx, ranx, rany, ranz);
          }
        }
        delay(getBeatDivisionPerLayer(speedMultiplier));
      }
    }
    if (left == 1) {
      if (side == 4) {
        //left-side to top
        for (yy=0; yy < 8; yy++) {
          for (xx=0; xx < 8; xx++) {
            setLED(yy + oldpullback[yy], LED_Old[7-yy],    xx, 0,    0,    0);
            setLED(yy + pullback[yy],    folderaddr[7-yy], xx, ranx, rany, ranz);
          }
        }
        delay(getBeatDivisionPerLayer(speedMultiplier));
      }
      if (side == 3) {
        //left-side to front-side
        for (yy=0; yy < 8; yy++) {
          for (xx=0; xx < 8; xx++) {
            setLED(xx, LED_Old[7-yy],    yy + oldpullback[yy], 0,    0,    0);
            setLED(xx, folderaddr[7-yy], yy + pullback[yy],    ranx, rany, ranz);
          }
        }
        delay(getBeatDivisionPerLayer(speedMultiplier));
      }
      if (side == 2) {
        //left-side to back-side
        for (yy=0; yy < 8; yy++) {
          for (xx=0; xx < 8; xx++) {
            setLED(xx, LED_Old[yy],    yy - oldpullback[yy], 0,    0,    0);
            setLED(xx, folderaddr[yy], yy - pullback[yy],    ranx, rany, ranz);
          }
        }
        delay(getBeatDivisionPerLayer(speedMultiplier));
      }
      if (side == 5) {
        //left-side to bottom
        for (yy=0; yy < 8; yy++) {
          for (xx=0; xx < 8; xx++) {
            setLED(yy - oldpullback[yy], LED_Old[yy],    xx, 0,    0,    0);
            setLED(yy - pullback[yy],    folderaddr[yy], xx, ranx, rany, ranz);
          }
        }
        delay(getBeatDivisionPerLayer(speedMultiplier));
      }
    }
    if (back == 1) {
      if (side == 1) {
        //back-side to right-side
        for (yy=0; yy < 8; yy++) {
          for (xx=0; xx < 8; xx++) {
            setLED(xx, yy + oldpullback[yy], LED_Old[7-yy],    0,    0,    0);
            setLED(xx, yy + pullback[yy],    folderaddr[7-yy], ranx, rany, ranz);
          }
        }
        delay(getBeatDivisionPerLayer(speedMultiplier));
      }
      if (side == 4) {
        // back-side to top-side
        for (yy=0; yy < 8; yy++) {
          for (xx=0; xx < 8; xx++) {
            setLED(yy + oldpullback[yy], xx, LED_Old[7-yy],    0,    0,    0);
            setLED(yy + pullback[yy],    xx, folderaddr[7-yy], ranx, rany, ranz);
          }
        }
        delay(getBeatDivisionPerLayer(speedMultiplier));
      }
      if (side == 5) {
        // back-side to bottom
        for (yy=0; yy < 8; yy++) {
          for (xx=0; xx < 8; xx++) {
            setLED(yy - oldpullback[yy], xx, LED_Old[yy],    0,    0,    0);
            setLED(yy - pullback[yy],    xx, folderaddr[yy], ranx, rany, ranz);
          }
        }
        delay(getBeatDivisionPerLayer(speedMultiplier));
      }
      if (side == 0) {
        //back-side to left-side
        for (yy=0; yy < 8; yy++) {
          for (xx=0; xx < 8; xx++) {
            setLED(xx, yy - oldpullback[yy], LED_Old[yy],    0,    0,    0);
            setLED(xx, yy - pullback[yy],    folderaddr[yy], ranx, rany, ranz);
          }
        }
        delay(getBeatDivisionPerLayer(speedMultiplier));
      }
    }
    if (bot == 1) {
      if (side == 1) {
        // bottom-side to right-side
        for (yy=0; yy < 8; yy++) {
          for (xx=0; xx < 8; xx++) {
            setLED(LED_Old[7-yy],    yy + oldpullback[yy], xx, 0,    0,    0);
            setLED(folderaddr[7-yy], yy + pullback[yy],    xx, ranx, rany, ranz);
          }
        }
        delay(getBeatDivisionPerLayer(speedMultiplier));
      }
      if (side == 3) {
        //bottom to front-side
        for (yy=0; yy < 8; yy++) {
          for (xx=0; xx < 8; xx++) {
            setLED(LED_Old[7-yy],    xx, yy + oldpullback[yy], 0,    0,    0);
            setLED(folderaddr[7-yy], xx, yy + pullback[yy],    ranx, rany, ranz);
          }
        }
        delay(getBeatDivisionPerLayer(speedMultiplier));
      }
      if (side == 2) {
        //bottom to back-side
        for (yy=0; yy < 8; yy++) {
          for (xx=0; xx < 8; xx++) {
            setLED(LED_Old[yy],    xx, yy - oldpullback[yy], 0,    0,    0);
            setLED(folderaddr[yy], xx, yy - pullback[yy],    ranx, rany, ranz);
          }
        }
        delay(getBeatDivisionPerLayer(speedMultiplier));
      }
      if (side == 0) {
        //bottom to left-side
        for (yy=0; yy < 8; yy++) {
          for (xx=0; xx < 8; xx++) {
            setLED(LED_Old[yy],    yy - oldpullback[yy], xx, 0,    0,    0);
            setLED(folderaddr[yy], yy - pullback[yy],    xx, ranx, rany, ranz);
          }
        }
        delay(getBeatDivisionPerLayer(speedMultiplier));
      }
    }
    if (front == 1) {
      if (side == 0) {
        //front-side to left-side
        for (yy=0; yy < 8; yy++) {
          for (xx=0; xx < 8; xx++) {
            setLED(xx, yy - oldpullback[yy], 7 - LED_Old[yy],    0,    0,    0);
            setLED(xx, yy - pullback[yy],    7 - folderaddr[yy], ranx, rany, ranz);
          }
        }
        delay(getBeatDivisionPerLayer(speedMultiplier));
      }
      if (side == 5) {
        // front-side to bottom
        for (yy=0; yy < 8; yy++) {
          for (xx=0; xx < 8; xx++) {
            setLED(yy - oldpullback[yy], xx, 7 - LED_Old[yy],    0,    0,    0);
            setLED(yy - pullback[yy],    xx, 7 - folderaddr[yy], ranx, rany, ranz);
          }
        }
        delay(getBeatDivisionPerLayer(speedMultiplier));
      }
      if (side == 4) {
        // front-side to top-side
        for (yy=0; yy < 8; yy++) {
          for (xx=0; xx < 8; xx++) {
            setLED(yy + oldpullback[yy], xx, 7 - LED_Old[7-yy],    0,    0,    0);
            setLED(yy + pullback[yy],    xx, 7 - folderaddr[7-yy], ranx, rany, ranz);
          }
        }
        delay(getBeatDivisionPerLayer(speedMultiplier));
      }
      if (side == 1) {
        //front-side to right-side
        for (yy=0; yy < 8; yy++) {
          for (xx=0; xx < 8; xx++) {
            setLED(xx, yy + oldpullback[yy], 7 - LED_Old[7-yy],    0,    0,    0);
            setLED(xx, yy + pullback[yy],    7 - folderaddr[7-yy], ranx, rany, ranz);
          }
        }
        delay(getBeatDivisionPerLayer(speedMultiplier));
      }
    }


    for (xx=0; xx < 8; xx++) {
      LED_Old[xx] = folderaddr[xx];
      oldpullback[xx] = pullback[xx];
    }

    if (folderaddr[7] == 7) {
      for (zz=0; zz < 8; zz++) {
        pullback[zz] = pullback[zz]+1;
      }

      if (pullback[7] == 8) {
        // delay(getBeatDivisionPerLayer(speedMultiplier) * 30);

        ranselect = random(3);
        if (ranselect == 0) {
          ranx = 0;
          rany = random(1, 16);
          ranz = random(1, 16);
        }
        if (ranselect == 1) {
          ranx = random(1, 16);
          rany = 0;
          ranz = random(1, 16);
        }
        if (ranselect == 2) {
          ranx = random(1, 16);
          rany = random(1, 16);
          ranz = 0;
        }

        side_select = random(3);

        if (top == 1) {           // TOP
          top = 0;

          if (side == 0) {          // top to left
            left = 1;

            if (side_select == 0) side = 2;
            if (side_select == 1) side = 3;
            if (side_select == 2) side = 5;
          } else if (side == 1) {   // top to right
            right = 1;

            if (side_select == 0) side = 5;
            if (side_select == 1) side = 2;
            if (side_select == 2) side = 3;
          } else if (side == 2) {   // top to back
            back = 1;

            if (side_select == 0) side = 0;
            if (side_select == 1) side = 1;
            if (side_select == 2) side = 5;
          } else if (side == 3) {   // top to front
            front = 1;

            if (side_select == 0) side = 0;
            if (side_select == 1) side = 1;
            if (side_select == 2) side = 5;
          }
        } else if (bot == 1) {    // BOTTOM
          bot = 0;

          if (side == 0) {          // bot to left
            left = 1;

            if (side_select == 0) side = 2;
            if (side_select == 1) side = 3;
            if (side_select == 2) side = 4;
          } else if (side == 1) {   // bot to right
            right = 1;

            if (side_select == 0) side = 2;
            if (side_select == 1) side = 3;
            if (side_select == 2) side = 4;
          } else if (side == 2) {   // bot to back
            back = 1;

            if (side_select == 0) side = 0;
            if (side_select == 1) side = 1;
            if (side_select == 2) side = 4;
          } else if (side == 3) {   // bot to front
            front = 1;

            if (side_select == 0) side = 0;
            if (side_select == 1) side = 1;
            if (side_select == 2) side = 4;
          }
        } else if (right == 1) {  // RIGHT
          right = 0;

          if (side == 4) {          // right to top
            top = 1;

            if (side_select == 0) side = 2;
            if (side_select == 1) side = 3;
            if (side_select == 2) side = 0;
          } else if (side == 5) {   // right to bot
            bot = 1;

            if (side_select == 0) side = 0;
            if (side_select == 1) side = 2;
            if (side_select == 2) side = 3;
          } else if (side == 2) {   // right to back
            back = 1;

            if (side_select == 0) side = 0;
            if (side_select == 1) side = 5;
            if (side_select == 2) side = 4;
          } else if (side == 3) {   // right to front
            front = 1;

            if (side_select == 0) side = 0;
            if (side_select == 1) side = 5;
            if (side_select == 2) side = 4;
          }
        } else if (left == 1) {   // LEFT
          left = 0;

          if (side == 4) {          // left to top
            top = 1;

            if (side_select == 0) side = 3;
            if (side_select == 1) side = 2;
            if (side_select == 2) side = 1;
          } else if (side == 5) {   // left to bot
            bot = 1;

            if (side_select == 0) side = 2;
            if (side_select == 1) side = 3;
            if (side_select == 2) side = 1;
          } else if (side == 2) {   // left to back
            back = 1;

            if (side_select == 0) side = 1;
            if (side_select == 1) side = 5;
            if (side_select == 2) side = 4;
          } else if (side == 3) {   // left to front
            front = 1;

            if (side_select == 0) side = 1;
            if (side_select == 1) side = 5;
            if (side_select == 2) side = 4;
          }
        } else if (front == 1) {  // FRONT
          front = 0;

          if (side == 4) {        // front to top
            top=1;

            if (side_select == 0) side = 2;
            if (side_select == 1) side = 0;
            if (side_select == 2) side = 1;
          } else if (side == 5) {   // front to bot
            bot = 1;

            if (side_select == 0) side = 0;
            if (side_select == 1) side = 2;
            if (side_select == 2) side = 1;
          } else if (side == 0) {   // front to left
            left = 1;

            if (side_select == 0) side = 2;
            if (side_select == 1) side = 5;
            if (side_select == 2) side = 4;
          } else if (side == 1) {   // front to right
            right = 1;

            if (side_select == 0) side = 2;
            if (side_select == 1) side = 5;
            if (side_select == 2) side = 4;
          }
        } else if (back == 1) {   // BACK
          back = 0;

          if (side == 4) {        // back to top
            top = 1;

            if (side_select == 0) side = 3;
            if (side_select == 1) side = 0;
            if (side_select == 2) side = 1;
          } else if (side==5) {   // back to bot
            bot = 1;

            if (side_select == 0) side = 0;
            if (side_select == 1) side = 3;
            if (side_select == 2) side = 1;
          } else if (side ==0) {   // back to left
            left = 1;

            if (side_select == 0) side = 3;
            if (side_select == 1) side = 5;
            if (side_select == 2) side = 4;
          } else if (side==1) {   // back to right
            right = 1;

            if (side_select == 0) side = 3;
            if (side_select == 1) side = 5;
            if (side_select == 2) side = 4;
          }
        }

        for (xx=0; xx < 8; xx++) {
          oldpullback[xx] = 0;
          pullback[xx] = 0;
        }

        folderaddr[0] = -8;
        folderaddr[1] = -7;
        folderaddr[2] = -6;
        folderaddr[3] = -5;
        folderaddr[4] = -4;
        folderaddr[5] = -3;
        folderaddr[6] = -2;
        folderaddr[7] = -1;

      }
    }

    if (folderaddr[7] != 7) {
      for (zz=0; zz < 8; zz++) {
        folderaddr[zz] = folderaddr[zz] + 1;
      }
    }
  }
}
void snake() {
  int wipex, wipey, wipez, ranr, rang, ranb, select;
  int oldx[50], oldy[50], oldz[50];
  int x[50], y[50], z[50];
  int ledcount = 20;
  int addr, direct, direcTwo;
  int xx[50], yy[50], zz[50];
  int xbit = 1, ybit = 1, zbit = 1;

  for (addr=0; addr < ledcount+1; addr++) {
    oldx[addr] = 0;
    oldy[addr] = 0;
    oldz[addr] = 0;

    x[addr] = 0;
    y[addr] = 0;
    z[addr] = 0;

    xx[addr] = 0;
    yy[addr] = 0;
    zz[addr] = 0;
  }

  start = millis();

  while(millis() - start < 15000) {
    direct = random(3);

    for (addr=1; addr < ledcount + 1; addr++) {
      setLED(oldx[addr], oldy[addr], oldz[addr], 0,        0,        0);
      setLED(x[addr],    y[addr],    z[addr],    xx[addr], yy[addr], zz[addr]);
    }

    for (addr=1; addr < ledcount + 1; addr++) {
      oldx[addr]=x[addr];
      oldy[addr]=y[addr];
      oldz[addr]=z[addr];
    }

    delay(20);

    if (direct == 0) x[0] = x[0] + xbit;
    if (direct == 1) y[0] = y[0] + ybit;
    if (direct == 2) z[0] = z[0] + zbit;

    if (direct == 3) x[0] = x[0] - xbit;
    if (direct == 4) y[0] = y[0] - ybit;
    if (direct == 5) z[0] = z[0] - zbit;

    if (x[0] > 7) {
      xbit  = -1;
      x[0]  = 7;
      xx[0] = random(16);
      yy[0] = random(16);
      zz[0] = 0;
    }
    if (x[0] < 0) {
      xbit  = 1;
      x[0]  = 0;
      xx[0] = random(16);
      yy[0] = 0;
      zz[0] = random(16);
    }
    if (y[0] > 7) {
      ybit  = -1;
      y[0]  = 7;
      xx[0] = 0;
      yy[0] = random(16);
      zz[0] = random(16);
    }
    if (y[0] < 0) {
      ybit  = 1;
      y[0]  = 0;
      xx[0] = 0;
      yy[0] = random(16);
      zz[0] = random(16);
    }
    if (z[0] > 7) {
      zbit  = -1;
      z[0]  = 7;
      xx[0] = random(16);
      yy[0] = 0;
      zz[0] = random(16);
    }
    if (z[0] < 0) {
      zbit  = 1;
      z[0]  = 0;
      xx[0] = random(16);
      yy[0] = random(16);
      zz[0] = 0;
    }

    for (addr = ledcount; addr > 0; addr--) {
      x[addr] = x[addr-1];
      y[addr] = y[addr-1];
      z[addr] = z[addr-1];

      xx[addr] = xx[addr-1];
      yy[addr] = yy[addr-1];
      zz[addr] = zz[addr-1];
    }
  }
}

void sinewave(int duration, float speedMultiplier = 1.0) {
  int addr, addrt, colselect, select;
  int sinewavearray[8], sinewavearrayOLD[8], sinemult[8];
  int rr = 0, gg = 0, bb = 15;
  int subZ = -7, subT = 7, multi = 0; // random(-1, 2);

  sinewavearray[0] = 0;
  sinewavearray[1] = 1;
  sinewavearray[2] = 2;
  sinewavearray[3] = 3;
  sinewavearray[4] = 4;
  sinewavearray[5] = 5;
  sinewavearray[6] = 6;
  sinewavearray[7] = 7;

  sinemult[0] = 1;
  sinemult[1] = 1;
  sinemult[2] = 1;
  sinemult[3] = 1;
  sinemult[4] = 1;
  sinemult[5] = 1;
  sinemult[6] = 1;
  sinemult[7] = 1;

  start = millis();

  while(millis() - start < duration) {
    for (addr=0; addr < 8; addr++) {
      if (sinewavearray[addr] == 7) sinemult[addr] = -1;
      if (sinewavearray[addr] == 0) sinemult[addr] = 1;
      sinewavearray[addr] = sinewavearray[addr] + sinemult[addr];
    }

    if (sinewavearray[0] == 7) {
      select = random(3);
      if (select == 0) {
        rr = random(1, 16);
        gg = random(1, 16);
        bb = 0;
      }
      if (select == 1) {
        rr = random(1, 16);
        gg = 0;
        bb = random(1, 16);
      }
      if (select == 2) {
        rr = 0;
        gg = random(1, 16);
        bb = random(1, 16);
      }
    }

    for (addr=0; addr < 8; addr++) {
      setLED(sinewavearrayOLD[addr],         addr,       0,          0,  0,  0);
      setLED(sinewavearrayOLD[addr],         0,          addr,       0,  0,  0);
      setLED(sinewavearrayOLD[addr],         subT-addr,  7,          0,  0,  0);
      setLED(sinewavearrayOLD[addr],         7,          subT-addr,  0,  0,  0);
      setLED(sinewavearray[addr],            addr,       0,          rr, gg, bb);
      setLED(sinewavearray[addr],            0,          addr,       rr, gg, bb);
      setLED(sinewavearray[addr],            subT-addr,  7,          rr, gg, bb);
      setLED(sinewavearray[addr],            7,          subT-addr,  rr, gg, bb);
    }

    for (addr=1; addr < 7; addr++) {
      setLED(sinewavearrayOLD[addr+multi*1], addr,       1,          0,  0,  0);
      setLED(sinewavearrayOLD[addr+multi*1], 1,          addr,       0,  0,  0);
      setLED(sinewavearrayOLD[addr+multi*1], subT-addr,  6,          0,  0,  0);
      setLED(sinewavearrayOLD[addr+multi*1], 6,          subT-addr,  0,  0,  0);
      setLED(sinewavearray[addr+multi*1],    addr,       1,          rr, gg, bb);
      setLED(sinewavearray[addr+multi*1],    1,          addr,       rr, gg, bb);
      setLED(sinewavearray[addr+multi*1],    subT-addr,  6,          rr, gg, bb);
      setLED(sinewavearray[addr+multi*1],    6,          subT-addr,  rr, gg, bb);
    }

    for (addr=2; addr < 6; addr++) {
      setLED(sinewavearrayOLD[addr+multi*2], addr,       2,          0, 0, 0);
      setLED(sinewavearrayOLD[addr+multi*2], 2,          addr,       0, 0, 0);
      setLED(sinewavearrayOLD[addr+multi*2], subT-addr,  5,          0, 0, 0);
      setLED(sinewavearrayOLD[addr+multi*2], 5,          subT-addr,  0, 0, 0);
      setLED(sinewavearray[addr+multi*2],    addr,       2,          rr, gg, bb);
      setLED(sinewavearray[addr+multi*2],    2,          addr,       rr, gg, bb);
      setLED(sinewavearray[addr+multi*2],    subT-addr,  5,          rr, gg, bb);
      setLED(sinewavearray[addr+multi*2],    5,          subT-addr,  rr, gg, bb);
    }

    for (addr=3; addr < 5; addr++) {
      setLED(sinewavearrayOLD[addr+multi*3], addr,       3,          0,  0,  0);
      setLED(sinewavearrayOLD[addr+multi*3], 3,          addr,       0,  0,  0);
      setLED(sinewavearrayOLD[addr+multi*3], subT-addr,  4,          0,  0,  0);
      setLED(sinewavearrayOLD[addr+multi*3], 4,          subT-addr,  0,  0,  0);
      setLED(sinewavearray[addr+multi*3],    addr,       3,          rr, gg, bb);
      setLED(sinewavearray[addr+multi*3],    3,          addr,       rr, gg, bb);
      setLED(sinewavearray[addr+multi*3],    subT-addr,  4,          rr, gg, bb);
      setLED(sinewavearray[addr+multi*3],    4,          subT-addr,  rr, gg, bb);
    }

    for (addr=0; addr < 8; addr++) sinewavearrayOLD[addr] = sinewavearray[addr];
    delay(getBeatDivision(speedMultiplier));
  }
}

void colorWheelVertical(int duration, float speedMultiplier = 1.0) {
  int xx, yy, zz, ww;
  int rr = 1, gg = 1, bb = 1;
  int ranx, rany, swiper;

  start = millis();

  while (millis() - start < duration) {
    swiper  = random(3);
    ranx    = random(16);
    rany    = random(16);

    for (xx=0; xx < CUBE_SIZE; xx++) {
      for (yy=0; yy < CUBE_SIZE; yy++) {
        for (zz=0; zz < CUBE_SIZE; zz++) {
          setLED(xx, yy, zz,  ranx, 0, rany);
        }
      }
      delay(getBeatDivisionPerLayer(speedMultiplier));
    }

    ranx = random(16);
    rany = random(16);

    for (xx=7; xx >= 0; xx--) {
      for (yy=0; yy < CUBE_SIZE; yy++) {
        for (zz=0; zz < CUBE_SIZE; zz++) {
          setLED(xx,yy, zz, ranx, rany, 0);
        }
      }
      delay(getBeatDivisionPerLayer(speedMultiplier));
    }

    ranx = random(16);
    rany = random(16);
    for (xx=0; xx < CUBE_SIZE; xx++) {
      for (yy=0; yy < CUBE_SIZE; yy++) {
        for (zz=0; zz < CUBE_SIZE; zz++) {
          setLED(xx, yy, zz, 0, ranx, rany);
        }
      }
      delay(getBeatDivisionPerLayer(speedMultiplier));
    }

    ranx = random(16);
    rany = random(16);

    for (xx=7; xx >= 0; xx--) {
      for (yy=0; yy < CUBE_SIZE; yy++) {
        for (zz=0; zz < CUBE_SIZE; zz++) {
          setLED(xx, yy, zz, rany, ranx, 0);
        }
      }
      delay(getBeatDivisionPerLayer(speedMultiplier));
    }
  }
}

void colorWheel(int duration, float speedMultiplier = 1.0) {
  int xx, yy, zz, ww;
  int rr = 1, gg = 1, bb = 1;
  int ranx, rany, ranz, select, swiper;

  start = millis();

  while (millis() - start < duration) {
    swiper = random(6);
    select = random(3);

    if (select == 0) {
      ranx = 0;
      rany = random(16);
      ranz = random(16);
    }
    if (select == 1) {
      ranx = random(16);
      rany = 0;
      ranz = random(16);
    }
    if (select == 2) {
      ranx = random(16);
      rany = random(16);
      ranz = 0;
    }

    if (swiper == 0) {
      for (yy=0; yy < 8; yy++) {//left to right
        for (xx=0; xx < 8; xx++) {
          for (zz=0; zz < 8; zz++) {
            setLED(xx, yy, zz,  ranx, ranz, rany);
          }
        }
        delay(getBeatDivisionPerLayer(speedMultiplier));
      }
    }
    if (swiper==1) {//bot to top
      for (xx=0; xx < 8; xx++) {
        for (yy=0; yy < 8; yy++) {
          for (zz=0; zz < 8; zz++) {
            setLED(xx, yy, zz,  ranx, ranz, rany);
          }
        }
        delay(getBeatDivisionPerLayer(speedMultiplier));
      }
    }
    if (swiper==2) {//back to front
      for (zz=0; zz < 8; zz++) {
        for (xx=0; xx < 8; xx++) {
          for (yy=0; yy < 8; yy++) {
            setLED(xx, yy, zz,  ranx, ranz, rany);
          }
        }
        delay(getBeatDivisionPerLayer(speedMultiplier));
      }
    }
    if (swiper==3) {
      for (yy=7; yy >= 0; yy--) {//right to left
        for (xx=0; xx < 8; xx++) {
          for (zz=0; zz < 8; zz++) {
            setLED(xx, yy, zz,  ranx, ranz, rany);
          }
        }
        delay(getBeatDivisionPerLayer(speedMultiplier));
      }
    }
    if (swiper==4) {//top to bot
      for (xx=7; xx >= 0; xx--) {
        for (yy=0; yy < 8; yy++) {
          for (zz=0; zz < 8; zz++) {
            setLED(xx, yy, zz,  ranx, ranz, rany);
          }
        }
        delay(getBeatDivisionPerLayer(speedMultiplier));
      }
    }
    if (swiper==5) {//front to back
      for (zz=7; zz >= 0; zz--) {
        for (xx=0; xx < 8; xx++) {
          for (yy=0; yy < 8; yy++) {
            setLED(xx, yy, zz,  ranx, ranz, rany);
          }
        }
        delay(getBeatDivisionPerLayer(speedMultiplier));
      }
    }
  }
}

void lineMovement() {
  int greenx  = random(1, 7), greeny  = random(1, 7);
  int bluex   = random(1, 7), bluey   = random(1, 7);
  int redx    = random(1, 7), redy    = random(1, 7);
  int greenmult   = 1, bluemult   = 1, redmult  = 1;
  int greenmulty  = 1, bluemulty  = 1, redmulty = 1;
  int oredx, oredy, obluex, obluey, ogreenx, ogreeny;
  int cb1 = 15, cb2 = 0, cr1 = 15 , cr2 = 0, cg1 = 15, cg2 = 0;
  int time_counter = 10, timemult = 2;
  int m;
  int c1 = 1, c2 = 1, c3 = 1;
  int xmult = 1, ymult = 1, zmult = 1;
  int x = 4, y = 4, z = 4;
  int color_select, xo, yo, zo;
  int c21 = 1, c22 = 1, c23 = 1;
  int x2mult = 1, y2mult = 1, z2mult = 1;
  int x2 = 2, y2 = 2, z2 = 2;
  int color_select2, x2o, y2o, z2o;

  int counter, i, j, k;
  for (counter=0; counter < 150; counter++) {
    for (i=0; i < 8; i++) {
      setLED(i, oredx, oredx, 0, 0, 0);
    }
    for (i=0; i < 8; i++) {
      setLED(i, redx, redx, 15, 0, 0);
    }

    oredx = redx;
    oredy = redy;

    for (i=100; i > time_counter; i--) delay(1);

    time_counter = time_counter + timemult;
    if (time_counter > 100 || time_counter < 10) timemult = timemult * -1;
    if (redy > 6 || redy < 1) redmulty = redmulty * -1;

    if (redx > 6 || redx < 1) {
      redmult = redmult*-1;

      cr1 = random(16);
      cr2 = random(16);
    }

    redy = redy + redmulty;
    redx = redx + redmult;
  }

  for (counter=0; counter < 85; counter++) {
    for (i=0; i < 8; i++) {
      setLED(i,        oredx,  oredx,    0, 0, 0);
      setLED(ogreenx,  i,      ogreeny,  0, 0, 0);
    }
    for (i=0; i < 8; i++) {
      setLED(i,      redx, redx,   15, 0,  0);
      setLED(greenx, i,    greeny, 0,  15, 0);
    }
    ogreenx = greenx;
    ogreeny = greeny;
    oredx = redx;
    oredy = redy;

    for (i=100; i > time_counter; i--) delay(1);

    time_counter = time_counter + timemult;
    if (time_counter > 100 || time_counter < 10) timemult = timemult * -1;
    if (greeny > 6 || greeny < 1) greenmulty = greenmulty * -1;
    if (redy > 6 || redy < 1) redmulty = redmulty * -1;

    if (greenx > 6 || greenx < 1) {
      greenmult = greenmult * -1;
      greeny = greeny + greenmulty;
      cg1 = random(16);
      cg2 = random(16);
    }

    if (redx > 6 || redx < 1) {
      redmult = redmult * -1;

      cr1 = random(16);
      cr2 = random(16);
    }

    greenx = greenx + greenmult;
    redy = redy + redmulty;
    redx = redx + redmult;
  }

  for (counter=0; counter < 85; counter++) {
    for (i=0; i < 8; i++) {
      setLED(i, oredx, oredx, 0, 0, 0);
      setLED(obluey, obluex, i, 0, 0, 0);
      setLED(ogreenx, i, ogreeny, 0, 0, 0);
    }
    for (i=0; i < 8; i++) {
      setLED(i, redx, redx, 15, 0, 0);
      setLED(bluey, bluex, i, 0, 0, 15);
      setLED(greenx, i, greeny, 0, 15, 0);
    }
    ogreenx = greenx;
    ogreeny = greeny;
    obluex = bluex;
    obluey = bluey;
    oredx = redx;
    oredy = redy;

    for (i=100; i > time_counter; i--) delay(1);

    time_counter = time_counter + timemult;
    if (time_counter > 100 || time_counter < 10) timemult = timemult * -1;
    if (greeny > 6 || greeny < 1) greenmulty = greenmulty * -1;
    if (bluey > 6 || bluey < 1) bluemulty = bluemulty * -1;
    if (redy > 6 || redy < 1) redmulty = redmulty * -1;

    if (greenx > 6 || greenx < 1) {
      greenmult = greenmult * -1;
      greeny = greeny + greenmulty;
      cg1 = random(16);
      cg2 = random(16);
    }
    if (bluex > 6 || bluex < 1) {
      bluemult = bluemult * -1;
      bluey = bluey + bluemulty;
      cb1 = random(16);
      cb2 = random(16);
    }
    if (redx > 6 || redx < 1) {
      redmult = redmult * -1;
      cr1 = random(16);
      cr2 = random(16);
    }

    greenx = greenx + greenmult;
    bluex = bluex + bluemult;
    redy = redy + redmulty;
    redx = redx + redmult;
  }
}

void risingSweepWhite() {
  int counter, i, j, k;

  for (counter=0; counter < 3; counter++) { // counter was 3
    for (i=0; i < 8; i++) {
      for (j=0; j < 8; j++) {
        for (k=0; k < 8; k++) {
          setLED(i, j, k, 8, 15, 15);// red too intense to make white at 15. Reduced to 8 - makes nice white level.
          delay(1);
        }
      }
    }
    for (i=0; i < 8; i++) {
      for (j=0; j < 8; j++) {
        for (k=0; k < 8; k++) {
          setLED(i, j, k, 0, 0, 0);
          delay(1);
        }
      }
    }
  }
}

void risingSweepRGB() {
  int counter, i, j, k;

  for (counter=0; counter < 1; counter++) {
    for (i=0; i < 8; i++) {
      for (j=0; j < 8; j++) {
        for (k=0; k < 8; k++) {
          setLED(i, j, k, 0, random(16), random(16));
        }
      }
    }

    for (i=7;  i >= 0; i--) {
      for (j=0;  j <8; j++) {
        for (k=0;  k <8; k++) {
          setLED(i, j, k, random(16), 0, random(16));
        }
      }
    }

    for (i=0; i < 8; i++) {
      for (j=0; j < 8; j++) {
        for (k=0; k < 8; k++) {
          setLED(i, j, k, random(16), random(16), 0);
        }
      }
    }

    for (i=7; i >= 0; i--) {
      for (j=0; j < 8; j++) {
        for (k=0; k < 8; k++) {
          setLED(i, j, k, random(16), 0, random(16));
        }
      }
    }
  }
}

void tasteTheRainbow() {
  int counter, i, j, k;

  for (counter=0; counter < 20; counter++) {
    for (k=0; k < 200; k++) {
      setLED(random(8), random(8), random(8), random(16), random(16), 0);
      setLED(random(8), random(8), random(8), random(16), 0,          random(16));
      setLED(random(8), random(8), random(8), 0,          random(16), random(16));
    }
    for (k=0; k < 200; k++) {
      setLED(random(8),random(8),random(8),0,0,0);
    }
  }
}

void bouncyBalls() {
  int greenx  = random(1, 7), greeny  = random(1, 7);
  int bluex   = random(1, 7), bluey   = random(1, 7);
  int redx    = random(1, 7), redy    = random(1, 7);
  int greenmult   = 1, bluemult   = 1, redmult  = 1;
  int greenmulty  = 1, bluemulty  = 1, redmulty = 1;
  int oredx, oredy, obluex, obluey, ogreenx, ogreeny;
  int cb1 = 15, cb2 = 0, cr1 = 15 , cr2 = 0, cg1 = 15, cg2 = 0;
  int time_counter = 10, timemult = 2;
  int m;
  int c1 = 1, c2 = 1, c3 = 1;
  int xmult = 1, ymult = 1, zmult = 1;
  int x = 4, y = 4, z = 4;
  int color_select, xo, yo, zo;
  int c21 = 1, c22 = 1, c23 = 1;
  int x2mult = 1, y2mult = 1, z2mult = 1;
  int x2 = 2, y2 = 2, z2 = 2;
  int color_select2, x2o, y2o, z2o;

  int counter, i, j, k;


  color_select = random(0, 3);
  if (color_select == 0) {
    c1 = 0;
    c2 = random(0, 16);
    c3 = random(0, 16);
  }
  if (color_select == 1) {
    c1 = random(0, 16);
    c2 = 0;
    c3 = random(0, 16);
  }
  if (color_select == 2) {
    c1 = random(0, 16);
    c2 = random(0, 16);
    c3 = 0;
  }

  color_select2 = random(0, 3);
  if (color_select2 == 0) {
    c21 = 0;
    c22 = random(0, 16);
    c23 = random(0, 16);
  }
  if (color_select2 == 1) {
    c21 = random(0, 16);
    c22 = 0;
    c23 = random(0, 16);
  }
  if (color_select2 == 2) {
    c21 = random(0, 16);
    c22 = random(0, 16);
    c23 = 0;
  }

  for (counter=0; counter < 200; counter++) {
    setLED(xo,       yo,       zo,       0,    0,    0);
    setLED(xo + 1,   yo,       zo,       0,    0,    0);
    setLED(xo + 2,   yo,       zo,       0,    0,    0);
    setLED(xo - 1,   yo,       zo,       0,    0,    0);
    setLED(xo - 2,   yo,       zo,       0,    0,    0);
    setLED(xo,       yo + 1,   zo,       0,    0,    0);
    setLED(xo,       yo - 1,   zo,       0,    0,    0);
    setLED(xo,       yo + 2,   zo,       0,    0,    0);
    setLED(xo,       yo - 2,   zo,       0,    0,    0);
    setLED(xo,       yo,       zo - 1,   0,    0,    0);
    setLED(xo,       yo,       zo + 1,   0,    0,    0);
    setLED(xo,       yo,       zo - 2,   0,    0,    0);
    setLED(xo,       yo,       zo + 2,   0,    0,    0);

    setLED(x2o,      y2o,      z2o,      0,    0,    0);
    setLED(x2o + 1,  y2o,      z2o,      0,    0,    0);
    setLED(x2o + 2,  y2o,      z2o,      0,    0,    0);
    setLED(x2o - 1,  y2o,      z2o,      0,    0,    0);
    setLED(x2o - 2,  y2o,      z2o,      0,    0,    0);
    setLED(x2o,      y2o + 1,  z2o,      0,    0,    0);
    setLED(x2o,      y2o - 1,  z2o,      0,    0,    0);
    setLED(x2o,      y2o + 2,  z2o,      0,    0,    0);
    setLED(x2o,      y2o - 2,  z2o,      0,    0,    0);
    setLED(x2o,      y2o,      z2o - 1,  0,    0,    0);
    setLED(x2o,      y2o,      z2o + 1,  0,    0,    0);
    setLED(x2o,      y2o,      z2o - 2,  0,    0,    0);
    setLED(x2o,      y2o,      z2o + 2,  0,    0,    0);

    setLED(xo + 1,   yo + 1,   zo,       0,    0,    0);
    setLED(xo + 1,   yo - 1,   zo,       0,    0,    0);
    setLED(xo - 1,   yo + 1,   zo,       0,    0,    0);
    setLED(xo - 1,   yo - 1,   zo,       0,    0,    0);
    setLED(xo + 1,   yo + 1,   zo + 1,   0,    0,    0);
    setLED(xo + 1,   yo - 1,   zo + 1,   0,    0,    0);
    setLED(xo - 1,   yo + 1,   zo + 1,   0,    0,    0);
    setLED(xo - 1,   yo - 1,   zo + 1,   0,    0,    0);
    setLED(xo + 1,   yo + 1,   zo - 1,   0,    0,    0);
    setLED(xo + 1,   yo - 1,   zo - 1,   0,    0,    0);
    setLED(xo - 1,   yo + 1,   zo - 1,   0,    0,    0);
    setLED(xo - 1,   yo - 1,   zo - 1,   0,    0,    0);

    setLED(x2o + 1,  y2o + 1,  z2o,      0,    0,    0);
    setLED(x2o + 1,  y2o - 1,  z2o,      0,    0,    0);
    setLED(x2o - 1,  y2o + 1,  z2o,      0,    0,    0);
    setLED(x2o - 1,  y2o - 1,  z2o,      0,    0,    0);
    setLED(x2o + 1,  y2o + 1,  z2o + 1,  0,    0,    0);
    setLED(x2o + 1,  y2o - 1,  z2o + 1,  0,    0,    0);
    setLED(x2o - 1,  y2o + 1,  z2o + 1,  0,    0,    0);
    setLED(x2o - 1,  y2o - 1,  z2o + 1,  0,    0,    0);
    setLED(x2o + 1,  y2o + 1,  z2o - 1,  0,    0,    0);
    setLED(x2o + 1,  y2o - 1,  z2o - 1,  0,    0,    0);
    setLED(x2o - 1,  y2o + 1,  z2o - 1,  0,    0,    0);
    setLED(x2o - 1,  y2o - 1,  z2o - 1,  0,    0,    0);

    setLED(x,        y,        z,        c1,   c2,   c3);
    setLED(x,        y,        z - 1,    c1,   c2,   c3);
    setLED(x,        y,        z + 1,    c1,   c2,   c3);
    setLED(x,        y,        z - 2,    c1,   c2,   c3);
    setLED(x,        y,        z + 2,    c1,   c2,   c3);
    setLED(x + 1,    y,        z,        c1,   c2,   c3);
    setLED(x - 1,    y,        z,        c1,   c2,   c3);
    setLED(x,        y + 1,    z,        c1,   c2,   c3);
    setLED(x,        y - 1,    z,        c1,   c2,   c3);
    setLED(x + 2,    y,        z,        c1,   c2,   c3);
    setLED(x - 2,    y,        z,        c1,   c2,   c3);
    setLED(x,        y + 2,    z,        c1,   c2,   c3);
    setLED(x,        y - 2,    z,        c1,   c2,   c3);
    setLED(x + 1,    y + 1,    z,        c1,   c2,   c3);
    setLED(x + 1,    y - 1,    z,        c1,   c2,   c3);
    setLED(x - 1,    y + 1,    z,        c1,   c2,   c3);
    setLED(x - 1,    y - 1,    z,        c1,   c2,   c3);
    setLED(x + 1,    y + 1,    z + 1,    c1,   c2,   c3);
    setLED(x + 1,    y - 1,    z + 1,    c1,   c2,   c3);
    setLED(x - 1,    y + 1,    z + 1,    c1,   c2,   c3);
    setLED(x - 1,    y - 1,    z + 1,    c1,   c2,   c3);
    setLED(x + 1,    y + 1,    z - 1,    c1,   c2,   c3);
    setLED(x + 1,    y - 1,    z - 1,    c1,   c2,   c3);
    setLED(x - 1,    y + 1,    z - 1,    c1,   c2,   c3);
    setLED(x - 1,    y - 1,    z - 1,    c1,   c2,   c3);

    setLED(x2,       y2,       z2,       c21,  c22,  c23);
    setLED(x2,       y2,       z2 - 1,   c21,  c22,  c23);
    setLED(x2,       y2,       z2 + 1,   c21,  c22,  c23);
    setLED(x2,       y2,       z2 - 2,   c21,  c22,  c23);
    setLED(x2,       y2,       z2 + 2,   c21,  c22,  c23);
    setLED(x2 + 1,   y2,       z2,       c21,  c22,  c23);
    setLED(x2 - 1,   y2,       z2,       c21,  c22,  c23);
    setLED(x2,       y2 + 1,   z2,       c21,  c22,  c23);
    setLED(x2,       y2 - 1,   z2,       c21,  c22,  c23);
    setLED(x2 + 2,   y2,       z2,       c21,  c22,  c23);
    setLED(x2 - 2,   y2,       z2,       c21,  c22,  c23);
    setLED(x2,       y2 + 2,   z2,       c21,  c22,  c23);
    setLED(x2,       y2 - 2,   z2,       c21,  c22,  c23);
    setLED(x2 + 1,   y2 + 1,   z2,       c21,  c22,  c23);
    setLED(x2 + 1,   y2 - 1,   z2,       c21,  c22,  c23);
    setLED(x2 - 1,   y2 + 1,   z2,       c21,  c22,  c23);
    setLED(x2 - 1,   y2 - 1,   z2,       c21,  c22,  c23);
    setLED(x2 + 1,   y2 + 1,   z2 + 1,   c21,  c22,  c23);
    setLED(x2 + 1,   y2 - 1,   z2 + 1,   c21,  c22,  c23);
    setLED(x2 - 1,   y2 + 1,   z2 + 1,   c21,  c22,  c23);
    setLED(x2 - 1,   y2 - 1,   z2 + 1,   c21,  c22,  c23);
    setLED(x2 + 1,   y2 + 1,   z2 - 1,   c21,  c22,  c23);
    setLED(x2 + 1,   y2 - 1,   z2 - 1,   c21,  c22,  c23);
    setLED(x2 - 1,   y2 + 1,   z2 - 1,   c21,  c22,  c23);
    setLED(x2 - 1,   y2 - 1,   z2 - 1,   c21,  c22,  c23);

    x2o = x2;
    y2o = y2;
    z2o = z2;

    xo = x;
    yo = y;
    zo = z;

    delay(45);

    x = x + xmult;
    y = y + ymult;
    z = z + zmult;

    x2 = x2 + x2mult;
    y2 = y2 + y2mult;
    z2 = z2 + z2mult;

    if (x >= 7)   xmult = random(-1, 1);
    if (y >= 7)   ymult = random(-1, 1);
    if (z >= 7)   zmult = random(-1, 1);
    if (x <= 0)   xmult = random(0, 2);
    if (y <= 0)   ymult = random(0, 2);
    if (z <= 0)   zmult = random(0, 2);

    if (x2 >= 7)  x2mult = random(-1, 1);
    if (y2 >= 7)  y2mult = random(-1, 1);
    if (z2 >= 7)  z2mult = random(-1, 1);
    if (x2 <= 0)  x2mult = random(0, 2);
    if (y2 <= 0)  y2mult = random(0, 2);
    if (z <= 0)   z2mult = random(0, 2);
  }
}

void upDownArrows() {
  int greenx  = random(1, 7), greeny  = random(1, 7);
  int bluex   = random(1, 7), bluey   = random(1, 7);
  int redx    = random(1, 7), redy    = random(1, 7);
  int greenmult   = 1, bluemult   = 1, redmult  = 1;
  int greenmulty  = 1, bluemulty  = 1, redmulty = 1;
  int oredx, oredy, obluex, obluey, ogreenx, ogreeny;
  int cb1 = 15, cb2 = 0, cr1 = 15 , cr2 = 0, cg1 = 15, cg2 = 0;
  int time_counter = 10, timemult = 2;
  int m;
  int c1 = 1, c2 = 1, c3 = 1;
  int xmult = 1, ymult = 1, zmult = 1;
  int x = 4, y = 4, z = 4;
  int color_select, xo, yo, zo;
  int c21 = 1, c22 = 1, c23 = 1;
  int x2mult = 1, y2mult = 1, z2mult = 1;
  int x2 = 2, y2 = 2, z2 = 2;
  int color_select2, x2o, y2o, z2o;

  int counter, i, j, k;

  for (counter=0; counter < 15; counter++) {
    color_select = random(0, 3);
    if (color_select == 0) {
      c1 = 0;
      c2 = random(0, 16);
      c3 = random(0, 16);
    }
    if (color_select == 1) {
      c1 = random(0, 16);
      c2 = 0;
      c3 = random(0, 16);
    }
    if (color_select == 2) {
      c1 = random(0, 16);
      c2 = random(0, 16);
      c3 = 0;
    }

    int num1 = -1, num2 = -4, num3 = -6, num4 = -10;
    for (m=0; m < 20; m++) {
      num1++;
      num2++;
      num3++;
      num4++;

      for (i=3; i < 5; i++) {
        setLED(num1,     i,  3,  0,  0,  0);
        setLED(num1,     3,  i,  0,  0,  0);
        setLED(num1,     4,  i,  0,  0,  0);
        setLED(num1,     i,  4,  0,  0,  0);
      }
      for (i=3; i < 5; i++) {
        setLED(num1 + 1, i,  4,  c1, c2, c3);
        setLED(num1 + 1, 4,  i,  c1, c2, c3);
        setLED(num1 + 1, 3,  i,  c1, c2, c3);
        setLED(num1 + 1, i,  3,  c1, c2, c3);
      }
      for (i=2; i < 6; i++) {
        setLED(num2,     i,  2,  0,  0,  0);
        setLED(num2,     2,  i,  0,  0,  0);
        setLED(num2,     5,  i,  0,  0,  0);
        setLED(num2,     i,  5,  0,  0,  0);
      }
      for (i=2; i < 6; i++) {
        setLED(num2 + 1, i,  2,  c1, c2, c3);
        setLED(num2 + 1, 2,  i,  c1, c2, c3);
        setLED(num2 + 1, 5,  i,  c1, c2, c3);
        setLED(num2 + 1, i,  5,  c1, c2, c3);
      }
      for (i=1; i < 7; i++) {
        setLED(num3,     i,  1,  0,  0,  0);
        setLED(num3,     1,  i,  0,  0,  0);
        setLED(num3,     6,  i,  0,  0,  0);
        setLED(num3,     i,  6,  0,  0,  0);
      }
      for (i=1; i < 7; i++) {
        setLED(num3 + 1, i,  1,  c1, c2, c3);
        setLED(num3 + 1, 1,  i,  c1, c2, c3);
        setLED(num3 + 1, 6,  i,  c1, c2, c3);
        setLED(num3 + 1, i,  6,  c1, c2, c3);
      }
      for (i=0; i < 8; i++) {
        setLED(num4,     i,  0,  0,  0,  0);
        setLED(num4,     0,  i,  0,  0,  0);
        setLED(num4,     7,  i,  0,  0,  0);
        setLED(num4,     i,  7,  0,  0,  0);
      }
      for (i=0; i < 8; i++) {
        setLED(num4 + 1, i,  0,  c1, c2, c3);
        setLED(num4 + 1, 0,  i,  c1, c2, c3);
        setLED(num4 + 1, 7,  i,  c1, c2, c3);
        setLED(num4 + 1, i,  7,  c1, c2, c3);
      }
    }

    num1 = 8;
    num2 = 11;
    num3 = 13;
    num4 = 17;

    for (m=0; m < 20; m++) {
      num1--;
      num2--;
      num3--;
      num4--;
      for (i=3; i < 5; i++) {
        setLED(num1,     i,  3,  0,  0,  0);
        setLED(num1,     3,  i,  0,  0,  0);
        setLED(num1,     4,  i,  0,  0,  0);
        setLED(num1,     i,  4,  0,  0,  0);
      }
      for (i=3; i < 5; i++) {
        setLED(num1 - 1, i,  4,  0,  0,  15);
        setLED(num1 - 1, 4,  i,  0,  0,  15);
        setLED(num1 - 1, 3,  i,  0,  0,  15);
        setLED(num1 - 1, i,  3,  0,  0,  15);
      }
      for (i=2; i < 6; i++) {
        setLED(num2,     i,  2,  0,  0,  0);
        setLED(num2,     2,  i,  0,  0,  0);
        setLED(num2,     5,  i,  0,  0,  0);
        setLED(num2,     i,  5,  0,  0,  0);
      }
      for (i=2; i < 6; i++) {
        setLED(num2 - 1, i,  2,  0,  0,  15);
        setLED(num2 - 1, 2,  i,  0,  0,  15);
        setLED(num2 - 1, 5,  i,  0,  0,  15);
        setLED(num2 - 1, i,  5,  0,  0,  15);
      }
      for (i=1; i < 7; i++) {
        setLED(num3,     i,  1,  0,  0,  0);
        setLED(num3,     1,  i,  0,  0,  0);
        setLED(num3,     6,  i,  0,  0,  0);
        setLED(num3,     i,  6,  0,  0,  0);
      }
      for (i=1; i < 7; i++) {
        setLED(num3 - 1, i,  1,  0,  0,  15);
        setLED(num3 - 1, 1,  i,  0,  0,  15);
        setLED(num3 - 1, 6,  i,  0,  0,  15);
        setLED(num3 - 1, i,  6,  0,  0,  15);
      }
      for (i=0; i < 8; i++) {
        setLED(num4,     i,  0,  0,  0,  0);
        setLED(num4,     0,  i,  0,  0,  0);
        setLED(num4,     7,  i,  0,  0,  0);
        setLED(num4,     i,  7,  0,  0,  0);
      }
      for (i=0; i < 8; i++) {
        setLED(num4 - 1, i,  0,  0,  0,  15);
        setLED(num4 - 1, 0,  i,  0,  0,  15);
        setLED(num4 - 1, 7,  i,  0,  0,  15);
        setLED(num4 - 1, i,  7,  0,  0,  15);
      }
    }
  }
}

void generateOutline(int pause, int outlineSize, byte red, byte green, byte blue) {
  int offset = int((8 - outlineSize) / 2);
  int altOffset = 7 - offset;
  int counter;

  for (counter=0; counter < outlineSize; counter++) {
    setLED(offset + counter,        offset,                   offset,                   red,  green,  blue);
    setLED(offset,                  offset + counter,         offset,                   red,  green,  blue);
    setLED(offset,                  offset,                   offset + counter,         red,  green,  blue);
    setLED(altOffset - counter,     altOffset,                altOffset,                red,  green,  blue);
    setLED(altOffset,               altOffset - counter,      altOffset,                red,  green,  blue);
    setLED(altOffset,               altOffset,                altOffset - counter,      red,  green,  blue);

    delay(pause);
  }

  for (counter=0; counter < outlineSize; counter++) {
    setLED(altOffset - counter,     altOffset,                offset,                   red,  green,  blue);
    setLED(offset + counter,        offset,                   altOffset,                red,  green,  blue);
    setLED(altOffset,               offset,                   altOffset - counter,      red,  green,  blue);
    setLED(offset,                  altOffset,                offset + counter,         red,  green,  blue);
    setLED(altOffset,               offset + counter,         offset,                   red,  green,  blue);
    setLED(offset,                  altOffset - counter,      altOffset,                red,  green,  blue);

    delay(pause);
  }
}

void outlineSlow() {
  int pause = 150;

  generateOutline(pause, 8, 0, 0, 15);
  generateOutline(pause, 6, 0, 15, 0);
  generateOutline(pause, 4, 15, 0, 0);
  generateOutline(pause, 2, 0, 0, 15);

  generateOutline(pause, 8, 0, 0, 0);
  generateOutline(pause, 6, 0, 0, 0);
  generateOutline(pause, 4, 0, 0, 0);
  generateOutline(pause, 2, 0, 0, 0);

  generateOutline(pause, 8, 15, 0, 0);
  generateOutline(pause, 6, 0, 0, 15);
  generateOutline(pause, 4, 0, 15, 0);
  generateOutline(pause, 2, 15, 0, 0);

  generateOutline(pause, 8, 0, 0, 0);
  generateOutline(pause, 6, 0, 0, 0);
  generateOutline(pause, 4, 0, 0, 0);
  generateOutline(pause, 2, 0, 0, 0);

  generateOutline(pause, 8, 0, 15, 0);
  generateOutline(pause, 6, 15, 0, 0);
  generateOutline(pause, 4, 0, 0, 15);
  generateOutline(pause, 2, 0, 15, 0);
}

void cardboardBox(int duration, float speedMultiplier = 1.0) {
  start = millis();
  while (millis() - start < duration) {
    generateOutline(0, 8, 0, 0, 15);
    delay(getBeatDivision(speedMultiplier));
    generateOutline(0, 8, 0, 0, 0);
    generateOutline(0, 6, 0, 3, 11);
    delay(getBeatDivision(speedMultiplier));
    generateOutline(0, 6, 0, 0, 0);
    generateOutline(0, 4, 0, 7, 7);
    delay(getBeatDivision(speedMultiplier));
    generateOutline(0, 4, 0, 0, 0);
    generateOutline(0, 2, 0, 11, 3);
    delay(getBeatDivision(speedMultiplier));
    generateOutline(0, 2, 0, 0, 0);
    generateOutline(0, 4, 0, 15, 0);
    delay(getBeatDivision(speedMultiplier));
    generateOutline(0, 4, 0, 0, 0);
    generateOutline(0, 6, 3, 11, 0);
    delay(getBeatDivision(speedMultiplier));
    generateOutline(0, 6, 0, 0, 0);
    generateOutline(0, 8, 7, 7, 0);
    delay(getBeatDivision(speedMultiplier));
    generateOutline(0, 8, 0, 0, 0);
    generateOutline(0, 6, 11, 3, 0);
    delay(getBeatDivision(speedMultiplier));
    generateOutline(0, 6, 0, 0, 0);
    generateOutline(0, 4, 15, 0, 0);
    delay(getBeatDivision(speedMultiplier));
    generateOutline(0, 4, 0, 0, 0);
    generateOutline(0, 2, 11, 0, 3);
    delay(getBeatDivision(speedMultiplier));
    generateOutline(0, 2, 0, 0, 0);
    generateOutline(0, 4, 7, 0, 7);
    delay(getBeatDivision(speedMultiplier));
    generateOutline(0, 4, 0, 0, 0);
    generateOutline(0, 6, 3, 0, 11);
    delay(getBeatDivision(speedMultiplier));
    generateOutline(0, 6, 0, 0, 0);
  }

  clean();
}

void clean() {
  int ii, jj, kk;
  for (ii=0; ii < 8; ii++) {
    for (jj=0; jj < 8; jj++) {
      for (kk=0; kk < 8; kk++) {
        setLED(ii, jj, kk, 0, 0, 0);
      }
    }
  }
}

void loop() {
  // colorWheelVertical(getBeatDivision(16), DIVISOR_4);
  // clean();
  // folder(getBeatDivision(16), DIVISOR_16);
  // clean();
  sinewave(getBeatDivision(16), DIVISOR_16);
  // clean();
  // colorWheel(getBeatDivision(16), DIVISOR_4);
  // clean();
  // bouncyBalls();
  // clean();
  // wipeOut();
  // snake();
  // clean();
  // fireworks(20, 15, 0);
  // cardboardBox(getBeatDivision(32));
  // cardboardBox(getBeatDivision(16), DIVISOR_8);
}
