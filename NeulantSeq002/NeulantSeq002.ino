/******************************************************************
 * 
 * THIS is the TRAM Sequencer Controller
 * 
 * Libs to build the Sketch in Arduino
 * - teensy 3.6
 * - AppleMidi lib
 * - EthernetLib
 * - MCP23017 lib
 * 
 * 
 * How it works 
 * On Dip Switch can selected Channel 1-16 it will read one time after reset the teensy
 * The Mac addr from w5500 is modified by midi channel to allow multiple boards in one network
 * also the IP adress. IP Config is static in this sketch
 * 
 * when midichannel < 7 the first 6x16 mcps are inputs and last 2x16 outputs
 * else the first 2x16 are inputs and last 6x6 are outputs for the board wich can handle the 7 seg displays
 * each noteOn/Off set an output register pin to drive any LED stuff by Darlinton Boards
 * each digital input pin send a noteon/off with the pin number starting by 0
 * 
 * when midi channel < 8 
 * it recieve cc50 for 3 7 segments for BPM + cc51 for 2 7 segments for Swing
 * to BPM musst add cont dec 80 value to allow display values > 127
 * 
 * if any questions:
 * me@zmors.de
 * 
 * Happy Patching!
 * 
 * Sven
 * 
 */

// TODO: check midichannel start by 0 or 1 ?
// TODO: check 7 seg code
// TODO: remove serial stuff to allow start the board without USB
// TODO: handle autoreconnect in apple script @matze
// TODO: config IP gateway and ip filter stuff to avoid any hacking of the board from outsite ^^
// TODO: test handle reconnect stuff to update current settings


#include <SPI.h>
#include <Ethernet.h>
#include "Adafruit_MCP23017.h"
#include <AppleMidi.h>

static void OnAppleMidiConnected(uint32_t ssrc, char* name);
static void OnAppleMidiDisconnected(uint32_t ssrc);
static void OnAppleMidiCC(byte channel, byte cc, byte value);
static void OnAppleMidiNoteOn(byte channel, byte note, byte velocity);
static void OnAppleMidiNoteOff(byte channel, byte note, byte velocity);

// https://github.com/adafruit/Adafruit-MCP23017-Arduino-Library/blob/master/Adafruit_MCP23017.cpp
Adafruit_MCP23017 mcps[8];    // alloc 8 ic per board

unsigned long t0 = millis();
bool isConnected = false;

APPLEMIDI_CREATE_DEFAULT_INSTANCE(); // see definition in AppleMidi_Defs.h

uint8_t midiChannel = 0;

uint8_t input_ic_count = 6;

// default alles aus (low active)
uint16_t BITS[8] = { 0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff };

// Reorder output Pins for PCB layout
// Inputs use the Same Layout
const uint8_t pinMapOut[16] = {
  4,    5,    6,    7,    3,    2,    1,  0,
  1+8,  0+8,  3+8,  2+8,  5+8,  4+8,  7+8,  6+8   };




void setup()  {
  // Serial communications and wait for port to open:
  Serial.begin(115200);
  // nowait! sonst muss usb verbunden sein!
  delay(100);



  Wire.setSDA(18);
  Wire.setSCL(19); 



  // Dip switch for Midichannel, Reset the Board to apply
  pinMode(24, INPUT_PULLUP);
  pinMode(25, INPUT_PULLUP);
  pinMode(26, INPUT_PULLUP);
  pinMode(27, INPUT_PULLUP);

  // Reset MCPs
  pinMode(23, OUTPUT);
  digitalWrite(23,LOW);
  digitalWrite(23,HIGH);

  // debug LED lets blink on begin it inverted low=led_on
  pinMode(16, OUTPUT);
  digitalWrite(16,LOW);
  delay(100);
  digitalWrite(16,HIGH);
  delay(100);
  digitalWrite(16,LOW);
  delay(100);
  digitalWrite(16,HIGH);

  // read encoder 0-f for midi channel,mac and ip adress
  if(!digitalRead(24))   midiChannel = midiChannel | (1 << 0);
  if(!digitalRead(27))   midiChannel = midiChannel | (1 << 1);
  if(!digitalRead(25))   midiChannel = midiChannel | (1 << 2);
  if(!digitalRead(26))   midiChannel = midiChannel | (1 << 3);

  // one special modul with 2 inputs only to drive 7 segment parts
  if(midiChannel > 8){
    input_ic_count = 2;
  }



  // create dynamic IP 8 + . midiChannel
  static IPAddress ip(10, 0, 0, 8 + midiChannel); // each module need another ip
  static IPAddress dns(8,8,8,8);                  // google dns server
  static IPAddress gw(10, 0, 0, 1 );              // default gateway
  static IPAddress mask(255, 255, 255, 0 );       // subnetmask

  // Newer Ethernet shields have a MAC address printed on a sticker on the shield
  static byte mac[] = {
    0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0x01 + midiChannel    // chosse different mac addr too 
  };
  
  
  Ethernet.init(10);  // CS Pin
  Serial.print(F("Getting IP address..."));

  Ethernet.begin(mac,ip,dns,gw,mask);

  // Starting I2C for the mux chips
  for(int i = 0 ; i < 8 ; i++){
    mcps[i].begin(i);                 // offset erfolgt in cpp class 
    for(int p = 0 ; p < 16 ; p ++){
      if(i < input_ic_count){
        mcps[i].pinMode(p,INPUT);     // die ersten 6 Chip Input
        mcps[i].pullUp(p,HIGH);       // activate 100k Pullup on chip
      } else{
        mcps[i].pinMode(p,OUTPUT);    // die restlichen 2 Outputs
      }
    }
  }
  
  Serial.print(F("IP address is "));
  Serial.println(Ethernet.localIP());

  Serial.println(F("OK, now make sure you an rtpMIDI session that is Enabled"));
  Serial.print(F("Add device named Arduino with Host/Port "));
  Serial.print(Ethernet.localIP());
  Serial.println(F(":5004"));
  Serial.println(F("Then press the Connect button"));
  Serial.println(F("Then open a MIDI listener (eg MIDI-OX) and monitor incoming notes"));

  // Create a session and wait for a remote host to connect to us
  AppleMIDI.begin("TRAM");

  AppleMIDI.OnConnected(OnAppleMidiConnected);
  AppleMIDI.OnDisconnected(OnAppleMidiDisconnected);

  AppleMIDI.OnReceiveNoteOn(OnAppleMidiNoteOn);
  AppleMIDI.OnReceiveNoteOff(OnAppleMidiNoteOff);
  AppleMIDI.OnReceiveControlChange(OnAppleMidiCC);
  // Serial.println(F("Sending NoteOn/Off of note 45, every second"));
}







// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
void loop() {
  // Listen to incoming notes
  AppleMIDI.run();
/*
  Serial.print( mcps[0].readGPIOAB()); 
  Serial.print(F(" "));
  Serial.print( mcps[1].readGPIOAB()); 
  Serial.print(F(" "));
  Serial.println( mcps[2].readGPIOAB()); 
  delay(500);

*/
  // erst updaten wenn alles connected ist
  if(!isConnected) {
    BITS[0] = 0xff; // reset all pinValue to resend when new midi reconnection is made
    BITS[1] = 0xff;
    BITS[2] = 0xff;
    BITS[3] = 0xff;
    BITS[4] = 0xff;
    BITS[5] = 0xff;
    BITS[6] = 0xff;
    BITS[7] = 0xff;
    return;
  }

  // ask the first 6 input IC´
  for(int p = 0 ; p < input_ic_count; p ++) {
    uint16_t oldValue = BITS[p];
    BITS[p] = mcps[p].readGPIOAB();
    for(int i = 0;  i < 16 ; i ++){
      if(bitRead(oldValue,pinMapOut[i]) != bitRead(BITS[p],pinMapOut[i]) ) {    // any changes since last loop ?
          if( bitRead(BITS[p],pinMapOut[i]) ) {  
            AppleMIDI.noteOff((p * 16) + i, 0, midiChannel +1);   
            Serial.print(F("NoteOff:")); 
            Serial.println((p * 16) + i); 
          } else {                 
            AppleMIDI.noteOn( (p * 16) + i, 127, midiChannel +1);   
            Serial.println(F("NoteOn:")); 
            Serial.print((p * 16) + i); 
            digitalWrite(16,HIGH);
          }
       }
    }
  }
  digitalWrite(16,LOW);



/*
  // send a note every second
  // (dont cáll delay(1000) as it will stall the pipeline)
  if (isConnected && (millis() - t0) > 1000) {
    t0 = millis();
    //   Serial.print(F(".");

    byte note = 45;
    byte velocity = 55;
    byte channel = 1;

    AppleMIDI.noteOn(note, velocity, 1 );
    AppleMIDI.noteOff(note, velocity, 1 );
  }
  */
}

// https://stackoverflow.com/questions/13247647/convert-integer-from-pure-binary-to-bcd
unsigned long toPackedBcd (unsigned int val) {
  unsigned long bcdresult = 0; char i;

  for (i = 0; val; i++) {
    ((char*)&bcdresult)[i / 2] |= i & 1 ? (val % 10) << 4 : (val % 10) & 0xf;
    val /= 10;
  }
  return bcdresult;
}


// some code to adapt for 7 seg Display
// https://github.com/DeanIsMe/SevSeg/blob/master/SevSeg.cpp


// the special FONT ;-)
static const byte digitCodeMap[] = {
  //     GFEDCBA  Segments      7-segment map:
  B00111111, // 0   "0"          AAA
  B00000110, // 1   "1"         F   B
  B01011011, // 2   "2"         F   B
  B01001111, // 3   "3"          GGG
  B01100110, // 4   "4"         E   C
  B01101101, // 5   "5"         E   C
  B01111101, // 6   "6"          DDD
  B00000111, // 7   "7"
  B01111111, // 8   "8"
  B01101111,  // 9   "9"
  B10000000,  // dot
  B10000000,  // dot
  B10000000,  // dot
  B10000000,  // dot
  B10000000,  // dot
  B10000000  // dot
};


// int to BCD to allow use 7 seg displays
uint32_t dec2bcd(uint16_t dec) {
    uint32_t result = 0;
    int shift = 0;

    while (dec) {
        result +=  (dec % 10) << shift;
        dec = dec / 10;
        shift += 4;
    }
    return result;
}

// ====================================================================================
// Event handlers for incoming MIDI messages
// ====================================================================================

// -----------------------------------------------------------------------------
// rtpMIDI session. Device connected -> TODO: LED on
// -----------------------------------------------------------------------------
static void OnAppleMidiConnected(uint32_t ssrc, char* name) {
  isConnected = true;
  digitalWrite(16,LOW);
  Serial.print(F("Connected to session "));
  Serial.println(name);
}

// -----------------------------------------------------------------------------
// rtpMIDI session. Device disconnected -> TODO: LED off
// -----------------------------------------------------------------------------
static void OnAppleMidiDisconnected(uint32_t ssrc) {
  isConnected = false;
  digitalWrite(16,HIGH);
  Serial.println(F("Disconnected"));
}




// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
static void OnAppleMidiNoteOn(byte channel, byte note, byte velocity) {

  if(channel!=(midiChannel +1)) return;  // check channel 0-15 or 1-16 !!!!!

  if(note > 95 && note < 112){
    mcps[6].digitalWrite(pinMapOut[note & 0x0f], HIGH);
  }else if(note > 111){
    mcps[7].digitalWrite(pinMapOut[note & 0x0f], HIGH);
  }
  /*
 
  Serial.print(F("Incoming NoteOn from channel:"));
  Serial.print(channel);
  Serial.print(F(" note:"));
  Serial.print(note);
  Serial.print(F(" velocity:"));
  Serial.print(velocity);
  Serial.println();
  */
}

// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
static void OnAppleMidiNoteOff(byte channel, byte note, byte velocity) {

  if(channel!=(midiChannel +1)) return;  // check channel 0-15 or 1-16 !!!!!

  
  if(note > 95 && note < 112){
    mcps[6].digitalWrite(pinMapOut[note & 0x0f], LOW);
  }else if(note > 111){
    mcps[7].digitalWrite(pinMapOut[note & 0x0f], LOW);
  }

   /*
  Serial.print(F("Incoming NoteOff from channel:"));
  Serial.print(channel);
  Serial.print(F(" note:"));
  Serial.print(note);
  Serial.print(F(" velocity:"));
  Serial.print(velocity);
  Serial.println();
    */
}


// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
static void OnAppleMidiCC(byte channel, byte cc, byte value) {

  if(channel!=(midiChannel +1)) return;  // check channel 0-15 or 1-16 !!!!!
  if(input_ic_count != 2 ) return; // CC only for 7 segment config


  // Tempodisplay
  if(cc == 50) {
      uint32_t t = dec2bcd(value+80); // allow show 80 ... 207 BPM
      uint8_t s71 = digitCodeMap[t >> 0  & 0x0f];
      uint8_t s72 = digitCodeMap[t >> 4  & 0x0f];
      uint8_t s73 = digitCodeMap[t >> 8  & 0x0f];
      for(int i = 0 ; i < 7 ; i++ )   mcps[3].digitalWrite(pinMapOut[i],     bitRead(s71,i));
      for(int i = 0 ; i < 7 ; i++ )   mcps[3].digitalWrite(pinMapOut[8 + i], bitRead(s72,i));
      for(int i = 0 ; i < 7 ; i++ )   mcps[4].digitalWrite(pinMapOut[i],     bitRead(s73,i));
  }else if(cc == 51) {
      // swing 2 segments
      uint32_t t = dec2bcd(value);
      uint8_t s71 = digitCodeMap[t >> 0  & 0x0f];
      uint8_t s72 = digitCodeMap[t >> 4  & 0x0f];
      for(int i = 0 ; i < 7 ; i++ )   mcps[5].digitalWrite(pinMapOut[i],     bitRead(s71,i));
      for(int i = 0 ; i < 7 ; i++ )   mcps[5].digitalWrite(pinMapOut[8 + i], bitRead(s72,i));
    
  }


  /*
  if(note > 95 && note < 112){
    mcps[6].digitalWrite(note & 0x0f, LOW);
  }else if(note > 111){
    mcps[7].digitalWrite(note & 0x0f, LOW);
  }

   
  Serial.print(F("Incoming NoteOff from channel:"));
  Serial.print(channel);
  Serial.print(F(" note:"));
  Serial.print(note);
  Serial.print(F(" velocity:"));
  Serial.print(velocity);
  Serial.println();
    */
}
