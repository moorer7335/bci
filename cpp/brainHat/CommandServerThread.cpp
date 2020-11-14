#include <netinet/in.h>
#include <string>
#include  <algorithm>

#include "brainHat.h"
#include "CommandServerThread.h"
#include "Parser.h"
#include "StringExtensions.h"
#include "TimeExtensions.h"
#include "NetworkAddresses.h"



using namespace std;


//  Constructor
//
CommandServerThread::CommandServerThread()
{
	
}


//  Destructor
//
CommandServerThread::~CommandServerThread()
{
}


//  Thread Start
//  override to open server socket
void CommandServerThread::Start()
{
	int port = OpenServerSocket(COMSERVER_PORT, true);
	if (port < 0)
	{
		Logging.AddLog("CommandServerThread", "Start", "Unable to open server socket port", LogLevelFatal);
		return;
	}
	
	TCPServerThread::Start();
}
	

//  Thread run function
//  waits on accept client TCPIP request, then handles the request
void CommandServerThread::RunFunction()
{
	Logging.AddLog("CommandServerThread", "RunFunction", "Starting CommandServerThread::RunFunction", LogLevelDebug);
	
	while (ThreadRunning)
	{
		struct sockaddr_in clientAddress;
		string readFromSocket;

		int acceptFileDescriptor = ReadStringFromSocket(&clientAddress, readFromSocket);
	
		if (acceptFileDescriptor < 0)
			return;
		
		readFromSocket.erase(remove(readFromSocket.begin(), readFromSocket.end(), '\r'), readFromSocket.end());
		readFromSocket.erase(remove(readFromSocket.begin(), readFromSocket.end(), '\n'), readFromSocket.end());
		Logging.AddLog("CommandServerThread", "RunFunction", format("Read from socket: %s", readFromSocket.c_str()), LogLevelTrace);
		
		Parser readParser(readFromSocket, "?");
		string command = readParser.GetNextString();
		
		if (command.compare("keyboard") == 0)
		{
			HandleKeyboardInputRequest(acceptFileDescriptor, readParser.GetNextString());
		}
		else if (command.compare("loglevel") == 0)
		{
			HandleLogLevelChangeRequest(acceptFileDescriptor, readParser.GetNextString());
		}
		else if (command.compare("ping") == 0)
		{	
			WriteStringToSocket(acceptFileDescriptor, format("ACK?time=%llu\n", GetUnixTimeMilliseconds()));
		}
		else
		{
			Logging.AddLog("CommandServerThread", "RunFunction", format("Unrecognized command: %s", command.c_str()), LogLevelWarn);
			WriteStringToSocket(acceptFileDescriptor, "NAK?response=Unrecognized command.\n");
		}
		
	}
}


//  Keyboard input request
//
void CommandServerThread::HandleKeyboardInputRequest(int acceptFileDesc, string args)
{
	Parser argParser(args, "=&");
	string key = argParser.GetNextString();
	int value = argParser.GetNextInt();
	WriteStringToSocket(acceptFileDesc, format("ACK?response=Key %d accepted.\n", value));
}


//  Change log level request
//
void CommandServerThread::HandleLogLevelChangeRequest(int acceptFileDesc, string args)
{
	Parser argParser(args, "=&");
			
	string destKey = argParser.GetNextString();
	string destValue = argParser.GetNextString();
			
	string levelKey = argParser.GetNextString();
	int level = argParser.GetNextInt();
			
	if (destValue != "w" && destValue != "a")
	{	
		WriteStringToSocket(acceptFileDesc, format("NAK?response=Unrecognized command %s.\n", args.c_str()).c_str());
	}
	else 
	{
		if (destValue == "a")
		{
			Logging.ToggleAppLogLevel((LogLevel)level);
		}
	
		WriteStringToSocket(acceptFileDesc, format("ACK?response=Log level for %s set to %d.\n", destValue.c_str(), level).c_str());
	}
}