/*
 * Copyright (C) 2024 Analog Devices, Inc.
 * Author: Alexandra Trifan <Alexandra.Trifan@analog.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 * */

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using iio;

namespace IIOCSharp
{
    class ExampleIIOEvent
    {
        static void Main(string[] args)
        {
            Scan scan = new Scan();
            if (scan.nb_results > 0) {
                Console.WriteLine("* Scanned contexts: " + scan.nb_results);
            }
            foreach (var entry in scan.results) {
                Console.WriteLine("\t" + entry.Key + " " + entry.Value);
            }
            scan.Dispose();

            if (args.Length <= 1)
            {
                Console.WriteLine("Please provide the context URI and IIO device.\n");
                return;
            }

            string uri = args[0];
            string ev_device = args[1];
            Context ctx;
            try
            {
                ctx = new Context(uri);
            } catch (IIOException e)
            {
                Console.WriteLine(e.ToString());
                return;
            }

            Console.WriteLine("* IIO context created: " + ctx.name);
            Console.WriteLine("* IIO context description: " + ctx.description);

            Console.WriteLine("* IIO context has " + ctx.devices.Count + " devices:");
            foreach (Device dev in ctx.devices) {
                Console.WriteLine("\t" + dev.id + ": " + dev.name);
                if (dev.name == ev_device)
                {
                    EventStream event_stream = new EventStream(dev);
                    while (true)
                    {
                        try 
                        {
                            IIOEvent ev = event_stream.read_event(false);
                            string timestamp = ev.timestamp.ToString();
                            string print_ev = "Event: time: " + timestamp;
                            if (ev.chn != null)
                            {
                                print_ev += ", channel(s): " + ev.chn.id.ToString();
                            }
                            if (ev.chn_diff != null)
                            {
                                print_ev += " - " + ev.chn_diff.id;
                            }
                            print_ev += ", evtype: " + ev.type;
                            print_ev += ", direction: " + ev.direction;
                            Console.WriteLine(print_ev);
                        }
                        catch (IIOException e)
                        {
                            Console.WriteLine("Error: " + e.ToString());
                            event_stream.Dispose();
                            return;
                        }
                    }
                    event_stream.Dispose();
                }
            }
        }
    }
}
