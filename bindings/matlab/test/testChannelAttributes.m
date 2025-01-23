classdef testChannelAttributes < matlab.unittest.TestCase
    properties(TestParameter)
        uri = {'ip:pluto.local'}
        phyDev = {'ad9361-phy'}
        chnParamsLongLong = {'altvoltage0', true, 'frequency', int64(1900000000)}
        chnParamsDouble = {'voltage0', true, 'hardwaregain', double(-12.5)}
    end

    methods(Test)
        function testPlutoChannelAttributes(testCase)
            %% Long Long
            % Read from Channel Attribute
            [status, attrName, result] = iioReadChannelAttributeLonglong(testCase.uri{1}, testCase.phyDev{1}, testCase.chnParamsLongLong{1}, testCase.chnParamsLongLong{2}, testCase.chnParamsLongLong{3});
            assert(strcmp(attrName, testCase.chnParamsLongLong{3}));
            assert(status==0);
            OrigValue = result;

            % Write to Channel Attribute and Verify Value is Written
            status = iioWriteChannelAttributeLonglong(testCase.uri{1}, testCase.phyDev{1}, testCase.chnParamsLongLong{1}, testCase.chnParamsLongLong{2}, testCase.chnParamsLongLong{3}, testCase.chnParamsLongLong{4});
            assert(status==0);
            [status, ~, result] = iioReadChannelAttributeLonglong(testCase.uri{1}, testCase.phyDev{1}, testCase.chnParamsLongLong{1}, testCase.chnParamsLongLong{2}, testCase.chnParamsLongLong{3});
            assert(status==0);
            assert(result==testCase.chnParamsLongLong{4});

            % Write Original Value to Channel Attribute and Verify Value is Written
            status = iioWriteChannelAttributeLonglong(testCase.uri{1}, testCase.phyDev{1}, testCase.chnParamsLongLong{1}, testCase.chnParamsLongLong{2}, testCase.chnParamsLongLong{3}, OrigValue);
            assert(status==0);
            [status, ~, result] = iioReadChannelAttributeLonglong(testCase.uri{1}, testCase.phyDev{1}, testCase.chnParamsLongLong{1}, testCase.chnParamsLongLong{2}, testCase.chnParamsLongLong{3});
            assert(status==0);
            assert(result==OrigValue);

            %% Double
            % Read from Channel Attribute
            [status, chnDataFormatPtr, attrName, result] = iioReadChannelAttributeDouble(testCase.uri{1}, testCase.phyDev{1}, testCase.chnParamsDouble{1}, testCase.chnParamsDouble{2}, testCase.chnParamsDouble{3});
            assert(~chnDataFormatPtr.isNull);
            assert(strcmp(attrName, testCase.chnParamsDouble{3}));
            assert(status==0);
            OrigValue = result;

            % Write to Channel Attribute and Verify Value is Written
            status = iioWriteChannelAttributeDouble(testCase.uri{1}, testCase.phyDev{1}, testCase.chnParamsDouble{1}, testCase.chnParamsDouble{2}, testCase.chnParamsDouble{3}, testCase.chnParamsDouble{4});
            assert(status==0);
            [status, ~, ~, result] = iioReadChannelAttributeDouble(testCase.uri{1}, testCase.phyDev{1}, testCase.chnParamsDouble{1}, testCase.chnParamsDouble{2}, testCase.chnParamsDouble{3});
            assert(status==0);
            assert(result==testCase.chnParamsDouble{4});

            % Write Original Value to Channel Attribute and Verify Value is Written
            status = iioWriteChannelAttributeDouble(testCase.uri{1}, testCase.phyDev{1}, testCase.chnParamsDouble{1}, testCase.chnParamsDouble{2}, testCase.chnParamsDouble{3}, OrigValue);
            assert(status==0);
            [status, ~, ~, result] = iioReadChannelAttributeDouble(testCase.uri{1}, testCase.phyDev{1}, testCase.chnParamsDouble{1}, testCase.chnParamsDouble{2}, testCase.chnParamsDouble{3});
            assert(status==0);
            assert(result==OrigValue);

            % Unload Library
            adi.libiio.helpers.unloadLibIIO();
        end
    end
end