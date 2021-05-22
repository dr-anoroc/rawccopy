[![Build & Publish](https://github.com/spectatorBH/rawccopy/actions/workflows/msbuild.yml/badge.svg)](https://github.com/spectatorBH/rawccopy/actions/workflows/msbuild.yml)

# rawccopy

Rawccopy is a one-to-one port of [rawcopy](https://github.com/jschicht/RawCopy) to C. The command line is backwards compatible with the original rawcopy (with some clarifications below) and the behaviour of the two programs is virtually identical.

#### Why the trouble of rewriting the whole thing?

##### Original rawccopy

For some reason, rawcopy started generating false positives in virus scanners and this became very annoying. In the beginning the problem could be circumvented by locally compiling it, but in the end even that didn't work. That resulted in a first version, which stayed close to the original rawcopy source code, which is written in [AutoIt3](https://www.autoitscript.com/site/).

##### Current version

Some of the bugs that were reported on Github revealed fundamental flaws in the extraction logic. At first, it looked like errors made while porting the code, but in reality they also existed in the original rawcopy. That led to a complete refactoring and simplification of the extraction logic while replacing the full rawcopy processing flow by a cleaner, streamlined version. At the same time, the code was refactored and the command line processing was clarified.

#### Differences with original rawcopy

##### Overall behaviour

The command line is exactly the same as documented [here](https://github.com/jschicht/RawCopy) and the specifications of the programs are almost identical. The differences are:

* rawccopy supports links, ie it is able to follow links such as `c:\Users\All Users` -> `c:\ProgramData`
* rawccopy correctly supports compressed files, where rawcopy, doesn't handle all of them correctly;

##### Some clarifications about the command line

There are some points to note about the rawccopy (and rawcopy) command line:

* When using a device or hard disk specification in the `/FileNamePath:` parameter, ie `/FileNamePath:HardDisk1Partition3`,  `/FileNamePath:HardDisk1Partition3`, `/FileNamePath:HarddiskVolume1`, `/FileNamePath:HarddiskVolumeShadowCopy1` or `/FileNamePath:PhysicalDrive1`, rawcopy imposes these indexes to be maximum one digit long, eg `/FileNamePath:HarddiskVolume1` is OK but not`/FileNamePath:HarddiskVolume11`.

  Windows does not limit hard disk, volume and partition indexes to one digit, therefore rawcopy's **behaviour is not correct** and rawccopy supports indexes of more than one digit.

* When using a file name specification like `/FileNamePath:PhysicalDrive1`, rawcopy actually needs to be told in which partition to look. Contrary to what is documented [here](https://github.com/jschicht/RawCopy), it does this through the `/ImageVolume:` parameter. For rawccopy the situation is exactly the same.

* As usual in the Windows universe, all aspects of the command line are case insensitive.

#### Build instructions

Download the source code and install Microsoft Visual Studio 2019. The community version is OK. Then simply build the version you need (x64 or x86).

If you have GUI-phobia, you can always type:

 ``msbuild rawccopy.sln /p:Configuration=release /p:Platform=x64`` for a 64 bit build and

 ``msbuild rawccopy.sln /p:Configuration=release /p:Platform=x86`` for a 32 bit build.

`msbuild` is  not in your `$PATH` variable, so you will need to locate it in one of the Visual Studio folders.

#### Supported Environments

Rawccopy should work on Windows XP and higher, both 32 and 64 bit. Like rawcopy, it needs to be run with administrator privileges and can only work on NTFS volumes.
