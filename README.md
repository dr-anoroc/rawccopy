# rawccopy

Rawccopy is a one-to-one port of [rawcopy](https://github.com/jschicht/RawCopy) to C. The command line is exactly the same as the original rawcopy and the behaviour of the two programs is virtually identical.

#### Why the trouble of rewriting the whole thing?

For some reason, rawcopy started generating false positives in virus scanners and this became very annoying. In the beginning the problem could be circumvented by locally compiling it, but in the end even that didn't work.

#### Differences with original rawcopy

The command line is exactly the same as documented [here](https://github.com/jschicht/RawCopy) and the specification of the programs are almost identical. The differences are:

* rawccopy supports links, ie it is able to follow links such as `c:\Users\All Users` -> `c:\ProgramData`
* rawccopy correctly supports compressed files, where rawcopy, doesn't handle all of them correctly;

#### Build instructions

Download the source code and install Microsoft Visual Studio 2019. The community version is OK. Then simply build the version you need (x64 or x86).

If you have GUI-phobia, you can always type:

 ``msbuild rawccopy.sln /p:Configuration=release /p:Platform=x64`` for a 64 bit build and

 ``msbuild rawccopy.sln /p:Configuration=release /p:Platform=x86`` for a 32 bit build.

`msbuild` is  not in your `$PATH` variable, so you will need to locate it in one of the Visual Studio folders.

#### Supported Environments

Rawccopy should work on Windows XP and higher, both 32 and 64 bit. Like rawcopy, it needs to be run with administrator privileges and can only work on NTFS volumes.
