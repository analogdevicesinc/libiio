classdef testChannelAttributes < matlab.unittest.TestCase
    properties(TestParameter)
        uri = {'ip:pluto.local'}
        phyDev = {'ad9361-phy'}
        chnParamsLongLong = {'altvoltage0', true, 'frequency', int64(1900000000)}
        chnParamsDouble = {'voltage0', true, 'hardwaregain', double(-12.5), uint16(1), 'IIO_VOLTAGE', 'IIO_NO_MOD'}
        chnParamsLogical = {'voltage0', false, 'quadrature_tracking_en', logical(false)}
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

            %% Logical
            % Read from Channel Attribute
            [status, chnDataFormatPtr, attrName, result] = iioReadChannelAttributeBool(testCase.uri{1}, testCase.phyDev{1}, testCase.chnParamsLogical{1}, testCase.chnParamsLogical{2}, testCase.chnParamsLogical{3});
            assert(~chnDataFormatPtr.isNull);
            assert(strcmp(attrName, testCase.chnParamsLogical{3}));
            assert(status==0);
            OrigValue = result;

            % Write to Channel Attribute and Verify Value is Written
            status = iioWriteChannelAttributeBool(testCase.uri{1}, testCase.phyDev{1}, testCase.chnParamsLogical{1}, testCase.chnParamsLogical{2}, testCase.chnParamsLogical{3}, testCase.chnParamsLogical{4});
            assert(status==0);
            [status, ~, ~, result] = iioReadChannelAttributeBool(testCase.uri{1}, testCase.phyDev{1}, testCase.chnParamsLogical{1}, testCase.chnParamsLogical{2}, testCase.chnParamsLogical{3});
            assert(status==0);
            assert(result==testCase.chnParamsLogical{4});

            % Write Original Value to Channel Attribute and Verify Value is Written
            status = iioWriteChannelAttributeBool(testCase.uri{1}, testCase.phyDev{1}, testCase.chnParamsLogical{1}, testCase.chnParamsLogical{2}, testCase.chnParamsLogical{3}, OrigValue);
            assert(status==0);
            [status, ~, ~, result] = iioReadChannelAttributeBool(testCase.uri{1}, testCase.phyDev{1}, testCase.chnParamsLogical{1}, testCase.chnParamsLogical{2}, testCase.chnParamsLogical{3});
            assert(status==0);
            assert(result==OrigValue);

            [PhyDevName, ChnId, ChnType, ModType, IsOutput, IsScanElement, AttrsCount, AttrNameGet, AttrNameFind] = ...
                iioContextGetChannelInfo(testCase.uri{1}, testCase.phyDev{1}, testCase.chnParamsDouble{1}, ...
                testCase.chnParamsDouble{2}, testCase.chnParamsDouble{5});
            assert(strcmp(PhyDevName, testCase.phyDev{1}));
            assert(strcmp(ChnId, testCase.chnParamsDouble{1}));
            assert(strcmp(ChnType, testCase.chnParamsDouble{6}));
            assert(strcmp(ModType, testCase.chnParamsDouble{7}));
            assert(IsOutput==testCase.chnParamsDouble{2});
            assert(~IsScanElement);
            assert(AttrsCount==10);
            assert(strcmp(AttrNameGet, testCase.chnParamsDouble{3}));
            assert(strcmp(AttrNameFind, testCase.chnParamsDouble{3}));
        end
    end
end