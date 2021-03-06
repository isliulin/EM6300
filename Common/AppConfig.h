// AppConfig.h

#ifndef __APPCONFIG_H
#define __APPCONFIG_H

#include "HardwareProfile.h"

#if defined(COMMUNICATOR_BOARD) || defined(METER_TOP_BOARD)
	#include "TCPIP Stack/TCPIP.h"
#endif

// Lists all the modules available.
// Comment the line to exclude the module.
// Uncomment the line to use the module.

#if defined(COMMUNICATOR_BOARD)
	#define				APP_COMM_USE_MODBUS_CLIENT
	#define				APP_USE_USB
	#define				APP_USE_ZIGBEE
	#if !defined(COORDINATOR)
		#define APP_USE_HUB
	#endif
#elif defined( METER_TOP_BOARD )
	#define				APP_METER_USE_MODBUS_SERVER
//	#define				APP_METER_USE_MODBUS_CLIENT
//	Liz 2012-09-27
//	#define				USE_25LC512
	#define				USE_25LC1024
	#define				APP_USE_LCD
	#define				APP_USE_MENU
	#define				APP_USE_MCU
	#define				APP_USE_BUZZER
#elif defined( SINGLE_PHASE_BOTTOM_BOARD ) 
	#define				APP_USE_DEVCONFIG
	#define				APP_USE_MCU
	#define				APP_USE_RTCC
	#define				APP_USE_MDD
	//#define				APP_USE_ADC
#elif defined( THREE_PHASE_BOTTOM_BOARD ) 
	#define				APP_USE_DEVCONFIG
	#define				APP_USE_MCU
	#define				APP_USE_RTCC
//	#define				APP_USE_NEGATIVE_ENERGY		// 2014-05-08 Liz added
	//#define				APP_USE_ADC
#else
	#error "No board defined."
#endif

/******************************************/
/*********** Variables ********************/
/******************************************/
#define USART_RX_BUFFER_SIZE	60
//extern char 			Receiving_USART;
extern char				USART_RX_DATA_RECEIVED;
extern unsigned char 	USART_RX_BUFFER_POINTER;
extern char 			USART_RX_BUFFER[USART_RX_BUFFER_SIZE];

/*******************************************/
/********* S T R U C T U R E S *************/
typedef struct __attribute__((__packed__))
{
	unsigned int		VOLTCC;
	unsigned int		AMPCC;
	unsigned int		ENRCC;
	unsigned int		PWRCC;
	unsigned int		VOLT_GAIN_A;
	unsigned int		CURRENT_GAIN_A;

// 2012-05-03(Eric) - CALIBRATION_VALUES should be a fixed length regardless of the bottom board used.
// This is because METER_TOP_BOARD is using this structure.

//	#if defined( THREE_PHASE_BOTTOM_BOARD )
	unsigned int		VOLT_GAIN_B;
	unsigned int		CURRENT_GAIN_B;
	unsigned int		VOLT_GAIN_C;
	unsigned int		CURRENT_GAIN_C;	
//	#endif
	
	unsigned int		AUX_CONFIG;
	unsigned int		CT_RANGE;
	WORD				EXT_VOLT_RATIO;
	
	// 2012-05-03(Eric) - Something to combat concurrency issue since we can modify
	// this structure by various means - webpage, menu and modbus commands.
	WORD				LAST_MODIFIED;  // Each save to this structure will increment by 1.
	WORD				CHECK_SUM;		// 2012-10-02
	
	struct
	{
		unsigned char bBoardReset : 1;
		unsigned char bPhaseA_RealPos_C2_Overflow : 1;
		unsigned char bPhaseB_RealPos_C2_Overflow : 1;
		unsigned char bPhaseC_RealPos_C2_Overflow : 1;
		unsigned char bEnergyReset : 1;
		unsigned char bChecksumChanged : 1;
		unsigned char bComputeWithNegativeEnergy : 1;
		unsigned char bIsModified : 1;
	} Flags;
	
	struct
	{
		BYTE : 5;
		unsigned char bIsGetDatetime : 1;//set to 1 if get date time from top board
		unsigned char bTOReset : 1;
		unsigned char bAutoReset : 1;
		//unsigned char bIsDateTimeUpdated: 1;
	} Flag2;  // 2012-05-03(Eric) - Created to keep the entire structure WORD aligned.
	
} CALIBRATION_VALUES;

#if defined(METER_TOP_BOARD)
	extern char Credit_Balance[17];
	extern char strIPAddress[16];
	extern char strMACAddress[16];
	extern SOCKET_INFO CommInfo;
	extern BOOL EEP_RECORD_FLAG;			// 2014-01-16 Liz.
	extern BOOL UPDATE_DATETIME;			// 2014-01-21 Liz.
	extern char DateTime[6];			// 2014-01-21 Liz.
	extern long EEPROM_INTERVAL;	// 2013-09-26 Liz added
	extern long LED_INTERVAL;	// 2014-04-01 LIZ added
#endif // #if defined(METER_TOP_BOARD)


#if defined (COMMUNICATOR_BOARD)
	extern char SERIAL_NUMBER[8];
#endif

//2012-07-25 Liz: Added max demand refresh interval
#if defined (SINGLE_PHASE_BOTTOM_BOARD) || defined (THREE_PHASE_BOTTOM_BOARD)
	extern long 	REFRESH_INTERVAL;
	extern CALIBRATION_VALUES	CalibrationData;
#endif
/******************************************/
/*********** Structures *******************/
/******************************************/

#if defined (SINGLE_PHASE_BOTTOM_BOARD) || defined (THREE_PHASE_BOTTOM_BOARD)
	// ENR_OVF_COUNTER is the block of variables which stores the energy overflow counters.
	typedef struct __attribute__((__packed__))
	{
		unsigned int		PHASE_A_REAL_POS;
		unsigned int		PHASE_A_REAL_NEG;	
		unsigned int		PHASE_A_REACTIVE_POS;
		unsigned int		PHASE_A_REACTIVE_NEG;
		unsigned int		PHASE_A_APPARENT;
	
		#if defined( THREE_PHASE_BOTTOM_BOARD )
			unsigned int		PHASE_B_REAL_POS;
			unsigned int		PHASE_B_REAL_NEG;
			unsigned int		PHASE_B_REACTIVE_POS;
			unsigned int		PHASE_B_REACTIVE_NEG;
			unsigned int		PHASE_B_APPARENT;
			
			unsigned int		PHASE_C_REAL_POS;
			unsigned int		PHASE_C_REAL_NEG;
			unsigned int		PHASE_C_REACTIVE_POS;
			unsigned int		PHASE_C_REACTIVE_NEG;
			unsigned int		PHASE_C_APPARENT;
		#endif

		struct
		{
			unsigned char : 5;
			unsigned char bIs_SPkwh_Ready : 1;	
			unsigned char bIs_3Pkwh_Ready : 1;	//2013-02-27 (Liz) keep track if energy reading is ready
			unsigned char bIsModified : 1;
		} Flags;                          	// Flag structure
	} ENR_OVF_COUNTER;  // 2012-04-17(Eric) - Renamed. Refer to release notes.
	
#endif  // #if defined (SINGLE_PHASE_BOTTOM_BOARD) || defined (THREE_PHASE_BOTTOM_BOARD)

/*-----------------------------------------------------------------------------
	EEPROM assigned location in bottom board
		+ 0 - 119 	: Calibration + Energy Overflow Counter 1 and 2 of 3 Phases
		+ 120 - 149 : Refresh interval + Max demand of 3 Phases
		+ 150 - 159	: DateTime 
		+ 160 - 250	: Raw Energy Reading
-----------------------------------------------------------------------------*/		
/*************************************************************************************************
* DEV_CONFIG is the block of variables which stores the calibration settings for the MAXQ3180.
* This block will be saved into the EEPROM.
* The order of the items here must correspond to the order of items in POWER_CALIBRATION_QUEUE.
*/


#if defined(COMMUNICATOR_BOARD)
	typedef union
	{
		char v[102];
		
		struct
		{
			NODE_INFO meter_01;
			NODE_INFO meter_02;
			NODE_INFO meter_03;
			NODE_INFO meter_04;
			NODE_INFO meter_05;
			NODE_INFO meter_06;
			NODE_INFO meter_07;
			NODE_INFO meter_08;
			NODE_INFO meter_09;
			NODE_INFO meter_10;
		
			unsigned char repeat_interval;  //Amount of time to wait before next round of requests starts in mins.
			unsigned char availableMeters;	//Liz 2012-02-01: keep track of how many meters are connected to COMM.
			struct
			{
				unsigned char : 7;
				unsigned char bIsModified : 1;
			} Flags; 
		} Val;
			
	} CONNECTED_METERS_LIST;	
	
	extern CONNECTED_METERS_LIST iplist;
	
	typedef enum //_GenericTCPExampleState
	{
		SM_HOME = 0,
		SM_SOCKET_OBTAINED,
		SM_GET_ACK,
		SM_SEND_REQUEST,
		SM_PROCESS_RESPONSE,
		SM_DISCONNECT,
		SM_DONE
	} TELNETCLIENT_STATUS;
#endif

// 2012-09-27 Liz: added meter status flag
#if defined (METER_TOP_BOARD)
//typedef struct __attribute__((__packed__))
typedef union
{
	unsigned char v[2];
	
	struct
	{
		struct {
			unsigned char : 1;
			unsigned char bBOTTOMReset : 1;
			unsigned char bTOPReset : 1;
			unsigned char bEepromReset : 1;
			unsigned char bEnergyReset : 1;
			unsigned char bCalibCksModified : 1;
			unsigned char bDateTimeUpdated : 1;
			unsigned char bHaveEepRecords : 1;
		} Flag1;

		struct
		{
			//BYTE : 1; 2014-2-25 CL update a new flag to store Supercap voltage indicator flag
			unsigned char bBOTupdateADC : 1;
			unsigned char bBOTTOReset : 1;
			unsigned char bTOPTOReset : 1;
			unsigned char bBOTAutoReset : 1;
			unsigned char bTOPAutoReset : 1;
			unsigned char bPhaseA_RealPos_C2_Overflow : 1;
			unsigned char bPhaseB_RealPos_C2_Overflow : 1;
			unsigned char bPhaseC_RealPos_C2_Overflow : 1;
		} Flag2;  
	} Flags;
	
} METER_STATUS_FLAG;		

//extern METER_STATUS_FLAG meter_flag;
#endif
///////

#endif  // #define __APPCONFIG_H

