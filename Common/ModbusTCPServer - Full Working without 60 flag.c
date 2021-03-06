// ModbusServerTCP.c

/*********************************************************************
 *
 *  ModBus Server Application
 *  Module for Microchip TCP/IP Stack
 *   -Implements an example ModBus client and should be used as a basis 
 *	  for creating new TCP client applications
 *	 -Reference: None.  Hopefully AN833 in the future.
 *
 *********************************************************************
 * FileName:        GenericTCPClient.c
 * Dependencies:    TCP, DNS, ARP, Tick
 * Processor:       PIC18, PIC24F, PIC24H, dsPIC30F, dsPIC33F, PIC32
 * Compiler:        Microchip C32 v1.05 or higher
 *					Microchip C30 v3.12 or higher
 *					Microchip C18 v3.30 or higher
 *					HI-TECH PICC-18 PRO 9.63PL2 or higher
 * Company:         Microchip Technology, Inc.
 *
 ********************************************************************/

#include "AppConfig.h"
#include "mcu.h"
#include "ModbusTCPServer.h"
#include "TCPIPConfig.h"
#include "zigbee.h"

#if defined(APP_USE_MODBUS_SERVER)

#include "TCPIP Stack/TCPIP.h"



/*********************************************************
 *
 * Listens for ModBus requests and processes them.
 *
 *********************************************************/
void ModbusTCPServerTask(void)
{
	//BYTE 		i;//, k;
	BYTE		vModbusSession;
	//WORD		w, w2;
	TCP_SOCKET	MySocket;
	
	//static char BUFFER[20];
	static MODBUS_REQUEST_BLOCK modbus_request;
	static DWORD Timer, ACK_Timer;
	static char retries;
	//static char function_code;
	//static char mbap_response[20] = "";
	//static unsigned char modbus_msg_len = 0, dropped_msg = 0;  //modbus_unit_id = 0
	
	MODBUS_SERVER_STATE ModbusServerState, pModbusServerState;// = MBSS_HOME;
	
	static TCP_SOCKET hModbusSockets[MAX_MODBUS_CONNECTIONS];
	static BYTE vModbusServerStates[MAX_MODBUS_CONNECTIONS];
	static BOOL bModbusInitialized = FALSE;
	
	static char error_flag = 0;
	static char e_array[10], e_i = 0;

	// 2012-05-10(Eric) - Support variable to combat 54/60 issues.
	static BYTE bWait;
	// end debug
	
	// Perform one time initialization on power up
	if(!bModbusInitialized)
	{
		for(vModbusSession = 0; vModbusSession < MAX_MODBUS_CONNECTIONS; vModbusSession++)
		{
			hModbusSockets[vModbusSession] = INVALID_SOCKET;
			vModbusServerStates[vModbusSession] = MBSS_HOME;
		}
		bModbusInitialized = TRUE;
		//is_MAC_linked = 0;
	}

	// Loop through each telnet session and process state changes and TX/RX data
	for(vModbusSession = 0; vModbusSession < MAX_MODBUS_CONNECTIONS; vModbusSession++)
	{
		// Load up static state information for this session
		MySocket = hModbusSockets[vModbusSession];
		ModbusServerState = vModbusServerStates[vModbusSession];
		
		if((MySocket == INVALID_SOCKET && ModbusServerState != MBSS_DISCONNECT))
		{
			ModbusServerState = MBSS_HOME;	
		}
		
		// Handle session state
		switch(ModbusServerState)
		{
			case MBSS_HOME: // Opens the socket at the specified port to listen for request.
			{
				static char invalid_count = 0;
				// Connect a socket to the remote TCP server
				MySocket = TCPOpen(0, TCP_OPEN_SERVER, MODBUS_PORT, TCP_PURPOSE_MODBUS);
				
				// Abort operation if no TCP socket of type TCP_PURPOSE_MODBUS is available
				// If this ever happens, you need to go add one to TCPIPConfig.h
				if(MySocket == INVALID_SOCKET)
				{
					if( invalid_count++ > 10 )
					{
						invalid_count = 0;
						error_flag = 70;
						ModbusServerState = MBSS_DISCONNECT;
					}
					break;
				}	
	
				Timer = TickGet();
				ModbusServerState = MBSS_LISTENING;
			}	
				break;
			
			case MBSS_LISTENING:  // This listens out for new requests and checks the message header.
			{
				static unsigned char received = 0;

				if( !TCPIsConnected(MySocket) || TCPWasReset(MySocket) )
				{
					received = 0;
					error_flag = 58;
					ModbusServerState = MBSS_DISCONNECT;
					// 2012-05-09(Eric) - Somehow using break here causes the communication breakdown
					// and RJ45 lights to blink like crazy... No idea why...
					return;
				}	
				
				Timer = TickGet();
				/* 
				 * Receive and process the ModBus header.
				 * +------------------+--------------+----------+
				 * |  Transaction ID  | Protocol ID  |  Length  |
				 * |    2-byte        |   2-byte     |  2-byte  |
				 * +------------------+--------------+----------+
				 */
				{
					char w = TCPIsGetReady(MySocket);
					while( w ) 	
					{
						if( sizeof(MODBUS_REQUEST_BLOCK)-received >= w )
							received += TCPGetArray(MySocket, &modbus_request.Serialised[received], w);
						else
							received += TCPGetArray(MySocket, &modbus_request.Serialised[received], sizeof(MODBUS_REQUEST_BLOCK)-received);
						
						if( received == sizeof(MODBUS_REQUEST_BLOCK) )
						{
							// Receive completed.
							received = 0;
							Timer = TickGet();
							ModbusServerState = MBSS_PROCESS_REQUEST;
							
							TCPPutROMString(MySocket, (ROM BYTE*)"ACK");
							TCPFlush(MySocket);
					
							break;
						}
						else if( received > sizeof(MODBUS_REQUEST_BLOCK) )
						{
							// 2012-05-09(Eric) - Debug.
							BUZZER = 1;
							error_flag = 99;
							ModbusServerState = MBSS_DISCONNECT;
							break;
						}
						else
						{
							// 2012-05-09(Eric) - Debug.
							BUZZER = 1;
							w = TCPIsGetReady(MySocket);
						}
					}
				}	
			}	
				break;
				
			case MBSS_PROCESS_REQUEST:  
			{	
				// At this point the full modbus request should be received.
				// We process the message here.
				
				/* Format of MODBUS request message format.
				 * Register is in little endian format.
				 * checksum is not required for Modbus TCP protocol.
				 * +-----------+---------------+---------------+------------------+
				 * | Device ID | Function Code | Starting Addr | No. of registers |
				 * |  1-byte   |    1 byte     |    2-byte     |      2-byte      |
				 * +-----------+---------------+---------------+------------------+
				 */
				
				// Connection lost.
				if( !TCPIsConnected(MySocket) || TCPWasReset(MySocket) )
				{
					//DWORD lapse = (TickGet()-Timer)/TICK_SECOND;
					error_flag = 48;
					ModbusServerState = MBSS_DISCONNECT;
					break;
				}	
				
				switch(modbus_request.w.FunctionCode)
				{
					case 3:  // MODBUS read request.
					{
						static char is_current_req = 0;
						MCU_REQUEST_BLOCK mcu_req;
						
						// Initialise the MCU_REQUEST_BLOCK.
						mcu_req.w.message_type = MMT_MODBUS_REQUEST;
						mcu_req.w.start_addr = modbus_request.w.StartAddr;
						mcu_req.w.register_count = modbus_request.w.RegisterCount;
						mcu_req.w.result_h = mcu_req.w.result_l = 0;
						
						// Request for reading for this particular register from bottom board.
						if(MCURequestToBOTTOMBoard(mcu_req.w.message_type, &mcu_req.w.start_addr, 12, FALSE, FALSE))
						{
							// Wait for response.
							ACK_Timer = Timer = TickGet();
							error_flag = 0;
							bWait = 0;  // 2012-05-10(Eric).
							ModbusServerState = MBSS_WAIT_RESPONSE;
						}
						else
						{
							// This line is for debug purpose only.
							mcu_req.w.result_h = 0;
						}	

						break;
					}
					case 6:  // MODBUS write request.
					{
						if (modbus_request.w.StartAddr == 0x0941)
							strcpy(Credit_Balance, modbus_request.w.Request_Data);
						// Wait for response.
						ACK_Timer = Timer = TickGet();
						error_flag = 0;
						ModbusServerState = MBSS_SEND_RESPONSE;
						break;
					}
					default:
					{
						// Unknown incoming request.
						error_flag = 30;
						ModbusServerState = MBSS_DISCONNECT;
						break;
					}	
				}
				
			}	
				break;
			
			case MBSS_WAIT_RESPONSE:
			{
				// Connection lost.
				if( !TCPIsConnected(MySocket) || TCPWasReset(MySocket) )
				{
					//DWORD lapse = (TickGet()-Timer)/TICK_SECOND;
					if(error_flag!=77 && error_flag != 76)
						error_flag = 78;
					// 2012-05-10(Eric) - This additional state seems to help remove the 54/62 error flag.
					ModbusServerState = MBSS_SEND_ERROR_CODE;
					//ModbusServerState = MBSS_DISCONNECT;
					
					break;
				}	
				
				if( MCUHasNewMessage==0 )
				{
					// Timeout waiting for data from bottom board.
					// 2012-05-10(Eric) - Modifying this section seems to have totally removed 54/60 issues.
					if( TickGet()-Timer > (TICK_SECOND) )
					{
						bWait++;
						
						// Instead of abruptly terminating the line, try to send a msg to inform the client.
						if(bWait == 3)
						{
							//Timer = TickGet();
							TCPPutROMString(MySocket, (ROM BYTE*)"CLS");
							TCPFlush(MySocket);
							break;
						}	
						// Wait for client to terminate.
						if(bWait==6)
						{
							error_flag = 79;
							ModbusServerState = MBSS_DISCONNECT;
							break;
						}
						Timer = TickGet();
						break;
					}
					
					// Send regulated 'ACK' messages to keep the connection alive while waiting for data to come in.
					if( (TickGet()-ACK_Timer>TICK_SECOND) && TCPIsPutReady(MySocket) > 5 )
					{
						error_flag = 77;
						MCUTasks();
						TCPPutROMString(MySocket, (ROM BYTE*)"ACK");
						TCPFlush(MySocket);
						ACK_Timer = TickGet();
						break;
					}
					
					// Still waiting for response from mcu anf
					error_flag = 76;
					break;
				}

				Timer = TickGet();
				ModbusServerState = MBSS_SEND_RESPONSE;
				break;
			}
				
			case MBSS_SEND_RESPONSE:
			{
				unsigned char w;
				if((TickGet()-Timer)>(3*TICK_SECOND))
				{
					// If we do not have a response back to the client, output an error message instead.
					// 2012-05-09(Eric) - Debug. Change CLS to DLS to differentiate the error occurrance in communicator.
					TCPPutROMString(MySocket, (ROM BYTE*)"DLS");
					TCPFlush(MySocket);

					error_flag = 2;
					ModbusServerState = MBSS_DISCONNECT;
					break;
				}	
				
				switch (modbus_request.w.FunctionCode)
				{
					case 3:
					{
						MCUHasNewMessage = 0;

						if( TCPIsPutReady(MySocket) < sizeof(MCU_REQUEST_BLOCK) )
						break;

						// Send the response.
						w = TCPPutArray(MySocket, MCUNewMessage, sizeof(MCU_REQUEST_BLOCK));
				
						if( w >= sizeof(MCU_REQUEST_BLOCK) )
						{
							// Sending completed.
							TCPFlush(MySocket);
							ModbusServerState = MBSS_COMPLETE;
							break;
						}
						break;
					}
					case 6:
					{
						TCPPutROMString(MySocket,(ROM BYTE*)"OK~");
						TCPFlush(MySocket);
						ACK_Timer = TickGet();		
						ModbusServerState = MBSS_COMPLETE;
						break;
					}
					default:
						break;
				}
				
				break;
			}
				
			case MBSS_COMPLETE:
			{
				error_flag = 5;
				ModbusServerState = MBSS_DISCONNECT;
				break;
			}
			case MBSS_DISCONNECT:
			{
				TCPDiscard(MySocket);
				TCPClose(MySocket);
				MySocket = INVALID_SOCKET;
				ModbusServerState = MBSS_HOME;
				e_array[e_i] = error_flag;
				if(e_i++ > 10)
					e_i = 0;	
				error_flag = 0;
			}
				break;
			case MBSS_SEND_ERROR_CODE:
			{
				ModbusServerState = MBSS_LISTENING;
				e_array[e_i] = error_flag;
				if(e_i++ > 10)
					e_i = 0;	
				error_flag = 0;
			}	
				break;
		}
		
		// Save session state back into the static array
		hModbusSockets[vModbusSession] = MySocket;
		vModbusServerStates[vModbusSession] = ModbusServerState;
		pModbusServerState = ModbusServerState;
	
	}		
}
	
