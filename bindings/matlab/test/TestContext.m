classdef TestContext < matlab.unittest.TestCase

    properties
        uri = 'ip:pluto.local';
    end

    properties(Hidden)
        emu = [];
    end

    methods(TestClassSetup)
        % Start emulation context
        function startEmulation(testCase)
            %% Start the emulation context
            % checkIIOEMU;
            [folder,~,~] = fileparts(mfilename('fullpath'));
            % folder = strsplit(filepath, filesep);
            % folder = fullfile(folder{end});
            % Load python virtual environment
            pe = getenv('PYTHON_EXE');
            if ~isempty(pe)
                p = pyenv(ExecutionMode="OutOfProcess",Version=pe);
            else
                p = pyenv(ExecutionMode="OutOfProcess");
            end
            disp(p);
            % Start background process
            xmlfile = fullfile(folder, 'pluto.xml');
            testCase.emu = py.pytest_libiio.plugin.iio_emu_manager(xmlfile);
            disp(testCase.emu.uri)
            testCase.emu.start();
            disp('iio-emu started');
            % Wait for the emulation to start
            pause(5);
            % Set uri to be loopback
            testCase.uri = char(testCase.emu.uri);
        end
    end

    methods(TestClassTeardown)
        function shutdownEmulation(testCase)
            testCase.emu.stop();
            disp('Emulation stop')
        end
    end

    methods(Test)
        % Test methods

        function testThunkGen(testCase)
            generateThunkFiles;
        end

        function testCreateContext(testCase)
            ctx = context.iio_create_context([],testCase.uri);
            name = context.iio_context_get_name(ctx);
            disp(name);
            assert(ctx);
        end

    end

end