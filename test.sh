#!/bin/sh

# webcam or similar
input=v4l2://
# screendump
#input=screen://

## dump to file
#outfile="$1.gif"
#vlc -I dummy --reset-plugins-cache --codec anigif \
#	-vvv "$input" \
#	--sout "#transcode{width=320,venc=anigif,acodec=none}:standard{mux=raw,dst=${outfile},access=file}" \
#	vlc://quit

## serve via http, scale to width 320
#vlc -I dummy --reset-plugins-cache --codec anigif \
#	-vvv "$input" \
#	--sout-http-mime image/gif --sout-anigif-loop 0 \
#	--sout "#transcode{width=320,venc=anigif,acodec=none}:standard{mux=raw,access=http,dst=:8080}" \
#	vlc://quit

## serve via http, set mime type and repeat gif infinitely
vlc -I dummy --reset-plugins-cache --codec anigif \
	-vvv "$input" \
	--sout-http-mime image/gif --sout-anigif-loop 0 \
	--sout "#transcode{venc=anigif,acodec=none}:standard{mux=raw,access=http,dst=:8080}" \
	vlc://quit

