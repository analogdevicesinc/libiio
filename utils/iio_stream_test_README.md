# iio_stream_test - Streaming Data Capture Utility

## Overview

`iio_stream_test` is a utility designed to capture streaming data from AD9361/FMCOMMS3 devices using small buffer sizes. It was created specifically to test data continuity when using 128 and 256 complex sample buffers at high sample rates (e.g., 8 Msps).

## Purpose

This utility helps validate that:
- Small buffer sizes can capture continuous data without discontinuities
- The RX path works correctly with 128 and 256 complex sample buffers
- Simultaneous RX/TX streaming works with small buffers
- The actual sample rate matches the configured rate

## Test Modes

The utility supports three test modes:

1. **rx128**: RX-only capture with 128 complex samples per buffer
2. **rx256**: RX-only capture with 256 complex samples per buffer  
3. **rxtx128**: Simultaneous RX/TX with 128 complex samples per buffer

## Usage

### Basic Usage

```bash
iio_stream_test -i <ip_address> -m <mode>
```

### Command-Line Options

- `-i, --ip <address>`: IP address of the remote board (required)
- `-m, --mode <mode>`: Test mode: rx128, rx256, or rxtx128 (default: rx128)
- `-s, --samples <count>`: Number of samples to capture (default: 8M samples = 1 second at 8 Msps)
- `-o, --output <file>`: Output filename for captured data (default: auto-generated based on mode)
- `-r, --rate <Hz>`: Sample rate in Hz (default: 8000000)
- `-l, --loopback`: Enable digital loopback mode (default: disabled)
- `-h, --help`: Display usage information
- `-V, --version`: Display version information

### Examples

#### Capture 1 second of data with 128 sample buffers (RX only):
```bash
iio_stream_test -i 192.168.2.1 -m rx128
```

#### Capture 1 second of data with 256 sample buffers:
```bash
iio_stream_test -i 192.168.2.1 -m rx256
```

#### Simultaneous RX/TX test with 128 sample buffers:
```bash
iio_stream_test -i 192.168.2.1 -m rxtx128
```

#### Capture 2 seconds of data with custom output file:
```bash
iio_stream_test -i 192.168.2.1 -m rx128 -s 16777216 -o my_capture.bin
```

#### Use custom sample rate (e.g., 10 Msps):
```bash
iio_stream_test -i 192.168.2.1 -m rx128 -r 10000000 -s 10000000
```

#### Enable digital loopback mode:
```bash
iio_stream_test -i 192.168.2.1 -m rx128 --loopback
```

This enables the AD9361's internal digital loopback, which routes TX data directly to RX internally. Useful for validating the data path without requiring external RF connections. Note: The utility does not transmit any test pattern; it only enables the loopback setting. You need to configure the TX DDS or other TX sources separately.

## Output Files

### RX-only modes (rx128, rx256)
- Default output: `capture_rx128.bin` or `capture_rx256.bin`
- Contains interleaved I/Q samples (int16_t each)
- Format: I0, Q0, I1, Q1, I2, Q2, ...

### RX/TX mode (rxtx128)
- RX data: `capture_rxtx128_rx.bin` (or custom filename with `-o`)
- TX reference: `capture_rxtx128_tx.bin`
- Both files contain interleaved I/Q samples (int16_t each)
- TX transmits zeros in cyclic mode (no test pattern generated)

## Data Format

All captured data files use the following format:
- **Sample type**: Complex samples (I and Q components)
- **Data type**: int16_t (signed 16-bit integers)
- **Byte order**: Native system byte order
- **Layout**: Interleaved I/Q pairs

Each complex sample consists of 4 bytes:
```
Bytes 0-1: I component (int16_t)
Bytes 2-3: Q component (int16_t)
```

## Statistics and Output

During capture, the utility displays:
- Real-time progress indicator
- Number of samples captured
- Number of blocks processed

After capture completes, it displays:
- Total samples captured
- Total blocks processed
- Block size used
- Capture duration
- Actual sample rate achieved
- Expected sample rate
- Rate error percentage

## Analyzing Captured Data

### Using Python/NumPy

```python
import numpy as np
import matplotlib.pyplot as plt

# Load the captured data
data = np.fromfile('capture_rx128.bin', dtype=np.int16)

# Reshape into I/Q pairs
iq_data = data.reshape(-1, 2)
i_samples = iq_data[:, 0]
q_samples = iq_data[:, 1]

# Convert to complex
complex_samples = i_samples + 1j * q_samples

# Plot time domain
plt.figure()
plt.plot(np.abs(complex_samples[:1000]))
plt.title('Magnitude (first 1000 samples)')
plt.xlabel('Sample')
plt.ylabel('Magnitude')
plt.show()

# Plot frequency domain
plt.figure()
fft = np.fft.fftshift(np.fft.fft(complex_samples[:8192]))
plt.plot(20*np.log10(np.abs(fft)))
plt.title('FFT')
plt.xlabel('Bin')
plt.ylabel('Magnitude (dB)')
plt.show()
```

### Using MATLAB

```matlab
% Load the captured data
fid = fopen('capture_rx128.bin', 'r');
data = fread(fid, 'int16');
fclose(fid);

% Reshape into I/Q pairs
iq_data = reshape(data, 2, [])';
i_samples = iq_data(:, 1);
q_samples = iq_data(:, 2);

% Convert to complex
complex_samples = complex(i_samples, q_samples);

% Plot time domain
figure;
plot(abs(complex_samples(1:1000)));
title('Magnitude (first 1000 samples)');
xlabel('Sample');
ylabel('Magnitude');

% Plot frequency domain
figure;
fft_data = fftshift(fft(complex_samples(1:8192)));
plot(20*log10(abs(fft_data)));
title('FFT');
xlabel('Bin');
ylabel('Magnitude (dB)');
```

## Checking for Discontinuities

To verify data continuity, you can:

1. **Visual inspection**: Plot the time-domain magnitude and look for sudden jumps
2. **Derivative analysis**: Calculate the sample-to-sample differences
3. **Spectral analysis**: Look for spurious tones or artifacts in the FFT
4. **Statistics**: Calculate mean, variance, and look for outliers

Example discontinuity detection in Python:

```python
import numpy as np

# Load data
data = np.fromfile('capture_rx128.bin', dtype=np.int16)
iq_data = data.reshape(-1, 2)
complex_samples = iq_data[:, 0] + 1j * iq_data[:, 1]

# Calculate magnitude
magnitude = np.abs(complex_samples)

# Look for large jumps (discontinuities)
diff = np.diff(magnitude)
threshold = 10000  # Adjust based on your signal
discontinuities = np.where(np.abs(diff) > threshold)[0]

if len(discontinuities) > 0:
    print(f"Found {len(discontinuities)} potential discontinuities at samples:")
    print(discontinuities)
else:
    print("No discontinuities detected")
```

## Expected Performance

For a ZCU102/FMCOMMS3 setup at 8 Msps:

- **Block size 128**: 512 bytes per block, ~15625 blocks/second
- **Block size 256**: 1024 bytes per block, ~7813 blocks/second
- **Rate error**: Should be < 1% under normal conditions

## Troubleshooting

### "RX device not found"
- Ensure the board is properly connected
- Verify the IP address is correct
- Check that IIOD is running on the target

### High rate error (> 1%)
- May indicate timing issues
- Check system load on the host
- Verify network connection quality

### Connection timeout
- Increase timeout with standard libiio options
- Check firewall settings
- Verify network connectivity

## Hardware Requirements

- **Target**: ZCU102 with FMCOMMS3 (or compatible AD9361-based platform)
- **Connection**: Network (IP-based)
- **Sample rate**: Tested at 8 Msps, configurable

## Building

The utility is built as part of the standard libiio build process:

```bash
mkdir build
cd build
cmake ..
make iio_stream_test
```

## Notes

- The utility uses the iio_block API directly for fine-grained control
- For RX/TX mode, TX transmits zeros in cyclic mode (no test pattern)
- For loopback mode (RX-only), the utility only enables the digital loopback setting without transmitting any pattern
- Data files can be large (8M samples = 32 MB)
- Press Ctrl+C to stop capture early (gracefully)
