/*****************************************************************************
 * anigif.c: animated gif encoder module making use of GIFLIB.
 *****************************************************************************
 * Copyright (C) 1999-2012 the VideoLAN team
 * $Id: b3c7b7406cf5713595711f1f8d5650ed7d3c184a $
 *
 * Authors: Jonathan Biegert <azrdev@googlemail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

/*****************************************************************************
 * Preamble
 *****************************************************************************/
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_codec.h>
#include <vlc_sout.h>
#include <vlc_input.h>

#include "anigif.h"

#include "gif_lib5.h"

#include <assert.h>
#include <limits.h>

/*****************************************************************************
 * Local prototypes
 *****************************************************************************/
static int  OpenEncoder( vlc_object_t *p_this );
static void CloseEncoder( vlc_object_t *p_this );
static block_t *Encode( encoder_t *p_enc, picture_t *p_pict );

/*****************************************************************************
 * Module descriptor
 *****************************************************************************/

vlc_module_begin ()
    set_category( CAT_INPUT )
    set_subcategory( SUBCAT_INPUT_VCODEC )
    set_shortname( "anigif" )
    set_description( "Animated GIF video encoder" )
    set_capability( "encoder", 150 )
    set_callbacks( OpenEncoder, CloseEncoder )
    add_shortcut( "anigif" )

#   define ENC_CFG_PREFIX "sout-anigif-"

#define ENC_LOOP "loop"
#define ENC_LOOP_DESC_SHORT "Animation repeat count"
#define ENC_LOOP_DESC_LONG "How often to repeat the animation, up to 65535. " \
  "0 means infinite, negative values mean no loop at all, this is the default. "
    add_integer( ENC_CFG_PREFIX ENC_LOOP, -1, ENC_LOOP_DESC_SHORT, ENC_LOOP_DESC_LONG, false );

vlc_module_end ()


static const char* const ppsz_enc_options[] = {
    ENC_LOOP, NULL
};

/*****************************************************************************
 * encoder_sys_t : anigif encoder descriptor
 *****************************************************************************/
struct encoder_sys_t
{
    /*
     * GIFLIB properties
     */
    GifFileType* gif;
    ColorMapObject* gifColorMap;
    GifByteType gcbCompiled[4];
    size_t gcbCompiledLen;
    int gifColorRes;
    int i_width, i_height;
    bool isFirstPicture;
    int displayDuration; // in 10ms units

    uint8_t* buffer;
    size_t bufferLen, bufferCapacity;
};

/*****************************************************************************
 * gifBufferWrite: callback for giflib to write to the internal buffer
 *****************************************************************************/
int gifBufferWrite(GifFileType* gif, const GifByteType* data, int len) {
    encoder_sys_t* p_sys = (encoder_sys_t*) gif->UserData;

    // expand buffer if necessary
    if(p_sys->bufferLen + len > p_sys->bufferCapacity) {
        size_t newCapacity =
            (p_sys->bufferCapacity << 1) - (p_sys->bufferCapacity >> 1); // * 1.5

        uint8_t* newBuffer = realloc(p_sys->buffer, newCapacity);
        if(newBuffer == NULL) {
            // realloc() failed, but the old buffer is still valid

            //perhaps we should at least write as much as there is capacity
            //remaining, instead of just failing
            return 0;
        }
        p_sys->bufferCapacity = newCapacity;
        p_sys->buffer = newBuffer;
    }

    memcpy(p_sys->buffer + p_sys->bufferLen, data, len);
    p_sys->bufferLen += len;
    return len;
}

/*****************************************************************************
 * OpenEncoder: probe the encoder and return score
 *****************************************************************************/
static int OpenEncoder( vlc_object_t *p_this )
{
    encoder_t *p_enc = (encoder_t *)p_this;
    encoder_sys_t *p_sys;
    GraphicsControlBlock gcb;
    GifColorType color;
    int ret, i;
    char aeb[3];
    int aniRepeatCount;

    if( p_enc->fmt_out.i_codec != VLC_CODEC_ANIGIF &&
        !p_enc->b_force )
    {
        return VLC_EGENERIC;
    }

    /* Allocate the memory needed to store the encoder's structure */
    if( ( p_sys = malloc(sizeof(encoder_sys_t)) ) == NULL )
        return VLC_ENOMEM;
    p_enc->p_sys = p_sys;

    p_enc->pf_encode_video = Encode;
    p_enc->fmt_in.i_codec = VLC_CODEC_RGB8;
    p_enc->fmt_out.i_codec = VLC_CODEC_ANIGIF;

    // configuration options
    config_ChainParse( p_enc, ENC_CFG_PREFIX, ppsz_enc_options, p_enc->p_cfg );
    aniRepeatCount = var_GetInteger(p_enc, ENC_CFG_PREFIX ENC_LOOP );
    if(aniRepeatCount >= 65535) {
	    msg_Info( p_enc, "loop count > maximum, capping to 65535" );
	    aniRepeatCount = 65535;
    }

    /*
    //TODO: option quality ~= color depth ?
    i_quality = var_GetInteger( p_enc, ENC_CFG_PREFIX "quality" );
    if( i_quality > 10 ) i_quality = 10;
    if( i_quality < 0 ) i_quality = 0;
    */

    p_sys->i_width = p_enc->fmt_in.video.i_width;
    p_sys->i_height = p_enc->fmt_in.video.i_height;
    p_sys->isFirstPicture = true;

    // calculate display duration for images from input frame rate
    if( !p_enc->fmt_in.video.i_frame_rate ||
        !p_enc->fmt_in.video.i_frame_rate_base )
    {
        p_sys->displayDuration = 4; // default to 4fps, which is 4*10ms
    }
    else
    {
        p_sys->displayDuration = 100 * p_enc->fmt_in.video.i_frame_rate_base
                                 / p_enc->fmt_in.video.i_frame_rate;
    };

    // initialize buffer for giflib to write to
    p_sys->bufferCapacity = p_sys->i_width * p_sys->i_height + 800; // wild guess
    p_sys->buffer = malloc(p_sys->bufferCapacity);
    p_sys->bufferLen = 0;

    if( ( p_sys->gif = EGifOpen(p_sys, gifBufferWrite, &ret) ) == NULL ) {
        msg_Err(p_enc, "EGifOpen failed: %s",
                GifErrorString(ret));
        return VLC_ENOMEM; // according to docs the only possible error condition
    }

    EGifSetGifVersion(p_sys->gif, 1);

    p_sys->gifColorMap = GifMakeMapObject(256, NULL);
    if(p_sys->gifColorMap == NULL)
    {
        msg_Warn(p_enc, "GifMakeMapObject failed: %s",
                 GifErrorString(p_sys->gif->Error));
        return VLC_EGENERIC; // might be (only) VLC_ENOMEM
    }
    for(i = 0; i < 256; i++)
    {
        color = (GifColorType){ i, i, i };
        /* p_palette always NULL :-(
        color.Red   = p_enc->fmt_in.video.p_palette->palette[i][0];
        color.Green = p_enc->fmt_in.video.p_palette->palette[i][1];
        color.Blue  = p_enc->fmt_in.video.p_palette->palette[i][2];
        */
        //FIXME: why the hell does this code work?
        p_sys->gifColorMap->Colors[i] = color;
    }

    ret = EGifPutScreenDesc(p_sys->gif,
                            p_sys->i_width,
                            p_sys->i_height,
                            7, // == log_2 ( color map size = 256 ) - 1
                            NO_TRANSPARENT_COLOR,
                            p_sys->gifColorMap);
    if( ret == GIF_ERROR )
    {
        msg_Warn(p_enc, "EGifPutScreenDesc failed: %s",
                 GifErrorString(p_sys->gif->Error));
        return VLC_EGENERIC;
    }

    // application extension block specifying animation loop count
    if(aniRepeatCount >= 0) {

        ret = EGifPutExtensionLeader(p_sys->gif, APPLICATION_EXT_FUNC_CODE);
        if( ret == GIF_ERROR )
        {
            msg_Info(p_enc, "EGifPutExtensionLeader failed: %s",
                     GifErrorString(p_sys->gif->Error));
        }

        ret = EGifPutExtensionBlock(p_sys->gif, 11, "NETSCAPE2.0");
        if( ret == GIF_ERROR )
        {
            msg_Info(p_enc, "EGifPutExtension failed: %s",
                     GifErrorString(p_sys->gif->Error));
        }

	//TODO: either this is wrong, or firefox ignores discrete loop count values
        aeb[0] = 0x1;
        aeb[1] = aniRepeatCount >> 8;
        aeb[2] = aniRepeatCount & 0xFF;
        ret = EGifPutExtensionBlock(p_sys->gif, 3, aeb);
        if( ret == GIF_ERROR )
        {
            msg_Info(p_enc, "EGifPutExtension failed: %s",
                     GifErrorString(p_sys->gif->Error));
        }

        ret = EGifPutExtensionTrailer(p_sys->gif);
        if( ret == GIF_ERROR )
        {
            msg_Info(p_enc, "EGifPutExtensionTrailer failed: %s",
                     GifErrorString(p_sys->gif->Error));
        }
    }

    // prepare 'compiled' gcb, because it is the same for all images
    gcb.DisposalMode = DISPOSE_BACKGROUND;
    gcb.DelayTime = p_sys->displayDuration;
    gcb.TransparentColor = NO_TRANSPARENT_COLOR;
    p_sys->gcbCompiledLen = 4;
    assert( p_sys->gcbCompiledLen == EGifGCBToExtension(&gcb, p_sys->gcbCompiled) );

    /*
    msg_Dbg(p_enc, "Anigif encoder intialized: width %d, height %d",
            p_sys->i_width, p_sys->i_height);
    */
    return VLC_SUCCESS;
}

/****************************************************************************
 * Encode: writes one GCB and one Image to the output gif
 ****************************************************************************/
static block_t *Encode( encoder_t *p_enc, picture_t *p_pict )
{
    encoder_sys_t *p_sys = p_enc->p_sys;
    block_t *p_block;
    int ret, line;

    if( !p_pict ) return NULL;
    /* Sanity check */
    if( p_pict->p[0].i_pitch < (int)p_sys->i_width ||
        p_pict->p[0].i_lines < (int)p_sys->i_height )
    {
        msg_Warn( p_enc, "frame is smaller than encoding size"
                  "(%ix%i->%ix%i) -> dropping frame",
                  p_pict->p[0].i_pitch, p_pict->p[0].i_lines,
                  p_sys->i_width, p_sys->i_height );
        return NULL;
    }

    /*
    msg_Dbg(p_enc,
            "Encoding frame. %d planes, p[0]: lines %d, pitch %d,"
               "pixel_pitch %d, visible lines %d, visible pitch %d",
            p_pict->i_planes,
            p_pict->p[0].i_lines,
            p_pict->p[0].i_pitch,
            p_pict->p[0].i_pixel_pitch,
            p_pict->p[0].i_visible_lines,
            p_pict->p[0].i_visible_pitch);
    */

    // graphics control extension - specify image display duration
    EGifPutExtension(p_sys->gif,
                     GRAPHICS_EXT_FUNC_CODE,
                     p_sys->gcbCompiledLen,
                     p_sys->gcbCompiled);

    // gif image header
    ret = EGifPutImageDesc(p_sys->gif,
                           0,
                           0,
                           p_sys->i_width,
                           p_sys->i_height,
                           0,
                           NULL);
    if( ret == GIF_ERROR )
    {
        msg_Warn(p_enc, "EGifPutImageDesc failed: %s",
                 GifErrorString(p_sys->gif->Error) );
        return NULL;
    }

    // put the image data line by line
    //FIXME: somehow the last 6 columns are black. a bug in the ->RGB8 conversion?
    for(line = 0; // maybe p_pict->format.i_y_offset;
        line < p_pict->p[0].i_visible_lines;
        line++)
    {
        ret = EGifPutLine(p_sys->gif,
                          p_pict->p[0].p_pixels
                             + line * p_pict->p[0].i_pitch,
                             // maybe + p_pict->format.i_x_offset,
                          p_pict->p[0].i_visible_pitch);
        if( ret == GIF_ERROR )
        {
            msg_Dbg(p_enc,
                    "EGifPutLine failed (line %d): %s",
                    line,
                    GifErrorString(p_sys->gif->Error) );
            return NULL; // better do break; ?
        }
    };

    p_block = block_New( p_enc, p_sys->bufferLen );
    memcpy( p_block->p_buffer, p_sys->buffer, p_sys->bufferLen );

    p_block->i_dts = p_block->i_pts = p_pict->date;
    if( p_sys->isFirstPicture ) {
	    p_block->i_flags = BLOCK_FLAG_HEADER; // buffer contains image header
	    p_sys->isFirstPicture = false;
    }
    // reset buffer
    p_sys->bufferLen = 0;

    return p_block;
}

/*****************************************************************************
 * CloseEncoder: anigif encoder destruction
 *****************************************************************************/
static void CloseEncoder( vlc_object_t *p_this )
{
    int ret;
    encoder_t *p_enc = (encoder_t *)p_this;
    encoder_sys_t *p_sys = p_enc->p_sys;

    GifFreeMapObject(p_sys->gifColorMap);

    ret = EGifCloseFile(p_sys->gif);
    if(ret == GIF_ERROR)
    {
        msg_Warn(p_enc, "EGifCloseFile failed: %s",
                 GifErrorString(p_sys->gif->Error));
    }
    free( p_sys->buffer );
    free( p_sys );
}
