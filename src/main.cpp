// LMNC Organ Brain - Speedy Edition (Gotta Go Fast)

/**
 * The general approach of this solution is the following
 *  - Track the input keyboard note states
 *  - Track the stop switch states
 *  - Calculate what the output notes need to be set to
 *  - When setting the output notes, only send a MIDI On/Off message if the 
 *      output is different that what it was before
 * 
 * This should allow for the same notes to be pressed and released from multiple
 * keyboards without prematurely stopping a note. It will also allow for the
 * output/pipe notes to respond correctly to changes in the stop switches while
 * keys are being held down across the various keyboard inputs.
 * 
 * It compiles for me, so that means it'll work... right??? :D
*/

#include <MIDI.h>

/**
 * This will use a baud rate of 31250 for the Serial out
 * 
 * This is the same as 0.03125 MHz
 * 
 * For context, the arduino nano runs at 16 MHz, so serial speed
 * should not be a limiting factor as long as we're aggressively
 * reading input and smoothing out our writes to not overload the
 * output buffer.
 * 
*/
MIDI_CREATE_DEFAULT_INSTANCE();

//Uncomment this line to force all settings for local testing,
//such as 115200 serial baud rate and force-enabling all stop switches

//#define LOCAL_TESTING_MODE 1

#define u_long unsigned long
#define ON 1
#define OFF 0

/**
 * Note Constants
*/
#define OCTAVE 12
#define TWO_OCTAVE 24
#define TWELFTH 31
#define DEFAULT_OUTPUT_VELOCITY 100

/**
 * MIDI Channels
 */ 
#define SwellChannel 3 //swell keyboard midi input
#define GreatChannel 2 //Great keyboard midi input
#define PedalChannel 1 //Pedal keyboard midi input

//Output Channels
#define PrincipalPipesChannel 13
#define StringPipesChannel 14
#define FlutePipesChannel 15
#define ReedPipesChannel 16

/**
 * Organ Stop Pins
*/
#define SwellOpenDiapason8_7 7 //Swell Stop Open Diapason 8
#define SwellStoppedDiapason8_6 6 //Swell Stop Stopped Diapason 8
#define SwellPrincipal4_5 5 //Swell Stop Principal 4
#define SwellFlute4_4 4 //Swell Stop Principal 4
#define SwellFifteenth2_3 3 //Swell Stop Fifteenth 2
#define SwellTwelfth22thirds_2 2 //Swell Stop twelfth 2 2/3

#define GreatOpenDiapason8_15 15 //Great Stop Open Diapason 8
#define GreatLieblich8_14 14 // Great Stop Lieblich 8
#define GreatSalicional8_13 13 //Great Stop Salicional 8 NEED TO REMOVE ARDUINO LED TO MAKE THIS WORK
#define GreatGemsHorn4_12 12 //Great Stop GemsHorn 4 dont know yet
#define GreatSalicet4_11 11 //Great Stop Salicet 4
#define GreatNazard22thirds_10 10 //Great Stop Nazard 2 2/3
#define GreatHorn8_9 9 //Great Stop Horn 8
#define GreatClarion4_8 8 //Great Stop Clarion 4

#define PedalBassFlute8_20 20 //Pedal BassFlute 8. Need to analogRead this pin
#define PedalBourdon16_19 19 //Pedal Bourdon 16

#define SwellToGreat_18 18
#define SwellToPedal_17 17
#define GreatToPedal_16 16

#define PanicButton_21 21 // Need to analogRead this pin

/**
 * Execution flags to handle different program states
*/
boolean panicking = false; // Let functions know if we're in panic mode
boolean handelerExecuted = false; // Keeps track of whether a note was handled or not

/**
 * State Arrays for Input and Output channels
*/
#define STOP_STATES_SIZE 21
boolean StopSwitchStates[STOP_STATES_SIZE] = {};

#define NOTES_SIZE 128
#define NOTES_STATE_ARRAY_SIZE 4
// Each state needs to hold

// State for the input keyboards
u_long SwellState[NOTES_STATE_ARRAY_SIZE] = {};
u_long GreatState[NOTES_STATE_ARRAY_SIZE] = {};
u_long PedalState[NOTES_STATE_ARRAY_SIZE] = {};

//State for the output channels
u_long PrincipalPipesState[NOTES_STATE_ARRAY_SIZE] = {};
u_long StringPipesState[NOTES_STATE_ARRAY_SIZE] = {};
u_long FlutePipesState[NOTES_STATE_ARRAY_SIZE] = {};
u_long ReedPipesState[NOTES_STATE_ARRAY_SIZE] = {};

//Temporary state to combinie keboard and stop states
u_long TempPrincipalPipesState[NOTES_STATE_ARRAY_SIZE] = {};
u_long TempStringPipesState[NOTES_STATE_ARRAY_SIZE] = {};
u_long TempFlutePipesState[NOTES_STATE_ARRAY_SIZE] = {};
u_long TempReedPipesState[NOTES_STATE_ARRAY_SIZE] = {};


//////////////////////////////////////////
//
//  State aray functions
//
///////////////

void printBufferState(u_long noteState[]){
    Serial.print(noteState[0], BIN);
    Serial.print(noteState[1], BIN);
    Serial.print(noteState[2], BIN);
    Serial.print(noteState[3], BIN);
    Serial.println();
}

void resetStateArrays(){
    for(int i = 0; i < NOTES_STATE_ARRAY_SIZE; i++){
      FlutePipesState[i] = 0UL;
      PrincipalPipesState[i] = 0UL;
      StringPipesState[i] = 0UL;
      ReedPipesState[i] = 0UL;

      TempFlutePipesState[i] = 0UL;
      TempPrincipalPipesState[i] = 0UL;
      TempStringPipesState[i] = 0UL;
      TempReedPipesState[i] = 0UL;
    }
}

/**
 * Sets a single bit to on or off in the provided noteState array
*/
void setStateBit(u_long noteState[], byte stateIndex, boolean value){
  byte arrayIndex = stateIndex / 32;
  byte offset = stateIndex % 32; 
  u_long chunk = noteState[arrayIndex];
  u_long editMask = 1UL << offset;
  noteState[arrayIndex] = value ? (chunk | editMask) : (chunk & ~editMask);
}

boolean getStateBit(u_long noteState[], byte stateIndex){
  byte arrayIndex = stateIndex / 32;
  byte offset = stateIndex % 32; 
  u_long chunk = noteState[arrayIndex];
  chunk = chunk >> offset; //put the bit we want in the lowest bit position
  bool val = (chunk & 1) == 1; // see if that bit is enabled/ON
  return val;
}

/////////////////////////////////////////
//
//  Function Definitions needed at the top of the file for C in Platform.io
//
/////////////
void handleMidiNoteOn(byte channel, byte pitch, byte velocity);
void handleMidiNoteOff(byte channel, byte pitch, byte velocity);
void readMidi();
void sendMidi();
void panicAndPause();
void panic();


////////////////////////////////////////////////
//
//  Output Buffer Code
//
//////////////////////

#define RING_BUFFER_MAX_SIZE 512

// We're using words to store the note in the lower byte and the channel and on/off in the upper byte
word outputRingBuffer[RING_BUFFER_MAX_SIZE]  = {};

int outputRingHead = 0; // Start of buffer (where to read from)
int outputRingTail = 0; // End of buffer (where to write to)
int outputRingSize = 0; // Number of pending output bytes. If this ever gets bigger than the max size, panic!


/**
 * Pushes midi information to the output ring buffer
 * 
 * @returns false if the buffer is full, true if the note was added to the buffer
*/
boolean pushToOutputBuffer(byte channel, byte pitch, boolean val){
  //Check if the buffer is full (it will start overwriting queued notes if we don't do something about it)
  if (outputRingSize >= RING_BUFFER_MAX_SIZE){
    // // The buffer is full, can't do anything right now.
    return false;
  }

  // encode the note into a space efficient word (2 bytes)
  word encodedNote = pitch | (channel << 8) | ((val ? 1 : 0 ) << 15);

  // Add it to the buffer and advance the tail pointer
  outputRingBuffer[outputRingTail++] = encodedNote;
  outputRingSize++;

  // Check if we're past the edge of the array so we can loop back around for the next one
  if(outputRingTail >= RING_BUFFER_MAX_SIZE - 1){
    outputRingTail = 0;
  }
  
  return true;
}

/**
 * Remove an encoded midi message from the buffer and send it.
 * 
 * @returns false if the buffer is empty, true if a MIDI.send__() was called
*/
boolean popAndSendMidi(){
  if(outputRingSize <= 0){
    //Buffer is empty, nothing to send
    return false;
  }

  // Get the next encoded midi message and advance the head pointer
  word encodedNote = outputRingBuffer[outputRingHead++];
  outputRingSize--;

  // Decode the midi note information
  byte pitch = encodedNote & 0x00FF;
  byte channel = (encodedNote >> 8) & 0b01111111;
  bool val = (encodedNote >> 15) == 1;

  // Send the note
  if(val){
    MIDI.sendNoteOn(pitch, DEFAULT_OUTPUT_VELOCITY, channel);
  } else {
    MIDI.sendNoteOff(pitch, DEFAULT_OUTPUT_VELOCITY, channel);  
  }

  // Check if we're past the edge of the array so we can loop back around
  if(outputRingHead >= RING_BUFFER_MAX_SIZE - 1){
    outputRingHead = 0;
  }

  return true;
}

/**
 * Clear the buffer by resetting the head, tail, and size. No need to
 * write over existing data in the buffer array.
*/
void resetOutputBuffer(){
  outputRingHead = 0;
  outputRingTail = 0;
  outputRingSize = 0;
}

/////////////////////////////////////////////////////////////////
//
//   State Management Code
//
////////////////////////////////

/**
 * Sets the note state to the specified value. 0 for off, 1 for on.
 * @param chanelState The bitmask to change
 * @param pitch The note index/pitch to change in the mask
 * @param val false for off, true for on
 * 
 * @return true if it changed, false if it did not change
*/
boolean setNoteState(u_long chanelState[], byte pitch, boolean val){
    boolean current = getStateBit(chanelState, pitch);
    setStateBit(chanelState, pitch, val);
    return current != val;
}

/**
 * Makes sure the note is on.
 * 
 * If the note state changed, then send the midi channel note on message to the channel.
 * 
*/
void setNoteStateOn(u_long channelState[], byte pitch, int channel)
{
  // Set the state. If it changed, queue up a midi output message
  if(setNoteState(channelState, pitch, ON)){
    if(!pushToOutputBuffer(channel, pitch, ON)){
      panicAndPause();
    }
  }
}

/**
 * Makes sure the note is off.
 * 
 * If the note state changed, then send the midi channel note off message to the channel.
 * 
*/
void setNoteStateOff(u_long channelState[], byte pitch, int channel)
{
  // Set the state. If it changed, queue up a midi output message
  if(setNoteState(channelState, pitch, OFF)){
    if(!pushToOutputBuffer(channel, pitch, OFF)){
      panicAndPause();
    }
  }
}

void updateStopStates(){
  StopSwitchStates[SwellOpenDiapason8_7] = digitalRead(SwellOpenDiapason8_7) == HIGH;
  StopSwitchStates[SwellStoppedDiapason8_6] = digitalRead(SwellStoppedDiapason8_6) == HIGH;
  StopSwitchStates[SwellPrincipal4_5] = digitalRead(SwellPrincipal4_5) == HIGH;
  StopSwitchStates[SwellFlute4_4] = digitalRead(SwellFlute4_4) == HIGH;
  StopSwitchStates[SwellFifteenth2_3] = digitalRead(SwellFifteenth2_3) == HIGH;
  StopSwitchStates[SwellTwelfth22thirds_2] = digitalRead(SwellTwelfth22thirds_2) == HIGH;

  StopSwitchStates[GreatOpenDiapason8_15] = digitalRead(GreatOpenDiapason8_15) == HIGH;
  StopSwitchStates[GreatLieblich8_14] = digitalRead(GreatLieblich8_14) == HIGH;
  StopSwitchStates[GreatSalicional8_13] = digitalRead(GreatSalicional8_13) == HIGH;
  StopSwitchStates[GreatGemsHorn4_12] = digitalRead(GreatGemsHorn4_12) == HIGH;
  StopSwitchStates[GreatSalicet4_11] = digitalRead(GreatSalicet4_11) == HIGH;
  StopSwitchStates[GreatNazard22thirds_10] = digitalRead(GreatNazard22thirds_10) == HIGH;
  StopSwitchStates[GreatHorn8_9] = digitalRead(GreatHorn8_9) == HIGH;
  StopSwitchStates[GreatClarion4_8] = digitalRead(GreatClarion4_8) == HIGH;

  StopSwitchStates[PedalBassFlute8_20] = analogRead(PedalBassFlute8_20) > 200; // Pin D20/A6 is analog input only
  StopSwitchStates[PedalBourdon16_19] = digitalRead(PedalBourdon16_19) == HIGH;

  // TODO: Running in a simulator was giving some weird results on some pins.
  // double check that they are configured in the correct way to read them.
  StopSwitchStates[SwellToGreat_18] = digitalRead(SwellToGreat_18) == HIGH; //18
  StopSwitchStates[SwellToPedal_17] = digitalRead(SwellToPedal_17) == HIGH; //17
  StopSwitchStates[GreatToPedal_16] = digitalRead(GreatToPedal_16) == HIGH; //16
}

/**
 * Test function to ignore stop switches and enable everything!
*/
void pullOutAllTheStops(){
  StopSwitchStates[SwellOpenDiapason8_7] = true;
  StopSwitchStates[SwellStoppedDiapason8_6] = true;
  StopSwitchStates[SwellPrincipal4_5] = true;
  StopSwitchStates[SwellFlute4_4] = true;
  StopSwitchStates[SwellFifteenth2_3] = true;
  StopSwitchStates[SwellTwelfth22thirds_2] = true;

  StopSwitchStates[GreatOpenDiapason8_15] = true;
  StopSwitchStates[GreatLieblich8_14] = true;
  StopSwitchStates[GreatSalicional8_13] = true;
  StopSwitchStates[GreatGemsHorn4_12] = true;
  StopSwitchStates[GreatSalicet4_11] = true;
  StopSwitchStates[GreatNazard22thirds_10] = true;
  StopSwitchStates[GreatHorn8_9] = true;
  StopSwitchStates[GreatClarion4_8] = true;

  StopSwitchStates[PedalBassFlute8_20] = true;
  StopSwitchStates[PedalBourdon16_19] = true;

  // TODO: Running in a simulator was giving some weird results on some pins.
  // double check that they are configured in the correct way to read them.
  StopSwitchStates[SwellToGreat_18] = true;//digitalRead(SwellToGreat_18) == HIGH;
  StopSwitchStates[SwellToPedal_17] = true;//digitalRead(SwellToPedal_17) == HIGH;
  StopSwitchStates[GreatToPedal_16] = true;//digitalRead(GreatToPedal_16) == HIGH;
}


/**
 * Set pitch for the temp state to ON, with array bounds checking
*/
void tempNoteOn(u_long tempState[], byte pitch){
  if(pitch >= 0 && pitch < NOTES_SIZE) {
    setNoteState(tempState, pitch, ON);
  }
}

/**
 * Enables output notes for a given pitch based on the Swell Stop switchs
*/
void onForSwellStops(byte pitch) {
  // Swell Stop To Principal Pipes
  if (StopSwitchStates[SwellOpenDiapason8_7]) {
    tempNoteOn(TempPrincipalPipesState, pitch);
  }
  // Swell Stop To Flute Pipes
  if (StopSwitchStates[SwellStoppedDiapason8_6]) {
    tempNoteOn(TempFlutePipesState, pitch);
  }
  // Swell Stop To Principal Pipes + 1 Octave
  if (StopSwitchStates[SwellPrincipal4_5]) {
    tempNoteOn(TempPrincipalPipesState, pitch + OCTAVE);
  }
  // Swell Stop To Flute Pipes + 1 & 2 Octave
  if (StopSwitchStates[SwellFlute4_4]) {
    tempNoteOn(TempFlutePipesState, pitch + OCTAVE); //Swell Stop To Flute Pipes + 1 Octave
    tempNoteOn(TempFlutePipesState, pitch + TWO_OCTAVE);
  }
  // Swell Stop To Principal Pipes + 2 Octave
  if (StopSwitchStates[SwellFifteenth2_3]) {
    tempNoteOn(TempPrincipalPipesState, pitch + TWO_OCTAVE);
  }
  // Swell Stop To Principal Pipes + 2 Octave and a fifth
  if (StopSwitchStates[SwellTwelfth22thirds_2]) {
    tempNoteOn(TempPrincipalPipesState, pitch + TWELFTH);
  }
}

/**
 * Enables output notes for a given pitch based on the Great Stop switchs
*/
void onForGreatStops(byte pitch){
  // Great Stop To Principal Pipes
  if (StopSwitchStates[GreatOpenDiapason8_15]) {
    tempNoteOn(TempPrincipalPipesState, pitch);
  }
  // Great Stop To Flute Pipes
  if (StopSwitchStates[GreatLieblich8_14]) {
    tempNoteOn(TempFlutePipesState, pitch);
  }
  // Great Stop To String Pipes
  if (StopSwitchStates[GreatSalicional8_13]) {
    tempNoteOn(TempStringPipesState, pitch);
  }
  // Great Stop To DONT KNOW YET
  if (StopSwitchStates[GreatGemsHorn4_12]) {
    tempNoteOn(TempPrincipalPipesState, pitch + OCTAVE);
  }
  // Great Stop To DONT KNOW YET
  if (StopSwitchStates[GreatSalicet4_11]) {
    tempNoteOn(TempStringPipesState, pitch + OCTAVE);
  }
  // Great Stop To Flute Rank Plus a third
  if (StopSwitchStates[GreatNazard22thirds_10]) {
    tempNoteOn(TempFlutePipesState, pitch + TWELFTH);
  }
  // Great Stop To Reeds
  if (StopSwitchStates[GreatHorn8_9]) {
    tempNoteOn(TempReedPipesState, pitch);
  }
  // Great Stop To Reeds + Octave
  if (StopSwitchStates[GreatClarion4_8]) {
    tempNoteOn(TempReedPipesState, pitch + OCTAVE);
  }
}

/**
 * Enables output notes for a given pitch based on the Pedal Stop switchs
*/
void onForPedalStops(u_long keyboardState[], byte pitch){
  // Great Stop To string Pipes
  if (StopSwitchStates[PedalBassFlute8_20]) {
    tempNoteOn(TempPrincipalPipesState, pitch); // Great Stop To Principal Pipes
    tempNoteOn(TempStringPipesState, pitch);
  }
  // Great Stop To Bourdon Pipes
  if (StopSwitchStates[PedalBourdon16_19]) {
    tempNoteOn(TempFlutePipesState, pitch);
  }
}

void resetTempState(){
  for(int i = 0; i < NOTES_STATE_ARRAY_SIZE; i++){
    TempFlutePipesState[i] = 0UL;
    TempPrincipalPipesState[i] = 0UL;
    TempStringPipesState[i] = 0UL;
    TempReedPipesState[i] = 0UL;
  }
}

void updateOutputState(u_long outputState[], u_long newState[], int channel, byte pitch){
  if(getStateBit(newState,pitch)){
    setNoteStateOn(outputState, pitch, channel);
  } else {
    setNoteStateOff(outputState, pitch, channel);
  }
}

void updateOutputStates(){
  //Clear out the temp state, so it can be constructed by combining the keyboard states and the active stop switches
  resetTempState();

  readMidi(); // Keep input buffer clear. Gotta go fast.

  //Build up the temporary state for each note/keyboard/stop switch combination
  for(int pitch = 0; pitch < NOTES_SIZE; pitch++){
    if(getStateBit(SwellState, pitch)){ // This note is pressed down on the Swell keyboard
      onForSwellStops(pitch);
    }
    readMidi(); // Keep input buffer clear. Gotta go fast.
    if(getStateBit(GreatState, pitch)){// This note is pressed down on the Great keyboard
      onForGreatStops(pitch);

      // We can send the state of any keyboard to one of the updateFor___Stop methods to apply those switch
      // settings to the active keys. This lets us reuse the same code when applying the Swell Stop panel to
      // the Great keyboard.

      // NOTE: You might need to change this around based on how the logic is supposed to work.
      // I might have the Stops panel and keyboards inverted. It should be easy enough to move around.
      if (StopSwitchStates[SwellToGreat_18]) {
        onForSwellStops(pitch);
      }
    }
    readMidi();// Keep input buffer clear. Gotta go fast.. Gotta go fast.

    if(getStateBit(PedalState, pitch)){ // This note is pressed down on the Pedal keyboard
      onForPedalStops(PedalState, pitch);
      if (StopSwitchStates[SwellToPedal_17]) {
        onForSwellStops(pitch);
      }
      if (StopSwitchStates[GreatToPedal_16]) {
        onForGreatStops(pitch);
      }
      readMidi(); // Keep input buffer clear. Gotta go fast.
    }
    readMidi(); // Keep input buffer clear. Gotta go fast.
  }

  // The temp output states now contain all of the active notes. Update the current output state and send
  // MIDI Off/On messages for any output notes that have changed.
  for(byte pitch = 0; pitch < NOTES_SIZE; pitch++){
     updateOutputState(FlutePipesState, TempFlutePipesState, FlutePipesChannel, pitch);
     
     readMidi(); // Keep input buffer clear. Gotta go fast.
     updateOutputState(PrincipalPipesState, TempPrincipalPipesState, PrincipalPipesChannel, pitch);
     readMidi(); // Keep input buffer clear. Gotta go fast.
     updateOutputState(StringPipesState, TempStringPipesState, StringPipesChannel, pitch);
     readMidi(); // Keep input buffer clear. Gotta go fast.
     updateOutputState(ReedPipesState, TempReedPipesState, ReedPipesChannel, pitch);
     
     // Prevent buffer issues by proactively reading and writing pending messages
     // It's safe to do here because reading new midi notes will will not effect the output states
     readMidi(); // Keep input buffer clear. Gotta go fast.
     sendMidi();
  }
}


///////////////////////////////////////////////////////////////////
//  MIDI Handlers - These need to execute quickly/do minimal work
/////////////


/**
 * One of the keyboard notes is now on. Update the state.
*/
void handleMidiNoteOn(byte channel, byte pitch, byte velocity)
{
  if(panicking){
    // I'm in danger :)
    // Ignore the note
    return;
  }
  handelerExecuted = true;

  if(channel == SwellChannel){
    setStateBit(SwellState, pitch, ON);
  }
  if (channel == GreatChannel){
    setStateBit(GreatState, pitch, ON);
  }
  if (channel == PedalChannel){
    setStateBit(PedalState, pitch, ON);
  }
}

/**
 * One of the keyboard notes is now off. Update the state.
 * 
 * The handleMidiNoteOn and handleMidiNotOff methods should be the only
 * places that 
*/
void handleMidiNoteOff(byte channel, byte pitch, byte velocity)
{
  if(panicking){
    // I'm in danger :)
    // Ignore the note
    return;
  }
  handelerExecuted = true;

  if(channel == SwellChannel){
    setStateBit(SwellState, pitch, OFF);
  }
  if (channel == GreatChannel){
    setStateBit(GreatState, pitch, OFF);
  }
  if (channel == PedalChannel){
    setStateBit(PedalState, pitch, OFF);
  }
}

/**
 * Reads all incoming midi messages. This will  block until no MIDI.read() calls
 * result in a handler being executed. This should be called AGRESSIVELY for best
 * performance. The handlers only record what notes were pressed, the application
 * loop will make calculate what needs to be done with the state of the notes.
*/
void readMidi(){
  do {
    // Read incoming messages until there are no more messages to read. This is
    // to prevent potential buffering issues if there are too many incoming messages
    // at once.
    handelerExecuted = false;
    MIDI.read();
  } while (!panicking && handelerExecuted);

}

// MIDI ON and MIDI OFF messages are 2 bytes each.  Our Tx buffer is 64 bytes, so it can hold
// a maximum of 32 ON/OFF messages before it starts overwriting itself. Limit to a lower
// capacity to ensure we don't overload the Serial output buffer. We can probably tweak this
// up or down based on performance
const int MAX_MIDI_SENDS_PER_CALL = 24;

void sendMidi(){
  for(int i = 0; i < MAX_MIDI_SENDS_PER_CALL; i++){
    if(!popAndSendMidi()){
      // The buffer must be empty, nothing left to do for this send batch
      return;
    }
    readMidi();
  }
  //Serial.flush();
}

////////////////////////////////////////////////
//
//  Setup and Loop
//
////////////////

void setup() {

  MIDI.setHandleNoteOn(handleMidiNoteOn); 
  MIDI.setHandleNoteOff(handleMidiNoteOff);

  MIDI.begin(MIDI_CHANNEL_OMNI);

  #ifdef LOCAL_TESTING_MODE
  // We need to use 115200 for the 'Hairless MIDI Serial Bridge so we can
  // test over usb serial and route to loopback midi devices for local
  // development
  Serial.begin(115200);
  #endif

  MIDI.turnThruOff();

  pinMode(SwellOpenDiapason8_7, INPUT);
  pinMode(SwellStoppedDiapason8_6, INPUT);
  pinMode(SwellPrincipal4_5, INPUT);
  pinMode(SwellFlute4_4, INPUT);
  pinMode(SwellFifteenth2_3, INPUT);
  pinMode(SwellTwelfth22thirds_2, INPUT);
  
  pinMode(GreatOpenDiapason8_15, INPUT);
  pinMode(GreatLieblich8_14, INPUT);
  pinMode(GreatSalicional8_13, INPUT);
  pinMode(GreatGemsHorn4_12, INPUT);
  pinMode(GreatSalicet4_11, INPUT);
  pinMode(GreatNazard22thirds_10, INPUT);
  pinMode(GreatHorn8_9, INPUT);
  pinMode(GreatClarion4_8, INPUT);

  pinMode(PedalBassFlute8_20, INPUT);
  pinMode(PedalBourdon16_19, INPUT);

  pinMode(SwellToGreat_18, INPUT);
  pinMode(SwellToPedal_17, INPUT);
  pinMode(GreatToPedal_16, INPUT);
  pinMode(PanicButton_21, INPUT);

  panic();
}


//////////////////////////////////////////////
//
//   Panic Code
//
///////////////////


void panicChannel(byte channel, byte pitch){  
    MIDI.sendNoteOff(pitch, DEFAULT_OUTPUT_VELOCITY, channel);
    // Flush inbetween each off to ensure the buffer doesn't fill up
    //Serial.flush();
    // Read incoming message
    MIDI.read();
}

/**
 * Reset all of the state arrays, output buffers, and send a MIDI off message
 * to every pipe channel for every note
*/
void panic() {

  panicking = true;
  handelerExecuted = false;

  // Send MIDI OFF messages to every pipe channel for every note
  for(int pitch = 0; pitch < NOTES_SIZE; pitch++){
    panicChannel(StringPipesChannel, pitch);
    panicChannel(PrincipalPipesChannel, pitch);
    panicChannel(FlutePipesChannel, pitch);
    panicChannel(ReedPipesChannel, pitch);
  }

  resetStateArrays();
  resetOutputBuffer();

  panicking = false;
}

/**
 * Panic that's called when the output buffer is overrun. This shouldn't happen unless
 * there are more than RING_BUFFER_MAX_SIZE that have not been written with MIDI.send___()
*/
void panicAndPause(){
  panic();
  panicking = true;

  // Lets pause for 5 seconds
  unsigned long waitTime = 5 * 1000;
  unsigned long start = millis();
  unsigned long end = start + waitTime;
  while(millis() < end){
    // Read the input buffers the entire time to keep them clear
    MIDI.read();
  }
  panicking = false;
  // Time to relax, now that it's all over. Grab a beer :D
}

void loop() {

  // TODO: Determine the correct evaluation for the panic button based on using a pulldown resistor or not
  // on the analog input pin.
  boolean panicButtonOn = false; //analogRead(PanicButton_21) > 200 // Pin D21/A7 is analog input only
  if(panicButtonOn){ // Pin D21/A7 is analog input only
    // Have a panic attack!!!
    panicAndPause();
    // Relax, all good now :)
  }

  // Read incoming midi messages and update the state of each keyboard, first.
  // We want the keyboard state to represent what is physically being pressed,
  // not the translated keys. Keys will be routed based on the stop switches. We'll
  // combine the physical keys being pressed with the stop switches that are 
  // enabled to determine what output notes and channels should be active at every
  // point in time
  readMidi();

  // Update the Stop Switch states
  updateStopStates();

  #ifdef LOCAL_TESTING_MODE
  pullOutAllTheStops(); // ALL THE STOPS!!!
  #endif
  
  readMidi();

  //Combine with keyboard states into temporary buffers. Update output states with
  //temporary states, comparing to send MIDI On/Off messages.
  updateOutputStates();

  readMidi();
  
  // Sends midi messages from the output ring buffer. This is rate limited by MAX_MIDI_SENDS_PER_LOOP
  // Notes that can't be sent will remain in the output ring buffer until the next loop, until we've
  // sent everything.
  sendMidi();

}