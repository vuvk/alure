#ifndef MAIN_H
#define MAIN_H

#include "AL/alure.h"
#include "alext.h"

#ifdef HAS_SNDFILE
#include <sndfile.h>
#endif
#ifdef HAS_VORBISFILE
#include <vorbis/vorbisfile.h>
#endif
#ifdef HAS_FLAC
#include <FLAC/all.h>
#endif

#ifdef HAVE_WINDOWS_H

#include <windows.h>

#else

#ifdef HAVE_DLFCN_H
#include <dlfcn.h>
#endif

#endif

#include <map>
#include <streambuf>
#include <istream>


void init_alure();

void SetError(const char *err);

struct UserCallbacks {
    void*     (*open_file)(const ALchar*);
    void*     (*open_mem)(const ALubyte*,ALuint);
    ALboolean (*get_fmt)(void*,ALenum*,ALuint*,ALuint*);
    ALuint    (*decode)(void*,ALubyte*,ALuint);
    ALboolean (*rewind)(void*);
    void      (*close)(void*);
};
extern std::map<ALint,UserCallbacks> InstalledCallbacks;


#define MAKE_FUNC(x) extern typeof(x) * p##x
#ifdef HAS_SNDFILE
extern void *sndfile_handle;
MAKE_FUNC(sf_open);
MAKE_FUNC(sf_open_virtual);
MAKE_FUNC(sf_close);
MAKE_FUNC(sf_readf_short);
MAKE_FUNC(sf_seek);
#endif
#ifdef HAS_VORBISFILE
extern void *vorbisfile_handle;
MAKE_FUNC(ov_open_callbacks);
MAKE_FUNC(ov_clear);
MAKE_FUNC(ov_info);
MAKE_FUNC(ov_read);
MAKE_FUNC(ov_pcm_seek);
#endif
#ifdef HAS_FLAC
extern void *flac_handle;
MAKE_FUNC(FLAC__stream_decoder_get_state);
MAKE_FUNC(FLAC__stream_decoder_get_channels);
MAKE_FUNC(FLAC__stream_decoder_init_file);
MAKE_FUNC(FLAC__stream_decoder_finish);
MAKE_FUNC(FLAC__stream_decoder_new);
MAKE_FUNC(FLAC__stream_decoder_get_blocksize);
MAKE_FUNC(FLAC__stream_decoder_get_bits_per_sample);
MAKE_FUNC(FLAC__stream_decoder_seek_absolute);
MAKE_FUNC(FLAC__stream_decoder_delete);
MAKE_FUNC(FLAC__stream_decoder_get_sample_rate);
MAKE_FUNC(FLAC__stream_decoder_process_single);
MAKE_FUNC(FLAC__stream_decoder_init_stream);
#endif
#undef MAKE_FUNC

struct alureStream {
    ALubyte *data;

    ALubyte *dataChunk;
    ALsizei chunkLen;

    virtual bool IsValid() = 0;
    virtual bool GetFormat(ALenum*,ALuint*,ALuint*) = 0;
    virtual ALuint GetData(ALubyte*,ALuint) = 0;
    virtual bool Rewind() = 0;

    alureStream() : data(NULL), dataChunk(NULL)
    { }
    virtual ~alureStream()
    { delete[] data; delete[] dataChunk; }
};

struct MemDataInfo {
    const ALubyte *Data;
    size_t Length;
    size_t Pos;

    MemDataInfo() : Data(NULL), Length(0), Pos(0)
    { }
    MemDataInfo(const MemDataInfo &inf) : Data(inf.Data), Length(inf.Length),
                                          Pos(inf.Pos)
    { }
};

class MemStreamBuf : public std::streambuf {
    MemDataInfo memInfo;

    virtual int_type underflow()
    {
        if(gptr() == egptr())
        {
            char_type *data = (char_type*)memInfo.Data;
            setg(data, data + memInfo.Pos, data + memInfo.Length);
            memInfo.Pos = memInfo.Length;
        }
        if(gptr() == egptr())
            return int_type(-1);
        return *gptr();
    }

    virtual pos_type seekoff(off_type offset, std::ios_base::seekdir whence, std::ios_base::openmode mode = std::ios_base::in | std::ios_base::out)
    {
        if((mode&std::ios_base::out))
            return traits_type::eof();

        ALuint ptell = memInfo.Pos;

        switch(whence)
        {
            case std::ios_base::beg:
                break;
            case std::ios_base::cur:
                if(offset == 0)
                    return pos_type(ptell) - pos_type(egptr()-gptr());
                offset += off_type(ptell) - off_type(egptr()-gptr());
                break;
            case std::ios_base::end:
                offset += off_type(memInfo.Length);
                break;
            default:
                return pos_type(off_type(-1));
        }

        return seekpos(pos_type(offset), mode);
    }

    virtual pos_type seekpos(pos_type pos, std::ios_base::openmode mode = std::ios_base::in | std::ios_base::out)
    {
        if((mode&std::ios_base::out))
            return pos_type(off_type(-1));

        if(pos < 0 || pos > pos_type(memInfo.Length))
            return pos_type(off_type(-1));
        memInfo.Pos = pos;

        setg(0, 0, 0);
        return pos;
    }

public:
    MemStreamBuf(const MemDataInfo &data)
      : memInfo(data)
    {
        memInfo.Pos /= sizeof(char_type);
        memInfo.Length /= sizeof(char_type);
    }
    virtual ~MemStreamBuf() { }
};

class IMemStream : public std::istream {
public:
    IMemStream(const MemDataInfo &memInfo)
      : std::istream(new MemStreamBuf(memInfo))
    { }
    virtual ~IMemStream()
    { delete rdbuf(); }
};

template <typename T>
alureStream *create_stream(const T &fdata);

#endif // MAIN_H
