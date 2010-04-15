/*
 * ALURE  OpenAL utility library
 * Copyright (C) 2009-2010 by Chris Robinson.
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
#include <sstream>


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
    nullStream():alureStream(NULL) {}
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

    virtual bool GetFormat(ALenum *fmt, ALuint *frequency, ALuint *blockalign)
    {
        if(format == AL_NONE)
        {
            if(!cb.get_fmt ||
               !cb.get_fmt(usrFile, &this->format, &samplerate, &blockAlign))
                return false;
        }

        *fmt = format;
        *frequency = samplerate;
        *blockalign = blockAlign;
        return true;
    }

    virtual ALuint GetData(ALubyte *data, ALuint bytes)
    { return cb.decode(usrFile, data, bytes); }

    virtual bool Rewind()
    {
        if(cb.rewind && cb.rewind(usrFile))
            return true;
        SetError("Rewind failed");
        return false;
    }

    customStream(const char *fname, const UserCallbacks &callbacks)
      : alureStream(NULL), usrFile(NULL), format(0), samplerate(0),
        blockAlign(0), cb(callbacks)
    { if(cb.open_file) usrFile = cb.open_file(fname); }

    customStream(const MemDataInfo &memData, const UserCallbacks &callbacks)
      : alureStream(NULL), usrFile(NULL), format(0), samplerate(0),
        blockAlign(0), memInfo(memData), cb(callbacks)
    { if(cb.open_mem) usrFile = cb.open_mem(memInfo.Data, memInfo.Length); }

    customStream(void *userdata, ALenum fmt, ALuint srate, const UserCallbacks &callbacks)
      : alureStream(NULL), usrFile(userdata), format(fmt), samplerate(srate),
        blockAlign(DetectBlockAlignment(format)), cb(callbacks)
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

    virtual bool GetFormat(ALenum *fmt, ALuint *frequency, ALuint *blockalign)
    {
        *fmt = format;
        *frequency = samplerate;
        *blockalign = blockAlign;
        return true;
    }

    virtual ALuint GetData(ALubyte *data, ALuint bytes)
    {
        std::streamsize rem = ((remLen >= bytes) ? bytes : remLen) / blockAlign;
        fstream->read(reinterpret_cast<char*>(data), rem*blockAlign);

        std::streamsize got = fstream->gcount();
        got -= got%blockAlign;
        remLen -= got;

        if(BigEndian && sampleSize > 1)
        {
            if(sampleSize == 2)
            {
                for(std::streamsize i = 0;i < got;i+=2)
                    swap(data[i], data[i+1]);
            }
            else if(sampleSize == 4)
            {
                for(std::streamsize i = 0;i < got;i+=4)
                {
                    swap(data[i+0], data[i+3]);
                    swap(data[i+1], data[i+2]);
                }
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

                format = GetSampleFormat(channels, sampleSize*8, false);

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

    virtual ~wavStream()
    { }
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

    virtual bool GetFormat(ALenum *fmt, ALuint *frequency, ALuint *blockalign)
    {
        *fmt = format;
        *frequency = samplerate;
        *blockalign = blockAlign;
        return true;
    }

    virtual ALuint GetData(ALubyte *data, ALuint bytes)
    {
        std::streamsize rem = ((remLen >= bytes) ? bytes : remLen) / blockAlign;
        fstream->read(reinterpret_cast<char*>(data), rem*blockAlign);

        std::streamsize got = fstream->gcount();
        got -= got%blockAlign;
        remLen -= got;

        if(LittleEndian)
        {
            if(sampleSize == 2)
            {
                for(std::streamsize i = 0;i < got;i+=2)
                    swap(data[i], data[i+1]);
            }
            else if(sampleSize == 4)
            {
                for(std::streamsize i = 0;i < got;i+=4)
                {
                    swap(data[i+0], data[i+3]);
                    swap(data[i+1], data[i+2]);
                }
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

                format = GetSampleFormat(channels, sampleSize*8, false);

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

    virtual ~aiffStream()
    { }
};

#ifdef HAS_SNDFILE
struct sndStream : public alureStream {
    SNDFILE *sndFile;
    SF_INFO sndInfo;
    ALenum format;

    virtual bool IsValid()
    { return sndFile != NULL; }

    virtual bool GetFormat(ALenum *fmt, ALuint *frequency, ALuint *blockalign)
    {
        if(format == AL_NONE)
            format = GetSampleFormat(sndInfo.channels, 16, false);
        *fmt = format;
        *frequency = sndInfo.samplerate;
        *blockalign = sndInfo.channels*2;
        return true;
    }

    virtual ALuint GetData(ALubyte *data, ALuint bytes)
    {
        const ALuint frameSize = 2*sndInfo.channels;
        return psf_readf_short(sndFile, (short*)data, bytes/frameSize) * frameSize;
    }

    virtual bool Rewind()
    {
        if(psf_seek(sndFile, 0, SEEK_SET) != -1)
            return true;

        SetError("Seek failed");
        return false;
    }

    sndStream(std::istream *_fstream)
      : alureStream(_fstream), sndFile(NULL), format(AL_NONE)
    {
        memset(&sndInfo, 0, sizeof(sndInfo));

        if(!sndfile_handle) return;

        static SF_VIRTUAL_IO streamIO = {
            get_filelen, seek,
            read, write, tell
        };
        sndFile = psf_open_virtual(&streamIO, SFM_READ, &sndInfo, this);
    }

    virtual ~sndStream()
    {
        if(sndFile)
            psf_close(sndFile);
        sndFile = NULL;
    }

private:
    // libSndFile iostream callbacks
    static sf_count_t get_filelen(void *user_data)
    {
        std::istream *stream = static_cast<sndStream*>(user_data)->fstream;
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
        std::istream *stream = static_cast<sndStream*>(user_data)->fstream;
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
        std::istream *stream = static_cast<sndStream*>(user_data)->fstream;
        stream->clear();
        stream->read(static_cast<char*>(ptr), count);
        return stream->gcount();
    }

    static sf_count_t write(const void*, sf_count_t, void*)
    { return -1; }

    static sf_count_t tell(void *user_data)
    {
        std::istream *stream = static_cast<sndStream*>(user_data)->fstream;
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
    OggVorbis_File oggFile;
    vorbis_info *oggInfo;
    int oggBitstream;
    ALenum format;

    virtual bool IsValid()
    { return oggInfo != NULL; }

    virtual bool GetFormat(ALenum *fmt, ALuint *frequency, ALuint *blockalign)
    {
        if(format == AL_NONE)
            format = GetSampleFormat(oggInfo->channels, 16, false);
        *fmt = format;
        *frequency = oggInfo->rate;
        *blockalign = oggInfo->channels*2;
        return true;
    }

    virtual ALuint GetData(ALubyte *data, ALuint bytes)
    {
        ALuint got = 0;
        while(bytes > 0)
        {
            int res = pov_read(&oggFile, (char*)&data[got], bytes, BigEndian?1:0, 2, 1, &oggBitstream);
            if(res <= 0)
                break;
            bytes -= res;
            got += res;
        }
        // 1, 2, and 4 channel files decode into the same channel order as
        // OpenAL, however 6(5.1), 7(6.1), and 8(7.1) channel files need to be
        // re-ordered
        if(oggInfo->channels == 6)
        {
            ALshort *samples = (ALshort*)data;
            for(ALuint i = 0;i < got/sizeof(ALshort);i+=6)
            {
                // OpenAL : FL, FR, FC, LFE, RL, RR
                // Vorbis : FL, FC, FR,  RL, RR, LFE
                swap(samples[i+1], samples[i+2]);
                swap(samples[i+3], samples[i+5]);
                swap(samples[i+4], samples[i+5]);
            }
        }
        else if(oggInfo->channels == 7)
        {
            ALshort *samples = (ALshort*)data;
            for(ALuint i = 0;i < got/sizeof(ALshort);i+=7)
            {
                // OpenAL : FL, FR, FC, LFE, RC, SL, SR
                // Vorbis : FL, FC, FR,  SL, SR, RC, LFE
                swap(samples[i+1], samples[i+2]);
                swap(samples[i+3], samples[i+6]);
                swap(samples[i+4], samples[i+6]);
                swap(samples[i+5], samples[i+6]);
            }
        }
        else if(oggInfo->channels == 8)
        {
            ALshort *samples = (ALshort*)data;
            for(ALuint i = 0;i < got/sizeof(ALshort);i+=8)
            {
                // OpenAL : FL, FR, FC, LFE, RL, RR, SL, SR
                // Vorbis : FL, FC, FR,  SL, SR, RL, RR, LFE
                swap(samples[i+1], samples[i+2]);
                swap(samples[i+3], samples[i+7]);
                swap(samples[i+4], samples[i+5]);
                swap(samples[i+5], samples[i+6]);
                swap(samples[i+6], samples[i+7]);
            }
        }
        return got;
    }

    virtual bool Rewind()
    {
        if(pov_pcm_seek(&oggFile, 0) == 0)
            return true;

        SetError("Seek failed");
        return false;
    }

    oggStream(std::istream *_fstream)
      : alureStream(_fstream), oggInfo(NULL), oggBitstream(0), format(AL_NONE)
    {
        if(!vorbisfile_handle) return;

        const ov_callbacks streamIO = {
            read, seek, NULL, tell
        };

        if(pov_open_callbacks(this, &oggFile, NULL, 0, streamIO) == 0)
        {
            oggInfo = pov_info(&oggFile, -1);
            if(!oggInfo)
                pov_clear(&oggFile);
        }
    }

    virtual ~oggStream()
    {
        if(oggInfo)
            pov_clear(&oggFile);
        oggInfo = NULL;
    }

private:
    // libVorbisFile iostream callbacks
    static int seek(void *user_data, ogg_int64_t offset, int whence)
    {
        std::istream *stream = static_cast<oggStream*>(user_data)->fstream;
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

    static size_t read(void *ptr, size_t size, size_t nmemb, void *user_data)
    {
        std::istream *stream = static_cast<oggStream*>(user_data)->fstream;
        stream->clear();

        stream->read(static_cast<char*>(ptr), nmemb*size);
        size_t ret = stream->gcount();
        return ret/size;
    }

    static long tell(void *user_data)
    {
        std::istream *stream = static_cast<oggStream*>(user_data)->fstream;
        stream->clear();
        return stream->tellg();
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
    ALenum format;

    virtual bool IsValid()
    { return mp3File != NULL; }

    virtual bool GetFormat(ALenum *fmt, ALuint *frequency, ALuint *blockalign)
    {
        *fmt = format;
        *frequency = samplerate;
        *blockalign = channels*2;
        return true;
    }

    virtual ALuint GetData(ALubyte *data, ALuint bytes)
    {
        ALuint amt = 0;
        while(bytes > 0)
        {
            size_t got = 0;
            int ret = pmpg123_read(mp3File, data, bytes, &got);

            bytes -= got;
            data += got;
            amt += got;

            if(ret == MPG123_NEW_FORMAT)
            {
                long newrate;
                int newchans, enc;
                pmpg123_getformat(mp3File, &newrate, &newchans, &enc);
                continue;
            }
            if(ret == MPG123_NEED_MORE)
            {
                unsigned char data[4096];
                fstream->read((char*)data, sizeof(data));
                std::streamsize insize = fstream->gcount();
                if(insize > 0 && pmpg123_feed(mp3File, data, insize) == MPG123_OK)
                    continue;
            }
            if(got == 0)
                break;
        }
        return amt;
    }

    virtual bool Rewind()
    {
        fstream->clear();
        std::istream::pos_type oldpos = fstream->tellg();
        fstream->seekg(0);

        mpg123_handle *newFile = pmpg123_new(NULL, NULL);
        if(pmpg123_open_feed(newFile) == MPG123_OK)
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
                ret = pmpg123_decode(newFile, data, amt, NULL, 0, NULL);
            } while(ret == MPG123_NEED_MORE && total < 64*1024);

            if(ret == MPG123_NEW_FORMAT &&
               pmpg123_getformat(newFile, &newrate, &newchans, &enc) == MPG123_OK)
            {
                if(pmpg123_format_none(newFile) == MPG123_OK &&
                   pmpg123_format(newFile, samplerate, channels, MPG123_ENC_SIGNED_16) == MPG123_OK)
                {
                    // All OK
                    pmpg123_delete(mp3File);
                    mp3File = newFile;
                    return true;
                }
            }
            pmpg123_delete(newFile);
        }

        fstream->seekg(oldpos);
        SetError("Restart failed");
        return false;
    }

    mp3Stream(std::istream *_fstream)
      : alureStream(_fstream), mp3File(NULL), format(AL_NONE)
    {
        if(!mp123_handle) return;

        mp3File = pmpg123_new(NULL, NULL);
        if(pmpg123_open_feed(mp3File) == MPG123_OK)
        {
            unsigned char data[4096];
            int ret, enc;

            ALuint amt, total = 0;
            do {
                fstream->read((char*)data, sizeof(data));
                amt = fstream->gcount();
                if(amt == 0)  break;
                total += amt;
                ret = pmpg123_decode(mp3File, data, amt, NULL, 0, NULL);
            } while(ret == MPG123_NEED_MORE && total < 64*1024);

            if(ret == MPG123_NEW_FORMAT &&
               pmpg123_getformat(mp3File, &samplerate, &channels, &enc) == MPG123_OK)
            {
                format = GetSampleFormat(channels, 16, false);
                if(pmpg123_format_none(mp3File) == MPG123_OK &&
                   pmpg123_format(mp3File, samplerate, channels, MPG123_ENC_SIGNED_16) == MPG123_OK)
                {
                    // All OK
                    return;
                }
            }
        }
        pmpg123_delete(mp3File);
        mp3File = NULL;
    }

    virtual ~mp3Stream()
    {
        if(mp3File)
            pmpg123_delete(mp3File);
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
            format = GetSampleFormat(2, 32, true);
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

        if(pdumb_it_sr_get_speed(pduh_get_it_sigrenderer(renderer)) == 0)
            return 0;

        ALuint sample_count = bytes / ((format==AL_FORMAT_STEREO16) ?
                                       sizeof(ALshort) : sizeof(ALfloat));

        sampleBuf.resize(sample_count);
        sample_t *samples[] = {
            &sampleBuf[0]
        };

        pdumb_silence(samples[0], sample_count);
        ret = pduh_sigrenderer_generate_samples(renderer, 1.0f, 1.0f, sample_count/2, samples);
        ret *= 2;
        if(format == AL_FORMAT_STEREO16)
        {
            for(ALuint i = 0;i < ret;i++)
                ((ALshort*)data)[i] = clamp(samples[0][i]>>8, -32768, 32767);
        }
        else
        {
            for(ALuint i = 0;i < ret;i++)
                ((ALfloat*)data)[i] = ((samples[0][i]>=0) ?
                                       samples[0][i]/(float)0x7FFFFF :
                                       samples[0][i]/(float)0x800000);
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
            pdumb_it_sr_set_speed(pduh_get_it_sigrenderer(renderer), prevSpeed);
            prevSpeed = 0;
            return true;
        }

        // Else, no loop point. Restart from scratch.
        DUH_SIGRENDERER *newrenderer = pdumb_it_start_at_order(duh, 2, lastOrder);
        if(!newrenderer)
        {
            SetError("Could start renderer");
            return false;
        }
        pduh_end_sigrenderer(renderer);
        renderer = newrenderer;
        return true;
    }

    virtual bool SetOrder(ALuint order)
    {
        DUH_SIGRENDERER *newrenderer = pdumb_it_start_at_order(duh, 2, order);
        if(!newrenderer)
        {
            SetError("Could not set order");
            return false;
        }
        pduh_end_sigrenderer(renderer);
        renderer = newrenderer;

        lastOrder = order;
        return true;
    }

    dumbStream(std::istream *_fstream)
      : alureStream(_fstream), dumbFile(NULL), duh(NULL), renderer(NULL),
        lastOrder(0), prevSpeed(0), format(AL_NONE)
    {
        if(!dumb_handle) return;

        DUH* (*funcs[])(DUMBFILE*) = {
            pdumb_read_it,
            pdumb_read_xm,
            pdumb_read_s3m,
            pdumb_read_mod,
            NULL
        };

        vfs.open = NULL;
        vfs.skip = skip;
        vfs.getc = read_char;
        vfs.getnc = read;
        vfs.close = NULL;

        for(size_t i = 0;funcs[i];i++)
        {
            dumbFile = pdumbfile_open_ex(this, &vfs);
            if(dumbFile)
            {
                duh = funcs[i](dumbFile);
                if(duh)
                {
                    renderer = pdumb_it_start_at_order(duh, 2, lastOrder);
                    if(renderer)
                    {
                        pdumb_it_set_loop_callback(pduh_get_it_sigrenderer(renderer), loop_cb, this);
                        break;
                    }

                    punload_duh(duh);
                    duh = NULL;
                }

                pdumbfile_close(dumbFile);
                dumbFile = NULL;
            }
            fstream->clear();
            fstream->seekg(0);
        }
    }

    virtual ~dumbStream()
    {
        if(renderer)
            pduh_end_sigrenderer(renderer);
        renderer = NULL;

        if(duh)
            punload_duh(duh);
        duh = NULL;

        if(dumbFile)
            pdumbfile_close(dumbFile);
        dumbFile = NULL;
    }

private:
    // DUMBFILE iostream callbacks
    static int skip(void *user_data, long offset)
    {
        std::istream *stream = static_cast<dumbStream*>(user_data)->fstream;
        stream->clear();

        if(stream->seekg(offset, std::ios_base::cur))
            return 0;
        return -1;
    }

    static long read(char *ptr, long size, void *user_data)
    {
        std::istream *stream = static_cast<dumbStream*>(user_data)->fstream;
        stream->clear();

        stream->read(ptr, size);
        return stream->gcount();
    }

    static int read_char(void *user_data)
    {
        std::istream *stream = static_cast<dumbStream*>(user_data)->fstream;
        stream->clear();

        unsigned char ret;
        stream->read(reinterpret_cast<char*>(&ret), 1);
        if(stream->gcount() > 0)
            return ret;
        return -1;
    }

    static int loop_cb(void *user_data)
    {
        dumbStream *self = static_cast<dumbStream*>(user_data);
        self->prevSpeed = pdumb_it_sr_get_speed(pduh_get_it_sigrenderer(self->renderer));
        pdumb_it_sr_set_speed(pduh_get_it_sigrenderer(self->renderer), 0);
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
#ifdef _WIN32
    HANDLE pcmFile;
    HANDLE cpid;
#else
#define INVALID_HANDLE_VALUE (-1)
    int pcmFile;
    pid_t cpid;
#endif
    ALCint Freq;

    virtual bool IsValid()
    { return cpid != INVALID_HANDLE_VALUE; }

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
        while(bytes > 0)
        {
#ifdef _WIN32
            DWORD got = 0;
            if(!ReadFile(pcmFile, data, bytes, &got, NULL))
                got = 0;
#else
            ssize_t got;
            while((got=read(pcmFile, &data[total], bytes)) == -1 && errno == EINTR)
                ;
#endif
            if(got <= 0)
                break;
            bytes -= got;
            total += got;
        }

        if(BigEndian)
        {
            for(ALuint i = 0;i < total;i+=2)
                swap(data[i], data[i+1]);
        }

        return total;
    }

    virtual bool Rewind()
    {
        fstream->clear();
        fstream->seekg(0);

#ifdef _WIN32
        HANDLE _pcmFile, _cpid;
        if(!StartStream(_pcmFile, _cpid))
        {
            SetError("Failed to restart timidity");
            return false;
        }

        TerminateProcess(cpid, 0);
        CloseHandle(cpid);
        cpid = _cpid;

        CloseHandle(pcmFile);
        pcmFile = _pcmFile;
#else
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
#endif

        return true;
    }

    midiStream(std::istream *_fstream)
      : alureStream(_fstream), pcmFile(INVALID_HANDLE_VALUE),
        cpid(INVALID_HANDLE_VALUE), Freq(44100)
    {
        ALCcontext *ctx = alcGetCurrentContext();
        ALCdevice *dev = alcGetContextsDevice(ctx);
        alcGetIntegerv(dev, ALC_FREQUENCY, 1, &Freq);

        StartStream(pcmFile, cpid);
    }

    virtual ~midiStream()
    {
        if(cpid != INVALID_HANDLE_VALUE)
        {
#ifdef _WIN32
            TerminateProcess(cpid, 0);
            CloseHandle(cpid);
#else
            kill(cpid, SIGTERM);
            waitpid(cpid, NULL, 0);
#endif
            cpid = INVALID_HANDLE_VALUE;
        }

        if(pcmFile != INVALID_HANDLE_VALUE)
        {
#ifdef _WIN32
            CloseHandle(pcmFile);
#else
            close(pcmFile);
#endif
            pcmFile = INVALID_HANDLE_VALUE;
        }
    }

private:
#ifdef _WIN32
    bool StartStream(HANDLE &pcmFile, HANDLE &cpid)
#else
    bool StartStream(int &pcmFile, pid_t &cpid)
#endif
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

#ifdef _WIN32
        SECURITY_ATTRIBUTES attr;
        attr.nLength = sizeof(attr);
        attr.bInheritHandle = TRUE;
        attr.lpSecurityDescriptor = NULL;

        HANDLE midPipe[2], pcmPipe[2];
        if(!CreatePipe(&midPipe[0], &midPipe[1], &attr, 0))
            return false;
        if(!CreatePipe(&pcmPipe[0], &pcmPipe[1], &attr, 0))
        {
            CloseHandle(midPipe[0]);
            CloseHandle(midPipe[1]);
            return false;
        }

        PROCESS_INFORMATION procInfo;
        memset(&procInfo, 0, sizeof(procInfo));

        STARTUPINFO startInfo;
        memset(&startInfo, 0, sizeof(startInfo));
        startInfo.cb = sizeof(startInfo);
        startInfo.hStdError = GetStdHandle(STD_ERROR_HANDLE);
        startInfo.hStdOutput = pcmPipe[1];
        startInfo.hStdInput = midPipe[0];
        startInfo.dwFlags |= STARTF_USESTDHANDLES;

        std::stringstream cmdstr;
        cmdstr << "timidity - -idqq -Ow1sl -o - -s " << Freq;
        std::string cmdline = cmdstr.str();

        if(!CreateProcessA(NULL, const_cast<CHAR*>(cmdline.c_str()), NULL,
                           NULL, TRUE, 0, NULL, NULL, &startInfo, &procInfo))
        {
            CloseHandle(midPipe[0]);
            CloseHandle(midPipe[1]);
            CloseHandle(pcmPipe[0]);
            CloseHandle(pcmPipe[1]);
            return false;
        }

        CloseHandle(midPipe[0]);
        CloseHandle(pcmPipe[1]);

        CloseHandle(procInfo.hThread);
        HANDLE pid = procInfo.hProcess;

        ALubyte *cur = &midiData[0];
        size_t rem = midiData.size();
        do {
            DWORD wrote;
            if(!WriteFile(midPipe[1], cur, rem, &wrote, NULL) ||
               wrote <= 0)
                break;
            cur += wrote;
            rem -= wrote;
        } while(rem > 0);
        CloseHandle(midPipe[1]); midPipe[1] = INVALID_HANDLE_VALUE;

        ALubyte fmthdr[44];
        cur = fmthdr;
        rem = sizeof(fmthdr);
        do {
            DWORD got;
            if(!ReadFile(pcmPipe[0], cur, rem, &got, NULL) ||
               got <= 0)
                break;
            cur += got;
            rem -= got;
        } while(rem > 0);
        if(rem > 0)
        {
            TerminateProcess(pid, 0);
            CloseHandle(pid);
            CloseHandle(pcmPipe[0]);
            return false;
        }
#else
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

                std::stringstream freqstr;
                freqstr << Freq;

                execlp("timidity","timidity","-","-idqq","-Ow1sl","-o","-",
                       "-s", freqstr.str().c_str(), NULL);
            }
            _exit(1);
        }

        close(midPipe[0]); midPipe[0] = -1;
        close(pcmPipe[1]); pcmPipe[1] = -1;

        void (*oldhandler)(int) = signal(SIGPIPE, SIG_IGN);
        ALubyte *cur = &midiData[0];
        size_t rem = midiData.size();
        do {
            ssize_t wrote;
            while((wrote=write(midPipe[1], cur, rem)) == -1 && errno == EINTR)
                ;
            if(wrote <= 0)
                break;
            cur += wrote;
            rem -= wrote;
        } while(rem > 0);
        close(midPipe[1]); midPipe[1] = -1;
        signal(SIGPIPE, oldhandler);

        ALubyte fmthdr[44];
        cur = fmthdr;
        rem = sizeof(fmthdr);
        do {
            ssize_t got;
            while((got=read(pcmPipe[0], cur, rem)) == -1 && errno == EINTR)
                ;
            if(got <= 0)
                break;
            cur += got;
            rem -= got;
        } while(rem > 0);
        if(rem > 0)
        {
            kill(pid, SIGTERM);
            waitpid(pid, NULL, 0);
            close(pcmPipe[0]);
            return false;
        }
#endif

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


template <typename T>
alureStream *get_stream_decoder(const T &fdata)
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

    std::istream *file = new InStream(fdata);
    if(!file->fail())
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

alureStream *create_stream(const char *fname)
{ return get_stream_decoder(fname); }
alureStream *create_stream(const MemDataInfo &memData)
{ return get_stream_decoder(memData); }

alureStream *create_stream(ALvoid *userdata, ALenum format, ALuint rate, const UserCallbacks &cb)
{ return new customStream(userdata, format, rate, cb); }