function status = iioWriteChannelAttributeDouble(uri, ...
    phyDevName, ...
    chnName, ...
    isOutput, ...
    chnAttrName, ...
    value)

    assert(isa(uri,'char') && all(size(uri) <= [1,20]));
    assert(isa(phyDevName,'char') && all(size(phyDevName) <= [1,50]));
    assert(isa(chnName,'char') && all(size(chnName) <= [1,50]));
    assert(isa(isOutput,'logical') && all(size(isOutput) <= [1,1]));
    assert(isa(chnAttrName,'char') && all(size(chnAttrName) <= [1,50]));
    assert(isa(value,'double') && isreal(value) && all(size(value) == [1,1]));

    % Get Context
    iioCtxPtr = adi.libiio.context.iio_create_context(uri);
    status = -int32(iioCtxPtr==coder.opaque('struct iio_context*', 'NULL'));
    if status ~= 0
        return;
    end

    % Get PhyDev Pointer
    iioPhyDevPtr = adi.libiio.context.iio_context_find_device(iioCtxPtr, phyDevName);

    % Get Channel Pointer
    iioPhyDevChnPtr = adi.libiio.device.iio_device_find_channel(iioPhyDevPtr, chnName, isOutput);

    % Get Channel Attribute Pointer
    iioPhyDevChnAttrPtr = adi.libiio.channel.iio_channel_find_attr(iioPhyDevChnPtr, chnAttrName);

    % Write Attribute
    status = adi.libiio.attribute.iio_attr_write_double(iioPhyDevChnAttrPtr, value);

    % Destroy Context
    adi.libiio.context.iio_context_destroy(iioCtxPtr);
end