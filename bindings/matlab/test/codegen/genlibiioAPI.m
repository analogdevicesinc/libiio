tic;
functionNames = {...
    'iioReadChannelAttributeDouble', ...
    'iioWriteChannelAttributeDouble', ...
    'iioReadDeviceAttributeLonglong', ...
    'iioWriteDeviceAttributeLonglong', ...
    'iioReadChannelAttributeLonglong', ...
    'iioWriteChannelAttributeLonglong', ...
    'iioReadChannelAttributeBool', ...
    'iioWriteChannelAttributeBool', ...
    'iioContextGetVersionInfo'...
    };
cfg = coder.config('dll');
cfg.TargetLang = 'C';
cfg.FilePartitionMethod = 'SingleFile';
cfg.RuntimeChecks = true;
cfg.GenCodeOnly = true;
cfg.EnableAutoExtrinsicCalls = false;
cfg.MATLABSourceComments = true;
% cfg.GenerateReport = true;
% cfg.LaunchReport = true;
cfg.HighlightPotentialDataTypeIssues = true;

outputLIBName = 'mlibiio';
cfg.HardwareImplementation.TargetHWDeviceType = 'Intel->x86-64 (Linux 64)'; %'Generic->32-bit Embedded Processor';
result = codegen('-config','cfg',functionNames{:},'-O ','disable:openmp','-o',outputLIBName);
% result = codegen('-config','cfg',functionNames{:},...
% '-args',coder.opaque('const struct iio_context_params *'),...
% '-O ','disable:openmp','-o',outputLIBName);
assert(result.summary.passed)
toc;