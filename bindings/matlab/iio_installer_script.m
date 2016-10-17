function installer_script(varargin)
	if nargin > 0
		install = varargin{1}; % use the command line arguement
	else
		install = true; % assume install
	end
	thisDir = fileparts(mfilename('fullpath')); % path to this script

	if install
		pathfunc = @addpath; % add paths for installation
	else
		pathfunc = @rmpath; % remove paths for uninstall
	end

	pathfunc(thisDir);
	savepath;
end
