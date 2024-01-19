/*
 * Copyright (C) 2014 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
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
    class ExampleProgram
    {
        static void Main(string[] args)
        {
            const uint blocksize = 1024;
            Scan scan = new Scan();
            if (scan.nb_results > 0) {
                Console.WriteLine("* Scanned contexts: " + scan.nb_results);
            }
            foreach (var entry in scan.results) {
                Console.WriteLine("\t" + entry.Key + " " + entry.Value);
            }
            scan.Dispose();

            Context ctx;
            try
            {
                ctx = new Context("ip:192.168.2.1");
                Console.WriteLine(ctx.xml);
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

                if (dev is Trigger)
                {
                    Console.WriteLine("* Found trigger! Rate=" + ((Trigger) dev).get_rate());
                }

                Console.WriteLine("\t\t" + dev.channels.Count + " channels found:");

                foreach (Channel chn in dev.channels)
                {
                    string type = "input";
                    if (chn.output)
                    {
                        type = "output";
                    }
                    Console.WriteLine("\t\t\t" + chn.id + ": " + chn.name + " (" + type + ")");

                    if (chn.attrs.Count == 0)
                    {
                        continue;
                    }

                    Console.WriteLine("\t\t\t" + chn.attrs.Count + " channel-specific attributes found:");
                    foreach (Attr attr in chn.attrs)
                    {
                        string attr_name = "\t\t\t\t" + attr.name;
                        string attr_val = "";
                        try
                        {
                            if (attr.name == "calibscale")
                            {
                                double val = attr.read_double();
                                Console.WriteLine(attr_name + " " + val);
                            }
                            else
                            {
                                attr_val = attr.read();
                                Console.WriteLine(attr_name + " " + attr_val);
                            }
                        }
                        catch (IIOException)
                        {
                            Console.WriteLine(attr_name);
                        }
                    }
                }

				/* If we find cf-ad9361-lpc, try to read a few bytes from the first channel */
                if (dev.name.CompareTo("cf-ad9361-lpc") == 0)
                {
                    ChannelsMask chnmask = new ChannelsMask((uint)dev.channels.Count);
                    Channel rx0_i = dev.channels[0];
                    rx0_i.enable(chnmask);
                    Channel rx0_q = dev.channels[1];
                    rx0_q.enable(chnmask);

                    IOBuffer buf = new IOBuffer(dev, chnmask);
                    uint sampleSize = dev.get_sample_size(chnmask);
                    Console.WriteLine("* Sample size is " + sampleSize + "\n");

                    Console.WriteLine("\t\t\t" + buf.attrs.Count + " buffer-specific attributes found:");
                    foreach (Attr attr in buf.attrs)
                    {
                        Console.WriteLine("\t\t\t\t" + attr.name + " " + attr.read());
                    }

                    Stream stream = new Stream(buf, 4, blocksize);
                    for (int i=0; i < 10; i++) {
                        Block block = stream.next();
                        byte[] databuf = new byte[blocksize];
                        block.read(databuf);
                        Console.WriteLine("* ("+ i + ") Read " + databuf.Length + " bytes from hardware");
                    }

                    stream.Dispose();
                    buf.Dispose();
                    chnmask.Dispose();
                }

                if (dev.attrs.Count == 0)
                {
                    continue;
                }

                Console.WriteLine("\n\t\t" + dev.attrs.Count + " device-specific attributes found:");
                foreach (Attr attr in dev.attrs)
                {
                    Console.WriteLine("\t\t\t" + attr.name);
                }

            }
        }
    }
}
