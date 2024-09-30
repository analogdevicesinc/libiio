// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2023 Analog Devices, Inc.
 * Author: Adrian Stanea <Adrian.Stanea@analog.com>
 */

using System;
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
        const double TXLO = 1_000_000_000;
        const double TXBW = 5_000_000;
        const double TXFS = 3_000_000;
        const double RXLO = TXLO;
        const double RXBW = TXBW;
        const double RXFS = TXFS;
        const string GAIN_CTRL_MODE = "slow_attack";
        const double HW_GAIN = -30;

        const string CHN_VOLTAGE0 = "voltage0";
        const string CHN_VOLTAGE1 = "voltage1";

        static void Main(string[] args)
        {
            Context ctx = new Context(URI);
            if (ctx == null)
            {
                Console.WriteLine("Unable to create IIO context");
                return;
            }
            // get reference to devices
            var ctrl = ctx.find_device("ad9361-phy");
            var tx_dac = ctx.find_device("cf-ad9361-dds-core-lpc");
            var rx_adc = ctx.find_device("cf-ad9361-lpc");

            rx_config(ctrl, RXLO, RXBW, RXFS, GAIN_CTRL_MODE);
            tx_config(ctrl, TXLO, TXBW, TXFS, HW_GAIN);

            // Enable all IQ channels
            enable_tx(tx_dac);
            enable_rx(rx_adc);

            // Cyclic buffer to output perdiodic waveforms
            uint samples_per_channel = Convert.ToUInt16(Math.Pow(2, 15));

            var tx_buf = new IOBuffer(tx_dac, samples_per_channel, true);
            var rx_buff = new IOBuffer(rx_adc, samples_per_channel, false);

            byte[] iqBytes = generate_sine(Convert.ToInt32(samples_per_channel), RXFS);

            // Send data to TX buffer
            tx_buf.fill(iqBytes);
            tx_buf.push();

            // read data on rx buffer
            const int num_readings = 10;
            (List<int> reals, List<int> imags) = read_rx_data(rx_buff, num_readings, Convert.ToInt32(samples_per_channel));
        }

        static void rx_config(Device device, double rx_lo, double rx_bw, double rx_fs, string gain_ctrl_mode)
        {
            var rx_chn = device.find_channel(CHN_VOLTAGE0, false);

            device.find_channel("RX_LO", true).find_attribute("frequency").write(rx_lo);
            rx_chn.find_attribute("rf_bandwidth").write(rx_bw);
            rx_chn.find_attribute("sampling_frequency").write(rx_fs);
            rx_chn.find_attribute("gain_control_mode").write(gain_ctrl_mode);
        }

        static void tx_config(Device device, double tx_lo, double tx_bw, double tx_fs, double hw_gain)
        {
            var tx_chn = device.find_channel(CHN_VOLTAGE0, true);

            device.find_channel("TX_LO", true).find_attribute("frequency").write(tx_lo);
            tx_chn.find_attribute("rf_bandwidth").write(tx_bw);
            tx_chn.find_attribute("sampling_frequency").write(tx_fs);
            tx_chn.find_attribute("hardwaregain").write(hw_gain);
        }

        static void enable_tx(Device tx_adc)
        {
            tx_adc.find_channel(CHN_VOLTAGE0, true).enable();
            tx_adc.find_channel(CHN_VOLTAGE1, true).enable();
        }

        static void enable_rx(Device rx_dac)
        {
            rx_dac.find_channel(CHN_VOLTAGE0, false).enable();
            rx_dac.find_channel(CHN_VOLTAGE1, false).enable();
        }

        static byte[] generate_sine(int samples_per_channel, double rx_fs)
        {
            const int fc = 10000;
            double ts = 1.0 / rx_fs;
            double[] t = Enumerable.Range(0, samples_per_channel).Select(i => i * ts).ToArray();

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

        static (List<int> reals, List<int> imags) read_rx_data(IOBuffer rx_buff, int num_readings, int samples_per_channel)
        {
            List<int> reals = new List<int>();
            List<int> imags = new List<int>();

            byte[] data = new byte[samples_per_channel];
            for (int k = 0; k < num_readings; k++)
            {
                rx_buff.refill();
                rx_buff.read(data);

                Int16[] data_casted = new Int16[data.Length / INT16_BYTE_STEP];
                Buffer.BlockCopy(data, 0, data_casted, 0, data.Length);
                for (int idx = 0; idx < data_casted.Length; idx += INT16_BYTE_STEP)
                {
                    reals.Add(data_casted[idx]);
                    imags.Add(data_casted[idx + 1]);
                }
            }
            return (reals, imags);
        }
    }
}
