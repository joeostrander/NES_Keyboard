/*
 * File:   Usb.c
 * Author: Szymon Roslowski
 *
 * Created on 13 October 2014, 17:46
 *
 * Firmware framework for USB I/O on PIC 16F1455 (and siblings)
 *
 * Based On
 *
 * Firmware framework for USB I/O on PIC 18F2455 (and siblings)
 * Copyright (C) 2005 Alexander Enzmann
 * adapted to MCC18 by Alberto Maccioni on 1/8/09
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111 USA
 * or see <http://www.gnu.org/licenses/>
 */

#include <htc.h>
#include "Usb.h"

/***********************/
/* Local Definitions   */
/***********************/

// Commands
#define GET_STATUS                  0x00
#define CLEAR_FEATURE               0x01
#define SET_FEATURE                 0x03
#define SET_ADDRESS                 0x05
#define GET_DESCRIPTOR              0x06
#define SET_DESCRIPTOR              0x07
#define GET_CONFIGURATION           0x08
#define SET_CONFIGURATION           0x09
#define GET_INTERFACE               0x0A
#define SET_INTERFACE               0x0B
#define SYNCH_FRAME                 0x0C

// Descriptor Types
#define DEVICE_DESCRIPTOR           0x01
#define CONFIGURATION_DESCRIPTOR    0x02
#define STRING_DESCRIPTOR           0x03
#define INTERFACE_DESCRIPTOR        0x04
#define ENDPOINT_DESCRIPTOR         0x05

// Class Descriptor Types
#define HID_DESCRIPTOR              0x21
#define REPORT_DESCRIPTOR           0x22
#define PHYSICAL_DESCRIPTOR         0x23

// HID Class specific requests
#define GET_REPORT                  0x01
#define GET_IDLE                    0x02
#define GET_PROTOCOL                0x03
#define SET_REPORT                  0x09
#define SET_IDLE                    0x0A
#define SET_PROTOCOL                0x0B

// Standard Feature Selectors
#define DEVICE_REMOTE_WAKEUP        0x01
#define ENDPOINT_HALT               0x00

// Device states (Chap 9.1.1)
#define DETACHED                    0x00
#define ATTACHED                    0x01
#define POWERED                     0x02
#define DEFAULT                     0x03
#define ADDRESS                     0x04
#define CONFIGURED                  0x05

/* Interrupt */
#define USB_SOF                     0x40
#define USB_STALL                   0x20
#define USB_IDLE                    0x10
#define USB_TRN                     0x08
#define USB_RESUM                   0x04
#define USB_UERR                    0x02
#define USB_URST                    0x01

// Buffer Descriptor bit masks (from PIC datasheet)
#define UOWN                        0x80 // USB Own Bit
#define DTS                         0x40 // Data Toggle Synchronization Bit
#define KEN                         0x20 // BD Keep Enable Bit
#define INCDIS                      0x10 // Address Increment Disable Bit
#define DTSEN                       0x08 // Data Toggle Synchronization Enable Bit
#define BSTALL                      0x04 // Buffer Stall Enable Bit
#define BC9                         0x02 // Byte count bit 9
#define BC8                         0x01 // Byte count bit 8

// Control Transfer Stages - see USB spec chapter 5
#define SETUP_STAGE                 0x00 // Start of a control transfer (followed by 0 or more data stages)
#define DATA_OUT_STAGE              0x01 // Data from host to device
#define DATA_IN_STAGE               0x02 // Data from device to host
#define STATUS_STAGE                0x03 // Unused - if data I/O went ok, then back to Setup

// Hardware
#define USB_RESET_FLAG              UIRbits.URSTIF
#define USB_RESUME_FLAG             UIRbits.ACTVIF
#define USB_IDLE_FLAG               UIRbits.IDLEIF
#define USB_STALL_FLAG              UIRbits.STALLIF
#define USB_SOF_FLAG                UIRbits.SOFIF
#define USB_ERROR_FLAG              UIRbits.UERRIF
#define USB_TRANSACTION_FLAG        UIRbits.TRNIF

/***********************/
/* Structures          */
/***********************/

typedef struct _BDT
{
    uint8_t Stat;
    uint8_t Cnt;
    uint16_t ADDR;
} BDT; //Buffer Descriptor Table

typedef struct _Interface
{
    BDT Output;
    BDT Input;
} Interface;

// Every device request starts with an 8 byte setup packet (USB 2.0, chap 9.3)
// with a standard layout.  The meaning of wValue and wIndex will
// vary depending on the request type and specific request.
typedef struct _setupPacketStruct
{
    uint8_t bmRequestType; // D7: Direction, D6..5: Type, D4..0: Recipient
    uint8_t bRequest;      // Specific request
    uint8_t wValue0;       // LSB of wValue
    uint8_t wValue1;       // MSB of wValue
    uint8_t wIndex0;       // LSB of wIndex
    uint8_t wIndex1;       // MSB of wIndex
    uint16_t wLength;       // Number of bytes to transfer if there's a data stage
    uint8_t extra[1];      // Fill out to same size as Endpoint 0 max buffer (E0SZ-7)
} setupPacketStruct;

/***********************/
/* Local Variables     */
/***********************/

uint8_t RemoteWakeup;
uint8_t DeviceAddress;
uint8_t SelfPowered;
uint8_t CtrlTransferStage; // Holds the current stage in a control transfer
uint8_t CurrentConfiguration;
uint8_t HIDPostProcess;    // Set to 1 if HID needs to process after the data stage
uint8_t RequestHandled;    // Set to 1 if request was understood and processed.
// HID Class variables
uint8_t HidIdleRate;
uint8_t HidProtocol; // [0] Boot Protocol [1] Report Protocol
uint8_t HidRxLen;    // # of bytes put into buffer

const uint8_t *ROMoutPtr;  // Data to send to the host
uint8_t *outPtr;           // Data to send to the host
uint8_t *inPtr;            // Data from the host
uint8_t transferType;	// 0=ram 1=rom
uint16_t wCount;            // Number of bytes of data

volatile setupPacketStruct SetupPacket;
volatile uint8_t ControlTransferBuffer[E0SZ];
//TESTING!!!volatile uint8_t EndpointFlags[InterfaceCount] @ 0xE99; // Endpoint Flag Registers
volatile uint8_t EndpointFlags[InterfaceCount] __at(0xE99); // Endpoint Flag Registers

// !!! It is ABSOLUTELY VITAL for the start of BDTs to point to 0x2000.
// !!! Won't work without it.
//TESTING!!!volatile Interface Interfaces[InterfaceCount + 1] @ 0x2000;
volatile Interface Interfaces[InterfaceCount + 1] __at(0x2000);
// ... The hours I've waisted before I found out... :(


/***********************/
/* Implementation      */
/***********************/
uint8_t IsUsbDataAvaialble(uint8_t InterfaceNo)
{
    if(InterfaceNo >= InterfaceCount) return 0;
    if(!(Interfaces[InterfaceNo + 1].Output.Stat & UOWN))
    {
        return Interfaces[InterfaceNo + 1].Output.Cnt;
    }
    return 0;
}

void ReArmInterface(uint8_t InterfaceNo)
{
    //If there is data received in the reeived buffer
    //Indicate that we have processed it and get the endpoint
    //ready to receive next packet.

    if(!(Interfaces[InterfaceNo + 1].Output.Stat & UOWN))
    {
        //Interfaces[InterfaceNo + 1].Output.Cnt =  *BufferSizes[0];
        Interfaces[InterfaceNo + 1].Output.Cnt =  Buffers[(InterfaceNo * 2) + 1].Size;
        //Interfaces[InterfaceNo + 1].Output.Cnt =  sizeof(HIDRxBuffer);

  //      HIDRxBuffer
        if(Interfaces[InterfaceNo + 1].Output.Stat & DTS)
            Interfaces[InterfaceNo + 1].Output.Stat = UOWN | DTSEN;
        else
            Interfaces[InterfaceNo + 1].Output.Stat = UOWN | DTS | DTSEN;
    }
}

void HIDSend(uint8_t InterfaceNo)
{
    // If the CPU still owns the SIE, then don't try to send anything.
    if (Interfaces[InterfaceNo + 1].Input.Stat & UOWN) return;
    // Toggle the data bit and give control to the SIE

    //Interfaces[InterfaceNo + 1].Input.Cnt = sizeof(HIDTxBuffer);
    Interfaces[InterfaceNo + 1].Input.Cnt =  Buffers[InterfaceNo * 2].Size;

    if(Interfaces[InterfaceNo + 1].Input.Stat & DTS)
        Interfaces[InterfaceNo + 1].Input.Stat = UOWN | DTSEN;
    else
        Interfaces[InterfaceNo + 1].Input.Stat = UOWN | DTS | DTSEN;
}

// After configuration is complete, this routine is called to initialize
// the endpoints (e.g., assign buffer addresses).
void HIDInitEndpoints(void)
{
    uint8_t i = 0;

    HidRxLen =0;

    for (i = 0 ; i < InterfaceCount; i++)
    {
        // Turn on both in and out for this endpoint
        EndpointFlags[i] = 0x1E;
        //Interfaces[i+1].Output.Cnt = sizeof(HIDRxBuffer);
        Interfaces[i + 1].Output.Cnt = Buffers[(i * 2) + 1].Size;

        //Interfaces[i+1].Output.ADDR = PTR16(&HIDRxBuffer);
        Interfaces[i + 1].Output.ADDR = PTR16(Buffers[(i * 2) + 1].Buffer);

        Interfaces[i + 1].Output.Stat = UOWN | DTSEN;

        //Interfaces[i + 1].Input.ADDR = PTR16(&HIDTxBuffer);
        Interfaces[i + 1].Input.ADDR = PTR16(Buffers[(i * 2)].Buffer);
        Interfaces[i + 1].Input.Stat = DTS;
    }
}

// Process HID specific requests
void ProcessHIDRequest(void)
{
    uint8_t bRequest;

     // Has to be to the HID interface
    if((SetupPacket.bmRequestType & 0x1F) != 0x01 || (SetupPacket.wIndex0 != 0x00)) return;

    bRequest = SetupPacket.bRequest;

    if (bRequest == GET_DESCRIPTOR)
    {
        // Request for a descriptor.
        uint8_t descriptorType  = SetupPacket.wValue1;
        if (descriptorType == HID_DESCRIPTOR)
        {
            RequestHandled = 1;
            ROMoutPtr = (const uint8_t*)&ConfigurationDescriptor.HIDDescriptor;
            wCount = sizeof(ConfigurationDescriptor.HIDDescriptor);
            transferType=1;
        }
        else if (descriptorType == REPORT_DESCRIPTOR)
        {
            RequestHandled = 1;
            ROMoutPtr = (const uint8_t*)HIDReport;
            wCount = sizeof(HIDReport);
            transferType=1;
        }
        else if (descriptorType == PHYSICAL_DESCRIPTOR)
        {   // Do Nothing
        }
        else
        {   // Unsupported Descriptor
        }
    }

    if ((SetupPacket.bmRequestType & 0x60) != 0x20)
    {
        return;
    }

    // HID-specific requests.
    if (bRequest == GET_REPORT)
    {   // Do Nothing
    }

    else if (bRequest == SET_REPORT)
    {
        HIDPostProcess = 1;
        RequestHandled = 1;
    }

    else if (bRequest == GET_IDLE)
    {
        RequestHandled = 1;
        outPtr = &HidIdleRate;
        wCount = 1;
        transferType=0;
    }

    else if (bRequest == SET_IDLE)
    {
        RequestHandled = 1;
        HidIdleRate = SetupPacket.wValue1;
    }

    else if (bRequest == GET_PROTOCOL)
    {
        RequestHandled = 1;
        outPtr = &HidProtocol;
        wCount = 1;
	transferType=0;
    }

    else if (bRequest == SET_PROTOCOL)
    {
        RequestHandled = 1;
        HidProtocol = SetupPacket.wValue0;
    }

    else
    {   // Unknown Request
    }
}

// Process GET_DESCRIPTOR
static void GetDescriptor(void)
{
    if(SetupPacket.bmRequestType == 0x80)
    {
        uint8_t descriptorType  = SetupPacket.wValue1;
        uint8_t descriptorIndex = SetupPacket.wValue0;

        if (descriptorType == DEVICE_DESCRIPTOR)
        {
                RequestHandled = 1;
                ROMoutPtr = (const uint8_t *) & DeviceDescriptor;
                wCount = sizeof(DeviceDescriptor);
                transferType=1;
        }
        else if (descriptorType == CONFIGURATION_DESCRIPTOR)
        {
                RequestHandled = 1;
                ROMoutPtr = (const uint8_t*)&ConfigurationDescriptor;
                wCount = sizeof(ConfigurationDescriptor);
		transferType=1;
        }
        else if (descriptorType == STRING_DESCRIPTOR)
        {
                RequestHandled = 1;
                if(descriptorIndex >= StringDescriptorCount)
                    ROMoutPtr = (const uint8_t*)&StringDescriptor0;
                else
                    ROMoutPtr = *(StringDescriptorPointers + descriptorIndex);

                wCount = *ROMoutPtr;
		transferType=1;
        }
        else
        {   // Unknown Descriptor
        }
    }
}

// Process GET_STATUS
static void GetStatus(void)
{
    // Mask off the Recipient bits
    uint8_t recipient = SetupPacket.bmRequestType & 0x1F;
    ControlTransferBuffer[0] = 0;
    ControlTransferBuffer[1] = 0;

    // See where the request goes
    if (recipient == 0x00)
    {
        // Device
        RequestHandled = 1;
        // Set bits for self powered device and remote wakeup.
        if (SelfPowered)
            ControlTransferBuffer[0] |= 0x01;
        if (RemoteWakeup)
            ControlTransferBuffer[0] |= 0x02;
    }
    else if (recipient == 0x01)
    {
        // Interface
        RequestHandled = 1;
    }
    else if (recipient == 0x02)
    {
        // Endpoint
        uint8_t endpointNum = SetupPacket.wIndex0 & 0x0F;
        uint8_t endpointDir = SetupPacket.wIndex0 & 0x80;
        RequestHandled = 1;
        // Endpoint descriptors are 8 bytes long, with each in and out taking 4 bytes
        // within the endpoint. (See PIC datasheet.)
        inPtr = (uint8_t *)&Interfaces[0].Output + (endpointNum * 8);
        if (endpointDir)
            inPtr += 4;
        if(*inPtr & BSTALL)
            ControlTransferBuffer[0] = 0x01;
    }

    if (RequestHandled)
    {
        outPtr = (uint8_t *)&ControlTransferBuffer;
        wCount = 2;
		transferType=0;
    }
}

// Process SET_FEATURE and CLEAR_FEATURE
static void SetFeature(void)
{
    uint8_t recipient = SetupPacket.bmRequestType & 0x1F;
    uint8_t feature = SetupPacket.wValue0;

    if (recipient == 0x00)
    {
        // Device
        if (feature == DEVICE_REMOTE_WAKEUP)
        {
            RequestHandled = 1;
            if (SetupPacket.bRequest == SET_FEATURE)
                RemoteWakeup = 1;
            else
                RemoteWakeup = 0;
        }
        // TBD: Handle TEST_MODE
    }
    else if (recipient == 0x02)
    {
        // Endpoint
        uint8_t endpointNum = SetupPacket.wIndex0 & 0x0F;
        uint8_t endpointDir = SetupPacket.wIndex0 & 0x80;
        if ((feature == ENDPOINT_HALT) && (endpointNum != 0))
        {
            // Halt endpoint (as long as it isn't endpoint 0)
            RequestHandled = 1;
            // Endpoint descriptors are 8 bytes long, with each in and out taking 4 bytes
            // within the endpoint. (See PIC datasheet.)
            inPtr = (uint8_t *)&Interfaces[0].Output + (endpointNum * 8);
            if (endpointDir)
                inPtr += 4;

            if(SetupPacket.bRequest == SET_FEATURE)
                *inPtr = 0x84;
            else
            {
                if(endpointDir == 1)
                    *inPtr = 0x00;
                else
                    *inPtr = 0x88;
            }
        }
    }
}

void ProcessStandardRequest(void)
{
    uint8_t request = SetupPacket.bRequest;

    if((SetupPacket.bmRequestType & 0x60) != 0x00)
        // Not a standard request - don't process here.  Class or Vendor
        // requests have to be handled seperately.
        return;

    if (request == SET_ADDRESS)
    {
            // Set the address of the device.  All future requests
            // will come to that address.  Can't actually set UADDR
            // to the new address yet because the rest of the SET_ADDRESS
            // transaction uses address 0.
            RequestHandled = 1;
            DeviceState = ADDRESS;
            DeviceAddress = SetupPacket.wValue0;
    }
    else if (request == GET_DESCRIPTOR)
    {
            GetDescriptor();
    }
    else if (request == SET_CONFIGURATION)
    {
            RequestHandled = 1;
            CurrentConfiguration = SetupPacket.wValue0;
            // TBD: ensure the new configuration value is one that
            // exists in the descriptor.
            if (CurrentConfiguration == 0)
                // If configuration value is zero, device is put in
                // address state (USB 2.0 - 9.4.7)
                DeviceState = ADDRESS;
            else
            {
                // Set the configuration.
                DeviceState = CONFIGURED;

                // Initialize the endpoints for all interfaces
                HIDInitEndpoints();

                // TBD: Add initialization code here for any additional
                // interfaces beyond the one used for the HID
            }
    }
    else if (request == GET_CONFIGURATION)
    {
            RequestHandled = 1;
            outPtr = (uint8_t*)&CurrentConfiguration;
            wCount = 1;
			transferType=0;
    }
    else if (request == GET_STATUS)
    {
            GetStatus();
    }
    else if ((request == CLEAR_FEATURE) ||
        (request == SET_FEATURE))
    {
            SetFeature();
    }
    else if (request == GET_INTERFACE)
    {
            // No support for alternate interfaces.  Send
            // zero back to the host.
            RequestHandled = 1;
            ControlTransferBuffer[0] = 0;
            outPtr = (uint8_t*)&ControlTransferBuffer;
            wCount = 1;
			transferType=0;
    }
    else if (request == SET_INTERFACE)
    {
            // No support for alternate interfaces - just ignore.
            RequestHandled = 1;
    }
    else if (request == SET_DESCRIPTOR)
    {
    }
    else if (request == SYNCH_FRAME)
    {
    }
    else
    {
    }
}

// Data stage for a Control Transfer that sends data to the host
void InDataStage(void)
{
    uint8_t i;
    uint16_t bufferSize;

    // Determine how many bytes are going to the host
    if(wCount < E0SZ)
        bufferSize = wCount;
    else
        bufferSize = E0SZ;

    // Load the high two bits of the byte count into BC8:BC9
    Interfaces[0].Input.Stat &= ~(BC8 | BC9); // Clear BC8 and BC9
    Interfaces[0].Input.Stat |= (uint8_t)((bufferSize & 0x0300) >> 8);
    Interfaces[0].Input.Cnt = (uint8_t)(bufferSize & 0xFF);
    Interfaces[0].Input.ADDR = PTR16(&ControlTransferBuffer);

    // Update the number of bytes that still need to be sent.  Getting
    // all the data back to the host can take multiple transactions, so
    // we need to track how far along we are.
    wCount = wCount - bufferSize;

    // Move data to the USB output buffer from wherever it sits now.
    inPtr = (uint8_t *)&ControlTransferBuffer;
	if(transferType==1) for(i=0;i<bufferSize;i++) *inPtr++ = *ROMoutPtr++;
	else for(i=0;i<bufferSize;i++) *inPtr++ = *outPtr++;
}

// Data stage for a Control Transfer that reads data from the host
void OutDataStage(void)
{
    uint16_t i, bufferSize;

    bufferSize = ((0x03 & Interfaces[0].Output.Stat) << 8) | Interfaces[0].Output.Cnt;

    // Accumulate total number of bytes read
    wCount = wCount + bufferSize;

    outPtr = (uint8_t*)&ControlTransferBuffer;

    for (i=0;i<bufferSize;i++)
    {
        *inPtr++ = *outPtr++;
    }
}

// Process the Setup stage of a control transfer.  This code initializes the
// flags that let the firmware know what to do during subsequent stages of
// the transfer.
void SetupStage(void)
{
    // Note: Microchip says to turn off the UOWN bit on the IN direction as
    // soon as possible after detecting that a SETUP has been received.
    Interfaces[0].Input.Stat &= ~UOWN;
    Interfaces[0].Output.Stat &= ~UOWN;

    // Initialize the transfer process
    CtrlTransferStage = SETUP_STAGE;
    RequestHandled = 0; // Default is that request hasn't been handled
    HIDPostProcess = 0; // Assume standard request until know otherwise
    wCount = 0;         // No bytes transferred

    // See if this is a standard (as definded in USB chapter 9) request
    ProcessStandardRequest();

    // See if the HID class can do something with it.
    ProcessHIDRequest();

    // TBD: Add handlers for any other classes/interfaces in the device

    if (!RequestHandled)
    {
        // If this service wasn't handled then stall endpoint 0
        Interfaces[0].Output.Cnt = E0SZ;
        Interfaces[0].Output.ADDR = PTR16(&SetupPacket);
        Interfaces[0].Output.Stat = UOWN | BSTALL;
        Interfaces[0].Input.Stat = UOWN | BSTALL;
    }
    else if (SetupPacket.bmRequestType & 0x80)
    {
        // Device-to-host
        if(SetupPacket.wLength < wCount)
            wCount = SetupPacket.wLength;
        InDataStage();
        CtrlTransferStage = DATA_IN_STAGE;
        // Reset the out buffer descriptor for endpoint 0
        Interfaces[0].Output.Cnt = E0SZ;
        Interfaces[0].Output.ADDR = PTR16(&SetupPacket);
        Interfaces[0].Output.Stat = UOWN;

        // Set the in buffer descriptor on endpoint 0 to send data
        Interfaces[0].Input.ADDR = PTR16(&ControlTransferBuffer);
        // Give to SIE, DATA1 packet, enable data toggle checks
        Interfaces[0].Input.Stat = UOWN | DTS | DTSEN;
    }
    else
    {
        // Host-to-device
        CtrlTransferStage = DATA_OUT_STAGE;

        // Clear the input buffer descriptor
        Interfaces[0].Input.Cnt = 0;
        Interfaces[0].Input.Stat = UOWN | DTS | DTSEN;

        // Set the out buffer descriptor on endpoint 0 to receive data
        Interfaces[0].Output.Cnt = E0SZ;
        Interfaces[0].Output.ADDR = PTR16(&ControlTransferBuffer);
        // Give to SIE, DATA1 packet, enable data toggle checks
        Interfaces[0].Output.Stat = UOWN | DTS | DTSEN;
    }

    // Enable SIE token and packet processing
    UCONbits.PKTDIS = 0;
}

// Configures the buffer descriptor for endpoint 0 so that it is waiting for
// the status stage of a control transfer.
void WaitForSetupStage(void)
{
    CtrlTransferStage = SETUP_STAGE;
    Interfaces[0].Output.Cnt = E0SZ;
    Interfaces[0].Output.ADDR = PTR16(&SetupPacket);
    Interfaces[0].Output.Stat = UOWN | DTSEN; // Give to SIE, enable data toggle checks
    Interfaces[0].Input.Stat = 0x00;         // Give control to CPU
}

// This is the starting point for processing a Control Transfer.  The code directly
// follows the sequence of transactions described in the USB spec chapter 5.  The
// only Control Pipe in this firmware is the Default Control Pipe (endpoint 0).
// Control messages that have a different destination will be discarded.
void ProcessControlTransfer(void)
{
    if (USTAT == 0)
    {
        // Endpoint 0:out
        uint8_t PID = (Interfaces[0].Output.Stat & 0x3C) >> 2; // Pull PID from middle of BD0STAT
        if (PID == 0x0D)
            // SETUP PID - a transaction is starting
            SetupStage();
        else if (CtrlTransferStage == DATA_OUT_STAGE)
        {
            OutDataStage();
            if(Interfaces[0].Output.Stat & DTS)
                Interfaces[0].Output.Stat = UOWN | DTSEN;
            else
                Interfaces[0].Output.Stat = UOWN | DTS | DTSEN;
        }
        else
        {
            // Prepare for the Setup stage of a control transfer
            WaitForSetupStage();
        }
    }

    else if(USTAT == 0x04)
    {
        // Endpoint 0:in
        if ((UADDR == 0) && (DeviceState == ADDRESS))
        {
            // TBD: ensure that the new address matches the value of
            // "deviceAddress" (which came in through a SET_ADDRESS).
            UADDR = SetupPacket.wValue0;
            if(UADDR == 0)
                // If we get a reset after a SET_ADDRESS, then we need
                // to drop back to the Default state.
                DeviceState = DEFAULT;
        }

        if (CtrlTransferStage == DATA_IN_STAGE)
        {
            // Start (or continue) transmitting data
            InDataStage();

            // Turn control over to the SIE and toggle the data bit
            if(Interfaces[0].Input.Stat & DTS)
                Interfaces[0].Input.Stat = UOWN | DTSEN;
            else
                Interfaces[0].Input.Stat = UOWN | DTS | DTSEN;
        }
        else
        {
            // Prepare for the Setup stage of a control transfer
            WaitForSetupStage();
        }
    }
    else
    {
    }
}

void ResetPPBuffers(void)
{
    do
    {
        UCONbits.PPBRST = 1;
	UCONbits.PPBRST = 0;
    } while(0);
}

void InitializeUSB(void)
{
    UCFG = 0x14; // Enable pullup resistors; full speed mode; No PingPong
    DeviceState = DETACHED;
    RemoteWakeup = 0x00;
    CurrentConfiguration = 0x00;
    UADDR = 0; // Reset USB Address
    UEIR = 0; // Clear all USB Error Interrupt Flags
    ResetPPBuffers();
    UCONbits.PKTDIS = 0;// Enable Packet Transfers
}

void EnableUSBModule(void)
{
    // TBD: Check for voltage coming from the USB cable and use that
    // as an indication we are attached.
    if(UCONbits.USBEN == 0)
    {
        UCON = 0;
        UIE = 0;
        UCONbits.USBEN = 1;
        DeviceState = ATTACHED;
    }

    while (UCONbits.SE0); //Single Ended Zero Is Set - Busywait for initial power-up

    // If we are attached and no single-ended zero is detected, then
    // we can move to the Powered state.
    UIR = 0;
    UIE = 0;
    UIEbits.URSTIE = 1; //USB Reset Interrupt Enable bit
    UIEbits.IDLEIE = 1; //Idle Detect Interrupt Enable bit
    DeviceState = POWERED;
}

// Unsuspend the device
void UnSuspend(void)
{
    UCONbits.SUSPND = 0;   // Bring USB module out of power conserve
    UIEbits.ACTVIE = 0;
    UIR &= 0xFB;
}

// Full speed devices get a Start Of Frame (SOF) packet every 1 millisecond.
// Nothing is currently done with this interrupt (it is simply masked out).
void StartOfFrame(void)
{
    // TBD: Add a callback routine to do something
    UIRbits.SOFIF = 0;
}

// This routine is called in response to the code stalling an endpoint.
void Stall(void)
{
    if(UEP0bits.EPSTALL == 1)
    {
        // Prepare for the Setup stage of a control transfer
        WaitForSetupStage();
        UEP0bits.EPSTALL = 0;
    }
    UIRbits.STALLIF = 0;
}

// Suspend all processing until we detect activity on the USB bus
void Suspend(void)
{
    UIEbits.ACTVIE = 1;                     // Enable bus activity interrupt
    UIR &= 0xEF;
    UCONbits.SUSPND = 1;                   // Put USB module in power conserve
}

void BusReset()
{
    UEIR  = 0x00;
    UIR   = 0x00;
    UEIE  = 0x9f;
    UIE   = 0x7b;
    UADDR = 0x00;

    // Set endpoint 0 as a control pipe
    UEP0 = 0x16;

    // Flush any pending transactions
    while (UIRbits.TRNIF == 1) UIRbits.TRNIF = 0;

    // Enable packet processing
    UCONbits.PKTDIS = 0;

    // Prepare for the Setup stage of a control transfer
    WaitForSetupStage();

    RemoteWakeup = 0;         // Remote wakeup is off by default
    SelfPowered = 0;          // Self powered is off by default
    CurrentConfiguration = 0; // Clear active configuration
    DeviceState = DEFAULT;
}

// Main entry point for USB tasks.  Checks interrupts, then checks for transactions.
void ProcessUSBTransactions(void)
{
    // See if the device is connected yet.
    if(DeviceState == DETACHED)
    {
        UIR = 0; // Clear All Interrupt Flags
        UsbInterrupt = 0; // Clear Global Usb Interrupt Flag
	return;
    }

    // If the USB became active then wake up from suspend
    if(UIRbits.ACTVIF && UIEbits.ACTVIE)
    {
        UnSuspend();
	ClearUsbInterruptFlag(USB_RESUM);
    }

    // If we are supposed to be suspended, then don't try performing any
    // processing.
    if(UCONbits.SUSPND == 1)
    {
        UIR = 0; // Clear All Interrupt Flags
        UsbInterrupt = 0; // Clear Global Usb Interrupt Flag
        return;
    }

     // Process a bus reset
    if (UIRbits.URSTIF && UIEbits.URSTIE)
    {
        BusReset();
    	ClearUsbInterruptFlag(USB_URST);
    }

    if (UIRbits.IDLEIF && UIEbits.IDLEIE)
    {
        // No bus activity for a while - suspend the firmware
        Suspend();
    	ClearUsbInterruptFlag(USB_IDLE);
    }

    if (UIRbits.SOFIF && UIEbits.SOFIE)
    {
        StartOfFrame();
        ClearUsbInterruptFlag(USB_SOF);
    }

    if (UIRbits.STALLIF && UIEbits.STALLIE)
    {
        Stall();
        ClearUsbInterruptFlag(USB_STALL);
    }

    if (UIRbits.UERRIF && UIEbits.UERRIE)
    {
        // TBD: See where the error came from.
        // Clear errors
        UIRbits.UERRIF = 0;
        UEIR = 0 ; //     Clear All Usb Error Interrupt Flags
        ClearUsbInterruptFlag(USB_UERR);
    }

    // Unless we have been reset by the host, no need to keep processing
    if (DeviceState < DEFAULT)
    {
        UIR = 0; // Clear All Interrupt Flags
        UsbInterrupt = 0; // Clear Global Usb Interrupt Flag
        return;
    }

    // A transaction has finished.  Try default processing on endpoint 0.
    if(UIRbits.TRNIF && UIEbits.TRNIE)
    {
        ProcessControlTransfer();
        ClearUsbInterruptFlag(USB_TRN);
    }
    UsbInterrupt = 0; // Clear Global Usb Interrupt Flag
}
