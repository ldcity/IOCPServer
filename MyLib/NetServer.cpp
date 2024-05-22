#include "PCH.h"
#include "NetServer.h"


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

// Worker Thread Call
unsigned __stdcall MoniteringThread(void* param)
{
	NetServer* lanServ = (NetServer*)param;

	lanServ->mMoniterThread_serv();

	return 0;
}

// Running Thread call
unsigned __stdcall RunningThread(void* param)
{
	NetServer* lanServ = (NetServer*)param;

	lanServ->mRunningThread_serv();

	return 0;
}


NetServer::NetServer() : ListenSocket(INVALID_SOCKET), ServerPort(0), IOCPHandle(0), mAcceptThread(0), mWorkerThreads{ 0 }, mUpdateThread(0), mMoniteringThread(0), mControlThread(0),
RunEvent(0), ExitEvent(0), SessionArray{ nullptr }, acceptCount(0), releaseCount(0), recvMsgTPS(0), sendMsgTPS(0),
recvMsgCount(0), sendMsgCount(0), recvCallTPS(0), sendCallTPS(0), recvCallCount(0), sendCallCount(0), recvPendingTPS(0), sendPendingTPS(0),
recvBytesTPS(0), sendBytesTPS(0), recvBytes(0), sendBytes(0), s_workerThreadCount(0), s_runningThreadCount(0), s_maxAcceptCount(0), s_sessionUniqueID(0), startMonitering(false)
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
	InetPton(AF_INET, IP, &serverAddr.sin_addr);

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
	RunEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (RunEvent == NULL)
	{
		int eventError = WSAGetLastError();
		logger.logger(dfLOG_LEVEL_ERROR, __LINE__, L"CreateEvent() Error : %d", eventError);

		return false;
	}

	// Create Auto Event
	MoniterEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (MoniterEvent == NULL)
	{
		int eventError = WSAGetLastError();
		logger.logger(dfLOG_LEVEL_ERROR, __LINE__, L"CreateEvent() Error : %d", eventError);

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

	// Create Auto Event
	JobEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (JobEvent == NULL)
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

	// Running Thread가 CPU Core 개수를 초과한다면 CPU Core 개수로 재설정
	if (s_runningThreadCount > si.dwNumberOfProcessors)
		s_runningThreadCount = si.dwNumberOfProcessors;

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

	// Monitering Thread
	mMoniteringThread = (HANDLE)_beginthreadex(NULL, 0, MoniteringThread, this, CREATE_SUSPENDED, NULL);
	if (mMoniteringThread == NULL)
	{
		int threadError = GetLastError();
		logger.logger(dfLOG_LEVEL_ERROR, __LINE__, L"_beginthreadex() Error : %d", threadError);

		return false;
	}
	mTotalThreads.push_back(mMoniteringThread);

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

	//// Running Thread
	//mRunningThread = (HANDLE)_beginthreadex(NULL, 0, RunningThread, this, 0, NULL);
	//if (mRunningThread == NULL)
	//{
	//	int threadError = GetLastError();
	//	logger.logger(dfLOG_LEVEL_ERROR, __LINE__, L"_beginthreadex() Error : %d", threadError);

	//	return false;
	//}
	//mTotalThreads.push_back(mRunningThread);

	//logger.logger(dfLOG_LEVEL_DEBUG, __LINE__, L"Create Running Thread > Count : %d", s_workerThreadCount);

	//SetEvent(RunEvent);

	WaitForMultipleObjects((DWORD)mTotalThreads.size(), &mTotalThreads[0], TRUE, INFINITE);

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

	while (true)
	{
		// accept
		clientSocket = accept(ListenSocket, (SOCKADDR*)&clientAddr, &addrLen);

		if (clientSocket == INVALID_SOCKET)
		{
			int acceptError = GetLastError();
			//logger.logger(dfLOG_LEVEL_ERROR, __LINE__, L"[TheradID : %d] accept() Error : %d", threadID, acceptError);

			break;
			//continue;
		}

		// 첫 접속 이후부터 모니터링 실행되도록 함
		if (!startMonitering)
		{
			ResumeThread(mMoniteringThread);
			startMonitering = true;
		}

		InetNtop(AF_INET, &clientAddr.sin_addr, szClientIP, 16);

		if (!OnConnectionRequest(szClientIP, ntohs(clientAddr.sin_port)))
		{
			// 접속 거부
			// ...
			closesocket(clientSocket);
			break;
		}

		InterlockedIncrement64(&acceptTPS);
		InterlockedIncrement64(&acceptCount);

		/*logger.logger(dfLOG_LEVEL_DEBUG, __LINE__, L"[ThreadID : %d / Client Accept] IP = %s, Port Number = %d",
			threadID, InetNtop(AF_INET, &clientAddr.sin_addr, szClientIP, 16), ntohs(clientAddr.sin_port));*/


			// 비어있는 배열 찾기
			// index stack이 비어있으면 배열이 다 사용중 (full!)
		if (AvailableIndexStack.GetSize() == 0)
		{
			// 방금 accept 했던 소켓 종료
			closesocket(clientSocket);
			//logger.logger(dfLOG_LEVEL_ERROR, __LINE__, L"Seesion Array Full!");
			continue;
		}

		// 사용 가능한 index 추출
		int index;
		AvailableIndexStack.Pop(&index);


		stSESSION* session = &SessionArray[index];
		session->sessionID = CreateSessionID(s_sessionUniqueID++, index);
		session->m_socketClient = clientSocket;
		wcscpy_s(session->IP_str, _countof(session->IP_str), szClientIP);
		session->PORT = ntohs(clientAddr.sin_port);

		//// Unique Value와 Index를 파싱하여 고유한 SessionID 생성
		//SessionArray[index]->sessionID = CreateSessionID(s_sessionUniqueID++, index);
		//SessionArray[index]->m_socketClient = clientSocket;
		//SessionArray[index]->useFlag = true;



		// IOCP와 소켓 연결
		// 세션 주소값이 키 값
		if (CreateIoCompletionPort((HANDLE)clientSocket, IOCPHandle, (ULONG_PTR)session, 0) == NULL)
		{
			int ciocpError = WSAGetLastError();

			//// 인자에 잘못된 소켓 핸들값이 들어갔을 경우 에러 -> 다른 쓰레드에서 소켓 해제했을 경우 발생
			//logger.logger(dfLOG_LEVEL_ERROR, __LINE__, L"AcceptThread - CreateIoCompletionPort Error : %d", ciocpError);
			wprintf(L"AcceptThread - CreateIoCompletionPort Error : %d", ciocpError);

			//// accept후 바로 반환하는 것이므로 iocount 감소 후 io 완료 유무 확인
			//if (0 >= InterlockedDecrement64((LONG64*)&session->ioCount))
			//{
			//	ReleaseSession(session->sessionID);
			//	continue;
			//}
		}

		// 접속 처리
		OnClientJoin(session->sessionID);

		// 비동기 recv I/O 요청
		RecvPost(session);
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

		bool completionOK = false;

		// Send / Recv Proc
		if (pOverlapped == &pSession->m_stRecvOverlapped && cbTransferred > 0)
		{
			completionOK = RecvProc(pSession, cbTransferred);

			//// RingBuffer에 Enqueue Fail
			//if (!completionOK)
			//	logger.logger(dfLOG_LEVEL_ERROR, __LINE__, L"Recv Proc Error");
		}
		else if (pOverlapped == &pSession->m_stSendOverlapped && cbTransferred > 0)
		{
			completionOK = SendProc(pSession, cbTransferred);

			//if (pSession->sendQ.GetSize() > 0)
			//	SendPost(pSession);

			//// RingBuffer에 Enqueue Fail
			//if (!completionOK)
			//	logger.logger(dfLOG_LEVEL_ERROR, __LINE__, L"Send Proc Error");
		}

		//LeaveCriticalSection(&pSession->sessionLock);

		// I/O 완료 통지가 더이상 없다면 세션 해제 작업
		if (0 == InterlockedDecrement64(&pSession->ioRefCount))
		{
			ReleaseSession(pSession);
		}
	}

	//logger.logger(dfLOG_LEVEL_DEBUG, __LINE__, L"WorkerThread[%d] Exit...", threadID);

	return true;
}

// Monitering Thread
bool NetServer::mMoniterThread_serv()
{
	// 처음 Session 생성 시, 이벤트 호출하여 모니터링 시작
	WaitForSingleObject(RunEvent, INFINITE);

	DWORD threadID = GetCurrentThreadId();

	//logger.logger(dfLOG_LEVEL_DEBUG, __LINE__, L"MoniteringThread[%d] Start...", threadID);

	while (true)
	{
		// 1초마다 모니터링
		DWORD ret = WaitForSingleObject(MoniterEvent, 1000);

		if (ret == WAIT_TIMEOUT)
		{
			__int64 _acceptTPS = InterlockedExchange64(&acceptTPS, 0);
			__int64 _releaseTPS = InterlockedExchange64(&releaseTPS, 0);
			__int64 _recvMsgTPS = InterlockedExchange64(&recvMsgTPS, 0);
			__int64 _sendMsgTPS = InterlockedExchange64(&sendMsgTPS, 0);
			__int64 _recvCallTPS = InterlockedExchange64(&recvCallTPS, 0);
			__int64 _sendCallTPS = InterlockedExchange64(&sendCallTPS, 0);
			__int64 _recvPendingTPS = InterlockedExchange64(&recvPendingTPS, 0);
			__int64 _sendPendingTPS = InterlockedExchange64(&sendPendingTPS, 0);
			__int64 _recvBytesTPS = InterlockedExchange64(&recvBytesTPS, 0);
			__int64 _sendBytesTPS = InterlockedExchange64(&sendBytesTPS, 0);

			wprintf(L"-------------------------------------------------------------\n");
			wprintf(L"Total Accept : %I64d\t\t\tTotal Release : %I64d\n", InterlockedOr64(&acceptCount, 0), InterlockedOr64(&releaseCount, 0));
			wprintf(L"Accept TPS : %I64d\t\t\t\tRelease TPS : %I64d\n", _acceptTPS, _releaseTPS);
			wprintf(L"Total Recv  Call : %I64d\t\tTotal Send Call : %I64d\n", InterlockedOr64(&recvCallCount, 0), InterlockedOr64(&sendCallCount, 0));
			wprintf(L"Recv  Call TPS : %I64d\t\t\tSend Call TPS : %I64d\n", _recvCallTPS, _sendCallTPS);
			wprintf(L"Recv Pending TPS : %I64d\t\tSend Pending TPS : %I64d\n", _recvPendingTPS, _sendPendingTPS);
			wprintf(L"Total Recv Bytes : %I64d Bytes\tTotal Send Bytes : %I64d Bytes\n", InterlockedOr64(&recvBytes, 0), InterlockedOr64(&sendBytes, 0));
			wprintf(L"Recv Bytes TPS : %I64d Bytes\t\tSend Bytes TPS : %I64d Bytes\n", _recvBytesTPS, _sendBytesTPS);
			wprintf(L"Total Recv Packet : %I64d\t\tTotal Send Packet : %I64d\n", InterlockedOr64(&recvMsgCount, 0), InterlockedOr64(&sendMsgCount, 0));
			wprintf(L"Recv Packet TPS : %I64d\t\tSend Packet TPS : %I64d\n", _recvMsgTPS, _sendMsgTPS);
			wprintf(L"PacketnPool # Capacity : %llu\tUse : %llu\tAlloc : %llu\tFree : %llu\n",
				CPacket::PacketPool.GetCapacityCount(), CPacket::PacketPool.GetUseCount(), CPacket::PacketPool.GetAllocCount(), CPacket::PacketPool.GetFreeCount());
			wprintf(L"-------------------------------------------------------------\n\n");
		}
	}

	return true;
}

// Control Thread
bool NetServer::mControlThread_serv()
{
	// 처음 Session 생성 시, 이벤트 호출하여 컨트롤 시작
	WaitForSingleObject(RunEvent, INFINITE);

	DWORD threadID = GetCurrentThreadId();

	while (true)
	{
		if (WaitForSingleObject(ExitEvent, 0) == WAIT_OBJECT_0)
			break;

		// q key press
		if (GetAsyncKeyState(0x51) & 0x8000)
		{
			Stop();
			SetEvent(ExitEvent);
		}
	}

	return true;
}

// Control Thread
bool NetServer::mRunningThread_serv()
{
	// 처음 Session 생성 시, 이벤트 호출하여 컨트롤 시작
	WaitForSingleObject(RunEvent, INFINITE);

	DWORD threadID = GetCurrentThreadId();

	while (true)
	{
		// JobQ에 Job이 삽입되면 이벤트 발생하여 깨어남
		WaitForSingleObject(JobEvent, INFINITE);

		Job* job = nullptr;

		while (JobQ.GetSize() > 0)
		{
			JobQ.Dequeue(job);

			switch (job->type)
			{
			case JOBTYPE::ECHO:
			{
				CPacket* sendPacket = CPacket::Alloc();
				*sendPacket << job->data;
				SendPacket(job->sessionID, sendPacket);
			}
			break;
			default:
				break;
			}
		}
	}

	return true;
}

bool NetServer::RecvProc(stSESSION* pSession, long cbTransferred)
{
	// Total Recv Bytes
	InterlockedAdd64((LONG64*)&recvBytes, cbTransferred);

	// Recv Bytes TPS
	InterlockedAdd64((LONG64*)&recvBytesTPS, cbTransferred);

	pSession->recvRingBuffer.MoveWritePtr(cbTransferred);

	int useSize = pSession->recvRingBuffer.GetUseSize();

	// Recv Message Process
	while (useSize > 0)
	{
		NetHeader header;

		// Header 크기만큼 있는지 확인
		if (useSize <= sizeof(NetHeader))
			break;

		// Header Peek
		pSession->recvRingBuffer.Peek((char*)&header, sizeof(NetHeader));

		// Packet 크기만큼 있는지 확인
		if (useSize < sizeof(NetHeader) + header.len)
			break;

		// Header 이동
		pSession->recvRingBuffer.MoveReadPtr(sizeof(NetHeader));

		// packet alloc
		CPacket* packet = CPacket::Alloc();

		// payload 크기만큼 데이터 Dequeue
		pSession->recvRingBuffer.Dequeue(packet->GetWriteBufferPtr(), header.len);

		// payload 크기만큼 packet write pos 이동
		packet->MoveWritePos(header.len);

	
		//EnterCriticalSection(&cs);
		//recvDataQ.push(*(__int64*)(packet->GetReadBufferPtr()));
		//LeaveCriticalSection(&cs);

		// 컨텐츠 쪽 recv 처리
		OnRecv(pSession->sessionID, packet);

		// Total Recv Message Count
		InterlockedIncrement64((LONG64*)&recvMsgCount);

		// Recv Message TPS
		InterlockedIncrement64((LONG64*)&recvMsgTPS);

		// packet free
		CPacket::Free(packet);

		useSize = pSession->recvRingBuffer.GetUseSize();
	}

	// Recv 재등록
	RecvPost(pSession);


	return true;
}

bool NetServer::SendProc(stSESSION* pSession, long cbTransferred)
{
	// send 링버퍼에 없다는 건 recv 링버퍼에서 제대로 받아오지 못했거나 recv가 느린 것
	if (pSession->sendPacketCount == 0)
		CRASH();

	int totalSendBytes = 0;

	// 패킷 제거
	int iSendCount;
	CPacket* p = nullptr;

	int packetCnt = pSession->sendPacketCount;

	for (iSendCount = 0; iSendCount < packetCnt; iSendCount++)
	{
		totalSendBytes += pSession->SendPackets[iSendCount]->GetDataSize();
		CPacket::Free(pSession->SendPackets[iSendCount]);
		pSession->SendPackets[iSendCount] = nullptr;
		--pSession->sendPacketCount;
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
	//LeaveCriticalSection(&pSession->sessionLock);

	if (pSession->sendQ.GetSize() > 0)
		SendPost(pSession);

	return true;
}

bool NetServer::RecvPost(stSESSION* pSession)
{
	// Release 진행 중 or 이미 Release한 세션이라면 더이상 데이터 받으면 안됨
	if (pSession->isDisconnected)
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
			}

			// Pending이 아닐 경우, 완료 통지 실패
			if (0 == InterlockedDecrement64(&pSession->ioRefCount))
			{
				ReleaseSession(pSession);
			}
			return false;
		}
		else
			InterlockedIncrement64(&recvPendingTPS);
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

	// send 링버퍼에 있는 packet 포인터 개수
	int _sendCount = (int)pSession->sendQ.GetSize();

	// Send 처음 진입인데, Recv와 순서가 뒤섞여서 Recv보다 먼저 발생할 경우, Send 링버퍼에 데이터 없을 수 있음
	// -> 그에 대한 예외처리로 다음 Recv 후에 Send 처리가 될 수 있도록 sendFlag를 다시 false로 변경
	if (_sendCount == 0)
	{
		InterlockedExchange8((char*)&pSession->sendFlag, false);

		if (pSession->sendQ.GetSize() > 0)
			return false;
	}

	if (_sendCount > MAX_WSA_BUF)
		_sendCount = MAX_WSA_BUF;

	// 링버퍼 등록
	WSABUF wsa[MAX_WSA_BUF] = { 0 };

	for (int iCount = 0; iCount < _sendCount; iCount++)
	{
		// Send 링버퍼에 있는 패킷 포인터 뽑아와서 WSABUF 등록
		pSession->sendQ.Dequeue(pSession->SendPackets[iCount]);

		wsa[iCount].buf = pSession->SendPackets[iCount]->GetNetPayloadPtr();
		wsa[iCount].len = pSession->SendPackets[iCount]->GetNetPayloadDataSize();

		//EnterCriticalSection(&cs);
		//sendDataQ.push(*(__int64*)((char*)(pSession->SendPackets[iCount]->GetReadBufferPtr()) + 2));
		//LeaveCriticalSection(&cs);
	}

	/*for (int i = 0; i < _sendCount; i++)
	{
		if (sendDataQ.front() != recvDataQ.front())
		{
			if (sendDataQ.front() > recvDataQ.front())
			{
				while (sendDataQ.front() > recvDataQ.front() && !recvDataQ.empty())
					recvDataQ.pop();

				if (recvDataQ.empty())
					CRASH();
			}
			else
			{
				while (sendDataQ.front() < recvDataQ.front() && !sendDataQ.empty())
					sendDataQ.pop();

				if (sendDataQ.empty())
					CRASH();
			}

			if (sendDataQ.front() != recvDataQ.front())
				CRASH();
		}

		EnterCriticalSection(&cs);
		sendDataQ.pop();
		recvDataQ.pop();
		LeaveCriticalSection(&cs);
	}*/


	pSession->sendPacketCount = _sendCount;

	// send overlapped I/O 구조체 reset
	ZeroMemory(&pSession->m_stSendOverlapped, sizeof(OVERLAPPED));

	// send
	// ioCount : WSASend 완료 통지가 리턴보다 먼저 떨어질 수 있으므로 WSASend 호출 전에 증가시켜야 함
	InterlockedIncrement64(&pSession->ioRefCount);
	int sendRet = WSASend(pSession->m_socketClient, wsa, _sendCount, NULL, 0, &pSession->m_stSendOverlapped, NULL);
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
			}

			// Pending이 아닐 경우, 완료 통지 실패 -> IOCount값 복원
			if (0 == InterlockedDecrement64(&pSession->ioRefCount))
			{
				ReleaseSession(pSession);
			}


			return false;
		}
		else
			InterlockedIncrement64(&sendPendingTPS);
	}

	return true;
}

void NetServer::ReleaseSession(stSESSION* pSession)
{
	// 세션 송신 다 마치고 종료해야함 
	// 세션 메모리 정리
	// 소켓 정리
	// 세선 해제

	// ioCount == 0 && releaseFlag == 0 => release = 1 (인터락 함수로 해결)
	if (InterlockedCompareExchange64(&pSession->ioRefCount, RELEASEMASKING, 0) != 0)
	{
		return;
	}

	//-----------------------------------------------------------------------------------
	// Release 실제 진입부
	//-----------------------------------------------------------------------------------

	// disconnected flag 올림
	pSession->isDisconnected = true;

	// recv는 더이상 받으면 안되므로 소켓 close
	closesocket(pSession->m_socketClient);
	pSession->m_socketClient = INVALID_SOCKET;

	// 사용자(Player) 관련 리소스 해제 (호출 후에 해당 세션이 사용되면 안됨)
	OnClientLeave(pSession->sessionID);

	// 링버퍼 내부에 남아있는 거 처리
	pSession->recvRingBuffer.ClearBuffer();
	//pSession->sendRingBuffer.ClearBuffer();

	// Send Packet 관련 리소스 정리
	// SendQ에서 Dqeueue하여 SendPacket 배열에 넣었지만 아직 WSASend 못해서 남아있는 것
	for (int iSendCount = 0; iSendCount < pSession->sendPacketCount; iSendCount++)
	{
		if (pSession->SendPackets[iSendCount])
		{
			CPacket::Free(pSession->SendPackets[iSendCount]);
			pSession->SendPackets[iSendCount] = nullptr;
		}
	}

	// SendQ에 남아있다는 건 WSABUF에 꽂아넣지도 못한 상태 
	if (pSession->sendQ.GetSize() > 0)
	{
		CPacket* packet = nullptr;
		for (int iSendQCount = 0; iSendQCount < pSession->sendQ.GetSize(); iSendQCount++)
		{
			pSession->sendQ.Dequeue(packet);
			CPacket::Free(packet);
		}
	}
	pSession->sendPacketCount = 0;

	pSession->sessionID = -1;

	ZeroMemory(pSession->IP_str, sizeof(pSession->IP_str));
	pSession->IP_num = 0;
	pSession->PORT = 0;
	pSession->LastRecvTime = 0;
	ZeroMemory(&pSession->m_stSendOverlapped, sizeof(OVERLAPPED));
	ZeroMemory(&pSession->m_stRecvOverlapped, sizeof(OVERLAPPED));

	InterlockedExchange8((char*)&pSession->sendFlag, false);

	int index = GetSessionIndex(pSession->sessionID);

	// 미사용 index를 stack에 push
	AvailableIndexStack.Push(index);


	// 접속중인 client 수 차감
	InterlockedDecrement64(&acceptCount);

	// 접속 해제한 clinet 수 증가
	InterlockedIncrement64(&releaseCount);
	InterlockedIncrement64(&releaseTPS);
}

bool NetServer::SendPacket(uint64_t sessionID, CPacket* packet)
{
	// index 찾기
	int index = GetSessionIndex(sessionID);
	if (index < 0 || index >= MAX_SESSION)
	{
		//logger.logger(dfLOG_LEVEL_ERROR, __LINE__, L"Session Index Bound Error : %d", index);
		CPacket::Free(packet);
		return false;
	}

	stSESSION* pSession = &SessionArray[index];

	if (pSession == nullptr)
	{
		//logger.logger(dfLOG_LEVEL_DEBUG, __LINE__, L"Session Not Found");
		wprintf(L"Session Nod Found\n");
		CPacket::Free(packet);
		return false;
	}

	// 재할당된 다른 세션일수도 있음
	if (sessionID != pSession->sessionID)
		return false;

	// 이미 해제된 세션일수도 있음
	if (pSession->isDisconnected)
		return false;

	// 세션 사용 참조카운트 증가 & Release 중인지 확인
	// Release 값이 1이면 이미 Release 하고 있는 상태 
	// -> 이 사이에 ioCount도 0인걸 확인한 상태임 (decrement 안해도 됨)
	if (InterlockedIncrement64(&pSession->ioRefCount) & RELEASEMASKING)
	{
		return false;
	}

	// Release 수행 없이 이곳에서만 세션 사용하려는 상태
	// but 내 세션이 맞는지 다시 확인 (다른 곳에서 세션 해제&할당되어, 다른 세션이 됐을 수도 있음)
	// 이전에 증가했던 ioCount를 되돌려야 함 (0이면 Release 진행)
	if (sessionID != pSession->sessionID)
	{
		if (0 == InterlockedDecrement64(&pSession->ioRefCount))
		{
			wprintf(L"SendPacket # ReAlloc Session -> Cur Session ID : %lld\tPrev Session ID : %lld\n", sessionID, pSession->sessionID);
			ReleaseSession(pSession);
		}
		return false;
	}

	// 헤더 셋팅
	packet->SetNetHeaderPtr();

	// packet 포인터 enqueue
	pSession->sendQ.Enqueue(packet);

	// 한번에 Send 등록
	SendPost(pSession);

	// sendPacket 함수에서 증가시킨 세션 참조 카운트 감소
	if (0 == InterlockedDecrement64(&pSession->ioRefCount))
	{
		ReleaseSession(pSession);
	}
}

void NetServer::Stop()
{

}


int NetServer::GetSessionCount()
{
	return 1;
}


//// ========================================================================
//// Contents Logic
//// ========================================================================
//bool NetServer::OnConnectionRequest(const wchar_t* IP, unsigned short PORT)
//{
//
//	return true;
//}
//
//void NetServer::OnClientJoin(__int64 sessionID)
//{
//
//}
//
//// 실제로 세션이 정리되어 컨텍츠 쪽에 알려주는 함수
//// -> Release 함수에서 정리 후 호출
//void NetServer::OnClientLeave(__int64 sessionID)
//{
//
//}

//// 패킷 처리
//void NetServer::OnRecv(__int64 sessionID, CPacket* packet)
//{
//	__int64 echo;
//
//	// 역직렬화
//	*packet >> echo;
//
//	// Send Msg 생성
//	CPacket* sendPacket = CPacket::PacketPool.Alloc();
//	*sendPacket << echo;
//
//	// Send 처리
//	SendPacket(sessionID, sendPacket);
//
//	CPacket::PacketPool.Free(sendPacket);
//}
//
//void NetServer::OnError(int errorCode, wchar_t* msg)
//{
//
//}
//
//bool NetServer::DisconnectSession(__int64 sessionID)
//{
//	
//	return true;
//}






