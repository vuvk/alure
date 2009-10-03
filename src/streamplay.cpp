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

#include <list>
#include <vector>

#ifdef HAVE_WINDOWS_H

typedef struct {
    ALuint (*func)(ALvoid*);
    ALvoid *ptr;
    HANDLE thread;
} ThreadInfo;

static DWORD CALLBACK StarterFunc(void *ptr)
{
    ThreadInfo *inf = (ThreadInfo*)ptr;
    ALint ret;

    ret = inf->func(inf->ptr);
    ExitThread((DWORD)ret);

    return (DWORD)ret;
}

static ThreadInfo *StartThread(ALuint (*func)(ALvoid*), ALvoid *ptr)
{
    DWORD dummy;
    ThreadInfo *inf = new ThreadInfo;
    inf->func = func;
    inf->ptr = ptr;

    inf->thread = CreateThread(NULL, 0, StarterFunc, inf, 0, &dummy);
    if(!inf->thread)
    {
        delete inf;
        return NULL;
    }

    return inf;
}

static ALuint StopThread(ThreadInfo *inf)
{
    DWORD ret = 0;

    WaitForSingleObject(inf->thread, INFINITE);
    GetExitCodeThread(inf->thread, &ret);
    CloseHandle(inf->thread);

    delete inf;

    return (ALuint)ret;
}

#else

typedef struct {
    ALuint (*func)(ALvoid*);
    ALvoid *ptr;
    ALuint ret;
    pthread_t thread;
} ThreadInfo;

static void *StarterFunc(void *ptr)
{
    ThreadInfo *inf = (ThreadInfo*)ptr;
    inf->ret = inf->func(inf->ptr);
    return NULL;
}

static ThreadInfo *StartThread(ALuint (*func)(ALvoid*), ALvoid *ptr)
{
    ThreadInfo *inf = new ThreadInfo;
    inf->func = func;
    inf->ptr = ptr;

    if(pthread_create(&inf->thread, NULL, StarterFunc, inf) != 0)
    {
        delete inf;
        return NULL;
    }

    return inf;
}

static ALuint StopThread(ThreadInfo *inf)
{
    ALuint ret;

    pthread_join(inf->thread, NULL);
    ret = inf->ret;

    delete inf;

    return ret;
}

#endif

struct AsyncPlayEntry {
	alureStream *stream;
	ALuint source;
	std::vector<ALuint> buffers;
	ALsizei loopcount;
	void (*eos_callback)(void*);
	void *user_data;

	AsyncPlayEntry() : stream(NULL), source(0), loopcount(0),
	                   eos_callback(NULL), user_data(NULL)
	{ }
};
static std::list<AsyncPlayEntry> AsyncPlayList;
static ThreadInfo *PlayThreadHandle;
static volatile ALboolean PlayThread;

ALuint AsyncPlayFunc(ALvoid*)
{
	while(PlayThread)
	{
		EnterCriticalSection(&cs_StreamPlay);
		std::list<AsyncPlayEntry>::iterator i = AsyncPlayList.begin(),
		                                    end = AsyncPlayList.begin();
		while(i != end)
		{
			ALuint buf;
			ALint processed;
			ALint queued;
			ALint state;

			alGetSourcei(i->source, AL_SOURCE_STATE, &state);
			alGetSourcei(i->source, AL_BUFFERS_QUEUED, &queued);
			alGetSourcei(i->source, AL_BUFFERS_PROCESSED, &processed);
			while(processed > 0)
			{
				queued--;
				processed--;
				alSourceUnqueueBuffers(i->source, 1, &buf);
				do {
					ALint filled = alureBufferDataFromStream(i->stream, 1, &buf);
					if(filled > 0)
					{
						queued++;
						alSourceQueueBuffers(i->source, 1, &buf);
						break;
					}
					if(i->loopcount == 0)
						break;
					if(i->loopcount != -1)
						i->loopcount--;
				} while(alureRewindStream(i->stream));
			}
			if(state != AL_PLAYING && state != AL_PAUSED)
			{
				if(queued == 0)
				{
					alSourcei(i->source, AL_BUFFER, 0);
					alDeleteBuffers(i->buffers.size(), &i->buffers[0]);
					if(i->eos_callback)
						i->eos_callback(i->user_data);
					i = AsyncPlayList.erase(i);
					continue;
				}
				alSourcePlay(i->source);
			}
			i++;
		}
		LeaveCriticalSection(&cs_StreamPlay);

		alureSleep(0.01f);
	}

	return 0;
}


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
	if(alGetError() != AL_NO_ERROR)
	{
		SetError("Existing OpenAL error");
		return AL_FALSE;
	}

	if(numBufs < 2)
	{
		SetError("Invalid buffer count");
		return AL_FALSE;
	}

	if(!alIsSource(source))
	{
		SetError("Invalid source ID");
		return AL_FALSE;
	}

	EnterCriticalSection(&cs_StreamPlay);

	std::list<AsyncPlayEntry>::iterator i = AsyncPlayList.begin(),
	                                    end = AsyncPlayList.end();
	while(i != end)
	{
		if(i->stream == stream)
		{
			SetError("Stream is already playing");
			LeaveCriticalSection(&cs_StreamPlay);
			return AL_FALSE;
		}
		i++;
	}

	{
		AsyncPlayEntry ent;
		AsyncPlayList.push_front(ent);
	}
	i = AsyncPlayList.begin();
	i->stream = stream;
	i->source = source;
	i->loopcount = loopcount;
	i->eos_callback = eos_callback;
	i->user_data = userdata;

	i->buffers.resize(numBufs);
	alGenBuffers(numBufs, &i->buffers[0]);
	if(alGetError() != AL_NO_ERROR)
	{
		AsyncPlayList.erase(i);
		LeaveCriticalSection(&cs_StreamPlay);
		SetError("Error generating buffers");
		return AL_FALSE;
	}

	if(alureBufferDataFromStream(stream, numBufs, &i->buffers[0]) < numBufs)
	{
		alDeleteBuffers(numBufs, &i->buffers[0]);
		alGetError();
		AsyncPlayList.erase(i);
		LeaveCriticalSection(&cs_StreamPlay);
		SetError("Error buffering from stream (perhaps too short)");
		return AL_FALSE;
	}

	alSourceStop(source);
	alSourcei(source, AL_BUFFER, 0);
	alSourceQueueBuffers(source, numBufs, &i->buffers[0]);
	if(alGetError() != AL_NO_ERROR)
	{
		alDeleteBuffers(numBufs, &i->buffers[0]);
		alGetError();
		AsyncPlayList.erase(i);
		LeaveCriticalSection(&cs_StreamPlay);
		SetError("Error starting source");
		return AL_FALSE;
	}

	PlayThread = true;
	if(!PlayThreadHandle)
		PlayThreadHandle = StartThread(AsyncPlayFunc, NULL);
	if(!PlayThreadHandle)
	{
		alSourcei(source, AL_BUFFER, 0);
		alGetError();
		AsyncPlayList.erase(i);
		LeaveCriticalSection(&cs_StreamPlay);
		SetError("Error starting async thread");
		return AL_FALSE;
	}

	LeaveCriticalSection(&cs_StreamPlay);

	return AL_TRUE;
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
	EnterCriticalSection(&cs_StreamPlay);
	std::list<AsyncPlayEntry>::iterator i = AsyncPlayList.begin(),
	                                    end = AsyncPlayList.end();
	while(i != end)
	{
		if(i->stream == stream)
		{
			alSourceStop(i->source);
			alSourcei(i->source, AL_BUFFER, 0);
			alDeleteBuffers(i->buffers.size(), &i->buffers[0]);
			alGetError();
			if(!ignore_callback && i->eos_callback)
				i->eos_callback(i->user_data);
			AsyncPlayList.erase(i);
			break;
		}
		i++;
	}
	PlayThread = (AsyncPlayList.size() > 0);
	LeaveCriticalSection(&cs_StreamPlay);
	if(!PlayThread)
	{
		StopThread(PlayThreadHandle);
		PlayThreadHandle = NULL;
	}
}

} // extern "C"
