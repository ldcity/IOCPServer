#pragma once
#ifndef __SESSION__
#define __SESSION__

#include "PCH.h"

#define MAX_WSA_BUF 200
#define RELEASEMASKING 0x8000'0000				// ioRefCount에서 ReleaseFlag만 뽑아냄

// Session Struct
struct alignas(64) stSESSION
{
	uint64_t sessionID;							// Session ID
	SOCKET m_socketClient;						// Client Socket
	wchar_t IP_str[20];							// String IP
	uint32_t IP_num;								// IP
	unsigned short PORT;						// PORT
	DWORD LastRecvTime;							// Last Recv Time

	OVERLAPPED m_stRecvOverlapped;				// Recv Overlapped I/O Struct
	OVERLAPPED m_stSendOverlapped;				// Send Overlapped I/O Struct
	RingBuffer recvRingBuffer;					// Recv RingBuffer
	LockFreeQueue<CPacket*> sendQ;				// Send LockFreeQueue
	
	CPacket* SendPackets[MAX_WSA_BUF] = { nullptr };			// Send Packets

	alignas(64) int sendPacketCount;			// WSABUF Count
	alignas(64) int sendPacketIndex;			// WSABUF Index
	alignas(64) __int64 ioRefCount;				// I/O Count & Session Ref Count
	alignas(64) bool sendFlag;					// Sending Message Check
	alignas(64) bool isDisconnected;			// Session Disconnected

	stSESSION()
	{
		sessionID = -1;
		m_socketClient = INVALID_SOCKET;
		ZeroMemory(IP_str, sizeof(IP_str));
		IP_num = 0;
		PORT = 0;
		LastRecvTime = 0;

		ZeroMemory(&m_stRecvOverlapped, sizeof(OVERLAPPED));
		ZeroMemory(&m_stSendOverlapped, sizeof(OVERLAPPED));
		recvRingBuffer.ClearBuffer();

		sendPacketCount = 0;
		sendPacketIndex = 0;
		ioRefCount = 0;			// accept 이후 바로 recv 걸어버리기 때문에 항상 default가 1
		sendFlag = false;
		isDisconnected = false;
	}

	~stSESSION()
	{
	}
};



#endif // !__SESSION__
