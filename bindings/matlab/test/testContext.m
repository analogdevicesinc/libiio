classdef testContext < matlab.unittest.TestCase
    properties(TestParameter)
        uri = {'ip:pluto.local'}
    end

    methods(Test)
        function testPlutoContextGetVersionInfo(testCase)
            [status, major, minor, vtag, ...
                xml, name, descr, deviceCount] = iioContextGetVersionInfo(testCase.uri{1});
            assert(status==0);
            assert(major~=-1);
            assert(minor~=-1);
            assert(~strcmp(vtag,'v0.00'));
            assert(~strcmp(xml,'xyz'));
            assert(~strcmp(name,'zz'));
            assert(~strcmp(descr,'abc'));
            assert(deviceCount~=-1);

            % Unload Library
            adi.libiio.helpers.unloadLibIIO();
        end
    end
end