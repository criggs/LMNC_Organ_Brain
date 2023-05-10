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

MIDI_CREATE_DEFAULT_INSTANCE();

const boolean DEBUG = false;

// Enable this to inject some test notes and pause for input on each iteration.
// This only works if DEBUG is also set to true.
const boolean DEBUG_INJECT_NOTES = false;

byte MIDIchannel;
int channelout;
int OCTAVE = 12;
int TWO_OCTAVE = 24;
int TWELFTH = 31;

const byte DEFAULT_OUTPUT_VELOCITY = 100;

int SwellChannel = 3; //swell keyboard midi input
int GreatChannel = 2; //Great keyboard midi input
int PedalChannel = 1; //Pedal keyboard midi input

int SwellOpenDiapason8 = 7; //Swell Stop Open Diapason 8
int SwellStoppedDiapason8 = 6; //Swell Stop Stopped Diapason 8
int SwellPrincipal4 = 5; //Swell Stop Principal 4
int SwellFlute4 = 4; //Swell Stop Principal 4
int SwellFifteenth2 = 3; //Swell Stop Fifteenth 2
int SwellTwelfth22thirds = 2; //Swell Stop twelfth 2 2/3

int GreatOpenDiapason8 = 15; //Great Stop Open Diapason 8
int GreatLieblich8 = 14; // Great Stop Lieblich 8
int GreatSalicional8 = 13; //Great Stop Salicional 8 NEED TO REMOVE ARDUINO LED TO MAKE THIS WORK
int GreatGemsHorn4 = 12; //Great Stop GemsHorn 4 dont know yet
int GreatSalicet4 = 11; //Great Stop Salicet 4
int GreatNazard22thirds = 10; //Great Stop Nazard 2 2/3
int GreatHorn8 = 9; //Great Stop Horn 8
int GreatClarion4 = 8; //Great Stop Clarion 4

int PedalBassFlute8 = 20; //Pedal BassFlute 8
int PedalBourdon16 = 19; //Pedal Bourdon 16

int SwellToGreat = 18;
int SwellToPedal = 17;
int GreatToPedal = 16;

// Keeps track of whether a note was handled or not
boolean handelerExecuted = false;

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

//Output Channels
int PrincipalPipesChannel = 13;
int StringPipesChannel = 14;
int FlutePipesChannel = 15;
int ReedPipesChannel = 16;

/**
 * Sets the note state to the specified value. 0 for off, 1 for on.
 * @param chanelState The bitmask to change
 * @param pitch The note index/pitch to change in the mask
 * @param val 0 for off, 1 for on
 * 
 * @return true if it changed, false if it did not change
*/
boolean setNoteState(boolean chanelState[], byte pitch, boolean val){
    boolean current = chanelState[pitch];
    chanelState[pitch] = val;
    return current != val;
}

/**
 * One of the keyboard notes is now on. Update the state.
*/
void handleMidiNoteOn(byte channel, byte pitch, byte velocity)
{
  handelerExecuted = true;
  if(DEBUG){
    Serial.print("Handle Note On c");
    Serial.print(channel);
    Serial.print(" ");
    Serial.println(pitch);
  }
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
  handelerExecuted = true;
  if(DEBUG){
    Serial.print("Handle Note Off c");
    Serial.print(channel);
    Serial.print(" ");
    Serial.println(pitch);
  }
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

void flushSerialBufferIfNeeded(){
  if(serialBufferFlushCounter++ > MAX_MESSAGES_BETWEEN_FLUSHES){
    Serial.flush();
    serialBufferFlushCounter = 0;
  }
}

/**
 * Makes sure the note is on.
 * 
 * If the note state changed, then send the midi channel note on message to the channel.
 * 
*/
void setNoteStateOn(boolean channelState[], byte pitch, int channel)
{
  if(setNoteState(channelState, pitch, ON)){
    //send off to channel
    if(DEBUG){
      Serial.print("Note ON c");
      Serial.print(channel);
      Serial.print(" ");
      Serial.println(pitch);
    }
    MIDI.sendNoteOn(pitch, DEFAULT_OUTPUT_VELOCITY, channel);
    flushSerialBufferIfNeeded();
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
  if(setNoteState(channelState, pitch, OFF)){
    //send off to channel
    if(DEBUG){
      Serial.print("Note OFF c");
      Serial.print(channel);
      Serial.print(" ");
      Serial.println(pitch);
    }
    MIDI.sendNoteOff(pitch, DEFAULT_OUTPUT_VELOCITY, channel);
    flushSerialBufferIfNeeded();
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
  StopSwitchStates[SwellToPedal] = digitalRead(SwellToPedal) == HIGH;
  StopSwitchStates[GreatToPedal] = digitalRead(GreatToPedal) == HIGH;
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


/**
 * Helper functions for printing debug output to serial out
*/
void printBits(boolean bits[]) {
  for(int i = 0; i < NOTES_SIZE; i++) {
    Serial.print(bits[i]);
  }
}

void dumpState(char* channelName, boolean channelState[]){
  Serial.print(channelName);
  Serial.print(": ");
  printBits(channelState);
  Serial.println();
}

void dumpStates() {
  Serial.println("Input Keyboards");
  dumpState("Swell     ", SwellState);
  dumpState("Great     ", GreatState);
  dumpState("Pedal     ", PedalState);

  Serial.println("Output Channels");
  //State for the output channels
  dumpState("Principal ", PrincipalPipesState);
  dumpState("String    ", StringPipesState);
  dumpState("Flute     ", FlutePipesState);
  dumpState("Reed      ", ReedPipesState);

  Serial.print("Stops: ");
  for(int i = 0; i < STOP_STATES_SIZE; i++){
    Serial.print(StopSwitchStates[i]);
    Serial.print(" ");
  }
  Serial.println();

}

//----------------------------------------------------------


void panicChannel(byte channel, byte pitch){  
    MIDI.sendNoteOff(pitch, DEFAULT_OUTPUT_VELOCITY, channel);
    flushSerialBufferIfNeeded();
}

void panic() {

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
}


void setup() {

  MIDI.setHandleNoteOn(handleMidiNoteOn); 
  MIDI.setHandleNoteOff(handleMidiNoteOff);

  MIDI.begin(MIDI_CHANNEL_OMNI);
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

/**
 * When debug is enabled, send some test note events to evaluate the output
*/
int debugCounter = 0;
void injectTestKeys(){
  int counterStep = debugCounter % 5;
  Serial.print("Injecting Keys, counterStep at ");
  Serial.println(counterStep);

  int pitch = 60;

  if(counterStep == 0){
    handleMidiNoteOn(SwellChannel, pitch, 100);
    handleMidiNoteOn(PedalChannel, pitch, 100);
    handleMidiNoteOn(GreatChannel, pitch, 100);
    handleMidiNoteOn(PedalChannel, 20, 100);
  }

  if(counterStep == 1){
    handleMidiNoteOff(PedalChannel, pitch, 100);
    //Should still be on
  }

  if(counterStep == 2){
    handleMidiNoteOff(GreatChannel, pitch, 100);
    handleMidiNoteOff(PedalChannel, pitch, 100);
    handleMidiNoteOff(PedalChannel, pitch, 100);
    
    handleMidiNoteOff(PedalChannel, 20, 100);
    //Should still be on
  }

  if(counterStep == 3){
    handleMidiNoteOff(SwellChannel, pitch, 100);
    handleMidiNoteOff(PedalChannel, pitch, 100);
    //Should actually be off now
  }
  
  debugCounter++;
}

/**
 * Reads all incoming midi messages. This will  block until no MIDI.read() calls
 * result in a handler being executed.
*/
void readMidi(){
  do {
    if(DEBUG){
      Serial.println("MIDI.read()");
    }
    // Read incoming messages until there are no more messages to read. This is
    // to prevent potential buffering issues if there are too many incoming messages
    // at once.
    handelerExecuted = false;
    MIDI.read();
  } while (handelerExecuted);

  if(DEBUG && DEBUG_INJECT_NOTES){
    injectTestKeys();
  }
}

void loop() {
  // Read incoming midi messages and update the state of each keyboard, first.
  // We want the keyboard state to represent what is physically being pressed,
  // not the translated keys that will be routed by the stop switches. We'll
  // combine the physical keys being pressed with the stop switches that are 
  // enabled to determine what output notes and channels should be active
  readMidi();

  // Update the Stop Switch states
  updateStopStates();
  
  //Combine with keyboard states into temporary buffers. Update output states with
  //temporary states, comparing to send MIDI On/Off messages.
  updateOutputStates();

  if(DEBUG){
    dumpStates();
    if(DEBUG_INJECT_NOTES){
      Serial.readString();
    }
  }
}