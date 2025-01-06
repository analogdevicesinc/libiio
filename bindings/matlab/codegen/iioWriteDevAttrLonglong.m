function status = iioWriteDevAttrLonglong(uri, phyDevName, devAttrName, value)
    assert(isa(uri,'char') && all(size(uri) <= [1,20]));
    assert(isa(phyDevName,'char') && all(size(phyDevName) <= [1,20]));
    assert(isa(devAttrName,'char') && all(size(devAttrName) <= [1,20]));
    assert(isa(value,'int64') && isreal(value) && all(size(value) == [1,1]));

    % Get Context
    iioCtxPtr = adi.libiio.context.iio_create_context(uri);
    status = -int32(iioCtxPtr==coder.opaque('struct iio_context*', 'NULL'));
    if status ~= 0
        return;
    end

    % Get PhyDev Pointer
    iioPhyDevPtr = adi.libiio.context.iio_context_find_device(iioCtxPtr, phyDevName);

    % Get Attribute Pointer
    iioPhyDevAttrPtr = adi.libiio.device.iio_device_find_attr(iioPhyDevPtr, devAttrName);

    % Write Attribute
    status = adi.libiio.attribute.iio_attr_write_longlong(iioPhyDevAttrPtr, value);

    % Destroy Context
    adi.libiio.context.iio_context_destroy(iioCtxPtr);
end