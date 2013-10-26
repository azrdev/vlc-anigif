# vlc-anigif

[vlc](https://www.videolan.org/vlc/) encoder plugin creating animated gif.
Requires [giflib](http://giflib.sourceforge.net/) >= v5.

## Trying it out

Copy the compiled libanigif_plugin.so to /usr/lib/vlc/plugins/codec/ and use it as shown in test.sh. This is only a codec, you have to tell vlc what to do with the encoded data - e.g. dump it (as it is) to a file, or serve it via HTTP.

