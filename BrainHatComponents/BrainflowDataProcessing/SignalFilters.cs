﻿using brainflow;
using LoggingInterfaces;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text;
using System.Xml.Linq;

namespace BrainflowDataProcessing
{
    /// <summary>
    /// Signal filter function
    /// </summary>
    public class SignalFilterFunction
    {
        public SignalFilterFunction(object methodObject, MethodInfo method, object[] parameters)
        {
            MethodObject = methodObject;
            Method = method;
            Parameters = parameters;
        }

        public double[] RunFilterFunction(double[] data, int sampleRate)
        {
            Parameters[0] = data;
            Parameters[1] = sampleRate;

            return (double[])Method.Invoke(MethodObject, Parameters);
        }


        object MethodObject;
        MethodInfo Method;
        object[] Parameters;
    }


    /// <summary>
    /// Signal filter
    /// </summary>
    public class SignalFilter
    {
        public double[] ApplyFilter(double[] data, int sampleRate)
        {
            foreach ( var nextFilterFunction in FilterFunctions )
            {
                data = nextFilterFunction.RunFilterFunction(data, sampleRate);
            }

            return data;
        }

        public void AddFunction(object methodObject, MethodInfo method, object[] parameters)
        {
            FilterFunctions.Add(new SignalFilterFunction(methodObject, method, parameters));
        }

        public SignalFilter(string name)
        {
            Name = name;
            FilterFunctions = new List<SignalFilterFunction>();
        }

        public string Name { get; protected set; }

        protected List<SignalFilterFunction> FilterFunctions;
       
    }


    /// <summary>
    /// Signal filters
    /// </summary>
    public class SignalFilters
    {
        /// <summary>
        /// Load filter definitions from XML file
        /// </summary>
        public void LoadSignalFilters(string xmlFilePath)
        {
            try
            {
                Filters.Clear();

                using (var reader = new StreamReader(xmlFilePath))
                {
                    var doc = XDocument.Load(reader);

                    foreach (var nextFilter in doc.Element("brainHatConfig")?.Element("Filters")?.Elements("Filter"))
                    {
                        var filterName = nextFilter.Element("Name")?.Value;
                        if ( filterName == null || Filters.ContainsKey(filterName) )
                        {
                            throw new Exception("Filter does not have a name or name is duplicated.");
                        }

                        var newFilter = new SignalFilter(filterName);

                        foreach (var nextFunction in nextFilter.Element("Functions")?.Elements("Function"))
                        {
                            //  get function name from XML
                            var functionName = nextFunction.Attribute("Name")?.Value;
                            //  get method from DataFilter class
                            MethodInfo mi = typeof(DataFilter).GetMethod(functionName, BindingFlags.Public | BindingFlags.Static);

                            if (mi == null)
                            {
                                throw new Exception("Filter specifies an invalid function");
                            }

                            //  get dictionary of parameters from XML 
                            var paramDict = nextFunction.Elements("Parameter").ToDictionary(d => d.Attribute("Name").Value, d => d.Attribute("Value").Value);
                            //  create object array from parameters, casting to proper type
                            object[] parameters = mi.GetParameters().Select(p => paramDict[p.Name].Length > 0 ? Convert.ChangeType(paramDict[p.Name], p.ParameterType) : null).ToArray();

                            newFilter.AddFunction(typeof(DataFilter).Assembly, mi, parameters);
                        }

                        Filters.Add(filterName, newFilter);
                    }
                }
            }
            catch (Exception e)
            {
                throw e;
            }
        }


        /// <summary>
        /// Get collection of all filter names
        /// </summary>
        public IEnumerable<string> GetFilterNames()
        {
            return Filters.Keys.ToArray();
        }


        /// <summary>
        /// Get a filter by name
        /// </summary>
        public SignalFilter GetFilter(string name)
        {
            if (Filters.ContainsKey(name))
                return Filters[name];

            return null;
        }


        /// <summary>
        /// Constructor
        /// </summary>
        public SignalFilters()
        {
            Filters = new Dictionary<string, SignalFilter>();
        }

        //  The filter collection
        protected Dictionary<string, SignalFilter> Filters;

    }
}
