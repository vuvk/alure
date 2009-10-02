/*
 * ALURE  OpenAL utility library
 * Copyright (C) 2009 by Chris Robinson.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 * Or go to http://www.gnu.org/copyleft/lgpl.html
 */

/* Title: Streaming Playback */

#include "config.h"

#include "main.h"

extern "C" {

/* Function: alurePlayStreamAsync
 *
 * Plays a stream asynchronously, using the specified source ID and buffer
 * count. A stream can only be played asynchronously if it is not already
 * playing. It is important that the current context is NOT changed while a
 * stream is playing, otherwise the asynchronous method used to play may start
 * calling OpenAL with an invalid ID.
 *
 * Parameters:
 * stream - The stream to play asynchronously. Any valid stream will work,
 *          although looping will only work if the stream can be rewound (ie.
 *          streams made with alureCreateStreamFromCallback cannot loop, but
 *          will play for as long as the callback provides data).
 * source - The source ID to play the stream with. Any buffers on the source
 *          will be cleared. It is valid to set source properties not related
 *          to the buffer queue or playback state (ie. you may change the
 *          source's position, pitch, gain, etc, but you must not stop the
 *          source or change the source's buffer queue). The exception is
 *          that you may pause the source. ALURE will not attempt to restart a
 *          paused source, while a stopped source is indicative of an underrun
 *          and /will/ be restarted automatically.
 * numBufs - The number of buffers used to queue with the OpenAL source. Each
 *           buffer will be filled with the chunk length specified when the
 *           source was created. This value must be at least 2.
 * loopcount - The number of times to loop the stream. When the stream reaches
 *             the end of processing, it will be rewound to continue buffering
 *             data. A value of -1 will cause the stream to loop indefinitely
 *             (until <alureStopStream> is called).
 * eos_callback - This callback will be called when the stream reaches the end,
 *                no more loops are pending, and the source reaches a stopped
 *                state.
 *
 * Returns:
 * AL_FALSE on error.
 *
 * See Also:
 * <alureStopStream>
 */
ALURE_API ALboolean ALURE_APIENTRY alurePlayStreamAsync(alureStream *stream,
    ALuint source, ALsizei numBufs, ALsizei loopcount,
    void (*eos_callback)(void *userdata), void *userdata)
{
	SetError("Not yet implemented");
	return AL_FALSE;
}

/* Function: alureStopStream
 *
 * Stops a stream currently playing asynchronously. If the stream is not
 * playing (eg. it stopped on its own, or was never started), it is silently
 * ignored. If 'ignore_callback' is not AL_FALSE, the callback specified by
 * <alurePlayStreamAsync> will be called.
 *
 * See Also:
 * <alurePlayStreamAsync>
 */
ALURE_API void ALURE_APIENTRY alureStopStream(alureStream *stream, ALboolean ignore_callback)
{
}

} // extern "C"
