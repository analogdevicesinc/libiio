classdef testDeviceAttributes < matlab.unittest.TestCase
    properties(TestParameter)
        uri = {'ip:pluto.local'}
        phyDev = {'ad9361-phy'}
        devAttrLL = {'xo_correction'}
        devAttrRaw = {'trx_rate_governor'}
        value = {int64(40000005)}
        chnParams = {'altvoltage0', true, 'frequency', int64(1900000000)}
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

            %% Raw
            % Read from Device Attribute
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

            [PhyDevName, DevId] = ...
                iioContextGetDeviceInfo(testCase.uri{1}, testCase.phyDev{1});
            assert(strcmp(PhyDevName, testCase.phyDev{1}));

            % Unload Library
            adi.libiio.helpers.unloadLibIIO();
        end
    end
end