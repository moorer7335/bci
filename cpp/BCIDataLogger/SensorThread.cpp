#include <stdlib.h>
#include <iostream>
#include <unistd.h>
#include <sstream>
#include <iomanip>
#include <sys/time.h>
#include <math.h>
#include <chrono>


#include "SensorThread.h"

#define USLEEP_MILI (1000)
#define USLEEP_SEC (1000000)

using namespace std;
using namespace chrono;

//  Constructor
//
SensorThread::SensorThread()
{
	BoardId = 0;
	Board = NULL;
	
	Logging = false;
	RecordsLogged = 0;
	
	NumInvalidPointsCounter = 0;
}


//  Destructor
//
SensorThread::~SensorThread()
{
	Cancel();
}




//  Thread Start
//
int SensorThread::Start(int board_id, struct BrainFlowInputParams params)
{
	BoardParamaters = params;
	BoardId = board_id;
	
	int res = InitializeBoard();
	
	if (res == 0)
		Thread::Start();
	
	return res;
}




//  Thread Cancel
//  
void SensorThread::Cancel()
{
	Thread::Cancel();
	
	ReleaseBoard();
}


//  Release Board
//  stops the session and deletes the board if it is initialized
void SensorThread::ReleaseBoard()
{
	if (Board != NULL)
	{
		if (Board->is_prepared())
		{
			Board->stop_stream();
			Board->release_session();
		}
		
		delete Board;
		Board = NULL;
		NumInvalidPointsCounter = 0;
	}
}


//  Initialize Board
//  creates a new Brainflow board object and starts streaming data
int SensorThread::InitializeBoard()
{
	int res = 0;
	
	ReleaseBoard();
	
	Board = new BoardShim(BoardId, BoardParamaters);
	
	try
	{
		Board->prepare_session();
		Board->start_stream();
			
		// for STREAMING_BOARD you have to query information using board id for master board
		// because for STREAMING_BOARD data format is determined by master board!
		if(BoardId == (int)BoardIds::STREAMING_BOARD)
		{
			BoardId = std::stoi(BoardParamaters.other_info);
			BoardShim::log_message((int)LogLevels::LEVEL_INFO, "Use Board Id %d", BoardId);
		}	
		
		usleep(7 * USLEEP_SEC);
	}
	catch (const BrainFlowException &err)
	{
		BoardShim::log_message((int)LogLevels::LEVEL_ERROR, err.what());
		res = err.exit_code;
		if (Board->is_prepared())
		{
			Board->release_session();
		}
	}
	
	return res;
}


//  Reconnect to Board
//  tries to restart board streaming
void SensorThread::ReconnectToBoard()
{
	BoardShim::log_message((int)LogLevels::LEVEL_ERROR, "Lost connection to the board, attempting to reconnect");
	ReleaseBoard();
	
	if (InitializeBoard() != 0)
	{
		BoardShim::log_message((int)LogLevels::LEVEL_ERROR, "Failed to reconnect to board.");
	}
}
	


//  Start Logging
//  begin logging board data to the a new file with this testname
void SensorThread::StartLogging(string testName)
{
	StopLogging();
	
	{
		LockMutex lockFile(LogFileMutex);
			
		timeval tv;
		gettimeofday(&tv, NULL);
		tm* logTime = localtime(&(tv.tv_sec));

		//  create file name from test name and start time
		ostringstream os;		
		os << "/home/pi/Source/BCI/DataLogs/" <<testName << "_" << setfill('0') << setw(2) << logTime->tm_hour <<  logTime->tm_min  << setw(2) << logTime->tm_sec << ".txt";	
		
		//  open log file
		LogFile.open(os.str());
		
		cout << "Starting log file " << os.str() << endl;
		
		//  header
		LogFile << "%OpenBCI Raw EEG Data" << endl;
		LogFile << "%Number of channels = 8" << endl;
		LogFile << "%Sample Rate = 250 Hz" << endl;
		LogFile << "%Board = OpenBCI_GUI$BoardCytonSerial" << endl;
		LogFile << "%Logger = OTTOMH BCIDataLogger" << endl;
		LogFile << "Sample Index, EXG Channel 0, EXG Channel 1, EXG Channel 2, EXG Channel 3, EXG Channel 4, EXG Channel 5, EXG Channel 6, EXG Channel 7, Accel Channel 0, Accel Channel 1, Accel Channel 2, Other, Other, Other, Other, Other, Other, Other, Analog Channel 0, Analog Channel 1, Analog Channel 2, Timestamp, Timestamp (Formatted)" << endl;
	
		LogFile <<  fixed << showpoint;
		
		StartTime = steady_clock::now();
		LastLoggedTime = StartTime;
		
		RecordsLogged = 0;
		
		Logging = true;
	}
}


//  Stop Logging
//  end logging and close open log file
void SensorThread::StopLogging()
{
	{
		LockMutex lockFile(LogFileMutex);
		
		Logging = false;
	
		if (LogFile.is_open())
			LogFile.close();
	}
}


//  Thread Run Function
//
void SensorThread::RunFunction()
{
	double **data = NULL;
	int res = 0;
	int num_rows = 0;
	int data_count = 0;
	
	while (Board != NULL && ThreadRunning)
	{
		try
		{
			//  approximately three seconds without data will trigger reconnect
			if(NumInvalidPointsCounter > 300)
			{
				ReconnectToBoard();
				continue;
			}
			
			data = Board->get_board_data(&data_count);
		
			//  count the epochs where we have no data, will trigger a reconnect eventually
			if(data_count == 0)
				NumInvalidPointsCounter++;
			else
				NumInvalidPointsCounter = 0;
				
			num_rows = BoardShim::get_num_rows(BoardId);

			if (Logging)
			{
				SaveDataToFile(data, num_rows, data_count);
			
				//  update the display once a second
				if(duration_cast<milliseconds>(steady_clock::now() - LastLoggedTime).count() > 1000)
				{
					cout << "Time Elapsed: " << chrono::duration_cast<seconds>(steady_clock::now() - StartTime).count() << "s. Records logged: " << RecordsLogged << endl;
					LastLoggedTime = steady_clock::now();
				}
			}
		
			if (data != NULL)
			{
				for (int i = 0; i < num_rows; i++)
				{
					delete[] data[i];
				}
			}
			delete[] data;
		
			usleep(5 * USLEEP_MILI);	
		}
		catch (const BrainFlowException &err)
		{
			BoardShim::log_message((int)LogLevels::LEVEL_ERROR, err.what());
			
			//  this is the error code thrown when board read fails due to power outage
			if(err.exit_code == 15)
				NumInvalidPointsCounter += 101;	//  increment this by one hundred because we have one second sleep on this condition, and we want to trigger reconnect at two seconds of normal time
			
			usleep(1 * USLEEP_SEC);
		}
	}
}


//  Save Data To File
//  saves a data buffer of data points to the file
//  note data buffer is rows of channels with columns of epochs
void SensorThread::SaveDataToFile(double **data_buf, int num_channels, int num_data_points)
{
	{
		LockMutex lockFile(LogFileMutex);
				
		for(int i = 0 ; i < num_data_points ; i++)
		{
			for (int j = 0; j < num_channels; j++)
			{
				if (j == 0)
				{
					LogFile << setprecision(1);
				}
				if (j == 1)
				{
					LogFile << setprecision(6);
				}	
				else if (j == 9)
				{
					LogFile << setprecision(1);
				}
				else if (j == 22)
				{
					LogFile << setprecision(6);
				}
				
				LogFile << data_buf[j][i] << ",";
			}

			//  local time from time stamp
			double seconds;
			double microseconds = modf(data_buf[num_channels - 1][i] , &seconds) ;
			time_t timeSeconds = (int)seconds;
			tm* logTime = localtime(&timeSeconds);
			LogFile << setw(4) << logTime->tm_year+1900 << "-" << setfill('0') << setw(2) << logTime->tm_mon+1 << "-" << logTime->tm_mday << " " << logTime->tm_hour << ":" <<  logTime->tm_min << ":" <<  logTime->tm_sec <<  "." << setw(4) << (int)(microseconds*10000);
		
			LogFile << endl;
		}
		
		RecordsLogged += num_data_points;
	}
}