/*********************************************************************
 *
 *  Application to Demo HTTP2 Server
 *  Support for HTTP2 module in Microchip TCP/IP Stack
 *	 -Implements the application 
 *	 -Reference: RFC 1002
 *
 *********************************************************************
 * FileName:        CustomHTTPApp.c
 * Dependencies:    TCP/IP stack
 * Processor:       PIC18, PIC24F, PIC24H, dsPIC30F, dsPIC33F, PIC32
 * Compiler:        Microchip C32 v1.05 or higher
 *					Microchip C30 v3.12 or higher
 *					Microchip C18 v3.30 or higher
 *					HI-TECH PICC-18 PRO 9.63PL2 or higher
 * Company:         Microchip Technology, Inc.
 *
 * Software License Agreement
 *
 * Copyright (C) 2002-2009 Microchip Technology Inc.  All rights
 * reserved.
 *
 * Microchip licenses to you the right to use, modify, copy, and
 * distribute:
 * (i)  the Software when embedded on a Microchip microcontroller or
 *      digital signal controller product ("Device") which is
 *      integrated into Licensee's product; or
 * (ii) ONLY the Software driver source files ENC28J60.c, ENC28J60.h,
 *		ENCX24J600.c and ENCX24J600.h ported to a non-Microchip device
 *		used in conjunction with a Microchip ethernet controller for
 *		the sole purpose of interfacing with the ethernet controller.
 *
 * You should refer to the license agreement accompanying this
 * Software for additional information regarding your rights and
 * obligations.
 *
 * THE SOFTWARE AND DOCUMENTATION ARE PROVIDED "AS IS" WITHOUT
 * WARRANTY OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT
 * LIMITATION, ANY WARRANTY OF MERCHANTABILITY, FITNESS FOR A
 * PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * MICROCHIP BE LIABLE FOR ANY INCIDENTAL, SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES, LOST PROFITS OR LOST DATA, COST OF
 * PROCUREMENT OF SUBSTITUTE GOODS, TECHNOLOGY OR SERVICES, ANY CLAIMS
 * BY THIRD PARTIES (INCLUDING BUT NOT LIMITED TO ANY DEFENSE
 * THEREOF), ANY CLAIMS FOR INDEMNITY OR CONTRIBUTION, OR OTHER
 * SIMILAR COSTS, WHETHER ASSERTED ON THE BASIS OF CONTRACT, TORT
 * (INCLUDING NEGLIGENCE), BREACH OF WARRANTY, OR OTHERWISE.
 *
 *
 * Author               Date    Comment
 *~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * Elliott Wood     	6/18/07	Original
 ********************************************************************/
#define __CUSTOMHTTPAPP_C

#include "AppConfig.h"
#include "TCPIPConfig.h"
#include <math.h>

#if defined(STACK_USE_HTTP2_SERVER)

#include "TCPIP Stack/TCPIP.h"
#include "MainDemo.h"		// Needed for SaveAppConfig() prototype
#include "../Common/mcu.h"
#include "../Common/utility.h"
#include "TCPIP Stack/AutoIP.h"
#ifdef APP_USE_ZIGBEE
	#include "zigbee.h"
#endif

void HTTPPrintMAC(MAC_ADDR mac);
void HTTPPrintIP(IP_ADDR ip);

/****************************************************************************
  Section:
	Function Prototypes and Memory Globaliz
	ers
  ***************************************************************************/
#if defined(HTTP_USE_POST)
	#if defined(USE_LCD)
		static HTTP_IO_RESULT HTTPPostLCD(void);
	#endif
	#if defined(STACK_USE_HTTP_MD5_DEMO)
		#if !defined(STACK_USE_MD5)
			#error The HTTP_MD5_DEMO requires STACK_USE_MD5
		#endif
		static HTTP_IO_RESULT HTTPPostMD5(void);
	#endif
	#if defined(APP_USE_HUB)
		static HTTP_IO_RESULT HTTPPostIPClients(void);
	#endif
	static HTTP_IO_RESULT HTTPSuperUserSettings(void);
	static HTTP_IO_RESULT HTTPPostAnalogSettings(BYTE * filename);
	static HTTP_IO_RESULT HTTPPostMaxDemandSettings(BYTE * filename);	//2012-07-25 Liz: Added max demand settings
	#if defined(STACK_USE_HTTP_APP_RECONFIG)
		extern APP_CONFIG AppConfig;
		static HTTP_IO_RESULT HTTPPostConfig(void);
		#if defined(STACK_USE_SNMP_SERVER)
		static HTTP_IO_RESULT HTTPPostSNMPCommunity(void);
		#endif
	#endif
	#if defined(STACK_USE_HTTP_EMAIL_DEMO) || defined(STACK_USE_SMTP_CLIENT)
		#if !defined(STACK_USE_SMTP_CLIENT)
			#error The HTTP_EMAIL_DEMO requires STACK_USE_SMTP_CLIENT
		#endif
		static HTTP_IO_RESULT HTTPPostEmail(void);
	#endif
	#if defined(STACK_USE_DYNAMICDNS_CLIENT)
		static HTTP_IO_RESULT HTTPPostDDNSConfig(void);
	#endif
#endif

// RAM allocated for DDNS parameters
#if defined(STACK_USE_DYNAMICDNS_CLIENT)
	static BYTE DDNSData[100];
#endif

// Sticky status message variable.
// This is used to indicated whether or not the previous POST operation was 
// successful.  The application uses these to store status messages when a 
// POST operation redirects.  This lets the application provide status messages
// after a redirect, when connection instance data has already been lost.
static BOOL lastSuccess = FALSE;

// Stick status message variable.  See lastSuccess for details.
static BOOL lastFailure = FALSE;

static unsigned int para_print_count = 0;
/****************************************************************************
  Section:
	Authorization Handlers
  ***************************************************************************/
  
/*****************************************************************************
  Function:
	BYTE HTTPNeedsAuth(BYTE* cFile)
	
  Internal:
  	See documentation in the TCP/IP Stack API or HTTP2.h for details.
  ***************************************************************************/
#if defined(HTTP_USE_AUTHENTICATION)
BYTE HTTPNeedsAuth(BYTE* cFile)
{
	// If the filename begins with the folder "protect", then require auth
	if(memcmppgm2ram(cFile, (ROM void*)"protect", 7) == 0)
		return 0x00;		// Authentication will be needed later

	// If the filename begins with the folder "snmp", then require auth
	if(memcmppgm2ram(cFile, (ROM void*)"snmp", 4) == 0)
		return 0x00;		// Authentication will be needed later

	#if defined(HTTP_MPFS_UPLOAD_REQUIRES_AUTH)
	if(memcmppgm2ram(cFile, (ROM void*)"mpfsupload", 10) == 0)
		return 0x00;
	#endif
	
	// 2012-05-11(Eric) - Allows super user authentication usable by Anacle staff only.
	if(memcmppgm2ram(cFile, (ROM void*)"super", 5) == 0)
		return 0x01;

	// You can match additional strings here to password protect other files.
	// You could switch this and exclude files from authentication.
	// You could also always return 0x00 to require auth for all files.
	// You can return different values (0x00 to 0x79) to track "realms" for below.

	return 0x80;			// No authentication required
}
#endif

/*****************************************************************************
  Function:
	BYTE HTTPCheckAuth(BYTE* cUser, BYTE* cPass)
	
  Internal:
  	See documentation in the TCP/IP Stack API or HTTP2.h for details.
  ***************************************************************************/
#if defined(HTTP_USE_AUTHENTICATION)
BYTE HTTPCheckAuth(BYTE* cUser, BYTE* cPass)
{
	if(curHTTP.isAuthorized == 0u)
	{
		// 2012-04-26(Eric) - Removed hard coded password.
		// 2012-05-11(Eric) - User authentication.
		if(strcmppgm2ram((char *)cUser,(ROM char *)"user") == 0
			&& strcmp((char *)cPass, (char *)AppConfig.Password) == 0)
			return 0x80;		// We accept this combination
	}
	else if(curHTTP.isAuthorized == 0x01u)
	{
		// 2012-05-11(Eric) - Validate super user password.
		// Hard-coded for now. Will modify later.
		// SUPER USER authentication. Only known to Anacle staff.
		if(strcmppgm2ram((char *)cUser,(ROM char *)"super") == 0
			&& strcmppgm2ram((char *)cPass, (ROM char *)"hc0811") == 0)
			return 0x81;		// We accept this combination
	}
	
	// You can add additional user/pass combos here.
	// If you return specific "realm" values above, you can base this 
	//   decision on what specific file or folder is being accessed.
	// You could return different values (0x80 to 0xff) to indicate 
	//   various users or groups, and base future processing decisions
	//   in HTTPExecuteGet/Post or HTTPPrint callbacks on this value.
	
	return 0x00;			// Provided user/pass is invalid
}
#endif

/****************************************************************************
  Section:
	GET Form Handlers
  ***************************************************************************/
  
/*****************************************************************************
  Function:
	HTTP_IO_RESULT HTTPExecuteGet(void)
	
  Internal:
  	See documentation in the TCP/IP Stack API or HTTP2.h for details.
  ***************************************************************************/
HTTP_IO_RESULT HTTPExecuteGet(void)
{
	BYTE *ptr;
	BYTE filename[20];
	
	// Load the file name
	// Make sure BYTE filename[] above is large enough for your longest name
	MPFSGetFilename(curHTTP.file, filename, 20);
	
	// If its the forms.htm page
	if(!memcmppgm2ram(filename, "forms.htm", 9))
	{

	}
	
	// If it's the LED updater file
	else if(!memcmppgm2ram(filename, "cookies.htm", 11))
	{
		// This is very simple.  The names and values we want are already in
		// the data array.  We just set the hasArgs value to indicate how many
		// name/value pairs we want stored as cookies.
		// To add the second cookie, just increment this value.
		// remember to also add a dynamic variable callback to control the printout.
		curHTTP.hasArgs = 0x01;
	}
	
	return HTTP_IO_DONE;
}


/****************************************************************************
  Section:
	POST Form Handlers
  ***************************************************************************/
#if defined(HTTP_USE_POST)

/*****************************************************************************
  Function:
	HTTP_IO_RESULT HTTPExecutePost(void)
	
  Internal:
  	See documentation in the TCP/IP Stack API or HTTP2.h for details.
  ***************************************************************************/
HTTP_IO_RESULT HTTPExecutePost(void)
{
	// Resolve which function to use and pass along
	BYTE filename[20];
	
	// Load the file name
	// Make sure BYTE filename[] above is large enough for your longest name
	MPFSGetFilename(curHTTP.file, filename, sizeof(filename));
	
#if defined(USE_LCD)
	if(!memcmppgm2ram(filename, "forms.htm", 9))
		return HTTPPostLCD();
#endif

#if defined(STACK_USE_HTTP_MD5_DEMO)
	if(!memcmppgm2ram(filename, "upload.htm", 10))
		return HTTPPostMD5();
#endif

/**
 *  Custom.
 **/
#if defined(APP_USE_HUB)
	if(!memcmppgm2ram(filename, "index.htm", 9))
		return HTTPPostIPClients();
#endif

#if defined(STACK_USE_HTTP_APP_RECONFIG)
	if(!memcmppgm2ram(filename, "protect/config.htm", 18))
		return HTTPPostConfig();
	#if defined(STACK_USE_SNMP_SERVER)
	else if(!memcmppgm2ram(filename, "snmp/snmpconfig.htm", 19))
		return HTTPPostSNMPCommunity();
	#endif
#endif

	// 2012-05-02(Eric) - Added.
	if((!strcmppgm2ram((char*)filename, (ROM char*)"protect/compute.htm")) || (!strcmppgm2ram((char*)filename, (ROM char*)"protect/calib.htm")))
		return HTTPPostAnalogSettings(filename);
	// End added.
	
	// 2012-05-10(Eric) - Added.
	if(!strcmppgm2ram((char*)filename, (ROM char*)"super/index.htm"))
		return HTTPSuperUserSettings();
	// End added.
	
	// 2012-07-25 Liz: Added Max Demand settings
	if(!strcmppgm2ram((char*)filename, (ROM char*)"protect/md.htm"))
		return HTTPPostMaxDemandSettings(filename);
	// End Added

#if defined(STACK_USE_SMTP_CLIENT)
	if(!strcmppgm2ram((char*)filename, (ROM char*)"email/index.htm"))
		return HTTPPostEmail();
#endif
	
#if defined(STACK_USE_DYNAMICDNS_CLIENT)
	if(!strcmppgm2ram((char*)filename, "dyndns/index.htm"))
		return HTTPPostDDNSConfig();
#endif

	return HTTP_IO_DONE;
}

/*****************************************************************************
  Function:
	static HTTP_IO_RESULT HTTPPostLCD(void)

  Summary:
	Processes the LCD form on forms.htm

  Description:
	Locates the 'lcd' parameter and uses it to update the text displayed
	on the board's LCD display.
	
	This function has four states.  The first reads a name from the data
	string returned as part of the POST request.  If a name cannot
	be found, it returns, asking for more data.  Otherwise, if the name 
	is expected, it reads the associated value and writes it to the LCD.  
	If the name is not expected, the value is discarded and the next name 
	parameter is read.
	
	In the case where the expected string is never found, this function 
	will eventually return HTTP_IO_NEED_DATA when no data is left.  In that
	case, the HTTP2 server will automatically trap the error and issue an
	Internal Server Error to the browser.

  Precondition:
	None

  Parameters:
	None

  Return Values:
  	HTTP_IO_DONE - the parameter has been found and saved
  	HTTP_IO_WAITING - the function is pausing to continue later
  	HTTP_IO_NEED_DATA - data needed by this function has not yet arrived
  ***************************************************************************/
#if defined(USE_LCD)
static HTTP_IO_RESULT HTTPPostLCD1(void)
{
	BYTE* cDest;
	
	#define SM_POST_LCD_READ_NAME		(0u)
	#define SM_POST_LCD_READ_VALUE		(1u)
	
	switch(curHTTP.smPost)
	{
		// Find the name
		case SM_POST_LCD_READ_NAME:
		
			// Read a name
			if(HTTPReadPostName(curHTTP.data, HTTP_MAX_DATA_LEN) == HTTP_READ_INCOMPLETE)
				return HTTP_IO_NEED_DATA;

			curHTTP.smPost = SM_POST_LCD_READ_VALUE;
			// No break...continue reading value
		
		// Found the value, so store the LCD and return
		case SM_POST_LCD_READ_VALUE:
					
			// If value is expected, read it to data buffer,
			// otherwise ignore it (by reading to NULL)	
			if(!strcmppgm2ram((char*)curHTTP.data, (ROM char*)"lcd"))
				cDest = curHTTP.data;
			else
				cDest = NULL;
			
			// Read a value string
			if(HTTPReadPostValue(cDest, HTTP_MAX_DATA_LEN) == HTTP_READ_INCOMPLETE)
				return HTTP_IO_NEED_DATA;
			
			// If this was an unexpected value, look for a new name
			if(!cDest)
			{
				curHTTP.smPost = SM_POST_LCD_READ_NAME;
				break;
			}
			
			// Copy up to 32 characters to the LCD
			if(strlen((char*)cDest) < 32u)
			{
				memset(LCDText, ' ', 32);
				strcpy((char*)LCDText, (char*)cDest);
			}
			else
			{
				memcpy(LCDText, (void *)cDest, 32);
			}
			LCDUpdate();
			
			// This is the only expected value, so callback is done
			strcpypgm2ram((char*)curHTTP.data, (ROM void*)"/forms.htm");
			curHTTP.httpStatus = HTTP_REDIRECT;
			return HTTP_IO_DONE;
	}
	
	// Default assumes that we're returning for state machine convenience.
	// Function will be called again later.
	return HTTP_IO_WAITING;
}
#endif

static HTTP_IO_RESULT HTTPSuperUserSettings(void)
{
	APP_CONFIG newAppConfig;
	
	if(curHTTP.byteCount > TCPIsGetReady(sktHTTP) + TCPGetRxFIFOFree(sktHTTP))
		goto SAFailure;
	
	// Ensure that all data is waiting to be parsed.  If not, keep waiting for 
	// all of it to arrive.
	if(TCPIsGetReady(sktHTTP) < curHTTP.byteCount)
		return HTTP_IO_NEED_DATA;
	
	// Copy out the original AppConfig.
	memcpy(&newAppConfig, &AppConfig, sizeof(AppConfig));
	
	while(curHTTP.byteCount)
	{
		// Read a form field name
		if(HTTPReadPostName(curHTTP.data, 6) != HTTP_READ_OK)
			goto SAFailure;

		// Read a form field value
		if(HTTPReadPostValue(curHTTP.data + 6, sizeof(curHTTP.data)-6-2) != HTTP_READ_OK)
			goto SAFailure;
			
		if(!strcmppgm2ram((char*)curHTTP.data, (ROM char*)"rupw"))
			memcpypgm2ram((void*)AppConfig.Password, (ROM void*)DEFAULT_USER_PASSWORD, 4);
		
		if(memcmp(&newAppConfig, &AppConfig, sizeof(AppConfig)) != 0)
			SaveAppConfig();
		
		strcpypgm2ram((char*)curHTTP.data, (ROM char*)"index.htm?S");
		curHTTP.httpStatus = HTTP_REDIRECT;
		return HTTP_IO_DONE;
	}
	
SAFailure:
	lastFailure = TRUE;
	
	return HTTP_IO_DONE;
}
	
static HTTP_IO_RESULT HTTPPostAnalogSettings(BYTE * filename)
{
	CALIBRATION_VALUES stCalib, stCalib_new;
	BOOL compute_neg = FALSE;
	WORD modified_count;
	BOOL bIsChanged = FALSE;
	WORD temp;
	
	if(curHTTP.byteCount > TCPIsGetReady(sktHTTP) + TCPGetRxFIFOFree(sktHTTP))
		goto SettingFailure;
	
	// Ensure that all data is waiting to be parsed.  If not, keep waiting for 
	// all of it to arrive.
	if(TCPIsGetReady(sktHTTP) < curHTTP.byteCount)
		return HTTP_IO_NEED_DATA;
	
	if( !MCURequestToBOTTOMBoard(MMT_GET_CALIBRATION_DATA, &stCalib, sizeof(CALIBRATION_VALUES), TRUE, TRUE) )
		goto SettingFailure;
	
	// Duplicate the structure for comparison...
	memcpy(&stCalib_new, &stCalib, sizeof(CALIBRATION_VALUES));
	stCalib_new.Flags.bComputeWithNegativeEnergy = FALSE;
	
	while(curHTTP.byteCount)
	{
		// Read a form field name
		if(HTTPReadPostName(curHTTP.data, 6) != HTTP_READ_OK)
			goto SettingFailure;

		// Read a form field value
		if(HTTPReadPostValue(curHTTP.data + 6, sizeof(curHTTP.data)-6-2) != HTTP_READ_OK)
			goto SettingFailure;
		else temp = (WORD)atoi(curHTTP.data+6);
		
		if(!strcmppgm2ram((char*)curHTTP.data, (ROM char*)"cneg"))
			stCalib_new.Flags.bComputeWithNegativeEnergy = TRUE;
		else if(!strcmppgm2ram((char*)curHTTP.data, (ROM char*)"ismod"))
		{
			// Check if calibration values have changed since we last loaded the webpage.
			if( stCalib_new.LAST_MODIFIED != atoi(curHTTP.data+6) )
				bIsChanged = TRUE;
		}		
		else if(!strcmppgm2ram((char*)curHTTP.data, (ROM char*)"ct_r"))
			stCalib_new.CT_RANGE = temp;
		else if(!strcmppgm2ram((char*)curHTTP.data, (ROM char*)"vcc"))
			stCalib_new.VOLTCC = temp;
		else if(!strcmppgm2ram((char*)curHTTP.data, (ROM char*)"phAv"))
			stCalib_new.VOLT_GAIN_A = temp;
		else if(!strcmppgm2ram((char*)curHTTP.data, (ROM char*)"phAc"))
			stCalib_new.CURRENT_GAIN_A = temp;
		else if(!strcmppgm2ram((char*)curHTTP.data, (ROM char*)"phBv"))
			stCalib_new.VOLT_GAIN_B = temp;
		else if(!strcmppgm2ram((char*)curHTTP.data, (ROM char*)"phBc"))
			stCalib_new.CURRENT_GAIN_B = temp;
		else if(!strcmppgm2ram((char*)curHTTP.data, (ROM char*)"phCv"))
			stCalib_new.VOLT_GAIN_C = temp;
		else if(!strcmppgm2ram((char*)curHTTP.data, (ROM char*)"phCc"))
			stCalib_new.CURRENT_GAIN_C = temp;
		else if(!strcmppgm2ram((char*)curHTTP.data, (ROM char*)"exvr"))	// 2012-05-24 Liz: added external voltage ratio
			stCalib_new.EXT_VOLT_RATIO = temp;
	}	
	
	//
	if( bIsChanged ) goto SettingFailure;
	
	// Check if there are changes to the values.
	if(memcmp(&stCalib, &stCalib_new, sizeof(CALIBRATION_VALUES)) != 0)
	{
		// There are changes... save it!
		MCURequestToBOTTOMBoard(MMT_SET_CALIBRATION_DATA_NEW, &stCalib_new, sizeof(CALIBRATION_VALUES), TRUE, TRUE);
		
		if( ((BYTE*)&stCalib_new)[0] != 'O' && ((BYTE*)&stCalib_new)[1] != 'K' )
			goto SettingFailure;
	}
	
	curHTTP.data[0] = '/';
	strcpy((char*)&curHTTP.data[1], (char*)filename);
	strcatpgm2ram((char*)curHTTP.data, (ROM char*)"?S");
	curHTTP.httpStatus = HTTP_REDIRECT;
	return HTTP_IO_DONE;

	
SettingFailure:
	lastFailure = TRUE;
	//strcpypgm2ram((char*)curHTTP.data, (ROM char*)"/protect/calib.htm");
	//curHTTP.httpStatus = HTTP_REDIRECT;
	
	return HTTP_IO_DONE;
}	

//2012-07-25 Liz: Added Max Demand settings
static HTTP_IO_RESULT HTTPPostMaxDemandSettings(BYTE * filename)
{
	//CALIBRATION_VALUES stCalib, stCalib_new;
	//BOOL compute_neg = FALSE;
	//WORD modified_count;
	char RefreshInterval[5] = "", RefreshInterval_new[5] = "";
	BOOL bIsChanged = FALSE;
	WORD len = 0;
	
	if(curHTTP.byteCount > TCPIsGetReady(sktHTTP) + TCPGetRxFIFOFree(sktHTTP))
		goto SettingFailure;
	
	// Ensure that all data is waiting to be parsed.  If not, keep waiting for 
	// all of it to arrive.
	if(TCPIsGetReady(sktHTTP) < curHTTP.byteCount)
		return HTTP_IO_NEED_DATA;
	
	if( !MCURequestToBOTTOMBoard(MMT_GET_MAX_DEMAND_REFRESH_INTERVAL, &RefreshInterval[0], 4, TRUE, TRUE) )
		goto SettingFailure;
	
	// Duplicate the structure for comparison...
	memcpy(&RefreshInterval_new[0], &RefreshInterval[0],MCUNewMessageLength+1);
	//stCalib_new.Flags.bComputeWithNegativeEnergy = FALSE;
	
	while(curHTTP.byteCount)
	{
		// Read a form field name
		if(HTTPReadPostName(curHTTP.data, 6) != HTTP_READ_OK)
			goto SettingFailure;

		// Read a form field value
		if(HTTPReadPostValue(curHTTP.data + 6, sizeof(curHTTP.data)-6-2) != HTTP_READ_OK)
			goto SettingFailure;
		//else temp = (WORD)strlen(curHTTP.data+6);	//(WORD)atoi(curHTTP.data+6);
		//else memcpy(&RefreshInterval_new[0], curHTTP.data+6, sizeof(curHTTP.data)-6-2);
		
		if(!strcmppgm2ram((char*)curHTTP.data, (ROM char*)"mdri"))
		{
			len = (WORD)strlen(curHTTP.data+6);
			memcpy(&RefreshInterval_new[0], curHTTP.data+6, len);
		}
	}	
	
	//
	//if( bIsChanged ) goto SettingFailure;
	
	// Check if there are changes to the values.
	if(memcmp(&RefreshInterval[0], &RefreshInterval_new[0], len) != 0)
	{
		// There are changes... save it!
		MCURequestToBOTTOMBoard(MMT_SET_MAX_DEMAND_REFRESH_INTERVAL, &RefreshInterval_new[0], len, TRUE, TRUE);
		
		if( RefreshInterval_new[0] != 'O' && RefreshInterval_new[1] != 'K' )
			goto SettingFailure;
	}
	
	curHTTP.data[0] = '/';
	strcpy((char*)&curHTTP.data[1], (char*)filename);
	strcatpgm2ram((char*)curHTTP.data, (ROM char*)"?S");
	curHTTP.httpStatus = HTTP_REDIRECT;
	return HTTP_IO_DONE;

	
SettingFailure:
	lastFailure = TRUE;
	//strcpypgm2ram((char*)curHTTP.data, (ROM char*)"/protect/calib.htm");
	//curHTTP.httpStatus = HTTP_REDIRECT;
	
	return HTTP_IO_DONE;
}	
//


#if defined(APP_USE_HUB)
static HTTP_IO_RESULT HTTPPostIPClients(void)
{
	BYTE *ptr;
	BYTE i;

	// Check to see if the browser is attempting to submit more data than we 
	// can parse at once.  This function needs to receive all updated 
	// parameters and validate them all before committing them to memory so that
	// orphaned configuration parameters do not get written (for example, if a 
	// static IP address is given, but the subnet mask fails parsing, we 
	// should not use the static IP address).  Everything needs to be processed 
	// in a single transaction.  If this is impossible, fail and notify the user.
	// As a web devloper, if you add parameters to AppConfig and run into this 
	// problem, you could fix this by to splitting your update web page into two 
	// seperate web pages (causing two transactional writes).  Alternatively, 
	// you could fix it by storing a static shadow copy of AppConfig someplace 
	// in memory and using it instead of newAppConfig.  Lastly, you could 
	// increase the TCP RX FIFO size for the HTTP server.  This will allow more 
	// data to be POSTed by the web browser before hitting this limit.
	if(curHTTP.byteCount > TCPIsGetReady(sktHTTP) + TCPGetRxFIFOFree(sktHTTP))
		goto ConfigFailure;
	
	// Ensure that all data is waiting to be parsed.  If not, keep waiting for 
	// all of it to arrive.
	if(TCPIsGetReady(sktHTTP) < curHTTP.byteCount)
		return HTTP_IO_NEED_DATA;
	
	// Read all browser POST data
	while(curHTTP.byteCount)
	{
		// Read a form field name
		if(HTTPReadPostName(curHTTP.data, 6) != HTTP_READ_OK)
			goto ConfigFailure;
			
		// Read a form field value
		if(HTTPReadPostValue(curHTTP.data + 6, sizeof(curHTTP.data)-6-2) != HTTP_READ_OK)
			goto ConfigFailure;
			
		// Parse the value that was read
		if(!strcmppgm2ram((char*)curHTTP.data, (ROM char*)"rep"))
		{
			// This does not check if the data is valid. Webpage have to validate that user inputs a number before sending the request.
			iplist.Val.repeat_interval = atoi((char*)curHTTP.data+6);
		}
		else
		{
			BYTE index;
			BYTE i;
	
			if( curHTTP.data[0] == 'I' && curHTTP.data[1] == 'P' )
			{
				index = atoi(&curHTTP.data[2]);
				index = (index-1)*10;
				if(!StringToIPAddress(curHTTP.data+6, &((NODE_INFO*)&iplist.v[index]).IPAddr))
					goto ConfigFailure;
			}
			else if( curHTTP.data[0] == 'M' && curHTTP.data[1] == 'C' )
			{
				index = atoi(&curHTTP.data[2]);
				index = ((index-1)*10);
				if(!StringToMACAddress(curHTTP.data+6, &((NODE_INFO*)&iplist.v[index]).MACAddr))
					goto ConfigFailure;
			}
		}
		
		// Mark as modified.
		iplist.Val.Flags.bIsModified = 1;
	}

	
	return HTTP_IO_DONE;


ConfigFailure:
	lastFailure = TRUE;
	strcpypgm2ram((char*)curHTTP.data, (ROM char*)"/index.htm");
	curHTTP.httpStatus = HTTP_REDIRECT;		

	return HTTP_IO_DONE;
}

#endif

/*****************************************************************************
  Function:
	static HTTP_IO_RESULT HTTPPostConfig(void)

  Summary:
	Processes the configuration form on config/index.htm

  Description:
	Accepts configuration parameters from the form, saves them to a
	temporary location in RAM, then eventually saves the data to EEPROM or
	external Flash.
	
	When complete, this function redirects to config/reboot.htm, which will
	display information on reconnecting to the board.

	This function creates a shadow copy of the AppConfig structure in 
	RAM and then overwrites incoming data there as it arrives.  For each 
	name/value pair, the name is first read to curHTTP.data[0:5].  Next, the 
	value is read to newAppConfig.  Once all data has been read, the new
	AppConfig is saved back to EEPROM and the browser is redirected to 
	reboot.htm.  That file includes an AJAX call to reboot.cgi, which 
	performs the actual reboot of the machine.
	
	If an IP address cannot be parsed, too much data is POSTed, or any other 
	parsing error occurs, the browser reloads config.htm and displays an error 
	message at the top.

  Precondition:
	None

  Parameters:
	None

  Return Values:
  	HTTP_IO_DONE - all parameters have been processed
  	HTTP_IO_NEED_DATA - data needed by this function has not yet arrived
  ***************************************************************************/
#if defined(STACK_USE_HTTP_APP_RECONFIG)
static HTTP_IO_RESULT HTTPPostConfig(void)
{
	APP_CONFIG newAppConfig;
	BYTE *ptr;
	BYTE i;

	// Check to see if the browser is attempting to submit more data than we 
	// can parse at once.  This function needs to receive all updated 
	// parameters and validate them all before committing them to memory so that
	// orphaned configuration parameters do not get written (for example, if a 
	// static IP address is given, but the subnet mask fails parsing, we 
	// should not use the static IP address).  Everything needs to be processed 
	// in a single transaction.  If this is impossible, fail and notify the user.
	// As a web devloper, if you add parameters to AppConfig and run into this 
	// problem, you could fix this by to splitting your update web page into two 
	// seperate web pages (causing two transactional writes).  Alternatively, 
	// you could fix it by storing a static shadow copy of AppConfig someplace 
	// in memory and using it instead of newAppConfig.  Lastly, you could 
	// increase the TCP RX FIFO size for the HTTP server.  This will allow more 
	// data to be POSTed by the web browser before hitting this limit.
	if(curHTTP.byteCount > TCPIsGetReady(sktHTTP) + TCPGetRxFIFOFree(sktHTTP))
		goto ConfigFailure;
	
	// Ensure that all data is waiting to be parsed.  If not, keep waiting for 
	// all of it to arrive.
	if(TCPIsGetReady(sktHTTP) < curHTTP.byteCount)
		return HTTP_IO_NEED_DATA;
	
	
	// Use current config in non-volatile memory as defaults
	#if defined(EEPROM_CS_TRIS)
		XEEReadArray(0x0001, (BYTE*)&newAppConfig, sizeof(AppConfig));
	#elif defined(SPIFLASH_CS_TRIS)
		SPIFlashReadArray(0x0001, (BYTE*)&newAppConfig, sizeof(AppConfig));
	#endif
	
	// Start out assuming that DHCP is disabled.  This is necessary since the 
	// browser doesn't submit this field if it is unchecked (meaning zero).  
	// However, if it is checked, this will be overridden since it will be 
	// submitted.
	newAppConfig.Flags.bIsDHCPEnabled = 0;


	// Read all browser POST data
	while(curHTTP.byteCount)
	{
		// Read a form field name
		if(HTTPReadPostName(curHTTP.data, 6) != HTTP_READ_OK)
			goto ConfigFailure;
			
		// Read a form field value
		if(HTTPReadPostValue(curHTTP.data + 6, sizeof(curHTTP.data)-6-2) != HTTP_READ_OK)
			goto ConfigFailure;
			
		// Parse the value that was read
		if(!strcmppgm2ram((char*)curHTTP.data, (ROM char*)"ip"))
		{// Read new static IP Address
			if(!StringToIPAddress(curHTTP.data+6, &newAppConfig.MyIPAddr))
				goto ConfigFailure;
				
			newAppConfig.DefaultIPAddr.Val = newAppConfig.MyIPAddr.Val;
		}
		else if(!strcmppgm2ram((char*)curHTTP.data, (ROM char*)"gw"))
		{// Read new gateway address
			if(!StringToIPAddress(curHTTP.data+6, &newAppConfig.MyGateway))
				goto ConfigFailure;
		}
		else if(!strcmppgm2ram((char*)curHTTP.data, (ROM char*)"sub"))
		{// Read new static subnet
			if(!StringToIPAddress(curHTTP.data+6, &newAppConfig.MyMask))
				goto ConfigFailure;

			newAppConfig.DefaultMask.Val = newAppConfig.MyMask.Val;
		}
		else if(!strcmppgm2ram((char*)curHTTP.data, (ROM char*)"dns1"))
		{// Read new primary DNS server
			if(!StringToIPAddress(curHTTP.data+6, &newAppConfig.PrimaryDNSServer))
				goto ConfigFailure;
		}
		else if(!strcmppgm2ram((char*)curHTTP.data, (ROM char*)"dns2"))
		{// Read new secondary DNS server
			if(!StringToIPAddress(curHTTP.data+6, &newAppConfig.SecondaryDNSServer))
				goto ConfigFailure;
		}
		else if(!strcmppgm2ram((char*)curHTTP.data, (ROM char*)"mac"))
		{
			// Read new MAC address
			WORD w;
			BYTE i;

			ptr = curHTTP.data+6;

			for(i = 0; i < 12u; i++)
			{// Read the MAC address
				
				// Skip non-hex bytes
				while( *ptr != 0x00u && !(*ptr >= '0' && *ptr <= '9') && !(*ptr >= 'A' && *ptr <= 'F') && !(*ptr >= 'a' && *ptr <= 'f') )
					ptr++;

				// MAC string is over, so zeroize the rest
				if(*ptr == 0x00u)
				{
					for(; i < 12u; i++)
						curHTTP.data[i] = '0';
					break;
				}
				
				// Save the MAC byte
				curHTTP.data[i] = *ptr++;
			}
			
			// Read MAC Address, one byte at a time
			for(i = 0; i < 6u; i++)
			{
				((BYTE*)&w)[1] = curHTTP.data[i*2];
				((BYTE*)&w)[0] = curHTTP.data[i*2+1];
				newAppConfig.MyMACAddr.v[i] = hexatob(*((WORD_VAL*)&w));
			}
		}
		else if(!strcmppgm2ram((char*)curHTTP.data, (ROM char*)"host"))
		{// Read new hostname
			FormatNetBIOSName(&curHTTP.data[6]);
			memcpy((void*)newAppConfig.NetBIOSName, (void*)curHTTP.data+6, 16);
		}
		else if(!strcmppgm2ram((char*)curHTTP.data, (ROM char*)"dhcp"))
		{// Read new DHCP Enabled flag
			if(curHTTP.data[6] == '1')
				newAppConfig.Flags.bIsDHCPEnabled = 1;
		}
		else if(!strcmppgm2ram((char*)curHTTP.data, (ROM char*)"pw"))
		{// Change password.
			if(strlen((BYTE*)curHTTP.data+6) > 0)
			{
				memcpy((void*)newAppConfig.Password, (void*)curHTTP.data+6, 4);
				newAppConfig.Flags.bIsPwSet = TRUE;
			}	
		}
	}


	// All parsing complete!  Save new settings and force a reboot			
	#if defined(EEPROM_CS_TRIS)
		XEEBeginWrite(0x0000);
		XEEWrite(0x70);  //2012-05-11(Eric) - Changed start byte of eeprom from 0x60 to 0x70
		XEEWriteArray((BYTE*)&newAppConfig, sizeof(AppConfig));
	#elif defined(SPIFLASH_CS_TRIS)
		SPIFlashBeginWrite(0x0000);
		SPIFlashWrite(0x60);
		SPIFlashWriteArray((BYTE*)&newAppConfig, sizeof(AppConfig));
	#endif
	
	// Set the board to reboot and display reconnecting information
	strcpypgm2ram((char*)curHTTP.data, (ROM char*)"/protect/reboot.htm?");
	memcpy((void*)(curHTTP.data+20), (void*)newAppConfig.NetBIOSName, 16);
	curHTTP.data[20+16] = 0x00;	// Force null termination
	for(i = 20; i < 20u+16u; i++)
	{
		if(curHTTP.data[i] == ' ')
			curHTTP.data[i] = 0x00;
	}		
	curHTTP.httpStatus = HTTP_REDIRECT;	
	
	return HTTP_IO_DONE;


ConfigFailure:
	lastFailure = TRUE;
	strcpypgm2ram((char*)curHTTP.data, (ROM char*)"/protect/config.htm");
	curHTTP.httpStatus = HTTP_REDIRECT;		

	return HTTP_IO_DONE;
}

#if defined(STACK_USE_SNMP_SERVER)
static HTTP_IO_RESULT HTTPPostSNMPCommunity(void)
{
	BYTE vCommunityIndex;
	BYTE *dest;

	#define SM_CFG_SNMP_READ_NAME	(0u)
	#define SM_CFG_SNMP_READ_VALUE	(1u)
	
	switch(curHTTP.smPost)
	{
		case SM_CFG_SNMP_READ_NAME:
			// If all parameters have been read, end
			if(curHTTP.byteCount == 0u)
			{
				SaveAppConfig();
				return HTTP_IO_DONE;
			}
		
			// Read a name
			if(HTTPReadPostName(curHTTP.data, sizeof(curHTTP.data)-2) == HTTP_READ_INCOMPLETE)
				return HTTP_IO_NEED_DATA;
				
			// Move to reading a value, but no break
			curHTTP.smPost = SM_CFG_SNMP_READ_VALUE;
			
		case SM_CFG_SNMP_READ_VALUE:
			// Read a value
			if(HTTPReadPostValue(curHTTP.data + 6, sizeof(curHTTP.data)-6-2) == HTTP_READ_INCOMPLETE)
				return HTTP_IO_NEED_DATA;

			// Default action after this is to read the next name, unless there's an error
			curHTTP.smPost = SM_CFG_SNMP_READ_NAME;

			// See if this is a known parameter and legal (must be null 
			// terminator in 4th field name byte, string must no greater than 
			// SNMP_COMMUNITY_MAX_LEN bytes long, and SNMP_MAX_COMMUNITY_SUPPORT 
			// must not be violated.
			vCommunityIndex = curHTTP.data[3] - '0';
			if(vCommunityIndex >= SNMP_MAX_COMMUNITY_SUPPORT)
				break;
			if(curHTTP.data[4] != 0x00u)
				break;
			if(memcmppgm2ram((void*)curHTTP.data, (ROM void*)"rcm", 3) == 0)
				dest = AppConfig.readCommunity[vCommunityIndex];
			else if(memcmppgm2ram((void*)curHTTP.data, (ROM void*)"wcm", 3) == 0)
				dest = AppConfig.writeCommunity[vCommunityIndex];
			else
				break;
			if(strlen((char*)curHTTP.data + 6) > SNMP_COMMUNITY_MAX_LEN)
				break;
			
			// String seems valid, lets copy it to AppConfig
			strcpy((char*)dest, (char*)curHTTP.data+6);
			break;			
	}

	return HTTP_IO_WAITING;		// Assume we're waiting to process more data
}
#endif //#if defined(STACK_USE_SNMP_SERVER)

#endif	// #if defined(STACK_USE_HTTP_APP_RECONFIG)

/*****************************************************************************
  Function:
	static HTTP_IO_RESULT HTTPPostMD5(void)

  Summary:
	Processes the file upload form on upload.htm

  Description:
	This function demonstrates the processing of file uploads.  First, the
	function locates the file data, skipping over any headers that arrive.
	Second, it reads the file 64 bytes at a time and hashes that data.  Once
	all data has been received, the function calculates the MD5 sum and
	stores it in curHTTP.data.

	After the headers, the first line from the form will be the MIME 
	separator.  Following that is more headers about the file, which we 
	discard.  After another CRLFCRLF, the file data begins, and we read 
	it 16 bytes at a time and add that to the MD5 calculation.  The reading
	terminates when the separator string is encountered again on its own 
	line.  Notice that the actual file data is trashed in this process, 
	allowing us to accept files of arbitrary size, not limited by RAM.  
	Also notice that the data buffer is used as an arbitrary storage array 
	for the result.  The ~uploadedmd5~ callback reads this data later to 
	send back to the client.
	
  Precondition:
	None

  Parameters:
	None

  Return Values:
	HTTP_IO_DONE - all parameters have been processed
	HTTP_IO_WAITING - the function is pausing to continue later
	HTTP_IO_NEED_DATA - data needed by this function has not yet arrived
  ***************************************************************************/
#if defined(STACK_USE_HTTP_MD5_DEMO)
static HTTP_IO_RESULT HTTPPostMD5(void)
{
	WORD lenA, lenB;
	static HASH_SUM md5;			// Assume only one simultaneous MD5
	
	#define SM_MD5_READ_SEPARATOR	(0u)
	#define SM_MD5_SKIP_TO_DATA		(1u)
	#define SM_MD5_READ_DATA		(2u)
	#define SM_MD5_POST_COMPLETE	(3u)
	
	// Don't care about curHTTP.data at this point, so use that for buffer
	switch(curHTTP.smPost)
	{
		// Just started, so try to find the separator string
		case SM_MD5_READ_SEPARATOR:
			// Reset the MD5 calculation
			MD5Initialize(&md5);
			
			// See if a CRLF is in the buffer
			lenA = TCPFindROMArray(sktHTTP, (ROM BYTE*)"\r\n", 2, 0, FALSE);
			if(lenA == 0xffff)
			{//if not, ask for more data
				return HTTP_IO_NEED_DATA;
			}
		
			// If so, figure out where the last byte of data is
			// Data ends at CRLFseparator--CRLF, so 6+len bytes
			curHTTP.byteCount -= lenA + 6;
			
			// Read past the CRLF
			curHTTP.byteCount -= TCPGetArray(sktHTTP, NULL, lenA+2);
			
			// Save the next state (skip to CRLFCRLF)
			curHTTP.smPost = SM_MD5_SKIP_TO_DATA;
			
			// No break...continue reading the headers if possible
				
		// Skip the headers
		case SM_MD5_SKIP_TO_DATA:
			// Look for the CRLFCRLF
			lenA = TCPFindROMArray(sktHTTP, (ROM BYTE*)"\r\n\r\n", 4, 0, FALSE);
	
			if(lenA != 0xffff)
			{// Found it, so remove all data up to and including
				lenA = TCPGetArray(sktHTTP, NULL, lenA+4);
				curHTTP.byteCount -= lenA;
				curHTTP.smPost = SM_MD5_READ_DATA;
			}
			else
			{// Otherwise, remove as much as possible
				lenA = TCPGetArray(sktHTTP, NULL, TCPIsGetReady(sktHTTP) - 4);
				curHTTP.byteCount -= lenA;
			
				// Return the need more data flag
				return HTTP_IO_NEED_DATA;
			}
			
			// No break if we found the header terminator
			
		// Read and hash file data
		case SM_MD5_READ_DATA:
			// Find out how many bytes are available to be read
			lenA = TCPIsGetReady(sktHTTP);
			if(lenA > curHTTP.byteCount)
				lenA = curHTTP.byteCount;
	
			while(lenA > 0u)
			{// Add up to 64 bytes at a time to the sum
				lenB = TCPGetArray(sktHTTP, curHTTP.data, (lenA < 64u)?lenA:64);
				curHTTP.byteCount -= lenB;
				lenA -= lenB;
				MD5AddData(&md5, curHTTP.data, lenB);
			}
					
			// If we've read all the data
			if(curHTTP.byteCount == 0u)
			{// Calculate and copy result to curHTTP.data for printout
				curHTTP.smPost = SM_MD5_POST_COMPLETE;
				MD5Calculate(&md5, curHTTP.data);
				return HTTP_IO_DONE;
			}
				
			// Ask for more data
			return HTTP_IO_NEED_DATA;
	}
	
	return HTTP_IO_DONE;
}
#endif // #if defined(STACK_USE_HTTP_MD5_DEMO)

/****************************************************************************
  Function:
    HTTP_IO_RESULT HTTPPostDDNSConfig(void)
    
  Summary:
    Parsing and collecting http data received from http form.

  Description:
    This routine will be excuted every time the Dynamic DNS Client
    configuration form is submitted.  The http data is received 
    as a string of the variables seperated by '&' characters in the TCP RX
    buffer.  This data is parsed to read the required configuration values, 
    and those values are populated to the global array (DDNSData) reserved 
    for this purpose.  As the data is read, DDNSPointers is also populated
    so that the dynamic DNS client can execute with the new parameters.
    
  Precondition:
     curHTTP is loaded.

  Parameters:
    None.

  Return Values:
    HTTP_IO_DONE 		-  Finished with procedure
    HTTP_IO_NEED_DATA	-  More data needed to continue, call again later
    HTTP_IO_WAITING 	-  Waiting for asynchronous process to complete, 
    						call again later
  ***************************************************************************/
#if defined(STACK_USE_DYNAMICDNS_CLIENT)
static HTTP_IO_RESULT HTTPPostDDNSConfig(void)
{
	static BYTE *ptrDDNS;

	#define SM_DDNS_START			(0u)
	#define SM_DDNS_READ_NAME		(1u)
	#define SM_DDNS_READ_VALUE		(2u)
	#define SM_DDNS_READ_SERVICE	(3u)
	#define SM_DDNS_DONE			(4u)

	#define DDNS_SPACE_REMAINING				(sizeof(DDNSData) - (ptrDDNS - DDNSData))

	switch(curHTTP.smPost)
	{
		// Sets defaults for the system
		case SM_DDNS_START:
			ptrDDNS = DDNSData;
			DDNSSetService(0);
			DDNSClient.Host.szROM = NULL;
			DDNSClient.Username.szROM = NULL;
			DDNSClient.Password.szROM = NULL;
			DDNSClient.ROMPointers.Host = 0;
			DDNSClient.ROMPointers.Username = 0;
			DDNSClient.ROMPointers.Password = 0;
			curHTTP.smPost++;
			
		// Searches out names and handles them as they arrive
		case SM_DDNS_READ_NAME:
			// If all parameters have been read, end
			if(curHTTP.byteCount == 0u)
			{
				curHTTP.smPost = SM_DDNS_DONE;
				break;
			}
		
			// Read a name
			if(HTTPReadPostName(curHTTP.data, HTTP_MAX_DATA_LEN) == HTTP_READ_INCOMPLETE)
				return HTTP_IO_NEED_DATA;
			
			if(!strcmppgm2ram((char *)curHTTP.data, (ROM char*)"service"))
			{
				// Reading the service (numeric)
				curHTTP.smPost = SM_DDNS_READ_SERVICE;
				break;
			}
			else if(!strcmppgm2ram((char *)curHTTP.data, (ROM char*)"user"))
				DDNSClient.Username.szRAM = ptrDDNS;
			else if(!strcmppgm2ram((char *)curHTTP.data, (ROM char*)"pass"))
				DDNSClient.Password.szRAM = ptrDDNS;
			else if(!strcmppgm2ram((char *)curHTTP.data, (ROM char*)"host"))
				DDNSClient.Host.szRAM = ptrDDNS;
			
			// Move to reading the value for user/pass/host
			curHTTP.smPost++;
			
		// Reads in values and assigns them to the DDNS RAM
		case SM_DDNS_READ_VALUE:
			// Read a name
			if(HTTPReadPostValue(ptrDDNS, DDNS_SPACE_REMAINING) == HTTP_READ_INCOMPLETE)
				return HTTP_IO_NEED_DATA;
				
			// Move past the data that was just read
			ptrDDNS += strlen((char*)ptrDDNS);
			if(ptrDDNS < DDNSData + sizeof(DDNSData) - 1)
				ptrDDNS += 1;			
			
			// Return to reading names
			curHTTP.smPost = SM_DDNS_READ_NAME;
			break;
		
		// Reads in a service ID
		case SM_DDNS_READ_SERVICE:
			// Read the integer id
			if(HTTPReadPostValue(curHTTP.data, HTTP_MAX_DATA_LEN) == HTTP_READ_INCOMPLETE)
				return HTTP_IO_NEED_DATA;
			
			// Convert to a service ID
			DDNSSetService((BYTE)atol((char*)curHTTP.data));

			// Return to reading names
			curHTTP.smPost = SM_DDNS_READ_NAME;
			break;
			
		// Sets up the DDNS client for an update
		case SM_DDNS_DONE:
			// Since user name and password changed, force an update immediately
			DDNSForceUpdate();
			
			// Redirect to prevent POST errors
			lastSuccess = TRUE;
			strcpypgm2ram((char*)curHTTP.data, (ROM void*)"/dyndns/index.htm");
			curHTTP.httpStatus = HTTP_REDIRECT;
			return HTTP_IO_DONE;				
	}
	
	return HTTP_IO_WAITING;		// Assume we're waiting to process more data
}
#endif	// #if defined(STACK_USE_DYNAMICDNS_CLIENT)

#endif //(use_post)


/****************************************************************************
  Section:
	Dynamic Variable Callback Functions
  ***************************************************************************/

/*****************************************************************************
  Function:
	void HTTPPrint_varname(void)
	
  Internal:
  	See documentation in the TCP/IP Stack API or HTTP2.h for details.
  ***************************************************************************/

// ******************************** //
// Custom HTTP dynamic variables.
// ******************************** //

// Gets the MAC address from CONNECTED_METERS_LIST.
void HTTPPrint_MAC(WORD index)
{
	#if defined(APP_USE_HUB)
	HTTPPrintMAC(((NODE_INFO*)&iplist.v[(index*10)]).MACAddr);
	#endif
}

// Gets the IP address from CONNECTED_METERS_LIST.
void HTTPPrint_IP(WORD index)
{
	#if defined(APP_USE_HUB)
	HTTPPrintIP(((NODE_INFO*)&iplist.v[index*10]).IPAddr);
	#endif
}	
	
void HTTPPrint_GetCalibration(void)
{
	CALIBRATION_VALUES	stCalib;
	
	if( curHTTP.callbackPos == 0 )
		curHTTP.callbackPos = 103;
	
	if( curHTTP.callbackPos > 100 )
	{
		if( MCURequestToBOTTOMBoard(MMT_GET_CALIBRATION_DATA, &stCalib, 1, FALSE, FALSE) )
			curHTTP.callbackPos = 80;
		curHTTP.callbackPos--;
		return;
	}	

	if( curHTTP.callbackPos > 0 )
	{
		if( MCUHasNewMessage )
		{
			BYTE c;
			BYTE sm[sizeof(CALIBRATION_VALUES)*2];
			
			// Get the calibration values.
			memcpy(&stCalib, MCUNewMessage, MCUNewMessageLength);
			
			for(c=0;c<sizeof(CALIBRATION_VALUES)/2;c++)
			{
				BYTE cu = c*2;
				
				sm[c*4] = btohexa_high(((BYTE*)&stCalib)[cu+1]);
				sm[(c*4)+1] = btohexa_low(((BYTE*)&stCalib)[cu+1]);
				sm[(c*4)+2] = btohexa_high(((BYTE*)&stCalib)[cu]);
				sm[(c*4)+3] = btohexa_low(((BYTE*)&stCalib)[cu]);
			}	
			// Received response!
			TCPPutArray(sktHTTP, sm, sizeof(CALIBRATION_VALUES)*2);
			curHTTP.callbackPos = 0;
			return;
		}
		curHTTP.callbackPos--;
	}	
	else
	{
		// No response...
		lastFailure = TRUE;
	}	
}
	
void HTTPPrint_GetMaxDemandSetting(void)
{
	BYTE	Interval[5];
	
	if( curHTTP.callbackPos == 0 )
		curHTTP.callbackPos = 103;
	
	if( curHTTP.callbackPos > 100 )
	{
		if( MCURequestToBOTTOMBoard(MMT_GET_MAX_DEMAND_REFRESH_INTERVAL, &Interval[0], 1, FALSE, FALSE) )
			curHTTP.callbackPos = 80;
		curHTTP.callbackPos--;
		return;
	}	

	if( curHTTP.callbackPos > 0 )
	{
		if( MCUHasNewMessage )
		{
			BYTE c;
			//BYTE sm[10];
			
			// Get the calibration values.
			memcpy(&Interval[0], MCUNewMessage, MCUNewMessageLength);
			Interval[MCUNewMessageLength] = 0;
			//sm[1] = 'K';
			// Received response!
			TCPPutArray(sktHTTP, &Interval[0], strlen(Interval));
			curHTTP.callbackPos = 0;
			return;
		}
		curHTTP.callbackPos--;
	}	
	else
	{
		// No response...
		lastFailure = TRUE;
	}		
}

// This function is used by MCU2 in the meter unit.
// Requests will be routed to MCU1.
void HTTPPrint_RT(void)
{
	#ifdef METER_TOP_BOARD
	BYTE * phase = HTTPGetROMArg(curHTTP.data, (ROM BYTE *)"phase");
	BYTE * reg = HTTPGetROMArg(curHTTP.data, (ROM BYTE *)"reg");
	
	char _phase = atol(phase);
	long _reg = atol(reg);

	char s[16] = "";

	ConvertPowerReadingsToCharArray(_phase, _reg, s);
	{
		char str[4];
		str[0] = _phase;
		str[1] = _reg;
		str[3] = 'N';	//2012-09-24: Liz added to distinguish from EEPROM request.
		MCURequestToBOTTOMBoard(MMT_READING_REQUEST, &str[0], 3, FALSE, TRUE);
	}
	TCPPutArray(sktHTTP, s, strlen(s));
		
	Delay1KTCYx(10);
	#endif
}	


//ROM BYTE HTML_UP_ARROW[] = "up";
//ROM BYTE HTML_DOWN_ARROW[] = "dn";


void HTTPPrint_cookiename(void)
{
	BYTE *ptr;
	
	ptr = HTTPGetROMArg(curHTTP.data, (ROM BYTE*)"name");
	
	if(ptr)
		TCPPutString(sktHTTP, ptr);
	else
		TCPPutROMString(sktHTTP, (ROM BYTE*)"not set");
	
	return;
}

void HTTPPrint_uploadedmd5(void)
{
	BYTE i;

	// Set a flag to indicate not finished
	curHTTP.callbackPos = 1;
	
	// Make sure there's enough output space
	if(TCPIsPutReady(sktHTTP) < 32u + 37u + 5u)
		return;

	// Check for flag set in HTTPPostMD5
#if defined(STACK_USE_HTTP_MD5_DEMO)
	if(curHTTP.smPost != SM_MD5_POST_COMPLETE)
#endif
	{// No file uploaded, so just return
		TCPPutROMString(sktHTTP, (ROM BYTE*)"<b>Upload a File</b>");
		curHTTP.callbackPos = 0;
		return;
	}
	
	TCPPutROMString(sktHTTP, (ROM BYTE*)"<b>Uploaded File's MD5 was:</b><br />");
	
	// Write a byte of the md5 sum at a time
	for(i = 0; i < 16u; i++)
	{
		TCPPut(sktHTTP, btohexa_high(curHTTP.data[i]));
		TCPPut(sktHTTP, btohexa_low(curHTTP.data[i]));
		if((i & 0x03) == 3u)
			TCPPut(sktHTTP, ' ');
	}
	
	curHTTP.callbackPos = 0x00;
	return;
}

extern APP_CONFIG AppConfig;

// This function is written based on the function HTTPPrintIP. It formats the output of the MAC address in the format xxxxxxxxxxxx.
void HTTPPrintMAC(MAC_ADDR mac)
{
	BYTE digits[6];
	BYTE i;
	
	// Write each byte
	for(i = 0; i < 6u; i++)
	{
		if(i)
			TCPPut(sktHTTP, ':');
		TCPPut(sktHTTP, btohexa_high(mac.v[i]));
		TCPPut(sktHTTP, btohexa_low(mac.v[i]));
	}
}

// This function is part of the library. It formats the output of the IP address in the format xxx.xxx.xxx.xxx.
void HTTPPrintIP(IP_ADDR ip)
{
	BYTE digits[4];
	BYTE i;
	
	for(i = 0; i < 4u; i++)
	{
		if(i)
			TCPPut(sktHTTP, '.');
		uitoa(ip.v[i], digits);
		TCPPutString(sktHTTP, digits);
	}
}

void HTTPPrint_config_hostname(void)
{
	TCPPutString(sktHTTP, AppConfig.NetBIOSName);
	return;
}

void HTTPPrint_config_dhcpchecked(void)
{
	if(AppConfig.Flags.bIsDHCPEnabled)
		TCPPutROMString(sktHTTP, (ROM BYTE*)"checked");
	return;
}

void HTTPPrint_config_ip(void)
{
	HTTPPrintIP(AppConfig.MyIPAddr);
	return;
}

void HTTPPrint_config_gw(void)
{
	HTTPPrintIP(AppConfig.MyGateway);
	return;
}

void HTTPPrint_config_subnet(void)
{
	HTTPPrintIP(AppConfig.MyMask);
	return;
}

void HTTPPrint_config_dns1(void)
{
	HTTPPrintIP(AppConfig.PrimaryDNSServer);
	return;
}

void HTTPPrint_config_dns2(void)
{
	HTTPPrintIP(AppConfig.SecondaryDNSServer);
	return;
}

void HTTPPrint_config_mac(void)
{
	BYTE i;
	
	if(TCPIsPutReady(sktHTTP) < 18u)
	{//need 17 bytes to write a MAC
		curHTTP.callbackPos = 0x01;
		return;
	}	
	
	// Write each byte
	for(i = 0; i < 6u; i++)
	{
		if(i)
			TCPPut(sktHTTP, ':');
		TCPPut(sktHTTP, btohexa_high(AppConfig.MyMACAddr.v[i]));
		TCPPut(sktHTTP, btohexa_low(AppConfig.MyMACAddr.v[i]));
	}
	
	// Indicate that we're done
	curHTTP.callbackPos = 0x00;
	return;
}


// SNMP Read communities configuration page
void HTTPPrint_read_comm(WORD num)
{
	#if defined(STACK_USE_SNMP_SERVER)
	// Ensure no one tries to read illegal memory addresses by specifying 
	// illegal num values.
	if(num >= SNMP_MAX_COMMUNITY_SUPPORT)
		return;
		
	// Send proper string
	TCPPutString(sktHTTP, AppConfig.readCommunity[num]);
	#endif
}

// SNMP Write communities configuration page
void HTTPPrint_write_comm(WORD num)
{
	#if defined(STACK_USE_SNMP_SERVER)
	// Ensure no one tries to read illegal memory addresses by specifying 
	// illegal num values.
	if(num >= SNMP_MAX_COMMUNITY_SUPPORT)
		return;
		
	// Send proper string
	TCPPutString(sktHTTP, AppConfig.writeCommunity[num]);
	#endif
}


void HTTPPrint_reboot(void)
{
	// This is not so much a print function, but causes the board to reboot
	// when the configuration is changed.  If called via an AJAX call, this
	// will gracefully reset the board and bring it back online immediately
	Reset();
}

void HTTPPrint_ddns_user(void)
{
	#if defined(STACK_USE_DYNAMICDNS_CLIENT)
	if(DDNSClient.ROMPointers.Username || !DDNSClient.Username.szRAM)
		return;
	if(curHTTP.callbackPos == 0x00u)
		curHTTP.callbackPos = (PTR_BASE)DDNSClient.Username.szRAM;
	curHTTP.callbackPos = (PTR_BASE)TCPPutString(sktHTTP, (BYTE*)(PTR_BASE)curHTTP.callbackPos);
	if(*(BYTE*)(PTR_BASE)curHTTP.callbackPos == '\0')
		curHTTP.callbackPos = 0x00;
	#endif
}

void HTTPPrint_ddns_pass(void)
{
	#if defined(STACK_USE_DYNAMICDNS_CLIENT)
	if(DDNSClient.ROMPointers.Password || !DDNSClient.Password.szRAM)
		return;
	if(curHTTP.callbackPos == 0x00u)
		curHTTP.callbackPos = (PTR_BASE)DDNSClient.Password.szRAM;
	curHTTP.callbackPos = (PTR_BASE)TCPPutString(sktHTTP, (BYTE*)(PTR_BASE)curHTTP.callbackPos);
	if(*(BYTE*)(PTR_BASE)curHTTP.callbackPos == '\0')
		curHTTP.callbackPos = 0x00;
	#endif
}

void HTTPPrint_ddns_host(void)
{
	#if defined(STACK_USE_DYNAMICDNS_CLIENT)
	if(DDNSClient.ROMPointers.Host || !DDNSClient.Host.szRAM)
		return;
	if(curHTTP.callbackPos == 0x00u)
		curHTTP.callbackPos = (PTR_BASE)DDNSClient.Host.szRAM;
	curHTTP.callbackPos = (PTR_BASE)TCPPutString(sktHTTP, (BYTE*)(PTR_BASE)curHTTP.callbackPos);
	if(*(BYTE*)(PTR_BASE)curHTTP.callbackPos == '\0')
		curHTTP.callbackPos = 0x00;
	#endif
}

extern ROM char * ROM ddnsServiceHosts[];
void HTTPPrint_ddns_service(WORD i)
{
	#if defined(STACK_USE_DYNAMICDNS_CLIENT)
	if(!DDNSClient.ROMPointers.UpdateServer || !DDNSClient.UpdateServer.szROM)
		return;
	if((ROM char*)DDNSClient.UpdateServer.szROM == ddnsServiceHosts[i])
		TCPPutROMString(sktHTTP, (ROM BYTE*)"selected");
	#endif
}


void HTTPPrint_ddns_status(void)
{
	#if defined(STACK_USE_DYNAMICDNS_CLIENT)
	DDNS_STATUS s;
	s = DDNSGetLastStatus();
	if(s == DDNS_STATUS_GOOD || s == DDNS_STATUS_UNCHANGED || s == DDNS_STATUS_NOCHG)
		TCPPutROMString(sktHTTP, (ROM BYTE*)"ok");
	else if(s == DDNS_STATUS_UNKNOWN)
		TCPPutROMString(sktHTTP, (ROM BYTE*)"unk");
	else
		TCPPutROMString(sktHTTP, (ROM BYTE*)"fail");
	#else
	TCPPutROMString(sktHTTP, (ROM BYTE*)"fail");
	#endif
}

void HTTPPrint_ddns_status_msg(void)
{
	if(TCPIsPutReady(sktHTTP) < 75u)
	{
		curHTTP.callbackPos = 0x01;
		return;
	}

	#if defined(STACK_USE_DYNAMICDNS_CLIENT)
	switch(DDNSGetLastStatus())
	{
		case DDNS_STATUS_GOOD:
		case DDNS_STATUS_NOCHG:
			TCPPutROMString(sktHTTP, (ROM BYTE*)"The last update was successful.");
			break;
		case DDNS_STATUS_UNCHANGED:
			TCPPutROMString(sktHTTP, (ROM BYTE*)"The IP has not changed since the last update.");
			break;
		case DDNS_STATUS_UPDATE_ERROR:
		case DDNS_STATUS_CHECKIP_ERROR:
			TCPPutROMString(sktHTTP, (ROM BYTE*)"Could not communicate with DDNS server.");
			break;
		case DDNS_STATUS_INVALID:
			TCPPutROMString(sktHTTP, (ROM BYTE*)"The current configuration is not valid.");
			break;
		case DDNS_STATUS_UNKNOWN:
			TCPPutROMString(sktHTTP, (ROM BYTE*)"The Dynamic DNS client is pending an update.");
			break;
		default:
			TCPPutROMString(sktHTTP, (ROM BYTE*)"An error occurred during the update.<br />The DDNS Client is suspended.");
			break;
	}
	#else
	TCPPutROMString(sktHTTP, (ROM BYTE*)"The Dynamic DNS Client is not enabled.");
	#endif
	
	curHTTP.callbackPos = 0x00;
}

void HTTPPrint_smtps_en(void)
{
	#if defined(STACK_USE_SSL_CLIENT)
		TCPPutROMString(sktHTTP, (ROM BYTE*)"inline");
	#else
		TCPPutROMString(sktHTTP, (ROM BYTE*)"none");
	#endif
}

void HTTPPrint_snmp_en(void)
{
	#if defined(STACK_USE_SNMP_SERVER)
		TCPPutROMString(sktHTTP, (ROM BYTE*)"none");
	#else
		TCPPutROMString(sktHTTP, (ROM BYTE*)"block");
	#endif
}

void HTTPPrint_status_ok(void)
{
	if(lastSuccess)
		TCPPutROMString(sktHTTP, (ROM BYTE*)"block");
	else
		TCPPutROMString(sktHTTP, (ROM BYTE*)"none");
	lastSuccess = FALSE;
}

void HTTPPrint_status_fail(void)
{
	if(lastFailure)
		TCPPutROMString(sktHTTP, (ROM BYTE*)"block");
	else
		TCPPutROMString(sktHTTP, (ROM BYTE*)"none");
	lastFailure = FALSE;
}

#endif
