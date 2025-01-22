function [status, major, minor, vtag, ...
    xml, name, descr, deviceCount] = iioContextGetVersionInfo(uri)
    assert(isa(uri,'char') && all(size(uri) <= [1,20]));

    % Get Context
    iioCtxPtr = adi.libiio.context.iio_create_context(uri);
    status = -int32(iioCtxPtr==coder.opaque('struct iio_context*', 'NULL'));
    if status ~= 0
        major = double(-1);
        minor = double(-1);
        vtag = uint8('v0.00');
        xml = uint8('xyz');
        name = uint8('zz');
        descr = uint8('abc');
        deviceCount = double(-1);
        return;
    end

    % Get Context Version Major
    major = adi.libiio.context.iio_context_get_version_major(iioCtxPtr);
    
    % Get Context Version Minor
    minor = adi.libiio.context.iio_context_get_version_minor(iioCtxPtr);

    % Get Context Version Tag
    vtag = adi.libiio.context.iio_context_get_version_tag(iioCtxPtr);

    % Get Context XML representation
    xml = adi.libiio.context.iio_context_get_xml(iioCtxPtr);

    % Get Context Name
    name = adi.libiio.context.iio_context_get_name(iioCtxPtr);
    
    % Get Context Description
    descr = adi.libiio.context.iio_context_get_description(iioCtxPtr);
    
    % Get Context Devices Count    
    deviceCount = adi.libiio.context.iio_context_get_devices_count(iioCtxPtr);

    % Destroy Context
    adi.libiio.context.iio_context_destroy(iioCtxPtr);
end