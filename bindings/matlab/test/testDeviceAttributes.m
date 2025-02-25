classdef testDeviceAttributes < matlab.unittest.TestCase
    properties(TestParameter)
        uri = {'ip:pluto.local'}
        phyDev = {'ad9361-phy'}
        deviceId = {'iio:device0'}
        AD9361PHYChnsCount = {9};
        AD9361PHYDevAttrsCount = {18};
        devAttr = {'dcxo_tune_coarse', 2};
        devAttrLL = {'xo_correction'}
        devAttrRaw = {'trx_rate_governor'}
        value = {int64(40000005)}
        chnParams = {'altvoltage0', true, 'frequency', int64(1900000000), 'RX_LO'}
    end

    methods(Test)
        function testPlutoDeviceAttributes(testCase)
            %% Long Long
            % Read from Device Attribute
            [status, attrName, result] = iioReadDeviceAttributeLonglong(testCase.uri{1}, testCase.phyDev{1}, testCase.devAttrLL{1});
            assert(strcmp(attrName, testCase.devAttrLL{1}));
            assert(status==0);
            OrigValue = result;

            % Write to Device Attribute and Verify Value is Written
            status = iioWriteDeviceAttributeLonglong(testCase.uri{1}, testCase.phyDev{1}, testCase.devAttrLL{1}, testCase.value{1});
            assert(status==0);
            [status, ~, result] = iioReadDeviceAttributeLonglong(testCase.uri{1}, testCase.phyDev{1}, testCase.devAttrLL{1});
            assert(status==0);
            assert(result==testCase.value{1});

            % Write Original Value to Device Attribute and Verify Value is Written
            status = iioWriteDeviceAttributeLonglong(testCase.uri{1}, testCase.phyDev{1}, testCase.devAttrLL{1}, OrigValue);
            assert(status==0);
            [status, ~, result] = iioReadDeviceAttributeLonglong(testCase.uri{1}, testCase.phyDev{1}, testCase.devAttrLL{1});
            assert(status==0);
            assert(result==OrigValue);

            % %% Raw
            % % Read from Device Attribute
            % [status, attrName, result] = iioReadDeviceAttributeRaw(testCase.uri{1}, testCase.phyDev{1}, testCase.devAttrRaw{1});
            % assert(strcmp(attrName, testCase.devAttrRaw{1}));
            % assert(status==0);
            % OrigValue = result;
            % 
            % % Write to Device Attribute and Verify Value is Written
            % status = iioWriteDeviceAttributeRaw(testCase.uri{1}, testCase.phyDev{1}, testCase.devAttrRaw{1}, testCase.value{1});
            % assert(status==0);
            % [status, attrName, result] = iioReadDeviceAttributeRaw(testCase.uri{1}, testCase.phyDev{1}, testCase.devAttrRaw{1});
            % assert(status==0);
            % assert(result==testCase.value{1});
            % 
            % % Write Original Value to Device Attribute and Verify Value is Written
            % status = iioWriteDeviceAttributeRaw(testCase.uri{1}, testCase.phyDev{1}, testCase.devAttrRaw{1}, OrigValue);
            % assert(status==0);
            % [status, attrName, result] = iioReadDeviceAttributeRaw(testCase.uri{1}, testCase.phyDev{1}, testCase.devAttrRaw{1});
            % assert(status==0);
            % assert(result==OrigValue);

            [PhyDevName, DevId, ChnsCount, DevAttrsCount, ...
                AttrNameAtIndex, ChnNameOut] = iioContextGetDeviceInfo(...
                testCase.uri{1}, testCase.phyDev{1}, testCase.devAttr{2}, testCase.chnParams{1}, testCase.chnParams{2});
            assert(strcmp(PhyDevName, testCase.phyDev{1}));
            assert(strcmp(DevId, testCase.deviceId{1}));
            assert(ChnsCount==testCase.AD9361PHYChnsCount{1});
            assert(DevAttrsCount==testCase.AD9361PHYDevAttrsCount{1});
            assert(strcmp(AttrNameAtIndex, testCase.devAttr{1}));
            assert(strcmp(ChnNameOut, testCase.chnParams{5}));

            % Unload Library
            adi.libiio.helpers.unloadLibIIO();
        end
    end
end