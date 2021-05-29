#pragma once
#include <queue>
#include "UdpMulticastServerThread.h"
#include "TimeExtensions.h"

void StartStatusBroadcast();
void StopStatusBroadcast();

//  UDP multicast thread for status broadcast
//
class BroadcastStatus : public UdpMulticastServerThread
{
public:
	BroadcastStatus(std::string interface = "");
	virtual ~BroadcastStatus();
	
	virtual void Start();
	
	virtual void RunFunction();
	
		
protected:
	
	std::string Interface;
	
	std::string HostName;
	std::string Eth0Address;
	std::string Wlan0Address;
	std::string Wlan0Mode;
	
	ChronoTimer BroadcastStatusTimer;
	ChronoTimer CheckIpConfigTimer;

	void SetIpConfig();
	
	void BroadcastStatusOverMulticast();
};

