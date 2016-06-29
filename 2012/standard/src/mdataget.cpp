// mdataget.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "windows.h"
#include "string"

#define RBUFFSIZE 1   // size of the read buffer
#define WBUFFSIZE 256 // size of the write buffer

HANDLE hSerial = NULL;

unsigned int setupserial();
unsigned int queryserial(char query[]);

int main(int argc, char **argv)
{
	// we take exactly one argument
	if (argc < 2) {
		printf ("\nUsage mdataget <key>\n");
		exit(1);
	}

	if (setupserial() == 0) {
        queryserial(argv[1]);
	}

	CloseHandle(hSerial);
	return 0;
}

unsigned int setupserial() {
	
	hSerial = CreateFile(L"\\\\.\\COM2", GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);

	if (hSerial == INVALID_HANDLE_VALUE) {
		if (GetLastError() == ERROR_FILE_NOT_FOUND) {
			// serial port does not exist
			printf ("%s\n", "Serial port does not exist");
			return 1;
		} 
		else {
			printf ("%s\n", "There was an error opening the serial port COM2");
		}
	}

	DCB dcbSerialParams = {0};
	dcbSerialParams.DCBlength = sizeof(dcbSerialParams);

	if (!GetCommState(hSerial, &dcbSerialParams)) {
		// there was an error getting state
		printf ("%s\n", "Error getting state of serial device");
		return 1;
	}

	dcbSerialParams.BaudRate = 19200;
	dcbSerialParams.ByteSize = 8;
	dcbSerialParams.StopBits = ONESTOPBIT;
	dcbSerialParams.Parity = NOPARITY;
	dcbSerialParams.fNull = false;
	dcbSerialParams.fOutX = false;
	dcbSerialParams.fInX = false; 
	dcbSerialParams.fRtsControl  = RTS_CONTROL_DISABLE;
	dcbSerialParams.fDtrControl = DTR_CONTROL_DISABLE;
	dcbSerialParams.fOutxCtsFlow = false;
	dcbSerialParams.fOutxDsrFlow = false;

	if (!SetCommState(hSerial, &dcbSerialParams)) {
		// error setting port state
		printf ("%s\n", "Error setting state of serial device");
		return 1;
	}

	COMMTIMEOUTS timeouts = {0};

	timeouts.ReadIntervalTimeout = 50;
	timeouts.ReadTotalTimeoutConstant = 50;
	timeouts.ReadTotalTimeoutMultiplier = 10;
	timeouts.WriteTotalTimeoutConstant = 50;
	timeouts.WriteTotalTimeoutMultiplier = 10;

	if (!SetCommTimeouts(hSerial, &timeouts)) {
		// error setting timeout values
		printf ("%s\n", "Error setting state of serial device");
		return 1;
	}

	return 0;
}	

// queries the serial port by prepending the query with
// "GET " and terminating with a newline
unsigned int queryserial(char query[]) {
	char readBuff[RBUFFSIZE + 1] = {0};
	char lastBuff[RBUFFSIZE + 1] = {0};
	
	char writeBuff[WBUFFSIZE] = "GET ";
	
	strcat(writeBuff, query);
	strcat(writeBuff, "\n");
	int len = strlen(writeBuff);
	
	DWORD bytesRead = 0;
	DWORD bytesWritten = 0;
	
	if (!WriteFile(hSerial, writeBuff, len, &bytesWritten, NULL)) {
		printf ("%s\n", "Error writing to serial device");
		return 1;
	}
	
	// read loop
	do {
		if (!ReadFile(hSerial, readBuff, RBUFFSIZE, &bytesRead, NULL)) {
			printf ("%s\n", "Erro reading from the serial device");
			return 1;
		} 
	
		if (strcmp(readBuff, ".") == 0 && strcmp(lastBuff, ".") == 0) {
			printf("%s", readBuff);
			memcpy(lastBuff, "", 1);
			continue; 
		}
		else if (strcmp(readBuff, "\n") == 0) {
			if (strcmp(lastBuff, ".") == 0) {
				break;
			} 
			else {
				printf("%s", lastBuff);
				memcpy(lastBuff, readBuff, 1);
			}
			
		}
		else {
			printf("%s", lastBuff);
			memcpy(lastBuff, readBuff, 1);
		}
		
	} while (bytesRead == RBUFFSIZE) ;

	return 0;
}





