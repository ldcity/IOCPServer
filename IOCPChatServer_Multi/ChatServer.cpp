#include "PCH.h"
#include "ChatServer.h"

#ifdef __LOG__

#define LOGARRMAX 50000

struct stChatLog
{
	int idx;
	string shType;
	INT64 playerAccountNo;
	INT64 packAccountNo;
	ULONG64 iSessionID;
	__int64 isDisconn;
};

long arrIdx = 0;

stChatLog stChatLogs[LOGARRMAX];
#endif

// Worker Thread Call
unsigned __stdcall MoniteringThread(void* param)
{
	ChatServer* chatServ = (ChatServer*)param;

	chatServ->MoniterThread_serv();

	return 0;
}

ChatServer::ChatServer() : startFlag(false)
{

}

ChatServer::~ChatServer()
{
	if (m_mapPlayer.size() > 0)
	{
		for (auto iter = m_mapPlayer.begin(); iter != m_mapPlayer.end();)
		{
			playerPool.Free(iter->second);
			iter = m_mapPlayer.erase(iter);
		}
	}

	// m_Sector 삭제해야함


	CloseHandle(m_jobHandle);
	CloseHandle(m_jobEvent);
	CloseHandle(m_moniteringThread);
	CloseHandle(m_moniterEvent);
	CloseHandle(m_runEvent);

	//DeleteCriticalSection(&mapLock);
}

bool ChatServer::ChatServerStart()
{
	chatLog = Log(L"CharServer_Log");

	InitializeSRWLock(&mapLock);
	InitializeSRWLock(&setLock);
	InitializeSRWLock(&sectorLock);
	//InitializeCriticalSection(&mapLock);

	// chat server 설정파일 파싱
	TextParser chatServerInfoTxt;

	const wchar_t* txtName = L"ChatServer.txt";
	chatServerInfoTxt.LoadFile(txtName);

	wchar_t ip[256];
	chatServerInfoTxt.GetValue(L"SERVER.BIND_IP", ip);

	int port;
	chatServerInfoTxt.GetValue(L"SERVER.BIND_PORT", &port);

	int workerThread;
	chatServerInfoTxt.GetValue(L"SERVER.IOCP_WORKER_THREAD", &workerThread);

	int runningThread;
	chatServerInfoTxt.GetValue(L"SERVER.IOCP_ACTIVE_THREAD", &runningThread);

	int nagleOff;
	chatServerInfoTxt.GetValue(L"SERVER.NAGLE_OFF", &nagleOff);

	int sessionMAXCnt;
	chatServerInfoTxt.GetValue(L"SERVER.SESSION_MAX", &sessionMAXCnt);

	chatServerInfoTxt.GetValue(L"SERVER.USER_MAX", &m_userMAXCnt);

	int packet_code;
	chatServerInfoTxt.GetValue(L"SERVER.PACKET_CODE", &packet_code);

	int packet_key;
	chatServerInfoTxt.GetValue(L"SERVER.PACKET_KEY", &packet_key);

	chatServerInfoTxt.GetValue(L"SERVICE.TIMEOUT_DISCONNECT", &m_timeout);

	bool ret = this->Start(ip, port, workerThread, runningThread, nagleOff, sessionMAXCnt, packet_code, packet_key);
	
	if (!ret)
	{
		chatLog.logger(dfLOG_LEVEL_ERROR, __LINE__, L"NetServer Start Error");
		return false;
	}

	// Create Manual Event
	m_runEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (m_runEvent == NULL)
	{
		int eventError = WSAGetLastError();
		chatLog.logger(dfLOG_LEVEL_ERROR, __LINE__, L"CreateEvent() Error : %d", eventError);

		return false;
	}

	// Create Auto Event
	m_moniterEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (m_moniterEvent == NULL)
	{
		int eventError = WSAGetLastError();
		chatLog.logger(dfLOG_LEVEL_ERROR, __LINE__, L"CreateEvent() Error : %d", eventError);

		return false;
	}

	// Create Auto Event
	m_jobEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (m_jobEvent == NULL)
	{
		int eventError = WSAGetLastError();
		chatLog.logger(dfLOG_LEVEL_ERROR, __LINE__, L"CreateEvent() Error : %d", eventError);

		return false;
	}

	// Monitering Thread
	m_moniteringThread = (HANDLE)_beginthreadex(NULL, 0, MoniteringThread, this, CREATE_SUSPENDED, NULL);
	if (m_moniteringThread == NULL)
	{
		int threadError = GetLastError();
		chatLog.logger(dfLOG_LEVEL_ERROR, __LINE__, L"_beginthreadex() Error : %d", threadError);

		return false;
	}

	chatLog.logger(dfLOG_LEVEL_DEBUG, __LINE__, L"Create Moniterting Thread");

	WaitForSingleObject(m_moniteringThread, INFINITE);
	WaitForSingleObject(m_jobHandle, INFINITE);

	return true;
}

bool ChatServer::ChatServerStop()
{
	// Sector & Player 객체 정리

	// ChatServer 리소스 정리
	CloseHandle(m_jobHandle);
	CloseHandle(m_jobEvent);

	// NetServer 종료
	this->Stop();

	return true;
}

// Monitering Thread
bool ChatServer::MoniterThread_serv()
{
	DWORD threadID = GetCurrentThreadId();

	//logger.logger(dfLOG_LEVEL_DEBUG, __LINE__, L"MoniteringThread[%d] Start...", threadID);

	while (true)
	{
		// 1초마다 모니터링 -> 타임아웃 건도 처리
		DWORD ret = WaitForSingleObject(m_moniterEvent, 999);

		if (ret == WAIT_TIMEOUT)
		{	
			//for (auto iter = m_mapPlayer.begin(); iter != m_mapPlayer.end();)
			//{                               
			//	Player* player = iter->second;
			//	++iter;

			//	DWORD curTime = timeGetTime();

			//	if (curTime - player->recvLastTime > TIMEOUT)
			//	{
			//		InterlockedIncrement64(&m_timeoutTotalCnt);
			//		InterlockedIncrement64(&m_timeoutCntTPS);

			//		DisconnectSession(player->sessionID);
			//	}
			//}

			wprintf(L"==============================================================\n");
			wprintf(L"Total Accept : %I64d\t\t\tTotal Release : %I64d\n", acceptCount, releaseCount);
			wprintf(L"Accept TPS : %I64d\t\t\t\tRelease TPS : %I64d\n", InterlockedExchange64(&acceptTPS, 0), InterlockedExchange64(&releaseTPS, 0));
			wprintf(L"Total Recv  Call : %I64d\t\tTotal Send Call : %I64d\n", recvCallCount, sendCallCount);
			wprintf(L"Recv  Call TPS : %I64d\t\t\tSend Call TPS : %I64d\n", InterlockedExchange64(&recvCallTPS, 0), InterlockedExchange64(&sendCallTPS, 0));
			wprintf(L"Recv Pending TPS : %I64d\t\tSend Pending TPS : %I64d\n", InterlockedExchange64(&recvPendingTPS, 0), InterlockedExchange64(&sendPendingTPS, 0));
			wprintf(L"Total Recv Bytes : %I64d Bytes\tTotal Send Bytes : %I64d Bytes\n", recvBytes, sendBytes);
			wprintf(L"Recv Bytes TPS : %I64d Bytes\t\tSend Bytes TPS : %I64d Bytes\n", InterlockedExchange64(&recvBytesTPS, 0), InterlockedExchange64(&sendBytesTPS, 0));
			wprintf(L"Total Recv Packet : %I64d\t\tTotal Send Packet : %I64d\n", recvMsgCount, sendMsgCount);
			wprintf(L"Recv Packet TPS : %I64d\t\tSend Packet TPS : %I64d\n", InterlockedExchange64(&recvMsgTPS, 0), InterlockedExchange64(&sendMsgTPS, 0));
			
			wprintf(L"------------------------[Contents]----------------------------\n\n");
			wprintf(L"Total Player Cnt : %I64d\n", m_totalPlayerCnt);
			wprintf(L"Total Timeout Cnt : %I64d\t Timeout TPS : %I64d\n", m_timeoutTotalCnt, InterlockedExchange64(&m_timeoutCntTPS, 0));
			wprintf(L"Player Pool # Capacity : %llu\tUse : %llu\tAlloc : %llu\tFree : %llu\n",
				playerPool.GetCapacity(), playerPool.GetObjectUseCount(), playerPool.GetObjectAllocCount(), playerPool.GetObjectFreeCount());
			wprintf(L"Packet Pool # Capacity : %llu\tUse : %llu\tAlloc : %llu\tFree : %llu\n",
				CPacket::GetPoolCapacity(), CPacket::GetPoolUseCnt(), CPacket::GetPoolTotalAllocCnt(), CPacket::GetPoolTotalFreeCnt());
			wprintf(L"Total Job Update Cnt : %I64d\n", InterlockedExchange64(&m_jobUpdatecnt, 0));
			wprintf(L"Login Packet TPS : %I64d\t\tSectorMove Packet TPS : %I64d\t\Chatting Req TPS : %I64d\tChatting Res TPS : %I64d\n",
				InterlockedExchange64(&m_loginPacketTPS, 0), InterlockedExchange64(&m_sectorMovePacketTPS, 0),
				InterlockedExchange64(&m_chattingReqTPS, 0), InterlockedExchange64(&m_chattingResTPS, 0));
			wprintf(L"==============================================================\n\n");
		}
	}

	return true;
}

//bool ChatServer::PacketProc(uint64_t sessionID, CPacket* packet)
//{
//	WORD type;
//	*packet >> type;
//
//	switch (type)
//	{
//	case en_PACKET_CS_CHAT_REQ_LOGIN:
//		netPacketProc_Login(sessionID, packet);					// 로그인 요청
//		break;
//	case en_PACKET_CS_CHAT_REQ_SECTOR_MOVE:
//		netPacketProc_SectorMove(sessionID, packet);			// 섹터 이동 요청
//		break;
//	case en_PACKET_CS_CHAT_REQ_MESSAGE:
//		netPacketProc_Chatting(sessionID, packet);				// 채팅 보내기
//		break;
//	case en_PACKET_CS_CHAT_REQ_HEARTBEAT:
//		netPacketProc_HeartBeat(sessionID, packet);				// 하트비트
//		break;
//	default:
//		// 잘못된 패킷
//		chatLog.logger(dfLOG_LEVEL_ERROR, __LINE__, L"Packet Type Error > %d", type);
//		DisconnectSession(sessionID);
//		break;
//	}
//
//	return true;
//}

bool ChatServer::OnConnectionRequest(const wchar_t* IP, unsigned short PORT)
{

	return true;
}

//// 접속 처리 - 이 때 Player 껍데기를 미리 만들어놔야함
//// 로그인 전에 연결만 된 세션에 대해 timeout도 판단해야하기 때문에
//void ChatServer::OnClientJoin(uint64_t sessionID)
//{
//	if (!startFlag)
//	{
//		ResumeThread(m_moniteringThread);
//		startFlag = true;
//	}
//
//	CreatePlayer(sessionID);
//
//	InterlockedIncrement64(&m_jobUpdatecnt);
//}

//// 해제 처리
//void ChatServer::OnClientLeave(uint64_t sessionID)
//{
//	DeletePlayer(sessionID);
//
//	InterlockedIncrement64(&m_jobUpdatecnt);
//}

// 패킷 처리
void ChatServer::OnRecv(uint64_t sessionID, CPacket* packet)
{
	WORD type;
	*packet >> type;

	switch (type)
	{
	case en_PACKET_CS_CHAT_REQ_LOGIN:
		netPacketProc_Login(sessionID, packet);					// 로그인 요청
		break;
	case en_PACKET_CS_CHAT_REQ_SECTOR_MOVE:
		netPacketProc_SectorMove(sessionID, packet);			// 섹터 이동 요청
		break;
	case en_PACKET_CS_CHAT_REQ_MESSAGE:
		netPacketProc_Chatting(sessionID, packet);				// 채팅 보내기
		break;
	case en_PACKET_CS_CHAT_REQ_HEARTBEAT:
		netPacketProc_HeartBeat(sessionID, packet);				// 하트비트
		break;
	default:
		// 잘못된 패킷
		chatLog.logger(dfLOG_LEVEL_ERROR, __LINE__, L"Packet Type Error > %d", type);
		DisconnectSession(sessionID);
		break;
	}

	CPacket::Free(packet);

	InterlockedIncrement64(&m_jobUpdatecnt);
}

void ChatServer::OnError(int errorCode, const wchar_t* msg)
{

}


//--------------------------------------------------------------------------------------
// player 관련 함수
//--------------------------------------------------------------------------------------

// player 생성
inline bool ChatServer::CreatePlayer(uint64_t sessionID)
{
	Player* player = playerPool.Alloc();
	
	if (player == nullptr)
	{
		chatLog.logger(dfLOG_LEVEL_ERROR, __LINE__, L"Player Pool Alloc Failed!");
		chatLog.~Log();
	
		CRASH();

		return false;
	}

	player->sessionID = sessionID;
	player->accountNo = -1;

	memset(player->sessionKey, 0, MSG_MAX_LEN);
	wmemset(player->ID, 0, ID_MAX_LEN);
	wmemset(player->nickname, 0, NICKNAME_MAX_LEN);

	player->sectorX = -1;
	player->sectorY = -1;

	player->disconnect = false;
	player->login = false;

	player->recvLastTime = timeGetTime();

	AcquireSRWLockExclusive(&mapLock);
	m_mapPlayer.insert({ sessionID, player });
	ReleaseSRWLockExclusive(&mapLock);

	InterlockedIncrement64(&m_totalPlayerCnt);

#ifdef __LOG__
	int _index = InterlockedIncrement((LONG*)&arrIdx) % LOGARRMAX;
	stChatLogs[_index].idx = GetSessionIndex(sessionID);
	stChatLogs[_index].iSessionID = sessionID;
	stChatLogs[_index].playerAccountNo = player->accountNo;
	stChatLogs[_index].packAccountNo = player->accountNo;
	stChatLogs[_index].shType = "Create";
	stChatLogs[_index].isDisconn = player->disconnect;
#endif // !__LOG__

	return true;
}


//// player 검색
//inline Player* ChatServer::FindPlayer(uint64_t sessionID)
//{
//	Player* player = nullptr;
//
//	AcquireSRWLockShared(&mapLock);
//	auto iter = m_mapPlayer.find(sessionID);
//
//	if (iter == m_mapPlayer.end())
//	{
//		ReleaseSRWLockShared(&mapLock);
//		chatLog.logger(dfLOG_LEVEL_DEBUG, __LINE__, L"Player Not Found!");
//		return nullptr;
//	}
//
//	ReleaseSRWLockShared(&mapLock);
//	return iter->second;
//}

// player 삭제
inline bool ChatServer::DeletePlayer(uint64_t sessionID)
{
	Player* player = FindPlayer(sessionID);

	if (player == nullptr)
		return false;

	player->recvLastTime = timeGetTime();
	player->disconnect = true;
	player->login = false;

	// 섹터에서 해당 player 객체 삭제
	// Sector 이동도 하지 않은 상태면 x,y 좌표가 65535
	if (player->sectorX != -1 && player->sectorY != -1)
	{
		AcquireSRWLockExclusive(&sectorLock);
		m_Sector[player->sectorY][player->sectorX].erase(player);
		ReleaseSRWLockExclusive(&sectorLock);
	}

	AcquireSRWLockExclusive(&mapLock);
	
	m_mapPlayer.erase(sessionID);

	ReleaseSRWLockExclusive(&mapLock);


	AcquireSRWLockExclusive(&setLock);

	m_setAccountNo.erase(player->accountNo);

	ReleaseSRWLockExclusive(&setLock);

	playerPool.Free(player);

	// player->AccountNo != -1 일 때, 로그인 player cnt 감소시키는 방향으로 변경하기
	InterlockedDecrement64(&m_totalPlayerCnt);

#ifdef __LOG__
	int _index = InterlockedIncrement((LONG*)&arrIdx) % LOGARRMAX;
	stChatLogs[_index].idx = GetSessionIndex(sessionID);
	stChatLogs[_index].iSessionID = sessionID;
	stChatLogs[_index].playerAccountNo = player->accountNo;
	stChatLogs[_index].packAccountNo = player->accountNo;
	stChatLogs[_index].shType = "Disconnect";
	stChatLogs[_index].isDisconn = player->disconnect;
#endif // !__LOG__

	return true;
}

// player 중복 체크
inline bool ChatServer::CheckPlayer(Player* player, INT64 accountNo)
{
	//AcquireSRWLockShared(&mapLock);

	AcquireSRWLockShared(&setLock);

	auto iter = m_setAccountNo.find(accountNo);

	// map 순회는 느리므로 set으로 변경하여 검색 -> 속도개선
	if (iter != m_setAccountNo.end())
	{
		ReleaseSRWLockShared(&setLock);

		chatLog.logger(dfLOG_LEVEL_DEBUG, __LINE__,
			L"Login Request Packet > Login Duplicated! SID : %016llx\taccountNo : %I64d",  player->sessionID, accountNo);

		wprintf(L"Login Request Packet > Login Duplicated! SID : %016llx\taccountNo : %I64d", player->sessionID, accountNo);
	

		DisconnectSession(iter->second);

		AcquireSRWLockShared(&mapLock);
		auto iterPlayer = m_mapPlayer.find(iter->second);

		if (iterPlayer != m_mapPlayer.end())
		{
			iterPlayer->second->disconnect = true;
		}

		ReleaseSRWLockShared(&mapLock);

		return false;
	}
	
	ReleaseSRWLockShared(&setLock);

	//// map 순회는 느리므로 변경해야함 -> hash
	//for (auto iter = m_mapPlayer.begin(); iter != m_mapPlayer.end(); ++iter)
	//{
	//	// 다른 세션인데 accountNo가 같은 경우 - 아직 이전 player가 삭제되지 않은 상태
	//	// 이전 player disconnect
	//	if (iter->second->accountNo == accountNo && iter->first != player->sessionID)
	//	{
	//		ReleaseSRWLockShared(&mapLock);

	//		chatLog.logger(dfLOG_LEVEL_DEBUG, __LINE__,
	//			L"Login Request Packet > Login Duplicated! prevSID : %016llx \t curSID : %016llx\taccountNo : %I64d", 
	//			iter->first, player->sessionID, accountNo);

	//		wprintf(L"Login Request Packet > Login Duplicated! prevSID : %016llx \t curSID : %016llx\taccountNo : %I64d",
	//			iter->first, player->sessionID, accountNo);

	//		DisconnectSession(iter->first);
	//		iter->second->disconnect = true;

	//		return false;
	//	}
	//}

	//ReleaseSRWLockShared(&mapLock);
	
	return true;
}


//--------------------------------------------------------------------------------------
// Packet Proc
//--------------------------------------------------------------------------------------

// 로그인 요청
void ChatServer::netPacketProc_Login(uint64_t sessionID, CPacket* packet)
{
	INT64 _accountNo;
	BYTE status = true;

	// 예외 처리 - 156 
	if (packet->GetDataSize() < sizeof(INT64) + ID_MAX_LEN * sizeof(wchar_t) + NICKNAME_MAX_LEN * sizeof(wchar_t) + MSG_MAX_LEN)
	{
		int size = packet->GetDataSize();

		chatLog.logger(dfLOG_LEVEL_ERROR, __LINE__, L"Login Request Packet > Size Error : %d", size);
		DisconnectSession(sessionID);
		return;
	}

	*packet >> _accountNo;

	// player 찾기
	Player* player = FindPlayer(sessionID);

#ifdef __LOG__
	int _index = InterlockedIncrement((LONG*)&arrIdx) % LOGARRMAX;
	stChatLogs[_index].idx = GetSessionIndex(sessionID);
	stChatLogs[_index].iSessionID = sessionID;
	stChatLogs[_index].playerAccountNo = player->accountNo;
	stChatLogs[_index].packAccountNo = _accountNo;
	stChatLogs[_index].shType = "Login";
	stChatLogs[_index].isDisconn = player->disconnect;
#endif // !__LOG__

	// error -> 존재하지 않는 세션에 접근하려함
	if (player == nullptr)
	{
		chatLog.logger(dfLOG_LEVEL_ERROR, __LINE__, L"%016llx Player Not Found!", sessionID);
		status = false;
	}
	else
	{
		// 중복 체크 (같은 accountNo를 가진 플레이어가 있는지 확인)
		if (!CheckPlayer(player, _accountNo))
		{
			chatLog.logger(dfLOG_LEVEL_DEBUG, __LINE__, L"Login Request Packet > Login Duplicated!");
		}                                  

		AcquireSRWLockExclusive(&setLock);
		
		m_setAccountNo.insert({ _accountNo, sessionID });

		ReleaseSRWLockExclusive(&setLock);

		player->login = true;
		player->recvLastTime = timeGetTime();
		player->accountNo = _accountNo;
		packet->GetData((char*)player->ID, ID_MAX_LEN * sizeof(wchar_t));
		packet->GetData((char*)player->nickname, NICKNAME_MAX_LEN * sizeof(wchar_t));

		// Login Server 개발 시, 로직 변경
		packet->GetData((char*)player->sessionKey, MSG_MAX_LEN);

		InterlockedIncrement64(&m_loginPacketTPS);
	}


//#ifdef __LOG__
//	int _index = InterlockedIncrement((LONG*)&arrIdx) % LOGARRMAX;
//	stChatLogs[_index].idx = GetSessionIndex(sessionID);
//	stChatLogs[_index].iSessionID = sessionID;
//	stChatLogs[_index].iAccountNo = player->accountNo;
//	stChatLogs[_index].shType = "Login";
//	stChatLogs[_index].isDisconn = player->disconnect;
//#endif // !__LOG__

	// 로그인 응답 패킷 전송
	CPacket* resLoginPacket = CPacket::Alloc();
	
	mpResLogin(resLoginPacket, status, _accountNo);
	SendPacket(sessionID, resLoginPacket);
	
	CPacket::Free(resLoginPacket);

	//// 로그인 실패 -> disconnect
	//if (status == false)
	//{
	//	chatLog.logger(dfLOG_LEVEL_DEBUG, __LINE__, L"Login Request Packet > Login Failed!");
	//	DisconnectSession(sessionID);
	//}
}
// 섹터 이동 요청
void ChatServer::netPacketProc_SectorMove(uint64_t sessionID, CPacket* packet)
{
	INT64 accountNo;
	short sectorX;
	short sectorY;

	// 예외 처리 -> 12
	if (packet->GetDataSize() < sizeof(INT64) + sizeof(WORD) * 2)
	{
		int size = packet->GetDataSize();

		chatLog.logger(dfLOG_LEVEL_ERROR, __LINE__, L"Move Sector Request Packet > Size Error : %d", size);
		DisconnectSession(sessionID);
		return;
	}

	*packet >> accountNo >> sectorX >> sectorY;

	// Player 검색
	Player* player = FindPlayer(sessionID);

	// Player가 없는데 패킷이 들어온다???? -> error
	if (player == nullptr)
	{
		chatLog.logger(dfLOG_LEVEL_ERROR, __LINE__, L"Move Sector Request Packet > Player Not Found");
		DisconnectSession(sessionID);
		return;
	}

#ifdef __LOG__
	int _index = InterlockedIncrement((LONG*)&arrIdx) % LOGARRMAX;
	stChatLogs[_index].idx = GetSessionIndex(sessionID);
	stChatLogs[_index].iSessionID = sessionID;
	stChatLogs[_index].playerAccountNo = player->accountNo;
	stChatLogs[_index].packAccountNo = accountNo;
	stChatLogs[_index].shType = "SectorMove";
	stChatLogs[_index].isDisconn = GetSessionDisconnFlag(sessionID);
#endif // !__LOG__

	// login 됐는지 확인
	if (player->login == false)
	{
		chatLog.logger(dfLOG_LEVEL_ERROR, __LINE__, L"Move Sector Request Packet > Not Login");
		DisconnectSession(sessionID);
		return;
	}

	// accountNo 확인
	if (player->accountNo != accountNo)
	{
		chatLog.logger(dfLOG_LEVEL_ERROR, __LINE__, L"Move Sector Request Packet > AccountNo Not Equal");
		DisconnectSession(sessionID);
		return;
	}

	// 섹터 범위 확인
	if (sectorX >= dfSECTOR_X_MAX || sectorX < 0 || sectorY >= dfSECTOR_Y_MAX || sectorY < 0)
	{
		chatLog.logger(dfLOG_LEVEL_ERROR, __LINE__, L"Move Sector Request Packet > Sector Bound Error");
		DisconnectSession(sessionID);
		return;
	}

	player->recvLastTime = timeGetTime();

	// 해당 섹터에 player있는지 확인
	// 만약 현재 섹터 좌표가 -1이 아니면 처음 움직이는 게 아니므로 현재 섹터에 있는 player 객체 삭제해야함
	if (player->sectorX != -1 && player->sectorY != -1)
	{
		// 이전 섹터좌표와도 다를 경우, 이전 섹터좌표 값 삭제
		if (player->sectorX != sectorX || player->sectorY != sectorY)
		{
			AcquireSRWLockExclusive(&sectorLock);

			auto iter = m_Sector[player->sectorY][player->sectorX].find(player);

			// 섹터 이동 전에 객체 삭제
			if (iter != m_Sector[player->sectorY][player->sectorX].end())
			{
				m_Sector[player->sectorY][player->sectorX].erase(iter);
			}

			// 이전 섹터에 있던 내 player 객체 삭제 후(or 처음 움직임) 현재 섹터 위치에 추가
			player->sectorX = sectorX;
			player->sectorY = sectorY;

			m_Sector[player->sectorY][player->sectorX].emplace(player);

			ReleaseSRWLockExclusive(&sectorLock);
		}
	}
	// 처음 좌표 이동 시, 해당 좌표에 객체 추가
	else
	{
		player->sectorX = sectorX;
		player->sectorY = sectorY;

		AcquireSRWLockExclusive(&sectorLock);

		// 섹터 위치에 추가
		m_Sector[player->sectorY][player->sectorX].emplace(player);

		ReleaseSRWLockExclusive(&sectorLock);
	}

//#ifdef __LOG__
//	int _index = InterlockedIncrement((LONG*)&arrIdx) % LOGARRMAX;
//	stChatLogs[_index].idx = GetSessionIndex(sessionID);
//	stChatLogs[_index].iSessionID = sessionID;
//	stChatLogs[_index].iAccountNo = player->accountNo;
//	stChatLogs[_index].shType = "SectorMove";
//	stChatLogs[_index].isDisconn = GetSessionDisconnFlag(sessionID);
//#endif // !__LOG__

	InterlockedIncrement64(&m_sectorMovePacketTPS);

	CPacket* resPacket = CPacket::Alloc();

	mpResSectorMove(resPacket, player->accountNo, player->sectorX, player->sectorY);
	SendPacket(sessionID, resPacket);

	CPacket::Free(resPacket);
}

// 채팅 보내기
void ChatServer::netPacketProc_Chatting(uint64_t sessionID, CPacket* packet)
{
	INT64 accountNo;
	WORD msgLen;
	WCHAR* message;

	// 예외 처리
	if (packet->GetDataSize() < sizeof(INT64) + sizeof(WORD) + sizeof(wchar_t))
	{
		int size = packet->GetDataSize();

		chatLog.logger(dfLOG_LEVEL_ERROR, __LINE__, L"Chatting Request Packet > Size Error : %d", size);
		DisconnectSession(sessionID);
		return;
	}

	*packet >> accountNo >> msgLen;

	// 헤더 페이로드 크기와 실제 페이로드 크기가 다른 경우
	if (packet->GetDataSize() != msgLen)
	{
		chatLog.logger(dfLOG_LEVEL_ERROR, __LINE__, L"Chatting Request Packet > Payload Size Error");
		DisconnectSession(sessionID);
		return;
	}

	// Player 검색
	Player* player = FindPlayer(sessionID);

	// Player가 없는데 패킷이 들어온다???? -> error
	if (player == nullptr)
	{
		chatLog.logger(dfLOG_LEVEL_ERROR, __LINE__, L"Chatting Request Packet > Player Not Found");
		DisconnectSession(sessionID);
		return;
	}

#ifdef __LOG__
	int _index = InterlockedIncrement((LONG*)&arrIdx) % LOGARRMAX;
	stChatLogs[_index].idx = GetSessionIndex(sessionID);
	stChatLogs[_index].iSessionID = sessionID;
	stChatLogs[_index].playerAccountNo = player->accountNo;
	stChatLogs[_index].packAccountNo = accountNo;
	stChatLogs[_index].shType = "Chatting";
	stChatLogs[_index].isDisconn = GetSessionDisconnFlag(sessionID);
#endif // !__LOG__

	// login 됐는지 확인
	if (player->login == false)
	{
		chatLog.logger(dfLOG_LEVEL_ERROR, __LINE__, L"Move Sector Request Packet > Not Login");
		DisconnectSession(sessionID);
		return;
	}

	player->recvLastTime = timeGetTime();

	// accountNo 확인
	if (player->accountNo != accountNo)
	{
		chatLog.logger(dfLOG_LEVEL_ERROR, __LINE__, L"Chatting Request Packet > AccountNo Not Equal");
		DisconnectSession(sessionID);
		return;
	}

	// 섹터 범위 확인
	if (player->sectorX >= dfSECTOR_X_MAX || player->sectorX < 0 || player->sectorY >= dfSECTOR_Y_MAX || player->sectorY < 0)
	{
		chatLog.logger(dfLOG_LEVEL_ERROR, __LINE__, L"Chatting Request Packet > Sector Bound Error");
		DisconnectSession(sessionID);
		return;
	}

//#ifdef __LOG__
//	int _index = InterlockedIncrement((LONG*)&arrIdx) % LOGARRMAX;
//	stChatLogs[_index].idx = GetSessionIndex(sessionID);
//	stChatLogs[_index].iSessionID = sessionID;
//	stChatLogs[_index].iAccountNo = player->accountNo;
//	stChatLogs[_index].shType = "Chatting";
//	stChatLogs[_index].isDisconn = GetSessionDisconnFlag(sessionID);
//#endif // !__LOG__

	InterlockedIncrement64(&m_chattingReqTPS);

	CPacket* resPacket = CPacket::Alloc();

	mpResChatMessage(resPacket, player->accountNo, player->ID, player->nickname, msgLen, (WCHAR*)packet->GetReadBufferPtr());
	packet->MoveReadPos(msgLen);

	// player 객체 주변 섹터 구하기
	st_SECTOR_AROUND sectorAround;
	GetSectorAround(player->sectorX, player->sectorY, &sectorAround);

	//// 브로드캐스팅으로 보내기 때문에 참조카운트 증가시킨후 보내야함
	//resPacket->addRefCnt();
	for (int i = 0; i < sectorAround.iCount; i++)
	{
		AcquireSRWLockShared(&sectorLock);
		auto iter = m_Sector[sectorAround.Around[i].y][sectorAround.Around[i].x].begin();
		for (; iter != m_Sector[sectorAround.Around[i].y][sectorAround.Around[i].x].end();)
		{
			Player* otherPlayer = *iter;
			++iter;

			InterlockedIncrement64(&m_chattingResTPS);
			SendPacket(otherPlayer->sessionID, resPacket);
		}
		ReleaseSRWLockShared(&sectorLock);
	}

	//resPacket->subRefCnt();
	CPacket::Free(resPacket);
}

// 하트비트
void ChatServer::netPacketProc_HeartBeat(uint64_t sessionID, CPacket* packet)
{

	// 예외 처리 -> 하트비트 패킷은 타입 외에 추가적인 데이터가 있으면 안됨
	if (packet->GetDataSize() > 0)
	{
		chatLog.logger(dfLOG_LEVEL_ERROR, __LINE__, L"HeartBeat Request Packet > Packet is not empty");
		DisconnectSession(sessionID);
		return;
	}

	// Player 검색
	Player* player = FindPlayer(sessionID);

	// Player가 없는데 패킷이 들어온다???? -> error
	if (player == nullptr)
	{
		chatLog.logger(dfLOG_LEVEL_ERROR, __LINE__, L"HeartBeat Request Packet > Player Not Found");
		DisconnectSession(sessionID);
		return;
	}

	player->recvLastTime = timeGetTime();
}