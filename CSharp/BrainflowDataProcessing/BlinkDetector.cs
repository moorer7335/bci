﻿using LoggingInterface;
using OpenBCIInterfaces;
using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace BrainflowDataProcessing
{
    public class BlinkDetector
    {
        //  Events
        public event LogEventDelegate Log;
        public event DetectedBlinkDelegate DetectedBlink;

        //  Delegates
        public GetOpenBciCyton8DataDelegate GetData;
        public GetOpenBciCyton8ReadingDelegate GetStdDevMedians;
        
        //  Blink detector properties
        //  turn the dials to tune your detector
        public double NoisyStdDevThreshold { get; set; }

        //  Period of time in seconds that rising and falling must happen to be considered a blink
        //  rising and falling must take more than this amount of time  default  = .2
        public double BlinkPeriodThresholdMin { get; set; }
        //  rising and falling must take less than this amount of time defualt = .65
        public double BlinkPeriodThresholdMax { get; set; }

        //  Rising edge trigger: reading stdDeviation / medianStdDeviation must be greater than this threshold default = 2.0
        public double BlinkUpDevThreshold { get; set; }

        //  Falling edge trigger: reading stdDeviation / medianStdDeviation must be lower than this threshold default = 1.7
        public double BlinkDownDevThreshold { get; set; }


        /// <summary>
        /// Handler for new reading event
        /// will check for blinks on each new reading
        /// </summary>
        public void OnNewReading(object sender, OpenBciCyton8ReadingEventArgs e)
        {
            var data = GetData(.25);
            var stdDevLeft = data.GetExgDataForChannel(0).StdDev();
            var stdDevRight = data.GetExgDataForChannel(1).StdDev();
            var stdDevMedians = GetStdDevMedians();
            DetectBlinks(e.Reading, stdDevLeft, stdDevMedians.ExgCh0, stdDevRight, stdDevMedians.ExgCh1);
        }

        /// <summary>
        /// Detect blinks function
        /// using current reading, the standard deviation from channel 0 (FP1) and channel 1 (FP2), 
        /// and the average deviaiton from channel 0,1
        /// </summary>
        public void DetectBlinks(OpenBciCyton8Reading currentReading, double stdDev0, double stdDevAvg0,  double stdDev1, double stdDevAvg1)
        {
            try
            {                   
                CheckForBlink(currentReading, stdDev0, stdDevAvg0, Eyes.Left);
                CheckForBlink(currentReading, stdDev1, stdDevAvg1, Eyes.Right);

                string log = $"{ (currentReading.TimeStamp - DataFileStartTimeTag).ToString("N3") }   ";
                log += $"Left {(currentReading.ExgCh0 / 1000.0).ToString("N3")}  {stdDev0.ToString("N3")} {stdDevAvg0.ToString("N3")} {(stdDev0 / stdDevAvg0).ToString("N2")}  ";
                log += $"Right {(currentReading.ExgCh1 / 1000.0).ToString("N3")}  {stdDev1.ToString("N3")} {stdDevAvg1.ToString("N3")}  {(stdDev1 / stdDevAvg1).ToString("N2")} ";
                Log?.Invoke(this, new LogEventArgs(this, "DetectBlinks", log, LogLevel.VERBOSE));
            }
            catch (Exception e)
            {
                Log?.Invoke(this, new LogEventArgs(this, "DetectBlinks", e, LogLevel.ERROR));
            }
        }

        
        /// <summary>
        /// Constructor
        /// </summary>
        public BlinkDetector()
        {
            //  blink detector parameter defaults
            BlinkPeriodThresholdMin = .2;
            BlinkPeriodThresholdMax = .65;
            BlinkUpDevThreshold = 1.7;
            BlinkDownDevThreshold = 1.2;
            //
            DataFileStartTimeTag = -0.01;
            NoisyStdDevThreshold = 75.0;

            DataToProcess = new ConcurrentQueue<OpenBciCyton8Reading>();
            NotifyAddedData = new SemaphoreSlim(0);
        }


        public double DataFileStartTimeTag { get; set; }


        protected CancellationTokenSource CancelTokenSource { get; set; }
        protected Task DataQueueProcessorTask { get; set; }
        protected SemaphoreSlim NotifyAddedData { get; set; }
        ConcurrentQueue<OpenBciCyton8Reading> DataToProcess;


        //  Flags to keep track of left/right rising edge event by saving the reading that triggered it
        OpenBciCyton8Reading BlinkLeftRisingEdgeTrigger;
        OpenBciCyton8Reading BlinkRightRisingEdgeTrigger;


       


        /// <summary>
        /// Check for Blink in the specified eye
        /// </summary>
        private void CheckForBlink(OpenBciCyton8Reading currentReading, double stdDev, double stdDevAvg, Eyes eye)
        {
            OpenBciCyton8Reading trigger = (eye == Eyes.Left) ? BlinkLeftRisingEdgeTrigger : BlinkRightRisingEdgeTrigger;
          
            //  search for rising and falling edge of the signal    
            if (trigger != null)
            {
                //  rising edge triggered, check for signal going below falling threashold
                if (stdDev / stdDevAvg < BlinkDownDevThreshold)
                {
                    if ((currentReading.TimeStamp - trigger.TimeStamp) > BlinkPeriodThresholdMin && (currentReading.TimeStamp - trigger.TimeStamp) < BlinkPeriodThresholdMax)
                    {
                        //  detected a blink
                        Log?.Invoke(this, new LogEventArgs(this, "CheckForBlink", $"{ (currentReading.TimeStamp - DataFileStartTimeTag).ToString("N3") }   Detected blink in {eye}.", LogLevel.DEBUG));
                        DetectedBlink?.Invoke(this, new DetectedBlinkEventArgs(eye, WinkState.Wink, currentReading.TimeStamp));
                        ClearTrigger(eye);
                    }
                    else
                    {
                        //  reject as noise
                        DetectedBlink?.Invoke(this, new DetectedBlinkEventArgs(eye, WinkState.Falling, currentReading.TimeStamp));
                        Log?.Invoke(this, new LogEventArgs(this, "CheckForBlink", $"{ (currentReading.TimeStamp - DataFileStartTimeTag).ToString("N3") }   Reject {eye} rising trigger as noise.", LogLevel.DEBUG));
                        ClearTrigger(eye);
                    }
                }
                else if (currentReading.TimeStamp - trigger.TimeStamp > BlinkPeriodThresholdMax)
                {
                    //  taken too long, clear the rising flag
                    DetectedBlink?.Invoke(this, new DetectedBlinkEventArgs(eye, WinkState.Falling, currentReading.TimeStamp));
                    Log?.Invoke(this, new LogEventArgs(this, "CheckForBlink", $"{ (currentReading.TimeStamp - DataFileStartTimeTag).ToString("N3") }   Clear {eye} Rising due to timeout.", LogLevel.DEBUG));
                    ClearTrigger(eye);
                }
            }
            else if (trigger == null /*&&  (stdDevAvg < NoisyStdDevThreshold)*/ && ( stdDev / stdDevAvg > BlinkUpDevThreshold) )
            {
                DetectedBlink?.Invoke(this, new DetectedBlinkEventArgs(eye, WinkState.Rising, currentReading.TimeStamp));
                Log?.Invoke(this, new LogEventArgs(this, "CheckForBlink", $"{ (currentReading.TimeStamp - DataFileStartTimeTag).ToString("N3") }  Detect {eye} rising.", LogLevel.DEBUG));
                SetTrigger(currentReading, eye);
            }
        }


        void SetTrigger(OpenBciCyton8Reading reading, Eyes eye)
        {
            switch (eye)
            {
                case Eyes.Left:
                    BlinkLeftRisingEdgeTrigger = reading;
                    break;

                case Eyes.Right:
                    BlinkRightRisingEdgeTrigger = reading;
                    break;
            }
        }


        void ClearTrigger(Eyes eye)
        {
            switch (eye)
            {
                case Eyes.Left:
                    BlinkLeftRisingEdgeTrigger = null;
                    break;

                case Eyes.Right:
                    BlinkRightRisingEdgeTrigger = null;
                    break;
            }
        }



    }
}
