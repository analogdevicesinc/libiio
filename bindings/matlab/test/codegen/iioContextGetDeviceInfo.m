function [PhyDevName, DevId] = ...
    iioContextGetDeviceInfo(uri, phyDevName)
    
    assert(isa(uri,'char') && all(size(uri) <= [1,20]));
    assert(isa(phyDevName,'char') && all(size(phyDevName) <= [1,50]));   

    % Get Context
    iioCtxPtr = adi.libiio.context.iio_create_context(uri);
    status = -int32(iioCtxPtr==coder.opaque('struct iio_context*', 'NULL'));
    if status ~= 0
        PhyDevName = uint8('xyz');
        DevId = uint8('abc');
        return;
    end

    % Get PhyDev Pointer
    iioPhyDevPtr = adi.libiio.context.iio_context_find_device(iioCtxPtr, phyDevName);

    % Get PhyDev Name
    PhyDevName = adi.libiio.device.iio_device_get_name(iioPhyDevPtr);

    % Get Device Id Associated with the PhyDev Pointer
    DevId = adi.libiio.device.iio_device_get_id(iioPhyDevPtr);

    % Destroy Context
    adi.libiio.context.iio_context_destroy(iioCtxPtr);
end