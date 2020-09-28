#include "StdAfx.h"
#include <intrin.h>

static const SCP_DS3_ACCEL default_accel = {512, 512, 400, 512};

CBTConnection::CBTConnection(void)
{
	m_hEvent = INVALID_HANDLE_VALUE;
	m_hThread = INVALID_HANDLE_VALUE;
    WSADATA wsaData;

	CollectionSize = 0;
	m_bInited = false;

    sockaddr_in Control, Report; 

	Control.sin_family      = AF_INET;
    Control.sin_addr.s_addr = inet_addr("127.0.0.1");
    Control.sin_port        = htons(ControlPort);

	Report.sin_family      = AF_INET;
    Report.sin_addr.s_addr = inet_addr("127.0.0.1");
    Report.sin_port        = htons(ReportPort);

	for (int Index = 0; Index < 4; Index++)
	{
		memset(&(m_padState    [Index]), 0, sizeof(XINPUT_STATE));
		memset(&(m_padVibration[Index]), 0, sizeof(XINPUT_VIBRATION));
		memset(&(m_Extended    [Index]), 0, sizeof(SCP_EXTN));
		m_Accel[Index] = default_accel;
	}

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) == NO_ERROR) 
	{
		if ((m_Control = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == INVALID_SOCKET) 
		{
			WSACleanup();
			return;
		}

	    if (connect(m_Control, (SOCKADDR*) &Control, sizeof(Control)) == SOCKET_ERROR)
		{
	        closesocket(m_Control);

			WSACleanup();
			return;
		}

		if ((m_Report = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == INVALID_SOCKET) 
		{
			closesocket(m_Control);

			WSACleanup();
			return;
		}

		if (bind(m_Report, (SOCKADDR*) &Report, sizeof(Report)) == SOCKET_ERROR)
		{
	        closesocket(m_Control);
			closesocket(m_Report);

			WSACleanup();
			return;
		}
    }

	m_bInited = true;
}

BOOL CBTConnection::Open()
{
	UCHAR Buffer[6]; 
	
	Buffer[0] = 0; 
	Buffer[1] = 0;

	if (m_bInited)
	{
		CollectionSize = 0;

		if (send(m_Control, (CHAR*) Buffer, 6, 0) != SOCKET_ERROR)
		{
			if (recv(m_Control, (CHAR*) Buffer, 6, 0) > 0)
			{
				for (int Index = 2; Index < 6; Index++)
				{
					if (Buffer[Index] > 0) CollectionSize++;
				}
			}
		}

		if (CollectionSize > 0)
		{
			m_bConnected = m_xConnected = true;

			m_hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
			m_hThread = CreateThread(NULL, 0, ReadThread, this, 0, NULL);
		}

		return CollectionSize > 0;
	}
	else
		return FALSE;
}

BOOL CBTConnection::Close()
{
	if (m_bInited)
	{
		m_bInited = m_bConnected = m_xConnected = false;

		try
		{
			closesocket(m_Control);
			closesocket(m_Report);
		
			WSACleanup();
		}
		catch (...)
		{
		}

		HANDLE Handles[2] = {m_hEvent, m_hThread};
		WaitForMultipleObjects(2, Handles, FALSE, INFINITE);
		CloseHandle(m_hEvent);
		CloseHandle(m_hThread);
		m_hEvent = INVALID_HANDLE_VALUE;
		m_hThread = INVALID_HANDLE_VALUE;
	}

	return !m_bConnected;
}


void CBTConnection::Report(DWORD dwUserIndex)
{
	UCHAR Buffer[4]; 
	
	Buffer[0] = (UCHAR) dwUserIndex; 
	Buffer[1] = 0x01;
	Buffer[2] = m_padVibration[dwUserIndex].wLeftMotorSpeed  >> 8;
	Buffer[3] = m_padVibration[dwUserIndex].wRightMotorSpeed >> 8;

	if (m_bConnected)
	{
		send(m_Control, (CHAR*) Buffer, 4, 0);
	}
}

BOOL CBTConnection::Read(UCHAR* Buffer)
{
	int bRead = 0;

	if (m_bConnected)
	{
		bRead = recv(m_Report, (CHAR*) Buffer, 96, 0);
	}

	return bRead > 0;
}

#ifdef _M_IX86
void __declspec(noinline) SignalAndExit(void *_ExitThread, HANDLE hEvent)
{
	*((void**)_AddressOfReturnAddress()) = SetEvent;
}
#elif defined(_M_X64) && defined(NDEBUG)
BOOL __declspec(noinline) SignalAndExit(HANDLE hEvent)
{
	void **p = (void**)_AddressOfReturnAddress();
	p[0] = GetCurrentThread;
	p[1] = ExitThread;
	return SetEvent(hEvent);
}
#endif

DWORD WINAPI CBTConnection::ReadThread(void *lpController)
{
	CBTConnection* Pad = (CBTConnection *) lpController;

	UCHAR Buffer[96];

	while (Pad->m_bConnected)
	{		
		if (Pad->Read(Buffer))
		{
			if (Buffer[1] == 2)
			{
				Pad->XInputMapState(Buffer[0], &Buffer[8], Buffer[89]);
			}
			else
			{
				memset(&(Pad->m_padState    [Buffer[0]]), 0, sizeof(XINPUT_STATE));
				memset(&(Pad->m_padVibration[Buffer[0]]), 0, sizeof(XINPUT_VIBRATION));
				memset(&(Pad->m_Extended    [Buffer[0]]), 0, sizeof(SCP_EXTN));
				Pad->m_Accel[Buffer[0]] = default_accel;
			}
		}
		else
		{
			Pad->m_bConnected = false;
		}
	}

	// HACK: need to ensure that code is still alive between SetEvent and ExitThread calls even when DLL is already unloaded
#ifdef _M_IX86
	SignalAndExit(ExitThread, Pad->m_hEvent);
	return (DWORD)(DWORD_PTR)&SignalAndExit; // prevent compiler optimizing away function arguments
#elif defined(_M_X64) && defined(NDEBUG)
	return SignalAndExit(Pad->m_hEvent); // requires compiler optimizing SetEvent call into jmp
#else
	// unsafe code: will crash if DLL is unloaded during this call
	SetEvent(Pad->m_hEvent);
	ExitThread(0);
	return 0;
#endif
}

void CBTConnection::XInputMapState(DWORD Pad, UCHAR* Report, UCHAR Model)
{
	m_padState[Pad].Gamepad.wButtons = 0;

	if (Model == 1)	// DS3
	{
		DWORD Buttons = Report[2] << 0 | Report[3] << 8 | Report[4] << 16 | Report[5] << 24;

		if (Buttons & (0x1 <<  0)) m_padState[Pad].Gamepad.wButtons |= XINPUT_GAMEPAD_BACK;
		if (Buttons & (0x1 <<  1)) m_padState[Pad].Gamepad.wButtons |= XINPUT_GAMEPAD_LEFT_THUMB;
		if (Buttons & (0x1 <<  2)) m_padState[Pad].Gamepad.wButtons |= XINPUT_GAMEPAD_RIGHT_THUMB;
		if (Buttons & (0x1 <<  3)) m_padState[Pad].Gamepad.wButtons |= XINPUT_GAMEPAD_START;

		if (Buttons & (0x1 <<  4)) m_padState[Pad].Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_UP;
		if (Buttons & (0x1 <<  5)) m_padState[Pad].Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_RIGHT;
		if (Buttons & (0x1 <<  6)) m_padState[Pad].Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_DOWN;
		if (Buttons & (0x1 <<  7)) m_padState[Pad].Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_LEFT;

		if (Buttons & (0x1 << 10)) m_padState[Pad].Gamepad.wButtons |= XINPUT_GAMEPAD_LEFT_SHOULDER;
		if (Buttons & (0x1 << 11)) m_padState[Pad].Gamepad.wButtons |= XINPUT_GAMEPAD_RIGHT_SHOULDER;

		if (Buttons & (0x1 << 12)) m_padState[Pad].Gamepad.wButtons |= XINPUT_GAMEPAD_Y;
		if (Buttons & (0x1 << 13)) m_padState[Pad].Gamepad.wButtons |= XINPUT_GAMEPAD_B;
		if (Buttons & (0x1 << 14)) m_padState[Pad].Gamepad.wButtons |= XINPUT_GAMEPAD_A;
		if (Buttons & (0x1 << 15)) m_padState[Pad].Gamepad.wButtons |= XINPUT_GAMEPAD_X;

		if (Buttons & (0x1 << 16)) m_padState[Pad].Gamepad.wButtons |= XINPUT_GAMEPAD_GUIDE;

		m_padState[Pad].Gamepad.sThumbRY = -Scale((SHORT) Report[9]);
		m_padState[Pad].Gamepad.sThumbRX =  Scale((SHORT) Report[8]);
		m_padState[Pad].Gamepad.sThumbLY = -Scale((SHORT) Report[7]);
		m_padState[Pad].Gamepad.sThumbLX =  Scale((SHORT) Report[6]);

		// Remap for Triggers - Not Unpacked as Axis by UnpackReport
		m_padState[Pad].Gamepad.bLeftTrigger  = Report[18];
		m_padState[Pad].Gamepad.bRightTrigger = Report[19];

		// Convert for Extension
		m_Extended[Pad].SCP_UP     = ToPressure(Report[14]);
		m_Extended[Pad].SCP_RIGHT  = ToPressure(Report[15]);
		m_Extended[Pad].SCP_DOWN   = ToPressure(Report[16]);
		m_Extended[Pad].SCP_LEFT   = ToPressure(Report[17]);

		m_Extended[Pad].SCP_LX     = ToAxis(m_padState[Pad].Gamepad.sThumbLX);
		m_Extended[Pad].SCP_LY     = ToAxis(m_padState[Pad].Gamepad.sThumbLY);

		m_Extended[Pad].SCP_L1     = ToPressure(Report[20]);
		m_Extended[Pad].SCP_L2     = ToPressure(Report[18]);
		m_Extended[Pad].SCP_L3     = m_padState[Pad].Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_THUMB ? 1.0f : 0.0f;

		m_Extended[Pad].SCP_RX     = ToAxis(m_padState[Pad].Gamepad.sThumbRX);
		m_Extended[Pad].SCP_RY     = ToAxis(m_padState[Pad].Gamepad.sThumbRY);

		m_Extended[Pad].SCP_R1     = ToPressure(Report[21]);
		m_Extended[Pad].SCP_R2     = ToPressure(Report[19]);
		m_Extended[Pad].SCP_R3     = m_padState[Pad].Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_THUMB ? 1.0f : 0.0f;

		m_Extended[Pad].SCP_T      = ToPressure(Report[22]);
		m_Extended[Pad].SCP_C      = ToPressure(Report[23]);
		m_Extended[Pad].SCP_X      = ToPressure(Report[24]);
		m_Extended[Pad].SCP_S      = ToPressure(Report[25]);

		m_Extended[Pad].SCP_SELECT = m_padState[Pad].Gamepad.wButtons & XINPUT_GAMEPAD_BACK        ? 1.0f : 0.0f;
		m_Extended[Pad].SCP_START  = m_padState[Pad].Gamepad.wButtons & XINPUT_GAMEPAD_START       ? 1.0f : 0.0f;

		m_Extended[Pad].SCP_PS     = m_padState[Pad].Gamepad.wButtons & XINPUT_GAMEPAD_GUIDE       ? 1.0f : 0.0f;

		m_Accel[Pad].SCP_ACCEL_X = 0x400-_byteswap_ushort(*(unsigned short*)&Report[41]);
		m_Accel[Pad].SCP_ACCEL_Z =       _byteswap_ushort(*(unsigned short*)&Report[43]);
		m_Accel[Pad].SCP_ACCEL_Y =       _byteswap_ushort(*(unsigned short*)&Report[45]);
		m_Accel[Pad].SCP_GYRO    = 0x400-_byteswap_ushort(*(unsigned short*)&Report[47]);
	}

	if (Model == 2)  // DS4
	{
		DWORD Buttons = Report[5] << 0 | Report[6] << 8 | Report[7] << 16;

		if (Buttons & (0x1 << 12)) m_padState[Pad].Gamepad.wButtons |= XINPUT_GAMEPAD_BACK;
		if (Buttons & (0x1 << 14)) m_padState[Pad].Gamepad.wButtons |= XINPUT_GAMEPAD_LEFT_THUMB;
		if (Buttons & (0x1 << 15)) m_padState[Pad].Gamepad.wButtons |= XINPUT_GAMEPAD_RIGHT_THUMB;
		if (Buttons & (0x1 << 13)) m_padState[Pad].Gamepad.wButtons |= XINPUT_GAMEPAD_START;

		if (Buttons & (0x1 <<  0)) m_padState[Pad].Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_UP;
		if (Buttons & (0x1 <<  1)) m_padState[Pad].Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_RIGHT;
		if (Buttons & (0x1 <<  2)) m_padState[Pad].Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_DOWN;
		if (Buttons & (0x1 <<  3)) m_padState[Pad].Gamepad.wButtons |= XINPUT_GAMEPAD_DPAD_LEFT;

		if (Buttons & (0x1 <<  8)) m_padState[Pad].Gamepad.wButtons |= XINPUT_GAMEPAD_LEFT_SHOULDER;
		if (Buttons & (0x1 <<  9)) m_padState[Pad].Gamepad.wButtons |= XINPUT_GAMEPAD_RIGHT_SHOULDER;

		if (Buttons & (0x1 <<  7)) m_padState[Pad].Gamepad.wButtons |= XINPUT_GAMEPAD_Y;
		if (Buttons & (0x1 <<  6)) m_padState[Pad].Gamepad.wButtons |= XINPUT_GAMEPAD_B;
		if (Buttons & (0x1 <<  5)) m_padState[Pad].Gamepad.wButtons |= XINPUT_GAMEPAD_A;
		if (Buttons & (0x1 <<  4)) m_padState[Pad].Gamepad.wButtons |= XINPUT_GAMEPAD_X;

		if (Buttons & (0x1 << 16)) m_padState[Pad].Gamepad.wButtons |= XINPUT_GAMEPAD_GUIDE;

		m_padState[Pad].Gamepad.sThumbRY = -Scale((SHORT) Report[4]);
		m_padState[Pad].Gamepad.sThumbRX =  Scale((SHORT) Report[3]);
		m_padState[Pad].Gamepad.sThumbLY = -Scale((SHORT) Report[2]);
		m_padState[Pad].Gamepad.sThumbLX =  Scale((SHORT) Report[1]);

		// Remap for Triggers
		m_padState[Pad].Gamepad.bLeftTrigger  = Report[8];
		m_padState[Pad].Gamepad.bRightTrigger = Report[9];

		// Convert for Extension
		m_Extended[Pad].SCP_UP     = m_padState[Pad].Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_UP        ? 1.0f : 0.0f;
		m_Extended[Pad].SCP_RIGHT  = m_padState[Pad].Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT     ? 1.0f : 0.0f;
		m_Extended[Pad].SCP_DOWN   = m_padState[Pad].Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN      ? 1.0f : 0.0f;
		m_Extended[Pad].SCP_LEFT   = m_padState[Pad].Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT      ? 1.0f : 0.0f;

		m_Extended[Pad].SCP_LX     = ToAxis(m_padState[Pad].Gamepad.sThumbLX);
		m_Extended[Pad].SCP_LY     = ToAxis(m_padState[Pad].Gamepad.sThumbLY);

		m_Extended[Pad].SCP_L1     = m_padState[Pad].Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER  ? 1.0f : 0.0f;
		m_Extended[Pad].SCP_L2     = ToPressure(Report[8]);
		m_Extended[Pad].SCP_L3     = m_padState[Pad].Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_THUMB     ? 1.0f : 0.0f;

		m_Extended[Pad].SCP_RX     = ToAxis(m_padState[Pad].Gamepad.sThumbRX);
		m_Extended[Pad].SCP_RY     = ToAxis(m_padState[Pad].Gamepad.sThumbRY);

		m_Extended[Pad].SCP_R1     = m_padState[Pad].Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER ? 1.0f : 0.0f;
		m_Extended[Pad].SCP_R2     = ToPressure(Report[9]);
		m_Extended[Pad].SCP_R3     = m_padState[Pad].Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_THUMB    ? 1.0f : 0.0f;

		m_Extended[Pad].SCP_T      = m_padState[Pad].Gamepad.wButtons & XINPUT_GAMEPAD_Y              ? 1.0f : 0.0f;
		m_Extended[Pad].SCP_C      = m_padState[Pad].Gamepad.wButtons & XINPUT_GAMEPAD_B              ? 1.0f : 0.0f;
		m_Extended[Pad].SCP_X      = m_padState[Pad].Gamepad.wButtons & XINPUT_GAMEPAD_A              ? 1.0f : 0.0f;
		m_Extended[Pad].SCP_S      = m_padState[Pad].Gamepad.wButtons & XINPUT_GAMEPAD_X              ? 1.0f : 0.0f;

		m_Extended[Pad].SCP_SELECT = m_padState[Pad].Gamepad.wButtons & XINPUT_GAMEPAD_BACK           ? 1.0f : 0.0f;
		m_Extended[Pad].SCP_START  = m_padState[Pad].Gamepad.wButtons & XINPUT_GAMEPAD_START          ? 1.0f : 0.0f;

		m_Extended[Pad].SCP_PS     = m_padState[Pad].Gamepad.wButtons & XINPUT_GAMEPAD_GUIDE          ? 1.0f : 0.0f;
	}
}


DWORD CBTConnection::GetState(DWORD dwUserIndex, XINPUT_STATE* pState)
{
	if (m_bConnected)
	{
		m_padState[dwUserIndex].dwPacketNumber++;

		memcpy(pState, &(m_padState[dwUserIndex]), sizeof(XINPUT_STATE));
	}

	return m_bConnected ? ERROR_SUCCESS : ERROR_DEVICE_NOT_CONNECTED;
}

DWORD CBTConnection::SetState(DWORD dwUserIndex, XINPUT_VIBRATION* pVibration)
{
	if (m_bConnected)
	{
		if ((pVibration->wLeftMotorSpeed  != m_padVibration[dwUserIndex].wLeftMotorSpeed)
		||  (pVibration->wRightMotorSpeed != m_padVibration[dwUserIndex].wRightMotorSpeed))
		{
			m_padVibration[dwUserIndex].wRightMotorSpeed = pVibration->wRightMotorSpeed;
			m_padVibration[dwUserIndex].wLeftMotorSpeed  = pVibration->wLeftMotorSpeed;

			Report(dwUserIndex);
		}
	}

	return m_bConnected ? ERROR_SUCCESS : ERROR_DEVICE_NOT_CONNECTED;
}

DWORD CBTConnection::GetExtended(DWORD dwUserIndex, SCP_EXTN* pPressure)
{
	if (m_bConnected)
	{
		memcpy(pPressure, &m_Extended[dwUserIndex], sizeof(SCP_EXTN));
	}

	return m_bConnected ? ERROR_SUCCESS : ERROR_DEVICE_NOT_CONNECTED;
}

DWORD CBTConnection::GetCustomData(DWORD dwUserIndex, DWORD Type, void* pData)
{
	if (!m_bConnected)
		return ERROR_DEVICE_NOT_CONNECTED;;

	if (!Type)
	{
		memcpy(pData, &m_Accel[dwUserIndex], sizeof(SCP_DS3_ACCEL));
		return ERROR_SUCCESS;
	}

	return ERROR_NOT_SUPPORTED;
}

// UNDOCUMENTED

DWORD CBTConnection::GetStateEx(DWORD dwUserIndex, XINPUT_STATE *pState)
{
	return GetState(dwUserIndex, pState);
}
