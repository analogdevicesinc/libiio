/*
 * Copyright (C) 2025 Analog Devices, Inc.
 * Author: Dan Nechita <dan.nechita@analog.com>
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

using iio;

namespace libiio_scan_context_app_csharp
{
    class Program
    {

        static void Main(string[] args)
        {
            Console.WriteLine("Creating an iio scan context instace" + Environment.NewLine);
            ScanContext scan_context = new ScanContext();

            Console.WriteLine("Scan for backends of type: usb");
            try
            {
                Dictionary<string, string> usb_backends = scan_context.get_usb_backend_contexts();
                Console.WriteLine("Usb backends found: " + usb_backends.Count);
                foreach (KeyValuePair<string, string> kvp in usb_backends)
                {
                    Console.WriteLine($"Key: {kvp.Key}, Value: {kvp.Value}");
                }
            }
            catch (Exception e)
            {
                Console.WriteLine("Exception: " + e.Message);
            }

            Console.WriteLine(Environment.NewLine + "Scan for backends of type: network");
            try
            {
                Dictionary<string, string> network_backends = scan_context.get_dns_sd_backend_contexts();
                Console.WriteLine("Network backends found: " + network_backends.Count);
                foreach (KeyValuePair<string, string> kvp in network_backends)
                {
                    Console.WriteLine($"Key: {kvp.Key}, Value: {kvp.Value}");
                }
            }
            catch (Exception e)
            {
                Console.WriteLine("Exception: " + e.Message);
            }

            Console.WriteLine(Environment.NewLine + "Scan for backends of type: local");
            Dictionary<string, string> local_backends;
            try
            {
                local_backends = scan_context.get_local_backend_contexts();
                Console.WriteLine("Local backends found: " + local_backends.Count);
                foreach (KeyValuePair<string, string> kvp in local_backends)
                {
                    Console.WriteLine($"Key: {kvp.Key}, Value: {kvp.Value}");
                }
            }
            catch (Exception e)
            {
                Console.WriteLine("Exception: " + e.Message);
                Console.WriteLine("Most likely not running locally.");
            }

            Console.WriteLine(Environment.NewLine + "Succesfully closing");
        }
    }
}
