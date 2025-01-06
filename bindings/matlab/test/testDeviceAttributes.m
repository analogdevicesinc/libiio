classdef testDeviceAttributes < matlab.unittest.TestCase
    properties(TestParameter)
        uri = {'ip:pluto.local'}
        phyDev = {'ad9361-phy'}
        devAttr = {'xo_correction'}
        value = {int64(40000005)}
    end

    methods(Test)
        function testPlutoDeviceAttributes(testCase)
            % Read from Device Attribute
            [status, attrName, result] = iioReadDevAttrLonglong(testCase.uri{1}, testCase.phyDev{1}, testCase.devAttr{1});
            assert(strcmp(attrName, testCase.devAttr{1}));
            assert(status==0);
            OrigValue = result;

            % Write to Device Attribute and Verify Value is Written
            status = iioWriteDevAttrLonglong(testCase.uri{1}, testCase.phyDev{1}, testCase.devAttr{1}, testCase.value{1});
            assert(status==0);
            [status, attrName, result] = iioReadDevAttrLonglong(testCase.uri{1}, testCase.phyDev{1}, testCase.devAttr{1});
            assert(status==0);
            assert(result==testCase.value{1});

            % Write Original Value to Device Attribute and Verify Value is Written
            status = iioWriteDevAttrLonglong(testCase.uri{1}, testCase.phyDev{1}, testCase.devAttr{1}, OrigValue);
            assert(status==0);
            [status, attrName, result] = iioReadDevAttrLonglong(testCase.uri{1}, testCase.phyDev{1}, testCase.devAttr{1});
            assert(status==0);
            assert(result==OrigValue);

            % Unload Library
            adi.libiio.helpers.unloadLibIIO();
        end
    end
end