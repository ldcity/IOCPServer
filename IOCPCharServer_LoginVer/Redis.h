#pragma once
#ifndef __REDIS_CLASS__
#define __REDIS_CLASS__

#include "PCH.h"

class CRedis
{
public:
	CRedis();
	~CRedis();

	void Connect(std::wstring IP, unsigned short port);

	bool syncSet(const std::string& key, const std::string& value, int timeout = 0);
	cpp_redis::reply syncGet(const std::string& key);

private:
	cpp_redis::client client;
};


class CRedis_TLS : public CRedis
{
public:
	CRedis_TLS(std::wstring IP, unsigned short port);
	~CRedis_TLS();

	bool syncSet(const std::string& key, const std::string& value, int timeout = 0);
	cpp_redis::reply syncGet(const std::string& key);
	
	CRedis* GetCRedisObj();

private:
	DWORD tlsIndex;

	LockFreeStack<CRedis*> redisStack;

	std::wstring mIP;
	unsigned short mPort;
};



#endif // !__REDIS_CLASS__
