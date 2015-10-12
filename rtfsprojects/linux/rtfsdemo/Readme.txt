
The rtfsdemo executable was built on Ubuntu desktop Linux for x86 target.

The executable launches into a command shell with familiar commands MKDIR, RMDIR, CAT, DIR, DELTREE, FDISK etc.

It can mount either a formatted file system stored inside a standard host file or a block device file.

If the drive you are experimenting with is accesible as block device file, then type:

    ./rtfsdemo DeviceFileName


For example:

    ./rtfsdemo /dev/sdg

Note: Opens a usb stick on Fedora.


If no arguments are provided Rtfsdemo will open a preformatted volume named Hostdisk_SEGMENT_0.HDK.

A sample of this file is included for experimentation purposes. This is small preformatted 4 MByte volume that
contains several subdirectories that where imported from the Rtfs source tree. These files are also accesible from the
host disk emulator device driver.

Warning: You may open the preformatted 4 MByte with both device drivers at the same time but don;t perform any
writes if you do.

To run the demo on the preformatted volume type the following command and follow the prompts.

The following text is captured from rtfsdemo.

Comments are preceeded by <<.

./rtfsdemo << run the program

<< commanets printed by the program.
Usage: ./rtfsdemo fspath
   <fspath> is not specified.
   <fspath> may be a device file like (./rtfsdemo /dev/sd0) for example.
   Rtfs will try to mount the host disk file. Hostdisk_SEGMENT_0.HDK
   If you meant to do this you should be sure to at least once select
   Install the host disk emulator at C:
   to format a host disk.

=========================================================
..The host disk driver simulates a disk using files.
..When it is first executed it asks how large a disk you want.
..After a simulated disk is initialized the first time you must
..Run the commands FDISK C: and FORMAT (or EXFATFORMAT)
..to format the volume before using it

Install the host disk emulator at C: ? (Y/N) N     << Type N to not use the file based emulator
Install the host raw device driver a P: ? (Y/N) Y  << Type Y to open the device file driver pointing to the emulator file.

 Mounting RAW DEVICE DRIVER on P:

Opening Hostdisk_SEGMENT_0.HDK
Open returned 3
Sizing
Sizing bites == 4194304
Sizing blocks == 8192
Insert linux mount point
Calling test shell
Press Return

RNDOP                                 | READ
SEEK                                  | WRITE
CLOSE                                 | LSTOPEN
---- Test and Miscelaneous Operations ------
OPENSPEED                             | REGRESSTEST D:
TESTNAND D:                           | QUIT
VERBOSE Y/N
---- Drive and System Operations ------
DSKSEL                                | DSKCLOSE
DSKFLUSH                              | DEVINFO
SHOWDISKSTATS                         | FORMAT
FDISK                                 | DEVICEFORMAT
DUMPMBR                               | DUMPBPB
HACKWIN7 Toggle Clobber/restore MBR   | CHKDSK
EJECT (simulate a removal event)      | RESET (reinitialize RTFS)
---- Utility Operations ------
CD PATH or CD to display PWD          | DIR
ENUMDIR                               | STAT
GETATTR                               | GETVOL
SETATTR
SETVOL D: VOLUME (use XXXXXXXX.YYY form)
MKDIR                                 | RMDIR
DELTREE                               | DELETE
Press return
Press return
RENAME                                | CHSIZE
FILLFILE                              | COPY
DIFF                                  | CAT
SHOWEXTENTS                           | LOOPFILE
LOOPDIR                               | LOOPFIX
ERRNO                                 | BREAK (to debugger)

<< Experiment with directories imported from the Rtfs source tree.
<< Type DIR, CAT, CD, MKDIR etc.
<< Type QUIT to exit.

CMD> DIR
MAKEAL~1.BAT        648       03-09-14 13:48 -  makeallreleases.bat
EXFAT   .             0 <DIR> 03-09-14 13:48 -  exfat
APPS    .             0 <DIR> 03-09-14 13:48 -  apps
RTFSPR~1.             0 <DIR> 03-09-14 13:48 -  rtfsproplus
RTFSPR~2.             0 <DIR> 03-09-14 13:48 -  rtfsproplusdvr
       5 File(s) 4458 Blocks Free
CMD>





