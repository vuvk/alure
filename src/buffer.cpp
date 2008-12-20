#include "config.h"

#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alext.h>

#include "main.h"

#include <string.h>

#include <vector>
#include <memory>


static alureStream *InitStream(alureStream *instream, ALsizei chunkLength, ALsizei numBufs, ALuint *bufs)
{
    std::auto_ptr<alureStream> stream(instream);
    ALenum format;
    ALuint freq, blockAlign;

    if(!stream->GetFormat(&format, &freq, &blockAlign))
    {
        last_error = "Unsupported format";
        return NULL;
    }

    chunkLength -= chunkLength%blockAlign;
    if(chunkLength < 0)
    {
        last_error = "Chunk length too small";
        return NULL;
    }

    stream->chunkLen = chunkLength;
    stream->dataChunk = new ALubyte[stream->chunkLen];

    alGenBuffers(numBufs, bufs);
    if(alGetError() != AL_NO_ERROR)
    {
        last_error = "Buffer creation failed";
        return NULL;
    }

    ALsizei filled = alureBufferDataFromStream(stream.get(), numBufs, bufs);
    if(filled < 0)
    {
        alDeleteBuffers(numBufs, bufs);
        alGetError();

        last_error = "Buffering error";
        return NULL;
    }

    while(filled < numBufs)
    {
        alBufferData(bufs[filled], format, stream->dataChunk, 0, freq);
        if(alGetError() != AL_NO_ERROR)
        {
            last_error = "Buffer load failed";
            return NULL;
        }
        filled++;
    }

    return stream.release();
}


extern "C" {

ALURE_API alureStream* ALURE_APIENTRY alureCreateStreamFromFile(const ALchar *fname, ALsizei chunkLength, ALsizei numBufs, ALuint *bufs)
{
    init_alure();

    if(alGetError() != AL_NO_ERROR)
    {
        last_error = "Existing OpenAL error";
        return NULL;
    }

    if(chunkLength < 0)
    {
        last_error = "Invalid chunk length";
        return NULL;
    }

    if(numBufs < 0)
    {
        last_error = "Invalid buffer count";
        return NULL;
    }

    alureStream *stream = create_stream(fname);
    if(!stream->IsValid())
    {
        delete stream;
        last_error = "Open failed";
        return NULL;
    }

    return InitStream(stream, chunkLength, numBufs, bufs);
}

ALURE_API alureStream* ALURE_APIENTRY alureCreateStreamFromMemory(const ALubyte *fdata, ALuint length, ALsizei chunkLength, ALsizei numBufs, ALuint *bufs)
{
    init_alure();

    if(alGetError() != AL_NO_ERROR)
    {
        last_error = "Existing OpenAL error";
        return NULL;
    }

    if(chunkLength < 0)
    {
        last_error = "Invalid chunk length";
        return NULL;
    }

    if(numBufs < 0)
    {
        last_error = "Invalid buffer count";
        return NULL;
    }

    if(length <= 0)
    {
        last_error = "Invalid data length";
        return NULL;
    }

    ALubyte *streamData = new ALubyte[length];
    memcpy(streamData, fdata, length);

    MemDataInfo memData;
    memData.Data = streamData;
    memData.Length = length;
    memData.Pos = 0;

    alureStream *stream = create_stream(memData);
    stream->data = streamData;
    if(!stream->IsValid())
    {
        delete stream;
        last_error = "Open failed";
        return NULL;
    }

    return InitStream(stream, chunkLength, numBufs, bufs);
}

ALURE_API alureStream* ALURE_APIENTRY alureCreateStreamFromStaticMemory(const ALubyte *fdata, ALuint length, ALsizei chunkLength, ALsizei numBufs, ALuint *bufs)
{
    init_alure();

    if(alGetError() != AL_NO_ERROR)
    {
        last_error = "Existing OpenAL error";
        return NULL;
    }

    if(chunkLength < 0)
    {
        last_error = "Invalid chunk length";
        return NULL;
    }

    if(numBufs < 0)
    {
        last_error = "Invalid buffer count";
        return NULL;
    }

    if(length <= 0)
    {
        last_error = "Invalid data length";
        return NULL;
    }

    MemDataInfo memData;
    memData.Data = fdata;
    memData.Length = length;
    memData.Pos = 0;

    alureStream *stream = create_stream(memData);
    if(!stream->IsValid())
    {
        delete stream;
        last_error = "Open failed";
        return NULL;
    }

    return InitStream(stream, chunkLength, numBufs, bufs);
}

ALURE_API ALsizei ALURE_APIENTRY alureBufferDataFromStream(alureStream *stream, ALsizei numBufs, ALuint *bufs)
{
    if(alGetError() != AL_NO_ERROR)
    {
        last_error = "Existing OpenAL error";
        return -1;
    }

    if(!stream)
    {
        last_error = "Null stream pointer";
        return -1;
    }

    if(numBufs < 0)
    {
        last_error = "Invalid buffer count";
        return -1;
    }

    ALenum format;
    ALuint freq, blockAlign;

    if(!stream->GetFormat(&format, &freq, &blockAlign))
    {
        last_error = "Unsupported format";
        return -1;
    }

    ALsizei filled;
    for(filled = 0;filled < numBufs;filled++)
    {
        ALuint got = stream->GetData(stream->dataChunk, stream->chunkLen);
        if(got == 0) break;

        alBufferData(bufs[filled], format, stream->dataChunk, got, freq);
        if(alGetError() != AL_NO_ERROR)
        {
            last_error = "Buffer load failed";
            return -1;
        }
    }

    return filled;
}

ALURE_API ALboolean ALURE_APIENTRY alureRewindStream(alureStream *stream)
{
    if(!stream)
    {
        last_error = "Null stream pointer";
        return AL_FALSE;
    }

    return stream->Rewind();
}

ALURE_API ALboolean ALURE_APIENTRY alureDestroyStream(alureStream *stream, ALsizei numBufs, ALuint *bufs)
{
    if(alGetError() != AL_NO_ERROR)
    {
        last_error = "Existing OpenAL error";
        return AL_FALSE;
    }

    if(numBufs < 0)
    {
        last_error = "Invalid buffer count";
        return AL_FALSE;
    }

    alDeleteBuffers(numBufs, bufs);
    if(alGetError() != AL_NO_ERROR)
    {
        last_error = "Buffer deletion failed";
        return AL_FALSE;
    }

    delete stream;
    return AL_TRUE;
}


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
