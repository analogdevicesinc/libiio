// SPDX-License-Identifier: MIT
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2023 Analog Devices, Inc.
 * Author: Adrian Stanea <Adrian.Stanea@analog.com>
 */

using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using iio;

[assembly: AssemblyTitle("Pluto_Example")]
[assembly: AssemblyVersion("1.0.0.0")]
[assembly: System.Runtime.InteropServices.ComVisible(false)]
[assembly: CLSCompliant(true)]
namespace IIOCSharp
{
    static class PlutoExample
    {
        const int INT16_BYTE_STEP = 2;
        const string URI = "ip:pluto.local";
        const double TXLO = 1000000000;
        const double TXBW = 5000000;
        const double TXFS = 3000000;
        const double RXLO = TXLO;
        const double RXBW = TXBW;
        const double RXFS = TXFS;
        const string GAIN_CTRL_MODE = "slow_attack";
        const double HW_GAIN = -30;

        const string CHN_VOLTAGE0 = "voltage0";
        const string CHN_VOLTAGE1 = "voltage1";

        static void Main(string[] args)
        {
            Context ctx;
            try
            {
                ctx = new Context(URI);
            }
            catch (IIOException e)
            {
                Console.WriteLine("Unable to create IIO context: " + e.Message);
                return;
            }
            // get reference to devices
            var ctrl = ctx.get_device("ad9361-phy");
            var tx_dac = ctx.get_device("cf-ad9361-dds-core-lpc");
            var rx_adc = ctx.get_device("cf-ad9361-lpc");

            rx_config(ctrl, RXLO, RXBW, RXFS, GAIN_CTRL_MODE);
            tx_config(ctrl, TXLO, TXBW, TXFS, HW_GAIN);

            // Enable all IQ channels
            ChannelsMask tx_mask = new ChannelsMask((uint)tx_dac.channels.Count);
            ChannelsMask rx_mask = new ChannelsMask((uint)rx_adc.channels.Count);
            enable_tx(tx_dac, tx_mask);
            enable_rx(rx_adc, rx_mask);

            // Cyclic buffer to output perdiodic waveforms
            uint samples_per_channel = Convert.ToUInt16(Math.Pow(2, 15));

            IOBuffer tx_buf = tx_dac.buffers[0];
            IOBuffer rx_buf = rx_adc.buffers[0];

            iio.Stream tx_stream = new iio.Stream(tx_buf, tx_mask, 4, samples_per_channel);
            iio.Stream rx_stream = new iio.Stream(rx_buf, rx_mask, 4, samples_per_channel);

            byte[] iqBytes = generate_sine(Convert.ToInt32(samples_per_channel), RXFS);

            // Send data to TX buffer
            Block tx_block = tx_stream.next();
            tx_block.fill(iqBytes);

            // read data on rx buffer
            const int num_readings = 10;
            List<int> reals, imags;
            read_rx_data(rx_stream, num_readings, Convert.ToInt32(samples_per_channel), out reals, out imags);
            Console.WriteLine("Read " + reals.Count + " I samples and " + imags.Count + " Q samples");

            rx_stream.Dispose();
            tx_stream.Dispose();
            rx_mask.Dispose();
            tx_mask.Dispose();
        }

        static void rx_config(Device device, double rx_lo, double rx_bw, double rx_fs, string gain_ctrl_mode)
        {
            var rx_chn = device.get_channel(CHN_VOLTAGE0, false);

            device.get_channel("RX_LO", true).attrs["frequency"].write(rx_lo);
            rx_chn.attrs["sampling_frequency"].write(rx_fs);
            rx_chn.attrs["rf_bandwidth"].write(rx_bw);
            rx_chn.attrs["gain_control_mode"].write(gain_ctrl_mode);
        }

        static void tx_config(Device device, double tx_lo, double tx_bw, double tx_fs, double hw_gain)
        {
            var tx_chn = device.get_channel(CHN_VOLTAGE0, true);

            device.get_channel("TX_LO", true).attrs["frequency"].write(tx_lo);
            tx_chn.attrs["sampling_frequency"].write(tx_fs);
            tx_chn.attrs["rf_bandwidth"].write(tx_bw);
            tx_chn.attrs["hardwaregain"].write(hw_gain);
        }

        static void enable_tx(Device tx_dac, ChannelsMask mask)
        {
            tx_dac.get_channel(CHN_VOLTAGE0, true).enable(mask);
            tx_dac.get_channel(CHN_VOLTAGE1, true).enable(mask);
        }

        static void enable_rx(Device rx_adc, ChannelsMask mask)
        {
            rx_adc.get_channel(CHN_VOLTAGE0, false).enable(mask);
            rx_adc.get_channel(CHN_VOLTAGE1, false).enable(mask);
        }

        static byte[] generate_sine(int samples_per_channel, double rx_fs)
        {
            const int fc = 10000;
            double ts = 1.0 / rx_fs;
            double[] t = Enumerable.Range(0, samples_per_channel).Select(n => n * ts).ToArray();

            double[] i = t.Select(x => (Math.Sin(2 * Math.PI * x * fc) * Math.Pow(2, 14))).ToArray();
            double[] q = t.Select(x => (Math.Cos(2 * Math.PI * x * fc) * Math.Pow(2, 14))).ToArray();

            Int16[] iq = new Int16[i.Length + q.Length];
            for (int j = 0; j < i.Length; j++)
            {
                iq[j * INT16_BYTE_STEP] = Convert.ToInt16(i[j]);
                iq[j * INT16_BYTE_STEP + 1] = Convert.ToInt16(q[j]);
            }

            byte[] iqBytes = iq.SelectMany(BitConverter.GetBytes).ToArray();
            return iqBytes;
        }

        static void read_rx_data(iio.Stream stream, int num_readings, int samples_per_channel,
                                  out List<int> reals, out List<int> imags)
        {
            reals = new List<int>();
            imags = new List<int>();

            byte[] data = new byte[samples_per_channel];
            for (int k = 0; k < num_readings; k++)
            {
                Block block = stream.next();
                block.read(data);

                Int16[] data_casted = new Int16[data.Length / INT16_BYTE_STEP];
                Buffer.BlockCopy(data, 0, data_casted, 0, data.Length);
                for (int idx = 0; idx < data_casted.Length; idx += INT16_BYTE_STEP)
                {
                    reals.Add(data_casted[idx]);
                    imags.Add(data_casted[idx + 1]);
                }
            }
        }
    }
}
