clc;
clear;

mfilepath = fileparts(mfilename('fullpath'));
cd(mfilepath);
addpath(strcat("..", filesep, "codegen"));
suite = testsuite("test");
import matlab.unittest.plugins.CodeCoveragePlugin
import matlab.unittest.plugins.codecoverage.CoverageReport
runner = testrunner("textoutput");
reportFormat = CoverageReport("coverageReport");
p = CodeCoveragePlugin.forFolder(strcat("..", filesep, "+adi", filesep, "+libiio"),"Producing",reportFormat);
runner.addPlugin(p);
results = runner.run(suite);
exit(any([results.Failed]));