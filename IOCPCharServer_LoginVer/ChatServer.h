#ifndef __NET_CHATSERVER_CLASS__
#define __NET_CHATSERVER_CLASS__

#include "PCH.h"
#include "NetServer.h"
#include "MonitoringLanClient.h"

class ChatServer : public NetServer
{
public:
	ChatServer();
	~ChatServer();

	bool ChatServerStart();
	bool ChatServerStop();

	bool PacketProc(uint64_t sessionID, CPacket* packet);

	bool OnConnectionRequest(const wchar_t* IP, unsigned short PORT);
	void OnClientJoin(uint64_t sessionID);
	void OnClientLeave(uint64_t sessionID);
	void OnRecv(uint64_t sessionID, CPacket* packet);
	void OnError(int errorCode, const wchar_t* msg);

	//--------------------------------------------------------------------------------------
	// player 관련 함수
	//--------------------------------------------------------------------------------------
	inline Player* FindPlayer(uint64_t sessionID)							// player 검색
	{
		Player* player = nullptr;

		auto iter = m_mapPlayer.find(sessionID);

		if (iter == m_mapPlayer.end())
		{
			//chatLog->logger(dfLOG_LEVEL_DEBUG, __LINE__, L"FindPlayer # Player Not Found!");
			return nullptr;
		}

		return iter->second;
	}
	bool CreatePlayer(uint64_t sessionID);									// player 생성
	bool DeletePlayer(uint64_t sessionID);									// player 삭제
	

	// player 중복 체크
	inline bool CheckPlayer(Player* player, INT64 accountNo)
	{
		// accountNo 중복체크
		auto accountIter = m_accountNo.find(accountNo);
		if (accountIter != m_accountNo.end())
		{
			Player* dupPlayer = FindPlayer(accountIter->second);

			if (dupPlayer == nullptr)
			{
				return false;
			}

			m_accountNo.erase(accountIter);

			DisconnectSession(dupPlayer->sessionID);

			return true;
		}

		return true;
	}

	bool Authentication(Player* player);


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
	Log* chatLog;

	int m_userMAXCnt;														// 최대 player 수
	int m_timeout;															// 타임아웃 시간

	HANDLE m_jobHandle;
	HANDLE m_jobEvent;

	HANDLE m_moniteringThread;							// Monitering Thread

	HANDLE m_moniterEvent;								// Monitering Event
	HANDLE m_runEvent;									// Thread Start Event

	TLSObjectPool<Player> playerPool = TLSObjectPool<Player>(200);
	TLSObjectPool<ChatJob> jobPool = TLSObjectPool<ChatJob>(300);
	LockFreeQueue<ChatJob*> chatJobQ = LockFreeQueue<ChatJob*>(30000);

	std::unordered_map<uint64_t, Player*> m_mapPlayer;							// 전체 Player 객체
	std::unordered_set<Player*> m_Sector[dfSECTOR_Y_MAX][dfSECTOR_X_MAX];		// 각 섹터에 존재하는 Player 객체
	
	// 중복 account 확인을 위해 중복을 허용하는 multimap으로 사용
	// -> 이렇게 하지 않으면 중복 account 인걸 확인하여 이전 세션을 disconnect 한 후,
	// m_accountNo에 새롭게 할당된 player의 accountNo(같은 번호)를 insert 하려고 해도
	// onLeave에서 해당 accountNo를 제거하기 직전이면 insert되지 않음
	// 잠깐의 시간동안은 중복을 허용해야 함
	std::unordered_multimap<int64_t, uint64_t> m_accountNo;

	friend unsigned __stdcall JobWorkerThread(PVOID param);					// Job 일 처리 스레드
	friend unsigned __stdcall MoniteringThread(void* param);

	bool JobWorkerThread_serv();
	bool MoniterThread_serv();

	// 모니터링 관련 변수들
private:
	__int64 m_totalPlayerCnt;												// player total
	__int64 m_loginPlayerCnt;
	__int64 m_jobUpdatecnt;													// job 개수
	__int64 m_jobThreadUpdateCnt;											// job thread update 횟수
	
	__int64 m_loginPacketTPS;
	__int64 m_sectorMovePacketTPS;
	__int64 m_chattingReqTPS;
	__int64 m_chattingResTPS;
	
	__int64 m_timeoutTotalCnt;
	__int64 m_timeoutCntTPS;
	
	__int64 m_deletePlayerCnt;
	__int64 m_deletePlayerTPS;

	__int64 m_redisGetCnt;
	__int64 m_redisGetTPS;

	bool startFlag;

private:
	PerformanceMonitor performMoniter;
	MonitoringLanClient lanClient;

	std::wstring m_tempIp;
	std::string m_ip;

	CRedis_TLS* redis_TLS;
};




#endif