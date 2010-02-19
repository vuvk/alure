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

/* Title: Streaming */

#include "config.h"

#include "main.h"

#include <string.h>

#include <algorithm>
#include <vector>
#include <memory>
#include <string>
#include <istream>
#include <fstream>
#include <iostream>


static inline ALuint read_le32(std::istream *file)
{
    ALubyte buffer[4];
    if(!file->read(reinterpret_cast<char*>(buffer), 4)) return 0;
    return buffer[0] | (buffer[1]<<8) | (buffer[2]<<16) | (buffer[3]<<24);
}

static inline ALushort read_le16(std::istream *file)
{
    ALubyte buffer[2];
    if(!file->read(reinterpret_cast<char*>(buffer), 2)) return 0;
    return buffer[0] | (buffer[1]<<8);
}

static inline ALuint read_be32(std::istream *file)
{
    ALubyte buffer[4];
    if(!file->read(reinterpret_cast<char*>(buffer), 4)) return 0;
    return (buffer[0]<<24) | (buffer[1]<<16) | (buffer[2]<<8) | buffer[3];
}

static inline ALushort read_be16(std::istream *file)
{
    ALubyte buffer[2];
    if(!file->read(reinterpret_cast<char*>(buffer), 2)) return 0;
    return (buffer[0]<<8) | buffer[1];
}

static inline ALuint read_be80extended(std::istream *file)
{
    ALubyte buffer[10];
    if(!file->read(reinterpret_cast<char*>(buffer), 10)) return 0;
    ALuint mantissa, last = 0;
    ALubyte exp = buffer[1];
    exp = 30 - exp;
    mantissa = (buffer[2]<<24) | (buffer[3]<<16) | (buffer[4]<<8) | buffer[5];
    while (exp--)
    {
        last = mantissa;
        mantissa >>= 1;
    }
    if((last&1)) mantissa++;
    return mantissa;
}


struct nullStream : public alureStream {
    virtual bool IsValid() { return false; }
    virtual bool GetFormat(ALenum*,ALuint*,ALuint*) { return false; }
    virtual ALuint GetData(ALubyte*,ALuint) { return 0; }
    virtual bool Rewind() { return false; }
    nullStream(){}
};


struct customStream : public alureStream {
    void *usrFile;
    ALenum format;
    ALuint samplerate;
    ALuint blockAlign;
    MemDataInfo memInfo;

    UserCallbacks cb;

    virtual bool IsValid()
    { return usrFile != NULL; }

    virtual bool GetFormat(ALenum *format, ALuint *frequency, ALuint *blockalign)
    {
        if(this->format == 0)
        {
            if(!cb.get_fmt ||
               !cb.get_fmt(usrFile, &this->format, &samplerate, &blockAlign))
                return false;
        }

        *format = this->format;
        *frequency = samplerate;
        *blockalign = blockAlign;
        return true;
    }

    virtual ALuint GetData(ALubyte *data, ALuint bytes)
    {
        bytes -= bytes%blockAlign;
        return cb.decode(usrFile, data, bytes);
    }

    virtual bool Rewind()
    {
        if(cb.rewind && cb.rewind(usrFile))
            return true;

        SetError("Rewind failed");
        return false;
    }

    customStream(const char *fname, const UserCallbacks &callbacks)
      : usrFile(NULL), format(0), samplerate(0), blockAlign(0), cb(callbacks)
    {
        if(cb.open_file)
            usrFile = cb.open_file(fname);
    }
    customStream(const MemDataInfo &memData, const UserCallbacks &callbacks)
      : usrFile(NULL), format(0), samplerate(0), blockAlign(0),
        memInfo(memData), cb(callbacks)
    {
        if(cb.open_mem)
            usrFile = cb.open_mem(memInfo.Data, memInfo.Length);
    }
    customStream(void *userdata, ALenum fmt, ALuint srate, const UserCallbacks &callbacks)
      : usrFile(userdata), format(fmt), samplerate(srate), blockAlign(1), cb(callbacks)
    { }

    virtual ~customStream()
    {
        if(cb.close && usrFile)
            cb.close(usrFile);
        usrFile = NULL;
    }
};

struct wavStream : public alureStream {
    ALenum format;
    int samplerate;
    int blockAlign;
    int sampleSize;
    long dataStart;
    long dataLen;
    size_t remLen;

    virtual bool IsValid()
    { return (dataStart > 0 && format != AL_NONE); }

    virtual bool GetFormat(ALenum *format, ALuint *frequency, ALuint *blockalign)
    {
        *format = this->format;
        *frequency = samplerate;
        *blockalign = blockAlign;
        return true;
    }

    virtual ALuint GetData(ALubyte *data, ALuint bytes)
    {
        static const union {
            int val;
            char b[sizeof(int)];
        } endian = { 1 };

        std::streamsize rem = ((remLen >= bytes) ? bytes : remLen) / blockAlign;
        fstream->read(reinterpret_cast<char*>(data), rem*blockAlign);

        std::streamsize got = fstream->gcount();
        got -= got%blockAlign;
        remLen -= got;

        if(endian.b[0] == 0 && sampleSize > 1)
        {
            if(sampleSize == 2)
                for(std::streamsize i = 0;i < got;i+=2)
                {
                    ALubyte tmp = data[i];
                    data[i] = data[i+1];
                    data[i+1] = tmp;
                }
            else if(sampleSize == 4)
                for(std::streamsize i = 0;i < got;i+=4)
                {
                    ALubyte tmp = data[i];
                    data[i] = data[i+3];
                    data[i+3] = tmp;
                    tmp = data[i+1];
                    data[i+1] = data[i+2];
                    data[i+2] = tmp;
                }
        }

        return got;
    }

    virtual bool Rewind()
    {
        fstream->clear();
        if(fstream->seekg(dataStart))
        {
            remLen = dataLen;
            return true;
        }

        SetError("Seek failed");
        return false;
    }

    wavStream(std::istream *_fstream)
      : alureStream(_fstream), format(0), dataStart(0)
    { Init(); }

    virtual ~wavStream()
    { }

private:
    void Init()
    {
        ALubyte buffer[25];
        int length;

        if(!fstream->read(reinterpret_cast<char*>(buffer), 12) ||
           memcmp(buffer, "RIFF", 4) != 0 || memcmp(buffer+8, "WAVE", 4) != 0)
            return;

        while(!dataStart || format == AL_NONE)
        {
            char tag[4];
            if(!fstream->read(tag, 4))
                break;

            /* read chunk length */
            length = read_le32(fstream);

            if(memcmp(tag, "fmt ", 4) == 0 && length >= 16)
            {
                /* Data type (should be 1 for PCM data) */
                int type = read_le16(fstream);
                if(type != 1)
                    break;

                /* mono or stereo data */
                int channels = read_le16(fstream);

                /* sample frequency */
                samplerate = read_le32(fstream);

                /* skip four bytes */
                fstream->ignore(4);

                /* bytes per block */
                blockAlign = read_le16(fstream);
                if(blockAlign == 0)
                    break;

                /* bits per sample */
                sampleSize = read_le16(fstream) / 8;

                format = alureGetSampleFormat(channels, sampleSize*8, 0);

                length -= 16;
            }
            else if(memcmp(tag, "data", 4) == 0)
            {
                dataStart = fstream->tellg();
                dataLen = remLen = length;
            }

            fstream->seekg(length, std::ios_base::cur);
        }

        if(dataStart > 0 && format != AL_NONE)
            fstream->seekg(dataStart);
    }
};

struct aiffStream : public alureStream {
    ALenum format;
    int samplerate;
    int blockAlign;
    int sampleSize;
    long dataStart;
    long dataLen;
    size_t remLen;

    virtual bool IsValid()
    { return (dataStart > 0 && format != AL_NONE); }

    virtual bool GetFormat(ALenum *format, ALuint *frequency, ALuint *blockalign)
    {
        *format = this->format;
        *frequency = samplerate;
        *blockalign = blockAlign;
        return true;
    }

    virtual ALuint GetData(ALubyte *data, ALuint bytes)
    {
        static const union {
            int val;
            char b[sizeof(int)];
        } endian = { 1 };

        std::streamsize rem = ((remLen >= bytes) ? bytes : remLen) / blockAlign;
        fstream->read(reinterpret_cast<char*>(data), rem*blockAlign);

        std::streamsize got = fstream->gcount();
        got -= got%blockAlign;
        remLen -= got;

        if(endian.b[0] == 1 && sampleSize > 1)
        {
            if(sampleSize == 2)
                for(std::streamsize i = 0;i < got;i+=2)
                {
                    ALubyte tmp = data[i];
                    data[i] = data[i+1];
                    data[i+1] = tmp;
                }
            else if(sampleSize == 4)
                for(std::streamsize i = 0;i < got;i+=4)
                {
                    ALubyte tmp = data[i];
                    data[i] = data[i+3];
                    data[i+3] = tmp;
                    tmp = data[i+1];
                    data[i+1] = data[i+2];
                    data[i+2] = tmp;
                }
        }

        return got;
    }

    virtual bool Rewind()
    {
        fstream->clear();
        if(fstream->seekg(dataStart))
        {
            remLen = dataLen;
            return true;
        }

        SetError("Seek failed");
        return false;
    }

    aiffStream(std::istream *_fstream)
      : alureStream(_fstream), format(0), dataStart(0)
    { Init(); }

    virtual ~aiffStream()
    { }

private:
    void Init()
    {
        ALubyte buffer[25];
        int length;

        if(!fstream->read(reinterpret_cast<char*>(buffer), 12) ||
           memcmp(buffer, "FORM", 4) != 0 || memcmp(buffer+8, "AIFF", 4) != 0)
            return;

        while(!dataStart || format == AL_NONE)
        {
            char tag[4];
            if(!fstream->read(tag, 4))
                break;

            /* read chunk length */
            length = read_be32(fstream);

            if(memcmp(tag, "COMM", 4) == 0 && length >= 18)
            {
                /* mono or stereo data */
                int channels = read_be16(fstream);

                /* number of sample frames */
                fstream->ignore(4);

                /* bits per sample */
                sampleSize = read_be16(fstream) / 8;

                /* sample frequency */
                samplerate = read_be80extended(fstream);

                /* block alignment */
                blockAlign = channels * sampleSize;

                format = alureGetSampleFormat(channels, sampleSize*8, 0);

                length -= 18;
            }
            else if(memcmp(tag, "SSND", 4) == 0)
            {
                dataStart = fstream->tellg();
                dataStart += 8;
                dataLen = remLen = length - 8;
            }

            fstream->seekg(length, std::ios_base::cur);
        }

        if(dataStart > 0 && format != AL_NONE)
            fstream->seekg(dataStart);
    }
};

#ifdef HAS_SNDFILE
struct sndStream : public alureStream {
    SNDFILE *sndFile;
    SF_INFO sndInfo;

    virtual bool IsValid()
    { return sndFile != NULL; }

    virtual bool GetFormat(ALenum *format, ALuint *frequency, ALuint *blockalign)
    {
        *format = alureGetSampleFormat(sndInfo.channels, 16, 0);
        *frequency = sndInfo.samplerate;
        *blockalign = sndInfo.channels*2;
        return true;
    }

    virtual ALuint GetData(ALubyte *data, ALuint bytes)
    {
        const ALuint frameSize = 2*sndInfo.channels;
        return sf_readf_short(sndFile, (short*)data, bytes/frameSize) * frameSize;
    }

    virtual bool Rewind()
    {
        if(sf_seek(sndFile, 0, SEEK_SET) != -1)
            return true;

        SetError("Seek failed");
        return false;
    }

    sndStream(std::istream *_fstream)
      : alureStream(_fstream), sndFile(NULL)
    {
        memset(&sndInfo, 0, sizeof(sndInfo));

        static SF_VIRTUAL_IO streamIO = {
            get_filelen, seek,
            read, write, tell
        };
        sndFile = sf_open_virtual(&streamIO, SFM_READ, &sndInfo, fstream);
    }

    virtual ~sndStream()
    {
        if(sndFile)
            sf_close(sndFile);
        sndFile = NULL;
    }

private:
    // libSndFile iostream callbacks
    static sf_count_t get_filelen(void *user_data)
    {
        std::istream *stream = static_cast<std::istream*>(user_data);
        stream->clear();

        std::streampos len = -1;
        std::streampos pos = stream->tellg();
        if(stream->seekg(0, std::ios_base::end))
        {
            len = stream->tellg();
            stream->seekg(pos);
        }

        return len;
    }

    static sf_count_t seek(sf_count_t offset, int whence, void *user_data)
    {
        std::istream *stream = static_cast<std::istream*>(user_data);
        stream->clear();

        if(whence == SEEK_CUR)
            stream->seekg(offset, std::ios_base::cur);
        else if(whence == SEEK_SET)
            stream->seekg(offset, std::ios_base::beg);
        else if(whence == SEEK_END)
            stream->seekg(offset, std::ios_base::end);
        else
            return -1;

        return stream->tellg();
    }

    static sf_count_t read(void *ptr, sf_count_t count, void *user_data)
    {
        std::istream *stream = static_cast<std::istream*>(user_data);
        stream->clear();
        stream->read(static_cast<char*>(ptr), count);
        return stream->gcount();
    }

    static sf_count_t write(const void*, sf_count_t, void*)
    { return -1; }

    static sf_count_t tell(void *user_data)
    {
        std::istream *stream = static_cast<std::istream*>(user_data);
        stream->clear();
        return stream->tellg();
    }
};
#else
struct sndStream : public nullStream {
    sndStream(std::istream*){}
};
#endif

#ifdef HAS_VORBISFILE
struct oggStream : public alureStream {
    OggVorbis_File *oggFile;
    int oggBitstream;

    virtual bool IsValid()
    { return oggFile != NULL; }

    virtual bool GetFormat(ALenum *format, ALuint *frequency, ALuint *blockalign)
    {
        vorbis_info *info = ov_info(oggFile, -1);
        if(!info) return false;

        *format = alureGetSampleFormat(info->channels, 16, 0);
        *frequency = info->rate;
        *blockalign = info->channels*2;
        return true;
    }

    virtual ALuint GetData(ALubyte *data, ALuint bytes)
    {
        static const union {
            short s;
            char b[sizeof(short)];
        } endian = { 0x0100 };

        vorbis_info *info = ov_info(oggFile, -1);
        if(!info) return 0;

        ALuint blockAlign = info->channels*2;
        bytes -= bytes%blockAlign;

        int got = 0;
        while(bytes > 0)
        {
            int res = ov_read(oggFile, (char*)&data[got], bytes, endian.b[0], 2, 1, &oggBitstream);
            if(res <= 0)
                break;
            bytes -= res;
            got += res;
        }
        return got;
    }

    virtual bool Rewind()
    {
        if(ov_pcm_seek(oggFile, 0) == 0)
            return true;

        SetError("Seek failed");
        return false;
    }

    oggStream(std::istream *_fstream)
      : alureStream(_fstream), oggFile(NULL), oggBitstream(0)
    {
        const ov_callbacks streamIO = {
            read, seek, NULL, tell
        };

        oggFile = new OggVorbis_File;
        if(ov_open_callbacks(this, oggFile, NULL, 0, streamIO) != 0)
        {
            delete oggFile;
            oggFile = NULL;
        }
    }

    virtual ~oggStream()
    {
        if(oggFile)
        {
            ov_clear(oggFile);
            delete oggFile;
        }
    }

private:
    // libVorbisFile iostream callbacks
    static int seek(void *user_data, ogg_int64_t offset, int whence)
    {
        oggStream *This = static_cast<oggStream*>(user_data);
        This->fstream->clear();

        if(whence == SEEK_CUR)
            This->fstream->seekg(offset, std::ios_base::cur);
        else if(whence == SEEK_SET)
            This->fstream->seekg(offset, std::ios_base::beg);
        else if(whence == SEEK_END)
            This->fstream->seekg(offset, std::ios_base::end);
        else
            return -1;

        return This->fstream->tellg();
    }

    static size_t read(void *ptr, size_t size, size_t nmemb, void *user_data)
    {
        oggStream *This = static_cast<oggStream*>(user_data);
        This->fstream->clear();

        This->fstream->read(static_cast<char*>(ptr), nmemb*size);
        size_t ret = This->fstream->gcount();
        return ret/size;
    }

    static long tell(void *user_data)
    {
        oggStream *This = static_cast<oggStream*>(user_data);
        This->fstream->clear();
        return This->fstream->tellg();
    }
};
#else
struct oggStream : public nullStream {
    oggStream(std::istream*){}
};
#endif

#ifdef HAS_FLAC
struct flacStream : public alureStream {
    FLAC__StreamDecoder *flacFile;
    ALenum format;
    ALuint samplerate;
    ALuint blockAlign;
    ALboolean useFloat;

    std::vector<ALubyte> initialData;

    ALubyte *outBytes;
    ALuint outLen;
    ALuint outTotal;

    virtual bool IsValid()
    { return flacFile != NULL; }

    virtual bool GetFormat(ALenum *format, ALuint *frequency, ALuint *blockalign)
    {
        *format = this->format;
        *frequency = samplerate;
        *blockalign = blockAlign;
        return true;
    }

    virtual ALuint GetData(ALubyte *data, ALuint bytes)
    {
        bytes -= bytes%blockAlign;

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
            if(FLAC__stream_decoder_process_single(flacFile) == false ||
               FLAC__stream_decoder_get_state(flacFile) == FLAC__STREAM_DECODER_END_OF_STREAM)
                break;
        }

        return outTotal;
    }

    virtual bool Rewind()
    {
        if(FLAC__stream_decoder_seek_absolute(flacFile, 0) != false)
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
        flacFile = FLAC__stream_decoder_new();
        if(flacFile)
        {
            if(FLAC__stream_decoder_init_stream(flacFile, ReadCallback, SeekCallback, TellCallback, LengthCallback, EofCallback, WriteCallback, MetadataCallback, ErrorCallback, this) == FLAC__STREAM_DECODER_INIT_STATUS_OK)
            {
                if(InitFlac())
                {
                    // all ok
                    return;
                }

                FLAC__stream_decoder_finish(flacFile);
            }
            FLAC__stream_decoder_delete(flacFile);
            flacFile = NULL;
        }
    }

    virtual ~flacStream()
    {
        if(flacFile)
        {
            FLAC__stream_decoder_finish(flacFile);
            FLAC__stream_decoder_delete(flacFile);
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
            if(FLAC__stream_decoder_process_single(flacFile) == false ||
               FLAC__stream_decoder_get_state(flacFile) == FLAC__STREAM_DECODER_END_OF_STREAM)
                break;
        }

        if(initialData.size() > 0)
            return true;
        return false;
    }

    static FLAC__StreamDecoderWriteStatus WriteCallback(const FLAC__StreamDecoder*, const FLAC__Frame *frame, const FLAC__int32 *const buffer[], void *client_data)
    {
        flacStream *This = static_cast<flacStream*>(client_data);
        ALubyte *data = This->outBytes + This->outTotal;
        ALuint i = 0;

        if(This->format == AL_NONE)
        {
            ALuint bps = frame->header.bits_per_sample;
            if(bps == 24 || bps == 32)
            {
                This->format = alureGetSampleFormat(frame->header.channels, 0, 32);
                if(This->format != AL_NONE)
                {
                    This->useFloat = AL_TRUE;
                    bps = 32;
                }
                else bps = 16;
            }
            if(This->format == AL_NONE)
                This->format = alureGetSampleFormat(frame->header.channels, bps, 0);
            This->blockAlign = frame->header.channels * bps/8;
            This->samplerate = frame->header.sample_rate;
        }

        const ALboolean useFloat = This->useFloat;
        while(This->outTotal < This->outLen && i < frame->header.blocksize)
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
                        ((ALfloat*)data)[c] = ((buffer[c][i]>=0) ?
                                               buffer[c][i]/(float)0x7FFFFF :
                                               buffer[c][i]/(float)0x800000);
                    else
                        ((ALshort*)data)[c] = buffer[c][i]>>8;
                }
                else if(frame->header.bits_per_sample == 32)
                {
                    if(useFloat)
                        ((ALfloat*)data)[c] = ((buffer[c][i]>=0) ?
                                               buffer[c][i]/(float)0x7FFFFFFF :
                                               buffer[c][i]/(float)0x80000000u);
                    else
                        ((ALshort*)data)[c] = buffer[c][i]>>16;
                }
            }
            This->outTotal += This->blockAlign;
            data += This->blockAlign;
            i++;
        }

        if(i < frame->header.blocksize)
        {
            ALuint blocklen = (frame->header.blocksize-i) *
                              This->blockAlign;
            ALuint start = This->initialData.size();

            This->initialData.resize(start+blocklen);
            data = &This->initialData[start];

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
                            ((ALfloat*)data)[c] = ((buffer[c][i]>=0) ?
                                                   buffer[c][i]/(float)0x7FFFFF :
                                                   buffer[c][i]/(float)0x800000);
                        else
                            ((ALshort*)data)[c] = buffer[c][i]>>8;
                    }
                    else if(frame->header.bits_per_sample == 32)
                    {
                        if(useFloat)
                            ((ALfloat*)data)[c] = ((buffer[c][i]>=0) ?
                                                   buffer[c][i]/(float)0x7FFFFFFF :
                                                   buffer[c][i]/(float)0x80000000u);
                        else
                            ((ALshort*)data)[c] = buffer[c][i]>>16;
                    }
                }
                data += This->blockAlign;
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
#else
struct flacStream : public nullStream {
    flacStream(std::istream*){}
};
#endif


#ifdef HAS_MPG123
struct mp3Stream : public alureStream {
    mpg123_handle *mp3File;
    long samplerate;
    int channels;
    std::istream *fstream;

    virtual bool IsValid()
    { return mp3File != NULL; }

    virtual bool GetFormat(ALenum *format, ALuint *frequency, ALuint *blockalign)
    {
        ALenum fmt = alureGetSampleFormat(channels, 16, 0);

        *format = fmt;
        *frequency = samplerate;
        *blockalign = channels*2;
        return true;
    }

    virtual ALuint GetData(ALubyte *data, ALuint bytes)
    {
        const ALuint blockAlign = channels*2;
        bytes -= bytes%blockAlign;

        ALuint amt = 0;
        do {
            size_t got = 0;
            int ret = mpg123_read(mp3File, data, bytes, &got);

            bytes -= got;
            data += got;
            amt += got;

            if(ret == MPG123_NEED_MORE)
            {
                unsigned char data[4096];
                fstream->read((char*)data, sizeof(data));
                std::streamsize insize = fstream->gcount();;
                if(insize > 0 &&
                   mpg123_decode(mp3File, data, insize, NULL, 0, NULL) == MPG123_OK)
                    continue;
            }

            return amt;
        } while(1);
    }

    virtual bool Rewind()
    {
        fstream->clear();
        std::istream::pos_type oldpos = fstream->tellg();
        fstream->seekg(0);

        mpg123_handle *newFile = mpg123_new(NULL, NULL);
        if(mpg123_open_feed(newFile) == MPG123_OK)
        {
            unsigned char data[4096];
            long newrate;
            int newchans;
            int ret, enc;

            ALuint amt, total = 0;
            do {
                fstream->read((char*)data, sizeof(data));
                amt = fstream->gcount();
                if(amt == 0)  break;
                total += amt;
                ret = mpg123_decode(newFile, data, amt, NULL, 0, NULL);
            } while(ret == MPG123_NEED_MORE && total < 64*1024);

            if(ret == MPG123_NEW_FORMAT &&
               mpg123_getformat(newFile, &newrate, &newchans, &enc) == MPG123_OK)
            {
                if(newrate == samplerate && newchans == channels &&
                   (enc == MPG123_ENC_SIGNED_16 ||
                    (mpg123_format_none(newFile) == MPG123_OK &&
                     mpg123_format(newFile, samplerate, channels, MPG123_ENC_SIGNED_16) == MPG123_OK)))
                {
                    // All OK
                    mpg123_delete(mp3File);
                    mp3File = newFile;
                    return true;
                }
            }
            mpg123_delete(newFile);
        }

        fstream->seekg(oldpos);
        SetError("Restart failed");
        return false;
    }

    mp3Stream(std::istream *_fstream)
      : mp3File(NULL), fstream(_fstream)
    {
        mp3File = mpg123_new(NULL, NULL);
        if(mpg123_open_feed(mp3File) == MPG123_OK)
        {
            unsigned char data[4096];
            int ret, enc;

            ALuint amt, total = 0;
            do {
                fstream->read((char*)data, sizeof(data));
                amt = fstream->gcount();
                if(amt == 0)  break;
                total += amt;
                ret = mpg123_decode(mp3File, data, amt, NULL, 0, NULL);
            } while(ret == MPG123_NEED_MORE && total < 64*1024);

            if(ret == MPG123_NEW_FORMAT &&
               mpg123_getformat(mp3File, &samplerate, &channels, &enc) == MPG123_OK)
            {
                if(enc == MPG123_ENC_SIGNED_16 ||
                   (mpg123_format_none(mp3File) == MPG123_OK &&
                    mpg123_format(mp3File, samplerate, channels, MPG123_ENC_SIGNED_16) == MPG123_OK))
                {
                    // All OK
                    return;
                }
            }
        }
        mpg123_delete(mp3File);
        mp3File = NULL;
    }

    virtual ~mp3Stream()
    {
        if(mp3File)
            mpg123_delete(mp3File);
        mp3File = NULL;
    }
};
#else
struct mp3Stream : public nullStream {
    mp3Stream(std::istream*){}
};
#endif


#ifdef HAS_DUMB
struct dumbStream : public alureStream {
    DUMBFILE_SYSTEM vfs;
    DUMBFILE *dumbFile;
    DUH *duh;
    DUH_SIGRENDERER *renderer;
    std::vector<sample_t> sampleBuf;
    ALuint lastOrder;
    int prevSpeed;
    ALenum format;

    virtual bool IsValid()
    { return renderer != NULL; }

    virtual bool GetFormat(ALenum *fmt, ALuint *frequency, ALuint *blockalign)
    {
        if(format == AL_NONE)
        {
            format = alureGetSampleFormat(2, 0, 32);
            if(format == AL_NONE)
                format = AL_FORMAT_STEREO16;
        }
        *fmt = format;
        *frequency = 65536;
        *blockalign = 2 * ((format==AL_FORMAT_STEREO16) ? sizeof(ALshort) :
                                                          sizeof(ALfloat));
        return true;
    }

    virtual ALuint GetData(ALubyte *data, ALuint bytes)
    {
        ALuint ret = 0;

        if(dumb_it_sr_get_speed(duh_get_it_sigrenderer(renderer)) == 0)
            return 0;

        ALuint sample_count = bytes / ((format==AL_FORMAT_STEREO16) ?
                                       sizeof(ALshort) : sizeof(ALfloat));

        sampleBuf.resize(sample_count);
        sample_t *samples = &sampleBuf[0];

        dumb_silence(samples, sample_count);
        ret = duh_sigrenderer_generate_samples(renderer, 1.0f, 1.0f, sample_count/2, &samples);
        ret *= 2;
        if(format == AL_FORMAT_STEREO16)
        {
            for(ALuint i = 0;i < ret;i++)
                ((ALshort*)data)[i] = clamp(samples[i]>>8, -32768, 32767);
        }
        else
        {
            for(ALuint i = 0;i < ret;i++)
                ((ALfloat*)data)[i] = ((samples[i]>=0) ?
                                       samples[i]/(float)0x7FFFFF :
                                       samples[i]/(float)0x800000);
        }
        ret *= ((format==AL_FORMAT_STEREO16) ? sizeof(ALshort) : sizeof(ALfloat));

        return ret;
    }

    virtual bool Rewind()
    {
        if(prevSpeed)
        {
            // If a previous speed was recorded, the stream tried to loop. So
            // let it loop on a rewind request.
            dumb_it_sr_set_speed(duh_get_it_sigrenderer(renderer), prevSpeed);
            prevSpeed = 0;
            return true;
        }

        // Else, no loop point. Restart from scratch.
        DUH_SIGRENDERER *newrenderer;
        newrenderer = (lastOrder ? dumb_it_start_at_order(duh, 2, lastOrder) :
                                   duh_start_sigrenderer(duh, 0, 2, 0));
        if(!newrenderer)
        {
            SetError("Could start renderer");
            return false;
        }
        duh_end_sigrenderer(renderer);
        renderer = newrenderer;
        return true;
    }

    virtual bool SetOrder(ALuint order)
    {
        DUH_SIGRENDERER *newrenderer = dumb_it_start_at_order(duh, 2, order);
        if(!newrenderer)
        {
            SetError("Could not set order");
            return false;
        }
        duh_end_sigrenderer(renderer);
        renderer = newrenderer;

        lastOrder = order;
        return true;
    }

    dumbStream(std::istream *_fstream)
      : alureStream(_fstream), dumbFile(NULL), duh(NULL), renderer(NULL),
        lastOrder(0), prevSpeed(0), format(AL_NONE)
    {
        DUH* (*funcs[])(DUMBFILE*) = {
            dumb_read_it_quick,
            dumb_read_xm_quick,
            dumb_read_s3m_quick,
            dumb_read_mod_quick,
            NULL
        };

        vfs.open = NULL;
        vfs.skip = skip;
        vfs.getc = read_char;
        vfs.getnc = read;
        vfs.close = NULL;

        for(size_t i = 0;funcs[i];i++)
        {
            dumbFile = dumbfile_open_ex(this, &vfs);
            if(dumbFile)
            {
                duh = funcs[i](dumbFile);
                if(duh)
                {
                    renderer = duh_start_sigrenderer(duh, 0, 2, 0);
                    if(renderer)
                    {
                        dumb_it_set_loop_callback(duh_get_it_sigrenderer(renderer), loop_cb, this);
                        break;
                    }

                    unload_duh(duh);
                    duh = NULL;
                }

                dumbfile_close(dumbFile);
                dumbFile = NULL;
            }
            fstream->clear();
            fstream->seekg(0);
        }
    }

    virtual ~dumbStream()
    {
        duh_end_sigrenderer(renderer);
        renderer = NULL;

        unload_duh(duh);
        duh = NULL;

        if(dumbFile)
            dumbfile_close(dumbFile);
        dumbFile = NULL;
    }

private:
    // DUMBFILE iostream callbacks
    static int skip(void *user_data, long offset)
    {
        dumbStream *This = static_cast<dumbStream*>(user_data);
        This->fstream->clear();

        if(This->fstream->seekg(offset, std::ios_base::cur))
            return 0;
        return -1;
    }

    static long read(char *ptr, long size, void *user_data)
    {
        dumbStream *This = static_cast<dumbStream*>(user_data);
        This->fstream->clear();

        This->fstream->read(static_cast<char*>(ptr), size);
        return This->fstream->gcount();
    }

    static int read_char(void *user_data)
    {
        dumbStream *This = static_cast<dumbStream*>(user_data);
        This->fstream->clear();

        unsigned char ret;
        This->fstream->read(reinterpret_cast<char*>(&ret), 1);
        if(This->fstream->gcount() > 0)
            return ret;
        return -1;
    }

    static int loop_cb(void *user_data)
    {
        dumbStream *This = static_cast<dumbStream*>(user_data);
        This->prevSpeed = dumb_it_sr_get_speed(duh_get_it_sigrenderer(This->renderer));
        dumb_it_sr_set_speed(duh_get_it_sigrenderer(This->renderer), 0);
        return 0;
    }
};
#else
struct dumbStream : public nullStream {
    dumbStream(std::istream*){}
};
#endif


#ifdef HAS_TIMIDITY
struct midiStream : public alureStream {
    int pcmFile;
    pid_t cpid;
    int initialByte;

    static const ALuint Freq = 48000;

    virtual bool IsValid()
    { return cpid > 0; }

    virtual bool GetFormat(ALenum *fmt, ALuint *frequency, ALuint *blockalign)
    {
        *fmt = AL_FORMAT_STEREO16;
        *frequency = Freq;
        *blockalign = 4;
        return true;
    }

    virtual ALuint GetData(ALubyte *data, ALuint bytes)
    {
        ALuint total = 0;
        if(initialByte != -1 && bytes > 0)
        {
            *(data++) = initialByte&0xff;
            bytes--;
            total++;
            initialByte = -1;
        }
        while(bytes > 0)
        {
            ssize_t got;
            while((got=read(pcmFile, data, bytes)) == -1 && errno == EINTR)
                ;
            if(got <= 0)
                break;

            data += got;
            bytes -= got;
            total += got;
        }
        return total;
    }

    virtual bool Rewind()
    {
        fstream->clear();
        fstream->seekg(0);

        int _pcmFile; pid_t _cpid;
        if(!StartStream(_pcmFile, _cpid))
        {
            SetError("Failed to restart timidity");
            return false;
        }

        kill(cpid, SIGTERM);
        waitpid(cpid, NULL, 0);
        cpid = _cpid;

        close(pcmFile);
        pcmFile = _pcmFile;

        return true;
    }

    midiStream(std::istream *_fstream)
      : alureStream(_fstream), pcmFile(-1), cpid(-1), initialByte(-1)
    {
        StartStream(pcmFile, cpid);
    }

    virtual ~midiStream()
    {
        if(cpid > 0)
        {
            kill(cpid, SIGTERM);
            waitpid(cpid, NULL, 0);
            cpid = -1;
        }
        if(pcmFile != -1)
            close(pcmFile);
        pcmFile = -1;
    }

private:
    bool StartStream(int &pcmFile, pid_t &cpid)
    {
        char hdr[4];
        std::vector<ALubyte> midiData;

        fstream->read(hdr, sizeof(hdr));
        if(fstream->gcount() != sizeof(hdr))
            return false;

        if(memcmp(hdr, "MThd", 4) == 0)
        {
            char ch;
            for(size_t i = 0;i < sizeof(hdr);i++)
                midiData.push_back(hdr[i]);
            while(fstream->get(ch))
                midiData.push_back(ch);
        }
        else if(memcmp(hdr, "MUS\x1a", sizeof(hdr)) == 0)
        {
            if(!ConvertMUS(midiData))
                return false;
        }
        else
            return false;

        int midPipe[2], pcmPipe[2];
        if(pipe(midPipe) == -1)
            return false;
        if(pipe(pcmPipe) == -1)
        {
            close(midPipe[0]);
            close(midPipe[1]);
            return false;
        }

        pid_t pid = fork();
        if(pid < 0)
        {
            close(midPipe[0]);
            close(midPipe[1]);
            close(pcmPipe[0]);
            close(pcmPipe[1]);
            return false;
        }

        if(pid == 0)
        {
            if(dup2(midPipe[0], STDIN_FILENO) != -1 &&
               dup2(pcmPipe[1], STDOUT_FILENO) != -1)
            {
                close(midPipe[0]);
                close(midPipe[1]);
                close(pcmPipe[0]);
                close(pcmPipe[1]);

                execlp("timidity","timidity","-","-idqq","-Or1sl","-s","48000",
                       "-o", "-", NULL);
            }
            _exit(1);
        }

        close(midPipe[0]); midPipe[0] = -1;
        close(pcmPipe[1]); pcmPipe[1] = -1;

        const ALubyte *cur = &midiData[0];
        size_t rem = midiData.size();
        do {
            ssize_t wrote = write(midPipe[1], cur, rem);
            if(wrote < 0)
            {
                if(errno == EINTR)
                    continue;
                break;
            }
            cur += wrote;
            rem -= wrote;
        } while(rem > 0);
        close(midPipe[1]); midPipe[1] = -1;

        ALubyte ch = 0;
        ssize_t got;
        while((got=read(pcmPipe[0], &ch, 1)) == -1 && errno == EINTR)
            ;
        if(got != 1)
        {
            kill(pid, SIGTERM);
            waitpid(pid, NULL, 0);
            close(pcmPipe[0]);
            return false;
        }

        initialByte = ch;
        pcmFile = pcmPipe[0];
        cpid = pid;
        return true;
    }

    alureUInt64 ReadVarLen()
    {
        alureUInt64 val = 0;
        char ch;

        do {
            if(!fstream->get(ch))
                break;
            val = (val<<7) | (ch&0x7f);
        } while((ch&0x80));

        return val;
    }

    void WriteVarLen(std::vector<ALubyte> &out, alureUInt64 val)
    {
        alureUInt64 buffer = val&0x7f;
        while((val>>=7) > 0)
            buffer = (buffer<<8) | 0x80 | (val&0x7f);
        while(1)
        {
            out.push_back(buffer&0xff);
            if(!(buffer&0x80))
                break;
            buffer >>= 8;
        }
    }

    static const ALubyte MIDI_SYSEX      = 0xF0;  // SysEx begin
    static const ALubyte MIDI_SYSEXEND   = 0xF7;  // SysEx end
    static const ALubyte MIDI_META       = 0xFF;  // Meta event begin
    static const ALubyte MIDI_META_TEMPO = 0x51;
    static const ALubyte MIDI_META_EOT   = 0x2F;  // End-of-track
    static const ALubyte MIDI_META_SSPEC = 0x7F;  // System-specific event

    static const ALubyte MIDI_NOTEOFF    = 0x80;  // + note + velocity
    static const ALubyte MIDI_NOTEON     = 0x90;  // + note + velocity
    static const ALubyte MIDI_POLYPRESS  = 0xA0;  // + pressure (2 bytes)
    static const ALubyte MIDI_CTRLCHANGE = 0xB0;  // + ctrlr + value
    static const ALubyte MIDI_PRGMCHANGE = 0xC0;  // + new patch
    static const ALubyte MIDI_CHANPRESS  = 0xD0;  // + pressure (1 byte)
    static const ALubyte MIDI_PITCHBEND  = 0xE0;  // + pitch bend (2 bytes)

    static const ALubyte MUS_EVENT_CHANNEL_MASK = 0x0F;
    static const ALubyte MUS_EVENT_DELTA_MASK   = 0x80;

    static const ALubyte MUS_NOTEOFF    = 0x00;
    static const ALubyte MUS_NOTEON     = 0x10;
    static const ALubyte MUS_PITCHBEND  = 0x20;
    static const ALubyte MUS_SYSEVENT   = 0x30;
    static const ALubyte MUS_CTRLCHANGE = 0x40;
    static const ALubyte MUS_SCOREEND   = 0x60;

    bool ConvertMUS(std::vector<ALubyte> &midiData)
    {
        static const ALubyte CtrlTranslate[15] = {
            0,   // program change
            0,   // bank select
            1,   // modulation pot
            7,   // volume
            10,  // pan pot
            11,  // expression pot
            91,  // reverb depth
            93,  // chorus depth
            64,  // sustain pedal
            67,  // soft pedal
            120, // all sounds off
            123, // all notes off
            126, // mono
            127, // poly
            121  // reset all controllers
        };

        static const ALubyte MIDIhead[22] = {
            'M','T','h','d', 0, 0, 0, 6,
            0, 0,  // format 0: only one track
            0, 1,  // yes, there is really only one track
            0, 70, // 70 divisions
            'M','T','r','k', 0xFF, 0xFF, 0xFF, 0xFF
        };

        // The MUS\x1a ID was already read and verified
        ALushort songLen = read_le16(fstream);
        ALushort songStart = read_le16(fstream);
        ALushort numChans = read_le16(fstream);

        // Sanity check the MUS file's channel count
        if(numChans > 15)
            return false;

        fstream->seekg(songStart);
        std::streamsize maxmus_p = songLen;
        maxmus_p += fstream->tellg();

        ALubyte chanVel[16];
        for(size_t i = 0;i < 16;i++)
            chanVel[i] = 100;

        bool firstUse[16];
        for(size_t i = 0;i < 16;i++)
            firstUse[i] = true;

        alureUInt64 deltaTime = 0;
        ALubyte event = 0;
        ALubyte status = 0;

        // Setup header
        for(size_t i = 0;i < sizeof(MIDIhead);i++)
            midiData.push_back(MIDIhead[i]);

        // The first event sets the tempo to 500,000 microsec/quarter note
        midiData.push_back(0);
        midiData.push_back(MIDI_META | 0);
        midiData.push_back(MIDI_META_TEMPO | 0);
        midiData.push_back(3);
        midiData.push_back(0x07);
        midiData.push_back(0xA1);
        midiData.push_back(0x20);

        while(fstream->good() && fstream->tellg() < maxmus_p && event != MUS_SCOREEND)
        {
            event = fstream->get();

            bool hasDelta = event&MUS_EVENT_DELTA_MASK;
            ALubyte channel = event&MUS_EVENT_CHANNEL_MASK;

            event &= ~MUS_EVENT_DELTA_MASK;
            event &= ~MUS_EVENT_CHANNEL_MASK;

            // Convert percussion channel (MUS #15 -> MIDI #9)
            if(channel == 15)
                channel = 9;
            else if(channel >= 9)
                channel++;

            if(firstUse[channel])
            {
                // This is the first time this channel has been used,
                // so sets its volume to 127.
                firstUse[channel] = false;
                midiData.push_back(0);
                midiData.push_back(MIDI_CTRLCHANGE | channel);
                midiData.push_back(7);
                midiData.push_back(127);
            }

            ALubyte t = 0;
            if(event != MUS_SCOREEND)
                t = fstream->get();

            ALubyte midArgs = 2;
            ALubyte midStatus = channel;
            ALubyte mid1 = 0, mid2 = 0;
            bool noop = false;

            switch(event)
            {
            case MUS_NOTEOFF:
                midStatus |= MIDI_NOTEOFF;
                mid1 = t&0x7f;
                mid2 = 64;
                break;

            case MUS_NOTEON:
                midStatus |= MIDI_NOTEON;
                mid1 = t&0x7f;
                if((t&0x80))
                    chanVel[channel] = fstream->get()&0x7f;
                mid2 = chanVel[channel];
                break;

            case MUS_PITCHBEND:
                midStatus |= MIDI_PITCHBEND;
                mid1 = (t&1) << 6;
                mid2 = (t>>1) & 0x7f;
                break;

            case MUS_SYSEVENT:
                if(t < 10 || t > 14)
                    noop = true;
                else
                {
                    midStatus |= MIDI_CTRLCHANGE;
                    mid1 = CtrlTranslate[t];
                    mid2 = ((t==12) /* Mono */ ? numChans : 0);
                }
                break;

            case MUS_CTRLCHANGE:
                if(t == 0)
                {
                    // Program change, only one arg
                    midArgs = 1;
                    midStatus |= MIDI_PRGMCHANGE;
                    mid1 = fstream->get()&0x7f;
                }
                else if(t < 10)
                {
                    midStatus |= MIDI_CTRLCHANGE;
                    mid1 = CtrlTranslate[t];
                    mid2 = fstream->get();
                }
                else
                    noop = true;
                break;

            case MUS_SCOREEND:
                midStatus = MIDI_META;
                mid1 = MIDI_META_EOT;
                mid2 = 0;
                break;

            default:
                return false;
            }

            if(noop)
            {
                // A system-specific event with no data is a no-op.
                midStatus = MIDI_META;
                mid1 = MIDI_META_SSPEC;
                mid2 = 0;
            }

            WriteVarLen(midiData, deltaTime);
            if(midStatus != status)
            {
                status = midStatus;
                midiData.push_back(status);
            }
            if(midArgs >= 1)
                midiData.push_back(mid1);
            if(midArgs >= 2)
                midiData.push_back(mid2);

            deltaTime = (hasDelta ? ReadVarLen() : 0);
        }

        // If reading failed or we overran the song length, the song is bad
        if(!fstream->good() || fstream->tellg() > maxmus_p)
            return false;

        // Fill in track length
        size_t trackLen = midiData.size() - 22;
        midiData[18] = (trackLen>>24) & 0xff;
        midiData[19] = (trackLen>>16) & 0xff;
        midiData[20] = (trackLen>>8) & 0xff;
        midiData[21] = trackLen&0xff;
        return true;
    }
};
#else
struct midiStream : public nullStream {
    midiStream(std::istream*){}
};
#endif


#ifdef HAS_GSTREAMER
struct gstStream : public alureStream {
    GstElement *gstPipeline;

    ALenum format;
    ALuint samplerate;
    ALuint blockAlign;

    std::vector<ALubyte> initialData;

    ALubyte *outBytes;
    ALuint outLen;
    ALuint outTotal;

    virtual bool IsValid()
    { return gstPipeline != NULL; }

    virtual bool GetFormat(ALenum *format, ALuint *frequency, ALuint *blockalign)
    {
        *format = this->format;
        *frequency = samplerate;
        *blockalign = blockAlign;
        return true;
    }

    virtual ALuint GetData(ALubyte *data, ALuint bytes)
    {
        bytes -= bytes%blockAlign;

        outTotal = 0;
        outLen = bytes;
        outBytes = data;

        if(initialData.size() > 0)
        {
            size_t rem = std::min(initialData.size(), (size_t)bytes);
            memcpy(data, &initialData[0], rem);
            outTotal += rem;
            initialData.erase(initialData.begin(), initialData.begin()+rem);
        }

        GstElement *gstSink = gst_bin_get_by_name(GST_BIN(gstPipeline), "alureSink");
        while(outTotal < outLen && !gst_app_sink_is_eos((GstAppSink*)gstSink))
            on_new_buffer_from_source(gstSink);
        gst_object_unref(gstSink);

        return outTotal;
    }

    virtual bool Rewind()
    {
        GstSeekFlags flags = GstSeekFlags(GST_SEEK_FLAG_FLUSH|GST_SEEK_FLAG_KEY_UNIT);
        if(gst_element_seek_simple(gstPipeline, GST_FORMAT_TIME, flags, 0))
        {
            initialData.clear();
            return true;
        }

        SetError("Seek failed");
        return false;
    }

    gstStream(std::istream *_fstream)
      : alureStream(_fstream), gstPipeline(NULL), format(AL_NONE), outBytes(NULL),
        outLen(0), outTotal(0)
    { Init(); }

    virtual ~gstStream()
    {
        if(gstPipeline)
        {
            gst_element_set_state(gstPipeline, GST_STATE_NULL);
            gst_object_unref(gstPipeline);
            gstPipeline = NULL;
        }
    }

private:
    void Init()
    {
        fstream->seekg(0, std::ios_base::end);
        std::streamsize len = fstream->tellg();
        fstream->seekg(0, std::ios_base::beg);

        if(!fstream->good() || len <= 0)
            return;

        std::string gst_audio_caps;
        if(alIsExtensionPresent("AL_EXT_MCFORMATS"))
        {
            static const struct {
                const char *ename;
                const char *chans;
                const char *order;
            } fmts32[] = {
                { "AL_FORMAT_71CHN32", "8", "GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT, GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT, GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER, GST_AUDIO_CHANNEL_POSITION_LFE, GST_AUDIO_CHANNEL_POSITION_REAR_LEFT, GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT, GST_AUDIO_CHANNEL_POSITION_SIDE_LEFT, GST_AUDIO_CHANNEL_POSITION_SIDE_RIGHT" },
                { "AL_FORMAT_51CHN32", "6", "GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT, GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT, GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER, GST_AUDIO_CHANNEL_POSITION_LFE, GST_AUDIO_CHANNEL_POSITION_REAR_LEFT, GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT" },
                { "AL_FORMAT_QUAD32", "4", "GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT, GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT, GST_AUDIO_CHANNEL_POSITION_REAR_LEFT, GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT" },
                { "AL_FORMAT_STEREO_FLOAT32", "2", "GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT, GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT" },
                { "AL_FORMAT_MONO_FLOAT32", "1", "GST_AUDIO_CHANNEL_POSITION_FRONT_MONO" },
                { NULL, NULL, NULL }
            }, fmts16[] = {
                { "AL_FORMAT_71CHN16", "8", "GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT, GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT, GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER, GST_AUDIO_CHANNEL_POSITION_LFE, GST_AUDIO_CHANNEL_POSITION_REAR_LEFT, GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT, GST_AUDIO_CHANNEL_POSITION_SIDE_LEFT, GST_AUDIO_CHANNEL_POSITION_SIDE_RIGHT" },
                { "AL_FORMAT_51CHN16", "6", "GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT, GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT, GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER, GST_AUDIO_CHANNEL_POSITION_LFE, GST_AUDIO_CHANNEL_POSITION_REAR_LEFT, GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT" },
                { "AL_FORMAT_QUAD16", "4", "GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT, GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT, GST_AUDIO_CHANNEL_POSITION_REAR_LEFT, GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT" },
                { "AL_FORMAT_STEREO16", "2", "GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT, GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT" },
                { "AL_FORMAT_MONO16", "1", "GST_AUDIO_CHANNEL_POSITION_FRONT_MONO" },
                { NULL, NULL, NULL }
            }, fmts8[] = {
                { "AL_FORMAT_71CHN8", "8", "GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT, GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT, GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER, GST_AUDIO_CHANNEL_POSITION_LFE, GST_AUDIO_CHANNEL_POSITION_REAR_LEFT, GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT, GST_AUDIO_CHANNEL_POSITION_SIDE_LEFT, GST_AUDIO_CHANNEL_POSITION_SIDE_RIGHT" },
                { "AL_FORMAT_51CHN8", "6", "GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT, GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT, GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER, GST_AUDIO_CHANNEL_POSITION_LFE, GST_AUDIO_CHANNEL_POSITION_REAR_LEFT, GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT" },
                { "AL_FORMAT_QUAD8", "4", "GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT, GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT, GST_AUDIO_CHANNEL_POSITION_REAR_LEFT, GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT" },
                { "AL_FORMAT_STEREO8", "2", "GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT, GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT" },
                { "AL_FORMAT_MONO8", "1", "GST_AUDIO_CHANNEL_POSITION_FRONT_MONO" },
                { NULL, NULL, NULL }
            };

            if(alIsExtensionPresent("AL_EXT_FLOAT32"))
            {
                for(int i = 0;fmts32[i].ename;i++)
                {
                    if(alGetEnumValue(fmts32[i].ename) == 0)
                        continue;

                    gst_audio_caps +=
                        "audio/x-raw-float, \n\t"
                        "endianness = (int) { " G_STRINGIFY(G_BYTE_ORDER) " }, \n\t"
                        "signed = (boolean) TRUE, \n\t"
                        "width = (int) 32, \n\t"
                        "depth = (int) 32, \n\t"
                        "rate = (int) [ 1, MAX ], \n\t"
                        "channels = (int) ";
                    gst_audio_caps += fmts32[i].chans;
                    gst_audio_caps += ", \n\t"
                        "channel-positions = (GstAudioChannelPosition) < ";
                    gst_audio_caps += fmts32[i].order;
                    gst_audio_caps += " >; \n";
                }
            }
            for(int i = 0;fmts16[i].ename;i++)
            {
                if(alGetEnumValue(fmts16[i].ename) == 0)
                    continue;

                gst_audio_caps +=
                    "audio/x-raw-int, \n\t"
                    "endianness = (int) { " G_STRINGIFY(G_BYTE_ORDER) " }, \n\t"
                    "signed = (boolean) TRUE, \n\t"
                    "width = (int) 16, \n\t"
                    "depth = (int) 16, \n\t"
                    "rate = (int) [ 1, MAX ], \n\t"
                    "channels = (int) ";
                gst_audio_caps += fmts16[i].chans;
                gst_audio_caps += ", \n\t"
                    "channel-positions = (GstAudioChannelPosition) < ";
                gst_audio_caps += fmts16[i].order;
                gst_audio_caps += " >; \n";
            }
            for(int i = 0;fmts8[i].ename;i++)
            {
                if(alGetEnumValue(fmts8[i].ename) == 0)
                    continue;

                gst_audio_caps +=
                    "audio/x-raw-int, \n\t"
                    "signed = (boolean) FALSE, \n\t"
                    "width = (int) 8, \n\t"
                    "depth = (int) 8, \n\t"
                    "rate = (int) [ 1, MAX ], \n\t"
                    "channels = (int) ";
                gst_audio_caps += fmts8[i].chans;
                gst_audio_caps += ", \n\t"
                    "channel-positions = (GstAudioChannelPosition) < ";
                gst_audio_caps += fmts8[i].order;
                gst_audio_caps += " >; \n";
            }
        }
        else
        {
            if(alIsExtensionPresent("AL_EXT_FLOAT32"))
            {
                gst_audio_caps +=
                    "audio/x-raw-float, \n\t"
                    "endianness = (int) { " G_STRINGIFY(G_BYTE_ORDER) " }, \n\t"
                    "signed = (boolean) TRUE, \n\t"
                    "width = (int) 32, \n\t"
                    "depth = (int) 32, \n\t"
                    "rate = (int) [ 1, MAX ], \n\t"
                    "channels = (int) [ 1, 2 ]; \n";
            }
            gst_audio_caps +=
                "audio/x-raw-int, \n\t"
                "endianness = (int) { " G_STRINGIFY(G_BYTE_ORDER) " }, \n\t"
                "signed = (boolean) TRUE, \n\t"
                "width = (int) 16, \n\t"
                "depth = (int) 16, \n\t"
                "rate = (int) [ 1, MAX ], \n\t"
                "channels = (int) [ 1, 2 ]; \n";
            gst_audio_caps +=
                "audio/x-raw-int, \n\t"
                "signed = (boolean) FALSE, \n\t"
                "width = (int) 8, \n\t"
                "depth = (int) 8, \n\t"
                "rate = (int) [ 1, MAX ], \n\t"
                "channels = (int) [ 1, 2 ]; \n";
        }

        gchar *string = g_strdup_printf("appsrc name=alureSrc ! decodebin ! audioconvert ! appsink caps=\"%s\" name=alureSink", gst_audio_caps.c_str());
        gstPipeline = gst_parse_launch(string, NULL);
        g_free(string);

        if(!gstPipeline)
            return;

        GstElement *gstSrc = gst_bin_get_by_name(GST_BIN(gstPipeline), "alureSrc");
        GstElement *gstSink = gst_bin_get_by_name(GST_BIN(gstPipeline), "alureSink");

        if(gstSrc && gstSink)
        {
            g_object_set(G_OBJECT(gstSrc), "size", (gint64)len, NULL);
            g_object_set(G_OBJECT(gstSrc), "stream-type", 2, NULL);

            /* configure the appsrc, we will push a buffer to appsrc when it
             * needs more data */
            g_signal_connect(gstSrc, "need-data", G_CALLBACK(feed_data), this);
            g_signal_connect(gstSrc, "seek-data", G_CALLBACK(seek_data), this);

            g_object_set(G_OBJECT(gstSink), "preroll-queue-len", 1, NULL);
            g_object_set(G_OBJECT(gstSink), "max-buffers", 2, NULL);
            g_object_set(G_OBJECT(gstSink), "drop", FALSE, NULL);
            g_object_set(G_OBJECT(gstSink), "sync", FALSE, NULL);

            GstBus *bus = gst_element_get_bus(gstPipeline);
            if(bus)
            {
                const GstMessageType types = GstMessageType(GST_MESSAGE_ERROR|GST_MESSAGE_ASYNC_DONE);
                GstMessage *msg;

                gst_element_set_state(gstPipeline, GST_STATE_PLAYING);
                while((msg=gst_bus_timed_pop_filtered(bus, GST_CLOCK_TIME_NONE, types)) != NULL)
                {
                    if(GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ASYNC_DONE)
                    {
                        on_new_preroll_from_source(gstSink);
                        gst_message_unref(msg);
                        break;
                    }

                    if(GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ERROR)
                    {
                        gchar  *debug;
                        GError *error;

                        gst_message_parse_error(msg, &error, &debug);
                        g_printerr("GST Error: %s\n", error->message);

                        g_free(debug);
                        g_error_free(error);

                        gst_message_unref(msg);
                        break;
                    }
                    gst_message_unref(msg);
                }

                gst_object_unref(bus);
                bus = NULL;
            }
        }

        if(gstSrc)  gst_object_unref(gstSrc);
        if(gstSink) gst_object_unref(gstSink);

        if(format == AL_NONE)
        {
            gst_element_set_state(gstPipeline, GST_STATE_NULL);
            gst_object_unref(gstPipeline);
            gstPipeline = NULL;
        }
    }

    void on_new_preroll_from_source(GstElement *elt)
    {
        // get the buffer from appsink
        GstBuffer *buffer = gst_app_sink_pull_preroll(GST_APP_SINK(elt));
        if(!buffer)
            return;

        if(format == AL_NONE)
        {
            GstCaps *caps = GST_BUFFER_CAPS(buffer);
            //GST_LOG("caps are %" GST_PTR_FORMAT, caps);

            ALint i;
            gint rate = 0, channels = 0, bits = 0;
            for(i = gst_caps_get_size(caps)-1;i >= 0;i--)
            {
                GstStructure *struc = gst_caps_get_structure(caps, i);
                if(gst_structure_has_field(struc, "channels"))
                    gst_structure_get_int(struc, "channels", &channels);
                if(gst_structure_has_field(struc, "rate"))
                    gst_structure_get_int(struc, "rate", &rate);
                if(gst_structure_has_field(struc, "width"))
                    gst_structure_get_int(struc, "width", &bits);
            }

            samplerate = rate;
            if(bits == 32)
                format = alureGetSampleFormat(channels, 0, bits);
            else
                format = alureGetSampleFormat(channels, bits, 0);
            blockAlign = channels * bits / 8;
        }

        /* we don't need the appsink buffer anymore */
        gst_buffer_unref(buffer);
    }

    void on_new_buffer_from_source(GstElement *elt)
    {
        // get the buffer from appsink
        GstBuffer *buffer = gst_app_sink_pull_buffer(GST_APP_SINK(elt));
        if(!buffer)
            return;

        ALubyte *src_buffer = (ALubyte*)(GST_BUFFER_DATA(buffer));
        guint size = GST_BUFFER_SIZE(buffer);
        guint rem = std::min(size, outLen-outTotal);
        if(rem > 0)
            memcpy(outBytes+outTotal, src_buffer, rem);
        outTotal += rem;

        if(size > rem)
        {
            size_t start = initialData.size();
            initialData.resize(start+size-rem);
            memcpy(&initialData[start], src_buffer+rem, initialData.size()-start);
        }

        /* we don't need the appsink buffer anymore */
        gst_buffer_unref(buffer);
    }

    static void feed_data(GstElement *appsrc, guint size, gstStream *app)
    {
        GstFlowReturn ret;

        if(!app->fstream->good())
        {
            // we are EOS, send end-of-stream
            g_signal_emit_by_name(appsrc, "end-of-stream", &ret);
            return;
        }

        // read any amount of data, we are allowed to return less if we are EOS
        GstBuffer *buffer = gst_buffer_new();
        void *data = g_malloc(size);

        app->fstream->read(static_cast<char*>(data), size);

        GST_BUFFER_SIZE(buffer) = app->fstream->gcount();
        GST_BUFFER_MALLOCDATA(buffer) = static_cast<guint8*>(data);
        GST_BUFFER_DATA(buffer) = GST_BUFFER_MALLOCDATA(buffer);

        //GST_DEBUG("feed buffer %p, %u", buffer, GST_BUFFER_SIZE(buffer));
        g_signal_emit_by_name(appsrc, "push-buffer", buffer, &ret);
        gst_buffer_unref(buffer);
    }

    static gboolean seek_data(GstElement */*appsrc*/, guint64 position, gstStream *app)
    {
        //GST_DEBUG("seek to offset %" G_GUINT64_FORMAT, position);
        app->fstream->clear();
        return (app->fstream->seekg(position) ? TRUE : FALSE);
    }
};
#else
struct gstStream : public nullStream {
    gstStream(std::istream*){}
};
#endif


template <typename T>
alureStream *create_stream(const T &fdata)
{
    alureStream *stream;

    std::map<ALint,UserCallbacks>::iterator i = InstalledCallbacks.begin();
    while(i != InstalledCallbacks.end() && i->first < 0)
    {
        stream = new customStream(fdata, i->second);
        if(stream->IsValid())
            return stream;
        delete stream;
        i++;
    }

    InStream *file = new InStream(fdata);
    if(file->IsOpen())
    {
        stream = new wavStream(file);
        if(stream->IsValid())
            return stream;
        delete stream;

        file->clear();
        file->seekg(0, std::ios_base::beg);
        stream = new aiffStream(file);
        if(stream->IsValid())
            return stream;
        delete stream;

        // Try libVorbisFile
        file->clear();
        file->seekg(0, std::ios_base::beg);
        stream = new oggStream(file);
        if(stream->IsValid())
            return stream;
        delete stream;

        // Try libFLAC
        file->clear();
        file->seekg(0, std::ios_base::beg);
        stream = new flacStream(file);
        if(stream->IsValid())
            return stream;
        delete stream;

        // Try DUMB
        file->clear();
        file->seekg(0, std::ios_base::beg);
        stream = new dumbStream(file);
        if(stream->IsValid())
            return stream;
        delete stream;

        // Try libSndFile
        file->clear();
        file->seekg(0, std::ios_base::beg);
        stream = new sndStream(file);
        if(stream->IsValid())
            return stream;
        delete stream;

        // Try MPG123
        file->clear();
        file->seekg(0, std::ios_base::beg);
        stream = new mp3Stream(file);
        if(stream->IsValid())
            return stream;
        delete stream;

        // Try Timidity
        file->clear();
        file->seekg(0, std::ios_base::beg);
        stream = new midiStream(file);
        if(stream->IsValid())
            return stream;
        delete stream;

        // Try GStreamer
        file->clear();
        file->seekg(0, std::ios_base::beg);
        stream = new gstStream(file);
        if(stream->IsValid())
            return stream;
        delete stream;

        SetError("Unsupported type");
        delete file;
    }
    else
    {
        SetError("Failed to open file");
        delete file;
    }

    while(i != InstalledCallbacks.end())
    {
        stream = new customStream(fdata, i->second);
        if(stream->IsValid())
            return stream;
        delete stream;
        i++;
    }

    return new nullStream;
}

static alureStream *InitStream(alureStream *instream, ALsizei chunkLength, ALsizei numBufs, ALuint *bufs)
{
    std::auto_ptr<alureStream> stream(instream);
    ALenum format;
    ALuint freq, blockAlign;

    if(!stream->GetFormat(&format, &freq, &blockAlign))
    {
        SetError("Could not get stream format");
        return NULL;
    }

    if(format == AL_NONE)
    {
        SetError("No valid format");
        return NULL;
    }
    if(blockAlign == 0)
    {
        SetError("Invalid block size");
        return NULL;
    }
    if(freq == 0)
    {
        SetError("Invalid sample rate");
        return NULL;
    }

    chunkLength -= chunkLength%blockAlign;
    if(chunkLength <= 0)
    {
        SetError("Chunk length too small");
        return NULL;
    }

    stream->chunkLen = chunkLength;
    stream->dataChunk = new ALubyte[stream->chunkLen];

    alGenBuffers(numBufs, bufs);
    if(alGetError() != AL_NO_ERROR)
    {
        SetError("Buffer creation failed");
        return NULL;
    }

    ALsizei filled = alureBufferDataFromStream(stream.get(), numBufs, bufs);
    if(filled < 0)
    {
        alDeleteBuffers(numBufs, bufs);
        alGetError();

        SetError("Buffering error");
        return NULL;
    }

    while(filled < numBufs)
    {
        alBufferData(bufs[filled], format, stream->dataChunk, 0, freq);
        if(alGetError() != AL_NO_ERROR)
        {
            SetError("Buffer load failed");
            return NULL;
        }
        filled++;
    }

    return stream.release();
}


extern "C" {

/* Function: alureCreateStreamFromFile
 *
 * Opens a file and sets it up for streaming. The given chunkLength is the
 * number of bytes each buffer will fill with. ALURE will optionally generate
 * the specified number of buffer objects, fill them with the beginning of the
 * data, then place the new IDs into the provided storage, before returning.
 * Requires an active context.
 *
 * Returns:
 * An opaque handle used to control the opened stream, or NULL on error.
 *
 * See Also:
 * <alureCreateStreamFromMemory>, <alureCreateStreamFromStaticMemory>,
 * <alureCreateStreamFromCallback>, <alureBufferDataFromStream>,
 * <alureRewindStream>, <alureDestroyStream>
 */
ALURE_API alureStream* ALURE_APIENTRY alureCreateStreamFromFile(const ALchar *fname, ALsizei chunkLength, ALsizei numBufs, ALuint *bufs)
{
    if(alGetError() != AL_NO_ERROR)
    {
        SetError("Existing OpenAL error");
        return NULL;
    }

    if(chunkLength < 0)
    {
        SetError("Invalid chunk length");
        return NULL;
    }

    if(numBufs < 0)
    {
        SetError("Invalid buffer count");
        return NULL;
    }

    alureStream *stream = create_stream(fname);
    if(!stream->IsValid())
    {
        delete stream;
        return NULL;
    }

    return InitStream(stream, chunkLength, numBufs, bufs);
}

/* Function: alureCreateStreamFromMemory
 *
 * Opens a file image from memory and sets it up for streaming, similar to
 * alureCreateStreamFromFile. The given data buffer can be safely deleted after
 * calling this function. Requires an active context.
 *
 * Returns:
 * An opaque handle used to control the opened stream, or NULL on error.
 *
 * See Also:
 * <alureCreateStreamFromFile>, <alureCreateStreamFromStaticMemory>,
 * <alureCreateStreamFromCallback>, <alureBufferDataFromStream>,
 * <alureRewindStream>, <alureDestroyStream>
 */
ALURE_API alureStream* ALURE_APIENTRY alureCreateStreamFromMemory(const ALubyte *fdata, ALuint length, ALsizei chunkLength, ALsizei numBufs, ALuint *bufs)
{
    if(alGetError() != AL_NO_ERROR)
    {
        SetError("Existing OpenAL error");
        return NULL;
    }

    if(chunkLength < 0)
    {
        SetError("Invalid chunk length");
        return NULL;
    }

    if(numBufs < 0)
    {
        SetError("Invalid buffer count");
        return NULL;
    }

    if(length <= 0)
    {
        SetError("Invalid data length");
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
        return NULL;
    }

    return InitStream(stream, chunkLength, numBufs, bufs);
}

/* Function: alureCreateStreamFromStaticMemory
 *
 * Identical to alureCreateStreamFromMemory, except the given memory is used
 * directly and not duplicated. As a consequence, the data buffer must remain
 * valid while the stream is alive. Requires an active context.
 *
 * Returns:
 * An opaque handle used to control the opened stream, or NULL on error.
 *
 * See Also:
 * <alureCreateStreamFromFile>, <alureCreateStreamFromMemory>,
 * <alureCreateStreamFromCallback>, <alureBufferDataFromStream>,
 * <alureRewindStream>, <alureDestroyStream>
 */
ALURE_API alureStream* ALURE_APIENTRY alureCreateStreamFromStaticMemory(const ALubyte *fdata, ALuint length, ALsizei chunkLength, ALsizei numBufs, ALuint *bufs)
{
    if(alGetError() != AL_NO_ERROR)
    {
        SetError("Existing OpenAL error");
        return NULL;
    }

    if(chunkLength < 0)
    {
        SetError("Invalid chunk length");
        return NULL;
    }

    if(numBufs < 0)
    {
        SetError("Invalid buffer count");
        return NULL;
    }

    if(length <= 0)
    {
        SetError("Invalid data length");
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
        return NULL;
    }

    return InitStream(stream, chunkLength, numBufs, bufs);
}

/* Function: alureCreateStreamFromCallback
 *
 * Creates a stream using the specified callback to retrieve data. Requires an
 * active context.
 *
 * Parameters:
 * callback - This is called when more data is needed from the stream. Up to
 *            the specified number of bytes should be written to the data
 *            pointer, and the number of bytes actually written should be
 *            returned. The number of bytes written must be block aligned for
 *            the format (eg. a multiple of 4 for AL_FORMAT_STEREO16), or an
 *            OpenAL error may occur during buffering.
 * userdata - A handle passed through to the callback.
 * format - The format of the data the callback will be giving. The format must
 *          be valid for the context.
 * samplerate - The sample rate (frequency) of the stream
 *
 * Returns:
 * An opaque handle used to control the opened stream, or NULL on error.
 *
 * See Also:
 * <alureCreateStreamFromFile>, <alureCreateStreamFromMemory>,
 * <alureCreateStreamFromStaticMemory>, <alureBufferDataFromStream>,
 * <alureRewindStream>, <alureDestroyStream>
 */
ALURE_API alureStream* ALURE_APIENTRY alureCreateStreamFromCallback(
      ALuint (*callback)(void *userdata, ALubyte *data, ALuint bytes),
      void *userdata, ALenum format, ALuint samplerate,
      ALsizei chunkLength, ALsizei numBufs, ALuint *bufs)
{
    if(alGetError() != AL_NO_ERROR)
    {
        SetError("Existing OpenAL error");
        return NULL;
    }

    if(callback == NULL)
    {
        SetError("Invalid callback");
        return NULL;
    }

    if(chunkLength < 0)
    {
        SetError("Invalid chunk length");
        return NULL;
    }

    if(numBufs < 0)
    {
        SetError("Invalid buffer count");
        return NULL;
    }

    UserCallbacks newcb;
    newcb.open_file = NULL;
    newcb.open_mem  = NULL;
    newcb.get_fmt   = NULL;
    newcb.decode    = callback;
    newcb.rewind    = NULL;
    newcb.close     = NULL;

    customStream *stream = new customStream(userdata, format, samplerate, newcb);
    return InitStream(stream, chunkLength, numBufs, bufs);
}

/* Function: alureGetStreamFormat
 *
 * Retrieves the format, frequency, and block-alignment used for the given
 * stream. If a parameter is NULL, that value will not be returned.
 *
 * Returns:
 * AL_FALSE on error.
 *
 * See Also:
 * <alureCreateStreamFromFile>, <alureCreateStreamFromMemory>,
 * <alureCreateStreamFromStaticMemory>, <alureCreateStreamFromCallback>,
 */
ALURE_API ALboolean ALURE_APIENTRY alureGetStreamFormat(alureStream *stream,
    ALenum *format, ALuint *frequency, ALuint *blockAlign)
{
    ALenum _fmt;
    ALuint _rate, _balign;

    if(!alureStream::Verify(stream))
    {
        SetError("Invalid stream pointer");
        return AL_FALSE;
    }

    if(!format) format = &_fmt;
    if(!frequency) frequency = &_rate;
    if(!blockAlign) blockAlign = &_balign;

    if(!stream->GetFormat(format, frequency, blockAlign))
    {
        SetError("Could not get stream format");
        return AL_FALSE;
    }

    return AL_TRUE;
}

/* Function: alureBufferDataFromStream
 *
 * Buffers the given buffer objects with the next chunks of data from the
 * stream. The given buffer objects do not need to be ones given by the
 * alureCreateStreamFrom* functions. Requires an active context.
 *
 * Returns:
 * The number of buffers filled with new data, or -1 on error. If the value
 * returned is less than the number requested, the end of the stream has been
 * reached.
 *
 * See Also:
 * <alureCreateStreamFromFile>, <alureCreateStreamFromMemory>,
 * <alureCreateStreamFromStaticMemory>, <alureCreateStreamFromCallback>,
 * <alureRewindStream>, <alureDestroyStream>
 */
ALURE_API ALsizei ALURE_APIENTRY alureBufferDataFromStream(alureStream *stream, ALsizei numBufs, ALuint *bufs)
{
    if(alGetError() != AL_NO_ERROR)
    {
        SetError("Existing OpenAL error");
        return -1;
    }

    if(!alureStream::Verify(stream))
    {
        SetError("Invalid stream pointer");
        return -1;
    }

    if(numBufs < 0)
    {
        SetError("Invalid buffer count");
        return -1;
    }

    ALenum format;
    ALuint freq, blockAlign;

    if(!stream->GetFormat(&format, &freq, &blockAlign))
    {
        SetError("Could not get stream format");
        return -1;
    }

    ALsizei filled;
    for(filled = 0;filled < numBufs;filled++)
    {
        ALuint got = stream->GetData(stream->dataChunk, stream->chunkLen);
        got -= got%blockAlign;
        if(got == 0) break;

        alBufferData(bufs[filled], format, stream->dataChunk, got, freq);
        if(alGetError() != AL_NO_ERROR)
        {
            SetError("Buffer load failed");
            return -1;
        }
    }

    return filled;
}

/* Function: alureRewindStream
 *
 * Rewinds the stream so that the next alureBufferDataFromStream call will
 * restart from the beginning of the audio file.
 *
 * Returns:
 * AL_FALSE on error.
 *
 * See Also:
 * <alureCreateStreamFromFile>, <alureCreateStreamFromMemory>,
 * <alureCreateStreamFromStaticMemory>, <alureCreateStreamFromCallback>,
 * <alureBufferDataFromStream>, <alureSetStreamOrder>, <alureDestroyStream>
 */
ALURE_API ALboolean ALURE_APIENTRY alureRewindStream(alureStream *stream)
{
    if(!alureStream::Verify(stream))
    {
        SetError("Invalid stream pointer");
        return AL_FALSE;
    }

    return stream->Rewind();
}

/* Function: alureSetStreamOrder
 *
 * Skips the module decoder to the specified order, so following buffering
 * calls will decode from the specified order. For non-module formats, setting
 * order 0 is identical to rewinding the stream (other orders will fail).
 *
 * Returns:
 * AL_FALSE on error.
 *
 * See Also:
 * <alureCreateStreamFromFile>, <alureCreateStreamFromMemory>,
 * <alureCreateStreamFromStaticMemory>, <alureCreateStreamFromCallback>,
 * <alureBufferDataFromStream>, <alureRewindStream>, <alureDestroyStream>
 */
ALURE_API ALboolean ALURE_APIENTRY alureSetStreamOrder(alureStream *stream, ALuint order)
{
    if(!alureStream::Verify(stream))
    {
        SetError("Invalid stream pointer");
        return AL_FALSE;
    }

    return stream->SetOrder(order);
}

/* Function: alureDestroyStream
 *
 * Closes an opened stream. For convenience, it will also delete the given
 * buffer objects. The given buffer objects do not need to be ones given by the
 * alureCreateStreamFrom* functions. Requires an active context.
 *
 * Returns:
 * AL_FALSE on error.
 *
 * See Also:
 * <alureCreateStreamFromFile>, <alureCreateStreamFromMemory>,
 * <alureCreateStreamFromStaticMemory>, <alureCreateStreamFromCallback>,
 * <alureBufferDataFromStream>, <alureRewindStream>
 */
ALURE_API ALboolean ALURE_APIENTRY alureDestroyStream(alureStream *stream, ALsizei numBufs, ALuint *bufs)
{
    if(alGetError() != AL_NO_ERROR)
    {
        SetError("Existing OpenAL error");
        return AL_FALSE;
    }

    if(numBufs < 0)
    {
        SetError("Invalid buffer count");
        return AL_FALSE;
    }

    if(stream && !alureStream::Verify(stream))
    {
        SetError("Invalid stream pointer");
        return AL_FALSE;
    }

    alDeleteBuffers(numBufs, bufs);
    if(alGetError() != AL_NO_ERROR)
    {
        SetError("Buffer deletion failed");
        return AL_FALSE;
    }

    if(stream)
    {
        StopStream(stream);
        std::istream *f = stream->fstream;
        delete stream; delete f;
    }
    return AL_TRUE;
}

}
