#include <Arduino.h>

#define u_long unsigned long

const byte NOTES_SIZE = 128;
const byte NOTES_STATE_ARRAY_SIZE = NOTES_SIZE / 32;
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
//  State aray functions
///////////////

void resetStateArrays(){
    for(int i = 0; i < NOTES_STATE_ARRAY_SIZE; i++){
      FlutePipesState[i] = (u_long) 0;
      PrincipalPipesState[i] = (u_long) 0;
      StringPipesState[i] = (u_long) 0;
      ReedPipesState[i] = (u_long) 0;

      TempFlutePipesState[i] = (u_long) 0;
      TempPrincipalPipesState[i] = (u_long) 0;
      TempStringPipesState[i] = (u_long) 0;
      TempReedPipesState[i] = (u_long) 0;
    }
}

/**
 * Sets a single bit to on or off in the provided noteState array
*/
void setStateBit(u_long noteState[], byte stateIndex, boolean value){
  byte arrayIndex = stateIndex / 32;
  byte offset = stateIndex % 32; 
  u_long chunk = noteState[arrayIndex];
  u_long editMask = 1L << offset;
  noteState[arrayIndex] = value ? (chunk | editMask) : (chunk & ~editMask);
}

boolean getStateBit(u_long noteState[], byte stateIndex){
  byte arrayIndex = stateIndex / 32;
  byte offset = stateIndex % 32; 
  u_long chunk = noteState[arrayIndex];
  chunk = chunk >> offset; //put the bit we want in the lowest bit position
  return (chunk & 1) == 1; // see if that bit is enabled
}

boolean printBuffer(u_long noteState[]){
  for(int i = 0; i < 128; i++){
    Serial.print(noteState[0], BIN);
    Serial.print(noteState[1], BIN);
    Serial.print(noteState[2], BIN);
    Serial.print(noteState[3], BIN);
  }
  Serial.println();
}

void printBuffers(){

  printBuffer(FlutePipesState);
  printBuffer(PrincipalPipesState);
  printBuffer(StringPipesState);
  printBuffer(ReedPipesState);
}

void loop()
{
  printBuffers();

  resetStateArrays();
  
  printBuffers();

  for(byte i = 0; i < 128; i++){
    setStateBit(FlutePipesState, i, true);
    printBuffer(FlutePipesState);
    Serial.println(getStateBit(FlutePipesState, i));
    Serial.println
  }

  

  

}