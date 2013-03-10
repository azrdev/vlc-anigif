#!/bin/sh
[ -r "$1" ] || exit 1
vlc -I dummy --reset-plugins-cache --codec anigif -vvv "$1" --sout "#transcode{venc=anigif,acodec=none}:standard{mux=raw,dst=$1.gif,access=file}" vlc://quit

