#pragma once

class CBTConnection : public CSCPController
{
public:

	DWORD CollectionSize;

	CBTConnection(void);

	virtual BOOL Open();

	virtual BOOL Close();

	virtual DWORD GetState(DWORD dwUserIndex, XINPUT_STATE* pState);

	virtual DWORD SetState(DWORD dwUserIndex, XINPUT_VIBRATION* pVibration);

	virtual DWORD GetExtended(DWORD dwUserIndex, SCP_EXTN* pPressure);

	virtual DWORD GetCustomData(DWORD dwUserIndex, DWORD Type, void* pData);

	// UNDOCUMENTED

	virtual DWORD GetStateEx(DWORD dwUserIndex, XINPUT_STATE *pState);

protected:

    static const unsigned short ControlPort = 26760;
    static const unsigned short ReportPort  = 26761;

	XINPUT_STATE     m_padState     [4];
	XINPUT_VIBRATION m_padVibration [4];
	SCP_EXTN		 m_Extended     [4];
	SCP_DS3_ACCEL	 m_Accel        [4];

	volatile bool	 m_bInited, m_bConnected;

	SOCKET m_Control;
	SOCKET m_Report;
	HANDLE m_hEvent;
	HANDLE m_hThread;

	virtual void Report(DWORD dwUserIndex);

	virtual void XInputMapState(DWORD dwUserIndex, UCHAR* Buffer, UCHAR Model);

	virtual BOOL Read(UCHAR* Buffer);

	static DWORD WINAPI ReadThread(void *lpController);
};
