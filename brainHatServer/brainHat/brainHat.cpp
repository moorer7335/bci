#include <iostream>
#include <algorithm>
#include <unistd.h>
#include "Logger.h"
#include "StringExtensions.h"
#include "BroadcastData.h"
#include "BroadcastStatus.h"
#include "BoardDataReader.h"
#include "CommandServer.h"
#include "OpenBciDataFile.h"
#include "BoardFileSimulator.h"
#include "GpioPinManager.h"
#include "TimeExtensions.h"
#include <lsl_cpp.h>


using namespace std;

//  callback functions
void BoardConnectionStateChanged(BoardConnectionStates state, int boardId, int sampleRate);
void GetBoardParams(int& boardId, int& sampleRate);

//  Program objects
Logger Logging;
BroadcastData BroadcastData;
BroadcastStatus BroadcastStatus(GetBoardParams);
CommandServer ComServer;
GpioManager Lights;



int Board_Id = -99;
struct BrainFlowInputParams BrainflowBoardParams;
BoardDataReader Board(BoardConnectionStateChanged);

//  The Simulator
string DemoFileName = "";
BoardFileSimulator SimulatorThread(BoardConnectionStateChanged);

bool parse_args(int argc, char *argv[], struct BrainFlowInputParams *params, int *board_id, string &demoFileName);
void RunBoardData();
void RunFileData();
void StartLights();

//  Main function
//
int main(int argc, char *argv[])
{
		
	//  did we launch with command line args
	if (argc > 1)
	{
		//  parse the args
		if (!parse_args(argc, argv, &BrainflowBoardParams, &Board_Id, DemoFileName))
		{
			return -1;
		}
		
		//  for conveinence, set defaults in absense of command line args
		if(Board_Id >= 0 && BrainflowBoardParams.serial_number.size() == 0)
			BrainflowBoardParams.serial_port = "/dev/ttyUSB0";
		
		//  default demo file
		if(Board_Id < -1 && DemoFileName == "")
		{
			cout << "Invalid startup parameters. Exiting program." << endl;
			getchar();
			return -1;
		}	
	}
	else
	{
		//  default to Cyton board on the default port
		Board_Id = 0;
		BrainflowBoardParams.serial_port = "/dev/ttyUSB0";
	}
		
	//  start program threads
	Logging.Start();

	
	StartLights();
	BroadcastStatus.Start();
	ComServer.Start();
	
	//  start board or file simulator data
	if (Board_Id >= -1)
		RunBoardData();
	else
		RunFileData();
	
	
	
	// stop threads
	BroadcastData.Cancel();
	BroadcastStatus.Cancel();
	ComServer.Cancel();
	Logging.Cancel();
	Lights.Cancel();
	
	return 0;
}



//  Run the program with data from the live board
//
void RunBoardData()
{	
	Lights.PowerToBoard(true);
	
	if (Board.Start(Board_Id, BrainflowBoardParams) != 0)
	{
		Logging.AddLog("main", "RunBoardData", "Unable to start board", LogLevelError);
	}

	Logging.AddLog("main", "RunBoardData", "Starting board data. Enter Q to quit.", LogLevelInfo);
	
	string input;
	while (true)
	{
		getline(cin, input);
		toUpper(input);
		
		if (input.compare("Q") == 0) 
		{
			Board.Cancel();
			break;
		}
		else if (input.compare("S") == 0)
		{
			Logging.ResetDisplay();
		}
		else if (input.compare("0") == 0)
		{
			Lights.Mode = LightsOff;
		}
		else if (input.compare("1") == 0)
		{
			Lights.Mode = LightsOn;
		}
		else if (input.compare("2") == 0)
		{
			Lights.Mode = LightsFlash;
		}
		else if (input.compare("3") == 0)
		{
			Lights.Mode = LightsSequence;
		}
		else if (input.compare("b") == 0)
		{
			Board.EnableBoard(!Board.Enabled());
		}
	}
}


//  Run the program with data from the demo file
//
void RunFileData()
{
	if (SimulatorThread.Start(DemoFileName) != 0)
	{
		Logging.AddLog("main", "RunFileData", "Unable to load file.", LogLevelError);
		return;
	}

	Logging.AddLog("main", "RunFileData", "Starting file data. Enter Q to quit.", LogLevelInfo);
	
	string input;
	while (true)
	{
		getline(cin, input);
		toUpper(input);
		
		if (input.compare("Q") == 0) 
		{
			SimulatorThread.Cancel();
			break;
		}
		else if (input.compare("S") == 0)
		{
			Logging.ResetDisplay();
		}
		else if (input.compare("0") == 0)
		{
			Lights.Mode = LightsOff;
		}
		else if (input.compare("1") == 0)
		{
			Lights.Mode = LightsOn;
		}
		else if (input.compare("2") == 0)
		{
			Lights.Mode = LightsFlash;
		}
		else if (input.compare("3") == 0)
		{
			Lights.Mode = LightsSequence;
		}
	}
}

void BoardConnectionStateChanged(BoardConnectionStates state, int boardId, int sampleRate)
{

	switch (state)
	{
	case New:
		Lights.Mode = LightsSequence;
		BroadcastData.SetBoard(boardId, sampleRate);
		break;
		
	case PowerOff:	//  power On board
		Lights.AllOff();
		break;
		
	case PowerOn:	//  power on board
		if(Board_Id > -1)
		{
			Lights.PowerToBoard(true);
			Sleep(2000);
		}
		Lights.Mode = LightsFlash;
		break;
		
	case Connected:	//  board connected
		Lights.Mode = LightsSequence;
		break;
		
	case Disconnected: //  board disconnected	
		Lights.Mode = LightsFlash;
		break;
	}
}


void GetBoardParams(int& boardId, int& sampleRate)
{
	if (Board_Id > -1)
	{
		boardId = Board.GetBoardId();
		sampleRate = Board.GetSampleRate();
	}
	else
	{
		boardId = SimulatorThread.GetBoardId();
		sampleRate = SimulatorThread.GetSampleRate();
	}
	
}


#define HWADDR_len 6




//  Parse the command line args 
//
bool parse_args(int argc, char *argv[], struct BrainFlowInputParams *params, int *board_id, string &demoFileName)
{
	bool board_id_found = false;
	for (int i = 1; i < argc; i++)
	{
		if (std::string(argv[i]) == std::string("--board-id"))
		{
			if (i + 1 < argc)
			{
				i++;
				board_id_found = true;
				*board_id = std::stoi(std::string(argv[i]));
			}
			else
			{
				std::cerr << "missed argument" << std::endl;
				return false;
			}
		}
		if (std::string(argv[i]) == std::string("--demo-file"))
		{
			if (i + 1 < argc)
			{
				i++;
				demoFileName = std::string(argv[i]);
			}
			else
			{
				std::cerr << "missed argument" << std::endl;
				return false;
			}
		}
		if (std::string(argv[i]) == std::string("--ip-address"))
		{
			if (i + 1 < argc)
			{
				i++;
				params->ip_address = std::string(argv[i]);
			}
			else
			{
				std::cerr << "missed argument" << std::endl;
				return false;
			}
		}
		if (std::string(argv[i]) == std::string("--ip-port"))
		{
			if (i + 1 < argc)
			{
				i++;
				params->ip_port = std::stoi(std::string(argv[i]));
			}
			else
			{
				std::cerr << "missed argument" << std::endl;
				return false;
			}
		}
		if (std::string(argv[i]) == std::string("--serial-port"))
		{
			if (i + 1 < argc)
			{
				i++;
				params->serial_port = std::string(argv[i]);
			}
			else
			{
				std::cerr << "missed argument" << std::endl;
				return false;
			}
		}
		if (std::string(argv[i]) == std::string("--ip-protocol"))
		{
			if (i + 1 < argc)
			{
				i++;
				params->ip_protocol = std::stoi(std::string(argv[i]));
			}
			else
			{
				std::cerr << "missed argument" << std::endl;
				return false;
			}
		}
		if (std::string(argv[i]) == std::string("--timeout"))
		{
			if (i + 1 < argc)
			{
				i++;
				params->timeout = std::stoi(std::string(argv[i]));
			}
			else
			{
				std::cerr << "missed argument" << std::endl;
				return false;
			}
		}
		if (std::string(argv[i]) == std::string("--other-info"))
		{
			if (i + 1 < argc)
			{
				i++;
				params->other_info = std::string(argv[i]);
			}
			else
			{
				std::cerr << "missed argument" << std::endl;
				return false;
			}
		}
		if (std::string(argv[i]) == std::string("--mac-address"))
		{
			if (i + 1 < argc)
			{
				i++;
				params->mac_address = std::string(argv[i]);
			}
			else
			{
				std::cerr << "missed argument" << std::endl;
				return false;
			}
		}
		if (std::string(argv[i]) == std::string("--serial-number"))
		{
			if (i + 1 < argc)
			{
				i++;
				params->serial_number = std::string(argv[i]);
			}
			else
			{
				std::cerr << "missed argument" << std::endl;
				return false;
			}
		}
	}
	
	return true;
}


void StartLights()
{
	char host[1024];
	gethostname(host, 1024);
	
	string hostName = string(host);
	Lights.Mode = LightsSequence;
	Lights.StartThreadForHost(hostName);
	
}