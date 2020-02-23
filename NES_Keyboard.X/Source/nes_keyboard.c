#include <stddef.h>
#include <stdint.h>
#include <pic16f1455.h>
#include "nes_keyboard.h"

#include <xc.h>

#define _XTAL_FREQ 48000000L

#define DATA_Get()      (PORTCbits.RC3 & 0x1)
#define CLK_Set()       do { LATCbits.LATC4 = 1; } while(0)
#define CLK_Clear()     do { LATCbits.LATC4 = 0; } while(0)
#define LATCH_Set()     do { LATCbits.LATC5 = 1; } while(0)
#define LATCH_Clear()   do { LATCbits.LATC5 = 0; } while(0)

/*
    keyboard mapping:
    B = B
    A = A
    Enter = Start
    ESC = Select
    Keypad up = Up
    Keypad left = Left
    Keypad down = Down
    Keypad right = Right
*/


uint8_t last_reading = 0;

void NES_GPIO_Initialize() 
{
    ANSELCbits.ANSC3 = 0;   // enable digital mode 
    TRISCbits.TRISC3 = 1;   //Set RC3 as input
    TRISCbits.TRISC4 = 0;   //Set RC4 as output
    TRISCbits.TRISC5 = 0;   //Set RC5 as output
    
    ANSELAbits.ANSA4 = 0;   // enable digital mode 
    TRISAbits.TRISA4 = 0;   //Set RA4 as output
}

uint8_t NES_read_pad() 
{
      /*
        NES Word Mapping
        x x x x x x x x
        | | | | | | | |_  A
        | | | | | | |___  B
        | | | | | |_____  SELECT
        | | | | |_______  START
        | | | |_________  UP
        | | |___________  DOWN
        | |_____________  LEFT
        |_______________  RIGHT
        
       Thanks to https://eskerda.com/arduino-nes-gamepad/ for the basic parts here...
       I wrote my own originally, but liked some of his better :)
       Also check out his:  https://github.com/eskerda/arduines
     */

  // Send a HIGH pulse to latch. Make 8 shift register store state
  // of all buttons
  LATCH_Set();
  __delay_us(12);
  LATCH_Clear();

  // Clock the 8 shift register to get the
  // state of the buttons
  uint8_t output = 0x00;
  int i;
  for ( i = 0; i < 8; i++)
  {
      output |= !( DATA_Get()) << i;
      CLK_Set();
      __delay_us(6);
      CLK_Clear();
      __delay_us(6);
  }
  
  return output;
}
