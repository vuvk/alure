/*
 * ALURE utility library
 * Copyright (C) 2009 by Chris Robinson.
 * This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 *  License along with this library; if not, write to the
 *  Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 *  Boston, MA  02111-1307, USA.
 * Or go to http://www.gnu.org/copyleft/lgpl.html
 */

/* Title: File Loading */

#include "config.h"

#include "main.h"

#include <string.h>

#include <vector>
#include <memory>


extern "C" {

/* Function: alureCreateBufferFromFile
 *
 * Loads the given file into a new OpenAL buffer object. The formats supported
 * depend on the options the library was compiled with, what libraries are
 * available at runtime, and the installed decode callbacks. Requires an active
 * context.
 *
 * Returns:
 * A new buffer ID with the loaded sound, or AL_NONE on error.
 *
 * See Also:
 * <alureCreateBufferFromMemory>, <alureBufferDataFromFile>
 */
ALURE_API ALuint ALURE_APIENTRY alureCreateBufferFromFile(const ALchar *fname)
{
    init_alure();

    if(alGetError() != AL_NO_ERROR)
    {
        SetError("Existing OpenAL error");
        return AL_NONE;
    }

    ALuint buf;
    alGenBuffers(1, &buf);
    if(alGetError() != AL_NO_ERROR)
    {
        SetError("Buffer creation failed");
        return AL_NONE;
    }

    if(alureBufferDataFromFile(fname, buf) == AL_FALSE)
    {
        alDeleteBuffers(1, &buf);
        alGetError();
    }

    return buf;
}

/* Function: alureCreateBufferFromMemory
 *
 * Loads a file image from memory into a new OpenAL buffer object, similar to
 * alureCreateBufferFromFile. Requires an active context.
 *
 * Returns:
 * A new buffer ID with the loaded sound, or AL_NONE on error.
 *
 * See Also:
 * <alureCreateBufferFromFile>, <alureBufferDataFromMemory>
 */
ALURE_API ALuint ALURE_APIENTRY alureCreateBufferFromMemory(const ALubyte *fdata, ALsizei length)
{
    init_alure();

    if(alGetError() != AL_NO_ERROR)
    {
        SetError("Existing OpenAL error");
        return AL_NONE;
    }

    ALuint buf;
    alGenBuffers(1, &buf);
    if(alGetError() != AL_NO_ERROR)
    {
        SetError("Buffer creation failed");
        return AL_NONE;
    }

    if(alureBufferDataFromMemory(fdata, length, buf) == AL_FALSE)
    {
        alDeleteBuffers(1, &buf);
        alGetError();
    }

    return buf;
}

/* Function: alureBufferDataFromFile
 *
 * Loads the given file into an existing OpenAL buffer object. The previous
 * contents of the buffer are replaced. Requires an active context.
 *
 * Returns:
 * AL_FALSE on error.
 *
 * See Also:
 * <alureCreateBufferFromFile>, <alureBufferDataFromMemory>
 */
ALURE_API ALboolean ALURE_APIENTRY alureBufferDataFromFile(const ALchar *fname, ALuint buffer)
{
    init_alure();

    if(alGetError() != AL_NO_ERROR)
    {
        SetError("Existing OpenAL error");
        return AL_FALSE;
    }

    std::auto_ptr<alureStream> stream(create_stream(fname));
    if(!stream->IsValid())
        return AL_FALSE;

    ALenum format;
    ALuint freq, blockAlign;

    if(!stream->GetFormat(&format, &freq, &blockAlign))
    {
        SetError("Could not get sample format");
        return AL_FALSE;
    }

    if(format == AL_NONE)
    {
        SetError("No valid format");
        return AL_FALSE;
    }
    if(blockAlign == 0)
    {
        SetError("Invalid block size");
        return AL_FALSE;
    }
    if(freq == 0)
    {
        SetError("Invalid sample rate");
        return AL_FALSE;
    }

    ALuint writePos = 0, got;
    std::vector<ALubyte> data(freq*4);
    while((got=stream->GetData(&data[writePos], data.size()-writePos)) > 0)
    {
        writePos += got;
        data.resize(data.size() * 2);
    }
    data.resize(writePos);
    stream.reset(NULL);

    alBufferData(buffer, format, &data[0], data.size(), freq);
    if(alGetError() != AL_NO_ERROR)
    {
        SetError("Buffer load failed");
        return AL_FALSE;
    }

    return AL_TRUE;
}

/* Function: alureBufferDataFromMemory
 *
 * Loads a file image from memory into an existing OpenAL buffer object,
 * similar to alureBufferDataFromFile. Requires an active context.
 *
 * Returns:
 * AL_FALSE on error.
 *
 * See Also:
 * <alureCreateBufferFromMemory>, <alureBufferDataFromFile>
 */
ALURE_API ALboolean ALURE_APIENTRY alureBufferDataFromMemory(const ALubyte *fdata, ALsizei length, ALuint buffer)
{
    init_alure();

    if(alGetError() != AL_NO_ERROR)
    {
        SetError("Existing OpenAL error");
        return AL_FALSE;
    }

    if(length < 0)
    {
        SetError("Invalid data length");
        return AL_FALSE;
    }

    MemDataInfo memData;
    memData.Data = fdata;
    memData.Length = length;
    memData.Pos = 0;

    std::auto_ptr<alureStream> stream(create_stream(memData));
    if(!stream->IsValid())
        return AL_FALSE;

    ALenum format;
    ALuint freq, blockAlign;

    if(!stream->GetFormat(&format, &freq, &blockAlign))
    {
        SetError("Could not get sample format");
        return AL_FALSE;
    }

    if(format == AL_NONE)
    {
        SetError("No valid format");
        return AL_FALSE;
    }
    if(blockAlign == 0)
    {
        SetError("Invalid block size");
        return AL_FALSE;
    }
    if(freq == 0)
    {
        SetError("Invalid sample rate");
        return AL_FALSE;
    }

    ALuint writePos = 0, got;
    std::vector<ALubyte> data(freq*4);
    while((got=stream->GetData(&data[writePos], data.size()-writePos)) > 0)
    {
        writePos += got;
        data.resize(data.size() * 2);
    }
    data.resize(writePos);
    stream.reset(NULL);

    alBufferData(buffer, format, &data[0], data.size(), freq);
    if(alGetError() != AL_NO_ERROR)
    {
        SetError("Buffer load failed");
        return AL_FALSE;
    }

    return AL_TRUE;
}

} // extern "C"
