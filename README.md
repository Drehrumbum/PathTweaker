# PathTweaker
PathTweaker, a small tool for tweaking the recording paths of QIRX-SDR

PathTweaker is primarily intended for the use with external but physically attached drives on your laptop, where you may collect your raw- and audio-recordings taken with QIRX. If this tool runs beside of QIRX, it can “tweak” the paths “rawOut”, “DABAudio” (this path is also used for AM/FM audio recordings) and the path “TiiLogger” inside of QIRX’s configuration-file. 

This means, for example, if you have connected your external “My RAW-Recordings-Collection 3” drive, PathTweaker notices that, tweaks the path in QIRX’s configuration-file for you automatically and the next raw-recordings will go to this drive (and the path you’ve selected there). If you want this. If not, you can switch to this drive with one click, as long as the drive is connected. The same works with audio recordings and – with a little limitation – also with the text files for the Tii-Logger. In principle, you can select and manage three different drives with PathTweaker. 

To make the long story short: With PathTweaker it’s no longer necessary to write on the system-drive at first and to copy these large files to another drive later, or to change QIRX’s configuration-file manually for targeting another drive.
