#include <list>
#include <unistd.h>
#include <net/if.h>
#include <ifaddrs.h>

#include "brainHat.h"
#include "BroadcastStatus.h"
#include "BrainHatServerStatus.h"
#include "StringExtensions.h"
#include "TimeExtensions.h"
#include "json.hpp"
#include "NetworkAddresses.h"
#include "NetworkExtensions.h"


using namespace std;
using json = nlohmann::json;

list<BroadcastStatus*> StatusBroadcasters;



//  Start the status broadcast on all available interfaces
//
void StartStatusBroadcast()
{
	struct ifaddrs * ifap;
	if (getifaddrs(&ifap) == 0)
	{
		struct ifaddrs * p = ifap;
		while (p)
		{
			uint32 ifaAddr  = SockAddrToUint32(p->ifa_addr);
			uint32 maskAddr = SockAddrToUint32(p->ifa_netmask);
			uint32 dstAddr  = SockAddrToUint32(p->ifa_dstaddr);
			if (ifaAddr > 0)
			{
				char ifaAddrStr[32]; Inet_NtoA(ifaAddr, ifaAddrStr);
				char maskAddrStr[32]; Inet_NtoA(maskAddr, maskAddrStr);
				char dstAddrStr[32]; Inet_NtoA(dstAddr, dstAddrStr);
				//printf("  Found interface:  name=[%s] desc=[%s] address=[%s] netmask=[%s] broadcastAddr=[%s]\n", p->ifa_name, "unavailable", ifaAddrStr, maskAddrStr, dstAddrStr);
				BroadcastStatus* newBroadcaster = new BroadcastStatus(p->ifa_name);
				newBroadcaster->Start();
				StatusBroadcasters.push_back(newBroadcaster);
			}
			p = p->ifa_next;
		}
		freeifaddrs(ifap);
	}
}


//  Stop status broadcasters
//
void StopStatusBroadcast()
{
	for (auto nextBroadcaster = StatusBroadcasters.begin(); nextBroadcaster != StatusBroadcasters.end(); ++nextBroadcaster)
	{
		(*nextBroadcaster)->Cancel();
		delete *nextBroadcaster;
	}
}




//  Constructor
//
BroadcastStatus::BroadcastStatus(string interface)
{
	Interface = interface;
	
	SocketFileDescriptor = -1;	
	
	Eth0Address = "";
	Wlan0Address = "";
	Wlan0Mode = "";
}


//  Destructor
//
BroadcastStatus::~BroadcastStatus()
{
}



//  Start the thread
//  make sure we can open the server socket for multicast before starting thread
void BroadcastStatus::Start()
{
	HostName = GetHostName();
	SetIpConfig();
	
	BroadcastStatusTimer.Start();
	CheckIpConfigTimer.Start(); 
	
	int port = OpenServerSocket(MULTICAST_STATUSPORT, MULTICAST_GROUPADDRESS, Interface);
	
	if (port < 0)
	{
		Logging.AddLog("BroadcastStatus", "Start", "Unable to open server socket port.", LogLevelFatal);
		return;
	}

	
	UdpMulticastServerThread::Start();
}



//  broadcast timeouts for different status properties
int BroadcastTimeoutStatus = (2 * 1000);
int CheckIpConfigTimeout = (60 * 1000);


void BroadcastStatus::SetIpConfig()
{	
	auto addresses = GetNetworkIp4Addresses();
	for (auto it = addresses.begin(); it != addresses.end(); ++it)
	{
		if (get<0>(*it).compare("eth0") == 0)
		{
			Eth0Address = get<1>(*it);
		}
		else if (get<0>(*it).compare("wlan0") == 0)
		{
			Wlan0Address = get<1>(*it);
		}
	}
}


//  Run function
//
void BroadcastStatus::RunFunction()
{
	while (ThreadRunning)
	{
		if (CheckIpConfigTimer.ElapsedMilliseconds() >= CheckIpConfigTimeout)
		{
			SetIpConfig();
			CheckIpConfigTimer.Reset();
		}
		
		if (BroadcastStatusTimer.ElapsedMilliseconds() >= BroadcastTimeoutStatus)
		{
			BroadcastStatusOverMulticast();
			BroadcastStatusTimer.Reset();
		}

		Sleep(1);
	}
}


//  Broadcast the hat status
//
void BroadcastStatus::BroadcastStatusOverMulticast()
{
	BrainHatServerStatus status;
	status.HostName = HostName;
	status.Eth0Address = Eth0Address;
	status.Wlan0Address  = Wlan0Address;
	status.Wlan0Mode = Wlan0Mode;
	status.LogPort = Logging.GetPortNumber();
	status.RecordingDataBrainHat = FileWriter != NULL ? FileWriter->IsRecording() : false;
	status.RecordingFileNameBrainHat = FileWriter != NULL ? (FileWriter->IsRecording() ? FileWriter->FileName() : "") : "";
	status.RecordingDurationBrainHat = FileWriter != NULL ? (FileWriter->IsRecording() ? FileWriter->ElapsedRecordingTime() : 0.0) : 0.0;
	
	if (DataSource != NULL)
	{
		status.BoardId = DataSource->GetBoardId();
		status.SampleRate = DataSource->GetSampleRate();
		status.CytonSRB1 = DataSource->GetSrb1(0);
		status.DaisySRB1 = DataSource->GetSrb1(1);
		status.IsStreaming = DataSource->GetIsStreamRunning();
	}
	
	status.UnixTimeMillis = GetUnixTimeMilliseconds();
	
	string networkString = format("networkstatus?hostname=%s&status=%s\n", HostName.c_str(), status.AsJson().dump().c_str());
				
	WriteMulticastString(networkString);	
}



