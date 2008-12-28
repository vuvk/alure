/* Title: File Streaming */

#include "config.h"

#include "main.h"

#include <string.h>

#include <algorithm>
#include <vector>
#include <memory>
#include <string>


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
    void *wavFile;
    ALenum format;
    int samplerate;
    int blockAlign;
    int sampleSize;
    long dataStart;
    size_t remLen;
    MemDataInfo memInfo;

    struct {
        size_t (*read)(void*,size_t,size_t,void*);
        int    (*seek)(void*,long,int);
        int    (*close)(void*);
        long   (*tell)(void*);
    } fio;

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

        size_t rem = ((remLen >= bytes) ? bytes : remLen);
        size_t got = fio.read(data, blockAlign, rem/blockAlign, wavFile) * blockAlign;
        remLen -= got;

        if(endian.b[0] == 0 && sampleSize > 1)
        {
            if(sampleSize == 2)
                for(size_t i = 0;i < got;i+=2)
                {
                    ALubyte tmp = data[i];
                    data[i] = data[i+1];
                    data[i+1] = tmp;
                }
            else if(sampleSize == 4)
                for(size_t i = 0;i < got;i+=4)
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
        if(fio.seek(wavFile, dataStart, SEEK_SET) == dataStart)
            return true;

        SetError("Seek failed");
        return false;
    }

    wavStream(const char *fname)
      : wavFile(NULL), format(0), dataStart(0)
    {
        FILE *file = fopen(fname, "rb");
        if(file)
        {
            fio.read = read_wrap;
            fio.seek = seek_wrap;
            fio.close = close_wrap;
            fio.tell = tell_wrap;
            if(!Init(file))
                fclose(file);
        }
    }
    wavStream(const MemDataInfo &memData)
      : wavFile(NULL), format(0), dataStart(0), memInfo(memData)
    {
        fio.read = mem_read;
        fio.seek = mem_seek;
        fio.close = NULL;
        fio.tell = mem_tell;
        Init(&memInfo);
    }

    virtual ~wavStream()
    {
        if(wavFile && fio.close)
            fio.close(wavFile);
        wavFile = NULL;
    }

private:
    ALuint read_le32(void *ptr)
    {
        ALubyte buffer[4];
        if(fio.read(buffer, 1, 4, ptr) != 4) return 0;
        return buffer[0] | (buffer[1]<<8) | (buffer[2]<<16) | (buffer[3]<<24);
    }

    ALushort read_le16(void *ptr)
    {
        ALubyte buffer[2];
        if(fio.read(buffer, 1, 2, ptr) != 2) return 0;
        return buffer[0] | (buffer[1]<<8);
    }

    bool Init(void *ptr)
    {
        ALubyte buffer[25];
        int length;

        fio.read(buffer, 1, 12, ptr);
        if(memcmp(buffer, "RIFF", 4) || memcmp(buffer+8, "WAVE", 4))
            return false;

        while(!dataStart || format == AL_NONE)
        {
            char tag[4];
            if(fio.read(tag, 1, 4, ptr) != 4)
                break;

            /* read chunk length */
            length = read_le32(ptr);

            if(memcmp(tag, "fmt ", 4) == 0 && length >= 16)
            {
                /* Data type (should be 1 for PCM data) */
                int type = read_le16(ptr);
                length -= 2;
                if(type != 1)
                    break;

                /* mono or stereo data */
                int channels = read_le16(ptr);
                length -= 2;

                /* sample frequency */
                samplerate = read_le32(ptr);
                length -= 4;

                /* skip four bytes */
                if(fio.read(buffer, 1, 4, ptr) != 4) break;
                length -= 4;

                /* bytes per block */
                blockAlign = read_le16(ptr);
                length -= 2;
                if(blockAlign == 0)
                    break;

                /* bits per sample */
                sampleSize = read_le16(ptr) / 8;
                length -= 2;

                format = alureGetSampleFormat(channels, sampleSize*8, 0);
            }
            else if(memcmp(tag, "data", 4) == 0)
            {
                dataStart = fio.tell(ptr);
                remLen = length;
            }

            fio.seek(ptr, length, SEEK_CUR);
        }

        if(dataStart > 0 && format != AL_NONE)
        {
            fio.seek(ptr, dataStart, SEEK_SET);
            wavFile = ptr;
            return true;
        }
        return false;
    }

public:
    // STDIO-style memory callbacks
    static size_t mem_read(void *ptr, size_t size, size_t nmemb, void *user_data)
    {
        MemDataInfo *data = (MemDataInfo*)user_data;

        if(data->Pos+(size*nmemb) > data->Length)
            nmemb = (data->Length-data->Pos) / size;

        memcpy(ptr, &data->Data[data->Pos], nmemb*size);
        data->Pos += nmemb*size;

        return nmemb;
    }

    static int mem_seek(void *user_data, long offset, int whence)
    {
        MemDataInfo *data = (MemDataInfo*)user_data;
        if(whence == SEEK_CUR)
        {
            if(offset < 0)
            {
                if(-offset > (long)data->Pos)
                    return -1;
            }
            else if(offset+data->Pos > data->Length)
                return -1;
            data->Pos += offset;
        }
        else if(whence == SEEK_SET)
        {
            if(offset < 0 || (unsigned long)offset > data->Length)
                return -1;
            data->Pos = offset;
        }
        else if(whence == SEEK_END)
        {
            if(offset > 0 || -offset > (long)data->Length)
                return -1;
            data->Pos = data->Length + offset;
        }
        else
            return -1;

        return data->Pos;
    }

    static long mem_tell(void *user_data)
    {
        MemDataInfo *data = (MemDataInfo*)user_data;
        return data->Pos;
    }


    static size_t read_wrap(void *ptr, size_t size, size_t nmemb, void *user_data)
    {
        FILE *f = (FILE*)user_data;
        return fread(ptr, size, nmemb, f);
    }

    static int seek_wrap(void *user_data, long offset, int whence)
    {
        FILE *f = (FILE*)user_data;
        return fseek(f, offset, whence);
    }

    static int close_wrap(void *user_data)
    {
        FILE *f = (FILE*)user_data;
        return fclose(f);
    }

    static long tell_wrap(void *user_data)
    {
        FILE *f = (FILE*)user_data;
        return ftell(f);
    }

};

#ifdef HAS_SNDFILE
struct sndStream : public alureStream {
    SNDFILE *sndFile;
    SF_INFO sndInfo;
    MemDataInfo memInfo;

    virtual bool IsValid()
    { return sndFile != NULL; }

    virtual bool GetFormat(ALenum *format, ALuint *frequency, ALuint *blockalign)
    {
        ALenum fmt = alureGetSampleFormat(sndInfo.channels, 16, 0);
        if(fmt == AL_NONE)
            return false;

        *format = fmt;
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
      : sndFile(NULL)
    {
        memset(&sndInfo, 0, sizeof(sndInfo));
        if(sndfile_handle)
            sndFile = psf_open(fname, SFM_READ, &sndInfo);
    }
    sndStream(const MemDataInfo &memData)
      : sndFile(NULL), memInfo(memData)
    {
        memset(&sndInfo, 0, sizeof(sndInfo));
        if(sndfile_handle)
        {
            static SF_VIRTUAL_IO memIO = {
                mem_get_filelen, mem_seek,
                mem_read, mem_write, mem_tell
            };
            sndFile = psf_open_virtual(&memIO, SFM_READ, &sndInfo, &memInfo);
        }
    }

    virtual ~sndStream()
    {
        if(sndFile)
            psf_close(sndFile);
        sndFile = NULL;
    }

    // libSndFile memory callbacks
    static sf_count_t mem_get_filelen(void *user_data)
    {
        MemDataInfo *data = (MemDataInfo*)user_data;
        return data->Length;
    }

    static sf_count_t mem_seek(sf_count_t offset, int whence, void *user_data)
    {
        MemDataInfo *data = (MemDataInfo*)user_data;
        if(whence == SEEK_CUR)
        {
            if(offset < 0)
            {
                if(-offset > data->Pos)
                    return -1;
            }
            else if(offset+data->Pos > data->Length)
                return -1;
            data->Pos += offset;
        }
        else if(whence == SEEK_SET)
        {
            if(offset < 0 || offset > data->Length)
                return -1;
            data->Pos = offset;
        }
        else if(whence == SEEK_END)
        {
            if(offset > 0 || -offset > data->Length)
                return -1;
            data->Pos = data->Length + offset;
        }
        else
            return -1;

        return data->Pos;
    }

    static sf_count_t mem_read(void *ptr, sf_count_t count, void *user_data)
    {
        MemDataInfo *data = (MemDataInfo*)user_data;
        if(count < 0)
            return -1;

        if(count > data->Length - data->Pos)
            count = data->Length - data->Pos;
        memcpy(ptr, &data->Data[data->Pos], count);
        data->Pos += count;

        return count;
    }

    static sf_count_t mem_write(const void*, sf_count_t, void*)
    {
        return -1;
    }

    static sf_count_t mem_tell(void *user_data)
    {
        MemDataInfo *data = (MemDataInfo*)user_data;
        return data->Pos;
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
    MemDataInfo memInfo;

    virtual bool IsValid()
    { return oggFile != NULL; }

    virtual bool GetFormat(ALenum *format, ALuint *frequency, ALuint *blockalign)
    {
        vorbis_info *info = pov_info(oggFile, -1);
        if(!info) return false;

        ALenum fmt = alureGetSampleFormat(info->channels, 16, 0);
        if(fmt == AL_NONE)
            return false;

        *format = fmt;
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
        FILE *f = fopen(fname, "rb");
        if(f)
        {
            const ov_callbacks fileIO = {
                wavStream::read_wrap,  seek_wrap,
                wavStream::close_wrap, wavStream::tell_wrap
            };

            oggFile = new OggVorbis_File;
            if(!vorbisfile_handle ||
               pov_open_callbacks(f, oggFile, NULL, 0, fileIO) != 0)
            {
                delete oggFile;
                oggFile = NULL;
                fclose(f);
            }
        }
    }
    oggStream(const MemDataInfo &memData)
      : oggFile(NULL), oggBitstream(0), memInfo(memData)
    {
        const ov_callbacks memIO = {
            wavStream::mem_read, mem_seek,
            NULL, wavStream::mem_tell
        };

        oggFile = new OggVorbis_File;
        if(!vorbisfile_handle ||
           pov_open_callbacks(&memInfo, oggFile, NULL, 0, memIO) != 0)
        {
            delete oggFile;
            oggFile = NULL;
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

    // libVorbisFile memory callbacks
    static int mem_seek(void *user_data, ogg_int64_t offset, int whence)
    {
        MemDataInfo *data = (MemDataInfo*)user_data;
        if(whence == SEEK_CUR)
        {
            if(offset < 0)
            {
                if(-offset > data->Pos)
                    return -1;
            }
            else if(offset+data->Pos > data->Length)
                return -1;
            data->Pos += offset;
        }
        else if(whence == SEEK_SET)
        {
            if(offset < 0 || offset > data->Length)
                return -1;
            data->Pos = offset;
        }
        else if(whence == SEEK_END)
        {
            if(offset > 0 && -offset > data->Length)
                return -1;
            data->Pos = data->Length + offset;
        }
        else
            return -1;

        return data->Pos;
    }

    static int seek_wrap(void *user_data, ogg_int64_t offset, int whence)
    {
        FILE *f = (FILE*)user_data;
        return fseek(f, offset, whence);
    }
};
#else
struct oggStream : public nullStream {
    oggStream(const char*){}
    oggStream(const MemDataInfo&){}
};
#endif

#ifdef HAS_MPG123
struct mp3Stream : public alureStream {
    mpg123_handle *mp3File;
    long samplerate;
    int channels;
    MemDataInfo memInfo;

    virtual bool IsValid()
    { return mp3File != NULL; }

    virtual bool GetFormat(ALenum *format, ALuint *frequency, ALuint *blockalign)
    {
        ALenum fmt = alureGetSampleFormat(channels, 16, 0);
        if(fmt == AL_NONE)
            return false;

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
            int ret = pmpg123_read(mp3File, data, bytes, &got);

            bytes -= got;
            data += got;
            amt += got;

            if(ret == MPG123_NEED_MORE)
            {
                ALuint insize = std::min(64*1024u, memInfo.Length-memInfo.Pos);
                if(insize > 0 &&
                   pmpg123_decode(mp3File, const_cast<unsigned char*>(memInfo.Data+memInfo.Pos), insize, NULL, 0, NULL) == MPG123_OK)
                {
                    memInfo.Pos += insize;
                    continue;
                }
                memInfo.Pos += insize;
            }

            return amt;
        } while(1);
    }

    virtual bool Rewind()
    {
        if(memInfo.Data)
        {
            mpg123_handle *newFile = pmpg123_new(NULL, NULL);
            if(pmpg123_open_feed(newFile) == MPG123_OK)
            {
                ALuint amt = std::min(64*1024u, memInfo.Length);
                long newrate;
                int newchans;
                int enc;

                if(pmpg123_decode(newFile, const_cast<unsigned char*>(memInfo.Data), amt, NULL, 0, NULL) == MPG123_NEW_FORMAT &&
                   pmpg123_getformat(newFile, &newrate, &newchans, &enc) == MPG123_OK)
                {
                    if(newrate == samplerate && newchans == channels &&
                       (enc == MPG123_ENC_SIGNED_16 ||
                        (pmpg123_format_none(newFile) == MPG123_OK &&
                         pmpg123_format(newFile, samplerate, channels, MPG123_ENC_SIGNED_16) == MPG123_OK)))
                    {
                        // All OK
                        pmpg123_delete(mp3File);
                        mp3File = newFile;
                        memInfo.Pos = amt;
                        return true;
                    }
                }
            }

            pmpg123_delete(newFile);
            SetError("Restart failed");
            return false;
        }

        if(pmpg123_seek_64(mp3File, 0, SEEK_SET) == 0)
            return true;

        SetError("Seek failed");
        return false;
    }

    mp3Stream(const char *fname)
      : mp3File(NULL)
    {
        if(!mpg123_hdl) return;

        mp3File = pmpg123_new(NULL, NULL);
        if(pmpg123_open_64(mp3File, const_cast<char*>(fname)) == MPG123_OK)
        {
            int enc;
            if(pmpg123_read(mp3File, NULL, 0, NULL) == MPG123_NEW_FORMAT &&
               pmpg123_getformat(mp3File, &samplerate, &channels, &enc) == MPG123_OK)
            {
                // Enforce signed 16-bit decoding
                if(enc == MPG123_ENC_SIGNED_16 ||
                   (pmpg123_format_none(mp3File) == MPG123_OK &&
                    pmpg123_format(mp3File, samplerate, channels, MPG123_ENC_SIGNED_16) == MPG123_OK))
                {
                    // All OK
                    return;
                }
            }
        }
        pmpg123_delete(mp3File);
        mp3File = NULL;
    }
    mp3Stream(const MemDataInfo &memData)
      : mp3File(NULL), memInfo(memData)
    {
        if(!mpg123_hdl) return;

        mp3File = pmpg123_new(NULL, NULL);
        if(pmpg123_open_feed(mp3File) == MPG123_OK)
        {
            ALuint amt = std::min(64*1024u, memInfo.Length);
            int enc;

            if(pmpg123_decode(mp3File, const_cast<unsigned char*>(memInfo.Data), amt, NULL, 0, NULL) == MPG123_NEW_FORMAT &&
               pmpg123_getformat(mp3File, &samplerate, &channels, &enc) == MPG123_OK)
            {
                if(enc == MPG123_ENC_SIGNED_16 ||
                   (pmpg123_format_none(mp3File) == MPG123_OK &&
                    pmpg123_format(mp3File, samplerate, channels, MPG123_ENC_SIGNED_16) == MPG123_OK))
                {
                    // All OK
                    memInfo.Pos = amt;
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
    mp3Stream(const char*){}
    mp3Stream(const MemDataInfo&){}
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

    // Try libSndFile
    stream.reset(new sndStream(fdata));
    if(stream->IsValid())
        return stream.release();

    // Try libVorbisFile
    stream.reset(new oggStream(fdata));
    if(stream->IsValid())
        return stream.release();

    // Try libmpg123
    stream.reset(new mp3Stream(fdata));
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
        SetError("Unsupported format");
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
        SetError("Unsupported format");
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
