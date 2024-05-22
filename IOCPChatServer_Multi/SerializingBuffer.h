#pragma once
#ifndef  __SERIALIZING_PACKET__
#define  __SERIALIZING_PACKET__

#define _CRT_SECURE_NO_WARNINGS

class CPacket
{
public:
	/*---------------------------------------------------------------
	Packet Enum.
	----------------------------------------------------------------*/
	enum en_PACKET
	{
		eBUFFER_DEFAULT = 1400		// 패킷의 기본 버퍼 사이즈.
	};

	//////////////////////////////////////////////////////////////////////////
	// 생성자, 파괴자.
	//
	// Return:
	//////////////////////////////////////////////////////////////////////////
	CPacket();
	CPacket(int iBufferSize);
	CPacket(CPacket& clSrcPacket);

	virtual	~CPacket();

	//////////////////////////////////////////////////////////////////////////
// 버퍼 늘리기.
//
// Parameters: 없음.
// Return: 없음.
//////////////////////////////////////////////////////////////////////////
	void	Resize(const char* methodName, int size);

	//////////////////////////////////////////////////////////////////////////
	// 패킷 청소.
	//
	// Parameters: 없음.
	// Return: 없음.
	//////////////////////////////////////////////////////////////////////////
	void	Clear(void);


	//////////////////////////////////////////////////////////////////////////
	// 버퍼 사이즈 얻기.
	//
	// Parameters: 없음.
	// Return: (int)패킷 버퍼 사이즈 얻기.
	//////////////////////////////////////////////////////////////////////////
	int	GetBufferSize(void);

	//////////////////////////////////////////////////////////////////////////
	// 현재 사용중인 사이즈 얻기.
	//
	// Parameters: 없음.
	// Return: (int)사용중인 데이타 사이즈.
	//////////////////////////////////////////////////////////////////////////
	int		GetDataSize(void);



	//////////////////////////////////////////////////////////////////////////
	// 버퍼 포인터 얻기.
	//
	// Parameters: 없음.
	// Return: (char *)버퍼 포인터.
	//////////////////////////////////////////////////////////////////////////
	char* GetBufferPtr(void);

	//////////////////////////////////////////////////////////////////////////
	// 버퍼 Pos 이동. (음수이동은 안됨)
	// GetBufferPtr 함수를 이용하여 외부에서 강제로 버퍼 내용을 수정할 경우 사용. 
	//
	// Parameters: (int) 이동 사이즈.
	// Return: (int) 이동된 사이즈.
	//////////////////////////////////////////////////////////////////////////
	int		MoveWritePos(int iSize);
	int		MoveReadPos(int iSize);


	/* ============================================================================= */
	// 연산자 오버로딩
	/* ============================================================================= */
	CPacket& operator = (CPacket& clSrcPacket);

	//////////////////////////////////////////////////////////////////////////
	// 넣기.	각 변수 타입마다 모두 만듬.
	//////////////////////////////////////////////////////////////////////////
	CPacket& operator << (unsigned char byValue);
	CPacket& operator << (char chValue);

	CPacket& operator << (short shValue);
	CPacket& operator << (unsigned short wValue);

	CPacket& operator << (int iValue);
	CPacket& operator << (unsigned long lValue);
	CPacket& operator << (float fValue);

	CPacket& operator << (__int64 iValue);
	CPacket& operator << (double dValue);

	//////////////////////////////////////////////////////////////////////////
	// 빼기.	각 변수 타입마다 모두 만듬.
	//////////////////////////////////////////////////////////////////////////
	CPacket& operator >> (unsigned char& byValue);
	CPacket& operator >> (char& chValue);

	CPacket& operator >> (short& shValue);
	CPacket& operator >> (unsigned short& wValue);

	CPacket& operator >> (int& iValue);
	CPacket& operator >> (unsigned long& dwValue);
	CPacket& operator >> (float& fValue);

	CPacket& operator >> (__int64& iValue);
	CPacket& operator >> (double& dValue);



	//////////////////////////////////////////////////////////////////////////
	// 데이타 얻기.
	//
	// Parameters: (char *)Dest 포인터. (int)Size.
	// Return: (int)복사한 사이즈.
	//////////////////////////////////////////////////////////////////////////
	int		GetData(char* chpDest, int iSize);

	//////////////////////////////////////////////////////////////////////////
	// 데이타 삽입.
	//
	// Parameters: (char *)Src 포인터. (int)SrcSize.
	// Return: (int)복사한 사이즈.
	//////////////////////////////////////////////////////////////////////////
	int		PutData(char* chpSrc, int iSrcSize);

	// 버퍼의 Front 포인터 얻음.
	char* GetReadBufferPtr(void);

	// 버퍼의 RearPos 포인터 얻음.
	char* GetWriteBufferPtr(void);

	//// 실제 데이터 위치 (Lan)
	//char* GetLanPayloadPtr();

	//// 실제 데이터 크기 (Lan)
	//int GetLanPayloadDataSize();

	// 실제 데이터 위치 (Net)
	char* GetNetPayloadPtr();

	// 실제 데이터 크기 (Net)
	int GetNetPayloadDataSize();

	//// 버퍼 앞단에 Net 헤더 셋팅
	//void SetNetHeaderPtr();

	//// 버퍼 앞단에 LAN 헤더 셋팅
	//void SetLanHeaderPtr();

	// 인코딩
	void Encoding();

	// 디코딩
	bool Decoding();


	// 프리리스트에서 할당
	inline static CPacket* Alloc()
	{
		CPacket* packet = PacketPool.Alloc();

		packet->addRefCnt();
		InterlockedExchange8((char*)&packet->isEncoded, false);

		packet->Clear();

		return packet;
	}

	// 프리리스트에 반납
	inline static void Free(CPacket* packet)
	{
		// 패킷을 참조하는 곳이 없다면 그때서야 풀로 반환
		if (0 == packet->subRefCnt())
		{
			packet->Clear();
			PacketPool.Free(packet);
		}

		if (packet->ref_cnt < 0)
			CRASH();
	}

	inline static void SetCode(unsigned char code)
	{
		m_code = code;
	}

	inline static unsigned char GetCode()
	{
		return m_code;
	}

	inline static void SetKey(unsigned char key)
	{
		m_key = key;
	}

	inline void addRefCnt()
	{
		InterlockedIncrement((LONG*)&ref_cnt);
	}

	inline LONG subRefCnt()
	{
		return InterlockedDecrement((LONG*)&ref_cnt);
	}

	inline int GetRefCnt()
	{
		return ref_cnt;
	}

	inline static __int64 GetPoolCapacity()
	{
		return PacketPool.GetCapacity();
	}

	inline static __int64 GetPoolTotalAllocCnt()
	{
		return PacketPool.GetObjectAllocCount();
	}

	inline static __int64 GetPoolTotalFreeCnt()
	{
		return PacketPool.GetObjectFreeCount();
	}

	inline static __int64 GetPoolUseCnt()
	{
		return PacketPool.GetObjectUseCount();
	}

	// Packet Memory Pool
	inline static TLSObjectPool<CPacket> PacketPool = TLSObjectPool<CPacket>(1000);

	//friend class LanServer;

private:
	// 참조 카운트
	int alignas(64) ref_cnt;

	// 직렬화 버퍼 크기
	int	m_iBufferSize;

	// 현재 버퍼에 사용중인 사이즈.
	int	m_iDataSize;

	// 직렬화 버퍼 할당 첫 포인터
	char* m_chpBuffer;

	//// 시작 위치
	//char* begin;

	// 읽기 위치
	char* readPos;

	// 쓰기 위치
	char* writePos;

	//// Network Header 위치
	//char* netHeaderPtr;

	//// Lan Header 위치
	//char* lanHeaderPtr;

	//// payload 위치
	//char* payloadPtr;

	// 인코딩 여부 확인 flag
	alignas(64) bool isEncoded;

	inline static unsigned char m_code;

	inline static unsigned char m_key;
};

#endif