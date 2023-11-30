// This test is designed to verify data from transmit to receive buffers by
// using a ramp signal. The ramp signal is generated on the TX side and then
// received on the RX side. The RX side then checks to make sure the ramp is
// continuous within the buffer but not across buffers. However, every buffer is
// checked to make sure the ramp is continuous.

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "iio/iio.h"

#ifndef TESTS_DEBUG
#define TESTS_DEBUG 0
#endif

// Use (void) to silence unused warnings.
#define assertm(exp, msg) assert(((void)msg, exp))

#define dprintf(fmt, ...)                                                      \
  do {                                                                         \
    if (TESTS_DEBUG>0)                        \
      fprintf(stderr, fmt, ##__VA_ARGS__);                                     \
  } while (0)

// User Set
#define N_TX_SAMPLES 128
#define RX_OVERSAMPLE 4
#define SUCCESSIVE_BUFFER_TO_CHECK 31
#define N_RX_BLOCKS 4

// Calculated/Constant
#define N_RX_SAMPLES N_TX_SAMPLES *RX_OVERSAMPLE
#define N_CHANNELS 2
#define BYTES_PER_SAMPLE 2

struct iio_context *ctx;
struct iio_device *phy, *rx, *tx;
const struct iio_attr *attr;
struct iio_channel *chn;
struct iio_channels_mask *txmask, *rxmask;
struct iio_buffer *txbuf, *rxbuf;
struct iio_block *txblock;
const struct iio_block *rxblock;
struct iio_stream *rxstream;

int main() {

  int err;

  const char *uri = getenv("URI_AD9361");
  if (uri == NULL)
    exit(0); // Cant find anything don't run tests
  ctx = iio_create_context(NULL, uri);

  phy = iio_context_find_device(ctx, "ad9361-phy");
  assertm(phy, "Unable to find AD9361-phy device");
  rx = iio_context_find_device(ctx, "cf-ad9361-lpc");
  assertm(rx, "Unable to find RX device");
  tx = iio_context_find_device(ctx, "cf-ad9361-dds-core-lpc");
  assertm(tx, "Unable to find TX device");

  // Configure device into loopback mode
  attr = iio_device_find_debug_attr(phy, "loopback");
  assertm(attr, "Unable to find loopback attribute");
  iio_attr_write_string(attr, "1");

  // TX Side
  txmask = iio_create_channels_mask(iio_device_get_channels_count(tx));
  assertm(txmask, "Unable to create TX mask");

  chn = iio_device_find_channel(tx, "voltage0", true);
  assertm(chn, "Unable to find TX channel");
  iio_channel_enable(chn, txmask);
  chn = iio_device_find_channel(tx, "voltage1", true);
  assertm(chn, "Unable to find TX channel");
  iio_channel_enable(chn, txmask);

  txbuf = iio_device_create_buffer(tx, 0, txmask);
  assertm(txbuf, "Unable to create TX buffer");

  txblock = iio_buffer_create_block(txbuf, N_TX_SAMPLES * BYTES_PER_SAMPLE *
                                               N_CHANNELS);
  assertm(txblock, "Unable to create TX block");

  // Generate ramp signal on both I and Q channels
  int16_t *p_dat, *p_end;
  ptrdiff_t p_inc;
  int16_t idx = 0;

  p_end = iio_block_end(txblock);
  p_inc = iio_device_get_sample_size(tx, txmask);
  chn = iio_device_find_channel(tx, "voltage0", true);

  for (p_dat = iio_block_first(txblock, chn); p_dat < p_end;
       p_dat += p_inc / sizeof(*p_dat)) {
    // Bitshift 4 bits up. During loopback hardware will shift back 4 bits
    p_dat[0] = idx << 4;
    p_dat[1] = idx << 4;
    idx++;
  }
  iio_block_enqueue(txblock, 0, true);
  iio_buffer_enable(txbuf);
  sleep(2);

  // RX Side
  rxmask = iio_create_channels_mask(iio_device_get_channels_count(rx));
  assertm(rxmask, "Unable to create RX mask");

  chn = iio_device_find_channel(rx, "voltage0", false);
  assertm(chn, "Unable to find RX channel voltage0");
  iio_channel_enable(chn, rxmask);
  chn = iio_device_find_channel(rx, "voltage1", false);
  assertm(chn, "Unable to find RX channel voltage1");
  iio_channel_enable(chn, rxmask);

  rxbuf = iio_device_create_buffer(rx, 0, rxmask);
  assertm(rxbuf, "Unable to create RX buffer");

  rxstream = iio_buffer_create_stream(rxbuf, N_RX_BLOCKS, N_RX_SAMPLES);
  assertm(rxstream, "Unable to create RX stream");

  p_inc = iio_device_get_sample_size(rx, rxmask);
  chn = iio_device_find_channel(rx, "voltage0", false);

  bool found_start = false;
  int16_t ramp_indx = 0;

  // Create check vector
  bool ramp_found_check_vector[SUCCESSIVE_BUFFER_TO_CHECK];
  bool continuous_check_vector[SUCCESSIVE_BUFFER_TO_CHECK];

  // Remove first few blocks as they might be old
  for (int i = 0; i < 30; i++) {
    rxblock = iio_stream_get_next_block(rxstream);
    dprintf("Removing block %d\n", i);
  }

  // Check several buffers to make sure no glitches occurred
  for (int i = 0; i < SUCCESSIVE_BUFFER_TO_CHECK; i++) {

    dprintf("Checking buffer %d of %d\n", i + 1, SUCCESSIVE_BUFFER_TO_CHECK);

    rxblock = iio_stream_get_next_block(rxstream);
    p_end = iio_block_end(rxblock);

    // Within a block data should be continuous but not necessarily across
    // blocks
    found_start = false;
    continuous_check_vector[i] = true; // assume good
    ramp_indx = 0;

    for (p_dat = iio_block_first(rxblock, chn); p_dat < p_end;
         p_dat += p_inc / sizeof(*p_dat)) {

      // Locate top of ramp
      if (p_dat[0] == (N_TX_SAMPLES - 1) && p_dat[1] == (N_TX_SAMPLES - 1) &&
          !found_start) {
        found_start = true;
        continue; // Wrap to ramp restarts on next sample
      }

      // Make sure ramp is continuous
      if (found_start) {
        dprintf("Expected: %d\n", ramp_indx);
        dprintf("Actual: %d, %d (I, Q)\n\n", p_dat[0], p_dat[1]);
        if (p_dat[0] != ramp_indx && p_dat[1] != ramp_indx) {
          dprintf("--->Expected: %d (Buffer %d)\n", ramp_indx, i);
          dprintf("--->Actual: %d, %d (I, Q) [Buffer %d]\n\n", p_dat[0],
                  p_dat[1], i);
          dprintf("\n\n");
          continuous_check_vector[i] = false;
        }
        if (ramp_indx == (N_TX_SAMPLES - 1)) {
          ramp_indx = 0;
        } else
          ramp_indx++;
      }
    }

    ramp_found_check_vector[i] = found_start;
    if (!found_start)
      continuous_check_vector[i] = false;
  }

  // Examine check vector
  bool failed_c1 = false;
  bool failed_c2 = false;
  dprintf("1 == Check Passed, 0 == Failed\n");
  dprintf("Ramp Check, Contiguous Check (Buffer #)\n");

  for (int i = 0; i < SUCCESSIVE_BUFFER_TO_CHECK; i++) {
    dprintf("%d, %d (%d)\n", ramp_found_check_vector[i],
            continuous_check_vector[i], i);
    if (!ramp_found_check_vector[i])
      failed_c1 = true;
    if (!continuous_check_vector[i])
      failed_c2 = true;
  }
  dprintf("\n");

  assertm(!failed_c1, "Ramp was not found in all buffers");
  assertm(!failed_c2, "Ramp was not contiguous in all buffers");

  iio_stream_destroy(rxstream);
  iio_buffer_destroy(rxbuf);

  //   // Manual check RX (disable asserts above first)
  //   printf("Open up the time scope to see data. Should be a ramp from
  //   0->%d\n",
  //          idx - 1);
  //   sleep(40);

  // Cleanup
  iio_block_destroy(txblock);
  iio_buffer_destroy(txbuf);

  return 0;
}