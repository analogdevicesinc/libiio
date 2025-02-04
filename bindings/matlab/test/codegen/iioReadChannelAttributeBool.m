function [status, chnDataFormatPtr, ...
    attrName, value] = iioReadChannelAttributeBool(...
    uri, ...
    phyDevName, ...
    chnName, ...
    isOutput, ...
    chnAttrName)

    assert(isa(uri,'char') && all(size(uri) <= [1,20]));
    assert(isa(phyDevName,'char') && all(size(phyDevName) <= [1,50]));
    assert(isa(chnName,'char') && all(size(chnName) <= [1,50]));
    assert(isa(isOutput,'logical') && all(size(isOutput) <= [1,1]));
    assert(isa(chnAttrName,'char') && all(size(chnAttrName) <= [1,50]));

    % Get Context
    iioCtxPtr = adi.libiio.context.iio_create_context(uri);
    status = -int32(iioCtxPtr==coder.opaque('struct iio_context*', 'NULL'));
    if status ~= 0
        chnDataFormatPtr = libpointer;
        attrName = char(zeros(1,1,'uint8'));
        value = bool(0);
        return;
    end

    % Get PhyDev Pointer
    iioPhyDevPtr = adi.libiio.context.iio_context_find_device(iioCtxPtr, phyDevName);

    % Get Channel Pointer
    iioPhyDevChnPtr = adi.libiio.device.iio_device_find_channel(iioPhyDevPtr, chnName, isOutput);

    % Get Channel Pointer
    chnDataFormatPtr = adi.libiio.lowlevel.iio_channel_get_data_format(iioPhyDevChnPtr);

    % Get Channel Attribute Pointer
    iioPhyDevChnAttrPtr = adi.libiio.channel.iio_channel_find_attr(iioPhyDevChnPtr, chnAttrName);

    % get Attribute Name
    attrName = adi.libiio.attribute.iio_attr_get_name(iioPhyDevChnAttrPtr);

    % Read Attribute
    [status, value] = adi.libiio.attribute.iio_attr_read_bool(iioPhyDevChnAttrPtr);

    % Destroy Context
    adi.libiio.context.iio_context_destroy(iioCtxPtr);
end