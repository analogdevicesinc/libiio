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
iioCtxParams = libpointer;
iioCtx = adi.libiio.context.iio_create_context(iioCtxParams, uri);
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
% unloadlibrary('libiio1');