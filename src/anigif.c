/*****************************************************************************
 * anigif.c: theora decoder module making use of libtheora.
 *****************************************************************************
 * Copyright (C) 1999-2012 the VideoLAN team
 * $Id: b3c7b7406cf5713595711f1f8d5650ed7d3c184a $
 *
 * Authors: Gildas Bazin <gbazin@videolan.org>
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

#include "gif_lib.h"

#include <assert.h>
#include <limits.h>

/*****************************************************************************
 * decoder_sys_t : theora decoder descriptor
 *****************************************************************************/
struct decoder_sys_t
{
    /*
     * Input properties
     */
    bool b_has_headers;

    /*
     * Common properties
     */
    mtime_t i_pts;
};

/*****************************************************************************
 * Local prototypes
 *****************************************************************************/
static int  OpenDecoder   ( vlc_object_t * );
static void CloseDecoder  ( vlc_object_t * );

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
    set_description( "Animated GIF decoder" )
    set_capability( "decoder", 100 )
    set_callbacks( OpenDecoder, CloseDecoder )
    add_shortcut( "anigif" )

    add_submodule ()
    set_description( "Animated GIF video encoder" )
    set_capability( "encoder", 150 )
    set_callbacks( OpenEncoder, CloseEncoder )
    add_shortcut( "anigif" )

#   define ENC_CFG_PREFIX "sout-anigif-"
vlc_module_end ()

/*****************************************************************************
 * OpenDecoder: probe the decoder and return score
 *****************************************************************************/
static int OpenDecoder( vlc_object_t *p_this )
{
    return VLC_EGENERIC;
}

/*****************************************************************************
 * CloseDecoder: theora decoder destruction
 *****************************************************************************/
static void CloseDecoder( vlc_object_t *p_this )
{
}

/*****************************************************************************
 * encoder_sys_t : theora encoder descriptor
 *****************************************************************************/
struct encoder_sys_t
{
    /*
     * GIFLIB properties
     */
    GifFileType* gif;
    ColorMapObject* gifColorMap;
    GifByteType* gcbCompiled;
    size_t gcbCompiledLen;
    int gifColorRes;
    int i_width, i_height;

    uint8_t* buffer;
    size_t bufferLen, bufferCapacity;
};

/*****************************************************************************
 * gifBufferWrite: callback for giflib to write to the internal buffer
 *****************************************************************************/
int gifBufferWrite(GifFileType* gif, const GifByteType* data, int len) {
    encoder_sys_t* p_sys = (encoder_sys_t*) gif->UserData;

    // resize buffer if necessary
    if(p_sys->bufferLen + len > p_sys->bufferCapacity) {
        size_t newCapacity =
            (p_sys->bufferCapacity << 1) - (p_sys->bufferCapacity >> 1); // * 1.5

        p_sys->buffer = realloc(p_sys->buffer, newCapacity);
        if(p_sys->buffer == NULL) {
            p_sys->bufferCapacity = 0; //TODO what will subsequent calls to gifBufferWrite do?
            return 0;
        }
        p_sys->bufferCapacity = newCapacity;
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
    int ret;

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

    /* //TODO: configuration options
    config_ChainParse( p_enc, ENC_CFG_PREFIX, ppsz_enc_options, p_enc->p_cfg );

    i_quality = var_GetInteger( p_enc, ENC_CFG_PREFIX "quality" );
    if( i_quality > 10 ) i_quality = 10;
    if( i_quality < 0 ) i_quality = 0;
    */

    p_sys->i_width = p_enc->fmt_in.video.i_width;
    p_sys->i_height = p_enc->fmt_in.video.i_height;

    //TODO: fps
    if( !p_enc->fmt_in.video.i_frame_rate ||
        !p_enc->fmt_in.video.i_frame_rate_base )
    { } else { };

    /* //TODO: huh?
    if( p_enc->fmt_in.video.i_sar_num > 0 && p_enc->fmt_in.video.i_sar_den > 0 )
    {
        unsigned i_dst_num, i_dst_den;
        vlc_ureduce( &i_dst_num, &i_dst_den,
                     p_enc->fmt_in.video.i_sar_num,
                     p_enc->fmt_in.video.i_sar_den, 0 );
        p_sys->ti.aspect_numerator = i_dst_num;
        p_sys->ti.aspect_denominator = i_dst_den;
    }
    else
    {
        p_sys->ti.aspect_numerator = 4;
        p_sys->ti.aspect_denominator = 3;
    }
    */

    // initialize buffer for giflib to write to
    p_sys->bufferCapacity = p_sys->i_width * p_sys->i_height + 800; // wild guess
    p_sys->buffer = malloc(p_sys->bufferCapacity);
    p_sys->bufferLen = 0;

    if( ( p_sys->gif = EGifOpen(p_sys, gifBufferWrite, &ret) ) == NULL ) {
        msg_Err(p_enc, "Anigif encoder initialisation failed: %s",
                GifErrorString(ret));
        return VLC_ENOMEM; // according to docs the only possible error condition
    }

    EGifSetGifVersion(p_sys->gif, 1);

    p_sys->gifColorMap = GifMakeMapObject(256, NULL); //TODO: precompute colors?
    if(p_sys->gifColorMap == NULL)
    {
        return VLC_ENOMEM; //TODO: review
    }

    ret = EGifPutScreenDesc(p_sys->gif,
                            p_sys->i_width,
                            p_sys->i_height,
                            7, // == log_2 ( color map size = 256 ) - 1
                            NO_TRANSPARENT_COLOR,
                            p_sys->gifColorMap);
    if( ret == GIF_ERROR )
    {
        msg_Warn(p_enc, "Anigif encoder initialisation failed: %s",
                 GifErrorString(p_sys->gif->Error));
        return VLC_EGENERIC;
    }

    //TODO: "application extension block" specifying animation loop / repeat count

    // prepare 'compiled' gcb, cause it is the same for all images
    gcb.DisposalMode = DISPOSE_BACKGROUND;
    gcb.DelayTime = 100; //in 10ms - TODO: use fps from input
    gcb.TransparentColor = NO_TRANSPARENT_COLOR;
    p_sys->gcbCompiledLen = EGifGCBToExtension(&gcb, p_sys->gcbCompiled);

    return VLC_SUCCESS;
}

/****************************************************************************
 * Encode: the whole thing
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

    EGifPutExtension(p_sys->gif,
                     GRAPHICS_EXT_FUNC_CODE,
                     p_sys->gcbCompiledLen,
                     p_sys->gcbCompiled);

    ret = EGifPutImageDesc(p_sys->gif,
                           0,
                           0,
                           p_sys->i_width,
                           p_sys->i_height,
                           0,
                           NULL);
    if( ret == GIF_ERROR )
    {
        msg_Warn(p_enc, "failed encoding a frame, at image description, message %s",
                 GifErrorString(p_sys->gif->Error) );
        return NULL;
    }

    for(line = 0; line < p_sys->i_height; line++)
    {
        ret = EGifPutLine(p_sys->gif, p_pict->p[0].p_pixels, p_pict->p[0].i_pitch);
        if( ret == GIF_ERROR )
        {
            msg_Warn(p_enc, "failed encoding a frame, line %d, message %s", line,
                     GifErrorString(p_sys->gif->Error) );
            return NULL;
        }
    };

    p_block = block_New( p_enc, p_sys->bufferLen );
    memcpy( p_block->p_buffer, p_sys->buffer, p_sys->bufferLen );

    p_block->i_dts = p_block->i_pts = p_pict->date;

    return p_block;
}

/*****************************************************************************
 * CloseEncoder: theora encoder destruction
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
        msg_Warn(p_enc, "Could not close encoder: %s",
                 GifErrorString(p_sys->gif->Error));
    }
    free( p_sys );
}
