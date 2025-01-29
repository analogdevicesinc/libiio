clear;

uri = 'ip:pluto.local';

% Rx stream config
RxCfg.BWHz = 2e6;
RxCfg.FsHz = 2.5e6;
RxCfg.LOHz = 2.5e9;
RxCfg.RFPort = 'A_BALANCED';

% Tx stream config
TxCfg.BWHz = 1.5e6;
TxCfg.FsHz = 2.5e6;
TxCfg.LOHz = 2.5e9;
TxCfg.RFPort = 'A';

fprintf("* Acquiring IIO context\n");
iioCtx = adi.libiio.context.iio_create_context(uri);
count = adi.libiio.context.iio_context_get_devices_count(iioCtx);

fprintf("* Acquiring AD9361 streaming devices\n");
iioDevTx = adi.libiio.context.iio_context_find_device(iioCtx, 'cf-ad9361-dds-core-lpc');
iioDevRx = adi.libiio.context.iio_context_find_device(iioCtx, 'cf-ad9361-lpc');

fprintf("* Configuring AD9361 for streaming\n");
fprintf("* Acquiring AD9361 phy channel 0\n");
ad9361PhyDev = adi.libiio.context.iio_context_find_device(iioCtx, 'ad9361-phy');
ad9361RxChn = adi.libiio.device.iio_device_find_channel(ad9361PhyDev, 'voltage0', false);
ad9361TxChn = adi.libiio.device.iio_device_find_channel(ad9361PhyDev, 'voltage0', true);

%% Rx
rfPortSelectAttrRx = adi.libiio.channel.iio_channel_find_attr(ad9361RxChn, 'rf_port_select');
status = adi.libiio.attribute.iio_attr_write_string(rfPortSelectAttrRx, RxCfg.RFPort);
assert(status==numel(RxCfg.RFPort)+1);

rfBWAttrRx = adi.libiio.channel.iio_channel_find_attr(ad9361RxChn, 'rf_bandwidth');
status = adi.libiio.attribute.iio_attr_write_longlong(rfBWAttrRx, RxCfg.BWHz);
assert(status==0);
fsAttrRx = adi.libiio.channel.iio_channel_find_attr(ad9361RxChn, 'sampling_frequency');
status = adi.libiio.attribute.iio_attr_write_longlong(fsAttrRx, RxCfg.FsHz);
assert(status==0);

% Configure LO channel
fprintf("* Acquiring AD9361 Rx lo channel\n")
ad9361RxLOChn = adi.libiio.device.iio_device_find_channel(ad9361PhyDev, 'altvoltage0', true);
loRx = adi.libiio.channel.iio_channel_find_attr(ad9361RxLOChn, 'frequency');
status = adi.libiio.attribute.iio_attr_write_longlong(loRx, RxCfg.LOHz);
assert(status==0);

%% Tx
rfPortSelectAttrTx = adi.libiio.channel.iio_channel_find_attr(ad9361TxChn, 'rf_port_select');
status = adi.libiio.attribute.iio_attr_write_string(rfPortSelectAttrTx, TxCfg.RFPort);
assert(status==numel(TxCfg.RFPort)+1);

rfBWAttrTx = adi.libiio.channel.iio_channel_find_attr(ad9361TxChn, 'rf_bandwidth');
status = adi.libiio.attribute.iio_attr_write_longlong(rfBWAttrTx, TxCfg.BWHz);
assert(status==0);
fsAttrTx = adi.libiio.channel.iio_channel_find_attr(ad9361TxChn, 'sampling_frequency');
status = adi.libiio.attribute.iio_attr_write_longlong(fsAttrTx, TxCfg.FsHz);
assert(status==0);

% Configure LO channel
fprintf("* Acquiring AD9361 Tx lo channel\n")
ad9361TxLOChn = adi.libiio.device.iio_device_find_channel(ad9361PhyDev, 'altvoltage1', true);
loTx = adi.libiio.channel.iio_channel_find_attr(ad9361TxLOChn, 'frequency');
status = adi.libiio.attribute.iio_attr_write_longlong(loTx, TxCfg.LOHz);
assert(status==0);

fprintf("* Initializing AD9361 IIO streaming channels\n");
ad9361Rx0_i = adi.libiio.device.iio_device_find_channel(iioDevRx, 'voltage0', false);
assert(ad9361Rx0_i.isNull==0);
ad9361Rx0_q = adi.libiio.device.iio_device_find_channel(iioDevRx, 'voltage1', false);
assert(ad9361Rx0_q.isNull==0);
ad9361Tx0_i = adi.libiio.device.iio_device_find_channel(iioDevTx, 'voltage0', true);
assert(ad9361Tx0_i.isNull==0);
ad9361Tx0_q = adi.libiio.device.iio_device_find_channel(iioDevTx, 'voltage1', true);
assert(ad9361Tx0_q.isNull==0);
ad9361RxMask = adi.libiio.lowlevel.iio_create_channels_mask(adi.libiio.device.iio_device_get_channels_count(iioDevRx));
assert(ad9361RxMask.isNull==0);
ad9361TxMask = adi.libiio.lowlevel.iio_create_channels_mask(adi.libiio.device.iio_device_get_channels_count(iioDevTx));
assert(ad9361TxMask.isNull==0);

fprintf("* Enabling IIO streaming channels\n");
adi.libiio.channel.iio_channel_enable(ad9361Rx0_i, ad9361RxMask);
adi.libiio.channel.iio_channel_enable(ad9361Rx0_q, ad9361RxMask);
adi.libiio.channel.iio_channel_enable(ad9361Tx0_i, ad9361TxMask);
adi.libiio.channel.iio_channel_enable(ad9361Tx0_q, ad9361TxMask);

fprintf("* Creating non-cyclic IIO buffers with 1 MiS\n");
ad9361RxBuffer = adi.libiio.buffer.iio_device_create_buffer(iioDevRx, 0, ad9361RxMask);
assert(ad9361RxBuffer.isNull==0);
ad9361TxBuffer = adi.libiio.buffer.iio_device_create_buffer(iioDevTx, 0, ad9361TxMask);
assert(ad9361TxBuffer.isNull==0);

adi.libiio.context.iio_context_destroy(iioCtx);
% unloadlibrary('libiio1');