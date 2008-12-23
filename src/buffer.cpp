/* Title: File Loading */

#include "config.h"

#include "main.h"

#include <string.h>

#include <vector>
#include <memory>


extern "C" {

/* Function: alureCreateBufferFromFile
 *
 * Loads the given file into an OpenAL buffer object. The formats supported
 * depend on the options the library was compiled with, what libraries are
 * available at runtime, and the installed decode callbacks. Requires an active
 * context.
 *
 * Returns:
 * A new buffer ID with the loaded sound, or AL_NONE on error.
 *
 * See Also:
 * <alureCreateBufferFromMemory>
 */
ALURE_API ALuint ALURE_APIENTRY alureCreateBufferFromFile(const ALchar *fname)
{
    init_alure();

    if(alGetError() != AL_NO_ERROR)
    {
        last_error = "Existing OpenAL error";
        return AL_NONE;
    }

    std::auto_ptr<alureStream> stream(create_stream(fname));
    if(!stream->IsValid())
    {
        last_error = "Open failed";
        return AL_NONE;
    }

    ALenum format;
    ALuint freq, blockAlign;

    if(!stream->GetFormat(&format, &freq, &blockAlign))
    {
        last_error = "Unsupported format";
        return AL_NONE;
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

    ALuint buf;
    alGenBuffers(1, &buf);
    if(alGetError() != AL_NO_ERROR)
    {
        last_error = "Buffer creation failed";
        return AL_NONE;
    }

    alBufferData(buf, format, &data[0], data.size(), freq);
    if(alGetError() != AL_NO_ERROR)
    {
        alDeleteBuffers(1, &buf);
        alGetError();

        last_error = "Buffer load failed";
        return AL_NONE;
    }

    return buf;
}

/* Function: alureCreateBufferFromMemory
 *
 * Loads a file image from memory into an OpenAL buffer object, similar to
 * alureCreateBufferFromFile. Requires an active context.
 *
 * Returns:
 * A new buffer ID with the loaded sound, or AL_NONE on error.
 *
 * See Also:
 * <alureCreateBufferFromFile>
 */
ALURE_API ALuint ALURE_APIENTRY alureCreateBufferFromMemory(const ALubyte *fdata, ALsizei length)
{
    init_alure();

    if(alGetError() != AL_NO_ERROR)
    {
        last_error = "Existing OpenAL error";
        return AL_NONE;
    }

    if(length < 0)
    {
        last_error = "Invalid data length";
        return AL_NONE;
    }

    MemDataInfo memData;
    memData.Data = fdata;
    memData.Length = length;
    memData.Pos = 0;

    std::auto_ptr<alureStream> stream(create_stream(memData));
    if(!stream->IsValid())
    {
        last_error = "Open failed";
        return AL_NONE;
    }

    ALenum format;
    ALuint freq, blockAlign;

    if(!stream->GetFormat(&format, &freq, &blockAlign))
    {
        last_error = "Unsupported format";
        return AL_NONE;
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

    ALuint buf;
    alGenBuffers(1, &buf);
    if(alGetError() != AL_NO_ERROR)
    {
        last_error = "Buffer creation failed";
        return AL_NONE;
    }

    alBufferData(buf, format, &data[0], data.size(), freq);
    if(alGetError() != AL_NO_ERROR)
    {
        alDeleteBuffers(1, &buf);
        alGetError();

        last_error = "Buffer load failed";
        return AL_NONE;
    }

    return buf;
}

} // extern "C"
