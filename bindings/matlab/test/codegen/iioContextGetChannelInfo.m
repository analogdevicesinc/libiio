function [PhyDevName, ChnId, ChnType, ModType, IsOutput, ...
    IsScanElement, AttrsCount, AttrFileName, AttrNameGet, AttrNameFind] = ...
    iioContextGetChannelInfo(uri, phyDevName, ...
    chnName, isOutput, AttrIndex)

    assert(isa(uri,'char') && all(size(uri) <= [1,20]));
    assert(isa(phyDevName,'char') && all(size(phyDevName) <= [1,50]));
    assert(isa(chnName,'char') && all(size(chnName) <= [1,50]));
    assert(isa(isOutput,'logical') && all(size(isOutput) <= [1,1]));
    assert(isa(AttrIndex,'uint16') && all(size(AttrIndex) <= [1,1]));    

    % Get Context
    iioCtxPtr = adi.libiio.context.iio_create_context(uri);
    status = -int32(iioCtxPtr==coder.opaque('struct iio_context*', 'NULL'));
    if status ~= 0
        PhyDevName = uint8('xyz');
        ChnId = uint8('abc');
        ChnType = uint8('IIO_XYZ');
        ModType = uint8('IIO_MOD_ABC');
        IsOutput = true;
        IsScanElement = true;
        AttrsCount = 0;
        AttrFileName = uint8('ijk');
        AttrNameGet = uint8('uvw');
        AttrNameFind = uint8('pqr');
        return;
    end

    % Get PhyDev Pointer
    iioPhyDevPtr = adi.libiio.context.iio_context_find_device(iioCtxPtr, phyDevName);

    % Get Channel Pointer
    iioPhyDevChnPtr = adi.libiio.device.iio_device_find_channel(iioPhyDevPtr, chnName, isOutput);

    % Get PhyDev Name Associated with the Channel Pointer
    iioPhyDevPtrFromChnPtr = adi.libiio.channel.iio_channel_get_device(iioPhyDevChnPtr);
    PhyDevName = adi.libiio.device.iio_device_get_name(iioPhyDevPtrFromChnPtr);

    % Get Channel Id Associated with the Channel Pointer
    ChnId = adi.libiio.channel.iio_channel_get_id(iioPhyDevChnPtr);

    % Get Channel Type Associated with the Channel Pointer
    ChnType = adi.libiio.channel.iio_channel_get_type(iioPhyDevChnPtr);

    % Get Modifier Type Associated with the Channel Pointer
    ModType = adi.libiio.channel.iio_channel_get_modifier(iioPhyDevChnPtr);

    % Is Channel Associated with the Channel Pointer an Output Channel?
    IsOutput = adi.libiio.channel.iio_channel_is_output(iioPhyDevChnPtr);

    % Is Channel Associated with the Channel Pointer a Scan Element?
    IsScanElement = adi.libiio.channel.iio_channel_is_scan_element(iioPhyDevChnPtr);

    % Get Channel Attributes Count Associated with the Channel Pointer
    AttrsCount = adi.libiio.channel.iio_channel_get_attrs_count(iioPhyDevChnPtr);

    % Get Attribute Pointer Associated with the Channel Pointer at Index
    AttrPtrGet = adi.libiio.channel.iio_channel_get_attr(iioPhyDevChnPtr, AttrIndex);
    AttrFileName = adi.libiio.attribute.iio_attr_get_filename(AttrPtrGet);
    AttrNameGet = adi.libiio.attribute.iio_attr_get_name(AttrPtrGet);

    % Get Attribute Pointer Associated with the Channel Pointer Given Name
    AttrPtrFind = adi.libiio.channel.iio_channel_find_attr(iioPhyDevChnPtr, AttrNameGet);
    AttrNameFind = adi.libiio.attribute.iio_attr_get_name(AttrPtrFind);

    % Destroy Context
    adi.libiio.context.iio_context_destroy(iioCtxPtr);
end