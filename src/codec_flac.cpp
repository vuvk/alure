/*
 * ALURE  OpenAL utility library
 * Copyright (c) 2009-2010 by Chris Robinson.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "config.h"

#include "main.h"

#include <string.h>
#include <assert.h>

#include <istream>

#include <FLAC/all.h>


#ifdef _WIN32
#define FLAC_LIB "libFLAC.dll"
#elif defined(__APPLE__)
#define FLAC_LIB "libFLAC.8.dylib"
#else
#define FLAC_LIB "libFLAC.so.8"
#endif

static void *flac_handle;
#define MAKE_FUNC(x) static typeof(x)* p##x
MAKE_FUNC(FLAC__stream_decoder_get_state);
MAKE_FUNC(FLAC__stream_decoder_finish);
MAKE_FUNC(FLAC__stream_decoder_new);
MAKE_FUNC(FLAC__stream_decoder_seek_absolute);
MAKE_FUNC(FLAC__stream_decoder_delete);
MAKE_FUNC(FLAC__stream_decoder_process_single);
MAKE_FUNC(FLAC__stream_decoder_init_stream);
#undef MAKE_FUNC


struct flacStream : public alureStream {
private:
    FLAC__StreamDecoder *flacFile;
    ALenum format;
    ALuint samplerate;
    ALuint blockAlign;
    ALboolean useFloat;

    std::vector<ALubyte> initialData;

    ALubyte *outBytes;
    ALuint outLen;
    ALuint outTotal;

public:
    static void Init()
    {
        flac_handle = OpenLib(FLAC_LIB);
        if(!flac_handle) return;

        LOAD_FUNC(flac_handle, FLAC__stream_decoder_get_state);
        LOAD_FUNC(flac_handle, FLAC__stream_decoder_finish);
        LOAD_FUNC(flac_handle, FLAC__stream_decoder_new);
        LOAD_FUNC(flac_handle, FLAC__stream_decoder_seek_absolute);
        LOAD_FUNC(flac_handle, FLAC__stream_decoder_delete);
        LOAD_FUNC(flac_handle, FLAC__stream_decoder_process_single);
        LOAD_FUNC(flac_handle, FLAC__stream_decoder_init_stream);
    }
    static void Deinit()
    {
        if(flac_handle)
            CloseLib(flac_handle);
        flac_handle = NULL;
    }

    virtual bool IsValid()
    { return flacFile != NULL; }

    virtual bool GetFormat(ALenum *fmt, ALuint *frequency, ALuint *blockalign)
    {
        *fmt = format;
        *frequency = samplerate;
        *blockalign = blockAlign;
        return true;
    }

    virtual ALuint GetData(ALubyte *data, ALuint bytes)
    {
        outBytes = data;
        outTotal = 0;
        outLen = bytes;

        if(initialData.size() > 0)
        {
            size_t rem = std::min(initialData.size(), (size_t)bytes);
            memcpy(data, &initialData[0], rem);
            outTotal += rem;
            initialData.erase(initialData.begin(), initialData.begin()+rem);
        }

        while(outTotal < bytes)
        {
            if(pFLAC__stream_decoder_process_single(flacFile) == false ||
               pFLAC__stream_decoder_get_state(flacFile) == FLAC__STREAM_DECODER_END_OF_STREAM)
                break;
        }

        return outTotal;
    }

    virtual bool Rewind()
    {
        if(pFLAC__stream_decoder_seek_absolute(flacFile, 0) != false)
        {
            initialData.clear();
            return true;
        }

        SetError("Seek failed");
        return false;
    }

    flacStream(std::istream *_fstream)
      : alureStream(_fstream), flacFile(NULL), format(AL_NONE), samplerate(0),
        blockAlign(0), useFloat(AL_FALSE)
    {
        if(!flac_handle) return;

        flacFile = pFLAC__stream_decoder_new();
        if(flacFile)
        {
            if(pFLAC__stream_decoder_init_stream(flacFile, ReadCallback, SeekCallback, TellCallback, LengthCallback, EofCallback, WriteCallback, MetadataCallback, ErrorCallback, this) == FLAC__STREAM_DECODER_INIT_STATUS_OK)
            {
                if(InitFlac())
                {
                    // all ok
                    return;
                }

                pFLAC__stream_decoder_finish(flacFile);
            }
            pFLAC__stream_decoder_delete(flacFile);
            flacFile = NULL;
        }
    }

    virtual ~flacStream()
    {
        if(flacFile)
        {
            pFLAC__stream_decoder_finish(flacFile);
            pFLAC__stream_decoder_delete(flacFile);
            flacFile = NULL;
        }
    }

private:
    bool InitFlac()
    {
        // We need to decode some data to be able to get the channel count, bit
        // depth, and sample rate. It also ensures the file has FLAC data, as
        // the FLAC__stream_decoder_init_* functions can succeed on non-FLAC
        // Ogg files.
        outLen = 0;
        outTotal = 0;
        while(initialData.size() == 0)
        {
            if(pFLAC__stream_decoder_process_single(flacFile) == false ||
               pFLAC__stream_decoder_get_state(flacFile) == FLAC__STREAM_DECODER_END_OF_STREAM)
                break;
        }

        if(initialData.size() > 0)
            return true;
        return false;
    }

    static FLAC__StreamDecoderWriteStatus WriteCallback(const FLAC__StreamDecoder*, const FLAC__Frame *frame, const FLAC__int32 *const buffer[], void *client_data)
    {
        flacStream *self = static_cast<flacStream*>(client_data);
        ALubyte *data = self->outBytes + self->outTotal;
        ALuint i = 0;

        if(self->format == AL_NONE)
        {
            ALuint bps = frame->header.bits_per_sample;
            if(bps == 24 || bps == 32)
            {
                self->format = GetSampleFormat(frame->header.channels, 32, true);
                if(self->format != AL_NONE)
                {
                    self->useFloat = AL_TRUE;
                    bps = 32;
                }
                else bps = 16;
            }
            if(self->format == AL_NONE)
                self->format = GetSampleFormat(frame->header.channels, bps, false);
            self->blockAlign = frame->header.channels * bps/8;
            self->samplerate = frame->header.sample_rate;
        }

        const ALboolean useFloat = self->useFloat;
        while(self->outTotal < self->outLen && i < frame->header.blocksize)
        {
            for(ALuint c = 0;c < frame->header.channels;c++)
            {
                if(frame->header.bits_per_sample == 8)
                    ((ALubyte*)data)[c] = buffer[c][i]+128;
                else if(frame->header.bits_per_sample == 16)
                    ((ALshort*)data)[c] = buffer[c][i];
                else if(frame->header.bits_per_sample == 24)
                {
                    if(useFloat)
                        ((ALfloat*)data)[c] = buffer[c][i] * (1.0/8388607.0);
                    else
                        ((ALshort*)data)[c] = buffer[c][i]>>8;
                }
                else if(frame->header.bits_per_sample == 32)
                {
                    if(useFloat)
                        ((ALfloat*)data)[c] = buffer[c][i] * (1.0/2147483647.0);
                    else
                        ((ALshort*)data)[c] = buffer[c][i]>>16;
                }
            }
            self->outTotal += self->blockAlign;
            data += self->blockAlign;
            i++;
        }

        if(i < frame->header.blocksize)
        {
            ALuint blocklen = (frame->header.blocksize-i) *
                              self->blockAlign;
            ALuint start = self->initialData.size();

            self->initialData.resize(start+blocklen);
            data = &self->initialData[start];

            do {
                for(ALuint c = 0;c < frame->header.channels;c++)
                {
                    if(frame->header.bits_per_sample == 8)
                        ((ALubyte*)data)[c] = buffer[c][i]+128;
                    else if(frame->header.bits_per_sample == 16)
                        ((ALshort*)data)[c] = buffer[c][i];
                    else if(frame->header.bits_per_sample == 24)
                    {
                        if(useFloat)
                            ((ALfloat*)data)[c] = buffer[c][i] * (1.0/8388607.0);
                        else
                            ((ALshort*)data)[c] = buffer[c][i]>>8;
                    }
                    else if(frame->header.bits_per_sample == 32)
                    {
                        if(useFloat)
                            ((ALfloat*)data)[c] = buffer[c][i] * (1.0/2147483647.0);
                        else
                            ((ALshort*)data)[c] = buffer[c][i]>>16;
                    }
                }
                data += self->blockAlign;
                i++;
            } while(i < frame->header.blocksize);
        }

        return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
    }
    static void MetadataCallback(const FLAC__StreamDecoder*,const FLAC__StreamMetadata*,void*)
    {
    }
    static void ErrorCallback(const FLAC__StreamDecoder*,FLAC__StreamDecoderErrorStatus,void*)
    {
    }

    static FLAC__StreamDecoderReadStatus ReadCallback(const FLAC__StreamDecoder*, FLAC__byte buffer[], size_t *bytes, void *client_data)
    {
        std::istream *stream = static_cast<flacStream*>(client_data)->fstream;
        stream->clear();

        if(*bytes <= 0)
            return FLAC__STREAM_DECODER_READ_STATUS_ABORT;

        stream->read(reinterpret_cast<char*>(buffer), *bytes);
        *bytes = stream->gcount();
        if(*bytes == 0 && stream->eof())
            return FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM;

        return FLAC__STREAM_DECODER_READ_STATUS_CONTINUE;
    }
    static FLAC__StreamDecoderSeekStatus SeekCallback(const FLAC__StreamDecoder*, FLAC__uint64 absolute_byte_offset, void *client_data)
    {
        std::istream *stream = static_cast<flacStream*>(client_data)->fstream;
        stream->clear();

        if(!stream->seekg(absolute_byte_offset))
            return FLAC__STREAM_DECODER_SEEK_STATUS_ERROR;
        return FLAC__STREAM_DECODER_SEEK_STATUS_OK;
    }
    static FLAC__StreamDecoderTellStatus TellCallback(const FLAC__StreamDecoder*, FLAC__uint64 *absolute_byte_offset, void *client_data)
    {
        std::istream *stream = static_cast<flacStream*>(client_data)->fstream;
        stream->clear();

        *absolute_byte_offset = stream->tellg();
        return FLAC__STREAM_DECODER_TELL_STATUS_OK;
    }
    static FLAC__StreamDecoderLengthStatus LengthCallback(const FLAC__StreamDecoder*, FLAC__uint64 *stream_length, void *client_data)
    {
        std::istream *stream = static_cast<flacStream*>(client_data)->fstream;
        stream->clear();

        std::streampos pos = stream->tellg();
        if(stream->seekg(0, std::ios_base::end))
        {
            *stream_length = stream->tellg();
            stream->seekg(pos);
        }

        if(!stream->good())
            return FLAC__STREAM_DECODER_LENGTH_STATUS_ERROR;
        return FLAC__STREAM_DECODER_LENGTH_STATUS_OK;
    }
    static FLAC__bool EofCallback(const FLAC__StreamDecoder*, void *client_data)
    {
        std::istream *stream = static_cast<flacStream*>(client_data)->fstream;
        return (stream->eof()) ? true : false;
    }
};
static DecoderDecl<flacStream> flacStream_decoder;
