#include "PCH.h"
#include "NetServer.h"

#ifdef __LOG__

#define NETLOGARRMAX 50000


struct stNetLog
{
	int idx;
	ULONG64 sessionID;
	__int64 ioCount;
	SOCKET socket;
	bool disconFlag;
	string type;
	__int64 start;
	__int64 end;
};

struct stRelease
{
	int idx;
	ULONG64 sessionIDPrev;
	ULONG64 sessionIDCur;
	__int64 ioCountPrev;
	__int64 ioCountCur;
	SOCKET socketPrev;
	SOCKET socketCur;
	bool disconFlagPrev;
	bool disconFlagCur;
	string type;
	__int64 start;
	__int64 end;
};

struct stSendPacket
{
	int idx;
	ULONG64 sessionIDPrev;
	ULONG64 sessionIDCur;
	__int64 ioCountPrev;
	__int64 ioCountCur;
	SOCKET socket;
	int packRefCnt;
	bool disconFlagPrev;
	bool disconFlagCur;
	string type;
	__int64 start;
	__int64 end;
};

struct stDisconnect
{
	int idx;
	ULONG64 sessionIDPrev;
	ULONG64 sessionIDCur;
	__int64 ioCountPrev;
	__int64 ioCountCur;
	SOCKET socket;
	int packRefCnt;
	bool disconFlagPrev;
	bool disconFlagCur;
	string type;
	__int64 start;
	__int64 end;
};

int recvIndex = 0;
int releaseIndex = 0;
int sendPacketIndex = 0;
int disconnectIndex = 0;

__int64 startNum = 0;
__int64 endNum = 0;

stNetLog recvNetLogArr[NETLOGARRMAX];
stRelease releaseLogArr[NETLOGARRMAX];
stSendPacket sendPackLogArr[NETLOGARRMAX];
stDisconnect disconnectLogArr[NETLOGARRMAX];

#endif // __LOG__

// ========================================================================
// Thread Call
// ========================================================================
// Accept Thread Call
unsigned __stdcall AcceptThread(void* param)
{
	NetServer* lanServ = (NetServer*)param;

	lanServ->AcceptThread_serv();

	return 0;
}

// Worker Thread Call
unsigned __stdcall WorkerThread(void* param)
{
	NetServer* lanServ = (NetServer*)param;

	lanServ->mWorkerThread_serv();

	return 0;
}

// Control Thread Call
unsigned __stdcall ControlThread(void* param)
{
	NetServer* lanServ = (NetServer*)param;

	lanServ->mControlThread_serv();

	return 0;
}


NetServer::NetServer() : ListenSocket(INVALID_SOCKET), ServerPort(0), IOCPHandle(0), mAcceptThread(0), mWorkerThreads{ 0 },  mControlThread(0),
RunEvent(0), ExitEvent(0), SessionArray{ nullptr }, acceptCount(0), releaseCount(0), recvMsgTPS(0), sendMsgTPS(0),
recvMsgCount(0), sendMsgCount(0), recvCallTPS(0), sendCallTPS(0), recvCallCount(0), sendCallCount(0), recvPendingTPS(0), sendPendingTPS(0),
recvBytesTPS(0), sendBytesTPS(0), recvBytes(0), sendBytes(0), s_workerThreadCount(0), s_runningThreadCount(0), s_maxAcceptCount(0), startMonitering(false)
{
	// ========================================================================
	// Initialize
	// ========================================================================
	logger = Log(L"Server Start");

	wprintf(L"Initializing...\n");

	timeBeginPeriod(1);

	startTime = timeGetTime();

	WSADATA  wsaData;

	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
	{
		int initError = WSAGetLastError();
		logger.logger(dfLOG_LEVEL_ERROR, __LINE__, L"WSAStartup() Error : %d", initError);

		return;
	}

	//InitializeCriticalSection(&cs);
}

NetServer::~NetServer()
{
	if (SessionArray != nullptr)
		delete[] SessionArray;

	WaitForMultipleObjects((DWORD)mTotalThreads.size(), &mTotalThreads[0], TRUE, INFINITE);

	//DeleteCriticalSection(&cs);

	CloseHandle(IOCPHandle);
}

bool NetServer::Start(const wchar_t* IP, unsigned short PORT, int createWorkerThreadCnt, int runningWorkerThreadCnt, bool nagelOff, int maxAcceptCnt, unsigned char packet_code, unsigned char packet_key)
{
	//if (maxAcceptCnt <= MAX_SESSION)
	//	s_maxAcceptCount = maxAcceptCnt;
	//else
	//	s_maxAcceptCount = MAX_SESSION;

	CPacket::SetCode(packet_code);
	CPacket::SetKey(packet_key);

	s_maxAcceptCount = maxAcceptCnt;

	SessionArray = new stSESSION[s_maxAcceptCount];

	// 프리 인덱스를 오름차순으로 꺼내기 위해 max index부터 push
	for (int i = s_maxAcceptCount - 1; i >= 0; i--)
	{
		// 사용 가능한 인덱스 push
		AvailableIndexStack.Push(i);
	}

	// Create Listen Socket
	ListenSocket = socket(AF_INET, SOCK_STREAM, 0);

	if (INVALID_SOCKET == ListenSocket)
	{
		int sockError = WSAGetLastError();
		logger.logger(dfLOG_LEVEL_ERROR, __LINE__, L"socket() Error : %d", sockError);

		return false;
	}

	SOCKADDR_IN serverAddr;
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(PORT);
	InetPtonW(AF_INET, IP, &serverAddr.sin_addr);

	// bind
	if (bind(ListenSocket, (SOCKADDR*)&serverAddr, sizeof(SOCKADDR_IN)) == SOCKET_ERROR)
	{
		int bindError = WSAGetLastError();
		logger.logger(dfLOG_LEVEL_ERROR, __LINE__, L"bind() Error : %d", bindError);

		return false;
	}

	// TCP Send Buffer Remove - zero copy
	int sendVal = 0;
	if (setsockopt(ListenSocket, SOL_SOCKET, SO_SNDBUF, (const char*)&sendVal, sizeof(sendVal)) == SOCKET_ERROR)
	{
		int setsockoptError = WSAGetLastError();
		logger.logger(dfLOG_LEVEL_ERROR, __LINE__, L"setsockopt() Error : %d", setsockoptError);

		return false;
	}

	if (nagelOff)
	{
		// Nagle off
		if (setsockopt(ListenSocket, IPPROTO_TCP, TCP_NODELAY, (const char*)&nagelOff, sizeof(nagelOff)) == SOCKET_ERROR)
		{
			int setsockoptError = WSAGetLastError();
			logger.logger(dfLOG_LEVEL_ERROR, __LINE__, L"setsockopt() Error : %d", setsockoptError);

			return false;
		}
	}

	// TIME_WAIT off
	struct linger ling;
	ling.l_onoff = 1;
	ling.l_linger = 0;
	if (setsockopt(ListenSocket, SOL_SOCKET, SO_LINGER, (const char*)&ling, sizeof(ling)) == SOCKET_ERROR)
	{
		int setsockoptError = WSAGetLastError();
		logger.logger(dfLOG_LEVEL_ERROR, __LINE__, L"setsockopt() Error : %d", setsockoptError);

		return false;
	}

	// listen
	if (listen(ListenSocket, SOMAXCONN) == SOCKET_ERROR)
	{
		int listenError = WSAGetLastError();
		logger.logger(dfLOG_LEVEL_ERROR, __LINE__, L"listen() Error : %d", listenError);

		return false;
	}

	// Create Manual Event
	ExitEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (ExitEvent == NULL)
	{
		int eventError = WSAGetLastError();
		logger.logger(dfLOG_LEVEL_ERROR, __LINE__, L"CreateEvent() Error : %d", eventError);

		return false;
	}

	SYSTEM_INFO si;
	GetSystemInfo(&si);

	// CPU Core Counting
	// Worker Thread 개수가 0 이하라면, 코어 개수 * 2 로 재설정
	if (createWorkerThreadCnt <= 0)
		s_workerThreadCount = si.dwNumberOfProcessors * 2;
	else
		s_workerThreadCount = createWorkerThreadCnt;

	// Running Thread가 CPU Core 개수를 초과한다면 CPU Core 개수로 재설정
	if (runningWorkerThreadCnt > si.dwNumberOfProcessors)
		s_runningThreadCount = si.dwNumberOfProcessors;
	else
		s_runningThreadCount = runningWorkerThreadCnt;

	// Create I/O Completion Port
	IOCPHandle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, s_runningThreadCount);
	if (IOCPHandle == NULL)
	{
		int iocpError = WSAGetLastError();
		logger.logger(dfLOG_LEVEL_ERROR, __LINE__, L"CreateIoCompletionPort() Error : %d", iocpError);

		return false;
	}

	logger.logger(dfLOG_LEVEL_SYSTEM, __LINE__, L"Init Success");

	// ========================================================================
	// Create Thread
	// ========================================================================
	logger.logger(dfLOG_LEVEL_SYSTEM, __LINE__, L"Create Thread");

	// Accept Thread
	mAcceptThread = (HANDLE)_beginthreadex(NULL, 0, AcceptThread, this, 0, NULL);
	if (AcceptThread == NULL)
	{
		int threadError = GetLastError();
		logger.logger(dfLOG_LEVEL_ERROR, __LINE__, L"_beginthreadex() Error : %d", threadError);

		return false;
	}

	mTotalThreads.push_back(mAcceptThread);

	// Worker Thread
	mWorkerThreads.resize(s_workerThreadCount);
	for (int i = 0; i < mWorkerThreads.size(); i++)
	{
		mWorkerThreads[i] = (HANDLE)_beginthreadex(NULL, 0, WorkerThread, this, 0, NULL);
		if (mWorkerThreads[i] == NULL)
		{
			int threadError = GetLastError();
			logger.logger(dfLOG_LEVEL_ERROR, __LINE__, L"_beginthreadex() Error : %d", threadError);

			return false;
		}
		mTotalThreads.push_back(mWorkerThreads[i]);
	}

	logger.logger(dfLOG_LEVEL_DEBUG, __LINE__, L"Create Worker Thread > Count : %d", s_workerThreadCount);

	// Control Thread
	mControlThread = (HANDLE)_beginthreadex(NULL, 0, ControlThread, this, 0, NULL);
	if (mControlThread == NULL)
	{
		int threadError = GetLastError();
		logger.logger(dfLOG_LEVEL_ERROR, __LINE__, L"_beginthreadex() Error : %d", threadError);

		return false;
	}
	mTotalThreads.push_back(mControlThread);

	logger.logger(dfLOG_LEVEL_DEBUG, __LINE__, L"Create Worker Thread > Count : %d", s_workerThreadCount);

	return true;
}

// Accept Thread
bool NetServer::AcceptThread_serv()
{
	DWORD threadID = GetCurrentThreadId();

	//logger.logger(dfLOG_LEVEL_DEBUG, __LINE__, L"AcceptThread[%d] Start...", threadID);
	wprintf(L"AcceptThread[%d] Start...\n", threadID);

	SOCKADDR_IN clientAddr;
	int addrLen = sizeof(SOCKADDR_IN);
	SOCKET clientSocket;
	DWORD recvBytes;

	// 접속한 클라 정보 출력
	wchar_t szClientIP[16] = { 0 };


	unsigned long long s_sessionUniqueID = 0;				// Unique Value

	while (true)
	{
		// accept
		clientSocket = accept(ListenSocket, (SOCKADDR*)&clientAddr, &addrLen);

		if (clientSocket == INVALID_SOCKET)
		{
			int acceptError = GetLastError();
			logger.logger(dfLOG_LEVEL_ERROR, __LINE__, L"[TheradID : %d] accept() Error : %d", threadID, acceptError);

			break;
			//continue;
		}

		// 세션 수
		InterlockedIncrement64(&acceptTPS);
		InterlockedIncrement64(&acceptCount);

		//// 첫 접속 이후부터 모니터링 실행되도록 함
		//if (!startMonitering)
		//{
		//	ResumeThread(mMoniteringThread);
		//	startMonitering = true;
		//}

		InetNtopW(AF_INET, &clientAddr.sin_addr, szClientIP, 16);

		if (!OnConnectionRequest(szClientIP, ntohs(clientAddr.sin_port)))
		{
			// 접속 거부
			// ...
			closesocket(clientSocket);
			break;
		}

		// 사용 가능한 index 추출
		int index;

		// 비어있는 배열 찾기
		// index stack이 비어있으면 배열이 다 사용중 (full!)
		if (!AvailableIndexStack.Pop(&index))
		{
			// 방금 accept 했던 소켓 종료
			closesocket(clientSocket);
			//logger.logger(dfLOG_LEVEL_ERROR, __LINE__, L"Seesion Array Full!");
			continue;
		}
		
		// 해당 index의 세션 셋팅
		stSESSION* session = &SessionArray[index];

		// 세션 참조카운트 증가 -> accept 함수 마지막에 정리 
		// -> accept 단계에서 소켓 얻은 후, 세션 사용을 할 때, refcount가 0이 되는 걸 방지
		// increment가 아니라 exchange하는 이유 
		// -> 이전에 사용했던 세션이면 release하면서 남아있던 release flag 잔재 지우기 위해
		InterlockedExchange64(&session->ioRefCount, 1);

		session->sessionID = CreateSessionID(++s_sessionUniqueID, index);

		__int64 id = session->sessionID;

		session->m_socketClient = clientSocket;
		wcscpy_s(session->IP_str, _countof(session->IP_str), szClientIP);
		session->PORT = ntohs(clientAddr.sin_port);

		// 종료flag 셋팅
		InterlockedExchange8((char*)&session->isDisconnected, false);

		// IOCP와 소켓 연결
		// 세션 주소값이 키 값
		if (CreateIoCompletionPort((HANDLE)clientSocket, IOCPHandle, (ULONG_PTR)session, 0) == NULL)
		{
			int ciocpError = WSAGetLastError();

			//// 인자에 잘못된 소켓 핸들값이 들어갔을 경우 에러 -> 다른 쓰레드에서 소켓 해제했을 경우 발생
			logger.logger(dfLOG_LEVEL_ERROR, __LINE__, L"AcceptThread - CreateIoCompletionPort Error : %d", ciocpError);
			//wprintf(L"AcceptThread - CreateIoCompletionPort Error : %d", ciocpError);

			// accept후 바로 반환하는 것이므로 iocount 감소 후 io 완료 유무 확인
			if (0 == InterlockedDecrement64((LONG64*)&session->ioRefCount))
			{
				ReleaseSession(session);
				continue;
			}
		}

		// 접속 처리
		// 사전에 받아놓은 id를 매개변수로 전달 -> 세션의 id 멤버변수에 직접 접근해 전달하게 되면
		// 그 사이에 재할당되어 다른 세션이 될 때, 해당 id를 가진 player가 삭제될 수 없음
		// ex) 더미 클라이언트 껐을 때, 세션은 다 정리됐는데 player가 남아있는 문제 발생
		OnClientJoin(id);

		InterlockedIncrement64(&sessionCnt);

		// 비동기 recv I/O 요청
		RecvPost(session);

		// accept에서 올린 참조카운트 감소
		if (0 == InterlockedDecrement64(&session->ioRefCount))
		{
			ReleaseSession(session);
		}
	}

	//logger.logger(dfLOG_LEVEL_DEBUG, __LINE__, L"AcceptThread[%d] Exit...", threadID);

	return true;
}

// Worker Thread
bool NetServer::mWorkerThread_serv()
{
	DWORD threadID = GetCurrentThreadId();

	//logger.logger(dfLOG_LEVEL_DEBUG, __LINE__, L"WorkerThread[%d] Start...", threadID);

	stSESSION* pSession = nullptr;
	BOOL bSuccess = true;
	DWORD cbTransferred = 0;
	ULONG_PTR completionKey = 0;
	LPOVERLAPPED lpOverlapped = nullptr;

	while (true)
	{
		// 초기화
		cbTransferred = 0;
		completionKey = 0;
		lpOverlapped = nullptr;

		// GQCS call
		// client가 send조차 하지않고 바로 disconnect 할 경우 -> WorkerThread에서 recv 0을 위한 GQCS가 깨어남
		bSuccess = GetQueuedCompletionStatus(IOCPHandle, &cbTransferred, &completionKey,
			&lpOverlapped, INFINITE);

		// IOCP Error or TIMEOUT or PQCS로 직접 NULL 던짐
		// 세션 멤버변수에서 error counting을 하여 5회 이상 발생했을 시, 해제 작업 들어가는 아이디어도 있긴 함
		// 완료통지가 안왔을 경우, 이번 루프를 skip
		if (lpOverlapped == NULL)
		{
			int iocpError = WSAGetLastError();
			wprintf(L"WorkerThread[%d] Error : %d\n", threadID, iocpError);

			/*	logger.logger(dfLOG_LEVEL_ERROR, __LINE__, L"IOCP Error or TIMEOUT : %d\n", iocpError);*/

			PostQueuedCompletionStatus(IOCPHandle, 0, NULL, NULL);
			break;
		}

		// 완료통지 성공
		// Type Casting
		LPOVERLAPPED pOverlapped = (LPOVERLAPPED)lpOverlapped;
		pSession = (stSESSION*)completionKey;

#ifdef __LOG__
		int _index = InterlockedIncrement((LONG*)&recvIndex) % NETLOGARRMAX;
		recvNetLogArr[_index].start = InterlockedIncrement64(&startNum);
		recvNetLogArr[_index].sessionID = pSession->sessionID;
#endif

		bool completionOK = false;

		// Send / Recv Proc
		if (pOverlapped == &pSession->m_stRecvOverlapped && cbTransferred > 0)
		{
#ifdef __LOG__
			recvNetLogArr[_index].type = "RecvProc";
#endif
			completionOK = RecvProc(pSession, cbTransferred);

			//// RingBuffer에 Enqueue Fail
			//if (!completionOK)
			//	logger.logger(dfLOG_LEVEL_ERROR, __LINE__, L"Recv Proc Error");
		}
		else if (pOverlapped == &pSession->m_stSendOverlapped && cbTransferred > 0)
		{
#ifdef __LOG__
			recvNetLogArr[_index].type = "SendPrcoc";
#endif
			completionOK = SendProc(pSession, cbTransferred);

			//if (pSession->sendQ.GetSize() > 0)
			//	SendPost(pSession);

			//// RingBuffer에 Enqueue Fail
			//if (!completionOK)
			//	logger.logger(dfLOG_LEVEL_ERROR, __LINE__, L"Send Proc Error");
		}
		else if (cbTransferred == 0)
		{
#ifdef __LOG__
			recvNetLogArr[_index].type = "Release";
#endif
		}

		// I/O 완료 통지가 더이상 없다면 세션 해제 작업
		if (0 == InterlockedDecrement64(&pSession->ioRefCount))
		{
			ReleaseSession(pSession);
		}

#ifdef __LOG__
		recvNetLogArr[_index].end = InterlockedIncrement64(&endNum);
		recvNetLogArr[_index].idx = GetSessionIndex(pSession->sessionID);
		recvNetLogArr[_index].ioCount = pSession->ioRefCount;
		recvNetLogArr[_index].disconFlag = pSession->isDisconnected;
		recvNetLogArr[_index].socket = pSession->m_socketClient;
#endif // __LOG__
	}

	//logger.logger(dfLOG_LEVEL_DEBUG, __LINE__, L"WorkerThread[%d] Exit...", threadID);

	return true;
}

//// Monitering Thread
//bool NetServer::mMoniterThread_serv()
//{
//	// 처음 Session 생성 시, 이벤트 호출하여 모니터링 시작
//	WaitForSingleObject(RunEvent, INFINITE);
//
//	DWORD threadID = GetCurrentThreadId();
//
//	//logger.logger(dfLOG_LEVEL_DEBUG, __LINE__, L"MoniteringThread[%d] Start...", threadID);
//
//	while (true)
//	{
//		// 1초마다 모니터링
//		DWORD ret = WaitForSingleObject(MoniterEvent, 1000);
//
//		if (ret == WAIT_TIMEOUT)
//		{
//			__int64 _acceptTPS = InterlockedExchange64(&acceptTPS, 0);
//			__int64 _releaseTPS = InterlockedExchange64(&releaseTPS, 0);
//			__int64 _recvMsgTPS = InterlockedExchange64(&recvMsgTPS, 0);
//			__int64 _sendMsgTPS = InterlockedExchange64(&sendMsgTPS, 0);
//			__int64 _recvCallTPS = InterlockedExchange64(&recvCallTPS, 0);
//			__int64 _sendCallTPS = InterlockedExchange64(&sendCallTPS, 0);
//			__int64 _recvPendingTPS = InterlockedExchange64(&recvPendingTPS, 0);
//			__int64 _sendPendingTPS = InterlockedExchange64(&sendPendingTPS, 0);
//			__int64 _recvBytesTPS = InterlockedExchange64(&recvBytesTPS, 0);
//			__int64 _sendBytesTPS = InterlockedExchange64(&sendBytesTPS, 0);
//
//			wprintf(L"-------------------------------------------------------------\n");
//			wprintf(L"Total Accept : %I64d\t\t\tTotal Release : %I64d\n", InterlockedOr64(&acceptCount, 0), InterlockedOr64(&releaseCount, 0));
//			wprintf(L"Accept TPS : %I64d\t\t\t\tRelease TPS : %I64d\n", _acceptTPS, _releaseTPS);
//			wprintf(L"Total Recv  Call : %I64d\t\tTotal Send Call : %I64d\n", InterlockedOr64(&recvCallCount, 0), InterlockedOr64(&sendCallCount, 0));
//			wprintf(L"Recv  Call TPS : %I64d\t\t\tSend Call TPS : %I64d\n", _recvCallTPS, _sendCallTPS);
//			wprintf(L"Recv Pending TPS : %I64d\t\tSend Pending TPS : %I64d\n", _recvPendingTPS, _sendPendingTPS);
//			wprintf(L"Total Recv Bytes : %I64d Bytes\tTotal Send Bytes : %I64d Bytes\n", InterlockedOr64(&recvBytes, 0), InterlockedOr64(&sendBytes, 0));
//			wprintf(L"Recv Bytes TPS : %I64d Bytes\t\tSend Bytes TPS : %I64d Bytes\n", _recvBytesTPS, _sendBytesTPS);
//			wprintf(L"Total Recv Packet : %I64d\t\tTotal Send Packet : %I64d\n", InterlockedOr64(&recvMsgCount, 0), InterlockedOr64(&sendMsgCount, 0));
//			wprintf(L"Recv Packet TPS : %I64d\t\tSend Packet TPS : %I64d\n", _recvMsgTPS, _sendMsgTPS);
//			wprintf(L"PacketnPool # Capacity : %llu\tUse : %llu\tAlloc : %llu\tFree : %llu\n",
//				CPacket::PacketPool.GetCapacityCount(), CPacket::PacketPool.GetUseCount(), CPacket::PacketPool.GetAllocCount(), CPacket::PacketPool.GetFreeCount());
//			wprintf(L"-------------------------------------------------------------\n\n");
//		}
//	}
//
//	return true;
//}

// Control Thread
bool NetServer::mControlThread_serv()
{
	// 처음 Session 생성 시, 이벤트 호출하여 컨트롤 시작
	WaitForSingleObject(RunEvent, INFINITE);

	DWORD threadID = GetCurrentThreadId();

	while (true)
	{
		//if (WaitForSingleObject(ExitEvent, 0) == WAIT_OBJECT_0)
		//	break;

		// q key press
		if (GetAsyncKeyState(0x51) & 0x8000)
		{
			//Stop();
			//SetEvent(ExitEvent);
			logger.~Log();

		}

		// end key
		if (GetAsyncKeyState(VK_END) & 0x8000)
		{
			logger.~Log();
			CRASH();
		}


	}

	return true;
}

bool NetServer::RecvProc(stSESSION* pSession, long cbTransferred)
{
	pSession->recvRingBuffer.MoveWritePtr(cbTransferred);

	int useSize = pSession->recvRingBuffer.GetUseSize();

	// Recv Message Process
	while (useSize > 0)
	{
//#ifdef __LOG__
//		int _index = InterlockedIncrement((LONG*)&recvIndex) % NETLOGARRMAX;
//		recvNetLogArr[_index].sessionID = GetSessionID(pSession->sessionID);
//		recvNetLogArr[_index].index = GetSessionIndex(pSession->sessionID);
//		recvNetLogArr[_index].ioCount = pSession->ioRefCount;
//		recvNetLogArr[_index].disconFlag = pSession->isDisconnected;
//#endif // __LOG__

		NetHeader header;

		// Header 크기만큼 있는지 확인
		if (useSize <= sizeof(NetHeader))
			break;

		// Header Peek
		pSession->recvRingBuffer.Peek((char*)&header, sizeof(NetHeader));

		// packet code 확인
		if (header.code != CPacket::GetCode())
			return false;

		// Len 확인 (음수거나 받을 수 있는 패킷 크기보다 클 때
		if (header.len < 0 || header.len > MAX_PACKET_LEN)
			return false;

		// Packet 크기만큼 있는지 확인
		if (useSize < sizeof(NetHeader) + header.len)
			break;

		//// Header 이동
		//pSession->recvRingBuffer.MoveReadPtr(sizeof(NetHeader));

		// packet alloc
		CPacket* packet = CPacket::Alloc();

		// payload 크기만큼 데이터 Dequeue
		pSession->recvRingBuffer.Dequeue(packet->GetNetPayloadPtr(), header.len + sizeof(NetHeader));

		// payload 크기만큼 packet write pos 이동
		packet->MoveWritePos(header.len);

		// 디코딩하여 원본 데이터 비교
		if (!packet->Decoding())
		{
			//logger.logger(dfLOG_LEVEL_ERROR, __LINE__, L"Decoding Failed...%016llx", pSession->sessionID);
			
			CPacket::Free(packet);

			DisconnectSession(pSession->sessionID);

			return false;
			//CRASH();
		}

		// Total Recv Message Count
		InterlockedIncrement64((LONG64*)&recvMsgCount);

		// Recv Message TPS
		InterlockedIncrement64((LONG64*)&recvMsgTPS);

		// Total Recv Bytes
		InterlockedAdd64((LONG64*)&recvBytes, header.len);

		// Recv Bytes TPS
		InterlockedAdd64((LONG64*)&recvBytesTPS, header.len);

		// 컨텐츠 쪽 recv 처리
		OnRecv(pSession->sessionID, packet);

		useSize = pSession->recvRingBuffer.GetUseSize();
	}

	// Recv 재등록
	RecvPost(pSession);


	return true;
}

bool NetServer::SendProc(stSESSION* pSession, long cbTransferred)
{
	// sendPost에서 사이즈 0일 경우를 걸러냈는데도 이 조건이 발생하는 경우는 error
	if (pSession->sendPacketCount == 0)
		CRASH();

	int totalSendBytes = 0;

	// 패킷 제거
	int iSendCount;
	int packetCnt = InterlockedExchange((LONG*)&pSession->sendPacketCount, 0);

	for (iSendCount = 0; iSendCount < packetCnt; iSendCount++)
	{
		totalSendBytes += pSession->SendPackets[iSendCount]->GetNetPayloadDataSize();
		CPacket::Free(pSession->SendPackets[iSendCount]);
		//pSession->SendPackets[iSendCount] = nullptr;
	}

	// Total Send Bytes
	InterlockedAdd64((long long*)&sendBytes, totalSendBytes);

	// Send Bytes TPS
	InterlockedAdd64((long long*)&sendBytesTPS, totalSendBytes);

	// Total Send Message Count
	InterlockedAdd64((long long*)&sendMsgCount, packetCnt);

	// Send Message TPS
	InterlockedAdd64((long long*)&sendMsgTPS, packetCnt);

	// 전송 중 flag를 다시 미전송 상태로 되돌리기
	InterlockedExchange8((char*)&pSession->sendFlag, false);

	if (pSession->sendQ.GetSize() > 0)
		SendPost(pSession);

	return true;
}

bool NetServer::RecvPost(stSESSION* pSession)
{
	// Release 진행 중 or 이미 Release 이후
	if ((pSession->ioRefCount & RELEASEMASKING) && pSession->isDisconnected)
	{
		return false;
	}

	// 링버퍼 등록
	WSABUF wsa[2] = { 0 };
	int wsaCnt = 1;
	DWORD flags = 0;

	int freeSize = pSession->recvRingBuffer.GetFreeSize();
	int directEequeueSize = pSession->recvRingBuffer.DirectEnqueueSize();

	if (freeSize == 0)
		return false;

	wsa[0].buf = pSession->recvRingBuffer.GetWriteBufferPtr();
	wsa[0].len = directEequeueSize;

	// 링버퍼 내부에서 빈 공간이 두 섹션으로 나뉠 경우
	if (freeSize > directEequeueSize)
	{
		wsa[1].buf = pSession->recvRingBuffer.GetBufferPtr();
		wsa[1].len = freeSize - directEequeueSize;
		++wsaCnt;
	}

	// recv overlapped I/O 구조체 reset
	ZeroMemory(&pSession->m_stRecvOverlapped, sizeof(OVERLAPPED));

	// recv
	// ioCount : WSARecv 완료 통지가 리턴보다 먼저 떨어질 수 있으므로 WSARecv 호출 전에 증가시켜야 함
	InterlockedIncrement64(&pSession->ioRefCount);
	int recvRet = WSARecv(pSession->m_socketClient, wsa, wsaCnt, NULL, &flags, &pSession->m_stRecvOverlapped, NULL);
	InterlockedIncrement64(&recvCallCount);
	InterlockedIncrement64(&recvCallTPS);

	// 예외처리
	if (recvRet == SOCKET_ERROR)
	{
		int recvError = WSAGetLastError();

		if (recvError != WSA_IO_PENDING)
		{
			if (recvError != ERROR_10054 && recvError != ERROR_10058 && recvError != ERROR_10060)
			{	// 에러
				//wprintf(L"Recv Error : %d\n", recvError);
				/*logger.logger(dfLOG_LEVEL_ERROR, __LINE__, L"WSARecv() Error : %d", recvError);*/
				OnError(recvError, L"RecvPost # WSARecv Error\n");
			}

			// Pending이 아닐 경우, 완료 통지 실패
			if (0 == InterlockedDecrement64(&pSession->ioRefCount))
			{
				CRASH();
				ReleaseSession(pSession);
			}
			return false;
		}
		// Pending일 경우
		else
		{
			InterlockedIncrement64(&recvPendingTPS);
			
			// Pending 걸렸는데, 이 시점에 disconnect되면 비동기 io 정리해줘야함
			if (pSession->isDisconnected)
			{
				CancelIoEx((HANDLE)pSession->m_socketClient, NULL);
			}

		}
	}

	return true;
}

bool NetServer::SendPost(stSESSION* pSession)
{
	// 1회 송신 제한을 위한 flag 확인
	// false -> true 면 최초 사용
	// true -> true 면 최초 사용이 아님
	if (true == InterlockedExchange8((char*)&pSession->sendFlag, true))
		return false;

	// SendQ가 비어있을 수 있음
	// -> 다른 스레드에서 Dequeue 진행했을 경우
	if (pSession->sendQ.GetSize() == 0)
	{
		InterlockedExchange8((char*)&pSession->sendFlag, false);
		
		//return false;

		// 그 사이에 SendQ에 Enqueue 됐다면 다시 SendPost Call 
		if (pSession->sendQ.GetSize() > 0)
			SendPost(pSession);
	
		return false;
	}


	//if (_sendCount > MAX_WSA_BUF)
	//	_sendCount = MAX_WSA_BUF;

	//// send 링버퍼에 있는 packet 포인터 개수
	//int _sendCount = (int)pSession->sendQ.GetSize();

	int deqIdx = 0;

	// 링버퍼 등록
	WSABUF wsa[MAX_WSA_BUF] = { 0 };

	while (pSession->sendQ.Dequeue(pSession->SendPackets[deqIdx]))
	{
		wsa[deqIdx].buf = pSession->SendPackets[deqIdx]->GetNetPayloadPtr();
		wsa[deqIdx].len = pSession->SendPackets[deqIdx]->GetNetPayloadDataSize();

		deqIdx++;

		if (deqIdx >= MAX_WSA_BUF)
			break;
	}

	//int iCount;

	//for (iCount = 0; iCount < deqIdx; iCount++)
	//{
	//	// Send 링버퍼에 있는 패킷 포인터 뽑아와서 WSABUF 등록
	//	if (pSession->sendQ.Dequeue(pSession->SendPackets[iCount]))
	//	{
	//		wsa[iCount].buf = pSession->SendPackets[iCount]->GetNetPayloadPtr();
	//		wsa[iCount].len = pSession->SendPackets[iCount]->GetNetPayloadDataSize();
	//	}
	//}

	pSession->sendPacketCount = deqIdx;

	// send overlapped I/O 구조체 reset
	ZeroMemory(&pSession->m_stSendOverlapped, sizeof(OVERLAPPED));

	// send
	// ioCount : WSASend 완료 통지가 리턴보다 먼저 떨어질 수 있으므로 WSASend 호출 전에 증가시켜야 함
	InterlockedIncrement64(&pSession->ioRefCount);
	int sendRet = WSASend(pSession->m_socketClient, wsa, deqIdx, NULL, 0, &pSession->m_stSendOverlapped, NULL);
	InterlockedIncrement64(&sendCallCount);
	InterlockedIncrement64(&sendCallTPS);

	// 예외처리
	if (sendRet == SOCKET_ERROR)
	{
		int sendError = WSAGetLastError();

		// default error는 무시
		if (sendError != WSA_IO_PENDING)
		{
			if (sendError != ERROR_10054 && sendError != ERROR_10058 && sendError != ERROR_10060)
			{
				//wprintf(L"Send Error : %d\n", sendError);
				//// 에러
				//logger.logger(dfLOG_LEVEL_ERROR, __LINE__, L"WSASend() Error : %d", sendError);
				OnError(sendError, L"SendPost # WSASend Error\n");
			}

			// Pending이 아닐 경우, 완료 통지 실패 -> IOCount값 복원
			if (0 == InterlockedDecrement64(&pSession->ioRefCount))
			{
				CRASH();
				ReleaseSession(pSession);
			}


			return false;
		}
		else
			InterlockedIncrement64(&sendPendingTPS);
	}

	return true;
}

bool NetServer::SendPacket(uint64_t sessionID, CPacket* packet)
{
#ifdef __LOG__
	int _startNum = InterlockedIncrement64(&startNum);
	int _index = InterlockedIncrement((LONG*)&sendPacketIndex) % NETLOGARRMAX;
	sendPackLogArr[_index].start = _startNum;
	sendPackLogArr[_index].sessionIDPrev = sessionID;
	sendPackLogArr[_index].packRefCnt = packet->GetRefCnt();
#endif // __LOG


	//packet->addRefCnt();

	// find session할 때도 io 참조카운트 증가시켜서 보장받아야함
	
	// index 찾기 -> out of indexing 예외 처리
	int index = GetSessionIndex(sessionID);
	if (index < 0 || index >= s_maxAcceptCount)
	{
		//logger.logger(dfLOG_LEVEL_ERROR, __LINE__, L"Session Index Bound Error : %d", index);
		//CPacket::Free(packet);
		return false;
	}

	stSESSION* pSession = &SessionArray[index];

	if (pSession == nullptr)
	{
#ifdef __LOG__
		sendPackLogArr[_index].sessionIDCur = -1;
		sendPackLogArr[_index].type = "sendPacketSessionNull";
		sendPackLogArr[_index].end = InterlockedIncrement64(&endNum);
#endif // __LOG

		wprintf(L"Session Not Found\n");

		return false;
	}

	// 세션 사용 참조카운트 증가 & Release 중인지 동시 확인
	// Release 비트값이 1이면 ReleaseSession 함수에서 ioCount = 0, releaseFlag = 1 인 상태
	// -> 이 사이에 Release 함수에서 ioCount도 0인걸 확인한 상태임 (decrement 안해도 됨)
	if ((InterlockedIncrement64(&pSession->ioRefCount) & RELEASEMASKING))
	{
#ifdef __LOG__
		sendPackLogArr[_index].end = InterlockedIncrement64(&endNum);
		sendPackLogArr[_index].sessionIDCur = pSession->sessionID;
		sendPackLogArr[_index].ioCountCur = pSession->ioRefCount;
		sendPackLogArr[_index].type = "sendPacketReleaseIng...";
		sendPackLogArr[_index].disconFlagCur = pSession->isDisconnected;
#endif // __LOG

		return false;
	}

#ifdef __LOG__
	sendPackLogArr[_index].idx = index;
	sendPackLogArr[_index].ioCountPrev = pSession->ioRefCount;
	sendPackLogArr[_index].socket = pSession->m_socketClient;
	sendPackLogArr[_index].disconFlagPrev = pSession->isDisconnected;
#endif // __LOG

	// ------------------------------------------------------------------------------------
	// Release 수행 없이 이곳에서만 세션 사용하려는 상태
	// but 내 세션이 맞는지 다시 확인 (다른 곳에서 세션 해제 & 할당되어, 다른 세션이 됐을 수도 있음)
	// 내 세션이 아닐 경우, 이전에 증가했던 ioCount를 되돌려야 함 (되돌리지 않으면 재할당 세션의 io가 0이 될 수가 없음)
	if (sessionID != pSession->sessionID)
	{
#ifdef __LOG__
		sendPackLogArr[_index].end = InterlockedIncrement64(&endNum);
		sendPackLogArr[_index].sessionIDCur = pSession->sessionID;
		sendPackLogArr[_index].ioCountCur = pSession->ioRefCount;
		sendPackLogArr[_index].type = "sendPacketUse/sessionID!=pSessionID";
		sendPackLogArr[_index].disconFlagCur = pSession->isDisconnected;
#endif // __LOG
		if (0 == InterlockedDecrement64(&pSession->ioRefCount))
		{
#ifdef __LOG__
			sendPackLogArr[_index].sessionIDCur = pSession->sessionID;
			sendPackLogArr[_index].ioCountCur = pSession->ioRefCount;
			sendPackLogArr[_index].type = "sendpacketUse/refcnt=0#1";
#endif

			wprintf(L"SendPacket # ReAlloc Session -> Cur Session ID : %016llx\tPrev Session ID : %016llx\n", sessionID, pSession->sessionID);

			ReleaseSession(pSession);
		}

		return false;
	}

	// disconnect 진입
	if (pSession->isDisconnected)
	{
#ifdef __LOG__
		sendPackLogArr[_index].end = InterlockedIncrement64(&endNum);
		sendPackLogArr[_index].sessionIDCur = pSession->sessionID;
		sendPackLogArr[_index].ioCountCur = pSession->ioRefCount;
		sendPackLogArr[_index].type = "sendPacketDisconnecting...";
		sendPackLogArr[_index].disconFlagCur = pSession->isDisconnected;
#endif // __LOG

		if (0 == InterlockedDecrement64(&pSession->ioRefCount))
		{
#ifdef __LOG__
			sendPackLogArr[_index].sessionIDCur = pSession->sessionID;
			sendPackLogArr[_index].ioCountCur = pSession->ioRefCount;
			sendPackLogArr[_index].type = "sendpacketUse/refcnt=0#0";
#endif

			wprintf(L"SendPacket # Session Disconnected...ID : %016llx\n", pSession->sessionID);
			ReleaseSession(pSession);
		}
		return false;
	}

	//// 헤더 셋팅
	//packet->SetNetHeaderPtr();

//#ifdef __LOG__
//	int _index = InterlockedIncrement((LONG*)&sendIndex) % NETLOGARRMAX;
//	sendNetLogArr[_index].sessionID = pSession->sessionID;
//	sendNetLogArr[_index].index = GetSessionIndex(pSession->sessionID);
//	sendNetLogArr[_index].ioCount = pSession->ioRefCount;
//	sendNetLogArr[_index].disconFlag = pSession->isDisconnected;
//	sendNetLogArr[_index].sendFlag = pSession->sendFlag;
//#endif // __LOG__

	// 헤더 셋팅 & 인코딩
	packet->Encoding();

	// Enqueue한 패킷을 다른 곳에서 사용하므로 패킷 참조카운트 증가 -> Dequeue할 때 감소
	packet->addRefCnt();

	// packet 포인터 enqueue
	pSession->sendQ.Enqueue(packet);

	// 한번에 Send 등록
	SendPost(pSession);

	// sendPacket 함수에서 증가시킨 세션 참조 카운트 감소
	if (0 == InterlockedDecrement64(&pSession->ioRefCount))
	{
#ifdef __LOG__
		sendPackLogArr[_index].end = InterlockedIncrement64(&endNum);
		sendPackLogArr[_index].sessionIDCur = pSession->sessionID;
		sendPackLogArr[_index].ioCountCur = pSession->ioRefCount;
		sendPackLogArr[_index].type = "sendpacketLast/refcnt=0";
#endif
		ReleaseSession(pSession);

		return false;
	}

#ifdef __LOG__
	sendPackLogArr[_index].end = InterlockedIncrement64(&endNum);
	sendPackLogArr[_index].sessionIDCur = pSession->sessionID;
	sendPackLogArr[_index].ioCountCur = pSession->ioRefCount;
	sendPackLogArr[_index].type = "sendpacketFinished";
	sendPackLogArr[_index].disconFlagCur = pSession->isDisconnected;
#endif // __LOG
}

void NetServer::ReleaseSession(stSESSION* pSession)
{

#ifdef __LOG__
	int _startNum = InterlockedIncrement64(&startNum);
	int _index = InterlockedIncrement((LONG*)&releaseIndex) % NETLOGARRMAX;
	releaseLogArr[_index].start = _startNum;
	releaseLogArr[_index].sessionIDPrev = pSession->sessionID;
	releaseLogArr[_index].idx = GetSessionIndex(pSession->sessionID);
	releaseLogArr[_index].ioCountPrev = pSession->ioRefCount;
	releaseLogArr[_index].socketPrev = pSession->m_socketClient;
	releaseLogArr[_index].disconFlagPrev = pSession->isDisconnected;
#endif // __LOG

	// 세션 송신 다 마치고 종료해야함 
	// 세션 메모리 정리
	// 소켓 정리
	// 세선 해제
	// ioCount == 0 && releaseFlag == 0 => release = 1 (인터락 함수로 해결)
	// 다른 곳에서 해당 세션을 사용하는지 확인
	if (InterlockedCompareExchange64(&pSession->ioRefCount, RELEASEMASKING, 0) != 0)
	{
#ifdef __LOG__
		releaseLogArr[_index].end = InterlockedIncrement64(&endNum);
		releaseLogArr[_index].ioCountCur = pSession->ioRefCount;
		releaseLogArr[_index].sessionIDCur = pSession->sessionID;
		releaseLogArr[_index].disconFlagCur = pSession->isDisconnected;
		releaseLogArr[_index].type = "releaseAnotherUsing";
#endif // __LOG

		return;
	}

	//-----------------------------------------------------------------------------------
	// Release 실제 진입부
	//-----------------------------------------------------------------------------------
	//InterlockedExchange8((char*)&pSession->isDisconnected, true);
	//pSession->isDisconnected = true;

	//ioCount = 0, releaseFlag = 1 인 상태

	uint64_t sessionID = pSession->sessionID;

	pSession->sessionID = -1;

	SOCKET sock = pSession->m_socketClient;

	// 소켓 Invalid 처리하여 더이상 해당 소켓으로 I/O 못받게 함
	pSession->m_socketClient = INVALID_SOCKET;

	// recv는 더이상 받으면 안되므로 소켓 close
	closesocket(sock);

	// 링버퍼 내부에 남아있는 거 처리
	pSession->recvRingBuffer.ClearBuffer();

	// Send Packet 관련 리소스 정리
	// SendQ에서 Dqeueue하여 SendPacket 배열에 넣었지만 아직 WSASend 못해서 남아있는 패킷 정리
	for (int iSendCount = 0; iSendCount < pSession->sendPacketCount; iSendCount++)
	{
		CPacket::Free(pSession->SendPackets[iSendCount]);
	}

	pSession->sendPacketCount = 0;

	// SendQ에 남아있다는 건 WSABUF에 꽂아넣지도 못한 상태 
	if (pSession->sendQ.GetSize() > 0)
	{
		CPacket* packet = nullptr;
		while (pSession->sendQ.Dequeue(packet))
		{
			CPacket::Free(packet);
		}
	}

	ZeroMemory(pSession->IP_str, sizeof(pSession->IP_str));
	pSession->IP_num = 0;
	pSession->PORT = 0;
	pSession->LastRecvTime = 0;
	ZeroMemory(&pSession->m_stSendOverlapped, sizeof(OVERLAPPED));
	ZeroMemory(&pSession->m_stRecvOverlapped, sizeof(OVERLAPPED));

	//pSession->sendFlag = false;

	InterlockedExchange8((char*)&pSession->sendFlag, false);

	// 미사용 index를 stack에 push
	AvailableIndexStack.Push(GetSessionIndex(sessionID));

	// 접속중인 client 수 차감
	InterlockedDecrement64(&acceptCount);

	// 접속 해제한 clinet 수 증가
	InterlockedIncrement64(&releaseCount);
	InterlockedIncrement64(&releaseTPS);

	// 사용자(Player) 관련 리소스 해제 (호출 후에 해당 세션이 사용되면 안됨)
	OnClientLeave(sessionID);

#ifdef __LOG__
	releaseLogArr[_index].end = InterlockedIncrement64(&endNum);
	releaseLogArr[_index].ioCountCur = pSession->ioRefCount;
	releaseLogArr[_index].sessionIDCur = pSession->sessionID;
	releaseLogArr[_index].disconFlagCur = pSession->isDisconnected;
	releaseLogArr[_index].socketCur = pSession->m_socketClient;
	releaseLogArr[_index].type = "releaseFinished";
#endif // __LOG
}

bool NetServer::DisconnectSession(uint64_t sessionID)
{
#ifdef __LOG__
	int _startNum = InterlockedIncrement64(&startNum);
	int _index = InterlockedIncrement((LONG*)&disconnectIndex) % NETLOGARRMAX;
	disconnectLogArr[_index].start = _startNum;
	disconnectLogArr[_index].sessionIDPrev = sessionID;
#endif // __LOG

	// 세션 검색
	// index 찾기
	int index = GetSessionIndex(sessionID);
	if (index < 0 || index >= s_maxAcceptCount)
	{
		//logger.logger(dfLOG_LEVEL_ERROR, __LINE__, L"Session Index Bound Error : %d", index);
		//CPacket::Free(packet);
		return false;
	}

	stSESSION* pSession = &SessionArray[index];

	if (pSession == nullptr)
	{
#ifdef __LOG__
		disconnectLogArr[_index].sessionIDCur = -1;
		disconnectLogArr[_index].type = "disconnectSessionNull";
		disconnectLogArr[_index].end = InterlockedIncrement64(&endNum);
#endif // __LOG

		//logger.logger(dfLOG_LEVEL_DEBUG, __LINE__, L"Session Not Found");
		wprintf(L"Session Not Found\n");

		//if (0 == InterlockedDecrement64(&pSession->ioRefCount))
		//	ReleaseSession(pSession);

		return false;
	}

	// 세션 사용 참조카운트 증가 & Release 중인지 동시 확인
	// Release 비트값이 1이면 ReleaseSession 함수에서 ioCount = 0, releaseFlag = 1 인 상태
	// -> 이 사이에 ioCount도 0인걸 확인한 상태임 (decrement 안해도 됨)
	if (InterlockedIncrement64(&pSession->ioRefCount) & RELEASEMASKING)
	{
#ifdef __LOG__
		disconnectLogArr[_index].end = InterlockedIncrement64(&endNum);
		disconnectLogArr[_index].sessionIDCur = pSession->sessionID;
		disconnectLogArr[_index].ioCountCur = pSession->ioRefCount;
		disconnectLogArr[_index].type = "disconnectReleaseIng...";
		disconnectLogArr[_index].disconFlagCur = pSession->isDisconnected;
#endif // __LOG
		return false;
	}

#ifdef __LOG__
	disconnectLogArr[_index].idx = index;
	disconnectLogArr[_index].ioCountPrev = pSession->ioRefCount;
	disconnectLogArr[_index].socket = pSession->m_socketClient;
	disconnectLogArr[_index].disconFlagPrev = pSession->isDisconnected;
#endif // __LOG

	// Release 수행 없이 이곳에서만 세션 사용하려는 상태
	// but 내 세션이 맞는지 다시 확인 (다른 곳에서 세션 해제&할당되어, 다른 세션이 됐을 수도 있음)
	// 이전에 증가했던 ioCount를 되돌려야 함 (0이면 Release 진행)
	if (sessionID != pSession->sessionID)
	{
#ifdef __LOG__
		disconnectLogArr[_index].end = InterlockedIncrement64(&endNum);
		disconnectLogArr[_index].sessionIDCur = pSession->sessionID;
		disconnectLogArr[_index].ioCountCur = pSession->ioRefCount;
		disconnectLogArr[_index].type = "disconnectUse/sessionID!=pSessionID";
		disconnectLogArr[_index].disconFlagCur = pSession->isDisconnected;
#endif // __LOG

		//logger.logger(dfLOG_LEVEL_DEBUG, __LINE__, L"DisconnectSession # Session sessionID failed : %016llx\n", sessionID);
		
		//wprintf(L"DisconnectSession # Session sessionID failed : %016llx\n", sessionID);

		if (0 == InterlockedDecrement64(&pSession->ioRefCount))
		{
#ifdef __LOG__
			disconnectLogArr[_index].sessionIDCur = pSession->sessionID;
			disconnectLogArr[_index].ioCountCur = pSession->ioRefCount;
			disconnectLogArr[_index].type = "disconnectUse/refcnt=0#1";
#endif
			ReleaseSession(pSession);
		}

		return false;
	}

	// 이미 해제된 세션 or 해제중인 세션일수도 있음
	// -> 이 사이에 Release 함수에서 ioCount도 0인걸 확인한 상태
	if (pSession->isDisconnected)
	{
#ifdef __LOG__
		disconnectLogArr[_index].end = InterlockedIncrement64(&endNum);
		disconnectLogArr[_index].sessionIDCur = pSession->sessionID;
		disconnectLogArr[_index].ioCountCur = pSession->ioRefCount;
		disconnectLogArr[_index].type = "disconnectDisconnecting...";
		disconnectLogArr[_index].disconFlagCur = pSession->isDisconnected;
#endif // __LOG

		//logger.logger(dfLOG_LEVEL_DEBUG, __LINE__, L"DisconnectSession # Session already released : %016llx\n", sessionID);
		//wprintf(L"DisconnectSession # Session already released : %016llx\n", sessionID);

		if (0 == InterlockedDecrement64(&pSession->ioRefCount))
		{
#ifdef __LOG__
			disconnectLogArr[_index].sessionIDCur = pSession->sessionID;
			disconnectLogArr[_index].ioCountCur = pSession->ioRefCount;
			disconnectLogArr[_index].type = "disconnectUse/refcnt=0#0";
#endif

			ReleaseSession(pSession);
		}
		return false;
	}

	// ------------------------ Disconnect 확정 ------------------------
	// 그냥 closesocket을 하게 되면 closesocket 함수와 CancelIoEx 함수 사이에서 해제된 세션이 
	// 재할당되어 다른 세션이 될 수 있음
	// 그때 재할당된 세션의 IO 작업들이 CancelIoEx에 의해 제거되는 문제 발생
	// disconnected flag를 true로 변경하면 sendPacket 과 recvPost 함수 진입을 막음
	//pSession->isDisconnected = true;
	InterlockedExchange8((char*)&pSession->isDisconnected, true);

	// 현재 IO 작업 모두 제거
	CancelIoEx((HANDLE)pSession->m_socketClient, NULL);

	// Disconnect 함수에서 증가시킨 세션 참조 카운트 감소
	if (0 == InterlockedDecrement64(&pSession->ioRefCount))
	{
#ifdef __LOG__
		disconnectLogArr[_index].end = InterlockedIncrement64(&endNum);
		disconnectLogArr[_index].sessionIDCur = pSession->sessionID;
		disconnectLogArr[_index].ioCountCur = pSession->ioRefCount;
		disconnectLogArr[_index].type = "disconnectLast/refcnt=0";
#endif

		ReleaseSession(pSession);

		return false;
	}

#ifdef __LOG__
	disconnectLogArr[_index].end = InterlockedIncrement64(&endNum);
	disconnectLogArr[_index].sessionIDCur = pSession->sessionID;
	disconnectLogArr[_index].ioCountCur = pSession->ioRefCount;
	disconnectLogArr[_index].type = "disconnectFinished";
	disconnectLogArr[_index].disconFlagCur = pSession->isDisconnected;
#endif // __LOG

	return true;
}




void NetServer::Stop()
{
	WaitForMultipleObjects((DWORD)mTotalThreads.size(), &mTotalThreads[0], TRUE, INFINITE);
}


int NetServer::GetSessionCount()
{
	return 1;
}

#ifdef __LOG__
bool NetServer::GetSessionDisconnFlag(uint64_t sessionID)
{
	// 세션 검색
	// index 찾기
	int index = GetSessionIndex(sessionID);

	stSESSION* pSession = &SessionArray[index];

	return pSession->isDisconnected;
}
#endif

