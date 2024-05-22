#include "PCH.h"
#include "SerializingBuffer.h"
#include "Exception.h"

CPacket::CPacket() : m_iDataSize(0), m_iBufferSize(eBUFFER_DEFAULT), m_chpBuffer(nullptr), isEncoded(false), ref_cnt(0)
{
	m_chpBuffer = new char[m_iBufferSize + 1];

	readPos = writePos = m_chpBuffer + sizeof(NetHeader);

	//begin = netHeaderPtr = m_chpBuffer;
	//payloadPtr = readPos = writePos = m_chpBuffer + sizeof(NetHeader);
	
	//begin = m_chpBuffer;

	//readPos = writePos = begin + sizeof(NetHeader);

	//lanHeaderPtr = payloadPtr - sizeof(LANHeader);
}

CPacket::CPacket(int iBufferSize) : m_iDataSize(0), m_iBufferSize(iBufferSize), m_chpBuffer(nullptr), isEncoded(false), ref_cnt(0)
{
	m_chpBuffer = new char[m_iBufferSize + 1];

	readPos = writePos = m_chpBuffer + sizeof(NetHeader);

	//begin = netHeaderPtr = m_chpBuffer;

	//payloadPtr = readPos = writePos = m_chpBuffer + sizeof(NetHeader);

	//begin = m_chpBuffer;

	//readPos = writePos = begin + sizeof(NetHeader);

	//lanHeaderPtr = payloadPtr - sizeof(LANHeader);

}

CPacket::CPacket(CPacket& clSrcPacket) : m_iDataSize(0), m_iBufferSize(eBUFFER_DEFAULT), m_chpBuffer(nullptr), isEncoded(false)
{
	if (this != &clSrcPacket)
	{
		// 기존 데이터 clear
		Clear();

		int copyBufferSize;

		// source 버퍼 크기가 dest 버퍼 크기보다 클 경우, 복사할 크기는 dest 버퍼 크기
		if (m_iBufferSize < clSrcPacket.m_iBufferSize)
			copyBufferSize = m_iBufferSize;
		else
			copyBufferSize = clSrcPacket.m_iBufferSize;

		memcpy_s(m_chpBuffer, m_iBufferSize, clSrcPacket.GetBufferPtr(), copyBufferSize);

		m_iDataSize = clSrcPacket.m_iDataSize;
		m_iBufferSize = clSrcPacket.m_iBufferSize;
		//netHeaderPtr = clSrcPacket.netHeaderPtr;
		//lanHeaderPtr = clSrcPacket.lanHeaderPtr;
		//payloadPtr = clSrcPacket.payloadPtr;
		readPos = clSrcPacket.readPos;
		writePos = clSrcPacket.writePos;
	}
}

CPacket::~CPacket()
{
	Clear();
	if (m_chpBuffer != nullptr)
	{
		delete[] m_chpBuffer;
		m_chpBuffer = nullptr;
	}
}

void CPacket::Resize(const char* methodName, int size)
{
	// 기존 직렬화 버퍼에 남아있던 데이터 임시 버퍼에 복사
	char* temp = new char[m_iDataSize];
	
	memcpy_s(temp, m_iDataSize, m_chpBuffer, m_iDataSize);

	// 기존 직렬화 버퍼 delete
	delete[] m_chpBuffer;

	//memcpy_s(temp, m_iDataSize, begin, m_iDataSize);

	//// 기존 직렬화 버퍼 delete
	//delete[] begin;

	int oldSize = m_iBufferSize;

	// default 직렬화 버퍼 크기(남아있는 데이터 크기에서 필요로하는 데이터 크기만큼 더하고 2배로 늘림
	m_iBufferSize = (oldSize + size) * 2;

	// 새로운 직렬화 버퍼 할당 & 임시 버퍼에 있던 데이터 복사
	//m_chpBuffer = new char[m_iBufferSize + sizeof(NetHeader) + sizeof(LANHeader)];
	m_chpBuffer = new char[m_iBufferSize];
	memcpy_s(m_chpBuffer, m_iBufferSize, temp, m_iDataSize);

	// 변수 초기화
	readPos = m_chpBuffer + sizeof(NetHeader);

	//begin = netHeaderPtr = m_chpBuffer;
	//payloadPtr = readPos = m_chpBuffer + sizeof(NetHeader);
	
	writePos = readPos + m_iDataSize;
	
	//lanHeaderPtr = payloadPtr - sizeof(LANHeader);

	//m_iBufferSize -= (sizeof(NetHeader) + sizeof(LANHeader));

	// 임시 버퍼 delete
	delete[] temp;

	//// 로깅 처리 (resize 전 버퍼 크기, resize 전 초과 크기, resize 후 버퍼 크기, resize 후 사용 가능한 크기
	//Log(methodName, oldSize, size - (oldSize - m_iDataSize), m_iBufferSize, m_iBufferSize - m_iDataSize);
}

void CPacket::Clear()
{
	//InterlockedExchange8((char*)&isEncoded, false);
	m_iDataSize = 0;
	//m_chpBuffer = begin;
	readPos = writePos = m_chpBuffer + sizeof(NetHeader);
}

int CPacket::GetBufferSize()
{
	return m_iBufferSize;
}

int CPacket::GetDataSize()
{
	return m_iDataSize;
}

//// 실제 데이터 크기 (Lan)
//int CPacket::GetLanPayloadDataSize()
//{
//	return m_iDataSize + sizeof(LANHeader);
//}

// 실제 데이터 크기 (Net)
int CPacket::GetNetPayloadDataSize()
{
	return m_iDataSize + sizeof(NetHeader);
}

char* CPacket::GetBufferPtr()
{
	return m_chpBuffer;
}

//// 실제 데이터 위치 (Lan)
//char* CPacket::GetLanPayloadPtr()
//{
//	return lanHeaderPtr;
//}

// 실제 데이터 위치 (Net)
char* CPacket::GetNetPayloadPtr()
{
	//return netHeaderPtr;
	return m_chpBuffer;
}

int CPacket::MoveWritePos(int iSize)
{
	if (iSize < 0)
		return -1;
	if (m_iBufferSize - m_iDataSize - sizeof(NetHeader) < iSize)
		return 0;

	writePos += iSize;
	m_iDataSize += iSize;

	return iSize;
}

int CPacket::MoveReadPos(int iSize)
{
	if (iSize < 0)
		return -1;
	if (m_iDataSize < iSize)
		return 0;

	readPos += iSize;
	m_iDataSize -= iSize;

	if (readPos == writePos) Clear();

	return iSize;
}

CPacket& CPacket::operator = (CPacket& clSrcPacket)
{
	if (this == &clSrcPacket) return *this;

	// 기존 데이터 clear
	Clear();

	int copyBufferSize;

	// source 버퍼 크기가 dest 버퍼 크기보다 클 경우, 복사할 크기는 dest 버퍼 크기
	if (m_iBufferSize < clSrcPacket.m_iBufferSize)
		copyBufferSize = m_iBufferSize;
	else
		copyBufferSize = clSrcPacket.m_iBufferSize;

	memcpy_s(m_chpBuffer, m_iBufferSize, clSrcPacket.GetBufferPtr(), copyBufferSize);

	m_iDataSize = clSrcPacket.m_iDataSize;
	m_iBufferSize = clSrcPacket.m_iBufferSize;
	//netHeaderPtr = clSrcPacket.netHeaderPtr;
	//lanHeaderPtr = clSrcPacket.lanHeaderPtr;
	//payloadPtr = clSrcPacket.payloadPtr;
	readPos = clSrcPacket.readPos;
	writePos = clSrcPacket.writePos;

	return *this;
}

//////////////////////////////////////////////////////////////////////////
// 넣기.	각 변수 타입마다 모두 만듬.
//////////////////////////////////////////////////////////////////////////
CPacket& CPacket::operator << (unsigned char byValue)
{
	// 남은 사이즈보다 넣을 변수 크기가 더 크면 바로 return
	if (m_iBufferSize - sizeof(NetHeader) - m_iDataSize < sizeof(unsigned char))
		Resize("operator << (unsigned char) ", sizeof(unsigned char));

	memcpy_s(writePos, m_iBufferSize - sizeof(NetHeader) - m_iDataSize, &byValue, sizeof(unsigned char));
	MoveWritePos(sizeof(unsigned char));

	return *this;
}

CPacket& CPacket::operator << (char chValue)
{
	// 남은 사이즈보다 넣을 변수 크기가 더 크면 바로 return
	if (m_iBufferSize - sizeof(NetHeader) - m_iDataSize < sizeof(char))
		Resize("operator << (char) ", sizeof(char));

	memcpy_s(writePos, m_iBufferSize - sizeof(NetHeader) - m_iDataSize, &chValue, sizeof(char));
	MoveWritePos(sizeof(char));

	return *this;
}

CPacket& CPacket::operator << (short shValue)
{
	// 남은 사이즈보다 넣을 변수 크기가 더 크면 바로 return
	if (m_iBufferSize - sizeof(NetHeader) - m_iDataSize < sizeof(short))
		Resize("operator << (short) ", sizeof(short));

	memcpy_s(writePos, m_iBufferSize - sizeof(NetHeader) - m_iDataSize, &shValue, sizeof(short));
	MoveWritePos(sizeof(short));

	return *this;
}

CPacket& CPacket::operator << (unsigned short wValue)
{
	// 남은 사이즈보다 넣을 변수 크기가 더 크면 바로 return
	if (m_iBufferSize - sizeof(NetHeader) - m_iDataSize < sizeof(unsigned short))
		Resize("operator << (short) ", sizeof(unsigned short));

	memcpy_s(writePos, m_iBufferSize - sizeof(NetHeader) - m_iDataSize, &wValue, sizeof(unsigned short));
	MoveWritePos(sizeof(unsigned short));

	return *this;
}

CPacket& CPacket::operator << (int iValue)
{
	// 남은 사이즈보다 넣을 변수 크기가 더 크면 바로 return
	if (m_iBufferSize - sizeof(NetHeader) - m_iDataSize < sizeof(int))
		Resize("operator << (int) ", sizeof(int));

	memcpy_s(writePos, m_iBufferSize - sizeof(NetHeader) - m_iDataSize, &iValue, sizeof(int));
	MoveWritePos(sizeof(int));

	return *this;
}

CPacket& CPacket::operator << (unsigned long lValue)
{
	// 남은 사이즈보다 넣을 변수 크기가 더 크면 바로 return
	if (m_iBufferSize - sizeof(NetHeader) - m_iDataSize < sizeof(unsigned long))
		Resize("operator << (long) ", sizeof(long));

	memcpy_s(writePos, m_iBufferSize - sizeof(NetHeader) - m_iDataSize, &lValue, sizeof(unsigned long));
	MoveWritePos(sizeof(unsigned long));

	return *this;
}

CPacket& CPacket::operator << (float fValue)
{
	// 남은 사이즈보다 넣을 변수 크기가 더 크면 바로 return
	if (m_iBufferSize - sizeof(NetHeader) - m_iDataSize < sizeof(float))
		Resize("operator << (float) ", sizeof(float));

	memcpy_s(writePos, m_iBufferSize - sizeof(NetHeader) - m_iDataSize, &fValue, sizeof(float));
	MoveWritePos(sizeof(float));

	return *this;
}

CPacket& CPacket::operator << (__int64 iValue)
{
	// 남은 사이즈보다 넣을 변수 크기가 더 크면 바로 return
	if (m_iBufferSize - sizeof(NetHeader) - m_iDataSize < sizeof(__int64))
		Resize("operator << (__int64) ", sizeof(__int64));

	memcpy_s(writePos, m_iBufferSize - sizeof(NetHeader) - m_iDataSize, &iValue, sizeof(__int64));
	MoveWritePos(sizeof(__int64));

	return *this;
}

CPacket& CPacket::operator << (double dValue)
{
	// 남은 사이즈보다 넣을 변수 크기가 더 크면 바로 return
	if (m_iBufferSize - sizeof(NetHeader) - m_iDataSize < sizeof(double))
		Resize("operator << (double) ", sizeof(double));

	memcpy_s(writePos, m_iBufferSize - sizeof(NetHeader) - m_iDataSize, &dValue, sizeof(double));
	MoveWritePos(sizeof(double));

	return *this;
}

//////////////////////////////////////////////////////////////////////////
// 빼기.	각 변수 타입마다 모두 만듬.
//////////////////////////////////////////////////////////////////////////
CPacket& CPacket::operator >> (unsigned char& byValue)
{
	if (m_iDataSize < sizeof(unsigned char))
		throw SerializingBufferException("Failed to read float", "operator >> (unsigned char)", __LINE__, this->GetBufferPtr());


	memcpy_s(&byValue, sizeof(unsigned char), readPos, sizeof(unsigned char));

	// 남은 데이터 앞으로 이동
	MoveReadPos(sizeof(unsigned char));

	return *this;
}

CPacket& CPacket::operator >> (char& chValue)
{
	if (m_iDataSize < sizeof(char))
		throw SerializingBufferException("Failed to read float", "operator >> (char)", __LINE__, this->GetBufferPtr());


	memcpy_s(&chValue, sizeof(char), readPos, sizeof(char));

	// 남은 데이터 앞으로 이동
	MoveReadPos(sizeof(char));

	return *this;
}


CPacket& CPacket::operator >> (short& shValue)
{
	if (m_iDataSize < sizeof(short))
		throw SerializingBufferException("Failed to read float", "operator >> (short)", __LINE__, this->GetBufferPtr());

	memcpy_s(&shValue, sizeof(short), readPos, sizeof(short));

	// 남은 데이터 앞으로 이동
	MoveReadPos(sizeof(short));

	return *this;
}

CPacket& CPacket::operator >> (unsigned short& wValue)
{
	if (m_iDataSize < sizeof(unsigned short))
		throw SerializingBufferException("Failed to read float", "operator >> (unsigned short)", __LINE__, this->GetBufferPtr());


	memcpy_s(&wValue, sizeof(unsigned short), readPos, sizeof(unsigned short));

	// 남은 데이터 앞으로 이동
	MoveReadPos(sizeof(unsigned short));

	return *this;
}

CPacket& CPacket::operator >> (int& iValue)
{
	if (m_iDataSize < sizeof(int))
		throw SerializingBufferException("Failed to read float", "operator >> (int)", __LINE__, this->GetBufferPtr());

	memcpy_s(&iValue, sizeof(int), readPos, sizeof(int));

	// 남은 데이터 앞으로 이동
	MoveReadPos(sizeof(int));

	return *this;
}

CPacket& CPacket::operator >> (unsigned long& dwValue)
{
	if (m_iDataSize < sizeof(unsigned long))
		throw SerializingBufferException("Failed to read float", "operator >> (unsigned long)", __LINE__, this->GetBufferPtr());


	memcpy_s(&dwValue, sizeof(unsigned long), readPos, sizeof(unsigned long));

	// 남은 데이터 앞으로 이동
	MoveReadPos(sizeof(unsigned long));

	return *this;
}

CPacket& CPacket::operator >> (float& fValue)
{
	if (m_iDataSize < sizeof(float))
		throw SerializingBufferException("Failed to read float", "operator >> (float)", __LINE__, this->GetBufferPtr());

	memcpy_s(&fValue, sizeof(float), readPos, sizeof(float));

	// 남은 데이터 앞으로 이동
	MoveReadPos(sizeof(float));

	return *this;
}

CPacket& CPacket::operator >> (__int64& iValue)
{
	if (m_iDataSize < sizeof(__int64))
		throw SerializingBufferException("Failed to read float", "operator >> (__int64)", __LINE__, this->GetBufferPtr());

	memcpy_s(&iValue, sizeof(__int64), readPos, sizeof(__int64));

	// 남은 데이터 앞으로 이동
	MoveReadPos(sizeof(__int64));

	return *this;
}

CPacket& CPacket::operator >> (double& dValue)
{
	if (m_iDataSize < sizeof(double))
		throw SerializingBufferException("Failed to read float", "operator >> (double)", __LINE__, this->GetBufferPtr());

	memcpy_s(&dValue, sizeof(double), readPos, sizeof(double));

	// 남은 데이터 앞으로 이동
	MoveReadPos(sizeof(double));

	return *this;
}

int CPacket::GetData(char* chpDest, int iSize)
{
	if (m_iDataSize < iSize)
		throw SerializingBufferException("Failed to read float", "GetData", __LINE__, this->GetBufferPtr());

	memcpy_s(chpDest, iSize, readPos, iSize);
	MoveReadPos(iSize);

	return iSize;
}

int CPacket::PutData(char* chpSrc, int iSrcSize)
{
	if (m_iBufferSize - sizeof(NetHeader) - m_iDataSize < iSrcSize)
		Resize("PutData", iSrcSize);

	memcpy_s(writePos, m_iBufferSize - sizeof(NetHeader) - m_iDataSize, chpSrc, iSrcSize);
	MoveWritePos(iSrcSize);

	return iSrcSize;
}

// 버퍼의 Front 포인터 얻음.
char* CPacket::GetReadBufferPtr(void)
{
	return readPos;
}

// 버퍼의 RearPos 포인터 얻음.
char* CPacket::GetWriteBufferPtr(void)
{
	return writePos;
}

//// 버퍼 앞단에 Net 헤더 셋팅
//void CPacket::SetNetHeaderPtr()
//{
//	NetHeader netHeader;
//	netHeader.code = m_code;
//	netHeader.len = m_iDataSize;
//
//	uint64_t checksum = 0;
//
//	// 체크섬 계산
//	for (unsigned char i = 0; i < m_iDataSize; i++)
//	{
//		checksum += *((unsigned char*)readPos + i);
//	}
//
//	checksum %= 256;
//	netHeader.checkSum = checksum;
//	netHeader.randKey = m_randKey;
//
//	memcpy_s(netHeaderPtr, sizeof(NetHeader), &netHeader, sizeof(NetHeader));
//}

//// 버퍼 앞단에 Lan 헤더 셋팅
//void CPacket::SetLanHeaderPtr()
//{
//	memcpy_s(lanHeaderPtr, sizeof(LANHeader), &m_iDataSize, sizeof(unsigned short));
//}

// 인코딩
void CPacket::Encoding()
{
	if (true == InterlockedExchange8((char*)&isEncoded, true))
	{
		return;
	}

	NetHeader netHeader;
	netHeader.code = m_code;
	netHeader.len = m_iDataSize;

	uint64_t checksum = 0;

	// 체크섬 계산
	for (unsigned char i = 0; i < m_iDataSize; i++)
	{
		checksum += *((unsigned char*)readPos + i);
	}

	checksum %= 256;
	netHeader.checkSum = checksum;
	srand(checksum);
	netHeader.randKey = rand();

	//memcpy_s(netHeaderPtr, sizeof(NetHeader), &netHeader, sizeof(NetHeader));
	memcpy_s(m_chpBuffer, sizeof(NetHeader), &netHeader, sizeof(NetHeader));

	//unsigned char randKey = *(unsigned char*)(netHeaderPtr + 3);
	//unsigned char* d = (unsigned char*)(netHeaderPtr + 4);
	unsigned char randKey = *(unsigned char*)(m_chpBuffer + 3);
	unsigned char* d = (unsigned char*)(m_chpBuffer + 4);
	unsigned char p = 0;
	unsigned char e = 0;
	unsigned char size = m_iDataSize + 1;			// 체크썸 포함 페이로드

	for (unsigned char i = 0; i < size; i++)
	{
		p = *(d + i) ^ (p + randKey + i + 1);
		e = p ^ (e + m_key + i + 1);
		*(d + i) = e;
	}
}


// 디코딩
bool CPacket::Decoding()

{
	//// 인코딩 된 적이 없는 경우
	//if (isEncoded)
	//	return false;

	//unsigned char code = *((unsigned char*)netHeaderPtr);

	//if (code != m_code)
	//	return false;


	//unsigned char randKey = *((unsigned char*)netHeaderPtr + 3);
	//unsigned char* d = (unsigned char*)netHeaderPtr + 4;
	//unsigned char size = *((unsigned short*)(netHeaderPtr + 1)) + 1;			// 체크썸 포함 페이로드

	unsigned char randKey = *((unsigned char*)m_chpBuffer + 3);
	unsigned char* d = (unsigned char*)m_chpBuffer + 4;
	unsigned char size = *((unsigned short*)(m_chpBuffer + 1)) + 1;			// 체크썸 포함 페이로드

	for (unsigned char i = size - 1; i >= 1; i--)
	{
		*(d + i) ^= (*(d + (i - 1)) + m_key + (i + 1));
	}

	*d ^= (m_key + 1);

	for (unsigned char i = size - 1; i >= 1; i--)
	{
		*(d + i) ^= (*(d + (i - 1)) + randKey + (i + 1));
	}

	*d ^= (randKey + 1);

	uint64_t checksum = 0;

	char* payloadPtr = m_chpBuffer + sizeof(NetHeader);

	// 체크섬 계산
	for (unsigned char i = 0; i < m_iDataSize; i++)
	{
		checksum += *((unsigned char*)payloadPtr + i);
	}

	checksum %= 256;

	// 기존 체크썸과 복호화된 체크썸이 다를 경우
	if (checksum != *d)
		return false;

	return true;
}