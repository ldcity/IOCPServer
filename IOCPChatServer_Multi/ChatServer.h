#pragma once
#ifndef __NET_CHATSERVER_CLASS__
#define __NET_CHATSERVER_CLASS__

#include "NetServer.h"

#include <unordered_map>
#include <unordered_set>


class ChatServer : public NetServer
{
public:
	ChatServer();
	~ChatServer();

	bool ChatServerStart();
	bool ChatServerStop();

	//bool PacketProc(uint64_t sessionID, CPacket* packet);

	bool OnConnectionRequest(const wchar_t* IP, unsigned short PORT);
	inline void OnClientJoin(uint64_t sessionID)
	{
		if (!startFlag)
		{
			ResumeThread(m_moniteringThread);
			startFlag = true;
		}

		CreatePlayer(sessionID);

		InterlockedIncrement64(&m_jobUpdatecnt);
	}

	inline void OnClientLeave(uint64_t sessionID)
	{
		DeletePlayer(sessionID);

		InterlockedIncrement64(&m_jobUpdatecnt);
	}

	void OnRecv(uint64_t sessionID, CPacket* packet);
	void OnError(int errorCode, const wchar_t* msg);

	//--------------------------------------------------------------------------------------
	// player 관련 함수
	//--------------------------------------------------------------------------------------
	inline Player* FindPlayer(uint64_t sessionID)							// player 검색
	{
		Player* player = nullptr;

		AcquireSRWLockShared(&mapLock);
		auto iter = m_mapPlayer.find(sessionID);

		if (iter == m_mapPlayer.end())
		{
			ReleaseSRWLockShared(&mapLock);
			chatLog.logger(dfLOG_LEVEL_DEBUG, __LINE__, L"Player Not Found!");
			return nullptr;
		}

		ReleaseSRWLockShared(&mapLock);
		return iter->second;
	}

	bool CreatePlayer(uint64_t sessionID);									// player 생성
	bool DeletePlayer(uint64_t sessionID);									// player 삭제
	bool CheckPlayer(Player* player, INT64 accountNo);						// player 중복 체크

	//--------------------------------------------------------------------------------------
	// Packet Proc
	//--------------------------------------------------------------------------------------
	void netPacketProc_Login(uint64_t sessionID, CPacket* packet);			// 로그인 요청
	void netPacketProc_SectorMove(uint64_t sessionID, CPacket* packet);		// 섹터 이동 요청
	void netPacketProc_Chatting(uint64_t sessionID, CPacket* packet);		// 채팅 보내기
	void netPacketProc_HeartBeat(uint64_t sessionID, CPacket* packet);		// 하트비트

private:
	//--------------------------------------------------------------------------------------
	// Job Info
	//--------------------------------------------------------------------------------------
	enum JobType
	{
		NEW_CONNECT,			// 새 접속
		DISCONNECT,				// 접속 해제
		MSG_PACKET				// 패킷
	};

	// Job 구조체
	struct ChatJob
	{
		uint64_t sessionID;
		WORD type;
		CPacket* packet;
	};

private:
	Log chatLog;

	int m_userMAXCnt;														// 최대 player 수
	int m_timeout;															// 타임아웃 시간

	HANDLE m_jobHandle;
	HANDLE m_jobEvent;
	
	HANDLE m_moniteringThread;							// Monitering Thread
	
	HANDLE m_moniterEvent;								// Monitering Event
	HANDLE m_runEvent;									// Thread Start Event

	TLSObjectPool<Player> playerPool = TLSObjectPool<Player>(100);

	unordered_map<uint64_t, Player*> m_mapPlayer;						// 전체 Player 객체
	unordered_map<int64_t, uint64_t> m_setAccountNo;									// accountNo 중복 확인을 위해 필요
	unordered_set<Player*> m_Sector[dfSECTOR_Y_MAX][dfSECTOR_X_MAX];		// 각 섹터에 존재하는 Player 객체

	// iocp 워커스레드에 일 처리를 넘겨 멀티스레드로 동작하려면
	// 컨텐츠 단의 공유 리소스에 대한 락 필요
	SRWLOCK mapLock;
	SRWLOCK setLock;
	SRWLOCK sectorLock;
	//CRITICAL_SECTION mapLock;

	friend unsigned __stdcall MoniteringThread(void* param);
	
	bool MoniterThread_serv();

// 모니터링 관련 변수들
private:
	__int64 m_totalPlayerCnt;												// player total
	__int64 m_jobUpdatecnt;													// job 개수
	__int64 m_jobThreadUpdateCnt;											// job thread update 횟수
	__int64 m_loginPacketTPS;
	__int64 m_sectorMovePacketTPS;
	__int64 m_chattingReqTPS;
	__int64 m_chattingResTPS;
	__int64 m_timeoutTotalCnt;
	__int64 m_timeoutCntTPS;

	bool startFlag;
};




#endif