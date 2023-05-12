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

// Uncomment this line to force all settings for local testing,
// such as 115200 serial baud rate and force-enabling all stop switches

#define LOCAL_TESTING_MODE 1

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
#define SwellChannel 3 // swell keyboard midi input
#define GreatChannel 2 // Great keyboard midi input
#define PedalChannel 1 // Pedal keyboard midi input

/**
 * Output Channels
 */
#define PrincipalPipesChannel 13
#define StringPipesChannel 14
#define FlutePipesChannel 15
#define ReedPipesChannel 16

////////////////////////////////////
//
// Organ Stop Pins
//
/////////////////

/**
 * Swell Stops
 */
#define SwellOpenDiapason8_7 7    // Swell Stop Open Diapason 8
#define SwellStoppedDiapason8_6 6 // Swell Stop Stopped Diapason 8
#define SwellPrincipal4_5 5       // Swell Stop Principal 4
#define SwellFlute4_4 4           // Swell Stop Principal 4
#define SwellFifteenth2_3 3       // Swell Stop Fifteenth 2
#define SwellTwelfth22thirds_2 2  // Swell Stop twelfth 2 2/3

/**
 * Great Stops
 */
#define GreatOpenDiapason8_15 15  // Great Stop Open Diapason 8
#define GreatLieblich8_14 14      // Great Stop Lieblich 8
#define GreatSalicional8_13 13    // Great Stop Salicional 8 NEED TO REMOVE ARDUINO LED TO MAKE THIS WORK
#define GreatGemsHorn4_12 12      // Great Stop GemsHorn 4 dont know yet
#define GreatSalicet4_11 11       // Great Stop Salicet 4
#define GreatNazard22thirds_10 10 // Great Stop Nazard 2 2/3
#define GreatHorn8_9 9            // Great Stop Horn 8
#define GreatClarion4_8 8         // Great Stop Clarion 4

/**
 * Pedal Stops
 */
#define PedalBassFlute8_20 20 // Pedal BassFlute 8. Need to analogRead this pin
#define PedalBourdon16_19 19  // Pedal Bourdon 16

/**
 *
 * Coupter Stops
 *
 * From: https://www.ibiblio.org/pipeorgan/Pages/Console.html
 *
 * Couplers
 * Although the different divisions are controlled through different manuals, it is possible to
 * combine stops from different divisions through the use of couplers. This allows one manual to
 * control stops from several divisions and gives the organist greater flexibility in registration.
 * Couplers are labeled to indicate which division the stops are coming from and which manual (including
 * the pedals) will now control these stops. Couplers from the Holtkamp at the University of Kentucky
 * For example, the Great to Pedal coupler means that stops that stops in the Great division will now
 * be controlled by the pedal board. This is especially useful on organs that only have 16' and 8'
 * pedal stops. However, the stops on the Great will still sound if keys on the Great manual are played.
 *
 * Couplers can also connect manuals at a specific range. For example, a Swell to Great 4' means that all the
 * stops currently playing in the Swell will be copied to the Great manual an octave higher than their regular
 * pitch on the Swell. So, an 8' flute in the Swell will sound at 8' pitch on the swell but at 4' pitch on the
 * great. Common ranges of these types of couplers are 16', and 4'. While this type of coupler is helpful, it
 * is not a necessary part of the organ so you may find organs which do not have them.
 *
 * Some organs also have a Unison coupler which disconnects the manual from playing stops on its corresponding
 * division. Let's say the organist used the Great to Pedal coupler and also turned on the Great Unison: The
 * pipes in the Great division will sound when the pedal keys are played but not when the Great manual is
 * played. When the Unison is used with other couplers, it can maximize the different combinations of stops
 * available.
 *
 */
#define SwellToGreat_18 18 // Send the Swell Stops to the Great Keyboard (Great Plays Swell and Great Stops)
#define SwellToPedal_17 17 // Send the Swell Stops to the Pedal Keyboard (Pedal Plays Swell and Pedal Stops)
#define GreatToPedal_16 16 // Send the Great Stops to the Pedal Keyboard (Pedal Plays Great and Pedal Stops)
// Note: If Both the SwellToPedal and GreatToPedal are enabled, The pedals play All Stops

#define PanicButton_21 21 // Pin D21/A7 is analog input only

/**
 * Execution flags to handle different program states
 */
boolean panicking = false;        // Let functions know if we're in panic mode
boolean handelerExecuted = false; // Keeps track of whether a note was handled or not

/**
 * State Arrays for Input and Output channels
 */
#define STOP_STATES_SIZE 21
boolean StopSwitchStates[STOP_STATES_SIZE] = {};

#define NOTES_SIZE 128
#define NOTES_STATE_ARRAY_SIZE 16 // NOTES_SIZE / 8
// Each state needs to hold

// State for the input keyboards
byte SwellState[NOTES_STATE_ARRAY_SIZE] = {};
byte GreatState[NOTES_STATE_ARRAY_SIZE] = {};
byte PedalState[NOTES_STATE_ARRAY_SIZE] = {};

// State for the output channels
byte PrincipalPipesState[NOTES_STATE_ARRAY_SIZE] = {};
byte StringPipesState[NOTES_STATE_ARRAY_SIZE] = {};
byte FlutePipesState[NOTES_STATE_ARRAY_SIZE] = {};
byte ReedPipesState[NOTES_STATE_ARRAY_SIZE] = {};

// Temporary state to combinie keboard and stop states
byte TempPrincipalPipesState[NOTES_STATE_ARRAY_SIZE] = {};
byte TempStringPipesState[NOTES_STATE_ARRAY_SIZE] = {};
byte TempFlutePipesState[NOTES_STATE_ARRAY_SIZE] = {};
byte TempReedPipesState[NOTES_STATE_ARRAY_SIZE] = {};

//////////////////////////////////////////
//
//  State aray functions
//
///////////////

void printBufferState(byte noteState[])
{
  for (int i = 0; i < NOTES_STATE_ARRAY_SIZE; i++)
  {
    Serial.print(noteState[0], BIN);
    Serial.print(noteState[1], BIN);
    Serial.print(noteState[2], BIN);
    Serial.print(noteState[3], BIN);
  }
  Serial.println();
}

void resetStateArrays()
{
  for (int i = 0; i < NOTES_STATE_ARRAY_SIZE; i++)
  {
    FlutePipesState[i] = 0;
    PrincipalPipesState[i] = 0;
    StringPipesState[i] = 0;
    ReedPipesState[i] = 0;

    TempFlutePipesState[i] = 0;
    TempPrincipalPipesState[i] = 0;
    TempStringPipesState[i] = 0;
    TempReedPipesState[i] = 0;
  }
}

/**
 * Sets a single bit to on or off in the provided noteState bitmap
 */
void setBitmapBit(byte bitmap[], byte index, byte val)
{
  int byteIndex = index >> 3;
  int bitOffset = index & 7;

  if (val)
  { // Set the bit to 1
    bitmap[byteIndex] |= (1 << bitOffset);
  }
  else
  { // Set the bit to 0
    bitmap[byteIndex] &= ~(1 << bitOffset);
  }
}

/**
 * Sets a single bit to on or off in the provided noteState bitmap
 */
bool getBitmapBit(byte bitmap[], byte index)
{
  int byteIndex = index >> 3;
  int bitOffset = index & 7;
  return (bitmap[byteIndex] >> bitOffset) & 1;
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
word outputRingBuffer[RING_BUFFER_MAX_SIZE] = {};

int outputRingHead = 0; // Start of buffer (where to read from)
int outputRingTail = 0; // End of buffer (where to write to)
int outputRingSize = 0; // Number of pending output bytes. If this ever gets bigger than the max size, panic!

/**
 * Pushes midi information to the output ring buffer
 *
 * @returns false if the buffer is full, true if the note was added to the buffer
 */
boolean pushToOutputBuffer(byte channel, byte pitch, boolean val)
{
  // Check if the buffer is full (it will start overwriting queued notes if we don't do something about it)
  if (outputRingSize >= RING_BUFFER_MAX_SIZE)
  {
    // // The buffer is full, can't do anything right now.
    return false;
  }

  // encode the note into a space efficient word (2 bytes)
  word encodedNote = pitch | (channel << 8) | ((val ? 1 : 0) << 15);

  // Add it to the buffer and advance the tail pointer
  outputRingBuffer[outputRingTail++] = encodedNote;
  outputRingSize++;

  // Check if we're past the edge of the array so we can loop back around for the next one
  if (outputRingTail >= RING_BUFFER_MAX_SIZE - 1)
  {
    outputRingTail = 0;
  }

  return true;
}

/**
 * Remove an encoded midi message from the buffer and send it.
 *
 * @returns false if the buffer is empty, true if a MIDI.send__() was called
 */
boolean popAndSendMidi()
{
  if (outputRingSize <= 0)
  {
    // Buffer is empty, nothing to send
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
  if (val)
  {
    MIDI.sendNoteOn(pitch, DEFAULT_OUTPUT_VELOCITY, channel);
  }
  else
  {
    MIDI.sendNoteOff(pitch, DEFAULT_OUTPUT_VELOCITY, channel);
  }

  // Check if we're past the edge of the array so we can loop back around
  if (outputRingHead >= RING_BUFFER_MAX_SIZE - 1)
  {
    outputRingHead = 0;
  }

  return true;
}

/**
 * Clear the buffer by resetting the head, tail, and size. No need to
 * write over existing data in the buffer array.
 */
void resetOutputBuffer()
{
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
boolean setNoteState(byte chanelState[], byte pitch, boolean val)
{
  if (pitch >= 0 && pitch < NOTES_SIZE)
  {
    boolean current = getBitmapBit(chanelState, pitch);
    setBitmapBit(chanelState, pitch, val);
    return current != val;
  }
  else
  {
    return false;
  }
}

/**
 * Makes sure the note is on.
 *
 * If the note state changed, then send the midi channel note on message to the channel.
 *
 */
void setNoteStateOn(byte channelState[], byte pitch, int channel)
{
  // Set the state. If it changed, queue up a midi output message
  if (setNoteState(channelState, pitch, ON))
  {
    if (!pushToOutputBuffer(channel, pitch, ON))
    {
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
void setNoteStateOff(byte channelState[], byte pitch, int channel)
{
  // Set the state. If it changed, queue up a midi output message
  if (setNoteState(channelState, pitch, OFF))
  {
    if (!pushToOutputBuffer(channel, pitch, OFF))
    {
      panicAndPause();
    }
  }
}

/**
 * Read the specified digital pin into a StopSwitchState
 */
void digitalReadStop(byte pin)
{
  StopSwitchStates[pin] = digitalRead(pin) == HIGH;
}

/**
 * Read the specific analog pin into a StopSwitchState
 */
void analogReadStop(byte pin)
{
  StopSwitchStates[pin] = analogRead(pin) > 200;
}

void updateStopStates()
{
  digitalReadStop(SwellOpenDiapason8_7);
  digitalReadStop(SwellStoppedDiapason8_6);
  digitalReadStop(SwellPrincipal4_5);
  digitalReadStop(SwellFlute4_4);
  digitalReadStop(SwellFifteenth2_3);
  digitalReadStop(SwellTwelfth22thirds_2);

  digitalReadStop(GreatOpenDiapason8_15);
  digitalReadStop(GreatLieblich8_14);
  digitalReadStop(GreatSalicional8_13);
  digitalReadStop(GreatGemsHorn4_12);
  digitalReadStop(GreatSalicet4_11);
  digitalReadStop(GreatNazard22thirds_10);
  digitalReadStop(GreatHorn8_9);
  digitalReadStop(GreatClarion4_8);

  analogReadStop(PedalBassFlute8_20); // Pin D20/A6 is analog input only
  digitalReadStop(PedalBourdon16_19);

  digitalReadStop(SwellToGreat_18);
  digitalReadStop(SwellToPedal_17);
  digitalReadStop(GreatToPedal_16);
}

/**
 * Test function to ignore stop switches and enable everything!
 */
void pullOutAllTheStops()
{
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

  StopSwitchStates[SwellToGreat_18] = true;
  // StopSwitchStates[SwellToPedal_17] = true;
  // StopSwitchStates[GreatToPedal_16] = true;
}

/**
 * Enables output notes for a given pitch based on the Swell Stop switchs
 */
void onForSwellStops(byte pitch)
{
  // Swell Stop To Principal Pipes
  if (StopSwitchStates[SwellOpenDiapason8_7])
  {
    setNoteState(TempPrincipalPipesState, pitch, ON);
  }
  // Swell Stop To Flute Pipes
  if (StopSwitchStates[SwellStoppedDiapason8_6])
  {
    setNoteState(TempFlutePipesState, pitch, ON);
  }
  // Swell Stop To Principal Pipes + 1 Octave
  if (StopSwitchStates[SwellPrincipal4_5])
  {
    setNoteState(TempPrincipalPipesState, pitch + OCTAVE, ON);
  }
  // Swell Stop To Flute Pipes + 1 & 2 Octave
  if (StopSwitchStates[SwellFlute4_4])
  {
    setNoteState(TempFlutePipesState, pitch + OCTAVE, ON); // Swell Stop To Flute Pipes + 1 Octave
    setNoteState(TempFlutePipesState, pitch + TWO_OCTAVE, ON);
  }
  // Swell Stop To Principal Pipes + 2 Octave
  if (StopSwitchStates[SwellFifteenth2_3])
  {
    setNoteState(TempPrincipalPipesState, pitch + TWO_OCTAVE, ON);
  }
  // Swell Stop To Principal Pipes + 2 Octave and a fifth
  if (StopSwitchStates[SwellTwelfth22thirds_2])
  {
    setNoteState(TempPrincipalPipesState, pitch + TWELFTH, ON);
  }
}

/**
 * Enables output notes for a given pitch based on the Great Stop switchs
 */
void onForGreatStops(byte pitch)
{
  // Great Stop To Principal Pipes
  if (StopSwitchStates[GreatOpenDiapason8_15])
  {
    setNoteState(TempPrincipalPipesState, pitch, ON);
  }
  // Great Stop To Flute Pipes
  if (StopSwitchStates[GreatLieblich8_14])
  {
    setNoteState(TempFlutePipesState, pitch, ON);
  }
  // Great Stop To String Pipes
  if (StopSwitchStates[GreatSalicional8_13])
  {
    setNoteState(TempStringPipesState, pitch, ON);
  }
  // Great Stop To DONT KNOW YET
  if (StopSwitchStates[GreatGemsHorn4_12])
  {
    setNoteState(TempPrincipalPipesState, pitch + OCTAVE, ON);
  }
  // Great Stop To DONT KNOW YET
  if (StopSwitchStates[GreatSalicet4_11])
  {
    setNoteState(TempStringPipesState, pitch + OCTAVE, ON);
  }
  // Great Stop To Flute Rank Plus a third
  if (StopSwitchStates[GreatNazard22thirds_10])
  {
    setNoteState(TempFlutePipesState, pitch + TWELFTH, ON);
  }
  // Great Stop To Reeds
  if (StopSwitchStates[GreatHorn8_9])
  {
    setNoteState(TempReedPipesState, pitch, ON);
  }
  // Great Stop To Reeds + Octave
  if (StopSwitchStates[GreatClarion4_8])
  {
    setNoteState(TempReedPipesState, pitch + OCTAVE, ON);
  }
}

/**
 * Enables output notes for a given pitch based on the Pedal Stop switchs
 */
void onForPedalStops(byte keyboardState[], byte pitch)
{
  // Great Stop To string Pipes
  if (StopSwitchStates[PedalBassFlute8_20])
  {
    setNoteState(TempPrincipalPipesState, pitch, ON); // Great Stop To Principal Pipes
    setNoteState(TempStringPipesState, pitch, ON);
  }
  // Great Stop To Bourdon Pipes
  if (StopSwitchStates[PedalBourdon16_19])
  {
    setNoteState(TempFlutePipesState, pitch, ON);
  }
}

void resetTempState()
{
  for (int i = 0; i < NOTES_STATE_ARRAY_SIZE; i++)
  {
    TempFlutePipesState[i] = 0UL;
    TempPrincipalPipesState[i] = 0UL;
    TempStringPipesState[i] = 0UL;
    TempReedPipesState[i] = 0UL;
  }
}

void updateOutputState(byte outputState[], byte newState[], int channel, byte pitch)
{
  if (getBitmapBit(newState, pitch))
  {
    setNoteStateOn(outputState, pitch, channel);
  }
  else
  {
    setNoteStateOff(outputState, pitch, channel);
  }
}

void updateOutputStates()
{
  // Clear out the temp state, so it can be constructed by combining the keyboard states and the active stop switches
  resetTempState();

  readMidi(); // Keep input buffer clear. Gotta go fast.

  // Build up the temporary state for each note/keyboard/stop switch combination
  for (int pitch = 0; pitch < NOTES_SIZE; pitch++)
  {
    if (getBitmapBit(SwellState, pitch))
    { // This note is pressed down on the Swell keyboard
      onForSwellStops(pitch);
    }
    readMidi(); // Keep input buffer clear. Gotta go fast.
    if (getBitmapBit(GreatState, pitch))
    { // This note is pressed down on the Great keyboard
      onForGreatStops(pitch);

      // We can send the state of any keyboard to one of the updateFor___Stop methods to apply those switch
      // settings to the active keys. This lets us reuse the same code when applying the Swell Stop panel to
      // the Great keyboard.

      // NOTE: You might need to change this around based on how the logic is supposed to work.
      // I might have the Stops panel and keyboards inverted. It should be easy enough to move around.
      if (StopSwitchStates[SwellToGreat_18])
      {
        onForSwellStops(pitch);
      }
    }
    readMidi(); // Keep input buffer clear. Gotta go fast.. Gotta go fast.

    if (getBitmapBit(PedalState, pitch))
    { // This note is pressed down on the Pedal keyboard
      onForPedalStops(PedalState, pitch);
      if (StopSwitchStates[SwellToPedal_17])
      {
        onForSwellStops(pitch);
      }
      if (StopSwitchStates[GreatToPedal_16])
      {
        onForGreatStops(pitch);
      }
      readMidi(); // Keep input buffer clear. Gotta go fast.
    }
    readMidi(); // Keep input buffer clear. Gotta go fast.
  }

  // The temp output states now contain all of the active notes. Update the current output state and send
  // MIDI Off/On messages for any output notes that have changed.
  for (byte pitch = 0; pitch < NOTES_SIZE; pitch++)
  {
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
  if (panicking)
  {
    // I'm in danger :)
    // Ignore the note
    return;
  }
  handelerExecuted = true;

  if (channel == SwellChannel)
  {
    setBitmapBit(SwellState, pitch, ON);
  }
  if (channel == GreatChannel)
  {
    setBitmapBit(GreatState, pitch, ON);
  }
  if (channel == PedalChannel)
  {
    setBitmapBit(PedalState, pitch, ON);
  }
}

/**
 * One of the keyboard notes is now off. Update the state.
 *
 * The handleMidiNoteOn and handleMidiNotOff methods should be the only
 * places that
 *
 */
void handleMidiNoteOff(byte channel, byte pitch, byte velocity)
{
  if (panicking)
  {
    // I'm in danger :)
    // Ignore the note
    return;
  }
  handelerExecuted = true;

  if (channel == SwellChannel)
  {
    setBitmapBit(SwellState, pitch, OFF);
  }
  if (channel == GreatChannel)
  {
    setBitmapBit(GreatState, pitch, OFF);
  }
  if (channel == PedalChannel)
  {
    setBitmapBit(PedalState, pitch, OFF);
  }
}

/**
 * Reads all incoming midi messages. This will  block until no MIDI.read() calls
 * result in a handler being executed. This should be called AGRESSIVELY for best
 * performance. The handlers only record what notes were pressed, the application
 * loop will make calculate what needs to be done with the state of the notes.
 */
void readMidi()
{
  do
  {
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

void sendMidi()
{
  for (int i = 0; i < MAX_MIDI_SENDS_PER_CALL; i++)
  {
    if (!popAndSendMidi())
    {
      // The buffer must be empty, nothing left to do for this send batch
      return;
    }
    readMidi();
  }
  // Serial.flush();
}

////////////////////////////////////////////////
//
//  Setup and Loop
//
////////////////

void setup()
{

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

void panicChannel(byte channel, byte pitch)
{
  MIDI.sendNoteOff(pitch, DEFAULT_OUTPUT_VELOCITY, channel);
  // Flush inbetween each off to ensure the buffer doesn't fill up
  // Serial.flush();
  // Read incoming message
  MIDI.read();
}

/**
 * Reset all of the state arrays, output buffers, and send a MIDI off message
 * to every pipe channel for every note
 */
void panic()
{

  panicking = true;
  handelerExecuted = false;

  // Send MIDI OFF messages to every pipe channel for every note
  for (int pitch = 0; pitch < NOTES_SIZE; pitch++)
  {
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
void panicAndPause()
{
  panic();
  panicking = true;

  // Lets pause for 5 seconds
  unsigned long waitTime = 5 * 1000;
  unsigned long start = millis();
  unsigned long end = start + waitTime;
  while (millis() < end)
  {
    // Read the input buffers the entire time to keep them clear
    MIDI.read();
  }
  panicking = false;
  // Time to relax, now that it's all over. Grab a beer :D
}

void loop()
{

  // TODO: Determine the correct evaluation for the panic button based on using a pulldown resistor or not
  // on the analog input pin.
  boolean panicButtonOn = false; // analogRead(PanicButton_21) > 200 // Pin D21/A7 is analog input only
  if (panicButtonOn)
  { // Pin D21/A7 is analog input only
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

  // Combine with keyboard states into temporary buffers. Update output states with
  // temporary states, comparing to send MIDI On/Off messages.
  updateOutputStates();

  readMidi();

  // Sends midi messages from the output ring buffer. This is rate limited by MAX_MIDI_SENDS_PER_LOOP
  // Notes that can't be sent will remain in the output ring buffer until the next loop, until we've
  // sent everything.
  sendMidi();
}