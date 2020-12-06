﻿using BrainHatServersMonitor;
using LoggingInterfaces;
using BrainflowInterfaces;
using System;
using System.Collections.Concurrent;
using System.IO;
using System.Threading;
using System.Threading.Tasks;

namespace BrainHatClient
{
    class OpenBCIGuiFormatRawFileWriter
    {
        //  Events
        public event LogEventDelegate Log;

        public bool IsLogging =>  FileWriterCancelTokenSource != null;


        /// <summary>
        /// Start the file writer
        /// </summary>
        public async Task StartWritingToFileAsync(string fileNameRoot, int boardId, int sampleRate)
        {
            FileNameRoot = fileNameRoot;
            BoardId = boardId;
            SampleRate = sampleRate;

            await StopWritingToFileAsync();
            Data.RemoveAll();

            FileWriterCancelTokenSource = new CancellationTokenSource();
            FileWritingTask = RunFileWriter(FileWriterCancelTokenSource.Token);
        }


        /// <summary>
        /// Stop the file writer
        /// </summary>
        public async Task StopWritingToFileAsync()
        {
            if (FileWriterCancelTokenSource != null)
            {
                FileWriterCancelTokenSource.Cancel();
                await FileWritingTask;
                FileWriterCancelTokenSource = null;
                FileWritingTask = null;
            }
        }


        /// <summary>
        /// Add data to the file writer
        /// </summary>
        public void AddData(object sender, BFSampleEventArgs e)
        {
            AddData(e.Sample);
        }


        /// <summary>
        /// Add data to the file writer
        /// </summary>
        public void AddData(IBFSample data)
        {
            if (FileWritingTask != null)
            {
                Data.Enqueue(data);
                NotifyAddedData.Release();
            }
        }


        /// <summary>
        /// Constructor
        /// </summary>
        public OpenBCIGuiFormatRawFileWriter()
        {
            Data = new ConcurrentQueue<IBFSample>();
            NotifyAddedData = new SemaphoreSlim(0);
        }

        //  File writing task 
        protected CancellationTokenSource FileWriterCancelTokenSource;
        protected Task FileWritingTask;
        protected SemaphoreSlim NotifyAddedData;
        
        // Queue to hold data pending write
        ConcurrentQueue<IBFSample> Data;

        //  File Name Root
        string FileNameRoot;
        int BoardId;
        int SampleRate;

        //OpenBCI_GUI$BoardCytonSerialDaisy
        //OpenBCI_GUI$BoardCytonSerial

        private string FileBoardDescription()
        {
            switch ( BoardId)
            {
                case 0:
                    return "OpenBCI_GUI$BoardCytonSerial";
                case 2:
                    return "OpenBCI_GUI$BoardCytonSerialDaisy";
                default:
                    return "Unknown?";
            }
        }
        /// <summary>
        /// Run function
        /// </summary>
        private async Task RunFileWriter(CancellationToken cancelToken)
        {
            try
            {
                //  generate test file name
                var timeNow = DateTimeOffset.Now;
                string fileName = Path.Combine( Path.Combine(System.Environment.GetFolderPath(Environment.SpecialFolder.Desktop), "hatClientRecordings"),  $"{FileNameRoot}_{timeNow.Year}{timeNow.Month.ToString("D02")}{timeNow.Day.ToString("D02")}-{timeNow.Hour.ToString("D02")}{timeNow.Minute.ToString("D02")}{timeNow.Second.ToString("D02")}.txt");

                if (!Directory.Exists(Path.GetDirectoryName(fileName)))
                {
                    Directory.CreateDirectory(Path.GetDirectoryName(fileName));
                }


                using (System.IO.StreamWriter file = new System.IO.StreamWriter(fileName))
                {
                    //  write header
                    file.WriteLine("%OpenBCI Raw EEG Data");
                    file.WriteLine($"%Number of channels = {brainflow.BoardShim.get_exg_channels(BoardId).Length}");
                    file.WriteLine($"%Sample Rate = {SampleRate} Hz");
                    file.WriteLine($"%Board = {FileBoardDescription()}");
                    file.WriteLine("%Logger = BCIpi Data Logger");
                    bool writeHeader = false;

                 
                    try
                    {

                        while (!cancelToken.IsCancellationRequested)
                        {
                            await NotifyAddedData.WaitAsync(cancelToken);

                            try
                            {
                                Data.TryDequeue(out var nextReading);
                                if (nextReading == null)
                                {
                                    Log?.Invoke(this, new LogEventArgs(this, "RunFileWriter", $"Null sample.", LogLevel.WARN));
                                    continue;
                                }

                                if (!writeHeader)
                                {
                                    WriteHeaderToFile(file, nextReading);
                                    writeHeader = true;
                                }

                                WriteToFile(file, nextReading);
                            }
                            catch (Exception ex)
                            {
                              Log?.Invoke(this, new LogEventArgs(this, "RunFileWriter", ex, LogLevel.ERROR));
                            }
                        }
                    }
                    catch (OperationCanceledException)
                    { }
                    catch (Exception e)
                    {
                        Log?.Invoke(this, new LogEventArgs(this, "RunFileWriter", e, LogLevel.FATAL));
                    }
                    finally
                    {
                        file.Close();
                    }
                }
            }
            catch (Exception e)
            {
                Log?.Invoke(this, new LogEventArgs(this, "RunFileWriter", e, LogLevel.FATAL));
            }
        }


        /// <summary>
        /// Write header to file based on the first sample recorded
        /// </summary>
        private void WriteHeaderToFile(StreamWriter file, IBFSample nextReading)
        {
            string header = "Sample Index";
            
            //  exg channels
            for (int i = 0; i < nextReading.NumberExgChannels; i++)
            {
                header += $", EXG Channel {i}";
            }

            //  accelerometer channels
            for (int i = 0; i < nextReading.NumberExgChannels; i++)
            {
                header += $", Accel Channel {i}";
            }

            //  other channels
            for (int i = 0; i < nextReading.NumberOtherChannels; i++)
            {
                header += $", Other";
            }

            //  analog channels
            for (int i = 0; i < nextReading.NumberAnalogChannels; i++)
            {
                header += $", Analog Channel {i}";
            }

            //  time stamps
            header += ", Timestamp, Timestamp (Formatted)";

            file.WriteLine(header);
        }


        /// <summary>
        /// Write a sample to file
        /// </summary>
        private void WriteToFile(StreamWriter file, IBFSample nextSample)
        {
            var seconds = (long)Math.Truncate(nextSample.TimeStamp);
            var time = DateTimeOffset.FromUnixTimeSeconds(seconds);
            var microseconds = nextSample.TimeStamp - seconds;

            //  sample index
            var writeLine = nextSample.SampleIndex.ToString("F3");

            //  exg channels
            foreach ( var nextExg in nextSample.ExgData)
            {
                writeLine += $",{nextExg:F4}";
            }

            //  accelerometer channels
            foreach (var nextAcel in nextSample.AccelData)
            {
                writeLine += $",{nextAcel:F4}";
            }

            //  other channels
            foreach ( var nextOther in nextSample.OtherData)
            {
                writeLine += $",{nextOther:F4}";
            }

            //  analog channels
            foreach (var nextAnalog in nextSample.AnalogData)
            {
                writeLine += $",{nextAnalog:F4}";
            }

            //  raw time stamp
            writeLine += $",{nextSample.TimeStamp:F6}";

            //  formatted time stamp
            writeLine += string.Format(",{0}-{1}-{2} {3}:{4}:{5}.{6}", time.LocalDateTime.Year.ToString("D2"), time.LocalDateTime.Month.ToString("D2"), time.LocalDateTime.Day.ToString("D2"), time.LocalDateTime.Hour.ToString("D2"), time.LocalDateTime.Minute.ToString("D2"), time.LocalDateTime.Second.ToString("D2"), ((int)(microseconds * 1000000)).ToString("D6"));

            file.WriteLine(writeLine);
        }
    }
}
