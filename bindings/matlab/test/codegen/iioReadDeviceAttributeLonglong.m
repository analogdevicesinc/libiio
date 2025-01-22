function [status, attrName, value] = iioReadDeviceAttributeLonglong(uri, phyDevName, devAttrName)
    assert(isa(uri,'char') && all(size(uri) <= [1,20]));
    assert(isa(phyDevName,'char') && all(size(phyDevName) <= [1,20]));
    assert(isa(devAttrName,'char') && all(size(devAttrName) <= [1,20]));

    % Get Context
    iioCtxPtr = adi.libiio.context.iio_create_context(uri);
    status = -int32(iioCtxPtr==coder.opaque('struct iio_context*', 'NULL'));
    if status ~= 0
        value = int64(0);
        attrName = char(zeros(1,1,'uint8'));
        return;
    end

    % Get PhyDev Pointer
    iioPhyDevPtr = adi.libiio.context.iio_context_find_device(iioCtxPtr, phyDevName);

    % Get Attribute Pointer
    iioPhyDevAttrPtr = adi.libiio.device.iio_device_find_attr(iioPhyDevPtr, devAttrName);

    % get Attribute Name
    attrName = adi.libiio.attribute.iio_attr_get_name(iioPhyDevAttrPtr);

    % Read Attribute
    [status, value] = adi.libiio.attribute.iio_attr_read_longlong(iioPhyDevAttrPtr);

    % Destroy Context
    adi.libiio.context.iio_context_destroy(iioCtxPtr);
end