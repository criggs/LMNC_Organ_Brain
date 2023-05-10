// LMNC Organ Brain Implementation

/**
 * The general approach of this implementation is the following
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

#define ON true
#define OFF false

//////////////////// DEBUG FLAGS /////////////////////
// const boolean DEBUG = false;
// Enable this to inject some test notes and pause for input on each iteration.
// This only works if DEBUG is also set to true.
// const boolean DEBUG_INJECT_NOTES = false;
///////////////////////////////////////////////////////////////////


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

/**
 * Note Constants
*/
const int OCTAVE = 12;
const int TWO_OCTAVE = 24;
const int TWELFTH = 31;
const byte DEFAULT_OUTPUT_VELOCITY = 100;

/**
 * MIDI Channels
 */ 
const byte SwellChannel = 3; //swell keyboard midi input
const byte GreatChannel = 2; //Great keyboard midi input
const byte PedalChannel = 1; //Pedal keyboard midi input

//Output Channels
const byte PrincipalPipesChannel = 13;
const byte StringPipesChannel = 14;
const byte FlutePipesChannel = 15;
const byte ReedPipesChannel = 16;

/**
 * Organ Stop Pins
*/
const byte SwellOpenDiapason8 = 7; //Swell Stop Open Diapason 8
const byte SwellStoppedDiapason8 = 6; //Swell Stop Stopped Diapason 8
const byte SwellPrincipal4 = 5; //Swell Stop Principal 4
const byte SwellFlute4 = 4; //Swell Stop Principal 4
const byte SwellFifteenth2 = 3; //Swell Stop Fifteenth 2
const byte SwellTwelfth22thirds = 2; //Swell Stop twelfth 2 2/3

const byte GreatOpenDiapason8 = 15; //Great Stop Open Diapason 8
const byte GreatLieblich8 = 14; // Great Stop Lieblich 8
const byte GreatSalicional8 = 13; //Great Stop Salicional 8 NEED TO REMOVE ARDUINO LED TO MAKE THIS WORK
const byte GreatGemsHorn4 = 12; //Great Stop GemsHorn 4 dont know yet
const byte GreatSalicet4 = 11; //Great Stop Salicet 4
const byte GreatNazard22thirds = 10; //Great Stop Nazard 2 2/3
const byte GreatHorn8 = 9; //Great Stop Horn 8
const byte GreatClarion4 = 8; //Great Stop Clarion 4

const byte PedalBassFlute8 = 20; //Pedal BassFlute 8
const byte PedalBourdon16 = 19; //Pedal Bourdon 16

const byte SwellToGreat = 18;
const byte SwellToPedal = 17;
const byte GreatToPedal = 16;

/**
 * Execution flags to handle different program states
*/
boolean panicking = false; // Let functions know if we're in panic mode
boolean handelerExecuted = false; // Keeps track of whether a note was handled or not

/**
 * State Arrays for Input and Output channels
*/
const byte STOP_STATES_SIZE = 21;
boolean StopSwitchStates[STOP_STATES_SIZE] = {};

const byte NOTES_SIZE = 128;

// State for the input keyboards
boolean SwellState[NOTES_SIZE] = {};
boolean GreatState[NOTES_SIZE] = {};
boolean PedalState[NOTES_SIZE] = {};

//State for the output channels
boolean PrincipalPipesState[NOTES_SIZE] = {};
boolean StringPipesState[NOTES_SIZE] = {};
boolean FlutePipesState[NOTES_SIZE] = {};
boolean ReedPipesState[NOTES_SIZE] = {};

//Temporary state to combinie keboard and stop states
boolean TempPrincipalPipesState[NOTES_SIZE] = {};
boolean TempStringPipesState[NOTES_SIZE] = {};
boolean TempFlutePipesState[NOTES_SIZE] = {};
boolean TempReedPipesState[NOTES_SIZE] = {};


/////////////////////////////////////////
//  Function Definitions needed for C in Platform.io
/////////////
void handleMidiNoteOn(byte channel, byte pitch, byte velocity);
void handleMidiNoteOff(byte channel, byte pitch, byte velocity);

////////////////////////////////////////////////
//  Output Buffer Code
//////////////////////

const int RING_BUFFER_MAX_SIZE = 64;

// We're using words to store the note in the lower byte and the channel and on/off in the upper byte
word outputRingBuffer[RING_BUFFER_MAX_SIZE]  = {};

int outputRingHead = 0; // Start of buffer (where to read from)
int outputRingTail = 0; // End of buffer (where to write to)
int outputRingSize = 0; // Number of pending output bytes. If this ever gets bigger than the max size, panic!

void panicAndPause(); // Defining here to compile as c in platform.io

/**
 * Pushes midi information to the output ring buffer
 * 
 * @returns false if the buffer is full, true if the note was added to the buffer
*/
boolean pushToOutputBuffer(byte channel, byte pitch, boolean val){
  //Check if the buffer is full (it will start overwriting queued notes if we don't do something about it)
  if (outputRingSize >= RING_BUFFER_MAX_SIZE){
    // // The buffer is full, can't do anything right now.
    //Serial.println(F("Cant push to output buffer ring. Too full."));
    return false;
  }

  // encode the note into a space efficient word (2 bytes)
  word encodedNote = pitch | (channel << 8) | ((val ? 1 : 0 ) << 15);

  // Add it to the buffer and advance the tail pointer
  outputRingBuffer[outputRingTail++] = encodedNote;
  outputRingSize++;

  // Serial.println(F("+++++++++++++++++Pushing a note+++++++++++++++++++"));
  // Serial.print(F("outputRingTail: "));
  // Serial.println(outputRingTail);
  // Serial.print(F("Size now: "));
  // Serial.println(outputRingSize);

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

  // Serial.println();
  // Serial.print(F("Send Note: "));
  // Serial.print(encodedNote);
  // Serial.print(F(" "));
  // Serial.print(pitch);
  // Serial.print(F(" "));
  // Serial.print(channel);
  // Serial.print(F(" "));
  // Serial.print(val);
  // Serial.println();

  // Send the note
  if(val){
    MIDI.sendNoteOn(pitch, DEFAULT_OUTPUT_VELOCITY, channel);
  } else {
    MIDI.sendNoteOff(pitch, DEFAULT_OUTPUT_VELOCITY, channel);  
  }
  // Serial.println(F("================Popping a note==============="));
  // Serial.print(F("outputRingHead: "));
  // Serial.println(outputRingHead);
  // Serial.print(F("Size now: "));
  // Serial.println(outputRingSize);

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

/////////////////////////////////////////////
//   State Management Code
////////////////////

/**
 * Sets the note state to the specified value. 0 for off, 1 for on.
 * @param chanelState The bitmask to change
 * @param pitch The note index/pitch to change in the mask
 * @param val false for off, true for on
 * 
 * @return true if it changed, false if it did not change
*/
boolean setNoteState(boolean chanelState[], byte pitch, boolean val){
    boolean current = chanelState[pitch];
    chanelState[pitch] = val;
    return current != val;
}

/**
 * Makes sure the note is on.
 * 
 * If the note state changed, then send the midi channel note on message to the channel.
 * 
*/
void setNoteStateOn(boolean channelState[], byte pitch, int channel)
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
void setNoteStateOff(boolean channelState[], byte pitch, int channel)
{
  // Set the state. If it changed, queue up a midi output message
  if(setNoteState(channelState, pitch, OFF)){
    if(!pushToOutputBuffer(channel, pitch, OFF)){
      panicAndPause();
    }
  }
}

void updateStopStates(){
  StopSwitchStates[SwellOpenDiapason8] = digitalRead(SwellOpenDiapason8) == HIGH;
  StopSwitchStates[SwellStoppedDiapason8] = digitalRead(SwellStoppedDiapason8) == HIGH;
  StopSwitchStates[SwellPrincipal4] = digitalRead(SwellPrincipal4) == HIGH;
  StopSwitchStates[SwellFlute4] = digitalRead(SwellFlute4) == HIGH;
  StopSwitchStates[SwellFifteenth2] = digitalRead(SwellFifteenth2) == HIGH;
  StopSwitchStates[SwellTwelfth22thirds] = digitalRead(SwellTwelfth22thirds) == HIGH;

  StopSwitchStates[GreatOpenDiapason8] = digitalRead(GreatOpenDiapason8) == HIGH;
  StopSwitchStates[GreatLieblich8] = digitalRead(GreatLieblich8) == HIGH;
  StopSwitchStates[GreatSalicional8] = digitalRead(GreatSalicional8) == HIGH;
  StopSwitchStates[GreatGemsHorn4] = digitalRead(GreatGemsHorn4) == HIGH;
  StopSwitchStates[GreatSalicet4] = digitalRead(GreatSalicet4) == HIGH;
  StopSwitchStates[GreatNazard22thirds] = digitalRead(GreatNazard22thirds) == HIGH;
  StopSwitchStates[GreatHorn8] = digitalRead(GreatHorn8) == HIGH;
  StopSwitchStates[GreatClarion4] = digitalRead(GreatClarion4) == HIGH;

  StopSwitchStates[PedalBassFlute8] = analogRead(PedalBassFlute8) > 200;
  StopSwitchStates[PedalBourdon16] = digitalRead(PedalBourdon16) == HIGH;

  // TODO: Running in a simulator was giving some weird results on some pins.
  // double check that they are configured in the correct way to read them.
  StopSwitchStates[SwellToGreat] = digitalRead(SwellToGreat) == HIGH;
  //StopStates[SwellToPedal] = digitalRead(SwellToPedal) == HIGH;
  //StopStates[GreatToPedal] = digitalRead(GreatToPedal) == HIGH;
}

/**
 * Test function to ignore stop switches and enable everything!
*/
void pullOutAllTheStops(){
  StopSwitchStates[SwellOpenDiapason8] = true;
  StopSwitchStates[SwellStoppedDiapason8] = true;
  StopSwitchStates[SwellPrincipal4] = true;
  StopSwitchStates[SwellFlute4] = true;
  StopSwitchStates[SwellFifteenth2] = true;
  StopSwitchStates[SwellTwelfth22thirds] = true;

  StopSwitchStates[GreatOpenDiapason8] = true;
  StopSwitchStates[GreatLieblich8] = true;
  StopSwitchStates[GreatSalicional8] = true;
  StopSwitchStates[GreatGemsHorn4] = true;
  StopSwitchStates[GreatSalicet4] = true;
  StopSwitchStates[GreatNazard22thirds] = true;
  StopSwitchStates[GreatHorn8] = true;
  StopSwitchStates[GreatClarion4] = true;

  StopSwitchStates[PedalBassFlute8] = true;
  StopSwitchStates[PedalBourdon16] = true;

  // TODO: Running in a simulator was giving some weird results on some pins.
  // double check that they are configured in the correct way to read them.
  StopSwitchStates[SwellToGreat] = digitalRead(SwellToGreat) == HIGH;
  //StopStates[SwellToPedal] = digitalRead(SwellToPedal) == HIGH;
  //StopStates[GreatToPedal] = digitalRead(GreatToPedal) == HIGH;
}

/**
 * Set pitch for the temp state to ON, with array bounds checking
*/
void tempNoteOn(boolean tempState[], byte pitch){
  if(pitch >= 0 && pitch < NOTES_SIZE) {
    tempState[pitch] = ON;
  }
}

/**
 * Enables output notes for a given pitch based on the Swell Stop switchs
*/
void onForSwellStops(byte pitch) {
  // Swell Stop To Principal Pipes
  if (StopSwitchStates[SwellOpenDiapason8]) {
    tempNoteOn(TempPrincipalPipesState, pitch);
  }
  // Swell Stop To Flute Pipes
  if (StopSwitchStates[SwellStoppedDiapason8]) {
    tempNoteOn(TempFlutePipesState, pitch);
  }
  // Swell Stop To Principal Pipes + 1 Octave
  if (StopSwitchStates[SwellPrincipal4]) {
    tempNoteOn(TempPrincipalPipesState, pitch + OCTAVE);
  }
  // Swell Stop To Flute Pipes + 1 & 2 Octave
  if (StopSwitchStates[SwellFlute4]) {
    tempNoteOn(TempFlutePipesState, pitch + OCTAVE); //Swell Stop To Flute Pipes + 1 Octave
    tempNoteOn(TempFlutePipesState, pitch + TWO_OCTAVE);
  }
  // Swell Stop To Principal Pipes + 2 Octave
  if (StopSwitchStates[SwellFifteenth2]) {
    tempNoteOn(TempPrincipalPipesState, pitch + TWO_OCTAVE);
  }
  // Swell Stop To Principal Pipes + 2 Octave and a fifth
  if (StopSwitchStates[SwellTwelfth22thirds]) {
    tempNoteOn(TempPrincipalPipesState, pitch + TWELFTH);
  }
}

/**
 * Enables output notes for a given pitch based on the Great Stop switchs
*/
void onForGreatStops(byte pitch){
  // Great Stop To Principal Pipes
  if (StopSwitchStates[GreatOpenDiapason8]) {
    tempNoteOn(TempPrincipalPipesState, pitch);
  }
  // Great Stop To Flute Pipes
  if (StopSwitchStates[GreatLieblich8]) {
    tempNoteOn(TempFlutePipesState, pitch);
  }
  // Great Stop To String Pipes
  if (StopSwitchStates[GreatSalicional8]) {
    tempNoteOn(TempStringPipesState, pitch);
  }
  // Great Stop To DONT KNOW YET
  if (StopSwitchStates[GreatGemsHorn4]) {
    tempNoteOn(TempPrincipalPipesState, pitch + OCTAVE);
  }
  // Great Stop To DONT KNOW YET
  if (StopSwitchStates[GreatSalicet4]) {
    tempNoteOn(TempStringPipesState, pitch + OCTAVE);
  }
  // Great Stop To Flute Rank Plus a third
  if (StopSwitchStates[GreatNazard22thirds]) {
    tempNoteOn(TempFlutePipesState, pitch + TWELFTH);
  }
  // Great Stop To Reeds
  if (StopSwitchStates[GreatHorn8]) {
    tempNoteOn(TempReedPipesState, pitch);
  }
  // Great Stop To Reeds + Octave
  if (StopSwitchStates[GreatClarion4]) {
    tempNoteOn(TempReedPipesState, pitch + OCTAVE);
  }
}

/**
 * Enables output notes for a given pitch based on the Pedal Stop switchs
*/
void onForPedalStops(boolean keyboardState[], byte pitch){
  // Great Stop To string Pipes
  if (StopSwitchStates[PedalBassFlute8]) {
    tempNoteOn(TempPrincipalPipesState, pitch); // Great Stop To Principal Pipes
    tempNoteOn(TempStringPipesState, pitch);
  }
  // Great Stop To Bourdon Pipes
  if (StopSwitchStates[PedalBourdon16]) {
    tempNoteOn(TempFlutePipesState, pitch);
  }
}

void resetTempState(){
  for(int i = 0; i < NOTES_SIZE; i++){
    TempFlutePipesState[i] = OFF;
    TempPrincipalPipesState[i] = OFF;
    TempStringPipesState[i] = OFF;
    TempReedPipesState[i] = OFF;
  }
}

void updateOutputState(boolean outputState[], boolean newState[], int channel, byte pitch){
  if(newState[pitch]){
    setNoteStateOn(outputState, pitch, channel);
  } else {
    setNoteStateOff(outputState, pitch, channel);
  }
}

void updateOutputStates(){
  //Clear out the temp state, so it can be constructed by combining the keyboard states and the active stop switches
  resetTempState();

  //Build up the temporary state for each note/keyboard/stop switch combination
  for(int pitch = 0; pitch < NOTES_SIZE; pitch++){
    if(SwellState[pitch]){ // This note is pressed down on the Swell keyboard
      onForSwellStops(pitch);
    }
    if(GreatState[pitch]){// This note is pressed down on the Great keyboard
      onForGreatStops(pitch);

      // We can send the state of any keyboard to one of the updateFor___Stop methods to apply those switch
      // settings to the active keys. This lets us reuse the same code when applying the Swell Stop panel to
      // the Great keyboard.

      // NOTE: You might need to change this around based on how the logic is supposed to work.
      // I might have the Stops panel and keyboards inverted. It should be easy enough to move around.
      if (StopSwitchStates[SwellToGreat]) {
        onForSwellStops(pitch);
      }
    }
    if(PedalState[pitch]){ // This note is pressed down on the Pedal keyboard
      onForPedalStops(PedalState, pitch);
      if (StopSwitchStates[SwellToPedal]) {
        onForSwellStops(pitch);
      }
      if (StopSwitchStates[GreatToPedal]) {
        onForGreatStops(pitch);
      }
    }
    
  }
  
  // The temp output states now contain all of the active notes. Update the current output state and send
  // MIDI Off/On messages for any output notes that have changed.
  for(byte pitch = 0; pitch < NOTES_SIZE; pitch++){
    updateOutputState(FlutePipesState, TempFlutePipesState, FlutePipesChannel, pitch);
    updateOutputState(PrincipalPipesState, TempPrincipalPipesState, PrincipalPipesChannel, pitch);
    updateOutputState(StringPipesState, TempStringPipesState, StringPipesChannel, pitch);
    updateOutputState(ReedPipesState, TempReedPipesState, ReedPipesChannel, pitch);
  }
}


/////////////////////////////////////////////
//     Debug Helpers
//////////////////

// void printBits(boolean bits[]) {
//   for(int i = 0; i < NOTES_SIZE; i++) {
//     Serial.print(bits[i]);
//   }
// }

// void dumpState(char* channelName, boolean channelState[]){
//   Serial.print(channelName);
//   Serial.print(F(": "));
//   printBits(channelState);
//   Serial.println();
// }

// void dumpStates() {
//   Serial.println(F("Input Keyboards"));
//   dumpState("Swell     ", SwellState);
//   dumpState("Great     ", GreatState);
//   dumpState("Pedal     ", PedalState);

//   Serial.println(F("Output Channels"));
//   //State for the output channels
//   dumpState("Principal ", PrincipalPipesState);
//   dumpState("String    ", StringPipesState);
//   dumpState("Flute     ", FlutePipesState);
//   dumpState("Reed      ", ReedPipesState);

//   Serial.print(F("Stops: "));
//   for(int i = 0; i < STOP_STATES_SIZE; i++){
//     Serial.print(StopStates[i]);
//     Serial.print(" ");
//   }
//   Serial.println();

// }


// /**
//  * When debug is enabled, send some test note events to evaluate the output
// */
// int debugCounter = 0;
// void injectTestKeys(){
//   int counterStep = debugCounter % 5;
//   //Serial.print("Injecting Keys, counterStep at ");
//   //Serial.println(counterStep);

//   int pitch = 60;

//   if(counterStep == 0){
//     handleMidiNoteOn(SwellChannel, pitch, 100);
//     handleMidiNoteOn(PedalChannel, pitch, 100);
//     handleMidiNoteOn(GreatChannel, pitch, 100);
//     handleMidiNoteOn(PedalChannel, 20, 100);
//   }

//   if(counterStep == 1){
//     handleMidiNoteOff(PedalChannel, pitch, 100);
//     //Should still be on
//   }

//   if(counterStep == 3){
//     for(int i = 0; i < 2; i++){
//       handleMidiNoteOff(SwellChannel, i, 100);
//       handleMidiNoteOff(PedalChannel, i+20, 100);
//       handleMidiNoteOff(GreatChannel, i+40, 100);
//       handleMidiNoteOff(PedalChannel, i+60, 100);
//     }
//     //Should still be on
//   }

//   if(counterStep == 2){
//     handleMidiNoteOff(SwellChannel, pitch, 100);
//     handleMidiNoteOff(PedalChannel, pitch, 100);
//     //Should actually be off now
//   }

//   if(counterStep == 4){
//     for(int i = 0; i < 2; i++){
//       handleMidiNoteOn(SwellChannel, i, 100);
//       handleMidiNoteOn(PedalChannel, i+20, 100);
//       handleMidiNoteOn(GreatChannel, i+40, 100);
//       handleMidiNoteOn(PedalChannel, i+60, 100);
//     }
//   }
  
//   debugCounter++;
// }

//////////////////////////////////////////////
//   Panic Code
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

  // if(DEBUG){
  //   Serial.println("Panic!");
  // }

  panicking = true;
  handelerExecuted = false;

  for(int pitch = 0; pitch < NOTES_SIZE; pitch++){
    FlutePipesState[pitch] = OFF;
    PrincipalPipesState[pitch] = OFF;
    StringPipesState[pitch] = OFF;
    ReedPipesState[pitch] = OFF;

    TempFlutePipesState[pitch] = OFF;
    TempPrincipalPipesState[pitch] = OFF;
    TempStringPipesState[pitch] = OFF;
    TempReedPipesState[pitch] = OFF;
    
    panicChannel(StringPipesChannel, pitch);
    panicChannel(PrincipalPipesChannel, pitch);
    panicChannel(FlutePipesChannel, pitch);
    panicChannel(ReedPipesChannel, pitch);
  }

  resetOutputBuffer();

  panicking = false;
}

/**
 * Panic that's called when the output buffer is overrun. This shouldn't happen unless
 * there are more than RING_BUFFER_MAX_SIZE that have not been written with MIDI.send___()
*/
void panicAndPause(){

  // Serial.println(F(""));
  // Serial.println(F(""));
  // Serial.println(F("PANIC!!!!"));
  // Serial.println(F(""));
  // Serial.println(F(""));

  panic();
  panicking = true;

  // Lets pause for 10 seconds
  unsigned long waitTime = 10 * 1000;
  unsigned long start = millis();
  unsigned long end = start + waitTime;
  while(millis() < end){
    // Read the input buffers the entire time to keep them clear
    MIDI.read();
  }
  panicking = false;
  // Time to relax, now that it's all over. Grab a beer :D
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

  //   Serial.print("Handle Note On c");
  //   Serial.print(channel);
  //   Serial.print(" ");
  //   Serial.println(pitch);

  if(channel == SwellChannel){
    setNoteState(SwellState, pitch, ON);
  }
  if (channel == GreatChannel){
    setNoteState(GreatState, pitch, ON);
  }
  if (channel == PedalChannel){
    setNoteState(PedalState, pitch, ON);
  }
}

/**
 * One of the keyboard notes is now off. Updat the state.
*/
void handleMidiNoteOff(byte channel, byte pitch, byte velocity)
{
  if(panicking){
    // I'm in danger :)
    // Ignore the note
    return;
  }
  handelerExecuted = true;

  // Serial.print(F("Handle Note Off c"));
  // Serial.print(channel);
  // Serial.print(F(" "));
  // Serial.println(pitch);

  if(channel == SwellChannel){
    setNoteState(SwellState, pitch, OFF);
  }
  if (channel == GreatChannel){
    setNoteState(GreatState, pitch, OFF);
  }
  if (channel == PedalChannel){
    setNoteState(PedalState, pitch, OFF);
  }
}

/**
 * Reads all incoming midi messages. This will  block until no MIDI.read() calls
 * result in a handler being executed.
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
const int MAX_MIDI_SENDS_PER_LOOP = 8;
void sendMidi(){
  for(int i = 0; i < MAX_MIDI_SENDS_PER_LOOP; i++){
    if(!popAndSendMidi()){
      // The buffer must be empty, nothing left to do for this send batch
      return;
    }
  }
  //Serial.flush();
}

////////////////////////////////////////////////
//  Setup and Loop
////////////////

void setup() {

  MIDI.setHandleNoteOn(handleMidiNoteOn); 
  MIDI.setHandleNoteOff(handleMidiNoteOff);

  MIDI.begin(MIDI_CHANNEL_OMNI);
  Serial.begin(115200);
  MIDI.turnThruOff();

  pinMode(SwellOpenDiapason8, INPUT);
  pinMode(SwellStoppedDiapason8, INPUT);
  pinMode(SwellPrincipal4, INPUT);
  pinMode(SwellFlute4, INPUT);
  pinMode(SwellFifteenth2, INPUT);
  pinMode(SwellTwelfth22thirds, INPUT);
  
  pinMode(GreatOpenDiapason8, INPUT);
  pinMode(GreatLieblich8, INPUT);
  pinMode(GreatSalicional8, INPUT);
  pinMode(GreatGemsHorn4, INPUT);
  pinMode(GreatSalicet4, INPUT);
  pinMode(GreatNazard22thirds, INPUT);
  pinMode(GreatHorn8, INPUT);
  pinMode(GreatClarion4, INPUT);

  pinMode(PedalBassFlute8, INPUT);
  pinMode(PedalBourdon16, INPUT);

  pinMode(SwellToGreat, INPUT);
  pinMode(SwellToPedal, INPUT);
  pinMode(GreatToPedal, INPUT);

  panic();
}


void loop() {
  // Read incoming midi messages and update the state of each keyboard, first.
  // We want the keyboard state to represent what is physically being pressed,
  // not the translated keys that will be routed by the stop switches. We'll
  // combine the physical keys being pressed with the stop switches that are 
  // enabled to determine what output notes and channels should be active
  readMidi();

  // Update the Stop Switch states
  //updateStopStates();

  // TEST/DEBUG: uncomment to force all stops to be enabled
  // pullOutAllTheStops();


  //StopStates[SwellOpenDiapason8] = true;
  //StopStates[SwellPrincipal4] = true;
  //StopStates[SwellFlute4] = true;
  //StopStates[SwellTwelfth22thirds] = true;

  //StopStates[GreatOpenDiapason8] = true;
  //StopStates[GreatSalicional8] = true;
  //StopStates[GreatHorn8] = true;
  StopSwitchStates[GreatClarion4] = true;

  //StopStates[PedalBassFlute8] = true;
  //StopStates[PedalBourdon16] = true;


  //Combine with keyboard states into temporary buffers. Update output states with
  //temporary states, comparing to send MIDI On/Off messages.
  updateOutputStates();

  // Sends midi messages from the output ring buffer. This is rate limited by MAX_MIDI_SENDS_PER_LOOP
  // Notes that can't be sent will remain in the output ring buffer until the next loop, until we've
  // sent everything.
  sendMidi();

  // TEST/DEBUG
  //injectTestKeys();

}