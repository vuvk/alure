#ifndef MAIN_H
#define MAIN_H

#include "AL/alure.h"
#include "alext.h"

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif
#ifdef HAVE_SIGNAL_H
#include <signal.h>
#endif

#ifdef HAVE_WINDOWS_H

#include <windows.h>

#else

#ifdef HAVE_DLFCN_H
#include <dlfcn.h>
#endif

#include <assert.h>
#include <pthread.h>
#ifdef HAVE_PTHREAD_NP_H
#include <pthread_np.h>
#endif
#include <errno.h>

typedef pthread_mutex_t CRITICAL_SECTION;
static inline void EnterCriticalSection(CRITICAL_SECTION *cs)
{
    int ret;
    ret = pthread_mutex_lock(cs);
    assert(ret == 0);
}
static inline void LeaveCriticalSection(CRITICAL_SECTION *cs)
{
    int ret;
    ret = pthread_mutex_unlock(cs);
    assert(ret == 0);
}
static inline void InitializeCriticalSection(CRITICAL_SECTION *cs)
{
    pthread_mutexattr_t attrib;
    int ret;

    ret = pthread_mutexattr_init(&attrib);
    assert(ret == 0);

    ret = pthread_mutexattr_settype(&attrib, PTHREAD_MUTEX_RECURSIVE);
#ifdef HAVE_PTHREAD_NP_H
    if(ret != 0)
        ret = pthread_mutexattr_setkind_np(&attrib, PTHREAD_MUTEX_RECURSIVE);
#endif
    assert(ret == 0);
    ret = pthread_mutex_init(cs, &attrib);
    assert(ret == 0);

    pthread_mutexattr_destroy(&attrib);
}
static inline void DeleteCriticalSection(CRITICAL_SECTION *cs)
{
    int ret;
    ret = pthread_mutex_destroy(cs);
    assert(ret == 0);
}

#endif

#include <map>
#include <streambuf>
#include <istream>
#include <list>
#include <algorithm>
#include <vector>
#include <memory>

static const union {
    int val;
    char b[sizeof(int)];
} endian_test = { 1 };
static const bool LittleEndian = (endian_test.b[0] != 0);
static const bool BigEndian = !LittleEndian;


void *OpenLib(const char *libname);
void CloseLib(void *handle);
#ifndef DYNLOAD
#define LOAD_FUNC(h, x) p##x = x
#else
void *GetLibProc(void *handle, const char *funcname);

template<typename T>
void LoadFunc(void *handle, const char *funcname, T **funcptr)
{ *funcptr = reinterpret_cast<T*>(GetLibProc(handle, funcname)); }

#define LOAD_FUNC(h, x) LoadFunc((h), #x, &(p##x));                          \
if(!(p##x))                                                                  \
{                                                                            \
    CloseLib((h));                                                           \
    (h) = NULL;                                                              \
    break;                                                                   \
}
#endif


void SetError(const char *err);
ALuint DetectBlockAlignment(ALenum format);
ALuint DetectCompressionRate(ALenum format);
ALenum GetSampleFormat(ALuint channels, ALuint bits, bool isFloat);

struct UserCallbacks {
    void*     (*open_file)(const ALchar*);
    void*     (*open_mem)(const ALubyte*,ALuint);
    ALboolean (*get_fmt)(void*,ALenum*,ALuint*,ALuint*);
    ALuint    (*decode)(void*,ALubyte*,ALuint);
    ALboolean (*rewind)(void*);
    void      (*close)(void*);
};
extern std::map<ALint,UserCallbacks> InstalledCallbacks;


struct alureStream {
    // Local copy of memory data
    ALubyte *data;

    // Storage when reading chunks
    std::vector<ALubyte> dataChunk;

    // Abstracted input stream
    std::istream *fstream;

    virtual bool IsValid() = 0;
    virtual bool GetFormat(ALenum*,ALuint*,ALuint*) = 0;
    virtual ALuint GetData(ALubyte*,ALuint) = 0;
    virtual bool Rewind() = 0;
    virtual bool SetOrder(ALuint order)
    {
        if(!order) return Rewind();
        SetError("Invalid order for stream");
        return false;
    }
    virtual bool SetPatchset(const char*)
    { return true; }

    alureStream(std::istream *_stream)
      : data(NULL), fstream(_stream)
    { StreamList.push_front(this); }
    virtual ~alureStream()
    {
        delete[] data;
        StreamList.erase(std::find(StreamList.begin(), StreamList.end(), this));
    }

    static void Clear(void)
    {
        while(StreamList.size() > 0)
            alureDestroyStream(*(StreamList.begin()), 0, NULL);
    }

    static bool Verify(alureStream *stream)
    {
        ListType::iterator i = std::find(StreamList.begin(), StreamList.end(), stream);
        return (i != StreamList.end());
    }

private:
    typedef std::list<alureStream*> ListType;
    static ListType StreamList;
};
void StopStream(alureStream *stream);


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

    virtual int_type underflow();
    virtual pos_type seekoff(off_type offset, std::ios_base::seekdir whence, std::ios_base::openmode mode = std::ios_base::in | std::ios_base::out);
    virtual pos_type seekpos(pos_type pos, std::ios_base::openmode mode = std::ios_base::in | std::ios_base::out);

public:
    MemStreamBuf(const MemDataInfo &data)
      : memInfo(data)
    {
        memInfo.Pos /= sizeof(char_type);
        memInfo.Length /= sizeof(char_type);
    }
    virtual ~MemStreamBuf() { }
};

struct UserFuncs {
    void* (*open)(const char *filename, ALuint mode);
    void (*close)(void *f);
    ALsizei (*read)(void *f, ALubyte *buf, ALuint count);
    ALsizei (*write)(void *f, const ALubyte *buf, ALuint count);
    alureInt64 (*seek)(void *f, alureInt64 offset, int whence);
};
extern UserFuncs Funcs;

class FileStreamBuf : public std::streambuf {
    void *usrFile;
    UserFuncs fio;

    char buffer[1024];

    virtual int_type underflow();
    virtual pos_type seekoff(off_type offset, std::ios_base::seekdir whence, std::ios_base::openmode mode = std::ios_base::in | std::ios_base::out);
    virtual pos_type seekpos(pos_type pos, std::ios_base::openmode mode = std::ios_base::in | std::ios_base::out);

public:
    bool IsOpen()
    { return usrFile != NULL; }

    FileStreamBuf(const char *filename, ALint mode)
      : usrFile(NULL), fio(Funcs)
    { usrFile = fio.open(filename, mode); }
    virtual ~FileStreamBuf()
    { if(usrFile) fio.close(usrFile); }
};

class InStream : public std::istream {
public:
    InStream(const char *filename)
      : std::istream(new FileStreamBuf(filename, 0))
    {
        if(!(static_cast<FileStreamBuf*>(rdbuf())->IsOpen()))
            clear(failbit);
    }
    InStream(const MemDataInfo &memInfo)
      : std::istream(new MemStreamBuf(memInfo))
    { }
    virtual ~InStream()
    { delete rdbuf(); }
};


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


extern CRITICAL_SECTION cs_StreamPlay;

alureStream *create_stream(const char *fname);
alureStream *create_stream(const MemDataInfo &memData);
alureStream *create_stream(ALvoid *userdata, ALenum format, ALuint rate, const UserCallbacks &cb);

template <typename T>
const T& clamp(const T& val, const T& min, const T& max)
{ return std::max(std::min(val, max), min); }

template <typename T>
void swap(T &val1, T &val2)
{
    val1 ^= val2;
    val2 ^= val1;
    val1 ^= val2;
}


struct Decoder {
    typedef std::auto_ptr<alureStream>(*FactoryType)(std::istream*);
    typedef std::vector<FactoryType> ListType;

    static const ListType& GetList();

protected:
    static ListType& AddList(FactoryType func);
};

template<typename T>
struct DecoderDecl : public Decoder {
    DecoderDecl() { AddList(Factory); }
    ~DecoderDecl()
    {
        ListType &list = AddList(NULL);
        list.erase(std::find(list.begin(), list.end(), Factory));
    }

private:
    static std::auto_ptr<alureStream> Factory(std::istream *file)
    {
        std::auto_ptr<alureStream> ret(new T(file));
        if(ret->IsValid()) return ret;
        return std::auto_ptr<alureStream>();
    }
};

#endif // MAIN_H
