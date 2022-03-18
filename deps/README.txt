Commits of the Libiio repository must *not* touch any file or directory
present in the deps/ folder.

The only commits allowed, are the one that are automatically created
when adding a new dependency with:
git subrepo clone https://... deps/new-dependency

And the ones created when updating the external repository to the latest
available with:
git subrepo pull deps/my-dependency


git-subrepo is an external tool that can be obtained here:
https://github.com/ingydotnet/git-subrepo
