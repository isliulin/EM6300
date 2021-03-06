//zigbee.h

//#include "AppConfig.h"

//#ifdef APP_USE_ZIGBEE

#ifndef __ZIGBEE_H
#define __ZIGBEE_H

void ZigbeeOpen(void);
void ZigbeeSetup(void);
void ZigbeeUnloadData(void);
void ZigbeeDiscoverNodes(void);
void ZigbeeTransmitRequest(unsigned int data_length, char * data);
void ZigbeeAPISendStringPGM(char identifier, unsigned int data_length, rom char * data);
void ZigbeeAPISendString(unsigned int data_length, char * data);  //, BOOL Wait_For_Response);
void ZigbeeSendStringPGM(rom char * str);
void ZigbeeSendString(char * str);
void ZigbeeTasks(void);
void Zigbee_WriteByte(char c);
char ProcessIncomingMessage(void);	//2012-08-21 Liz: changed ProcessIncomingMessage() from "unsigned char" to "char" 
void ZigbeeClose(void);


extern char PENDING_REQUESTS_POINTER;
extern char telnet_timer;
extern char DATETIME[13];
extern char Is_DateTime_Modified;
//************************
// Define pinout.
//************************

#if defined(HARDWARE_V3)
    #if defined(__18F87J50)
	    #define ZIGBEE_RX_INTERRUPT_FLAG		PIR1bits.RC1IF
		#define ZIGBEE_RX_INTERRUPT_PRIORITY	IPR1bits.RC1IP
	//	#define	ZIGBEE_HIGH_BAUDRATE			TXSTAbits.BRGH
		#define ZIGBEE_AUTO_BAUDRATE			BAUDCON1bits.ABDEN
		#define ZIGBEE_16BIT_BAUDRATE			BAUDCON1bits.BRG16
	#endif
#endif


//******************************************************************
// Command code definition.
// Commands received from the server
// will be an integer value and mapped to one of the below.
// Commands with MSB set are commands received from the server.
// Commands with MSB cleared are commands sent to the server.
//******************************************************************
#define		ZB_CMD_SETDATETIME			0x40
#define		ZB_CMD_START_HISPEED		0x40+1
#define		ZB_NODE_REGISTERED			0x40+2
#define		ZB_REQUEST_NODE_JOIN		0x40+3		//Server received ping or data packets from this node.
													//However server has not received registration info from this node
													//or node was removed from server due to timeout.
													//Server is now requesting node to resend registration info.
#define		ZB_SEND_BEEP				0x40+4
//#define		ZB_DISCOVER_DEVICES			0x40+4

#if defined(HARDWARE_V3)
	#define USART_RX_EN						RCSTA1bits.CREN
	#define ZIGBEE_RX_INTERRUPT_EN			PIE1bits.RC1IE
	//#define ZIGBEE_RX_INTERRUPT_FLAG		PIR1bits.RC1IF
	//#define ZIGBEE_RX_INTERRUPT_PRIORITY	IPR1bits.RC1IP
	//#define	ZIGBEE_HIGH_BAUDRATE			TXSTAbits.BRGH
	//#define ZIGBEE_AUTO_BAUDRATE			BAUDCON1bits.ABDEN
	//#define ZIGBEE_16BIT_BAUDRATE			BAUDCON1bits.BRG16
	#ifdef __18F87J50
		#define ZIGBEE_READ						Read1USART()
		#define ZIGBEE_DATARDY					DataRdy1USART()
		#define USART_OVERRUN_ERROR				RCSTA1bits.OERR
	#elif defined( __18F2455 )
		#define ZIGBEE_READ						ReadUSART()
		#define ZIGBEE_DATARDY					DataRdyUSART()
		#define USART_OVERRUN_ERROR				RCSTA1bits.OERR
	#else
		#error "Processor not supported."
	#endif
#elif defined(HARDWARE_V4)
	#define USART_RX_EN						RCSTA2bits.CREN
	#define ZIGBEE_RX_INTERRUPT_EN			PIE3bits.RC2IE
	#define ZIGBEE_RX_INTERRUPT_FLAG		PIR3bits.RC2IF
	#define ZIGBEE_RX_INTERRUPT_PRIORITY	IPR3bits.RC2IP
//	#define	ZIGBEE_HIGH_BAUDRATE			TXSTAbits.BRGH
	#define ZIGBEE_AUTO_BAUDRATE			BAUDCON2bits.ABDEN
	#define ZIGBEE_16BIT_BAUDRATE			BAUDCON2bits.BRG16
	
	#ifdef __18F87J50
		#define ZIGBEE_READ						Read2USART()
		#define ZIGBEE_DATARDY					DataRdy2USART()
		#define USART_OVERRUN_ERROR				RCSTA2bits.OERR
	#elif defined( __18F2455 )
		#define ZIGBEE_READ						ReadUSART()
		#define ZIGBEE_DATARDY					DataRdyUSART()
		#define USART_OVERRUN_ERROR				RCSTA1bits.OERR
	#else
		#error "Processor not supported."
	#endif
#endif

typedef enum					//0x1X for Zigbee type errors.
{
	RX_PARITY_ERROR					= 0x11,
	RX_INVALID_DESTINATION_ADDRESS	= 0x12,
	RX_INVALID_COMMAND_CODE			= 0x13,
	RX_INVALID_DATA_SIZE			= 0x14,
	RX_INVALID_START_MARKER			= 0x15
} ZB_ERROR_CODE;

typedef enum
{
	ZB_CMD_REGNODE				= 1,
	ZB_CMD_POWERREADING			= 2,
	ZB_CMD_DEVICE_PING			= 3,				//This is for inter-device ping. Packets sent are smaller.
	ZB_CMD_POWERMREADING		= 4,
	ZB_MSG						= 5,
	ZB_ERROR					= 6,
	ZB_NETROUTE					= 7,
	ZB_CMD_SERVER_PING			= 8,				//This is for device-server ping. Packets are larger with timestamp.
	ZB_DISCOVER_DEVICES 		= 14,				//When this command is received, nodes immediately send a inter-device ping.
	ZB_NUDGE					= 15,
	ZB_FORCE_RESET				= 16
} ZB_MESSAGE_TYPE;


//*************************
// Declare variables.
//*************************
#define ZB_WRITE_INCLUDE_PREDELAY			0b11111011
#define ZB_WRITE_INCLUDE_PREDELAY_MASK		~ZB_WRITE_INCLUDE_PREDELAY
#define ZB_WRITE_SEND_CR					0b11111110
#define ZB_WRITE_SEND_CR_MASK				~ZB_WRITE_SEND_CR
#define ZB_WRITE_WAIT_RESPONSE				0b11111101
#define ZB_WRITE_WAIT_RESPONSE_MASK			~ZB_WRITE_WAIT_RESPONSE


#define BAUDRATE_9600						3
#define BAUDRATE_19200						4
#define BAUDRATE_38400						5
#define BAUDRATE_57600						6
#define BAUDRATE_115200						7

#define IS_RUNNING_HIGH_SPEED			OSCTUNEbits.PLLEN

//extern char INCOMING_BUFFER[60];
extern char ZIGBEE_TASKS;
extern char node_list[10][12];

#define ZIGBEE_TASKS_RUN_ATND	0b00000001
#define ZIGBEE_TASKS_RUN_ATVR	0b00000010

//extern char FIRMWARE_VERSION[2];
//extern char 					NETWORK_ADDRESS[5];
extern char 					DEVICE_TYPE[2];
extern char 					SERIAL_NUMBER[8];
extern char		 				PARENT_ADDRESS[32];

extern char						IS_REGISTERED_TO_SERVER;
extern unsigned char 			LOCK_RX_BUFFER;
extern unsigned char			RECEIVED_SERVER_NUDGE;


extern volatile unsigned char HI_SPEED_INTERVAL;
extern volatile unsigned long PING_INTERVAL;

#define PARENT_TIMEOUT			200
//extern volatile unsigned char USART_RX_BUFFER_POINTER;
//extern volatile char USART_RX_BUFFER[USART_RX_BUFFER_SIZE];


//*************************
// Function declaration.
//*************************
//#define 	Zigbee_Nudge()						Zigbee_SendMessageToDevice(ZB_NUDGE, 0, NETWORK_ADDRESS);
#define		Zigbee_DiscoverDevices()			Zigbee_SendMessageToDevice(ZB_DISCOVER_DEVICES, 0, NETWORK_ADDRESS);

void zigbee_high_isr(void);

//void Zigbee_Open(unsigned char baud_rate, unsigned char is_running_high_speed);
//void Zigbee_Close(void);
//void Zigbee_Reset(void);
//unsigned char Zigbee_EnterCommandMode(void);
//int Zigbee_CheckConnection(void);
//void Zigbee_LeavePAN(void);
//int Zigbee_SetDestAddressL(char * address, int send_broadcast);
//int Zigbee_UpdateNetworkInfo(char * serial_number, char * network_address, char * parent_address, char * device_type);
//void Zigbee_FlushBuffer(void);
//unsigned int Zigbee_BroadcastString_1(char command_code, char * msg, int chars_to_send, rom char * destination_address);
//unsigned int Zigbee_BroadcastString_2(char command_code, char * msg, int chars_to_send);
//unsigned int Zigbee_BroadcastStringPGM(ZB_MESSAGE_TYPE command_code, rom char * msg, int chars_to_send);
//unsigned int Zigbee_WriteString(char * msg, unsigned char write_config, char * response, unsigned char buffer_size);
//unsigned int Zigbee_WriteStringPGM(far char rom * msg, unsigned char write_config, char * response, unsigned char buffer_size);
//void Zigbee_SendMessageToDevice(ZB_MESSAGE_TYPE command_code, char * destination_address, char * msg);
////void Zigbee_SendMessageToDevicePGM_RAM(ZB_MESSAGE_TYPE command_code, rom char * destination_address, char * msg);
////void Zigbee_SendMessageToDeviceRAM_PGM(ZB_MESSAGE_TYPE command_code, char * destination_address, rom char * msg);
//unsigned int Zigbee_ReceiveBytes(char * result, unsigned char buffer_size);
//void ResetDevice(void);
//int IsValidCommandCode(char code);
//void Zigbee_ProcessIncomingMessage(void);
//char Zigbee_getcUSART(void);
//void Zigbee_WriteByte_ShortDelay(char c);
//void Zigbee_WriteByte(char c);

#endif


//#endif

