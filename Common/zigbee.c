//zigbee.c


#include "HardwareProfile.h"
#include "AppConfig.h"

#include "./USB/usb_function_cdc.h"
#include "C:\MCC18\h\usart.h"

//#ifdef APP_USE_ZIGBEE

#include <delays.h>
#include <stdlib.h>
#include <string.h>
#include <timers.h>
#include "C:\MCC18\h\usart.h"

#include "power_hold.h"
#include "zigbee.h"
#include "eeprom_task.h"


char Zigbee_getcUSART(void);

#pragma idata ZigbeeNodesBuffer
char FIRMWARE_VERSION[2];
char node_list[10][12];

#pragma idata ZigbeeDataBuffer
//char INCOMING_BUFFER[60] = "";

#pragma idata

char ZIGBEE_TASKS = 0;

char			NETWORK_ADDRESS[2];
char			DEVICE_TYPE[2];
char			SERIAL_NUMBER[8];
char			PARENT_ADDRESS[32] = "";

static char		CURRENT_DEST_ADDR[5] = "FFFF";
char			IS_REGISTERED_TO_SERVER=0;
unsigned char 	LOCK_RX_BUFFER = 0;
unsigned char	RECEIVED_SERVER_NUDGE = 0;

char 			telnet_timer;
char			DATETIME[13] = "151210163045";
char			Is_DateTime_Modified;
char 			Receiving_USART = 0;
static unsigned char is_7ESB_found = 0;

#define ZIGBEE_TX_BUFFER_SIZE	2
// Variable for Zigbee buffers holding data to be transmitted.
volatile unsigned char 	ZIGBEE_TX_BUFFER_POINTER = 0;
volatile far char 		ZIGBEE_TX_BUFFER[ZIGBEE_TX_BUFFER_SIZE][60] = {"\0"};
// Variable for Zigbee buffers holding recevied data.
char			USART_RX_DATA_RECEIVED = 0;
unsigned char 	USART_RX_BUFFER_POINTER = 0;
char 			USART_RX_BUFFER[USART_RX_BUFFER_SIZE] = {"\0"};

#pragma code

void ZigbeeOpen(void)
{
	unsigned int br = 0;
	
	#if !defined(CLOCK_SPEED)
		#error "No processor clock speed defined."
	#else
		#if defined(__18F87J50) && (CLOCK_SPEED == 48000000)
			    #if defined(__18CXX)
	    unsigned char c;
        #if defined(__18F14K50)
    	    //ANSELHbits.ANS11 = 0;	// Make RB5 digital so USART can use pin for Rx
            ANSELH = 0;
            #ifndef BAUDCON
                #define BAUDCON BAUDCTL
            #endif
        #endif
	
		#if defined(HARDWARE_V3) 
        TRISCbits.TRISC7=1;				// RX
        TRISCbits.TRISC6=0;				// TX
        TXSTA1 = 0x24;       	// TX enable BRGH=1
        RCSTA1 = 0x90;       	// Single Character RX
        SPBRG1 = 0x71;
        SPBRGH1 = 0x02;      	// 0x0271 for 48MHz -> 19200 baud
        BAUDCON1 = 0x08;     	// BRG16 = 1
		
		c = RCREG;				// read 
        
        //Enable USART RX interrupt.
        PIE1bits.RCIE = 1;
        INTCONbits.GIEH = 1;

		#elif defined(HARDWARE_V4)
    	TRISGbits.TRISG1 = 0;	//TX ->
    	TRISGbits.TRISG2 = 1;	//RX <-
        ZB_DTR_TRIS = 0;	// sleep mode
        ZB_DTR = 0;
        //ZB_RESET_TRIS = 0;	// reset
        //ZB_RESET = 1;
        ZB_CTS_TRIS = 0;
        ZB_CTS = 0;
        ZB_RTS_TRIS = 0;
        ZB_RTS = 0;
        ZB_STA_TRIS = 1;
        
        TXSTA2 = 0x24;       	// TX enable BRGH=1
        RCSTA2 = 0x90;       	// Single Character RX
        SPBRG2 = 0x71;
        SPBRGH2 = 0x02;      	// 0x0271 for 48MHz -> 19200 baud
        BAUDCON2 = 0x08;     	// BRG16 = 1        
        c = RCREG;				// read 
        
        //Enable USART RX interrupt.
        ZIGBEE_RX_INTERRUPT_EN = 1;
        INTCONbits.GIEH = 1;
        
        ZB_DTR_TRIS = 0;	//Control Sleep mode
		ZB_DTR = 0;
		#endif

    #endif
			/*
			baud1USART(BAUD_IDLE_CLK_HIGH &
            	BAUD_16_BIT_RATE &
            	BAUD_WAKEUP_OFF &
            	BAUD_AUTO_OFF);
			Open1USART(
				USART_TX_INT_OFF
				& USART_RX_INT_ON
				& USART_ASYNCH_MODE
				& USART_EIGHT_BIT
				& USART_CONT_RX
				& USART_BRGH_HIGH,
				0x0271);  // 77);
			*/
		#elif defined(__18F2455) && (CLOCK_SPEED == 48000000)
			OpenUSART(
				USART_TX_INT_OFF
				& USART_RX_INT_OFF
				& USART_ASYNCH_MODE
				& USART_EIGHT_BIT
				& USART_CONT_RX
				& USART_BRGH_LOW,
				77);
		#elif defined(__18F26K20) && (CLOCK_SPEED == 32000000)
			OpenUSART(
				USART_TX_INT_OFF
				& USART_RX_INT_OFF
				& USART_ASYNCH_MODE
				& USART_EIGHT_BIT
				& USART_CONT_RX
				& USART_BRGH_HIGH,
				207);
		#else
			#error "No supported controller defined."
		#endif
	#endif
	
	// Set up Auto Baud Rate.
//	BAUDCON1bits.ABDEN = 1;
//	Delay10KTCYx(0);
//	Delay10KTCYx(0);
//	Zigbee_WriteByte(0x55);
//	while( BAUDCON1bits.ABDEN );
	
	INTCONbits.GIEL = 1;
	INTCONbits.GIEH = 1;
}

void ZigbeeClose(void)
{
	#if defined(__18F87J50)
		#if defined(HARDWARE_V3)
			Close1USART();
		#elif defined(HARDWARE_V4)
			Close2USART();
		#else
			#error Not defined
		#endif
	#elif defined(__18F2455) || defined(__18F26K20)
		CloseUSART();
	#else
		#error "No supported controller defined."
	#endif
}

void ZigbeeSendStringPGM(rom char * str)
{
	char s[40];
	
	strcpypgm2ram(s, str);
	ZigbeeSendString(s);
}

// Do not allow broadcast address to be used because it causes slowness in data transmission.
//<param value="data">This contain the message to send. Note: the zigbee headers will be automatically generated.</param>
void ZigbeeTransmitRequest(unsigned int data_length, char * data)
{
	unsigned char i = 0;
	// 2011-12-06 Liz added. The Modbus reply will be broadcasted since the TCP Bridge is used in system.
	//char s[40] = {0x10, 0x01, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xfe, 0, 0};
	char s[40] = {0x10, 0x01, 0, 0, 0, 0, 0, 0, 0xff, 0xff, 0xff, 0xfe, 0, 0};
	//


	for( i=0; i<data_length; i++ )
	{
		s[14+i] = data[i];
	}	
					
	ZigbeeAPISendString(14+data_length, s);
}	

// Four bytes of data are general headers and applies for every packets that will be sent.
// The following four bytes will be populated by this function.
// First byte - start byte denoted by 0x7E.
// Second and third byte - size of the entire packet less these 4 bytes.
// Last byte - the checksum.
// The first byte within the data packet is the message type.
void ZigbeeAPISendString(unsigned int data_length, char * data)  //, BOOL Wait_For_Response)
{
	unsigned char checksum = 0;
	unsigned long i = 0;

	Zigbee_WriteByte(0x7E);
	Zigbee_WriteByte(data_length & 0xFF00);
	Zigbee_WriteByte(data_length & 0x00FF);
	for( i=0; i<data_length; i++ )
	{
		Zigbee_WriteByte(data[i]);
		checksum += data[i];
	}

	Zigbee_WriteByte(0xFF-checksum);
	
	#if defined(HARDWARE_V4)
	{
		unsigned int count = 0;
		LED2_IO = 1;
		while(count++ < 10000);
		LED2_IO = 0;
	}	
	#endif
}

void ZigbeeSendString(char * str)
{
	while(*str)
	{
		Zigbee_WriteByte(*str);
		*str++;	
	}
	Zigbee_WriteByte(13);
}

// Gets basic information regarding the zigbee node.
void ZigbeeSetup(void)
{
	// Get serial number - high-order.
	{	
		char s[8] = {0x08, 0x01, 'S', 'H'};
		// Reset receive buffers.
		USART_RX_BUFFER_POINTER = USART_RX_BUFFER[0] = 0;
		ZigbeeAPISendString(4, s);
		{ unsigned long i=0;  while( !ZIGBEE_RX_INTERRUPT_FLAG && i++ < 400000 ); }
		ZigbeeUnloadData();
		ProcessIncomingMessage();
	}
	// Get serial number - low-order.
	{
		char s[8] = {0x08, 0x01, 'S', 'L'};
		// Reset receive buffers.
		USART_RX_BUFFER_POINTER = USART_RX_BUFFER[0] = 0;
		ZigbeeAPISendString(4, s);
		{ unsigned long i=0;  while( !ZIGBEE_RX_INTERRUPT_FLAG && i++ < 400000 ); }
		ZigbeeUnloadData();
		ProcessIncomingMessage();
	}
	// Get firmware version.
	// Get network address.
	{
		char s[8] = {0x08, 0x01, 'M', 'Y'};
		// Reset receive buffers.
		USART_RX_BUFFER_POINTER = USART_RX_BUFFER[0] = 0;
		ZigbeeAPISendString(4, s);
		{ unsigned long i=0;  while( !ZIGBEE_RX_INTERRUPT_FLAG && i++ < 400000 ); }
		ZigbeeUnloadData();
		ProcessIncomingMessage();
	}
}
	
void ZigbeeAPI_ATVR(void)
{
	char s[8] = {0x08, 0x01, 'V', 'R'};
	//char s[8] = {0x08, 0x01, 0x56, 0x52};

	// Reset receive buffers.
	USART_RX_BUFFER_POINTER = USART_RX_BUFFER[0] = 0;

	ZigbeeAPISendString(4, s);
	{
		unsigned long i=0;  // , j=0;
		while( !ZIGBEE_RX_INTERRUPT_FLAG && i++ < 400000 );

		ProcessIncomingMessage();
	}		
}

void ZigbeeAPIDiscoverNodes(void)
{
	{
		int i=0;
		for( i=0; i<10; i++ )
			node_list[i][0] = 0;
	}
	{  // Reset receive buffers.
		USART_RX_BUFFER_POINTER = USART_RX_BUFFER[0] = 0;
	}
	{
		char s[8] = {0x08, 0x01, 0x4E, 0x44};  //ND.
		ZigbeeAPISendString(4, s);
	}
	{
		unsigned long i=0, j=0;
		while( !ZIGBEE_RX_INTERRUPT_FLAG && i++ < 400000 );

		j=0;
		ProcessIncomingMessage();
	}		
}

unsigned char ProcessOutgoingMessage(void)
{
	// Go through the list of TX buffer and increment the retry byte.
	int i = 0;
	for( i=0; i<ZIGBEE_TX_BUFFER_SIZE; i++ )
	{
		// Ignore if retry byte is 0;
		if( ZIGBEE_TX_BUFFER[i][0] == 0 ) continue;
		ZIGBEE_TX_BUFFER[i][0]++;
		// If retry byte reaches 0x0F, resend the message.
		if( ZIGBEE_TX_BUFFER[i][0] >= 0x0F )
		{
			ZigbeeAPISendString(ZIGBEE_TX_BUFFER[i][1], &ZIGBEE_TX_BUFFER[i][2]);
			ZIGBEE_TX_BUFFER[i][0] = 0;
		}	
	}	
}

void ZigbeeUnloadData(void)
{
	char c1;

	while( ZIGBEE_RX_INTERRUPT_FLAG )
	{
		c1 = Zigbee_getcUSART();
		
		if(!is_7ESB_found && c1 == 0x7E)
		{
			USART_RX_BUFFER[0] = USART_RX_BUFFER_POINTER = 0;
			is_7ESB_found = 1;
		}	
		
		if( USART_RX_BUFFER_POINTER < USART_RX_BUFFER_SIZE-1)
			USART_RX_BUFFER[USART_RX_BUFFER_POINTER++] = c1;
	}
	Receiving_USART = 0;
}
	

//
#if defined( COMMUNICATOR_BOARD )
// Returns identifier of received message.
//2012-08-21 Liz: changed ProcessIncomingMessage() from "unsigned char" to "char" 
char ProcessIncomingMessage(void)
{
	// 2012-08-21 Liz: Removed
	//if( !ZIGBEE_RX_INTERRUPT_FLAG && USART_RX_BUFFER_POINTER == 0 ) return -1;
	//
	
	if( USART_RX_BUFFER[0] != 0x7E ) 
	{
		USART_RX_BUFFER[0] = USART_RX_BUFFER_POINTER = 0;
		is_7ESB_found = 0;
		// 2012-08-21 Liz: need to clear data_length
		USART_RX_BUFFER[1] = USART_RX_BUFFER[2] = 0;
		return -1;
	}
	
	if( USART_RX_BUFFER_POINTER < 5 ) return -2;	
	
	// Process the messages in the buffer.
	{
		char identifier;
		unsigned char checksum = 0;
		unsigned int data_length = 0, i = 0;
						
		// Get expected length of data we should receive.
		data_length |= USART_RX_BUFFER[1];
		data_length <<= 8;
		data_length |= USART_RX_BUFFER[2];
		
		//2012-07-16 Liz: Changed condition since the previous condition can make process stuck 
		//	inside next loop forever if data_length in range of [56,60]
		// Check if we received correct length of data.
		if( data_length >= (USART_RX_BUFFER_SIZE - 4))
		{
			USART_RX_BUFFER[0] = USART_RX_BUFFER_POINTER = 0;
			is_7ESB_found = 0;
			// 2012-08-21 Liz: need to clear data_length
			USART_RX_BUFFER[1] = USART_RX_BUFFER[2] = 0;
			return -3;
		}
		
		// Check if we received this length of data.
		if( data_length > (USART_RX_BUFFER_POINTER-4) ) return -4;
		
		// 2012-08-21 Liz: added verify checksum
		for( i=3; i<data_length+3; i++ )
		{
			// 2012-05-12(Eric) - As MCU_RX_BUFFER is defined as char type and checksum a unsigned type,
			// using the operator += could cause undesirable result. Eg. checksum(1000) += MCU_RX_BUFFER(-20).
			checksum += USART_RX_BUFFER[i];
		}
		
		// Calculate final checksum
		checksum = 0xFF-checksum;
		
		if( USART_RX_BUFFER[i] != checksum ) 
		{
			USART_RX_BUFFER[0] = USART_RX_BUFFER_POINTER = 0; 
			USART_RX_BUFFER[1] = USART_RX_BUFFER[2] = 0;
			is_7ESB_found = 0;
			USART_RX_BUFFER[data_length+3] = 0;	// Remove checksum byte.
			return;
		}
		
		// Remove checksum byte.
		USART_RX_BUFFER[data_length+3] = 0;	
		//
		
		identifier = USART_RX_BUFFER[3];
		
		// Assume data received. If none of the identifier matches within the switch statement,
		// USART_RX_DATA_RECEIVED will be set to 0;
		USART_RX_DATA_RECEIVED = 1;  
		
		#if defined(HARDWARE_V4)		
		{
			unsigned int count = 0;
			LED1_IO = 1;
			while(count++ < 10000);
			LED1_IO = 0;
		}
		#endif

		switch( identifier )  // This switch statement filters through the Zigbee API Frame names.
		{
			case 0x8B:  // Zigbee Transmit Status.
			{
				char frameid = USART_RX_BUFFER[4];
				char status = USART_RX_BUFFER[8];
				ZIGBEE_TX_BUFFER[frameid-1][0] = 0;  // Set retry count to 0 to stop retry checks.
				break;
			}
			case 0x90:  // Zigbee Receive Packet.
			{
				char i=0;
				char src[12] = "", data[32] = "";

				// Get serial number and network address of the source node.
				for( i=0; i<10; i++ )
					src[i] = USART_RX_BUFFER[4+i];
				
				// Extract the data from this packet.
				for( i=0; i<data_length-12; i++ )
					data[i] = USART_RX_BUFFER[15+i];
				data[data_length-12] = 0;
		
				{  // Process the data.
					char msg_type = data[0];
					switch( msg_type )
					{
						case 0xA0:  // Modbus request from the coordinator.
						{
							#if defined(APP_USE_MODBUS_CLIENT) && !defined(COORDINATOR)
	    					MAC_ADDR mac_addr;
	    					WORD reg;
							char req_data[20]="";
	    					
	    					for( i=0; i<6; i++ )
	    						mac_addr.v[i] = data[i+1];
	    					
	    					((BYTE*)&reg)[0] = (BYTE)data[9];
	    					((BYTE*)&reg)[1] = (BYTE)data[8];
	    					for (i=0;i<20;i++)
								req_data[i]=data[11+i];
	    					if( CreateNewModbusRequest(mac_addr, data[7], reg, data[10],req_data) == 1) {}
	    					else
	    					{
	    						// The requested devices is not set up to this communicator.
	    					}

							// Clear the buffer after we have successfully received the message we want.
							USART_RX_DATA_RECEIVED = 0;
							USART_RX_BUFFER[0] = USART_RX_BUFFER_POINTER = 0;
							#endif
							break;
						}
						case 0x30:	// Get frequency of telnet
						{
							// Set time delay for telnet
							telnet_timer = data[1];
								
							// Save value to eeprom
							XEEBeginWrite(POWER_SAVE_END + 20);
    						XEEWrite(0x30);
    						XEEWrite(data[1]);	 
    						XEEEndWrite();

							break;	
						}
						case 0x40:	// Get chosen parameters to be read from bottom board
						{
							//char i = 0;
							//for(i=0; i<8; i++)
							//	ZIGBEE_READINGS_REQUEST[i] = data[i+1];

							break;
						}	
						case 0x50:	// Get setting dateTime from server.
						{
							char i = 0;
							for(i=0; i<12; i++)
								DATETIME[i] = data[i+1];
							Is_DateTime_Modified = 1;

							break;
						}
						default:
						{
							if( !USB_BUS_SENSE )
							{
								USART_RX_DATA_RECEIVED = 0;
								USART_RX_BUFFER[0] = USART_RX_BUFFER_POINTER = 0;
								// 2012-08-21 Liz: need to clear data_length
								USART_RX_BUFFER[1] = USART_RX_BUFFER[2] = 0;
							}	
						}	
					}	
				}
				
				i=0;
				break;
			}
			case 0x88:  // Zibgee Command Response. 
			{
				if( USART_RX_BUFFER[5] == 'S' && USART_RX_BUFFER[6] == 'H' )  // High-order byte of the serial number.
				{
					SERIAL_NUMBER[0] = USART_RX_BUFFER[8];
					SERIAL_NUMBER[1] = USART_RX_BUFFER[9];
					SERIAL_NUMBER[2] = USART_RX_BUFFER[10];
					SERIAL_NUMBER[3] = USART_RX_BUFFER[11];
				}	
				else if( USART_RX_BUFFER[5] == 'S' && USART_RX_BUFFER[6] == 'L' )  // Low-order byte of the serial number.
				{
					SERIAL_NUMBER[4] = USART_RX_BUFFER[8];
					SERIAL_NUMBER[5] = USART_RX_BUFFER[9];
					SERIAL_NUMBER[6] = USART_RX_BUFFER[10];
					SERIAL_NUMBER[7] = USART_RX_BUFFER[11];
				}	
				else if( USART_RX_BUFFER[5] == 'M' && USART_RX_BUFFER[6] == 'Y' )  // Firmware version.
				{
					NETWORK_ADDRESS[0] = USART_RX_BUFFER[8];
					NETWORK_ADDRESS[1] = USART_RX_BUFFER[9];
				}	
				else if( USART_RX_BUFFER[5] == 'V' && USART_RX_BUFFER[6] == 'R' )  // Firmware version.
				{
					FIRMWARE_VERSION[0] = USART_RX_BUFFER[8];
					FIRMWARE_VERSION[1] = USART_RX_BUFFER[9];
				}	
				else if( USART_RX_BUFFER[5] == 'N' && USART_RX_BUFFER[6] == 'D' )  // Network Discover.
				{
					// Code runs here only if there are other nodes in the network.
					// If code does not run here while doing network discover,
					// make sure there are nodes connected to the same network.
					char i, j;
					for( i=0; i<10; i++ )
					{
						if( node_list[i][0] == 0 )
						{
							node_list[i][0] = '#';
							for( j=0; j<10; j++ )
								node_list[i][j+1] = USART_RX_BUFFER[8+j];
							break;
						}	
					}	
				}	
				break;
			}	
			default:
			{
				if( !USB_BUS_SENSE )
				{
					USART_RX_DATA_RECEIVED = 0;
					USART_RX_BUFFER[0] = USART_RX_BUFFER_POINTER = 0;
					is_7ESB_found = 0;
					// 2012-08-21 Liz: need to clear data_length
					USART_RX_BUFFER[1] = USART_RX_BUFFER[2] = 0;
					break;
				}	
			}	
		}

		
		#if !defined(COORDINATOR)
			{
				USART_RX_DATA_RECEIVED = 0;
				USART_RX_BUFFER[0] = USART_RX_BUFFER_POINTER = 0;
				is_7ESB_found = 0;
				// 2012-08-21 Liz: need to clear data_length
				USART_RX_BUFFER[1] = USART_RX_BUFFER[2] = 0;
			}	
		#endif
		//ProcessOutgoingMessage();
		return identifier;
	}
	return 1;	
}

#endif

void ZigbeeTasks(void)
{
	if( ZIGBEE_TASKS != 0 )
	{
		if( ZIGBEE_TASKS & ZIGBEE_TASKS_RUN_ATVR )
		{
			INTCONbits.GIEH = 0;
			ZigbeeAPI_ATVR();
			INTCONbits.GIEH = 1;
		
			ZIGBEE_TASKS &= 0b11111101;  // Clear the flag.
		}	
		else if( ZIGBEE_TASKS & ZIGBEE_TASKS_RUN_ATND )
		{
			INTCONbits.GIEH = 0;
			ZigbeeAPIDiscoverNodes();
			INTCONbits.GIEH = 1;
		
			ZIGBEE_TASKS &= 0b11111110;  // Clear the flag.
		}
	}
	
	// Ping.
	/*
	if( NETWORK_ADDRESS[0] != 0 && NETWORK_ADDRESS[1] != 0 )
	{
		 This block of text sends the phase "A4" over Zigbee.
		static unsigned char co = 0;
		if( co++ > 4 )
		{
			char so[20] = {0x10, 0x01, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xfe, 0, 0, 0x50, 0x49, 0x4E, 0x47};  // , 0, 0, 0, 0, 0, 0, 8};  // , 0x64};
			ZigbeeAPISendString(18, so);
			co = 0;
		}
	}
	*/	
			
	ProcessIncomingMessage();
}	

//***********************************/
//**** Private Functions.************/
//***********************************/
char Zigbee_getcUSART(void)
{
	char c = 0;
	
	#if defined(HARDWARE_V3)
	if( RCSTA1bits.OERR )  // Overrun error.
	{
		c = 0;
		RCSTA1bits.CREN = 0;
		c = ZIGBEE_READ;
		RCSTA1bits.CREN = 1;
	}
	if( RCSTA1bits.FERR )  // Framing error.
	{
		// Try to close and reopen the USART.	
		c = ZIGBEE_READ;
	}	
	else c = ZIGBEE_READ;
	#elif defined(HARDWARE_V4)
	if( RCSTA2bits.OERR )  // Overrun error.
	{
		c = 0;
		RCSTA2bits.CREN = 0;
		c = ZIGBEE_READ;
		RCSTA2bits.CREN = 1;
	}
	if( RCSTA2bits.FERR )  // Framing error.
	{
		// Try to close and reopen the USART.
		c = ZIGBEE_READ;	// 2012-08-21 Liz: continue to read next byte to clear error flag.	
	}	
	else c = ZIGBEE_READ;
	#else
		#error Not defined
	#endif

	return c;
}	

void Zigbee_WriteByte(char c)
{
	#if defined(__18F87J50)
		#if defined(HARDWARE_V3)
			Write1USART(c);
		#elif defined(HARDWARE_V4)
			Write2USART(c);
		#else
			#error Not defined
		#endif
	#elif defined(__18F2455) || defined(__18F26K20)
		WriteUSART(c);
	#else
		#error "Processor not supported."
	#endif

	Delay1KTCYx(40);
}

//#endif

