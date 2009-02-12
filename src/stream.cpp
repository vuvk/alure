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
    std::istream *wavFile;
    ALenum format;
    int samplerate;
    int blockAlign;
    int sampleSize;
    long dataStart;
    size_t remLen;

    virtual bool IsValid()
    { return wavFile != NULL; }

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
        wavFile->read(reinterpret_cast<char*>(data), rem*blockAlign);

        std::streamsize got = wavFile->gcount();
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
        wavFile->clear();
        if(wavFile->seekg(dataStart))
            return true;

        SetError("Seek failed");
        return false;
    }

    wavStream(const char *fname)
      : wavFile(NULL), format(0), dataStart(0)
    {
        wavFile = new std::ifstream(fname, std::ios_base::in|std::ios_base::binary);
        Init();
    }
    wavStream(const MemDataInfo &memData)
      : wavFile(NULL), format(0), dataStart(0)
    {
        wavFile = new IMemStream(memData);
        Init();
    }

    virtual ~wavStream()
    {
        delete wavFile;
        wavFile = NULL;
    }

private:
    void Init()
    {
        ALubyte buffer[25];
        int length;

        if(!wavFile->read(reinterpret_cast<char*>(buffer), 12) ||
           memcmp(buffer, "RIFF", 4) != 0 || memcmp(buffer+8, "WAVE", 4) != 0)
        {
            delete wavFile;
            wavFile = NULL;

            return;
        }

        while(!dataStart || format == AL_NONE)
        {
            char tag[4];
            if(!wavFile->read(tag, 4))
                break;

            /* read chunk length */
            length = read_le32(wavFile);

            if(memcmp(tag, "fmt ", 4) == 0 && length >= 16)
            {
                /* Data type (should be 1 for PCM data) */
                int type = read_le16(wavFile);
                if(type != 1)
                    break;

                /* mono or stereo data */
                int channels = read_le16(wavFile);

                /* sample frequency */
                samplerate = read_le32(wavFile);

                /* skip four bytes */
                wavFile->ignore(4);

                /* bytes per block */
                blockAlign = read_le16(wavFile);
                if(blockAlign == 0)
                    break;

                /* bits per sample */
                sampleSize = read_le16(wavFile) / 8;

                format = alureGetSampleFormat(channels, sampleSize*8, 0);

                length -= 16;
            }
            else if(memcmp(tag, "data", 4) == 0)
            {
                dataStart = wavFile->tellg();
                remLen = length;
            }

            wavFile->seekg(length, std::ios_base::cur);
        }

        if(dataStart <= 0 || format == AL_NONE)
        {
            delete wavFile;
            wavFile = NULL;
        }
        else
            wavFile->seekg(dataStart);
    }
};

struct aiffStream : public alureStream {
    std::istream *aiffFile;
    ALenum format;
    int samplerate;
    int blockAlign;
    int sampleSize;
    long dataStart;
    size_t remLen;

    virtual bool IsValid()
    { return aiffFile != NULL; }

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
        aiffFile->read(reinterpret_cast<char*>(data), rem*blockAlign);

        std::streamsize got = aiffFile->gcount();
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
        aiffFile->clear();
        if(aiffFile->seekg(dataStart))
            return true;

        SetError("Seek failed");
        return false;
    }

    aiffStream(const char *fname)
      : aiffFile(NULL), format(0), dataStart(0)
    {
        aiffFile = new std::ifstream(fname, std::ios_base::in|std::ios_base::binary);
        Init();
    }
    aiffStream(const MemDataInfo &memData)
      : aiffFile(NULL), format(0), dataStart(0)
    {
        aiffFile = new IMemStream(memData);
        Init();
    }

    virtual ~aiffStream()
    {
        delete aiffFile;
        aiffFile = NULL;
    }

private:
    void Init()
    {
        ALubyte buffer[25];
        int length;

        if(!aiffFile->read(reinterpret_cast<char*>(buffer), 12) ||
           memcmp(buffer, "FORM", 4) != 0 || memcmp(buffer+8, "AIFF", 4) != 0)
        {
            delete aiffFile;
            aiffFile = NULL;
            return;
        }

        while(!dataStart || format == AL_NONE)
        {
            char tag[4];
            if(!aiffFile->read(tag, 4))
                break;

            /* read chunk length */
            length = read_be32(aiffFile);

            if(memcmp(tag, "COMM", 4) == 0 && length >= 18)
            {
                /* mono or stereo data */
                int channels = read_be16(aiffFile);

                /* number of sample frames */
                aiffFile->ignore(4);

                /* bits per sample */
                sampleSize = read_be16(aiffFile) / 8;

                /* sample frequency */
                samplerate = read_be80extended(aiffFile);

                /* block alignment */
                blockAlign = channels * sampleSize;

                format = alureGetSampleFormat(channels, sampleSize*8, 0);

                length -= 18;
            }
            else if(memcmp(tag, "SSND", 4) == 0)
            {
                dataStart = aiffFile->tellg();
                dataStart += 8;
                remLen = length - 8;
            }

            aiffFile->seekg(length, std::ios_base::cur);
        }

        if(dataStart <= 0 || format == AL_NONE)
        {
            delete aiffFile;
            aiffFile = NULL;
        }
        else
            aiffFile->seekg(dataStart);
    }
};

#ifdef HAS_SNDFILE
struct sndStream : public alureStream {
    SNDFILE *sndFile;
    SF_INFO sndInfo;
    std::istream *fstream;

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
        return psf_readf_short(sndFile, (short*)data, bytes/frameSize) * frameSize;
    }

    virtual bool Rewind()
    {
        if(psf_seek(sndFile, 0, SEEK_SET) != -1)
            return true;

        SetError("Seek failed");
        return false;
    }

    sndStream(const char *fname)
      : sndFile(NULL), fstream(NULL)
    {
        memset(&sndInfo, 0, sizeof(sndInfo));
        if(sndfile_handle)
        {
            static SF_VIRTUAL_IO streamIO = {
                get_filelen, seek,
                read, write, tell
            };
            fstream = new std::ifstream(fname, std::ios_base::in|std::ios_base::binary);
            sndFile = psf_open_virtual(&streamIO, SFM_READ, &sndInfo, fstream);
        }
    }
    sndStream(const MemDataInfo &memData)
      : sndFile(NULL), fstream(NULL)
    {
        memset(&sndInfo, 0, sizeof(sndInfo));
        if(sndfile_handle)
        {
            static SF_VIRTUAL_IO streamIO = {
                get_filelen, seek,
                read, write, tell
            };
            fstream = new IMemStream(memData);
            sndFile = psf_open_virtual(&streamIO, SFM_READ, &sndInfo, fstream);
        }
    }

    virtual ~sndStream()
    {
        if(sndFile)
            psf_close(sndFile);
        sndFile = NULL;

        delete fstream;
        fstream = NULL;
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
    sndStream(const char*){}
    sndStream(const MemDataInfo&){}
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
        vorbis_info *info = pov_info(oggFile, -1);
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

        vorbis_info *info = pov_info(oggFile, -1);
        if(!info) return 0;

        ALuint blockAlign = info->channels*2;
        bytes -= bytes%blockAlign;

        int got = 0;
        while(bytes > 0)
        {
            int res = pov_read(oggFile, (char*)&data[got], bytes, endian.b[0], 2, 1, &oggBitstream);
            if(res <= 0)
                break;
            bytes -= res;
            got += res;
        }
        return got;
    }

    virtual bool Rewind()
    {
        if(pov_pcm_seek(oggFile, 0) == 0)
            return true;

        SetError("Seek failed");
        return false;
    }

    oggStream(const char *fname)
      : oggFile(NULL), oggBitstream(0)
    {
        std::istream *f = new std::ifstream(fname, std::ios_base::in|std::ios_base::binary);
        const ov_callbacks streamIO = {
            read, seek, close, tell
        };

        oggFile = new OggVorbis_File;
        if(!vorbisfile_handle ||
           pov_open_callbacks(f, oggFile, NULL, 0, streamIO) != 0)
        {
            delete oggFile;
            oggFile = NULL;
            delete f;
        }
    }
    oggStream(const MemDataInfo &memData)
      : oggFile(NULL), oggBitstream(0)
    {
        std::istream *f = new IMemStream(memData);
        const ov_callbacks streamIO = {
            read, seek, close, tell
        };

        oggFile = new OggVorbis_File;
        if(!vorbisfile_handle ||
           pov_open_callbacks(f, oggFile, NULL, 0, streamIO) != 0)
        {
            delete oggFile;
            oggFile = NULL;
            delete f;
        }
    }

    virtual ~oggStream()
    {
        if(oggFile)
        {
            pov_clear(oggFile);
            delete oggFile;
        }
    }

private:
    // libVorbisFile iostream callbacks
    static int seek(void *user_data, ogg_int64_t offset, int whence)
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

    static size_t read(void *ptr, size_t size, size_t nmemb, void *user_data)
    {
        std::istream *stream = static_cast<std::istream*>(user_data);
        stream->clear();

        stream->read(static_cast<char*>(ptr), nmemb*size);
        size_t ret = stream->gcount();
        return ret/size;
    }

    static long tell(void *user_data)
    {
        std::istream *stream = static_cast<std::istream*>(user_data);
        stream->clear();
        return stream->tellg();
    }

    static int close(void *user_data)
    {
        std::istream *stream = static_cast<std::istream*>(user_data);
        delete stream;
        return 0;
    }
};
#else
struct oggStream : public nullStream {
    oggStream(const char*){}
    oggStream(const MemDataInfo&){}
};
#endif

#ifdef HAS_FLAC
struct flacStream : public alureStream {
    FLAC__StreamDecoder *flacFile;
    ALenum format;
    ALuint samplerate;
    ALuint blockAlign;
    std::istream *fstream;

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
            ALuint rem = std::min(initialData.size(), bytes);
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

    flacStream(const char *fname)
      : flacFile(NULL), fstream(NULL)
    {
        if(!flac_handle) return;

        flacFile = pFLAC__stream_decoder_new();
        if(flacFile)
        {
            fstream = new std::ifstream(fname, std::ios_base::in|std::ios_base::binary);
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
    flacStream(const MemDataInfo &memData)
      : flacFile(NULL), fstream(NULL)
    {
        if(!flac_handle) return;

        flacFile = pFLAC__stream_decoder_new();
        if(flacFile)
        {
            fstream = new IMemStream(memData);
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
        delete fstream;
        fstream = NULL;
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
        {
            blockAlign = pFLAC__stream_decoder_get_channels(flacFile) *
                         pFLAC__stream_decoder_get_bits_per_sample(flacFile)/8;
            samplerate = pFLAC__stream_decoder_get_sample_rate(flacFile);
            format = alureGetSampleFormat(pFLAC__stream_decoder_get_channels(flacFile),
                                          pFLAC__stream_decoder_get_bits_per_sample(flacFile), 0);
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
    flacStream(const char*){}
    flacStream(const MemDataInfo&){}
};
#endif


template <typename T>
alureStream *create_stream(const T &fdata)
{
    std::auto_ptr<alureStream> stream(NULL);

    std::map<ALint,UserCallbacks>::iterator i = InstalledCallbacks.begin();
    while(i != InstalledCallbacks.end() && i->first < 0)
    {
        stream.reset(new customStream(fdata, i->second));
        if(stream->IsValid())
            return stream.release();
        i++;
    }

    stream.reset(new wavStream(fdata));
    if(stream->IsValid())
        return stream.release();

    stream.reset(new aiffStream(fdata));
    if(stream->IsValid())
        return stream.release();

    // Try libSndFile
    stream.reset(new sndStream(fdata));
    if(stream->IsValid())
        return stream.release();

    // Try libVorbisFile
    stream.reset(new oggStream(fdata));
    if(stream->IsValid())
        return stream.release();

    // Try libFLAC
    stream.reset(new flacStream(fdata));
    if(stream->IsValid())
        return stream.release();

    while(i != InstalledCallbacks.end())
    {
        stream.reset(new customStream(fdata, i->second));
        if(stream->IsValid())
            return stream.release();
        i++;
    }

    return stream.release();
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
    init_alure();

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
        SetError("Unsupported type");
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
    init_alure();

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
        SetError("Unsupported type");
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
    init_alure();

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
        SetError("Unsupported type");
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
    init_alure();

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

    delete stream;
    return AL_TRUE;
}

}
