#include "PCH.h"
#include "SerializingBuffer.h"
#include "Exception.h"

CPacket::CPacket() : m_iDataSize(0), m_iBufferSize(eBUFFER_DEFAULT), m_chpBuffer(nullptr), ref_cnt(0)
{
	m_chpBuffer = new char[m_iBufferSize + 1];

	readPos = writePos = m_chpBuffer + sizeof(NetHeader);
}

CPacket::CPacket(int iBufferSize) : m_iDataSize(0), m_iBufferSize(iBufferSize), m_chpBuffer(nullptr), ref_cnt(0)
{
	m_chpBuffer = new char[m_iBufferSize + 1];

	readPos = writePos = m_chpBuffer + sizeof(NetHeader);
}

CPacket::CPacket(CPacket& clSrcPacket) : m_iDataSize(0), m_iBufferSize(eBUFFER_DEFAULT), m_chpBuffer(nullptr)
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

	int oldSize = m_iBufferSize;

	// default 직렬화 버퍼 크기(남아있는 데이터 크기에서 필요로하는 데이터 크기만큼 더하고 2배로 늘림
	m_iBufferSize = (oldSize + size) * 2;

	// 새로운 직렬화 버퍼 할당 & 임시 버퍼에 있던 데이터 복사
	m_chpBuffer = new char[m_iBufferSize];
	memcpy_s(m_chpBuffer, m_iBufferSize, temp, m_iDataSize);

	// 변수 초기화
	readPos = m_chpBuffer + sizeof(NetHeader);

	writePos = readPos + m_iDataSize;

	// 임시 버퍼 delete
	delete[] temp;
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