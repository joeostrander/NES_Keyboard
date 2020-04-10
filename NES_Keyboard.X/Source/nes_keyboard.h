#ifndef NES_KEYBOARD_H
#define NES_KEYBOARD_H

#include "usb_hid_keys.h"

#define BUTTON_A        (1<<0)
#define BUTTON_B        (1<<1)
#define BUTTON_SELECT   (1<<2)
#define BUTTON_START    (1<<3)
#define BUTTON_UP       (1<<4)
#define BUTTON_DOWN     (1<<5)
#define BUTTON_LEFT     (1<<6)
#define BUTTON_RIGHT    (1<<7)

struct KEYMAP
{
	uint8_t button;
	uint16_t key;
};

struct KEYMAP button_key_maps[] =
{
    { BUTTON_A, KEY_X },
    { BUTTON_B, KEY_Z },
    { BUTTON_SELECT, KEY_RIGHTSHIFT },
    { BUTTON_START, KEY_ENTER },
    { BUTTON_UP, KEY_UP },
    { BUTTON_DOWN, KEY_DOWN },
    { BUTTON_LEFT, KEY_LEFT },
    { BUTTON_RIGHT, KEY_RIGHT },
    
    { NULL, NULL },
};

void NES_GPIO_Initialize();
uint8_t NES_read_pad();

#endif /* NES_KEYBOARD_H */