#include "PCH.h"
#include "NetClient.h"


// ========================================================================
// Thread Call
// ========================================================================

// Worker Thread Call
unsigned __stdcall WorkerThread(void* param)
{
	NetClient* lanServ = (NetClient*)param;

	lanServ->mWorkerThread_serv();

	return 0;
}


NetClient::NetClient() :  IOCPHandle(0), mIP{ 0 }, mPORT(0), mWorkerThreads{ 0 }, recvMsgTPS(0), sendMsgTPS(0),
recvMsgCount(0), sendMsgCount(0), recvCallTPS(0), sendCallTPS(0), recvCallCount(0), sendCallCount(0), recvPendingTPS(0), sendPendingTPS(0),
recvBytesTPS(0), sendBytesTPS(0), recvBytes(0), sendBytes(0), s_workerThreadCount(0), s_runningThreadCount(0), startMonitering(false)
{
	// ========================================================================
	// Initialize
	// ========================================================================
	logger = new Log(L"NetClient");

	wprintf(L"NetClient Initializing...\n");

	pSession = new stSESSION;

	WSADATA  wsaData;

	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
	{
		int initError = WSAGetLastError();

		return;
	}
}

NetClient::~NetClient()
{
	Stop();
}

bool NetClient::Start(const wchar_t* IP, unsigned short PORT, int createWorkerThreadCnt, int runningWorkerThreadCnt, bool nagelOff, unsigned char packet_code, unsigned char packet_key)
{
	CPacket::SetCode(packet_code);
	CPacket::SetKey(packet_key);

	if (pSession->m_socketClient != INVALID_SOCKET)
		return false;

	wmemcpy_s(mIP, wcslen(IP) + 1, IP, wcslen(IP) + 1);
	mPORT = PORT;

	// Create Listen Socket
	pSession->m_socketClient = socket(AF_INET, SOCK_STREAM, 0);
	if (INVALID_SOCKET == pSession->m_socketClient)
	{
		int sockError = WSAGetLastError();

		return false;
	}

	SOCKADDR_IN serverAddr;
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(PORT);
	InetPtonW(AF_INET, IP, &serverAddr.sin_addr);

	// TCP Send Buffer Remove - zero copy
	int sendVal = 0;
	if (setsockopt(pSession->m_socketClient, SOL_SOCKET, SO_SNDBUF, (const char*)&sendVal, sizeof(sendVal)) == SOCKET_ERROR)
	{
		int setsockoptError = WSAGetLastError();

		return false;
	}

	if (nagelOff)
	{
		// Nagle off
		if (setsockopt(pSession->m_socketClient, IPPROTO_TCP, TCP_NODELAY, (const char*)&nagelOff, sizeof(nagelOff)) == SOCKET_ERROR)
		{
			int setsockoptError = WSAGetLastError();

			return false;
		}
	}

	if (connect(pSession->m_socketClient, (SOCKADDR*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR)
	{
		int connectError = WSAGetLastError();

		closesocket(pSession->m_socketClient);
		pSession->m_socketClient = INVALID_SOCKET;

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
		
		closesocket(pSession->m_socketClient);
		pSession->m_socketClient = INVALID_SOCKET;

		return false;
	}

	if (CreateIoCompletionPort((HANDLE)&pSession->m_socketClient, IOCPHandle, (ULONG_PTR)pSession, 0) == NULL)
	{
		int ciocpError = WSAGetLastError();

		closesocket(pSession->m_socketClient);
		pSession->m_socketClient = INVALID_SOCKET;

		CloseHandle(IOCPHandle);

		return false;
	}

	// ========================================================================
	// Create Thread
	// ======================================================================== 
 
	// Worker Thread
	mWorkerThreads.resize(s_workerThreadCount);
	for (int i = 0; i < mWorkerThreads.size(); i++)
	{
		mWorkerThreads[i] = (HANDLE)_beginthreadex(NULL, 0, WorkerThread, this, 0, NULL);
		if (mWorkerThreads[i] == NULL)
		{
			int threadError = GetLastError();

			return false;
		}
	}

	// 세션 참조 카운트를 올려줌
	InterlockedExchange64(&pSession->ioRefCount, 1);

	OnClientJoin();

	RecvPost();

	// 올린 참조카운트 감소
	if (0 == InterlockedDecrement64(&pSession->ioRefCount))
	{
		ReleaseSession();
	}

	return true;
}

// Worker Thread
bool NetClient::mWorkerThread_serv()
{
	DWORD threadID = GetCurrentThreadId();

	stSESSION* pSession = nullptr;
	BOOL bSuccess = true;
	long cbTransferred = 0;
	LPOVERLAPPED pOverlapped = nullptr;

	bool completionOK;

	while (true)
	{
		// 초기화
		cbTransferred = 0;
		pSession = nullptr;
		pOverlapped = nullptr;
		completionOK = false;

		// GQCS call
			// client가 send조차 하지않고 바로 disconnect 할 경우 -> WorkerThread에서 recv 0을 위한 GQCS가 깨어남
		bSuccess = GetQueuedCompletionStatus(IOCPHandle, (LPDWORD)&cbTransferred, (PULONG_PTR)&pSession,
			&pOverlapped, INFINITE);

		// IOCP Error or TIMEOUT or PQCS로 직접 NULL 던짐
		// 세션 멤버변수에서 error counting을 하여 5회 이상 발생했을 시, 해제 작업 들어가는 아이디어도 있긴 함
		// 완료통지가 안왔을 경우, 이번 루프를 skip
		if (pOverlapped == NULL)
		{
			int iocpError = WSAGetLastError();
			PostQueuedCompletionStatus(IOCPHandle, 0, 0, 0);
			break;
		}

		// sendpost
		if (cbTransferred == 0 && (unsigned char)pOverlapped == PQCSTYPE::SENDPOST)
		{
			InterlockedExchange8((char*)&pSession->sendFlag, false);

			// 다른 IOCP worker thread에 의해서 dequeue되어 sendQ가 비어있을 수도 있으니
			// 그럴 땐 굳이 함수 call을 하여 내부에서 조건 검사를 할 필요 없음 (성능에 안좋음)
			if (pSession->sendQ.GetSize() > 0)
				SendPost();
		}
		// rlease
		else if (cbTransferred == 0 && (unsigned char)pOverlapped == PQCSTYPE::RELEASE)
		{
			// ioRefCount가 0인 경우에 PQCS로 IOCP 포트로 enqueue한 것이므로
			// 이미 이 시점에 ioRefCount는 0임 -> continue로 넘겨야 하단에 ioRefCount를 내려주는 로직 skip 가능
			ReleaseSession();
			continue;
		}
		// Send / Recv Proc
		else if (pOverlapped == &pSession->m_stRecvOverlapped && cbTransferred > 0)
		{
			completionOK = RecvProc(cbTransferred);
		}
		else if (pOverlapped == &pSession->m_stSendOverlapped && cbTransferred > 0)
		{
			completionOK = SendProc(cbTransferred);
		}

		// I/O 완료 통지가 더이상 없다면 세션 해제 작업
		if (0 == InterlockedDecrement64(&pSession->ioRefCount))
		{
			ReleaseSession();
		}

	}

	return true;
}


bool NetClient::RecvProc(long cbTransferred)
{
	pSession->recvRingBuffer.MoveWritePtr(cbTransferred);

	int useSize = pSession->recvRingBuffer.GetUseSize();
	
	// Recv Message Process
	while (useSize > 0)
	{
		LANHeader header;

		// Header 크기만큼 있는지 확인
		if (useSize <= sizeof(LANHeader))
			break;

		// Header Peek
		pSession->recvRingBuffer.Peek((char*)&header, sizeof(LANHeader));

		// Packet 크기만큼 있는지 확인
		if (useSize < sizeof(LANHeader) + header.len)
			break;

		// Len 확인 (음수거나 받을 수 있는 패킷 크기보다 클 때
		if (header.len < 0 || header.len > MAX_PACKET_LEN)
		{
			DisconnectSession();

			return false;
		}

		// Packet 크기만큼 있는지 확인
		if (useSize < sizeof(LANHeader) + header.len)
			break;

		// packet alloc
		CPacket* packet = CPacket::Alloc();

		// payload 크기만큼 데이터 Dequeue
		pSession->recvRingBuffer.Dequeue(packet->GetNetPayloadPtr(), header.len + sizeof(LANHeader));

		// payload 크기만큼 packet write pos 이동
		packet->MoveWritePos(header.len);

		// Total Recv Message Count
		InterlockedIncrement64((LONG64*)&recvMsgCount);

		// Recv Message TPS
		InterlockedIncrement64((LONG64*)&recvMsgTPS);

		// Total Recv Bytes
		InterlockedAdd64((LONG64*)&recvBytes, header.len);

		// Recv Bytes TPS
		InterlockedAdd64((LONG64*)&recvBytesTPS, header.len);

		// 컨텐츠 쪽 recv 처리
		OnRecv(packet);

		useSize = pSession->recvRingBuffer.GetUseSize();
	}

	// Recv 재등록
	RecvPost();


	return true;
}

bool NetClient::SendProc(long cbTransferred)
{
	// send 링버퍼에 없다는 건 recv 링버퍼에서 제대로 받아오지 못했거나 recv가 느린 것
	if (pSession->sendPacketCount == 0)
		return false;

	int totalSendBytes = 0;
	int iSendCount;

	// send 완료 통지된 패킷 제거
	for (iSendCount = 0; iSendCount < pSession->sendPacketCount; iSendCount++)
	{
		totalSendBytes += pSession->SendPackets[iSendCount]->GetNetPayloadDataSize();
		CPacket::Free(pSession->SendPackets[iSendCount]);
	}
	// Total Send Bytes
	InterlockedAdd64((long long*)&sendBytes, totalSendBytes);

	// Send Bytes TPS
	InterlockedAdd64((long long*)&sendBytesTPS, totalSendBytes);

	// Total Send Message Count
	InterlockedAdd64((long long*)&sendMsgCount, pSession->sendPacketCount);

	// Send Message TPS
	InterlockedAdd64((long long*)&sendMsgTPS, pSession->sendPacketCount);

	pSession->sendPacketCount = 0;

	// 전송 중 flag를 다시 미전송 상태로 되돌리기
	InterlockedExchange8((char*)&pSession->sendFlag, false);

	// 1회 send 후, sendQ에 쌓여있던 나머지 데이터 모두 send
	if (pSession->sendQ.GetSize() > 0)
	{
		// sendFlag가 false인걸 한번 확인한 다음에 인터락 비교 (어느정도 이 사이에 true인 경우가 걸러져서 인터락 call 줄임)
		if (pSession->sendFlag == false)
		{
			if (false == InterlockedExchange8((char*)&pSession->sendFlag, true))
			{
				InterlockedIncrement64(&pSession->ioRefCount);
				PostQueuedCompletionStatus(IOCPHandle, 0, (ULONG_PTR)pSession, (LPOVERLAPPED)PQCSTYPE::SENDPOST);
			}
		}
	}

	return true;
}

bool NetClient::RecvPost()
{
	// recv 걸기 전에 외부에서 disconnect 호출될 수 있음
	// -> recv 안 걸렸을 때 io 취소해도 의미 없으니까 사전에 recvpost 막아버림
	if (pSession->isDisconnected)
		return false;

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
			{
				// 에러				
				OnError(recvError, L"RecvPost # WSARecv Error\n");
			}

			// Pending이 아닐 경우, 완료 통지 실패
			if (0 == InterlockedDecrement64(&pSession->ioRefCount))
			{
				ReleaseSession();
			}
			return false;
		}
		// Pending일 경우
		else
		{
			InterlockedIncrement64(&recvPendingTPS);

			// Pending 걸렸는데, 이 시점에 disconnect되면 이 때 남아있던 비동기 io 정리해줘야함
			if (pSession->isDisconnected)
			{
				CancelIoEx((HANDLE)pSession->m_socketClient, &pSession->m_stRecvOverlapped);
			}

		}
	}

	return true;
}

bool NetClient::SendPost()
{
	// 1회 송신 제한을 위한 flag 확인 (send call 횟수 줄이려고 -> send call 자체가 느림
	// true면 최조 사용 아님
	// false -> true 면 최초 사용
	// true -> true 면 최초 사용이 아님
	if ((pSession->sendFlag == true) || true == InterlockedExchange8((char*)&pSession->sendFlag, true))
		return false;

	// SendQ가 비어있을 수 있음
	// -> 다른 스레드에서 Dequeue 진행했을 경우
	if (pSession->sendQ.GetSize() <= 0)
	{
		// * 일어날 수 있는 상황
		// 다른 스레드에서 dequeue를 전부 해서 size가 0이 돼서 이 조건문에 진입한건데
		// 이 위치에서 또다른 스레드에서 패킷이 enqueue되고 sendpost가 일어나게 되면
		// 아직 sendFlag가 false로 변경되지 않은 상태이기 때문에 sendpost 함수 상단 조건에 걸려 빠져나가게 됨
		// 그 후, 이 스레드로 다시 돌아오게 될 경우, 
		// sendQ에 패킷이 있는 상태이므로 sendFlas를 false로 바꿔주기만 하고 리턴하는게 아니라
		// 한번 더 sendQ의 size 확인 후 sendpost PQCS 날릴 지 결졍

		InterlockedExchange8((char*)&pSession->sendFlag, false);

		// 그 사이에 SendQ에 Enqueue 됐다면 다시 SendPost Call 
		if (pSession->sendQ.GetSize() > 0)
		{
			// sendpost 함수 내에서 send call을 1회 제한함
			// sendpost 함수를 호출하기 위한 PQCS도 1회 제한을 둬야 성능 개선됨
			// -> 그렇지 않을 경우, sendpacket 오는대로 계속 PQCS 호출하게 되어 성능이 생각한대로 안나올 수 있음 
			if (pSession->sendFlag == false)
			{
				if (false == InterlockedExchange8((char*)&pSession->sendFlag, true))
				{
					InterlockedIncrement64(&pSession->ioRefCount);
					PostQueuedCompletionStatus(IOCPHandle, 0, (ULONG_PTR)pSession, (LPOVERLAPPED)PQCSTYPE::SENDPOST);
				}
			}
		}
		return false;
	}

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
				OnError(sendError, L"SendPost # WSASend Error\n");
			}

			// Pending이 아닐 경우, 완료 통지 실패 -> IOCount값 복원
			if (0 == InterlockedDecrement64(&pSession->ioRefCount))
			{
				ReleaseSession();
			}

			return false;
		}
		else
			InterlockedIncrement64(&sendPendingTPS);
	}

	return true;
}

bool NetClient::SendPacket(CPacket* packet)
{
	// 세션 사용 참조카운트 증가 & Release 중인지 동시 확인
	// Release 비트값이 1이면 ReleaseSession 함수에서 ioCount = 0, releaseFlag = 1 인 상태
	// 어처피 다시 release가서 해제될 세션이므로 ioRefCount 감소시키지 않아도 됨
	if (InterlockedIncrement64(&pSession->ioRefCount) & RELEASEMASKING)
	{
		return false;
	}

	// ------------------------------------------------------------------------------------
	// Release 수행 없이 이곳에서만 세션 사용하려는 상태

	// 외부에서 disconnect 하는 상태
	if (pSession->isDisconnected)
	{
		if (0 == InterlockedDecrement64(&pSession->ioRefCount))
		{
			ReleasePQCS();
		}

		return false;
	}

	// Enqueue한 패킷을 다른 곳에서 사용하므로 패킷 참조카운트 증가 -> Dequeue할 때 감소
	packet->addRefCnt();

	// packet 포인터 enqueue
	pSession->sendQ.Enqueue(packet);

	// sendpost 함수 내에서 send call을 1회 제한함
	// sendpost 함수를 호출하기 위한 PQCS도 1회 제한을 둬야 성능 개선됨
	// -> 그렇지 않을 경우, sendpacket 오는대로 계속 PQCS 호출하게 되어 성능이 생각한대로 안나올 수 있음 
	if (pSession->sendFlag == false)
	{
		if (false == InterlockedExchange8((char*)&pSession->sendFlag, true))
		{
			InterlockedIncrement64(&pSession->ioRefCount);
			PostQueuedCompletionStatus(IOCPHandle, 0, (ULONG_PTR)pSession, (LPOVERLAPPED)PQCSTYPE::SENDPOST);
		}
	}

	// sendPacket 함수에서 증가시킨 세션 참조 카운트 감소
	if (0 == InterlockedDecrement64(&pSession->ioRefCount))
	{
		ReleasePQCS();

		return false;
	}
}

void NetClient::ReleaseSession()
{
	// 세션 송신 다 마치고 종료해야함 
	// 세션 메모리 정리
	// 소켓 정리
	// 세선 해제
	// ioCount == 0 && releaseFlag == 0 => release = 1 (인터락 함수로 해결)
	// 다른 곳에서 해당 세션을 사용(sendpacket or disconnect)하는지 확인
	if (InterlockedCompareExchange64(&pSession->ioRefCount, RELEASEMASKING, 0) != 0)
	{
		return;
	}

	//-----------------------------------------------------------------------------------
	// Release 실제 진입부
	//-----------------------------------------------------------------------------------
	//ioCount = 0, releaseFlag = 1 인 상태

	uint64_t sessionID = pSession->sessionID;

	pSession->sessionID = -1;

	SOCKET sock = pSession->m_socketClient;

	// 소켓 Invalid 처리하여 더이상 해당 소켓으로 I/O 못받게 함
	pSession->m_socketClient = INVALID_SOCKET;

	// recv는 더이상 받으면 안되므로 소켓 close
	closesocket(sock);

	InterlockedExchange8((char*)&pSession->sendFlag, false);

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

	// 사용자(Player) 관련 리소스 해제 (호출 후에 해당 세션이 사용되면 안됨)
	OnClientLeave();

	delete pSession;
	pSession = nullptr;
}

bool NetClient::DisconnectSession()
{
	// 세션 사용 참조카운트 증가 & Release 중인지 동시 확인
	// Release 비트값이 1이면 ReleaseSession 함수에서 ioCount = 0, releaseFlag = 1 인 상태
	// 어처피 다시 release가서 해제될 세션이므로 ioRefCount 감소시키지 않아도 됨
	if (InterlockedIncrement64(&pSession->ioRefCount) & RELEASEMASKING)
	{
		return false;
	}

	// Release 수행 없이 이곳에서만 세션 사용하려는 상태

	// 외부에서 disconnect 하는 상태
	if (pSession->isDisconnected)
	{
		if (0 == InterlockedDecrement64(&pSession->ioRefCount))
		{
			ReleasePQCS();
		}

		return false;
	}

	// ------------------------ Disconnect 확정 ------------------------
	// 그냥 closesocket을 하게 되면 closesocket 함수와 CancelIoEx 함수 사이에서 해제된 세션이 
	// 재할당되어 다른 세션이 될 수 있음
	// 그때 재할당된 세션의 IO 작업들이 CancelIoEx에 의해 제거되는 문제 발생
	// disconnected flag를 true로 변경하면 sendPacket 과 recvPost 함수 진입을 막음
	InterlockedExchange8((char*)&pSession->isDisconnected, true);

	// 현재 IO 작업 모두 취소
	CancelIoEx((HANDLE)pSession->m_socketClient, NULL);

	// Disconnect 함수에서 증가시킨 세션 참조 카운트 감소
	if (0 == InterlockedDecrement64(&pSession->ioRefCount))
	{
		ReleasePQCS();

		return false;
	}

	return true;
}

void NetClient::Stop()
{
	// stop 함수 추후 구현 완료

	// worker thread로 종료 PQCS 날김
	for (int i = 0; i < s_workerThreadCount; i++)
	{
		PostQueuedCompletionStatus(IOCPHandle, 0, 0, 0);
	}


	WaitForMultipleObjects(s_workerThreadCount, &mWorkerThreads[0], TRUE, INFINITE);

	closesocket(pSession->m_socketClient);

	CloseHandle(IOCPHandle);

	for (int i = 0; i < s_workerThreadCount; i++)
		CloseHandle(mWorkerThreads[i]);

	delete pSession;

	WSACleanup();
}