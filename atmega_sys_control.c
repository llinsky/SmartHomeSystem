/*************************************************************
 *       atmega_sys_control.c - Embedded software controller for primary system. Controls
 *		 radio communication through Xbee, LCD interface, and physical user interface command
 *		 handling.
 *
 *       This system has the ability to take input from a certain number of external
 *       buttons that will affect the current device state. Through using this user input to change
 *       stored settings values, a user can configure the home management device to change the
 *       room environment as s/he sees fit.
 *
 *       PORTB, bit 7 (0x80) - Input from button 0
 *              bit 4 (0x10) - Output to RS (Register Select) input of display
 *              bit 3 (0x08) - Output to R/W (Read/Write) input of display
 *              bit 2 (0x04) - Output to E (Enable) input of display
 *       PORTB, bits 0-1, PORTD, bits 2-7 - Outputs to DB0-DB7 inputs of display.
 *       PORTC, bits 1-5 - Inputs from buttons 1-4
 *
 *************************************************************/

#include <avr/eeprom.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

// Pre-declare C functions

void var_config();
void packet_config();

// Clock settings/timing
void clk();

// Settings configuration
void tempr_config(uint8_t *, uint8_t *);
void humid_config(uint8_t *, uint8_t *);
void light_config(uint8_t *, uint8_t *);

// Button manipulation
void btn_db_mod();
uint8_t btn_db_val();

// LCD configuration
void initialize(void);
void strout(int, unsigned char *);
void cmdout(unsigned char, unsigned char);
void datout(unsigned char);
void busywt(void);


// Serial I/O configuration
void usart_init(unsigned short ubrr);
unsigned char usart_in_xbee(void);
void usart_out_xbee(char ch);
unsigned char usart_in_imp(void);
void usart_out_imp(char ch);

// ---------- DEFINES ----------

#define FOSC            9830400         // Clock frequency
#define BAUD            9600            // Baud rate used by the LCD and electric IMP
#define MYUBRR          FOSC/16/BAUD-1  // Value for UBRR0 register

// Define buttons for use in the main loop
#define BTN_0          (1 << PB7)
#define BTN_1          (1 << PC1)
#define BTN_2          (1 << PC2)
#define BTN_3          (1 << PC3)

// Define clock variables
#define TCLCL           4

// Define EEPROM locations
#define TEMPR_0         0X20
#define TEMPR_1         0x21
#define HUMID_0         0x22
#define HUMID_1         0x23
#define LIGHT_0         0x24
#define LIGHT_1         0x25
#define PACKET0         0x26
#define PACKET1         0x27
#define PACKET2         0x28

// Define bits for LCD initialisation
#define LCD_RS          0x10
#define LCD_RW          0x08
#define LCD_E           0x04
#define LCD_Bits       (LCD_RS|LCD_RW|LCD_E)

#define LCD_Data_B      0x03    // Bits in Port B for LCD data
#define LCD_Data_D      0xFC    // Bits in Port D for LCD data

#define WAIT            1
#define NOWAIT          0

// ---------- GLOBALS ----------

// Define global variables (embedded system...)
char str_0[24];
char str_1[24];

volatile uint8_t current = 0;   // Currently selected mode
volatile uint8_t editing = 0;   // Whether or not user is editing stored data
volatile uint8_t changed = 0;   // Whether the user edited anything

uint8_t counter   = TCLCL;
uint8_t pos_level = 0;

// Define global bools for integration purposes
bool fan_on       = false;
bool cooler_on    = false;
bool heater_on    = false;
bool ac_auto      = false;
uint8_t tempr_val = 75;

bool humid_on     = false;
uint8_t humid_val = 40;

unsigned char temp_sen = 0;
unsigned char humid_sen = 0;

bool lights       = false;
bool lights_auto  = false;

int main(void) {
    uint8_t one = 1;      // Warning solved on 04/22/08
    // The variable "current" may have any one of three values:
    //     00 - Current mode is temperature mode
    //     01 - Current mode is humidity mode
    //     10 - Current mode is lighting mode
    //     11 - Illegal combination
    
    unsigned char tempDataByte = 0;
    unsigned char io_char = 0;
    unsigned char temp_char = 0;
    unsigned char humid_char = 0;
    
    // Initialise string buffers to null terminators.
    str_0[0] = '\0';
    str_1[0] = '\0';
    
    // Initialise LCD bits.
    DDRB |= LCD_Data_B;         // Set PORTB bits 0-1 for output.
    DDRB |= LCD_Bits;           // Set PORTB bits 2, 3, and 4 for output.
    DDRD |= LCD_Data_D;         // Set PORTD bits 2-7 for output.
    
    // Initialise serial I/O and XBee.
    usart_init(MYUBRR);
    
    initialize();               // Initialise the LCD display.
    // Note that LCD can only display 24 characters per line.
    
    // Initialise EEPROM data to be all zeroes except temperature and humidity.
    // Initialise default temperature to 75 F and default humidity to 40%.
    if (eeprom_read_byte((uint8_t *) TEMPR_0) == 0xFF) {
        eeprom_write_byte((uint8_t *) TEMPR_0, 117);
        eeprom_write_byte((uint8_t *) TEMPR_1, 0);
        eeprom_write_byte((uint8_t *) HUMID_0, 64);
        eeprom_write_byte((uint8_t *) HUMID_1, 0);
        eeprom_write_byte((uint8_t *) LIGHT_0, 0);
        eeprom_write_byte((uint8_t *) LIGHT_1, 0);
        eeprom_write_byte((uint8_t *) PACKET0, 0);
        eeprom_write_byte((uint8_t *) PACKET1, 0);
        eeprom_write_byte((uint8_t *) PACKET2, 0);
    }
    
    uint8_t current_loop = current;
    while (one) {                         // Outer loop is for switching modes and reading memory.
    	
        uint8_t * addr = (current == 2) ? (uint8_t *) LIGHT_0 :
        (current == 1) ? (uint8_t *) HUMID_0 : (uint8_t *) TEMPR_0;
        uint8_t data_0 = eeprom_read_byte(addr);
        uint8_t data_1 = eeprom_read_byte(addr+1);
        uint8_t local_data_0 = data_0;
        uint8_t local_data_1 = data_1;
        while (one) {                       // Inner loop is for editing values.
        	
            btn_db_mod();
            if (!editing && changed) {
                // If the user has stopped editing and has changed some values, update EEPROM.
                eeprom_update_byte(addr, local_data_0);
                eeprom_update_byte(addr+1, local_data_1);
            }
            if (current_loop != current) {
                // If the user has changed the current selection, change the LCD to reflect new selection.
                current_loop = current;
                break;
            }
            switch (current) {
                case 0: tempr_config(&local_data_0, &local_data_1); break;
                case 1: humid_config(&local_data_0, &local_data_1); break;
                case 2: light_config(&local_data_0, &local_data_1); break;
                default: break;
            }
            strout(0x00, (unsigned char *) str_0); // Print first line of text to LCD.
            strout(0x40, (unsigned char *) str_1); // Print second line of text to LCD.
            
            // Run the internal clock.
            clk();
            
            
            
            packet_config();
            io_char = eeprom_read_byte((uint8_t *) PACKET0);
			temp_char = eeprom_read_byte((uint8_t *) PACKET1);
			humid_char = eeprom_read_byte((uint8_t *) PACKET2);
            
            
            UCSR0B &= ~(1 << RXEN0);
            _delay_ms(5);
            UCSR0B |= (1 << RXEN0);
            _delay_ms(5);
            
            tempDataByte = usart_in_imp();
            if (tempDataByte == 0xA9) {
            	tempDataByte = usart_in_imp();
            if (tempDataByte == 0x65) {
            	tempDataByte = usart_in_imp();
            	
            if (tempDataByte != 0xFF) {
            	io_char = tempDataByte;
            	tempDataByte = usart_in_imp();
            		if (tempDataByte != 0xFF) {
            			temp_char = tempDataByte;
            			tempDataByte = usart_in_imp();
            				if (tempDataByte != 0xFF) {
            					humid_char = tempDataByte;
            					
            					//At this point we have presumably valid data
            					//check if it is a status request or not
            					
            					if ((temp_char & 0x80) != 0x00) {
            						//send data to imp
            						packet_config(); //Make sure data is current
            						usart_out_imp(eeprom_read_byte((uint8_t *) PACKET0)); 
            						usart_out_imp(eeprom_read_byte((uint8_t *) PACKET1)); 
            						usart_out_imp(eeprom_read_byte((uint8_t *) PACKET2)); 
            					}
            					
            					else {
            						//update our data and send to xbee
            						eeprom_update_byte(((uint8_t *) PACKET0), io_char);
            						eeprom_update_byte(((uint8_t *) PACKET1), temp_char);
            						eeprom_update_byte(((uint8_t *) PACKET2), humid_char);
            						
            						//TODO: reverse packet config method - given packets in memory, modify the control variables appropriately
            						var_config();
            						
            						usart_out_xbee(io_char);
            						usart_out_xbee(temp_char);
            						usart_out_xbee(humid_char);
            					}
            				}
           			}
            }
            
            }// end if header 1
            }// end if header 0
            
            UCSR0B &= ~(1 << RXEN0);
            _delay_ms(5);
            UCSR0B |= (1 << RXEN0);
            _delay_ms(5);
            
            tempDataByte = usart_in_xbee();
            //if (tempDataByte != 0xFF) {
            if (tempDataByte == 0xE3) {
            	tempDataByte = usart_in_xbee();
            	if (tempDataByte != 0xFF) {
            		temp_char = tempDataByte;
            		tempDataByte = usart_in_xbee();
            		if (tempDataByte != 0xFF) {
            			humid_char = tempDataByte;
            			
            			humid_sen = (humid_char & 0x7F);
            			temp_sen = (temp_char & 0x7F);

            			
            			usart_out_xbee(0xD4);
            			usart_out_xbee(io_char);
            			usart_out_xbee(temp_char);
            			usart_out_xbee(humid_char);
            			
           			}
           		}
            }
            
            
            
            _delay_ms(50);
        }
    }
    return 0;                             // Should never be reached in embedded system!
}

/*
 clk - Advance the clock and adjust relevant variables accordingly.
 */
void clk() {
    // Decrement the clock counter.
    counter--;
    // Reset the clock if necessary.
    if (counter == 0) {
        counter = TCLCL;
        pos_level = 0;
    } else if (counter == TCLCL/2) {
        // Set the positive clock level if applicable.
        pos_level = 1;
    }
}

/*
 data_corruption - States that data has been corrupted at a location in the LCD.
 */
void data_corruption(uint8_t address)
{
    // Reset string buffers
    str_0[0] = '\0';
    str_1[1] = '\0';
    char buf[8];  // Temporary character buffer for LCD display
    
    // Display data corruption error and location.
    strcat(str_0, "Data corruption during  ");
    sprintf(buf, "0x%X", address);
    strcat(str_1, "read! addr: ");
    strcat(str_1, buf);
    strcat(str_1, "        ");
    
    // Print the text to the LCD and busywait forever (crash).
    strout(0x00, (unsigned char *) str_0); // Print first line of text to LCD.
    strout(0x40, (unsigned char *) str_1); // Print second line of text to LCD.
    
    _delay_ms(2000);
}

/*
 prepare_config - Read from EEPROM and determine values to send in an outbound packet to either
 the imp or the sensor array.
 */

void var_config()
{
	int byte_bools = eeprom_read_byte((uint8_t *) PACKET0);
	int byte_tempr = eeprom_read_byte((uint8_t *) PACKET1);
	int byte_humid = eeprom_read_byte((uint8_t *) PACKET2);
	
	/*
	lights = ((statusBit0 & 0x40) != 0x00);
    lights_auto = ((statusBit0 & 0x20) != 0x00);
    
    heater_on = ((statusBit0 & 0x10) != 0x00);
    cooler_on = ((statusBit0 & 0x08) != 0x00);
    fan_on = ((statusBit0 & 0x04) != 0x00);
    
    ac_auto = ((statusBit0 & 0x02) != 0x00);
    
    
    //Update temperature and humidity data
    
    tempr_val = (statusBit1 & 0x7F);
    humid_val = (statusBit2 & 0x7F);
    humid_on = ((statusBit2 & 0x80) != 0x00);
    */
    
    uint8_t tempr_MSD = (byte_tempr / 10) << 4;
  	uint8_t tempr_LSD = byte_tempr % 10;
  	uint8_t tempr_BCD = tempr_MSD | tempr_LSD;
  
  	uint8_t tempr_set = (byte_bools & 0x02) ? 0 :
                      (byte_bools & 0x04) ? 1 :
                      (byte_bools & 0x08) ? 2 : 3;
  	tempr_set <<= 6;
  
  	eeprom_update_byte((uint8_t *) TEMPR_0, tempr_BCD);
  	eeprom_update_byte((uint8_t *) TEMPR_1, tempr_set);
  
  	// Write humidity data and settings.
  	uint8_t humid_MSD = (byte_humid / 10) << 4;
  	uint8_t humid_LSD = byte_humid % 10;
  	uint8_t humid_BCD = humid_MSD | humid_LSD;
  
  	uint8_t humid_set = byte_humid & 0x80;
  
  	eeprom_update_byte((uint8_t *) HUMID_0, humid_BCD);
  	eeprom_update_byte((uint8_t *) HUMID_1, humid_set);
  
  	// Write light settings.
  	uint8_t light_set = (byte_bools & 0x40) ? 0 :
                      (byte_bools & 0x20) ? 2 : 1;
  	light_set <<= 6;
  
  	eeprom_update_byte((uint8_t *) LIGHT_0, light_set);

}

void packet_config()
{
    // Perform the very slow process of reading from the entire EEPROM
    uint8_t data_0 = eeprom_read_byte((uint8_t *) TEMPR_0);
    uint8_t data_1 = eeprom_read_byte((uint8_t *) TEMPR_1);
    uint8_t data_2 = eeprom_read_byte((uint8_t *) HUMID_0);
    uint8_t data_3 = eeprom_read_byte((uint8_t *) HUMID_1);
    uint8_t data_4 = eeprom_read_byte((uint8_t *) LIGHT_0);
    
    // First, read temperature data (1 word)
    // Byte stored in data_0 contains temperature information in BCD form
    //     Return if value is greater than hex 9
    uint8_t tempr_high = (data_0 & 0xF0) >> 4;    // Upper digit of temperature
    uint8_t tempr_low = data_0 & 0x0F;            // Lower digit of temperature
    if (tempr_high > 9 || tempr_low > 9) data_corruption(TEMPR_0);
    // Byte stored in data_1 contains settings information
    uint8_t mode = (data_1 & 0xC0) >> 6;          // Temperature unit mode settings
    
    // Generate a raw temperature value for stored target from digit values.
    tempr_val = tempr_high * 10 + tempr_low;
    
    // Set global bools based on mode value
    ac_auto   = (mode == 0);
    fan_on    = (mode == 1);
    heater_on = (mode == 2);
    cooler_on = (mode == 3);
    
    // Second, read humidity data (1 word)
    // Byte stored in data_2 contains humidity information in BCD form
    //     Return if value is greater than hex 9
    uint8_t humid_high = (data_2 & 0xF0) >> 4;    // Upper digit of humidity
    uint8_t humid_low = data_2 & 0x0F;            // Lower digit of humidity
    //if (humid_high > 9 || humid_low > 9) data_corruption(HUMID_0);
    
    // Byte stored in data_1 contains settings information, though only 1 bit of it
    uint8_t hum_e = (data_3 & 0x80) >> 7;         // Humidifier enable (1 is on)
    
    // Generate a raw humidity value for stored target from digit values.
    humid_val = humid_high * 10 + humid_low;
    
    // Set global bool based on humidity enable
    humid_on = (hum_e == 1);
    
    // Third, read lighting data (1 byte)
    // Byte stored in data_0 contains settings information
    uint8_t light = (data_4 & 0xC0) >> 6;         // Light settings
    
    // Set global bools based on light settings
    lights      = (light == 2);
    lights_auto = (light == 0);
    
    // Create three bytes of data for later transmission.
    uint8_t byte_bools = 0;
    uint8_t byte_tempr = 0;
    uint8_t byte_humid = 0;
    
    // Set bytes with appropriate data
    byte_bools |= lights_auto << 6;
    byte_bools |= lights      << 5;
    byte_bools |= cooler_on   << 4;
    byte_bools |= heater_on   << 3;
    byte_bools |= fan_on      << 2;   // Unimplemented in sensor array
    byte_bools |= ac_auto     << 1;
    
    byte_tempr |= (tempr_val & 0x7F);
    byte_humid |= humid_on    << 7;
    byte_humid |= (humid_val & 0x7F);
    
    // Write the universal packets to the EEPROM for later retrieval.
    eeprom_update_byte((uint8_t *) PACKET0, byte_bools);
    eeprom_update_byte((uint8_t *) PACKET1, byte_tempr);
    eeprom_update_byte((uint8_t *) PACKET2, byte_humid);
}



/*
 tempr_config - Modify stored temperature and display temperature on LCD.
 */
void tempr_config(uint8_t * data_0, uint8_t * data_1)
{
    // TEMPR_0: BD[7:4] - upper digit, BD[3:0] - lower digit
    // TEMPR_1: BD[7:6] - Mode, BD[5:3] - Unused, BD[2:0] - Checksum
    
    // Byte stored in data_0 contains temperature information in BCD form
    //     Return if value is greater than hex 9
    uint8_t tempr_high = ((*data_0) & 0xF0) >> 4; // Upper digit of temperature
    uint8_t tempr_low = (*data_0) & 0x0F;         // Lower digit of temperature
    //if (tempr_high > 9 || tempr_low > 9) data_corruption(TEMPR_0);
    
    // Byte stored in data_1 contains settings information
    uint8_t mode = ((*data_1) & 0xC0) >> 6;       // Temperature unit mode settings
    // Remaining bits 5:0 are not data bits
    
    // Determine whether data is currently being edited.
    if (editing != 0) {
        uint8_t btn_type = btn_db_val();
        if (btn_type != 0) {
            changed = 1;
            if (editing == 1) {
                mode = (mode == 0) ? 1 :
                (mode == 1) ? 2 :
                (mode == 2) ? 3 : 0;
            } else {
                if (btn_type == 1) {
                    // Increment temperature by 1 degree F.
                    tempr_low++;
                    if (tempr_low > 9) {
                        tempr_low = 0;
                        tempr_high++;
                    }
                    // If the current temperature is 91, wrap around.
                    if (tempr_high == 9 && tempr_low > 0) {
                        tempr_high = 6;
                        tempr_low = 0;
                    }
                } else if (btn_type == 2) {
                    // Decrement temperature by 1 degree F.
                    tempr_low--;
                    if (tempr_low > 9) {
                        tempr_low = 9;
                        tempr_high--;
                    }
                    // If the current temperature is 5X, wrap around.
                    if (tempr_high == 5) {
                        tempr_high = 9;
                        tempr_low = 0;
                    }
                }
            }
            // Set the temperature data bits by clearing and or'ing.
            (*data_0) = 0;
            (*data_0) |= (tempr_high << 4);
            (*data_0) |= (tempr_low);
            
            // Set the remaining data bits.
            (*data_1) = 0;
            (*data_1) |= (mode << 6);
            // (*data_1) |= (checksum);
        }
    }
    
    char * mode_disp;   // Stores text representation of temperature mode.
    // Use ternary operator to create the appropriate text.
    mode_disp = (mode == 3) ? "Cold" :
    (mode == 2) ? " Hot" :
    (mode == 1) ? " Fan" : "Auto";
    
    // Toggle the currently edited field.
    if (editing != 0 && !pos_level) {
        if (editing == 1) mode_disp = "   ";
    }
    
    str_0[0] = '\0';  // Delete previous string to prepare for new one.
    str_1[0] = '\0';
    char buf[8];      // Create a temporary string buffer for storing numbers for printing.
    // Generate strings based on values, settings, and editing status
    strcat(str_0, "T        Type: ");
    strcat(str_0, mode_disp);
    strcat(str_0, "     ");
    
    strcat(str_1, "   Actual/Set: ");
    
    unsigned char tempLSD = temp_sen%10;
    unsigned char tempMSD = temp_sen/10;
    
    sprintf(buf, "%d%d", tempMSD, tempLSD);
    strcat(str_1, buf); // Temporary until sensors polling is working
    strcat(str_1, "/");
    // Toggle printing of temperature
    sprintf(buf, "%d", tempr_high);
    if (editing == 2 && !pos_level) buf[0] = ' ';
    strcat(str_1, buf);
    sprintf(buf, "%d", tempr_low);
    if (editing == 2 && !pos_level) buf[0] = ' ';
    strcat(str_1, buf);
    strcat(str_1, " F  ");
    
    // Prints something akin to the following:
    // T        Type: Cold     
    //    Actual/Set: 70/70 F  
}

void humid_config(uint8_t * data_0, uint8_t * data_1)
{
    // HUMID_0: BD[7:4] - upper digit, BD[3:0] - lower digit
    // HUMID_1: BD[7] - Humidifier, BD[6:0] unused
    
    // Byte stored in data_0 contains humidity information in BCD form
    //     Return if value is greater than hex 9
    uint8_t humid_high = ((*data_0) & 0xF0) >> 4; // Upper digit of humidity
    uint8_t humid_low = (*data_0) & 0x0F;         // Lower digit of humidity
    //if (humid_high > 9 || humid_low > 9) data_corruption(HUMID_0);
    
    // Byte stored in data_1 contains settings information, though only 1 bit of it
    uint8_t hum_e = ((*data_1) & 0x80) >> 7;      // Humidifier enable (1 is on)
    // Remaining bits 6:0 are unused
    
    // Determine whether data is currently being edited.
    if (editing != 0) {
        uint8_t btn_type = btn_db_val();
        if (btn_type != 0) {
            changed = 1;
            if (editing == 1) {
                hum_e = (hum_e == 0) ? 1 : 0;
            } else {
                if (btn_type == 1) {
                    // Increment humidity by 5 percent.
                    humid_low += 5;
                    if (humid_low > 9) {
                        humid_low = 0;
                        humid_high++;
                    }
                    // If the current temperature is 105, wrap around.
                    if (humid_high == 10)
                        humid_high = 0;
                } else if (btn_type == 2) {
                    // Decrement humidity by 5 percent.
                    humid_low -= 5;
                    if (humid_low > 9) {
                        humid_low = 5;
                        humid_high--;
                    }
                    // If the current humidity high is 255, wrap around.
                    if (humid_high > 9)
                        humid_high = 9;
                }
            }
            // Set the humidity data bits by clearing and or'ing.
            (*data_0) = 0;
            (*data_0) |= (humid_high << 4);
            (*data_0) |= (humid_low);
            
            // Set the remaining data bits.
            (*data_1) = 0;
            (*data_1) |= (hum_e << 7);
        }
    }
    
    char * hum_disp;  // Stores text representation of humidifier status.
    // Use ternary operators to create the appropriate text.
    hum_disp = (hum_e == 1) ? " On" : "Off";
    
    // Toggle the currently edited field.
    if (!pos_level && editing == 1) {
        hum_disp = "   ";
    }
    
    str_0[0] = '\0';  // Delete previous string to prepare for new one.
    str_1[0] = '\0';
    char buf[8];      // Create a temporary string buffer for storing numbers for printing.
    // Generate strings based on values, settings, and editing status
    strcat(str_0, "H      Humidifer: ");
    strcat(str_0, hum_disp);
    strcat(str_0, "   ");
    
    strcat(str_1, " Hum Actual/Set: ");
    
    unsigned char humidLSD = humid_sen%10;
    unsigned char humidMSD = humid_sen/10;
    
    sprintf(buf, "%d%d", humidMSD, humidLSD);
    strcat(str_1, buf); // Temporary until sensors polling is working
    strcat(str_1, "/");
    // Toggle printing of humidity
    sprintf(buf, "%d", humid_high);
    if (editing == 2 && !pos_level) buf[0] = ' ';
    strcat(str_1, buf);
    sprintf(buf, "%d", humid_low);
    if (editing == 2 && !pos_level) buf[0] = ' ';
    strcat(str_1, buf);
    strcat(str_1, "%");
    
    // Prints something akin to the following (note empty spaces):
    // H      Humidifer: Off   
    //   Hum Actual/Set: 70/70%
}

void light_config(uint8_t * data_0, uint8_t * data_1)
{
    // LIGHT_0: BD[7:6] - Light settings, BD[5:0] unused
    // LIGHT_1: BD[7:0] unused; memory location held for compatibility
    
    // Byte stored in data_0 contains settings information
    uint8_t light = ((*data_0) & 0xC0) >> 6;      // Light settings
    // Remaining bits 5:0 are unused
    
    // Determine whether data is currently being edited.
    if (editing != 0 && btn_db_val()) {
        changed = 1;
        light = (light == 0) ? 1 :
        (light == 1) ? 2 : 0;
        // Set the light settings data bits by clearing and or'ing.
        (*data_0) = 0;
        (*data_0) |= (light << 6);
        
        // Ignore the second data byte to save on EEPROM writes.
    }
    
    char * light_disp;    // Stores text representation of light settings.
    // Use ternary operators to create the appropriate text.
    light_disp = (light == 2) ? " On " :
    (light == 1) ? " Off" : "Auto";
    
    // Toggle the currently edited field.
    if (!pos_level && editing == 1) {
        light_disp = "      ";
    }
    
    str_0[0] = '\0';  // Delete previous string to prepare for new one.
    str_1[0] = '\0';
    // Generate strings based on values, settings, and editing status
    strcat(str_0, "L                       ");
    strcat(str_1, "    Lighting: ");
    strcat(str_1, light_disp);
    strcat(str_1, "      ");
    
    // Prints something akin to the following:
    // L                       
    //     Lighting: Auto      
}

/*
 btn_db_mode - Read buttons for determining which mode to enter; db is for debounce.
 */
void btn_db_mod()
{
    // Initialise local variables for storing button presses.
    volatile uint8_t btn_0 = BTN_0 & PINB;
    volatile uint8_t btn_1 = BTN_1 & PINC;
    
    // Check mode buttons for user activity unless user is in editing mode.
    if (editing == 0 && btn_0) {
        // Debounce mode selection button and iterate through available modes.
        while (btn_0) {
            btn_0 = BTN_0 & PINB;
        }
        current++;
        if (current > 2)
            current = 0;
        return;
    }
    
    // Check editing button to determine which field user is editing.
    if (btn_1) {
        while (btn_1) {
            btn_1 = BTN_1 & PINC;
        }
        if (current == 0) {
            // If currently in temperature mode, editing can go up to 2.
            editing++;
            if (editing > 2)
                editing = 0;
        } else if (current == 1) {
            // If current in humidity mode, editing can go up to 2.
            editing++;
            if (editing > 2)
                editing = 0;
        } else {
            // If in any other mode (lighting), editing can go up to 1 (toggle).
            editing = !editing;
        }
    }
}

/*
 btn_db_val - Read buttons for determining which value to edit in edit mode; db is for debounce.
 */
uint8_t btn_db_val()
{
    // Initialise local variables for storing button presses.
    volatile uint8_t btn_2 = BTN_2 & PINC;
    volatile uint8_t btn_3 = BTN_3 & PINC;
    
    if (btn_2) {
        while (btn_2) {
            btn_2 = BTN_2 & PINC;
        }
        return 1;
    } else if (btn_3) {
        while (btn_3) {
            btn_3 = BTN_3 & PINC;
        }
        return 2;
    }
    
    return 0;     // User did not press anything.
}

// ---------- LCD CONFIGURATION ----------

/*
 strout - Print the contents of the character string "s" starting at LCD
 RAM location "x".  The string must be terminated by a zero byte.
 */
void strout(int x, unsigned char *s)
{
    unsigned char ch;
    
    cmdout(x | 0x80, WAIT);   // Make A contain a Set Display Address command
    
    while ((ch = *s++) != (unsigned char) '\0') {
        datout(ch);     // Output the next character
    }
}

/*
 datout - Output a byte to the LCD display data register (the display)
 and wait for the busy flag to reset.
 */
void datout(unsigned char x)
{
    PORTB |= (x & LCD_Data_B);  // Put low 2 bits of data in PORTB
    PORTB &= (x | ~LCD_Data_B);
    PORTD |= (x & LCD_Data_D);  // Put high 6 bits of data in PORTD
    PORTD &= (x | ~LCD_Data_D);
    PORTB &= ~(LCD_RW|LCD_E);   // Set R/W=0, E=0, RS=1
    PORTB |= LCD_RS;
    PORTB |= LCD_E;             // Set E to 1
    PORTB &= ~LCD_E;            // Set E to 0
    busywt();                   // Wait for BUSY flag to reset
}

/*
 cmdout - Output a byte to the LCD display instruction register.  If
 "wait" is non-zero, wait for the busy flag to reset before returning.
 If "wait" is zero, return immediately since the BUSY flag isn't
 working during initialization.
 */

void cmdout(unsigned char x, unsigned char wait)
{
    PORTB |= (x & LCD_Data_B);  // Put low 2 bits of data in PORTB
    PORTB &= (x | ~LCD_Data_B);
    PORTD |= (x & LCD_Data_D);  // Put high 6 bits of data in PORTD
    PORTD &= (x | ~LCD_Data_D);
    PORTB &= ~LCD_Bits;         // Set R/W=0, E=0, RS=0
    PORTB |= LCD_E;             // Set E to 1
    PORTB &= ~LCD_E;            // Set E to 0
    if (wait)
        busywt();                   // Wait for BUSY flag to reset
}

/*
 initialize - Do various things to force a initialization of the LCD
 display by instructions, and then set up the display parameters and
 turn the display on.
 */
void initialize()
{
    _delay_ms(15);      // Delay at least 15ms
    
    cmdout(0x30, NOWAIT); // Send a 0x30
    _delay_ms(4);       // Delay at least 4msec
    
    cmdout(0x30, NOWAIT); // Send a 0x30
    _delay_us(120);     // Delay at least 100usec
    
    cmdout(0x38, WAIT); // Function Set: 8-bit interface, 2 lines
    
    cmdout(0x0f, WAIT); // Display and cursor on
}

/*
 busywt - Wait for the BUSY flag to reset.
 */
void busywt()
{
    unsigned char bf;
    
    PORTB &= ~LCD_Data_B;       // Set for no pull ups
    PORTD &= ~LCD_Data_D;
    DDRB &= ~LCD_Data_B;        // Set for input
    DDRD &= ~LCD_Data_D;
    
    PORTB &= ~(LCD_E|LCD_RS);   // Set E=0, R/W=1, RS=0
    PORTB |= LCD_RW;
    
    do {
        PORTB |= LCD_E;         // Set E=1
        _delay_us(1);           // Wait for signal to appear
        bf = PIND & 0x80;       // Read status register
        PORTB &= ~LCD_E;        // Set E=0
    } while (bf != 0);          // If Busy (PORTD, bit 7 = 1), loop
    
    DDRB |= LCD_Data_B;         // Set PORTB, PORTD bits for output
    DDRD |= LCD_Data_D;
}



// ---------- SERIAL I/O CONFIGURATION ----------



#define time_const1 10000
#define time_const2 20000  
#define time_const4 40000

void usart_init(unsigned short ubrr)
{
	UBRR0 = ubrr;
	UCSR0B |= (1 << TXEN0);
	UCSR0B |= (1 << RXEN0);
	UCSR0C = (3 << UCSZ00);
}


void usart_out_imp(char ch)
{
	PORTC &= ~(1 << PC0);
	_delay_ms(5);
	unsigned int timeOut = 0;
	while ((UCSR0A & (1 <<UDRE0)) == 0) {
		timeOut++;
		if (timeOut >= (time_const1)) {
			return;
		}
	}
	UDR0 = ch;
}

void usart_out_xbee(char ch)
{
	PORTC |= 1 << PC0;
	_delay_ms(5);
	unsigned int timeOut = 0;
	while ((UCSR0A & (1 <<UDRE0)) == 0) {
		timeOut++;
		if (timeOut >= (time_const1)) {
			return;
		}
	}
	UDR0 = ch;
	_delay_ms(5);
	PORTC &= ~(1 << PC0);
}

unsigned char usart_in_imp(void)
{
	_delay_ms(5);
	PORTC &= ~(1 << PC0); //Set select line to 0 to select Imp on UART mux
	_delay_ms(5);
	unsigned int timeOut = 0;
	while (!(UCSR0A & (1 << RXC0)))
	{
		timeOut++;
		if (timeOut >= (time_const1)) {
			return 0xFF;
		}
	}
	return UDR0;
}

unsigned char usart_in_xbee(void)
{
	_delay_ms(5);
	PORTC |= 1 << PC0; //Set select line to 1 to select Xbee on UART mux
	_delay_ms(5);
	unsigned int timeOut = 0;
	while (!(UCSR0A & (1 << RXC0)))
	{
		timeOut++;
		if (timeOut >= (time_const4)) {
			return 0xFF;
		}
	}
	return UDR0;
}


