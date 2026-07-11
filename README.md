This is the prompt sent to Gemini AI - Gems.

The attached sketch is a project to automate a bread machine to make sourdough bread 
hich can takes longer time that the bread machine allows. 
By interfacing the bread machine's button input pins with the output pins of the Arduino Nano board, 
and running a sketch for user to select the timing options for the different stages in 
making sourdough  bread.  
The program will set the pins digital output to low , 
then high with various timings to simulate pressing the buttons on a bread machine. 
The timing and clock countdown of the sketch has been modified to compress 1 minute to 1 second 
to allow easier testing. 


// --- PIN ASSIGNMENTS for Arduino Nano ---
const int PIN_BTN_MENU        = 4;
const int PIN_BTN_UP          = 5;
const int PIN_BTN_DOWN        = 6;
const int PIN_BTN_START_RESET = 7; 

const int srvMenu        = 9;   // Purple Wire on bread machine header Pin 7 
const int srvMinus       = 10;  // Yellow Wire on bread machine header pin 4
const int srvRunReset    = 11;  // Orange Wire on bread machine header pin 3
const int srvColour      = 12;  // Grey Wire on bread machine header pin 9

/*  
DonLim DL-T065-K Bread Machine
Solder a 10-pin femal header on the circuit board for the following buttons , GND and 5V.

Pin Header 
Pin 1 - ground Black 
Pin 2 - 5V Red
Pin 3 - RunReset button Orange
Pin 4 - Minus button Yellow
Pin 5 - Plus button Green 
Pin 6 - Weight button Blue
Pin 7 - Menu button Purple
Pin 8 - Colour button Grey

SSD1306 i2c
SDA A4
SCL A5
GND GND
VCC 5V
*/
