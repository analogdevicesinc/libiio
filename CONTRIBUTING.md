# Contributing to the libIIO

When contributing to this repository, please first discuss the change you wish to make via the 
[issue tracker](https://github.com/analogdevicesinc/libiio/issues) before making a pull request. 

Please note we have a code of conduct, please follow it in all your interactions with the project.

The [libIIO repository](https://github.com/analogdevicesinc/libiio) is a aggregate of a library 
and separate applications/programs/examples and doc which use that library:
* the libiio library (which is released and distributed under the LGPL 2.0 or greater license) and 
* examples and test code (which is released and distributed under the GPL 2.1 or greater license).
* groff source for man pages are distributed under the GPL 2.1 or greater.

Any pull requests will be covered by one of these licenses.

## Pull Request Checklist

1. Commit message includes a "Signed-off-by: [name] < email >" to the commit message. 
   This ensures you have the rights to submit your code, by agreeing to the 
   [Developer Certificate of Origin](https://developercertificate.org/). If you can not agree to
   the DCO, don't submit a pull request, as we can not accept it.
2. Commit should be "atomic", ie : should do one thing only. A pull requests should only contain 
   multiple commits if that is required to fix the bug or implement the feature.
3. Commits should have good commit messages. Check out [The git Book](https://git-scm.com/book/en/v2/Distributed-Git-Contributing-to-a-Project)
   for some pointers, and tools to use.
4. The project must build and run on MacOS, Windows, and Linux. This is checked on every pull request by the 
   continuous integration system, things that fail to build can not be merged.

## Pull Request Process

1. Make a fork, if you are not sure on how to make a fork, check out [GitHub help](https://help.github.com/en/github/getting-started-with-github/fork-a-repo)
2. Make a Pull Request, if you are not sure on how to make a pull request, check out [GitHub help](https://help.github.com/en/github/collaborating-with-issues-and-pull-requests/creating-a-pull-request-from-a-fork)
3. Before a Pull Request can be merged, it must be reviewd by at least one reviewer, and tested on as
   many different IIO devices as possible. If you have tested it, you can indicated that in your commit
   message.

