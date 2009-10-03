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
      : alureStream(_fstream), flacFile(NULL)
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
        {
            blockAlign = FLAC__stream_decoder_get_channels(flacFile) *
                         FLAC__stream_decoder_get_bits_per_sample(flacFile)/8;
            samplerate = FLAC__stream_decoder_get_sample_rate(flacFile);
            format = alureGetSampleFormat(FLAC__stream_decoder_get_channels(flacFile),
                                          FLAC__stream_decoder_get_bits_per_sample(flacFile), 0);
            return true;
        }
        return false;
    }

    static FLAC__StreamDecoderWriteStatus WriteCallback(const FLAC__StreamDecoder*, const FLAC__Frame *frame, const FLAC__int32 *const buffer[], void *client_data)
    {
        flacStream *This = static_cast<flacStream*>(client_data);
        ALubyte *data = This->outBytes + This->outTotal;
        ALuint i = 0;

        while(This->outTotal < This->outLen && i < frame->header.blocksize)
        {
            for(ALuint c = 0;c < frame->header.channels;c++)
            {
                if(frame->header.bits_per_sample == 8)
                    *((ALubyte*)data) = buffer[c][i]+128;
                else if(frame->header.bits_per_sample == 16)
                    *((ALshort*)data) = buffer[c][i];
                This->outTotal += frame->header.bits_per_sample/8;
                data += frame->header.bits_per_sample/8;
            }
            i++;
        }

        if(i < frame->header.blocksize)
        {
            ALuint blocklen = (frame->header.blocksize-i) *
                              frame->header.channels *
                              frame->header.bits_per_sample/8;
            ALuint start = This->initialData.size();

            This->initialData.resize(start+blocklen);
            data = &This->initialData[start];

            while(i < frame->header.blocksize)
            {
                for(ALuint c = 0;c < frame->header.channels;c++)
                {
                    if(frame->header.bits_per_sample == 8)
                        *((ALubyte*)data) = buffer[c][i]+128;
                    else if(frame->header.bits_per_sample == 16)
                        *((ALshort*)data) = buffer[c][i];
                    data += frame->header.bits_per_sample/8;
                }
                i++;
            }
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
            fstream->read((char*)data, sizeof(data));
            std::streamsize amt = fstream->gcount();
            long newrate;
            int newchans;
            int enc;

            if(mpg123_decode(newFile, data, amt, NULL, 0, NULL) == MPG123_NEW_FORMAT &&
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
            fstream->read((char*)data, sizeof(data));
            ALuint amt = fstream->gcount();
            int enc;

            if(mpg123_decode(mp3File, data, amt, NULL, 0, NULL) == MPG123_NEW_FORMAT &&
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

        // Try MPG123
        file->clear();
        file->seekg(0, std::ios_base::beg);
        stream = new mp3Stream(file);
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

    if(!stream)
    {
        SetError("Null stream pointer");
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
 * <alureBufferDataFromStream>, <alureDestroyStream>
 */
ALURE_API ALboolean ALURE_APIENTRY alureRewindStream(alureStream *stream)
{
    if(!stream)
    {
        SetError("Null stream pointer");
        return AL_FALSE;
    }

    return stream->Rewind();
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

    alDeleteBuffers(numBufs, bufs);
    if(alGetError() != AL_NO_ERROR)
    {
        SetError("Buffer deletion failed");
        return AL_FALSE;
    }

    if(stream)
    {
        alureStopStream(stream, AL_FALSE);
        std::istream *f = stream->fstream;
        stream->fstream = NULL;
        delete stream;
        delete f;
    }
    return AL_TRUE;
}

}
