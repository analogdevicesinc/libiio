// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * libiio - AD9361 IIO streaming example
 *
 * Copyright (C) 2014 IABG mbH
 * Author: Michael Feilen <feilen_at_iabg.de>
 **/


#include <iio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <stdio.h>
#include <errno.h>

#define NUM_SAMPLES 792000
#define NUM_SHA_SAMPLES 100
#define ADC_RST_REG 0x40
#define DAC_SYNC_REG 0x44
#define IIO_ENSURE(expr) { \
	if (!(expr)) { \
	(void) fprintf(stderr, "assertion failed (%s:%d)\n", __FILE__, __LINE__); \
	(void) abort(); \
	} \
	}

/* RX is input, TX is output */
enum iodev { RX, TX };

/* static scratch mem for strings */
static char tmpstr[64];

/* IIO structs required for streaming */
static struct iio_context *ctx   = NULL;
static struct iio_channel *rx0_i = NULL;
static struct iio_channel *rx1_i = NULL;
static struct iio_buffer  *rxbuf = NULL;
static struct iio_buffer  *rxbuf_sha = NULL;
static bool stop;


static void handle_sig(int sig)
{
	printf("Waiting for process to finish... Got signal %d\n", sig);
	stop = true;
}

/* cleanup and exit */
static void shutdown(void)
{
	printf("* Destroying buffers\n");
	if (rxbuf) { iio_buffer_destroy(rxbuf); }
	if (rxbuf_sha) { iio_buffer_destroy(rxbuf_sha); }

	printf("* Destroying context\n");
	if (ctx) { iio_context_destroy(ctx); }
	exit(0);
}

/* helper function generating channel names */
static char* get_ch_name(const char* type, int id)
{
	snprintf(tmpstr, sizeof(tmpstr), "%s%d", type, id);
	return tmpstr;
}

/* returns ad9361 phy device */
static struct iio_device* get_ad9361_phy(void)
{
	struct iio_device *dev =  iio_context_find_device(ctx, "ad9361-phy");
	IIO_ENSURE(dev && "No ad9361-phy found");
	return dev;
}

/* finds AD9361 streaming IIO devices */
static bool get_ad9361_stream_dev(enum iodev d, struct iio_device **dev)
{
	switch (d) {
	case TX: *dev = iio_context_find_device(ctx, "cf-ad9361-dds-core-lpc"); return *dev != NULL;
	case RX: *dev = iio_context_find_device(ctx, "cf-ad9361-lpc");  return *dev != NULL;
	default: IIO_ENSURE(0); return false;
	}
}

/* finds AD9361 streaming IIO channels */
static bool get_ad9361_stream_ch(enum iodev d, struct iio_device *dev, int chid, struct iio_channel **chn)
{
	*chn = iio_device_find_channel(dev, get_ch_name("voltage", chid), d == TX);
	if (!*chn)
		*chn = iio_device_find_channel(dev, get_ch_name("altvoltage", chid), d == TX);
	return *chn != NULL;
}

/* finds AD9361 phy IIO configuration channel with id chid */
static bool get_phy_chan(enum iodev d, int chid, struct iio_channel **chn)
{
	switch (d) {
	case RX: *chn = iio_device_find_channel(get_ad9361_phy(), get_ch_name("voltage", chid), false); return *chn != NULL;
	case TX: *chn = iio_device_find_channel(get_ad9361_phy(), get_ch_name("voltage", chid), true);  return *chn != NULL;
	default: IIO_ENSURE(0); return false;
	}
}

int main (int argc, char **argv)
{
     struct iio_device *dev_ad9361_phy;
     struct iio_device *dev_ad9361_adc;
     struct iio_device *dev_ad9361_dac;
     struct iio_device *dev_sha3_dma;
     size_t nrx = 0;
 
     int err;
 
     // Listen to ctrl+c and IIO_ENSURE
     signal(SIGINT, handle_sig);
     
     printf("* Acquiring IIO context\n");
     if (argc == 1) {
	     IIO_ENSURE((ctx = iio_create_default_context()) && "No context");
     }
     else if (argc == 2) {
	     IIO_ENSURE((ctx = iio_create_context_from_uri(argv[1])) && "No context");
     }
     IIO_ENSURE(iio_context_get_devices_count(ctx) > 0 && "No devices");
     if (!ctx) {
          shutdown();
     }

     printf("* Acquiring AD9363 devices\n");
     IIO_ENSURE(get_ad9361_stream_dev(TX, &dev_ad9361_dac) && "No tx dev found");
     IIO_ENSURE(get_ad9361_stream_dev(RX, &dev_ad9361_adc) && "No rx dev found");

     printf("* Acquiring sha3 reader device\n");
     dev_sha3_dma = iio_context_find_device(ctx, "sha3-reader");
     if (!dev_sha3_dma) {
	     printf("* No sha3-reader device available\n");
	     shutdown();
     }

     printf("* Enabling IIO ADC channels\n");
     size_t adc_chn_count = iio_device_get_channels_count(dev_ad9361_adc);
     for (unsigned int i = 0; i < adc_chn_count; i++) {
	     struct iio_channel *chn = iio_device_find_channel(dev_ad9361_adc, get_ch_name("voltage", i), false);
	     if (iio_channel_is_scan_element(chn)) {
		     iio_channel_enable(chn);
	     }
     }

     printf("* Enabling IIO SHA channels\n");
     struct iio_channel *sha3_chn = iio_device_find_channel(dev_sha3_dma, get_ch_name("voltage", 0), false);
     if (!sha3_chn) {
	     printf("* No sha3-reader channel available\n");
	     shutdown();
     }
     iio_channel_enable(sha3_chn);

     printf("* Preparing adc and dac registers\n");
     int ret = iio_device_reg_write(dev_ad9361_adc, ADC_RST_REG, 2);
     if (ret < 0) {
	     printf("Error writing value 2 into the ADC RST register\n");
     } else {
	     printf("Succesfully written 2 into the ADC RST register.\n");
     }

     ret = iio_device_reg_write(dev_ad9361_dac, DAC_SYNC_REG, 1);
     if (ret < 0) {
	     printf("Error writing value 1 into the DAC SYNC register\n");
     } else {
	     printf("Succesfully written 1 into the DAC SYNC register.\n");
     }
     
     printf("* Creating non-cyclic IIO buffer with 792000 samples \n");
     rxbuf = iio_device_create_buffer(dev_ad9361_adc, NUM_SAMPLES, false);
     if (!rxbuf) {
	     perror("Could not create RX buffer");
	     shutdown();
     }

     printf("* Creating non-cyclic sha3-reader IIO buffer with 100 samples \n");
     rxbuf_sha = iio_device_create_buffer(dev_sha3_dma, NUM_SHA_SAMPLES, false);
     if (!rxbuf_sha) {
	     perror("Could not create RX buffer for sha3-reader");
	     shutdown();
     }


     printf("* Resetting adc and dac registers\n");
     ret = iio_device_reg_write(dev_ad9361_dac, DAC_SYNC_REG, 1);
     if (ret < 0) {
	     printf("Error writing value 1 into the DAC SYNC register\n");
     } else {
	     printf("Succesfully written 1 into the DAC SYNC register.\n");
     }

     ret = iio_device_reg_write(dev_ad9361_adc, ADC_RST_REG, 3);
     if (ret < 0) {
	     printf("Error writing value 3 into the ADC RST register\n");
     } else {
	     printf("Succesfully written 3 into the ADC RST register.\n");
     }


     printf("* Starting acquisition \n");
     ssize_t nbytes_rx, nbytes_rx_sha;
     char *p_dat, *p_end;
     ptrdiff_t p_inc;

     // Refill RX buffer
     nbytes_rx = iio_buffer_refill(rxbuf);
     if (nbytes_rx < 0) { printf("Error refilling buf %d\n",(int) nbytes_rx); shutdown(); }

     // Refill SHA buffer
     nbytes_rx_sha = iio_buffer_refill(rxbuf_sha);
     if (nbytes_rx_sha < 0) { printf("Error refilling buf sha3-reader %d\n",(int) nbytes_rx_sha); shutdown(); }

     p_inc = iio_buffer_step(rxbuf);
     p_end = iio_buffer_end(rxbuf);
     IIO_ENSURE(get_ad9361_stream_ch(RX, dev_ad9361_adc, 0, &rx0_i) && "RX chan i not found");

     // Prepare a file for the data output
     FILE *fileDataBinary = fopen("dataBinary.bin", "wb");
     if (fileDataBinary == NULL) {
	     perror("Error opening file");
	     return 1;
     }

     ssize_t sample_size = iio_device_get_sample_size(dev_ad9361_adc);
     void *start = iio_buffer_start(rxbuf);
     size_t read_len, len = (intptr_t) iio_buffer_end(rxbuf) - (intptr_t) start;

     if (NUM_SAMPLES && len > NUM_SAMPLES * sample_size)
	     len = NUM_SAMPLES * sample_size;

     for (read_len = len; len; ) {
	     size_t nb = fwrite(start, 1, len, fileDataBinary);
	     if (!nb)
		     shutdown();

	     len -= nb;
	     start = (void *)((intptr_t) start + nb);
     }

     // Close the file
     fclose(fileDataBinary);

     // Prepare a file for the sha3 output
     FILE *file = fopen("sha3_reader_output.bin", "w");
     if (file == NULL) {
	     perror("Error opening file");
	     return 1;
     }

     int j = 0;
     p_inc = iio_buffer_step(rxbuf_sha);
     p_end = iio_buffer_end(rxbuf_sha);
     void *p_dat_sha;
     for (p_dat_sha = (uint8_t *)iio_buffer_start(rxbuf_sha); (char*)p_dat_sha < p_end; p_dat_sha += p_inc) {
	     uint8_t *bytes = (uint8_t *)p_dat_sha;
	     for (int i = 0; i < 64; i++) {
		     fprintf(file, "%02x", bytes[i]);
	     }
	     fprintf(file, "\n");

     }

     // Close the file
     fclose(file);
     shutdown();
     
     return 0;
}

