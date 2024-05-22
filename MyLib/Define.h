#pragma once
#ifndef __SERVER_DEFINE__
#define __SERVER_DEFINE__

#include "PCH.h"

#define dfSECTOR_X_MAX		50
#define dfSECTOR_Y_MAX		50
#define ID_MAX_LEN			20
#define NICKNAME_MAX_LEN	20
#define MSG_MAX_LEN			64

const char SESSION_ID_BITS = 47;
const __int64 SESSION_INDEX_MASK = 0x00007FFFFFFFFFFF;

const unsigned char PACKET_CODE = 0x56;
const unsigned char KEY = 0xa9;


#pragma pack(1)
// LAN Header
struct LANHeader
{
	// Payload Len
	unsigned short len;
};

// Net Header
struct NetHeader
{
	unsigned char code;
	unsigned short len;
	unsigned char randKey;
	unsigned char checkSum;
};

struct Player
{
	ULONG64 sessionID;					// 세션 ID
	INT64 accountNo;					// 회원 번호
	char sessionKey[64];				// 인증토큰
	wchar_t ID[ID_MAX_LEN];						// ID
	wchar_t nickname[NICKNAME_MAX_LEN];				// 닉네임

	WORD sectorX;
	WORD sectorY;

	DWORD recvLastTime;						// 하트비트 시간

	Player() : sessionID(-1), accountNo(-1), sessionKey{ 0 }, ID{ 0 }, nickname{ 0 }, sectorX(-1), sectorY(-1), recvLastTime(0) {}
	Player(ULONG64 _sessionID, INT64 _accountNo, const wchar_t* _ID, const wchar_t* _nickname,
		WORD _sectorX, WORD _sectorY) :
		sessionID(_sessionID), accountNo(_accountNo), sectorX(_sectorX), sectorY(_sectorY), sessionKey{ 0 }, recvLastTime(0)
	{
		wmemcpy_s(ID, ID_MAX_LEN, _ID, ID_MAX_LEN);
		wmemcpy_s(nickname, NICKNAME_MAX_LEN, _nickname, NICKNAME_MAX_LEN);
	}

};

struct ChatJob
{
	uint64_t sessionID;
	WORD type;
	CPacket* packet;
};

#pragma pack()


#endif // !__SERVER_DEFINE__
