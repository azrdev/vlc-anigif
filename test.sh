#!/bin/sh
[ -r "$1" ] || exit 1
input=v4l2://
#vlc -I dummy --reset-plugins-cache --codec anigif -vvv "$input" --sout "#transcode{width=320,venc=anigif,acodec=none}:standard{mux=raw,dst=$1.gif,access=file}" vlc://quit
#vlc -I dummy --reset-plugins-cache --codec anigif -vvv "$input" --sout "#transcode{width=320,venc=anigif,acodec=none}:standard{mux=raw,access=http,dst=:8080}" vlc://quit
vlc -I dummy --reset-plugins-cache --codec anigif -vvv v4l2:// --sout-http-mime image/gif --sout "#transcode{venc=anigif,acodec=none}:standard{mux=raw,access=http,dst=:8080}" vlc://quit

