function [PhyDevName, DevId, ChnsCount, DevAttrsCount, ...
    AttrNameAtIndex, ChnNameOut] = ...
    iioContextGetDeviceInfo(uri, phyDevName, ...
    AttrIndex, ChnNameIn, IsOutput)
    
    assert(isa(uri,'char') && all(size(uri) <= [1,20]));
    assert(isa(phyDevName,'char') && all(size(phyDevName) <= [1,50]));   

    % Get Context
    iioCtxPtr = adi.libiio.context.iio_create_context(uri);
    status = -int32(iioCtxPtr==coder.opaque('struct iio_context*', 'NULL'));
    if status ~= 0
        PhyDevName = uint8('xyz');
        DevId = uint8('abc');
        ChnsCount = 0;
        DevAttrsCount = 0;
        return;
    end

    % Get PhyDev Pointer
    iioPhyDevPtr = adi.libiio.context.iio_context_find_device(iioCtxPtr, phyDevName);

    % Get PhyDev Name
    PhyDevName = adi.libiio.device.iio_device_get_name(iioPhyDevPtr);

    % Get Device Id Associated with the PhyDev Pointer
    DevId = adi.libiio.device.iio_device_get_id(iioPhyDevPtr);

    % Get Device Label Associated with the PhyDev Pointer
    DevLabel = adi.libiio.device.iio_device_get_label(iioPhyDevPtr);
    
    % Get Channels Count Associated with the PhyDev Pointer
    ChnsCount = adi.libiio.device.iio_device_get_channels_count(iioPhyDevPtr);
    
    % Get Device Attributes Count Associated with the PhyDev Pointer
    DevAttrsCount = adi.libiio.device.iio_device_get_attrs_count(iioPhyDevPtr);

    % Get Attribute Pointer Associated with the PhyDev Pointer at Index
    iioAttrPtrFromPhyDevPtr = adi.libiio.device.iio_device_get_attr(iioPhyDevPtr, AttrIndex);
    AttrNameAtIndex = adi.libiio.attribute.iio_attr_get_name(iioAttrPtrFromPhyDevPtr);

    % Get Channel Pointer Associated with the PhyDev Pointer Given Name
    iioChnPtrFromPhyDevPtr = adi.libiio.device.iio_device_find_channel(iioPhyDevPtr, ChnNameIn, IsOutput);
    ChnNameOut = adi.libiio.channel.iio_channel_get_name(iioChnPtrFromPhyDevPtr);

    % Destroy Context
    adi.libiio.context.iio_context_destroy(iioCtxPtr);
end