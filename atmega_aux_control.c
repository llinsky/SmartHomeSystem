/*************************************************************
*       atmega_sys_control.c
*
*		Embedded microcontroller software main loop for auxilliary system controller -- communicates 
*		with the Electric Imp and relays commands from the user interface to HVAC relays and the 
*		cloud.
*************************************************************/



/*
Commands for the Electric Imp:

Packet will be sent from the iPhone encoded in hex in 2 bytes:

Will be called "command"

Example code: ?command=3y

'3y' is converted to OxSTAT (2 bytes hex)

First Byte: Controls Lights, Heater, Cooler, Fan, AutoTemp
Second Byte: Temperature (Fahrenheit) -- limit to 60<x<90 

status = 0xSTAT && 0x8000 //NO ALWAYS 0
UPDATE: status = 0xSTAT && 0x0080;

light = 0xSTAT && 0x4000
lightauto = 0xSTAT && 0x2000
heat = 0xSTAT && 0x1000
cold = 0xSTAT && 0x0800
fan = 0xSTAT && 0x0400
acauto = 0xSTAT && 0x0200
device = 0xSTAT && 0x0100 

temp = 0xSTAT && 0x00FF


?status=1   ---   Request the current status from the Atmel
If status is 1, the rest of the command is ignored (nothing 
is set)

Lights:
?light=0    ---   Turn Lights Off
?light=1    ---   Turn Lights On

?lightauto=1    ---   Autoset Lights On (Turn off lights first)
?lightauto=1    ---   Autoset Lights Off (Turn lights off)

A/C:
?heat=1     ---   Turn Heater On (and Cooler/Auto Off)  
?heat=0     ---   Turn Heater Off

?cold=1     ---   Turn Cooler On (and Heater/Auto Off)  
?cold=0     ---   Turn Cooler Off

?fan=1      ---   Turn Fan On (and A/C and Auto Off)  
?fan=0      ---   Turn Fan Off

?acauto=0     ---   Turn off Automatic Temperature (and all A/C)
?acauto=1     ---   Turn on Automatic Temp. (Turn off all A/C
//                  functions first before starting auto)

?device=1     ---   Device is set to 1 to notify that packet is
                    coming from iPhone. Set to 0 when sent from
                    hardware/sensor board


?temp=n     ---   Set the Auto Temperature (doesn't turn on auto)
^For setting temp, range is between 60 and 90 (Fahrenheit.)

All of this processing will be done in the Atmel
The Server just forwards on the message to the Imp
The Imp just sends from the server to the serial lines

The Atmel processes the data, adjusts settings, and then
*/



#define FOSC 8000000		// Clock frequency
#define BAUD 9600           // Baud rate used by the LCD and Imp
#define MYUBRR (FOSC/16/BAUD)-1 


#include <avr/io.h>
#include <util/delay.h>


void usart_init(unsigned short ubrr)
{
	UBRR0 = ubrr;
	UCSR0B |= (1 << TXEN0);
	UCSR0B |= (1 << RXEN0);
	UCSR0C = (3 << UCSZ00);
}


void usart_out(char ch)
{
	while ((UCSR0A & (1 <<UDRE0)) == 0);
	UDR0 = ch;
}

char usart_in(void)
{
	while (!(UCSR0A & (1 << RXC0)));
	return UDR0;
}


//Electric Imp Serial Input
int main(void) {

	DDRC |= 1 << DDC0;  //Set Port C to output 

	unsigned char currentState;
	
	unsigned char lightsOn = 1;
	unsigned char lightsAuto = 0;
	unsigned char coolerOn = 0;
	unsigned char heaterOn = 0;
	unsigned char fanOn = 0;
	unsigned char acAutoModeOn = 0;
	
	unsigned char temp = 72;
	
	
	
	unsigned short n;
	
	unsigned char io_char;
	unsigned char io_temp;
	
	
    usart_init(MYUBRR);                 // Initialize the SCI port
    
    while (1) {               // Loop forever
    	
    	io_char = usart_in();
    	io_temp = usart_in();
    	
    	
    	unsigned short i = 0;
    	n = 0;
    	
    	if ((io_char & 0x01) != 0x00) //coming from electric imp
    	{
    		if ((io_temp & 0x80) != 0x00) //STATUS set to request information
    		{
    			io_char = 0x00;
    			if (lightsOn > 0) {
    				io_char |= 0x40;
    			} if (lightsAuto > 0) {
    				io_char |= 0x20;
    			} if (coolerOn > 0) {
    				io_char |= 0x10;
    			} if (heaterOn > 0) {
    				io_char |= 0x08;
    			} if (fanOn > 0) {
    				io_char |= 0x04;
    			} if (acAutoModeOn > 0) {
    				io_char |= 0x02;
    			}
    			io_char |= 0x01; //Sending to Imp
    			
    			io_temp = temp;
    			
    			usart_out(io_char);
    			usart_out(io_temp);
    		}
    		else {
    			
    			if (io_char & 0x40) {
    				lightsOn = 1;
    			} else {
    				lightsOn=0;
    			} if (io_char & 0x20) {
    				lightsAuto = 1;
    			}  else {
    				lightsAuto=0;
    			} if (io_char & 0x10) {
    				coolerOn = 1;
    			}  else {
    				coolerOn=0;
    			} if (io_char & 0x08) {
    				heaterOn = 1;
    			}  else {
    				heaterOn=0;
    			} if (io_char & 0x04) {
    				fanOn = 1;
    			}  else {
    				fanOn=0;
    			} if (io_char & 0x02) {
    				acAutoModeOn = 1;
    			}  else {
    				acAutoModeOn=0;
    			}
    			
    		}
    	}
    		
    	if (lightsOn != 0) {
    		PORTC |= 1 << PC0;
    	} else {
    		PORTC &= ~(1 << PC0);
    	}
    	_delay_ms(100);
    	
    }
    return 0;   // never reached 
}
