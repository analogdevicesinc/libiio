% Verify iio-emu executable is in the path
[status, r] = system('iio-emu -l');
% Check error code
assert(~status, 'iio-emu executable not found in the path');