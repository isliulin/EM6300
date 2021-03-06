#include <delays.h>
#include <timers.h>
#include <stdlib.h>
#include <math.h>

#include "HardwareProfile.h"
#include "AppConfig.h"
#include "eep.h"
#include "FSIO.h"

#include "adc.h"			//2014-2-21 CL added for Vbatvoltage detection

#ifdef APP_USE_ADC
#include "adcon.h"
#endif
#include "eeprom.h"
#include "main.h"
#include "mcu.h"
#include "power.h"
#include "registers.h"
#include "rtcc.h"
#include "utility.h"
#ifdef APP_USE_MDD
	#include "MDD_task.h"
#endif

#if defined(__18F26K20) || defined( __18F46K20 )
	#pragma config PBADEN = OFF
	#pragma config FOSC = HSPLL
	#pragma config XINST = OFF
	#pragma config WDTEN = ON
	#pragma config WDTPS = 4096
	#pragma config LVP = OFF
	#pragma config MCLRE = OFF	// added in since the New Hardware design doesn't have pull up resistor.
	#pragma config PWRT = ON
	#pragma config LPT1OSC = ON			
//	#pragma config IESO = ON			//Enable oscilator mode switch over
	#pragma config BOREN = NOSLP		//Brown-out Reset enabled in hardware only and disabled in Sleep mode (SBOREN is disabled)
#endif
#if defined(__18F87J50)
	#pragma config PLLDIV = 2
	#pragma config FOSC = HSPLL
	#pragma config CPUDIV = OSC1
#endif
#if defined(__18F2455)
	#pragma config PLLDIV = 2
	#pragma config FOSC = HSPLL_HS
	#pragma config CPUDIV = OSC4_PLL6
	#pragma config VREGEN = OFF
	#pragma config LVP = OFF
	#pragma config USBDIV = 2
	#pragma config WDT = OFF
#endif


// Provides a way for the C# bootloader software to identify the device the hex file was compiled for.
// This will appear in the hex file as :04010000<dwDeviceID><checksum>
// where the first 2 and last 2 hex digits of dwDeviceID will always be FF.
// dwDeviceID = 0xFF0001FF is for PIC18F46K20, bottom board.
// dwDeviceID = 0xFF0010FF is for PIC18F87J50, top board.
#pragma romdata DevID = 0x100
far rom DWORD dwDeviceID = BOOTLOAD_DEVID;	// 0xF000EF02 is a goto 0x000004 instruction
#pragma romdata

//**********************************
// Function prototypes.
//**********************************
void InitDataFromEEPROM(void);
void DoReadings(void);
void ResetSPIDevices(void);
void CaptureOverflow(void);
void Update_Max_Min(void);  		// 2012-06-15 Liz: Added for MAX_MIN voltage/current/power record
void VBatVoltageDetection(void);	// 2014-2-21 CL added
//void BotUpdateTopDateTime(void);	//2014-2-26 CL added

void InitTimer(void);		// 2014-03-04 Liz added	
void InitIOPorts(void);		// 2014-03-04 Liz added	
void InitMAXQ(void);		// 2014-03-04 Liz added
void InitParameters(void);	// 2014-03-04 Liz added
void InitMeter(void);		// 2014-03-04 Liz added
void SaveRawEnergy(void);	// 2014-02-20 Liz added.
void Add_raw_energy(unsigned long * net_raw_ENR, unsigned long * net_ENR_OVFC1, BYTE phase, BYTE reg, unsigned char b_compute_neg); // 2014-02-27 Liz added
void SaveRawEnergyToEEP(void);		//2014-04-01 Liz added.
void Check_MAXQGlobalStatus(void);	//2014-03-31 Liz added.
//**********************************
// Global Variables.
//**********************************
static BOOL is_save_RTCC = FALSE; 		//2014-02-17 CL added
static BOOL is_stop_mode = FALSE;		//2014-02-11 CL added. 
static char StartDoAdc = 0;				//2014-2-26 CL added. Flag to determine the time to measure the voltage of super capacitor
static BOOL bMAXQ_init = FALSE;			//2014-06-06 Liz
static BOOL Vsense_high = FALSE;		//2014-06-09 Liz
static BOOL bMeterReset = FALSE;		//2014-06-16 Liz

static  char			AUTO_RESET_TIME = 0;	// 2012-11-14 Liz added. Reset bot every 12 hours
static	char			RefreshMaxDemand = 0;
static  char			StartDoReadingFlag = 0;
//static  char			IS_SD_CARD_FOUND = 0;	// 2013-09-13 Liz removed. Not in use.
//		char 			setTime[6] = {0x02,0x0A,0x0C,0x00,0x00,0x01};
		long 			REFRESH_INTERVAL = 2;	// 2012-07-25 Liz: Added location of credit balance in eeprom
static	char			SAVE_RTCC = 0;			// 2013-09-13 Liz added.
static	char			SAVE_RAW_ENERGY = 0;

#if defined (THREE_PHASE_BOTTOM_BOARD) || defined METER_TOP_BOARD
	// 2012-05-13(Eric) - Modified initialisation array of calibration data to include new elements.
	static BYTE InitCalibrationData[32] = 
	{0x38,0x40,0x40,0x1F,0x9D,0x13,0x60,0x33,0x10,0x40,0xDE,0x3F,0x10,0x40,0xDE,0x3F,0x10,0x40,0xDE,0x3F,0x41,0x00,0x64,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00};	// 100Amp CT - 16350}
#endif
#if defined (SINGLE_PHASE_BOTTOM_BOARD)
	static BYTE InitCalibrationData[15] = 
	{0x68,0x06,0xA0,0x0F,0x01,0x0D,0x10,0x22,0xAC,0x3F,0xB6,0x40,0x41,0x00}; // 100Amp C
#endif
//**********************************
// Interrupt routines.
//**********************************
void LowISR(void);
void HighISR(void);

	#pragma code lowVector=0x18
	void LowVector(void){_asm goto LowISR _endasm}

	#pragma code highVector=0x08
	void HighVector(void){_asm goto HighISR _endasm}
	
	#pragma code // Return to default code section

	#pragma interruptlow LowISR
	void LowISR(void)
	{	
		
	}
	#pragma code

	#pragma interrupt HighISR
	void HighISR(void)
	{
		#ifdef APP_USE_MCU
			if( MCU_RX_INTERRUPT_FLAG) //&& (is_processing == 1) )
			{
				MCUUnloadData();
			}
		#endif
				
// 2013-08-15 Liz removed. Changed to use 8MHz osc for system clock		
// 2014-04-22 Liz add back for meter V6
		if( PIR1bits.TMR1IF )
		{	
			static i = 0;
			static unsigned long j = 0;
	
			TMR1H = 0x80;
			TMR1L = 0x02;
			
			//////////////////////////////////////////
			// 2014-03-19 Liz added.
			if( StartDoReadingFlag == 0 && i++ >= 2) 
			{					
				// Read MAXQ values every 3 seconds
				StartDoReadingFlag = 1;
				i = 0;
			}
			///////////////////////////////////////////
												
			HandleRTCC();			// Update time and calendar
		}
		PIR1bits.TMR1IF = 0;	// Clear interrupt flag
	}
	#pragma code

//When this flag is set, an error msg was displayed on the LCD.
//This flag is checked regularly to make sure that the error msg is not displayed forever.
//static char error_msg_flag=0;
#pragma code
		
ENR_OVF_COUNTER		EnrOvfCounter1;  // 2012-04-17(Eric) ENR_CONFIG and EnrConfig renamed. Refer to release notes.
ENR_OVF_COUNTER		EnrOvfCounter2;
CALIBRATION_VALUES	CalibrationData;

//*****************************************************************
// 2014-03-04 Liz. 
//*****************************************************************
void InitTimer(void)
{
	// 2014-03-04 Liz. Timer 1 is initialised once time only when meter power up and not in sleep mode
	// Initialise timer 1.
	OpenTimer1(
		TIMER_INT_ON
		& T1_16BIT_RW
		& T1_SOURCE_EXT
		& T1_PS_1_1
		& T1_OSC1EN_ON  
		& T1_SYNC_EXT_OFF );
			
	INTCONbits.GIEH = 1;
	INTCONbits.GIEL = 1;	
}

void InitIOPorts(void)
{
	//2014-2-17 CL added . Turn off super capacitor discharing loop
	//2014-06-09 Liz removed. Not in use.
//	VCONTROL_TRIS = 0;
//	VCONTROL = 1;
	//
	
	// Initialise LEDs and MAIN_SENSE
	LED1_TRIS = 0;
	LED2_TRIS = 0;
	LED3_TRIS = 0;
	POWER_RESET_TRIS = 0;	// PIC controls the RESET pin of MAXQ
	VSENSE_TRIS = 1;		//2014 -02-11 CL: Added VSENSE to detect the power trip
	
	POWER_RESET_LAT = 1;	// Keep RESET pin high	
		
	LED1 = 1;
	LED2 = 1;
	LED3 = 1;
		
	Delay10KTCYx(100);
		
	LED1 = 0;
	LED2 = 0;
	LED3 = 0;		
}

void InitMAXQ(void)
{
	//**********************************************
	//**** Initialise MAXQ3180 *********************
	//**********************************************

	//Variables for MAXQ3180.
	long hresults = 0, lresults = 0;
	unsigned char r, retry = 0;
	
	// 2014-03-04 Liz. Wake up MAXQ from stop mode
	POWER_RESET_TRIS = 0;
	
	POWER_RESET_LAT = 0;	// Keep RESET pin LOW	
	Delay1KTCYx(1);
	POWER_RESET_LAT = 1;	// Keep RESET pin high	
	Delay1KTCYx(1);
	////
		
	PowerOpen();
	
	while( ((r = PowerSendReadCommand(0x026, 2, &hresults, &lresults))) && (retry++ < 3) )
	{
		if( r )
		{
			if( lresults == 0x0000009C )
			{
				// MAXQ detected...
				break;
			}	
		}
		else
			Delay1KTCYx(10);
	}
		
	//2012-08-08 Liz: reset meter if cannot detect MAXQ
	if((retry >= 3) && (r != 1))
	{
		//Reset();
	}	
	//
	
	// 2014-03-31(LIZ). Need to clear global status flag
	Power_WriteToMAXQ(0x000, 2, 0, 3);

	// Configure OPMODE0
	Power_WriteToMAXQ(0x001, 2, 0, 3);

	// Configure OPMODE1 to set method to calculate power for 3 phases. Set the polarity of pulse generator(active high)
	Power_WriteToMAXQ(0x002, 2, 4, 3);

	// Write value for Undervoltage condition, currently set to 20V
	//	However, should set to ~90% nominal value (110-120V or 220-240V)
	// 2012-10-15 Liz: set condition to 198V 
	Power_WriteToMAXQ(0x048, 2, 26056, 3);
				
	// Configure value for No-load condition. Set to 0.05% full scale current 
	Power_WriteToMAXQ(0x04A, 2, 33, 3);
		
	// Subscribing to global interrupt mask.
	Power_WriteToMAXQ(0x006, 2, 4, 3);
				
	// Set Interrupt Mask Phase A
	Power_WriteToMAXQ(0x145, 2, 0x0F, 3);
		
	// Set Interrupt Mask Phase B
	Power_WriteToMAXQ(0x231, 2, 0x0F, 3);
		
	// Set Interrupt Mask Phase C
	Power_WriteToMAXQ(0x31D, 2, 0x0F, 3);
				
	// Configure CFP - Total real energy
	Power_WriteToMAXQ(0x01E, 2, 7, 3);
		
	// Configure CFP threshold
	Power_WriteToMAXQ(0x022, 4, 0x28C5030, 3);
				
	// Enable neutral current sampling, clear DADCNV bit in SCAN_IN register
	Power_WriteToMAXQ(0x00E, 2, 0x60, 3);
			
#ifdef THREE_PHASE_BOTTOM_BOARD		
		// Write to OPMODE2 to set the current offset calibration method and
		// coefficient used in calculating apparent power. 
		Power_WriteToMAXQ(0x003, 2, 0, 3);
		
		// 2012-10-15 Liz added: calibration setting to meet 0.5%e accuracy
		// Set current offset HIGH
		Power_WriteToMAXQ(0x138, 2, 52, 3);
		Power_WriteToMAXQ(0x224, 2, 52, 3);
		Power_WriteToMAXQ(0x310, 2, 52, 3);

		// Set current offset LOW
		Power_WriteToMAXQ(0x13C, 2, 0xFF00, 3);
		Power_WriteToMAXQ(0x228, 2, 0xFFF6, 3);
		Power_WriteToMAXQ(0x314, 2, 0xFFF6, 3);
		
		// Set current Gain LOW
		Power_WriteToMAXQ(0x13A, 2, 16400, 3);
		Power_WriteToMAXQ(0x226, 2, 16400, 3);
		Power_WriteToMAXQ(0x312, 2, 16400, 3);
		
		// Set Phase Compensation
		Power_WriteToMAXQ(0x13E, 2, 412, 3);
		Power_WriteToMAXQ(0x22A, 2, 412, 3);
		Power_WriteToMAXQ(0x316, 2, 412, 3);	
#endif
	
	// Set up to read interrupt pin from MAXQ3180.
	POWER_INT_PIN = 0;
	POWER_INT_PIN_TRIS = 1;
		
	PowerClose();
	
	// Initialize data from EEPROM
	#ifdef APP_USE_DEVCONFIG
		InitDataFromEEPROM();
		EnrOvfCounter1.Flags.bIs_3Pkwh_Ready = 0;	//2014-06-10 Liz. Need to initialise this flag.
	#endif
}	

void InitParameters(void)
{
	// Initialise POWER_READINGS register.
	unsigned char i, j, length;
	int * phase_storage = POWER_READINGS[0]; 
	long * kk = phase_storage[0]; 
		
	for(i=0; i<4; i++)
	{
		for(j=0; j<POWER_REGISTERS_QUEUE_SIZE; j++)
		{
			phase_storage = (int *)POWER_READINGS[i];
			kk = (long *)phase_storage[j];
			length = POWER_REGISTERS_SIZE_QUEUE[i][j];
			
			if(i>0 && j == 13)	continue;	// skip Max_demand value since it's read from eeprom
				
			// Init variables
			if( (void*)kk != 0 )
			{
				kk[0] = 0;	
				if(length >= 8)	kk[1] = 0;
			}	
		}	
	}
	
	// 2013-02-21 Liz Need to check if it is auto reset every 12 hours.
		if(RCONbits.NOT_TO == 0)
		{
			// 2013-03-28 Liz
			// If previous reset was called due to watchdog timeout event
			CalibrationData.Flag2.bTOReset = 1;	//2013-03-28 Liz 
			CalibrationData.Flags.bBoardReset = 0;
			CalibrationData.Flag2.bAutoReset = 0;
			CalibrationData.Flags.bIsModified = 1;	
		}	
		else if(RCONbits.NOT_RI == 0)
		{
			// If previous reset was called due to Reset() function
			CalibrationData.Flags.bBoardReset = 0;
			CalibrationData.Flag2.bAutoReset = 1;
			CalibrationData.Flags.bIsModified = 1;	
			CalibrationData.Flag2.bTOReset = 0;	//2013-03-28 Liz
		}
		else
		{
			// If previous reset was called due to other reason.
			CalibrationData.Flags.bBoardReset = 1;
			CalibrationData.Flag2.bAutoReset = 0;
			CalibrationData.Flags.bIsModified = 1;
			CalibrationData.Flag2.bTOReset = 0;	//2013-03-28 Liz
		}	
	// 2014-03-11 (LIZ). Clear bIsGetDatetime flag if detect meter reset.
//	CalibrationData.Flag2.bIsGetDatetime = 0;

	////////////////////////////
}	

void InitMeter(void)
{
	//2014-03-04 Liz
	InitTimer();
	InitIOPorts();					
//	ResetSPIDevices();		// 2014-06-09 Liz moved to main loop, when detect Vsense is high
//	InitMAXQ();				// 2014-06-09 Liz moved to main loop, when detect Vsense is high
	
	// Initialise MCU.
	#ifdef APP_USE_MCU
		MCUOpen();
	#endif	
	
//	InitParameters();		// 2014-06-12 Liz moved to main loop, when detect Vsense is high 
	// Reset SPI devices.
	ResetSPIDevices();		
}					
//*****************************************************************

void main (void)
{	
	// Initialise meter
	InitMeter();
	// 2014-03-12 Liz. DateTime is initialised once time only when meter reset.
	ReadRTCCFromEEPROM();
	
	// 2014-03-11 (LIZ). Clear bIsGetDatetime flag if detect meter reset.
//	CalibrationData.Flag2.bIsGetDatetime = 0;
	// 2014-06-16 Liz
	bMeterReset = TRUE;				
	//**********************************************
	// Main Program Loop.
	//**********************************************

	while (1)
	{
		// 2014-06-09 Liz. Check VSENSE
		{
			BYTE i = 0, k = 0, j = 0;
			BOOL is_Vsense_high = FALSE, is_Vsense_low = FALSE;
						
			// Read VSENSE 3 times to check power status state of meter
			do{
				k = 0;
				j = 0;
				
				for(i=0; i<3; i++)
				{
					if(VSENSE)	k++;
					else		j++;
				}	
					
				if(k >= 3)
				{
					is_Vsense_high = TRUE;
					is_Vsense_low = FALSE;
				}
				else if(j >= 3)
				{ 
					is_Vsense_high = FALSE;
					is_Vsense_low = TRUE;	
				}
				else
				{
					is_Vsense_high = FALSE;
					is_Vsense_low = FALSE;
				}	
				
			}while(is_Vsense_high==FALSE && is_Vsense_low==FALSE);
			
			// Update power status flag
			if(is_Vsense_high)
				Vsense_high = TRUE;	
			else
				Vsense_high = FALSE;
							
			is_Vsense_high = FALSE;
			is_Vsense_low = FALSE;
		}
		ClrWdt();
		
		//if (VSENSE)			//2014-2-12 CL added. Normal mode, meter is supplied full power.
		if(Vsense_high)		// 2014-06-09 Liz 
		{	
			// if is_stop_mode is true, it indicate the maxq is run under stop mode before.
			// Board Reinitialize is needed.
			if(is_stop_mode )
			{										
				// 2014-04-22 Liz. Have to wake up MAXQ from stop mode before reset MAXQ
				//		If not, MAXQ will give wrong voltage reading.
				PowerOpen();	
				Power_SendWriteCommand(0x001, 2, 0);
				PowerClose();
				Delay100TCYx(1);
				////////////////////////////////////////////////////////////////////////
				ClrWdt();
				// Initialise Meter
				InitMeter();
				// 2014-03-11 (LIZ). Dont clear bIsGetDatetime flag if detect meter wake up from sleep mode.
				//CalibrationData.Flag2.bIsGetDatetime = 1;	
				// 2014-03-24 (LIZ). Need to compensate time when meter's initialising and cause timer lag
				//		Estimate 1 sec for bottom board to initialise. Add in 1sec.
				rtcc.second++;	
				bMAXQ_init = FALSE;		// 2014-06-09 Liz
				is_stop_mode = FALSE; 		
			}		
			ClrWdt();

			// 2014-06-09 Liz. Only initialise MAXQ if there is 3.3V supply.
			if((bMAXQ_init == FALSE) || (POWER_RESET_PIN != 1))
			{
				ResetSPIDevices();
				InitMAXQ();	
				InitParameters();
				bMAXQ_init = TRUE;
			}							
			//2014-06-16 Liz. Reset Datetime flag if detect meter has been reset
			if(bMeterReset == TRUE)
			{
				CalibrationData.Flag2.bIsGetDatetime = 0;
				bMeterReset = FALSE;
				//SaveRawEnergyToEEP();
			}			
			//////
				
			//No 12 hour reset function 			
			//if(AUTO_RESET_TIME)
			//{
			//	ResetBot();
			//}	
			
			#ifdef APP_USE_MCU
				MCUTasks();
			#endif

			//2014-2-26 CL added. To measured the voltage of the super cap and update data to top board
			//if(StartDoAdc ==1 )
			//{
			//	VBatVoltageDetection();
			//	StartDoAdc = 0;
			//}
						
			if( StartDoReadingFlag == 1 )
			{			
				ResetSPIDevices();
				DoReadings();
				ComputeTotalRealEnergy();
								
				#ifdef APP_USE_MCU
				{
					MCUTasks();
				}
				#endif
				
				Update_Max_Min();
				Check_MAXQGlobalStatus();	// 2014-03-31 Liz added.
				StartDoReadingFlag = 0;
			}
	
			if(POWER_INT_PIN == 0)	// There is an interrupt caused by energy overflow
			{
				unsigned char r = 0;
				long hresults, lresults;
		
				PowerOpen();
				// Read register 0x004. If (REG:0x004 & 0x0004) == 1, we have a overflow condition.
				if( (r = PowerSendReadCommand(0x004, 2, &hresults, &lresults)) == 1 )
				{
					if((lresults & 0x0004))
						CaptureOverflow();	// 2011-05-01(Eric) - This function closes the SPI!
				}
				PowerClose();
			}
			
			if( CalibrationData.Flags.bIsModified ) SaveCalibrationData();	// Check and save new calibration values if they are modified.
			
			// 2012-11-09 Liz added:read current maxq status
			// Clear Maxq status flag after sucessfully sent to server
			if(IS_STATUS_FLAG_SENT)
			{
				BYTE r = 0, retries = 0, i=0;
				long hresult = 0, lresult = 0;
				short address_list[3] = {0x144, 0x230, 0x31C};	// status flag phase A,B,C
				
				PowerOpen();
				
				for(i=0; i<3; i++)
				{
					retries = 0;
					
					do{
						r = PowerSendReadCommand(address_list[i], 1, &hresult, &lresult);
						
						if(r!=1)
						{
							// When there is an error talking to MAXQ3180...
							#if (CLOCK_SPEED == 32000000)
								Delay100TCYx(20);	//2012-10-15 Liz: changed from 10 to 20. Requesting too fast makes MAXQ unable to response
							#else
								#error "No valid clock speed defined."
							#endif
						}	
					}while(retries++<3 && r!=1);	//2014-06-09 Liz. forgot to increase retries.
				
					if(r == 1)
					{
						maxq_mask[i] = ~maxq_mask[i];
						lresult &= maxq_mask[i];
						r = 0;
						Power_WriteToMAXQ(address_list[i], 2, lresult, 3);
					}
				}	
										
				PowerClose();
				
				IS_STATUS_FLAG_SENT = 0;
			}	
			
			//2014-03-31(LIZ) removed. New V6 design have battery, so rtcc is still running in sleepmode.
			//		Firmware doesnt need to keep saving datetime to eeprom every 5 mins.
			// 2013-09-13Liz added. Save DateTime every 5 mins
			//if(SAVE_RTCC == 1)
			//{
			//	SaveRTCCToEPPROM();
			//	SAVE_RTCC = 0;
			//}	

			// 2014-01-16 Liz. If it's time to write eeprom record, send request to Top board.
			if(EEP_TIMER_FLAG == TRUE && EEP_RECORD_FLAG == FALSE)
			{
				char msg[5] = {MMT_EEP_RECORD_TIME, '#', 'E', 'P', 0};
			
				// Send notification to TOP board. Clear the Flag after receive ACK from TOP
				if(msg!=0)
				{
					BOOL result = MCUSendString(4, &msg[0]);
					if(result)
					{
						EEP_RECORD_FLAG = TRUE;
					}	
				}
			}	 
			
			//////////////////////////////////////////
			Delay1KTCYx(5);  // Critical delay. (50)
			ClrWdt();
;
		}
		else	// 2014-03-31(LIZ). Sleep mode. Meter is using battery supply.
		{		
			if(!is_stop_mode)
			{	
				long hresult = 0, lresult=0; 
				
				ClrWdt();	// 2014-06-09 Liz
								
				if(bMAXQ_init == TRUE)								 
				{
					SaveRTCCToEEPROM();			//2014-2-18 :RECORD Last date time at the moment VSENSE = low
					SaveRawEnergyToEEP();		//2014-04-01(LIZ). Save raw energy to eeprom before go to sleep.
				}
				//////////////////////////////
				
				LED1 = 0;
				LED2 = 0;
				LED3 = 0;
				
				//2014-2-14 CL added: To configure the MaxQ into stop mode
				PowerOpen();
				PowerSendReadCommand(0x0C02, 1, &hresult, &lresult);
				PowerClose();
					
				// Disable Intterupt
				INTCONbits.GIEH = 1;
				INTCONbits.GIEL = 1;
				INTCONbits.RBIE = 0;		//Disable PortB on change int
				INTCONbits.INT0IE = 0;		
				INTCON3bits.INT1IE = 0;
				INTCON3bits.INT2IE = 0;

				PIE1bits.TMR1IE = 1;
							
				// Disable Timer 3
				CloseTimer3();

				// Disable SPI
				CloseSPI();
				
				// Disable USART
				CloseUSART();
				
				// Software Disable watchdog timer
				//WDTCONbits.SWDTEN = 0;
				
				//2014-2-17 CL added . Turn on super capacitor discharing loop
				//2014-06-09 Liz removed. Not in use.
				//VCONTROL = 0;
				
				ANSEL = 0xFF;			//disable RE0-RE2,RA0-RA5 Digital Input buffer
//				ANSELH = 0b01011;		//Enable RB0,RB1 digital input buffer; disable RB4,RB3,RB2 Digital input buffer
				ANSELH = 0b11011;		//2014-06-17 Liz. Only enable RB1 for VSENSE
				
				TRISA = 0xFF;
				TRISB = 0xFF;
				TRISC = 0xFF;
				TRISD = 0xFF;
				TRISE = 0xFF;
				
			//	SET PORTE as General input
				DDREbits.PSPMODE = 0;
				DDREbits.RE2 = 1;
				DDREbits.RE1 = 1;
				DDREbits.RE0 = 1;
				
				OSCCONbits.IDLEN = 0;	
				CM1CON0bits.C1ON = 0;
				CVRCONbits.CVREN = 0;		//power down CVref circuit
				CVRCON2bits.FVREN = 0;
				HLVDCONbits.HLVDEN = 0;	
								
				bMAXQ_init = FALSE;			// 2014-06-09 Liz
				is_stop_mode = TRUE;		//update FLAG						
			}
			
			ClrWdt();
			Sleep();
		}//end
	}
}

//*************************************************************************
// 2012-05-08 Liz: This function read data from eeprom twice
//					and compare 2 array to make sure data is read correctly
// EEPROM stores :  
//		Start from 0x00: 0x70| EnrOvfCounter1 | EnrOvfCounter2 | CalibrationData 
//		Start from 0x78: 0x80| MAX_DEMAND_REFRESH_INTERVAL | OLD_MAX_VALUE
//*************************************************************************
void InitDataFromEEPROM(void)
{
	// Read calibration settings from MCU's EEPROM.
	unsigned int i = 0, j = 0;
	BYTE temp[5];	//2014-06-18 Liz modified
	BOOL is_data_correct = FALSE, is_eeprom_blank = TRUE;
	BYTE checksum = 0, eep_chk = 0;	// 2014-06-17 Liz 
		
	BYTE * cnfg = (BYTE*)&CalibrationData;
	BYTE * enrovfc1 = (BYTE*)&EnrOvfCounter1;
	BYTE * enrovfc2 = (BYTE*)&EnrOvfCounter2;
	
	// Initialise CalibrationData with default values first.
	CalibrationData.Flags.bComputeWithNegativeEnergy = FALSE;

	// 2013-11-12 Liz. Check if refenrece is 0
	if(cnfg == 0 || enrovfc1 == 0 || enrovfc2 == 0)	return;
	//
	
	// 2012-05-11 Liz: Read first byte 3 times to make sure it is read correctly
	{
		for(i=0;i<3;i++)
		{
			temp[i] = Read_b_eep(0);
			if(temp[i] == 0x70)	j++;	// count number of times can read 0x70	
		}
		
		// If all 3 times read different values, eeprom is spoiled, try to reset bottom board
		if( (temp[0] != temp[1])&&(temp[1] != temp[2])&&(temp[2] != temp[0])  )
		{
			RCONbits.NOT_POR = 0;
			Reset();
		}
		else if( j++ >= 1)	is_eeprom_blank = FALSE;	// eeprom have data, read Calibration data from eeprom
		else				is_eeprom_blank = TRUE;		// eeprom is blank, set up default value for calibration data and write into eeprom	
	}

	// Read out all data from EEPROM.
	//if( Read_b_eep(0) == 0x70u )  // Check if a valid calibration setting was found in EEPROM.
	if(!is_eeprom_blank)
	{						
		// Read out EnrOvfCounter1 from EEPROM.
		for( i=0; i<sizeof(EnrOvfCounter1); i++ )
		{
			do
			{
				enrovfc1[i] = Read_b_eep(i+1);
				temp[0] = Read_b_eep(i+1);
				if(enrovfc1[i] == temp[0])	is_data_correct = TRUE;
				else					is_data_correct = FALSE;	
			} while(!is_data_correct);
//			checksum += enrovfc1[i];	// 2014-06-17 Liz
		}
		
		// Read out EnrOvfCounter2 from EEPROM.
		j = sizeof(EnrOvfCounter1);
		for( i=0; i<sizeof(EnrOvfCounter2); i++ )
		{	
			do
			{
				enrovfc2[i] = Read_b_eep(j+i+1);
				temp[0] = Read_b_eep(j+i+1);
				if(enrovfc2[i] == temp[0])	is_data_correct = TRUE;
				else					is_data_correct = FALSE;	
			} while(!is_data_correct);
//			checksum += enrovfc2[i];	// 2014-06-17 Liz
		}

		// Read out DevConfig from EEPROM.
		j = sizeof(EnrOvfCounter1) + sizeof(EnrOvfCounter2);
		for( i=0; i<sizeof(CALIBRATION_VALUES); i++ )
		{
			do
			{
				cnfg[i] = Read_b_eep(j+i+1);
				temp[0] = Read_b_eep(j+i+1);
				if(cnfg[i] == temp[0])	is_data_correct = TRUE;
				else					is_data_correct = FALSE;	
			} while(!is_data_correct);
//			checksum += cnfg[i];	// 2014-06-17 Liz
		}
		
		// 2014-06-17 Liz. Get checksum saved in eeprom and compare with the calculated one.
//		for(i=0; i<3; i++)
//			eep_chk = Read_b_eep(100);
//		is_data_correct = FALSE;
//		if(eep_chk == checksum)
//			is_data_correct = TRUE;
//		else
//			is_data_correct = FALSE;
	}
	else
//	if(is_eeprom_blank || is_data_correct == FALSE)
	{
		// No valid calibration setting was found.
		// 2012-05-03(Eric) - Initialise the structures containing calibration, energy counter 1 and energ counter 2.
		char k = 0;
		
		// 2012-05-13(Eric) - Calibration values was not fully initialised. Modify to initialise.
		memset(&CalibrationData, 0, sizeof(CALIBRATION_VALUES));
		memcpy((BYTE*)&CalibrationData, (BYTE*)&InitCalibrationData, sizeof(CALIBRATION_VALUES));
		// Initialise flags in calibration data.
		CalibrationData.Flags.bComputeWithNegativeEnergy = 0;
		// End fix initialisation.
		
		//2012-08-10 Liz: use memset() to reduce program memory
		memset(&EnrOvfCounter1, 0, sizeof(ENR_OVF_COUNTER));
		memset(&EnrOvfCounter2, 0, sizeof(ENR_OVF_COUNTER));
		
		CalibrationData.Flags.bIsModified = 1;
		// Eric(2012-10-23) This line is not needed since handled by call to AutoCalibration() below...
		//CalibrationData.Flags.bChecksumChanged = 1;  
		CalibrationData.Flags.bEnergyReset = 1;
		// 2012-05-03(Eric) - Structure will automatically be saved by setting
		// CalibrationData.Flags.bIsModified to 1.
		//SaveCalibrationData();
	}	
	
	//2012-07-27 Liz: Read max demand & refresh interval
	{
		temp[0] = Read_b_eep(120);
		
		if(temp[0] == 0x80)
		{
			REFRESH_INTERVAL = 0;
			for(i=0; i<4; i++)
			{
				REFRESH_INTERVAL |= (Read_b_eep(124-i) & 0x000000FF);
				if(i<3)	REFRESH_INTERVAL <<= 8;
			}	
			
			for(i=0; i<3; i++)
			{
				BYTE * eep_mvalue = (BYTE *)MAX_MIN_STORAGE_QUEUE[i][0]; 
			
				// 2013-11-12 Liz. Check if refenrece is 0
				if(eep_mvalue == 0)	return;
				//
			
				// Write to eeprom
				// 2014-04-04 Liz modified. Max Demand size is reduced from 8 to 4 bytes.
				for( j=0; j<4; j++ )
				{
					eep_mvalue[j] = Read_b_eep(j+125+(i*4)) ;
				}
			}	
		}	
		else
		{
			REFRESH_INTERVAL = 0;
			// Eric(2013-05-16) - Clear max_min_storage structure.
			memset(&max_min_storage, 0, sizeof(MAX_MIN_STORAGE));	
		}
	}
		
	//2014-02-21 Liz: read raw energy stored in eeprom
	{		
		checksum = 0;				//2014-06-17 Liz
		eep_chk = 0;				//2014-06-17 Liz
		is_data_correct = FALSE;	//2014-06-17 Liz
		is_eeprom_blank = TRUE;		//2014-06-17 Liz
		
		for(i=0; i<3; i++)
			temp[0] = Read_b_eep(160);		//160
		if(temp[0] == 0x70)		is_eeprom_blank = FALSE;	
		else					is_eeprom_blank = TRUE;	
		
		if(is_eeprom_blank == FALSE)
		{
			cnfg = (BYTE*)&raw_energy_storage_2;
			
			// 2014-05-07 Liz. Prevent reference addr 0x0000
			if(cnfg == 0)	return;
			
			for( i=0; i<sizeof(raw_energy_storage_2); i++ )
			{
				do
				{
					cnfg[i] = Read_b_eep(i+161);	//161
					temp[0] = Read_b_eep(i+161);	//161
					if(cnfg[i] == temp[0])	is_data_correct = TRUE;
					else					is_data_correct = FALSE;	
				} while(!is_data_correct);
//				checksum += cnfg[i];	//2014-06-17 Liz
			}
			// 2014-06-17 Liz. Get checksum saved in eeprom and compare with the calculated one.
//			for(i=0; i<3; i++)
//				eep_chk = Read_b_eep(230);	//230
//			is_data_correct = FALSE;
//			if(eep_chk == checksum)
//				is_data_correct = TRUE;
//			else
//				is_data_correct = FALSE;
		}	
		else
//		if(is_eeprom_blank == TRUE || is_data_correct == FALSE)
			memset(&raw_energy_storage_2, 0, sizeof(raw_energy_storage_2));
	}		
	// End added
	
	// Calibrate the MAXQ3180 with values read out to DevConfig.
	AutoCalibration();
}

// 2012-05-08 Liz: changed eeprom data allocation
// EEPROM stores :  Start Address 0x000000
//					0x70| EnrOvfCounter1 | EnrOvfCounter2 | CalibrationData 
void SaveCalibrationData(void)
{
	char s[5];
	BYTE checksum = 0;	// 2014-06-17 Liz
	
	while( Read_b_eep(0) != 0x70u )
	{
		Write_b_eep(0, 0x70u);  //0
		Busy_eep();
	}

	{
		unsigned int i = 0, j = 0;
		BYTE * cnfg = (BYTE*)&EnrOvfCounter1;
		CalibrationData.LAST_MODIFIED += 1;
		
		// 2013-11-11 Liz added to check 0 reference
		if(cnfg == 0)		return;
		//
			
		// Write EnrOvfCounter1 to eeprom
		for( i=0; i<sizeof(EnrOvfCounter1); i++ )
		{
			// while loop to make sure the correct value was written in.
			while( Read_b_eep(i+1) != (unsigned char)cnfg[i] )
			{
				Write_b_eep(i+1, (unsigned char)cnfg[i]);  //0
				Busy_eep();
			}
//			checksum += cnfg[i];	// 2014-06-16 Liz
		}
		
		// Write EnrOvfCounter2 to eeprom
		cnfg = (BYTE*)&EnrOvfCounter2;
		
		// 2013-11-11 Liz added to check 0 reference
		if(cnfg == 0)		return;
		
		j = sizeof(EnrOvfCounter1);
		for( i=0; i<sizeof(EnrOvfCounter2); i++ )
		{
			// while loop to make sure the correct value was written in.
			while( Read_b_eep(j+i+1) != (unsigned char)cnfg[i] )
			{
				Write_b_eep(j+i+1, (unsigned char)cnfg[i]);  //0
				Busy_eep();
			}
//			checksum += cnfg[i];	// 2014-06-16 Liz
		}

		// Write calibration data to eeprom
		cnfg = (BYTE*)&CalibrationData;
		
		// 2013-11-11 Liz added to check 0 reference
		if(cnfg == 0)		return;
		
		j = sizeof(EnrOvfCounter1) + sizeof(EnrOvfCounter2);
		for( i=0; i<sizeof(CALIBRATION_VALUES); i++ )
		{
			// while loop to make sure the correct value was written in.
			while( Read_b_eep(j+i+1) != (unsigned char)cnfg[i] )
			{
				Write_b_eep(j+i+1, (unsigned char)cnfg[i]);  //0
				Busy_eep();
			}
//			checksum += cnfg[i];	// 2014-06-16 Liz
		}
		
		// 2014-06-16 Liz. Saved checksum
//		while( Read_b_eep(100) != checksum )
//		{
//			Write_b_eep(100, checksum);  //0
//			Busy_eep();
//		}
	}
	CalibrationData.Flags.bIsModified = 0;
	
	// 2012-10-02 Liz: Notify Top board that calibration data is changed.
	// 2014-03-11 Liz added 1 more flag.
	{
		short int * ptr = (short int*)&CalibrationData.Flags;
		short int * ptr2 = &CalibrationData.Flag2;
		// 2013-11-11 Liz added to check 0 reference
		if(ptr == 0 || ptr2 == 0)		return;
		//
		
		s[0] = (BYTE)MMT_METER_STATUS_CHANGED;
		s[1] = *ptr;
		s[3] = *ptr2;
		MCUSendString(5, &s[0]);
	}	
	//
	
	//2014-03-11 Liz removed. Cannot send 2 request next to each other. Top board will lost the second request.
	//2014-02-26 : CL added: Notify top board the IsGetDatetime Flag
//	if (CalibrationData.Flag2.bIsGetDatetime == 1)
//	{	
//		short int * ptr = &CalibrationData.Flag2;
//		s[0] = (BYTE)MMT_BOT_UPDATE_TOP_DATETIME;
//		s[1] = *ptr;
//		MCUSendString(2, &s[0]);
//	}
	//////////////////////////////////////////////////////////
}

//*******************************************************
//	Handle energy overflow counter.
//	Remember to call PowerOpen() and PowerClose() before 
//		and after calling this function
//*******************************************************
void CaptureOverflow(void)
{
	unsigned char i = 0, j = 0, r = 0;
	long hresults, lresults, tmp=1;
	static unsigned char is_flag_cleared = 0;
	static unsigned long u = 0;
	static unsigned int * wOvfc1, * wEnr_cc;
	BYTE retries = 0;
	
//	PowerOpen();	//2012-08-10 Liz: Removed since PowerOpen() is called before this function
	
//	for(i=0; i<3; i++)	// Loop through phase A, B, C
	#if defined (SINGLE_PHASE_BOTTOM_BOARD)
	while(i < 1)
	#endif
	#if defined (THREE_PHASE_BOTTOM_BOARD)
	while(i < 3)	// Use while loop here to make sure that mcu reads all overflow flags before clearing the global flag.
	#endif
	{	
		if( (r = PowerSendReadCommand(
		(unsigned short)ENERGY_OVERFLOW_FLAGS_QUEUE[i],
		(unsigned short)1,
		&hresults, &lresults)) == 1 )
		{
			if( hresults == 0 && lresults == 0 )
			{
				i++;
				continue;
			}
			
			for(j = 0; j<5; j++)	// Loop through each energy_overflow_flag
			{
				if( lresults&tmp )
				{					
					wOvfc1 = (unsigned int *)EEPROM_ENR_OVFC1[(i*5)+j];  // 2012-04-17(Eric) - Copy overflow counter1 into temp.
					wEnr_cc = (unsigned int *)EEPROM_CALIBRATION_DATA[2];  // 2012-04-17(Eric) - EEPROM_METER_CONFIG[2] = ENR_CC.
					
					// 2013-11-11 Liz added to check 0 reference
					if(wOvfc1 == 0 || wEnr_cc == 0)	continue;
					//
					
					u = (unsigned long)(*wOvfc1 + 1) * (*wEnr_cc);  // 2012-04-17(Eric) - OverflowCounter1 * ENR_CC.
					
					if(u >= 200000000)	// Check if counter 1 is overflow
					{
						// 2012-04-17(Eric) - Counter has overflowed.
						*EEPROM_ENR_OVFC1[(i*5)+j] = 1;
						
						if(*EEPROM_ENR_OVFC2[(i*5)+j] == 0xFFFF && j!=4)	// Check if the counter 2 is overflow
						{
							switch(j)
							{
								case 0:		// 0: Real positive energy
								case 2:		// 2: Reactive positive energy
									{
										*EEPROM_ENR_OVFC2[(i*5)+j] = 0;	    // reset counter
										*EEPROM_ENR_OVFC2[(i*5)+j+1] = 0;	// reset negative energy counter 2
										*EEPROM_ENR_OVFC1[(i*5)+j+1] = 0; 	// reset negative counter 1
										
										if( j == 0 )  // Only tracks overflow of counter 2 for real positive energy.
										{
											switch(i)
											{
												case 0:
													CalibrationData.Flags.bPhaseA_RealPos_C2_Overflow = 1;
													break;
												case 1:
													CalibrationData.Flags.bPhaseB_RealPos_C2_Overflow = 1;
													break;
												case 2:
													CalibrationData.Flags.bPhaseC_RealPos_C2_Overflow = 1;
													break;
												default:
													break;
											}	
										}
									}	
									break;
								case 1:		// 1: Real negative energy
								case 3:		// 3: Reactive negative energy
									{
										*EEPROM_ENR_OVFC2[(i*5)+j] = 0;	   // reset counter
										*EEPROM_ENR_OVFC2[(i*5)+j-1] = 0;  // reset positive energy counter 2
										*EEPROM_ENR_OVFC1[(i*5)+j-1] = 1;  // reset positive counter 1
									}	
									break;
								default:
									break;
							}		
						}	
						else
						{
							// 2012-04-17(Eric) - Counter has not overflow. Increment counter 2.
							*EEPROM_ENR_OVFC2[(i*5)+j] = (*EEPROM_ENR_OVFC2[(i*5)+j]+1);
						}	
					}
					else
					{
						// 2012-04-17(Eric) - Increment counter 1.
						*EEPROM_ENR_OVFC1[(i*5)+j] = (*EEPROM_ENR_OVFC1[(i*5)+j]+1);
					}	
				}	
				tmp <<= 1;
			}	
			
			// Clear flag
			Power_WriteToMAXQ(ENERGY_OVERFLOW_FLAGS_QUEUE[i], 2, 0, 3);

			// Mark EEPROM data as modified in order to save it to EEPROM.
			CalibrationData.Flags.bIsModified = 1;
			i++;
		}
		else
		{
			// When there is an error talking to MAXQ3180...
			#if (CLOCK_SPEED == 32000000)
				Delay1KTCYx(10);
			#else
				#error "No valid clock speed defined."
			#endif	
			
			// 2014-06-09 Liz. Cannot have infinite loop
			if(retries>=3)
			{
				i++;
				retries = 0;
			}
			else
				retries++;
		}
	}

	// Clear global status flag
	Power_WriteToMAXQ(0x004, 2, 0, 3);
		
//	PowerClose();	//2012-08-10 Liz: Removed since PowerOpen() is called before this function
}
	
void ResetSPIDevices(void)
{
	POWER_CS_TRIS = 0;
	PORTDbits.RD4 = 1;
	POWER_CS_LAT = 1;
	
	SD_CS_TRIS = 0;
	SD_CS = 1;
	SD_CS_LAT = 1;

	Delay1KTCYx(10);	//0
}	

void DoReadings(void)
{
	// 1) Take all required readings from MAXQ3180 and put it in variable reading_storage.	
	// Variables for MAXQ3180.
	long hresults, lresults;
	unsigned char s = 0, phase = 0;
	char r;
	static int * jj;
	static long * kk, * k;
	static unsigned long dwK, dwNET_X_ENR_OVFC1, dwNET_X_ENR_OVFC2;
	static unsigned int * wPOS_X_ENR_OVFC1, * wNEG_X_ENR_OVFC1, * wPOS_X_ENR_OVFC2, * wNEG_X_ENR_OVFC2 ;
	static unsigned long * dwRAW_POS_ENERGY, * dwRAW_NEG_ENERGY ;
	
	PowerOpen();
	Delay1KTCYx(10);
	#if defined SINGLE_PHASE_BOTTOM_BOARD
	for (phase = 0; phase < 2; phase++)  // Cycle through M and A.
	#elif defined THREE_PHASE_BOTTOM_BOARD 
	for (phase = 0; phase < 4; phase++)  // Cycle through M, A, B, C.
	#endif
	{	
		for( s=0; s<POWER_REGISTERS_QUEUE_SIZE; s++ )
		{
			//int * jj = POWER_READINGS[phase];
			//long * kk = jj[s];
			jj = (int*)POWER_READINGS[phase];
			kk = (long*)jj[s];
			
					
			if( kk == 0 ) continue;	// Do not read if there is nowhere to save this data.	
			if((phase > 0) && (s > 9))	continue;	// Do not read if it is energy.
			if((phase==0) && (s==10)) continue;	// Do not read if it is total real energy.
			if(phase==0 && s==15)	// Read CT_Range;
			{
				*kk = ((unsigned long)CalibrationData.CT_RANGE & 0xFFFFFFFF);
				continue;	
			}	
			{
				// Energy readings need to be specially handled.
				// We are only interested in the raw energy readings.				
				if( (r = PowerSendReadCommand(
					(unsigned short *)POWER_REGISTERS_QUEUE[phase][s],
					(unsigned char *)POWER_REGISTERS_SIZE_QUEUE[phase][s],
					&hresults, &lresults)) == 1 )
				{								
					if( POWER_REGISTERS_SIZE_QUEUE[phase][s] == 8 )
					{
						kk[0] = (lresults & 0xFFFFFFFF);
						kk[1] = (hresults & 0xFFFFFFFF);
						
						if(phase>0 && s==1)		// Get voltage reading
						{
							if((lresults <= 0x80000000) && (hresults==0))	// Turn off LED if voltage value less then 1.5V(means power line is not connected)
							{
								switch(phase)
								{
									case 1:	// Phase A
										if(LED1 == 1)	LED1 = 0;
										break;
									case 2:	// Phase B
										//if(LED2 == 1)	LED2 = 0;
										break;
									case 3:	// Phase C
										//if(LED3 == 1)	LED3 = 0;
										break;
									default:
										break;
								}	
							}
							else	// Turn on LED if power line is connected
							{
								switch(phase)
								{
									case 1:	// Phase A
										if(LED1 == 0)	LED1 = 1;
										break;
									case 2:	// Phase B
										//if(LED2 == 0)	LED2 = 1;
										break;
									case 3:	// Phase C
										//if(LED3 == 0)	LED3 = 1;
										break;
									default:
										break;
								}	
							}		
						}
						
						if(phase>0 && s==7)		// Check unload condition
						{
							if((lresults == 0) && (hresults==0))
							{
								// get maxq status flag
								jj = (int*)POWER_READINGS[0];
								kk = (long*)jj[phase+5];
								
								// Eric(2013-05-16) - Dont do anything if pointer value is 0.
								if( kk == 0 ) continue;
								
								lresults = *kk;
								lresults |= 0x40;
								*kk	= (lresults & 0xFFFFFFFF);
							}					
						}
					}	
					else
					{
						*kk = (lresults & 0xFFFFFFFF);		
					}								
				}
				else
				{
					// When there is an error talking to MAXQ3180...
					#if (CLOCK_SPEED == 32000000)
						Delay1KTCYx(10);
					#else
						#error "No valid clock speed defined."
					#endif
				}	
			}	
		}	
		
		// Special cases.
		// If we are reading energy raw data, append the overflow counter
		// Append the overflow counter to the reading.
		if( phase != 0 )
		{
			int e = 0;
			BYTE retries = 0;	// 2013-03-20 Liz added
						
			// Read energy overflow flag.
			if( (r = PowerSendReadCommand(
				(unsigned short)ENERGY_OVERFLOW_FLAGS_QUEUE[phase-1],
				(unsigned char)1,
				&hresults, &lresults)) == 1 )
			{
				// If there are any overflow in energy accumulation, skip reading all raw energy data.
				if( lresults == 0 )
				{
					// Read raw energy readings...		
					for( e=0; e<5; e++ )
					{
						// k is pointer to where the reading will be saved.
						//long * k = PHASE_RAW_ENERGY_STORAGE_QUEUE[phase-1][e];
						k = (long*)PHASE_RAW_ENERGY_STORAGE_QUEUE[phase-1][e];
							
						if( k == 0 ) continue;  // No storage space defined.
												
						// 2013-03-20 Liz: need to check if successfully read from MAXQ  
						retries = 0;
						do{	
							if( (r = PowerSendReadCommand(
								(unsigned short)PHASE_ENERGY_REGISTERS_QUEUE[phase-1][e],
								4,
								&hresults, &lresults)) == 1 )
							{
								*k = ((unsigned long)lresults);
							}
							else
							{
								// When there is an error talking to MAXQ3180...
								#if (CLOCK_SPEED == 32000000)
									Delay1KTCYx(10);
								#else
									#error "No valid clock speed defined."
								#endif
							}
						}while(r!=1 && retries++<3);	// 2013-08-13 Move to next 1 if failed after 3 times. Suspect this place cause 
														//		bottom board timeout reset
					}
					
					// Calculate energy
					for(e=0; e<3; e++)
					{
						jj = (int*)POWER_READINGS[phase];
						kk = (long*)jj[e+10];
						
						// 2013-11-12 Liz. Check if refenrece is 0
						if(kk == 0)	continue;
						//
						
						wPOS_X_ENR_OVFC1 = (unsigned int *)EEPROM_ENR_OVFC1[((phase-1)*5)+(e*2)];  // &EnrOvfCounter1.PHASE_A_REAL_POS
						wNEG_X_ENR_OVFC1 = (unsigned int *)EEPROM_ENR_OVFC1[((phase-1)*5)+(e*2+1)];  // &EnrOvfCounter1.PHASE_A_REAL_NEG
						wPOS_X_ENR_OVFC2 = (unsigned int *)EEPROM_ENR_OVFC2[((phase-1)*5)+(e*2)];  // &EnrOvfCounter2.PHASE_A_REAL_POS_OVFC
						wNEG_X_ENR_OVFC2 = (unsigned int *)EEPROM_ENR_OVFC2[((phase-1)*5)+(e*2+1)];  // &EnrOvfCounter2.PHASE_A_REAL_NEG_OVFC
						dwRAW_POS_ENERGY = (unsigned long *)PHASE_RAW_ENERGY_STORAGE_QUEUE[phase-1][e*2];  // &raw_energy_storage.PHASE_A_REAL_POSITIVE
						dwRAW_NEG_ENERGY = (unsigned long *)PHASE_RAW_ENERGY_STORAGE_QUEUE[phase-1][e*2+1];  // &raw_energy_storage.PHASE_A_REAL_NEGATIVE					
						
						// 2013-11-12 Liz. Check if refenrece is 0
						if(wPOS_X_ENR_OVFC1 == 0 || wNEG_X_ENR_OVFC1 == 0)	continue;
						if(wPOS_X_ENR_OVFC2 == 0 || wNEG_X_ENR_OVFC2 == 0)	continue;
						if(dwRAW_POS_ENERGY == 0 || dwRAW_NEG_ENERGY == 0)	continue;
						//
						
						// 2012-05-02(Eric)
						// When e=0, we are processing real energy.
						// When e=1, we are processing reactive energy.
						// When e=2, we are processing apparent energy.
						//
						#if defined(APP_USE_NEGATIVE_ENERGY)
						if((e==0) && CalibrationData.Flags.bComputeWithNegativeEnergy)	// For real energy, read both positive and negative regs 
						{					
							dwNET_X_ENR_OVFC2 = (unsigned long)(*wPOS_X_ENR_OVFC2 - *wNEG_X_ENR_OVFC2);
					
							if(*wPOS_X_ENR_OVFC1 <= *wNEG_X_ENR_OVFC1)	// Check if positive OVF_counter1 is bigger than negative one.
							{
								if(dwNET_X_ENR_OVFC2>0)
								{
									unsigned int * enr_cc = (unsigned int *)EEPROM_CALIBRATION_DATA[2];	// multiply by ENR_CC
									
									// Make sure there is no division by 0 here
									if (*enr_cc == 0)	continue;							
									
									dwNET_X_ENR_OVFC1 = 200000000/(*enr_cc); 
									dwNET_X_ENR_OVFC1 = dwNET_X_ENR_OVFC1 - *wNEG_X_ENR_OVFC1 + *wPOS_X_ENR_OVFC1;
									dwNET_X_ENR_OVFC2--;
								}	
								else
									dwNET_X_ENR_OVFC1 = 0;
							}	
							else
								dwNET_X_ENR_OVFC1 = (unsigned long)(*wPOS_X_ENR_OVFC1 - *wNEG_X_ENR_OVFC1);
					
							if(*dwRAW_POS_ENERGY < *dwRAW_NEG_ENERGY)	// Check if positive raw value is bigger than negative one.
							{
								if(dwNET_X_ENR_OVFC1>0)
								{
									dwNET_X_ENR_OVFC1--;
									dwK = 0xFFFFFFFF - *dwRAW_NEG_ENERGY + *dwRAW_POS_ENERGY;
								}
								else
									dwK = 0;
							}	
							else
								dwK = (unsigned long)(*dwRAW_POS_ENERGY - *dwRAW_NEG_ENERGY);
						}
						else if((e==1) && CalibrationData.Flags.bComputeWithNegativeEnergy)	// For real energy, read both positive and negative regs
						{
							// ***Note: for reactive energy, final vlaue is mainly contributed by negative value.
							//			However value is computed as ABSOLUTE VALUE due to some limitation. 
							dwNET_X_ENR_OVFC2 = (unsigned long)(*wNEG_X_ENR_OVFC2 - *wPOS_X_ENR_OVFC2);
					
							if( *wNEG_X_ENR_OVFC1 <= *wPOS_X_ENR_OVFC1)	// Check if negative OVF_counter1 is bigger than positive one.
							{
								if(dwNET_X_ENR_OVFC2>0)
								{
									unsigned int * enr_cc = (unsigned int *)EEPROM_CALIBRATION_DATA[2];	// multiply by ENR_CC
									
									// Make sure there is no division by 0 here
									if (*enr_cc == 0)	continue;
									
									dwNET_X_ENR_OVFC1 = 200000000/(*enr_cc); 
									dwNET_X_ENR_OVFC1 = dwNET_X_ENR_OVFC1 - *wPOS_X_ENR_OVFC1 + *wNEG_X_ENR_OVFC1  ;
									dwNET_X_ENR_OVFC2--;
								}	
								else
									dwNET_X_ENR_OVFC1 = 0;
							}	
							else
								dwNET_X_ENR_OVFC1 = (unsigned long)(*wNEG_X_ENR_OVFC1 - *wPOS_X_ENR_OVFC1);
					
							if(*dwRAW_NEG_ENERGY < *dwRAW_POS_ENERGY)	// Check if negative raw value is bigger than positve one.
							{
								if(dwNET_X_ENR_OVFC1>0)
								{
									dwNET_X_ENR_OVFC1--;
									dwK = 0xFFFFFFFF - *dwRAW_POS_ENERGY + *dwRAW_NEG_ENERGY;
								}
								else
									dwK = 0;
							}	
							else
								dwK = (unsigned long)(*dwRAW_NEG_ENERGY - *dwRAW_POS_ENERGY);
						}	
						//else
						#else
						{
							dwNET_X_ENR_OVFC1 = (unsigned long)*wPOS_X_ENR_OVFC1;
							dwK = (unsigned long)*dwRAW_POS_ENERGY;	
							dwNET_X_ENR_OVFC2 = (unsigned long)*wPOS_X_ENR_OVFC2;
						}					
						#endif
						
						// 2014-02-27 Liz. Add raw energy stored in eep
						Add_raw_energy(&dwK, &dwNET_X_ENR_OVFC1, phase, e, CalibrationData.Flags.bComputeWithNegativeEnergy);
							
						// Copy the energy counter to kk[0];
						// Higher 4-bytes is ENR_CC.
						// Lower 4-bytes is accumulated value.
						kk[0] = *((short*)(reading_storage_1.ENRCC));
						kk[0] <<= 16;
						kk[0] |= ((unsigned long)dwNET_X_ENR_OVFC1 & 0x0000FFFF);  	// Overflow Counter 1.
						kk[1] = (dwK & 0xFFFFFFFF);                  				// Current accumulated energy values from MAXQ.
						kk[2] = ((unsigned long)dwNET_X_ENR_OVFC2 & 0x0000FFFF);   	// Overflow counter 2.
					}
				}
			}
			else
			{
				// When there is an error talking to MAXQ3180...
				#if (CLOCK_SPEED == 32000000)
					Delay1KTCYx(10);
				#else
					#error "No valid clock speed defined."
				#endif
			}

			// 2012-04-30(Eric) - Do not call MCUTasks() inside DoReadings() as this will close the SPI to MAXQ causing problems to DoReadings().
		}							
	}
		
	PowerClose();
	Delay1KTCYx(100);
}

/*------------------------------------------------------------------
	This function get CT rating and calculate VOLT_CC, AMP_CC,
		PWR_CC and ENR_CC
------------------------------------------------------------------*/
void AutoCalibration(void)
{
	static float I_fs=0, V_fs=420.864, temp=0;
	static long result=0;
	unsigned int CT = CalibrationData.CT_RANGE;
	WORD ex_volt_ratio = CalibrationData.EXT_VOLT_RATIO;
	char LSB=0;
	BYTE checksum = 0;
	
//	PowerOpen();	//2012-08-10 Liz: Removed. Dont need to call this.
	
	if(CT==0)	// Initialize if there's no data stored in EEPROM
	{
		CalibrationData.CT_RANGE = 100;
		CT = 100;
	}
	if(ex_volt_ratio==0)	// Initialize if there's no data stored in EEPROM
	{
		CalibrationData.EXT_VOLT_RATIO = 1;
		ex_volt_ratio = 1;
	}
	
	CT *= 2;
	
	// Calculate full scaled current
	I_fs = ((float)CT)*1024/1000;
	
	// Calculate VOLT_CC
	temp = V_fs/256;
	temp = temp*ex_volt_ratio;
	LSB = FindLSB(temp, 0);
	Set_div(LSB, 1, 1);		// Reassign value for POWER_REGISTERS_DIVIDE_BY_A[];
	Set_div(LSB, 10, 0);	// Reassign value for POWER_REGISTERS_DIVIDE_BY_M[];

	// Calculate AMP_CC
	temp = I_fs/256;
	LSB = FindLSB(temp, 1);
	Set_div(LSB, 2, 1);		// Reassign value for POWER_REGISTERS_DIVIDE_BY_A[];
	Set_div(LSB, 11, 0);	// Reassign value for POWER_REGISTERS_DIVIDE_BY_M[];
	
	// Calculate ENR_CC
	// Include Ratio 100:1 (11kV->110V) or 30:1 (3.3kV->110) for HT meter
	temp = 0;
	temp = (I_fs*V_fs*88.9)/1000000000;
	temp = temp*65536;
	temp = temp*ex_volt_ratio;
	//temp = temp*100;	// Ratio for HT meter
	LSB = FindLSB(temp, 2);
	Set_div(LSB+3, 10, 1);	// Reassign value for POWER_REGISTERS_DIVIDE_BY_A[];
	Set_div(LSB+3, 11, 1);	
	Set_div(LSB+3, 12, 1);	
	
	// Calculate PWR_CC
	// Include Ratio 100:1 (11kV->110V) or 30:1 (3.3kV->110) for HT meter
	temp = 0;
	temp = (I_fs*V_fs)/65536;
	temp = temp*ex_volt_ratio;
	//temp = temp*100;	// Ratio for HT meter
	LSB = FindLSB(temp, 3);
	Set_div(LSB+3, 7, 1);	// Reassign value for POWER_REGISTERS_DIVIDE_BY_A[];
	Set_div(LSB+3, 8, 1);
	Set_div(LSB+3, 9, 1);
	Set_div(LSB+3, 13, 1);	// 2012-07-19 Liz: Updated for Max Demand
	Set_div(LSB+3, 12, 0);	// Reassign value for POWER_REGISTERS_DIVIDE_BY_M[];
	Set_div(LSB+3, 13, 0);
	Set_div(LSB+3, 14, 0);
	
	CalibrationData.Flags.bIsModified = 1;
	checksum = PowerCalculateCALIBChecksum();
	
	if(checksum != CalibrationData.CHECK_SUM)
	{
		CalibrationData.Flags.bChecksumChanged = 1;
		CalibrationData.CHECK_SUM = checksum;
	}	
	
	//CalibrationData.CHECK_SUM = PowerCalculateCALIBChecksum();	// 2012-10-03 Liz: Calculate checksum
	
	//2012-08-10 Liz: Write new settings calib values to MAXQ
	PowerWriteCALBToMAXQ();
//	PowerClose();	//2012-08-10 Liz: Removed. Dont need to call this.
}

void VBatVoltageDetection(void)// 2014-2-21 CL added
{	
	unsigned int ADCresult;
	float voltage = 0;
	
	char msg[3] = {(BYTE)MMT_BOT_VBATVOLTAGE,0,0};  

	//configure RA0 as analog input
	ANSELbits.ANS0 = 1;		//Disable Digital input buffer
	VBatLevel_TRIS = 1;		//Configure PORTA.RA0 as a input 
	
	//Configure the ADC module:
	OpenADC(ADC_FOSC_64 & ADC_LEFT_JUST & ADC_20_TAD, ADC_CH0 & ADC_INT_OFF & 	ADC_REF_VDD_VSS, ADC_1ANA);
		
	Delay10TCYx(4);
	ConvertADC();
	while(BusyADC()){}
	ADCresult = ReadADC();
	voltage = 3.3*(ADCresult>>6)/1023;		//2014-04-01(LIZ). Need to cast to float before doing calculation
	
	CloseADC();

	//if (voltage >= 3)		
	//{
	//	msg[1] = ADCresult>>8;		//ADRESH
	//	msg[2] = ADCresult;			//ADRESL			
	//	MCUSendString(3, &msg[0]);	
	//}	
	if (voltage >= 3)		
		msg[1] = 'H';	// Battery is sufficient to run for more than 2 hours
	else
		msg[1] = 'L';	// Battery is sufficient to run for less than 2 hours
	MCUSendString(2, &msg[0]);	
}

/*------------------------------------------------------------------
	This function get results from AutoCalibration() to caluclate 
		decimal place number
------------------------------------------------------------------*/
#if defined(THREE_PHASE_BOTTOM_BOARD) || defined(SINGLE_PHASE_BOTTOM_BOARD)
char FindLSB(float value, unsigned char reg)
{
	char i = 0, r=0;
	
	// Make sure the units conversion coefficients > 3100
	if(value > 35535)
	{
		while(value > 35535)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                              
		{
			value /= 10;
			i--;
		}
	}
	else
	{
		while(value < 3100)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                              
		{
			value *= 10;
			i++;
		}
	}
	
	// Save new calibration value to EEPROM.						
	*((int *)EEPROM_CALIBRATION_DATA[reg]) = (unsigned int)value;
	
	return i;
}

/*------------------------------------------------------------------
	2012-05-05 Liz added 
	This function writes calibration values to MAXQ
------------------------------------------------------------------*/
void PowerWriteCALBToMAXQ(void)
{
	PowerOpen();
	{
		char k = 0, r=0;
		unsigned long hresults = 0, lresults = 0;
		BYTE retries1 = 0, retries2 = 0;
		static BOOL is_correct = FALSE;
		
		for( k=0; k<POWER_CALIBRATION_QUEUE_SIZE; k++ )
		{
			is_correct = FALSE;
			
			// 2013-11-22 Liz. Need to check if voltage gain or current gain is bigger then 0x7FFF
			if(k >= 4 && k <= 9)
			{
				// These values are voltage gain or current gain of 3 phase A,B,C.
				unsigned int temp = *((int *)EEPROM_CALIBRATION_DATA[k]);
				
				// Check and make sure that value <= 0x7FFF
				if(temp > 32767)
				{
					*((int *)EEPROM_CALIBRATION_DATA[k]) = 32767;
				}
				else if(temp < 1000)
				{
					// Check and make sure that value >= 1000
					*((int *)EEPROM_CALIBRATION_DATA[k]) = 1000;
				}
			}	
			//////////////////////////////////////////////
			
			do{
				// 2013-03-11 Liz: write calib value to MAXQ
				Power_WriteToMAXQ(POWER_CALIBRATION_QUEUE[k], 2, *((int *)EEPROM_CALIBRATION_DATA[k]), 3);
				r = 0;
				
				// 2013-03-11 Liz: read back calib value from MAXQ			
				do{
					r = PowerSendReadCommand(POWER_CALIBRATION_QUEUE[k], 2, &hresults, &lresults);
					
					if(r!=1)
					{
						// When there is an error talking to MAXQ3180...
						#if (CLOCK_SPEED == 32000000)
							Delay100TCYx(20);	//2012-10-15 Liz: changed from 10 to 20. Requesting too fast makes MAXQ unable to response
						#else
							#error "No valid clock speed defined."
						#endif
					}
					retries1++;	
				}while(retries1<3 && r!=1);
			
				// Verify if value is write correctly to MAXQ
				if(lresults != *((int *)EEPROM_CALIBRATION_DATA[k]))
					retries2++;
				else
					is_correct = TRUE;
				
			}while(is_correct != TRUE && retries2<3);	
			
			// 2013-03-11 Liz: If fail to write 3 times, reset MAXQ and reset microcontroller
			// *Important*: Should reset bottom board after that since 
			//		MAXQ need to be re-configured ater reset
			if(retries2 >= 3 && is_correct != TRUE)
			{
				POWER_RESET_LAT = 1;	// Keep RESET pin high	
				Delay100TCYx(1);
				POWER_RESET_LAT = 0;
				Delay100TCYx(1);
				POWER_RESET_LAT = 1;
				SaveCalibrationData();	// Save Calibration data before reset
				ResetBot();
			}
		}
	}	
	PowerClose();
}

/*-----------------------------------------------------------------------
	2012-07-19 Liz Added
	This function update Max Demand
	2014-04-04 Liz modified. Changed size of max demand from 8 bytes to 4 bytes
------------------------------------------------------------------------*/
void Update_Max_Min(void)
{
	BYTE phase=0, i=0, div=0;
	long hvalue=0, lvalue=0, hmax=0, lmax=0, app_pow = 0, pf = 0;
	static int * jj;
	static long * kk, * max_value, *ptr;	

	for(phase=1; phase<4; phase++)
	{
		jj = (int *)POWER_READINGS[phase];
		kk = (long *)jj[7];
		max_value = (long *)MAX_MIN_STORAGE_QUEUE[phase-1][0];
	
		// 2013-11-12 Liz. Check if refenrece is 0
		if(kk == 0)	continue;
		if(max_value == 0)	continue;
		//
	
		hvalue = kk[1];
		lvalue = kk[0];
		hvalue = ((hvalue << 16) | (lvalue >> 16));
		hmax = *max_value;	//2014-04-04 Liz edited. MAX_STORAGE size was reduced from 8 to 4 BYTEs
		
		// MaxDemand list will be refresh every 30mins. 
		//	If it's time to refresh, max demand value is updated with current real power value
//		if(RefreshMaxDemand == 1)
//		{
//			//max_value[0] = kk[0];
//			//max_value[1] = kk[1];
//			max_value[0] = hvalue;	//2013-05-05 Liz edited. MAX_STORAGE size was reduced from 8 to 4 BYTEs
//			if(phase >= 3)
//				RefreshMaxDemand = 0;
//			continue;
//		}	
		
		// Compare with current value and update	
		if(hvalue > hmax)
		{
			BYTE * eep_mvalue = (BYTE *)MAX_MIN_STORAGE_QUEUE[phase-1][0];	
			float ratio = 0, calc_pow = 0;
			//current = (long *)jj[9];
			
			// 2013-11-12 Liz. Check if refenrece is 0
			if(eep_mvalue == 0)	continue;
			//
			
			// Get apparent power
			ptr = (long *)jj[8];
			
			// 2013-11-12 Liz. Check if refenrece is 0
			if(ptr == 0)	continue;
			//
			
			lvalue = ptr[0];
			lmax = ptr[1];
			app_pow = ((lmax << 16) | (lvalue >> 16));
			
			// Get power factor
			ptr = (long *)jj[0];
			
			// 2013-11-12 Liz. Check if refenrece is 0
			if(ptr == 0)	continue;
			//
			
			pf = ptr[0];

			// Calculate power
			ratio = ((float)pf)/16384.00f;
			calc_pow = ((float)app_pow) * ratio;
			
			// Compare with real power
			// NOTE: make sure there is no division by 0 here
			if(hvalue != 0)
				ratio = (float)calc_pow/(float)hvalue;
			
			// If real power which calculated from apparent power is almost equal to
			//		real power read from MaxQ then update max_demand. Ignore if not, 
			//		since it's maybe a fake peak.		
			if( ratio >= 0.8f && ratio <= 1.2f)
			{
				// Update max_value	
				*max_value = (hvalue&0xFFFFFFFF);	//2014-04-04 Liz edited. MAX_STORAGE size was reduced from 8 to 4 BYTEs
				
				// Write to eeprom
				while( Read_b_eep(120) != 0x80u )
				{
					Write_b_eep(120, 0x80u);  //0
					Busy_eep();
				}
				
				//2014-04-04 Liz edited. MAX_STORAGE size was reduced from 8 to 4 BYTEs
				for( i=0; i<4; i++ )
				{
					// while loop to make sure the correct value was written in.
					while( Read_b_eep(i+125+(phase-1)*4) != eep_mvalue[i] )
					{
						Write_b_eep(i+125+(phase-1)*4, eep_mvalue[i]);  //0
						Busy_eep();
					}
				}
			}
		}
	}
}

/*-------------------------------------------------------------------
	This function will reset meter and communicator ervery 12hours
	Note: This function is temporaly use to prevent Bot_tob comm hangs
		and unable to detect IP. Better to remove once problem are fixed.
-------------------------------------------------------------------*/
void ResetBot(void)
{	
	// 2013-09-18 Liz added.
	// Save datetime before reset
	SaveRTCCToEEPROM();
				
	// Reset bot 
	Reset();
}	

//void BotUpdateTopDateTime(void)
//{
//	char s[10];
//	#ifdef APP_USE_MCU
//	{
//		MCUTasks();
//	}
//	#endif
//	
//	//2014-02-26 : CL added: Notify top board the IsGetDatetime Flag
//	if (CalibrationData.Flag2.bIsGetDatetime == 1)
//	{	
//		short int * ptr = &CalibrationData.Flag2;
//		s[0] = (BYTE)MMT_BOT_UPDATE_TOP_DATETIME;
//		s[1] = *ptr;
//		MCUSendString(2, &s[0]);
//	}
//}

/*-------------------------------------------------------------------------
	2014-02-26 Liz added.
	Add raw energy stored in 
--------------------------------------------------------------------------*/
void Add_raw_energy(unsigned long * net_raw_ENR, unsigned long * net_ENR_OVFC1, BYTE phase, BYTE reg, unsigned char b_compute_neg)
{	
	//raw_energy_storage_2
	unsigned long * raw_POS_ENERGY = (unsigned long *)&raw_energy_storage_2; 	
	unsigned long * raw_NEG_ENERGY = (unsigned long *)&raw_energy_storage_2; 

	// 2014-05-07 Liz. Make sure no reference to addr 0x0000
	if(raw_POS_ENERGY == 0 || raw_NEG_ENERGY == 0)	return;
	
	// Get positve component pointer of energy stored in eep
	raw_POS_ENERGY += (phase - 1)*5 + reg*2; 
		
	// Get negative component pointer of energy stored in eep
	raw_NEG_ENERGY += (phase - 1)*5 + reg*2 + 1;

	#if defined(APP_USE_NEGATIVE_ENERGY)
	if(b_compute_neg == 1 && reg == 0)
	{
		// Include negative component in NET_REAL_ENERGY
		
		// Firstly, add positive component of energy stored in eep
		// Check if it may overflow
		if(0xFFFFFFFF - *net_raw_ENR < *raw_POS_ENERGY)
		{
			// It's overflow, need to increase NET_OVFC1
			*net_ENR_OVFC1++;
			
			// Put the remain in NET_RAW_ENR
			*net_raw_ENR = *raw_POS_ENERGY - (0xFFFFFFFF - *net_raw_ENR); 
		}
		else
			*net_raw_ENR += *raw_POS_ENERGY;
			
		// Next, add negative component of energy stored in eep  
		// Check if positive raw value is bigger than negative one.
		if(*net_raw_ENR < *raw_NEG_ENERGY)
		{
			if(*net_ENR_OVFC1>0)
			{
				*net_ENR_OVFC1--;
				*net_raw_ENR = 0xFFFFFFFF - *raw_NEG_ENERGY + *net_raw_ENR;
			}
			else
				*net_raw_ENR = 0;
		}	
		else
			*net_raw_ENR -= *raw_NEG_ENERGY;	  
	}	
	else if(b_compute_neg == 1 && reg == 1)
	{
		// Include negative component in NET_REACTIVE_ENERGY
		// ***Note: for reactive energy, final vlaue is mainly contributed by negative value.
		//			However value is computed as ABSOLUTE VALUE due to some limitation. 				
		
		// Firstly, add negative component of energy stored in eep
		// Check if it may overflow
		if(0xFFFFFFFF - *net_raw_ENR < *raw_NEG_ENERGY)
		{
			// It's overflow, need to increase NET_OVFC1
			*net_ENR_OVFC1++;
			
			// Put the remain in NET_RAW_ENR
			*net_raw_ENR = *raw_NEG_ENERGY - (0xFFFFFFFF - *net_raw_ENR); 
		}
		else
			*net_raw_ENR += *raw_NEG_ENERGY;
			
		// Next, add negative component of energy stored in eep  
		// Check if positive raw value is bigger than negative one.
		if(*net_raw_ENR < *raw_POS_ENERGY)
		{
			if(*net_ENR_OVFC1>0)
			{
				*net_ENR_OVFC1--;
				*net_raw_ENR = 0xFFFFFFFF - *raw_POS_ENERGY + *net_raw_ENR;
			}
			else
				*net_raw_ENR = 0;
		}	
		else
			*net_raw_ENR -= *raw_POS_ENERGY;
	}
	//else
	#else
	{
		// Only include positive energy component in calculation
		
		// Check if it may overflow
		if(0xFFFFFFFF - *net_raw_ENR < *raw_POS_ENERGY)
		{
			// It's overflow, need to increase NET_OVFC1
			*net_ENR_OVFC1 += 1;
			
			// Put the remain in NET_RAW_ENR
			*net_raw_ENR = *raw_POS_ENERGY - (0xFFFFFFFF - *net_raw_ENR); 
		}
		else
			*net_raw_ENR += *raw_POS_ENERGY;
	}
	#endif			
}

/*-------------------------------------------------------------------------
	2014-04-01 Liz added.
	Save raw energy into eeprom before go into sleep mode 
--------------------------------------------------------------------------*/
void SaveRawEnergyToEEP(void)
{
	unsigned int i = 0;
	BYTE count = 0, checksum = 0;	//2014-06-17 Liz
	unsigned long * real_time_raw = (unsigned long *)&raw_energy_storage;
	unsigned long * eep_raw = (unsigned long *)&raw_energy_storage_2;
	unsigned int * ovfc1 = (unsigned int *)&EnrOvfCounter1;
	BYTE * enr = (BYTE *)&raw_energy_storage;
	
	// Check 0 reference
	if(real_time_raw == 0 || eep_raw == 0 || ovfc1 == 0)	return;
	if(enr == 0)		return;
	
	// Add real_time_raw_enery and eep_stored_raw_energy 
	for(count = 0; count < 15; count++)
	{		
		// Add real_time_raw_enery and eep_stored_raw_energy 
		// Check if it may overflow
		if(0xFFFFFFFF - *real_time_raw < *eep_raw)
		{
			// It's overflow, need to increase OVFC1
			*ovfc1 += 1;
			
			// Put the remain in real_time_raw
			*real_time_raw = *eep_raw - (0xFFFFFFFF - *real_time_raw); 
		}
		else
			*real_time_raw += *eep_raw;
			
		// Get pointer of energy stored in real_time_energy_storage
		real_time_raw++;
		
		// Get pointer of energy stored in eeprom_energy_storage
		eep_raw++;
		
		// Get pointer of ovfc1 stored in EnrOvfcCounter1_storage
		ovfc1++;
	}		
			
	while( Read_b_eep(160) != 0x70u )	// 160
	{
		Write_b_eep(160, 0x70u);  		// 160
		Busy_eep();
	}
			
	// Write raw energy to eeprom
	for( i=0; i<sizeof(raw_energy_storage); i++ )
	{
		// while loop to make sure the correct value was written in.
		while( Read_b_eep(i+161) != (unsigned char)enr[i] )		// 161
		{
			Write_b_eep(i+161, (unsigned char)enr[i]);  		// 161
			Busy_eep();
		}
//		checksum += enr[i];	//2014-06-17 Liz
	}

	// 2014-06-17 Liz. Save checksum of raw energy.
//	while( Read_b_eep(230) != checksum )		//230
//	{
//		Write_b_eep(230, checksum);  			//230
//		Busy_eep();
//	}
	
	// 2014-06-19 Liz. Must clear buffer after save.
	memset(&raw_energy_storage, 0, sizeof(raw_energy_storage));
	memset(&raw_energy_storage_2, 0, sizeof(raw_energy_storage_2));
	//
	
	// Write new overflow counter 1 set to eeprom
	SaveCalibrationData();	
}	

/*-----------------------------------------------------------
	2014-03-31(LIZ). Check MAXQ global status flag. 
------------------------------------------------------------*/
void Check_MAXQGlobalStatus(void)
{
	BYTE global_status = 0;
	
	memcpy(&global_status, &reading_storage_1.GLOBAL_STATUS, 1);
	
	if(global_status != 0)
	{
		// Check if MAXQ have power-on-reset or watch dog time out reset.
		if(global_status & 0x20 || global_status & 0x10)
		{ 
			// Need to re-configure MAXQ and calibration settings. 	
			ResetSPIDevices();
			InitMAXQ();			
		}	
		
		// Need to clear MAXQ global status flag
		// Actually the flag has been cleared in InitMAXQ()
	}	
}
	
#endif
