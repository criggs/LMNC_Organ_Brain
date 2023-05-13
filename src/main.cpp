// LMNC Organ Brain - Speedy Edition (Gotta Go Fast)

/**
 * The general approach of this solution is the following:
 * 
 * Read and track all of the input keyboard note states, we want the actual physical keys being 
 * pressed here, not the transposed/combined ouput
 * 
 * Read and track the stop switch states 
 * 
 * Caclculate the output notes for each combine the physical keys being pressed with the stop 
 * switches that are enabled to determine what output notes and channels should be active
 * 
 * When setting the output notes, only send a MIDI On/Off message if theoutput is different that 
 * what it was before
 *
 * This should allow for the same notes to be pressed and released from multiple keyboards without 
 * prematurely stopping a note. It will also allow for the output/pipe notes to respond correctly to
 * changes in the stop switches while keys are being held down across the various keyboard inputs.
 *
 */

#include <MIDI.h>

/**
 * This will use a baud rate of 31250 for the Serial out by default
 * which is standard for Arduino
 */
MIDI_CREATE_DEFAULT_INSTANCE();

// Uncomment this line to force all settings for local testing,
// such as 115200 serial baud rate and force-enabling all stop switches
//#define LOCAL_TESTING_MODE 1

/**
 * ON/OFF constants help with readability
 */
#define ON true
#define OFF false

#define PANIC_WAIT_TIME_SECONDS 5

/**
 * MIDI ON and MIDI OFF messages are 2 bytes each.  Our Tx buffer is 64 bytes, so it can hold
 * a maximum of 32 ON/OFF messages before it starts overwriting itself. Limit to a lower
 * capacity to ensure we don't overload the Serial output buffer. We can probably tweak this
 * up or down based on performance
 */
#define MAX_MIDI_SENDS_PER_CALL 24
#define RING_BUFFER_MAX_SIZE 512

/**
 * Note Constants
 */
#define OCTAVE 12
#define TWO_OCTAVE 24
#define TWELFTH 31 // 2 Octaves + 7
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

// Organ Stop Switch Pins

/**
 * Swell Stop Switches
 */
#define SwellOpenDiapason8_PIN_7 7    // Swell Stop Open Diapason 8
#define SwellStoppedDiapason8_PIN_6 6 // Swell Stop Stopped Diapason 8
#define SwellPrincipal4_PIN_5 5       // Swell Stop Principal 4
#define SwellFlute4_PIN_4 4           // Swell Stop Principal 4
#define SwellFifteenth2_PIN_3 3       // Swell Stop Fifteenth 2
#define SwellTwelfth22thirds_PIN_2 2  // Swell Stop twelfth 2 2/3

/**
 * Great Stop Switches
 */
#define GreatOpenDiapason8_PIN_15 15  // Great Stop Open Diapason 8
#define GreatLieblich8_PIN_14 14      // Great Stop Lieblich 8
#define GreatSalicional8_PIN_13 13    // Great Stop Salicional 8 NEED TO REMOVE ARDUINO LED TO MAKE THIS WORK
#define GreatGemsHorn4_PIN_12 12      // Great Stop GemsHorn 4 dont know yet
#define GreatSalicet4_PIN_11 11       // Great Stop Salicet 4
#define GreatNazard22thirds_PIN_10 10 // Great Stop Nazard 2 2/3
#define GreatHorn8_PIN_9 9            // Great Stop Horn 8
#define GreatClarion4_PIN_8 8         // Great Stop Clarion 4

/**
 * Pedal Stop Switches
 */
#define PedalBassFlute8_PIN_20 20 // Pedal BassFlute 8. Need to analogRead this pin
#define PedalBourdon16_PIN_19 19  // Pedal Bourdon 16

/**
 *
 * Coupler Stops
 *
 * From: https://www.ibiblio.org/pipeorgan/Pages/Console.html
 *
 * "For example, the Great to Pedal coupler means that stops that stops in the Great division will now
 * be controlled by the pedal board. This is especially useful on organs that only have 16' and 8'
 * pedal stops. However, the stops on the Great will still sound if keys on the Great manual are played."
 *
 * Couplers can also connect manuals at a specific range. For example, a Swell to Great 4' means that all the
 * stops currently playing in the Swell will be copied to the Great manual an octave higher than their regular
 * pitch on the Swell. So, an 8' flute in the Swell will sound at 8' pitch on the swell but at 4' pitch on the
 * great. Common ranges of these types of couplers are 16', and 4'. While this type of coupler is helpful, it
 * is not a necessary part of the organ so you may find organs which do not have them.
 *
 */
#define SwellToGreat_PIN_18 18 // Send the Swell Stops to the Great Keyboard (Great Plays Swell and Great Stops)
#define SwellToPedal_PIN_17 17 // Send the Swell Stops to the Pedal Keyboard (Pedal Plays Swell and Pedal Stops)
#define GreatToPedal_PIN_16 16 // Send the Great Stops to the Pedal Keyboard (Pedal Plays Great and Pedal Stops)
                               // Note: If Both the SwellToPedal and GreatToPedal are enabled, The pedals play All Stops

#define PanicButton_PIN_21 21 // Pin D21/A7 is analog input only

///////////////////////////////////////////////////////////////////////////////
//
// Program State
//

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
#define NOTES_BITMAP_ARRAY_SIZE 16 // NOTES_SIZE / 8
// Each state needs to hold

// State for the input keyboards
byte SwellState[NOTES_BITMAP_ARRAY_SIZE] = {};
byte GreatState[NOTES_BITMAP_ARRAY_SIZE] = {};
byte PedalState[NOTES_BITMAP_ARRAY_SIZE] = {};

// State for the output channels
byte PrincipalPipesState[NOTES_BITMAP_ARRAY_SIZE] = {};
byte StringPipesState[NOTES_BITMAP_ARRAY_SIZE] = {};
byte FlutePipesState[NOTES_BITMAP_ARRAY_SIZE] = {};
byte ReedPipesState[NOTES_BITMAP_ARRAY_SIZE] = {};

// Temporary state to combinie keboard and stop states
byte NewPrincipalPipesState[NOTES_BITMAP_ARRAY_SIZE] = {};
byte NewStringPipesState[NOTES_BITMAP_ARRAY_SIZE] = {};
byte NewFlutePipesState[NOTES_BITMAP_ARRAY_SIZE] = {};
byte NewReedPipesState[NOTES_BITMAP_ARRAY_SIZE] = {};

///////////////////////////////////////////////////////////////////////////////
//
//  Forward Definitions of Functions
//
//  This is not needed for Arduino, but it will make this valid C++ for
//  use in a PaltformIO project. Arduino IDE will auto-generate any forward
//  definitions when compiling to make it a bit easier for non-programmers
//

// Setup
void setupMidi();
void setupPins();

// Panic
void checkForPanic();
void panicAndPause();
void panic();

// MIDI
void handleMidiNoteOn(byte channel, byte pitch, byte velocity);
void handleMidiNoteOff(byte channel, byte pitch, byte velocity);
void handleMidiNote(byte channel, byte pitch, byte velocity, boolean value);
void readMidi();
void sendMidi();

// State Management
void resetStateArrays();
boolean setNoteState(byte noteBitmap[], byte pitch, boolean val);
void setNoteStateOn(byte noteBitmap[], byte pitch, int channel);
void setNoteStateOff(byte noteBitmap[], byte pitch, int channel);
void readStopSwitchStates();
#ifdef LOCAL_TESTING_MODE
// Testint functions
void pullOutAllTheStops();
#endif
void resetNewState();

// Calculate Output
void calculateOutputNotes();
void updateOutputState(byte outputState[], byte newState[], int channel, byte pitch);
void enableNoteForSwellSwitches(byte pitch);
void enableNoteForGreatSwitches(byte pitch);
void enableNoteForPedalSwitches(byte pitch);

// Bitmap function
void setBitmapBit(byte bitmap[], byte index, byte val);
bool getBitmapBit(byte bitmap[], byte index);
void printNoteBitmap(byte bitmap[]);

// Output Ring Buffer
void resetOutputBuffer();
boolean pushToOutputBuffer(byte channel, byte pitch, boolean val);
boolean popAndSendMidi();

// IO Helpers
void digitalReadSwitch(byte pin);
void analogReadSwitch(byte pin);

///////////////////////////////////////////////////////////////////////////////
//
// Setup and Loop
//

/**
 * Entry point into the application
 */
void setup()
{
  setupMidi();
  setupPins();
  // Start with a panic to send out MIDI Off to all pipe notes
  panic();
}

/**
 * Configure the MIDI library settings
 */
void setupMidi()
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
}

/**
 * Configure all of the IO pins
 */
void setupPins()
{
  pinMode(SwellOpenDiapason8_PIN_7, INPUT);
  pinMode(SwellStoppedDiapason8_PIN_6, INPUT);
  pinMode(SwellPrincipal4_PIN_5, INPUT);
  pinMode(SwellFlute4_PIN_4, INPUT);
  pinMode(SwellFifteenth2_PIN_3, INPUT);
  pinMode(SwellTwelfth22thirds_PIN_2, INPUT);

  pinMode(GreatOpenDiapason8_PIN_15, INPUT);
  pinMode(GreatLieblich8_PIN_14, INPUT);
  pinMode(GreatSalicional8_PIN_13, INPUT);
  pinMode(GreatGemsHorn4_PIN_12, INPUT);
  pinMode(GreatSalicet4_PIN_11, INPUT);
  pinMode(GreatNazard22thirds_PIN_10, INPUT);
  pinMode(GreatHorn8_PIN_9, INPUT);
  pinMode(GreatClarion4_PIN_8, INPUT);

  pinMode(PedalBassFlute8_PIN_20, INPUT);
  pinMode(PedalBourdon16_PIN_19, INPUT);

  pinMode(SwellToGreat_PIN_18, INPUT);
  pinMode(SwellToPedal_PIN_17, INPUT);
  pinMode(GreatToPedal_PIN_16, INPUT);
  pinMode(PanicButton_PIN_21, INPUT);
}

/**
 * Application Loop. Keep on keeping on.
 */
void loop()
{
  checkForPanic();        // Panic if panic button is pressed
  readMidi();             // Keep input buffer clear. Gotta go fast.
  readStopSwitchStates(); // Update the Stop Switch states
#ifdef LOCAL_TESTING_MODE
  pullOutAllTheStops(); // ALL THE STOPS!!!
#endif
  readMidi(); // Keep input buffer clear. Gotta go fast.
  calculateOutputNotes();
  readMidi(); // Keep input buffer clear. Gotta go fast.
  sendMidi(); // Send a batch of midi messages from the output ring buffer
}

///////////////////////////////////////////////////////////////////////////////
//
// Panic
//

/**
 * See if the panic button is being pressed. If so, panic and pause.
 */
void checkForPanic()
{
  // TODO: Determine the correct evaluation for the panic button based on using a pulldown resistor or not
  // on the analog input pin.
  boolean panicButtonOn = false; // analogRead(PanicButton_PIN_21) > 200 // Pin D21/A7 is analog input only
  if (panicButtonOn)
  { // Pin D21/A7 is analog input only
    // Have a panic attack!!!
    panicAndPause();
    // Relax, all good now :)
  }
}

/**
 *
 * Panics all of the pipe channels and waits for PANIC_WAIT_TIME_SECONDS before
 * accepting accepting input again.
 *
 * Druing the panic state, while waiting, the input buffers will be consumed
 * but no output will be written.
 *
 * This will be automatically called if the Output Ring Buffer is overrun
 *
 */
void panicAndPause()
{
  panic();
  panicking = true;

  unsigned long waitTime = PANIC_WAIT_TIME_SECONDS * 1000;
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

    MIDI.sendNoteOff(pitch, DEFAULT_OUTPUT_VELOCITY, StringPipesChannel);
    MIDI.sendNoteOff(pitch, DEFAULT_OUTPUT_VELOCITY, PrincipalPipesChannel);
    MIDI.sendNoteOff(pitch, DEFAULT_OUTPUT_VELOCITY, FlutePipesChannel);
    MIDI.sendNoteOff(pitch, DEFAULT_OUTPUT_VELOCITY, ReedPipesChannel);
    Serial.flush();

    MIDI.read(); // Keep consuming the input buffer. It will be ignored in the panic state
  }

  resetStateArrays();
  resetOutputBuffer();

  panicking = false;
}

///////////////////////////////////////////////////////////////////////////////
//
// MIDI
//

/**
 * Handler called by the MIDI Library when a midi note is now ON (pressed)
 *
 * WARNING: This needs to be as fast as possible. If too much work is done
 * inside this function, we are at risk of input buffer overruns and dropping
 * midi notes
 *
 */
void handleMidiNoteOn(byte channel, byte pitch, byte velocity)
{
  handleMidiNote(channel, pitch, velocity, ON);
}

/**
 * Handler called by the MIDI Library when a midi note is now OFF (released)
 *
 * WARNING: This needs to be as fast as possible. If too much work is done
 * inside this function, we are at risk of input buffer overruns and dropping
 * midi notes
 *
 */
void handleMidiNoteOff(byte channel, byte pitch, byte velocity)
{
  handleMidiNote(channel, pitch, velocity, OFF);
}

/**
 * Sets the state of an incoming midi note.
 */
void handleMidiNote(byte channel, byte pitch, byte velocity, boolean value)
{
  if (panicking)
  {
    // I'm in danger :)
    // Ignore the note
    return;
  }
  handelerExecuted = true;
  switch (channel)
  {
  case SwellChannel:
    setBitmapBit(SwellState, pitch, value);
    break;
  case GreatChannel:
    setBitmapBit(GreatState, pitch, value);
    break;
  case PedalChannel:
    setBitmapBit(PedalState, pitch, value);
    break;
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

/**
 * Send midi messages from the Output Ring Buffer. This will send up to
 * MAX_MIDI_SENDS_PER_CALL message. readMidi() is called in between each sent note
 * to ensure we're keeping the input buffer clear.
 */
void sendMidi()
{
  for (int i = 0; i < MAX_MIDI_SENDS_PER_CALL; i++)
  {
    if (!popAndSendMidi())
    {
      // The buffer must be empty, nothing left to do for this send batch
      return;
    }
    readMidi(); // Keep input buffer clear. Gotta go fast
  }
  // Serial.flush();
}

///////////////////////////////////////////////////////////////////////////////
//
// State Management
//

void resetStateArrays()
{
  for (int i = 0; i < NOTES_BITMAP_ARRAY_SIZE; i++)
  {
    FlutePipesState[i] = 0;
    PrincipalPipesState[i] = 0;
    StringPipesState[i] = 0;
    ReedPipesState[i] = 0;

    NewFlutePipesState[i] = 0;
    NewPrincipalPipesState[i] = 0;
    NewStringPipesState[i] = 0;
    NewReedPipesState[i] = 0;
  }
}

/**
 * Makes sure the note is on in the given bitmap
 *
 * If the note state changed, then send the midi channel note on message to the channel.
 *
 */
void setNoteStateOn(byte noteBitmap[], byte pitch, int channel)
{
  // Set the state. If it changed, queue up a midi output message
  if (setNoteState(noteBitmap, pitch, ON))
  {
    if (!pushToOutputBuffer(channel, pitch, ON))
    {
      panicAndPause();
    }
  }
}

/**
 * Makes sure the note is off in the given bitmap
 *
 * If the note state changed, then send the midi channel note off message to the channel.
 *
 */
void setNoteStateOff(byte noteBitmap[], byte pitch, int channel)
{
  // Set the state. If it changed, queue up a midi output message
  if (setNoteState(noteBitmap, pitch, OFF))
  {
    if (!pushToOutputBuffer(channel, pitch, OFF))
    {
      panicAndPause();
    }
  }
}

/**
 * Sets the note state to the specified value. 0 for off, 1 for on.
 * @param noteBitmap The note bitmap to change
 * @param pitch The note index/pitch to change in the mask
 * @param val false for off, true for on
 *
 * @return true if it changed, false if it did not change
 */
boolean setNoteState(byte noteBitmap[], byte pitch, boolean val)
{
  if (pitch >= 0 && pitch < NOTES_SIZE)
  {
    boolean current = getBitmapBit(noteBitmap, pitch);
    setBitmapBit(noteBitmap, pitch, val);
    return current != val;
  }
  else
  {
    return false;
  }
}

/**
 * Read the specific analog pin into a StopSwitchState
 */
void analogReadSwitch(byte pin)
{
  StopSwitchStates[pin] = analogRead(pin) > 200;
}

/**
 * Read and update the state of all read switches
 */
void readStopSwitchStates()
{
  digitalReadSwitch(SwellOpenDiapason8_PIN_7);
  digitalReadSwitch(SwellStoppedDiapason8_PIN_6);
  digitalReadSwitch(SwellPrincipal4_PIN_5);
  digitalReadSwitch(SwellFlute4_PIN_4);
  digitalReadSwitch(SwellFifteenth2_PIN_3);
  digitalReadSwitch(SwellTwelfth22thirds_PIN_2);

  digitalReadSwitch(GreatOpenDiapason8_PIN_15);
  digitalReadSwitch(GreatLieblich8_PIN_14);
  digitalReadSwitch(GreatSalicional8_PIN_13);
  digitalReadSwitch(GreatGemsHorn4_PIN_12);
  digitalReadSwitch(GreatSalicet4_PIN_11);
  digitalReadSwitch(GreatNazard22thirds_PIN_10);
  digitalReadSwitch(GreatHorn8_PIN_9);
  digitalReadSwitch(GreatClarion4_PIN_8);

  // Pin D20/A6 is analog input only
  analogReadSwitch(PedalBassFlute8_PIN_20); // Principal and String
  digitalReadSwitch(PedalBourdon16_PIN_19); // Flute

  // Coupler Stops
  digitalReadSwitch(SwellToGreat_PIN_18);
  digitalReadSwitch(SwellToPedal_PIN_17);
  digitalReadSwitch(GreatToPedal_PIN_16);
}

#ifdef LOCAL_TESTING_MODE
/**
 * Test function to ignore stop switches and enable everything!
 */
void pullOutAllTheStops()
{
  StopSwitchStates[SwellOpenDiapason8_PIN_7] = true;
  StopSwitchStates[SwellStoppedDiapason8_PIN_6] = true;
  StopSwitchStates[SwellPrincipal4_PIN_5] = true;
  StopSwitchStates[SwellFlute4_PIN_4] = true;
  StopSwitchStates[SwellFifteenth2_PIN_3] = true;
  StopSwitchStates[SwellTwelfth22thirds_PIN_2] = true;

  StopSwitchStates[GreatOpenDiapason8_PIN_15] = true;
  StopSwitchStates[GreatLieblich8_PIN_14] = true;
  StopSwitchStates[GreatSalicional8_PIN_13] = true;
  StopSwitchStates[GreatGemsHorn4_PIN_12] = true;
  StopSwitchStates[GreatSalicet4_PIN_11] = true;
  StopSwitchStates[GreatNazard22thirds_PIN_10] = true;
  StopSwitchStates[GreatHorn8_PIN_9] = true;
  StopSwitchStates[GreatClarion4_PIN_8] = true;

  StopSwitchStates[PedalBassFlute8_PIN_20] = true; // Principal and String
  StopSwitchStates[PedalBourdon16_PIN_19] = true;  // Flute

  StopSwitchStates[SwellToGreat_PIN_18] = true;
  StopSwitchStates[SwellToPedal_PIN_17] = true;
  StopSwitchStates[GreatToPedal_PIN_16] = true;
}
#endif

/**
 * Resets the state bitmaps for all new states. Ready for calculating
 */
void resetNewState()
{
  for (int i = 0; i < NOTES_BITMAP_ARRAY_SIZE; i++)
  {
    NewFlutePipesState[i] = 0;
    NewPrincipalPipesState[i] = 0;
    NewStringPipesState[i] = 0;
    NewReedPipesState[i] = 0;
  }
}

///////////////////////////////////////////////////////////////////////////////
//
// Calculate Output
//

/**
 * Combine with keyboard states into the new output states. Update output states with
 * temporary states, comparing to send MIDI On/Off messages.
 */
void calculateOutputNotes()
{
  // Clear out the temp state, so it can be constructed by combining the keyboard states and the active stop switches
  resetNewState();

  readMidi(); // Keep input buffer clear. Gotta go fast.

  // Build up the temporary state for each note/keyboard/stop switch combination
  for (int pitch = 0; pitch < NOTES_SIZE; pitch++)
  {
    if (getBitmapBit(SwellState, pitch))
    { // This note is pressed down on the Swell keyboard
      enableNoteForSwellSwitches(pitch);
    }
    readMidi(); // Keep input buffer clear. Gotta go fast.
    if (getBitmapBit(GreatState, pitch))
    { // This note is pressed down on the Great keyboard
      enableNoteForGreatSwitches(pitch);
      if (StopSwitchStates[SwellToGreat_PIN_18]) // Coupler to combine Swell with Pedal
      {
        // TODO: Do we need to transpose up or down any octaves here?
        enableNoteForSwellSwitches(pitch);
      }
    }
    readMidi(); // Keep input buffer clear. Gotta go fast.. Gotta go fast.

    if (getBitmapBit(PedalState, pitch))
    { // This note is pressed down on the Pedal keyboard
      enableNoteForPedalSwitches(pitch);
      if (StopSwitchStates[SwellToPedal_PIN_17]) // Coupler to combine Swell with Pedal
      {
        // TODO: Do we need to transpose up or down any octaves here?
        enableNoteForSwellSwitches(pitch);
      }
      if (StopSwitchStates[GreatToPedal_PIN_16]) // Coupler to combine Great with Pedal
      {
        // TODO: Do we need to transpose up or down any octaves here?
        enableNoteForGreatSwitches(pitch);
      }
      readMidi(); // Keep input buffer clear. Gotta go fast.
    }
    readMidi(); // Keep input buffer clear. Gotta go fast.
  }

  // The temp output states now contain all of the active notes. Update the current output state and send
  // MIDI Off/On messages for any output notes that have changed.
  for (byte pitch = 0; pitch < NOTES_SIZE; pitch++)
  {
    updateOutputState(FlutePipesState, NewFlutePipesState, FlutePipesChannel, pitch);

    readMidi(); // Keep input buffer clear. Gotta go fast.
    updateOutputState(PrincipalPipesState, NewPrincipalPipesState, PrincipalPipesChannel, pitch);
    readMidi(); // Keep input buffer clear. Gotta go fast.
    updateOutputState(StringPipesState, NewStringPipesState, StringPipesChannel, pitch);
    readMidi(); // Keep input buffer clear. Gotta go fast.
    updateOutputState(ReedPipesState, NewReedPipesState, ReedPipesChannel, pitch);

    // Prevent buffer issues by proactively reading and writing pending messages
    // It's safe to do here because reading new midi notes will will not effect the output states
    readMidi(); // Keep input buffer clear. Gotta go fast.
    sendMidi();
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

/**
 * Updates output notes based on the state of the swell stop switched for the
 * given pitch being on
 *
 * @param pitch The pitch to enable notes for based on stop switch state for Swell
 */
void enableNoteForSwellSwitches(byte pitch)
{
  if (StopSwitchStates[SwellOpenDiapason8_PIN_7]) // Swell Stop To Principal Pipes
  {
    setNoteState(NewPrincipalPipesState, pitch, ON);
  }
  if (StopSwitchStates[SwellStoppedDiapason8_PIN_6]) // Swell Stop To Flute Pipes
  {
    setNoteState(NewFlutePipesState, pitch, ON);
  }
  if (StopSwitchStates[SwellPrincipal4_PIN_5]) // Swell Stop To Principal Pipes + 1 Octave
  {
    setNoteState(NewPrincipalPipesState, pitch + OCTAVE, ON);
  }
  if (StopSwitchStates[SwellFlute4_PIN_4]) // Swell Stop To Flute Pipes + 1 & 2 Octave
  {
    setNoteState(NewFlutePipesState, pitch + OCTAVE, ON);
    setNoteState(NewFlutePipesState, pitch + TWO_OCTAVE, ON);
  }
  if (StopSwitchStates[SwellFifteenth2_PIN_3]) // Swell Stop To Principal Pipes + 2 Octave
  {
    setNoteState(NewPrincipalPipesState, pitch + TWO_OCTAVE, ON);
  }
  if (StopSwitchStates[SwellTwelfth22thirds_PIN_2]) // Swell Stop To Principal Pipes + 2 Octave and a fifth
  {
    setNoteState(NewPrincipalPipesState, pitch + TWELFTH, ON);
  }
}

/**
 * Updates output notes based on the state of the swell stop switched for the
 * given pitch being on
 *
 * @param pitch The pitch to enable notes for based on stop switch state for Great Switches
 */
void enableNoteForGreatSwitches(byte pitch)
{
  if (StopSwitchStates[GreatOpenDiapason8_PIN_15]) // Great Stop To Principal Pipes
  {
    setNoteState(NewPrincipalPipesState, pitch, ON);
  }
  if (StopSwitchStates[GreatLieblich8_PIN_14]) // Great Stop To Flute Pipes
  {
    setNoteState(NewFlutePipesState, pitch, ON);
  }
  if (StopSwitchStates[GreatSalicional8_PIN_13]) // Great Stop To String Pipes
  {
    setNoteState(NewStringPipesState, pitch, ON);
  }
  if (StopSwitchStates[GreatGemsHorn4_PIN_12]) // Great Stop To DONT KNOW YET TODO: Update
  {
    setNoteState(NewPrincipalPipesState, pitch + OCTAVE, ON);
  }
  if (StopSwitchStates[GreatSalicet4_PIN_11]) // Great Stop To DONT KNOW YET TODO: Update
  {
    setNoteState(NewStringPipesState, pitch + OCTAVE, ON);
  }
  if (StopSwitchStates[GreatNazard22thirds_PIN_10]) // Great Stop To Flute Rank Plus a third
  {
    setNoteState(NewFlutePipesState, pitch + TWELFTH, ON);
  }
  if (StopSwitchStates[GreatHorn8_PIN_9]) // Great Stop To Reeds
  {
    setNoteState(NewReedPipesState, pitch, ON);
  }
  if (StopSwitchStates[GreatClarion4_PIN_8]) // Great Stop To Reeds + Octave
  {
    setNoteState(NewReedPipesState, pitch + OCTAVE, ON);
  }
}

/**
 * Updates output notes based on the state of the swell stop switched for the
 * given pitch being on
 *
 * @param pitch The pitch to enable notes for based on stop switch state for Pedal Switches
 */
void enableNoteForPedalSwitches(byte pitch)
{
  if (StopSwitchStates[PedalBassFlute8_PIN_20]) // Great Stop to Principal and String
  {
    setNoteState(NewPrincipalPipesState, pitch, ON);
    setNoteState(NewStringPipesState, pitch, ON);
  }
  if (StopSwitchStates[PedalBourdon16_PIN_19]) // Great Stop To Bourdon Pipes (Flute)
  {
    setNoteState(NewFlutePipesState, pitch, ON);
  }
}

///////////////////////////////////////////////////////////////////////////////
//
// Bitmap Functions
//

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

/**
 * Debug function for printing a state
 */
void printNoteBitmap(byte bitmap[])
{
  for (int i = 0; i < NOTES_BITMAP_ARRAY_SIZE; i++)
  {
    Serial.print(bitmap[i], BIN);
  }
  Serial.println();
}

///////////////////////////////////////////////////////////////////////////////
//
// Output Ring Buffer
//

// We're using words to store the note in the lower byte and the channel and on/off in the upper byte
word outputRingBuffer[RING_BUFFER_MAX_SIZE] = {};

int outputRingHead = 0; // Start of buffer (where to read from)
int outputRingTail = 0; // End of buffer (where to write to)
int outputRingSize = 0; // Number of pending output bytes. If this ever gets bigger than the max size, panic!

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
    return false; // The buffer is full, cannot add or we will overflow
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

///////////////////////////////////////////////////////////////////////////////
//
// IO Helpers
//

/**
 * Read the specified digital pin into a StopSwitchState
 */
void digitalReadSwitch(byte pin)
{
  StopSwitchStates[pin] = digitalRead(pin) == HIGH;
}
