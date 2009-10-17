#ifndef AL_ALURE_H
#define AL_ALURE_H

#ifdef _WIN32
#include <al.h>
#include <alc.h>
#elif defined(__APPLE__)
#include <OpenAL/alc.h>
#include <OpenAL/al.h>
#else
#include <AL/al.h>
#include <AL/alc.h>
#endif

#if defined(__cplusplus)
extern "C" {
#endif

#if defined(_WIN32)
 #if defined(ALURE_BUILD_LIBRARY)
  #define ALURE_API __declspec(dllexport)
 #else
  #define ALURE_API __declspec(dllimport)
 #endif
#else
 #if defined(ALURE_BUILD_LIBRARY) && defined(HAVE_GCC_VISIBILITY)
  #define ALURE_API __attribute__((visibility("default")))
 #else
  #define ALURE_API extern
 #endif
#endif

#if defined(_WIN32)
 #define ALURE_APIENTRY __cdecl

 #include <sys/types.h>
 typedef __int64 alureInt64;
 typedef unsigned __int64 alureUInt64;
#else
 #define ALURE_APIENTRY

 #include <stdint.h>
 typedef int64_t alureInt64;
 typedef uint64_t alureUInt64;
#endif

typedef struct alureStream alureStream;

ALURE_API void ALURE_APIENTRY alureGetVersion(ALuint *major, ALuint *minor);
ALURE_API const ALchar* ALURE_APIENTRY alureGetErrorString(void);

ALURE_API const ALCchar** ALURE_APIENTRY alureGetDeviceNames(ALCboolean all, ALCsizei *count);
ALURE_API ALvoid ALURE_APIENTRY alureFreeDeviceNames(const ALCchar **names);

ALURE_API ALboolean ALURE_APIENTRY alureInitDevice(const ALCchar *name, const ALCint *attribs);
ALURE_API ALboolean ALURE_APIENTRY alureShutdownDevice(void);

ALURE_API ALenum ALURE_APIENTRY alureGetSampleFormat(ALuint channels, ALuint bits, ALuint floatbits);

ALURE_API ALboolean ALURE_APIENTRY alureSleep(ALfloat duration);

ALURE_API ALuint ALURE_APIENTRY alureCreateBufferFromFile(const ALchar *fname);
ALURE_API ALuint ALURE_APIENTRY alureCreateBufferFromMemory(const ALubyte *data, ALsizei length);
ALURE_API ALboolean ALURE_APIENTRY alureBufferDataFromFile(const ALchar *fname, ALuint buffer);
ALURE_API ALboolean ALURE_APIENTRY alureBufferDataFromMemory(const ALubyte *fdata, ALsizei length, ALuint buffer);

ALURE_API alureStream* ALURE_APIENTRY alureCreateStreamFromFile(const ALchar *fname, ALsizei chunkLength, ALsizei numBufs, ALuint *bufs);
ALURE_API alureStream* ALURE_APIENTRY alureCreateStreamFromMemory(const ALubyte *data, ALuint length, ALsizei chunkLength, ALsizei numBufs, ALuint *bufs);
ALURE_API alureStream* ALURE_APIENTRY alureCreateStreamFromStaticMemory(const ALubyte *data, ALuint length, ALsizei chunkLength, ALsizei numBufs, ALuint *bufs);
ALURE_API alureStream* ALURE_APIENTRY alureCreateStreamFromCallback(
    ALuint (*callback)(void *userdata, ALubyte *data, ALuint bytes),
    void *userdata, ALenum format, ALuint samplerate,
    ALsizei chunkLength, ALsizei numBufs, ALuint *bufs);
ALURE_API ALboolean ALURE_APIENTRY alureGetStreamFormat(alureStream *stream,
    ALenum *format, ALuint *frequency, ALuint *blockAlign);
ALURE_API ALsizei ALURE_APIENTRY alureBufferDataFromStream(alureStream *stream, ALsizei numBufs, ALuint *bufs);
ALURE_API ALboolean ALURE_APIENTRY alureRewindStream(alureStream *stream);
ALURE_API ALboolean ALURE_APIENTRY alureDestroyStream(alureStream *stream, ALsizei numBufs, ALuint *bufs);

ALURE_API ALboolean ALURE_APIENTRY alurePlaySourceStream(ALuint source,
    alureStream *stream, ALsizei numBufs, ALsizei loopcount,
    void (*eos_callback)(void *userdata, ALuint source), void *userdata);
ALURE_API ALboolean ALURE_APIENTRY alurePlaySource(ALuint source,
    void (*callback)(void *userdata, ALuint source), void *userdata);
ALURE_API ALboolean ALURE_APIENTRY alureStopSource(ALuint source, ALboolean run_callback);
ALURE_API alureUInt64 ALURE_APIENTRY alureGetSourceOffset(ALuint source);

ALURE_API ALboolean ALURE_APIENTRY alureInstallDecodeCallbacks(ALint index,
    void*     (*open_file)(const ALchar*),
    void*     (*open_mem)(const ALubyte*,ALuint),
    ALboolean (*get_format)(void*,ALenum*,ALuint*,ALuint*),
    ALuint    (*decode)(void*,ALubyte*,ALuint),
    ALboolean (*rewind)(void*),
    void      (*close)(void*));
ALURE_API ALboolean ALURE_APIENTRY alureSetIOCallbacks(
      void* (*open)(const char*,ALuint),
      void (*close)(void*),
      ALsizei (*read)(void*,ALubyte*,ALuint),
      ALsizei (*write)(void*,const ALubyte*,ALuint),
      alureInt64 (*seek)(void*,alureInt64,int));

ALURE_API void* ALURE_APIENTRY alureGetProcAddress(const ALchar *funcname);

typedef void            (ALURE_APIENTRY *LPALUREGETVERSION)(ALuint*,ALuint*);
typedef const ALchar*   (ALURE_APIENTRY *LPALUREGETERRORSTRING)(void);
typedef const ALCchar** (ALURE_APIENTRY *LPALUREGETDEVICENAMES)(ALCboolean,ALCsizei*);
typedef ALvoid          (ALURE_APIENTRY *LPALUREFREEDEVICENAMES)(const ALCchar**);
typedef ALboolean       (ALURE_APIENTRY *LPALUREINITDEVICE)(const ALCchar*,const ALCint*);
typedef ALboolean       (ALURE_APIENTRY *LPALURESHUTDOWNDEVICE)(void);
typedef ALenum          (ALURE_APIENTRY *LPALUREGETSAMPLEFORMAT)(ALuint,ALuint,ALuint);
typedef ALboolean       (ALURE_APIENTRY *LPALURESLEEP)(ALfloat);
typedef ALuint          (ALURE_APIENTRY *LPALURECREATEBUFFERFROMFILE)(const ALchar*);
typedef ALuint          (ALURE_APIENTRY *LPALURECREATEBUFFERFROMMEMORY)(const ALubyte*,ALsizei);
typedef ALboolean       (ALURE_APIENTRY *LPALUREBUFFERDATAFROMFILE)(const ALchar *fname, ALuint buffer);
typedef ALboolean       (ALURE_APIENTRY *LPALUREBUFFERDATAFROMMEMORY)(const ALubyte *fdata, ALsizei length, ALuint buffer);
typedef alureStream*    (ALURE_APIENTRY *LPALURECREATESTREAMFROMFILE)(const ALchar*,ALsizei,ALsizei,ALuint*);
typedef alureStream*    (ALURE_APIENTRY *LPALURECREATESTREAMFROMMEMORY)(const ALubyte*,ALuint,ALsizei,ALsizei,ALuint*);
typedef alureStream*    (ALURE_APIENTRY *LPALURECREATESTREAMFROMSTATICMEMORY)(const ALubyte*,ALuint,ALsizei,ALsizei,ALuint*);
typedef alureStream*    (ALURE_APIENTRY *LPALURECREATESTREAMFROMCALLBACK)(ALuint(*)(void*,ALubyte*,ALuint),void*,ALenum,ALuint,ALsizei,ALsizei,ALuint*);
typedef ALboolean       (ALURE_APIENTRY *LPALUREGETSTREAMFORMAT)(alureStream*,ALenum*,ALuint*,ALuint*);
typedef ALsizei         (ALURE_APIENTRY *LPALUREBUFFERDATAFROMSTREAM)(alureStream*,ALsizei,ALuint*);
typedef ALboolean       (ALURE_APIENTRY *LPALUREREWINDSTREAM)(alureStream*);
typedef ALboolean       (ALURE_APIENTRY *LPALUREDESTROYSTREAM)(alureStream*,ALsizei,ALuint*);
typedef ALboolean       (ALURE_APIENTRY *LPALUREPLAYSOURCESTREAM)(ALuint,alureStream*,ALsizei,ALsizei,void(*)(void*,ALuint),void*);
typedef ALboolean       (ALURE_APIENTRY *LPALUREPLAYSOURCE)(ALuint,void(*)(void*,ALuint),void*);
typedef ALboolean       (ALURE_APIENTRY *LPALURESTOPSOURCE)(ALuint,ALboolean);
typedef alureUInt64     (ALURE_APIENTRY *LPALUREGETSOURCEOFFSET)(ALuint);
typedef ALboolean       (ALURE_APIENTRY *LPALUREINSTALLDECODECALLBACKS)(ALint,void*(*)(const char*),void*(*)(const ALubyte*,ALuint),ALboolean(*)(void*,ALenum*,ALuint*,ALuint*),ALuint(*)(void*,ALubyte*,ALuint),ALboolean(*)(void*),void(*)(void*));
typedef ALboolean       (ALURE_APIENTRY *LPALURESETIOCALLBACKS)(void*(*)(const char*,ALuint),void(*)(void*),ALsizei(*)(void*,ALubyte*,ALuint),ALsizei(*)(void*,const ALubyte*,ALuint),alureInt64(*)(void*,alureInt64,int));
typedef void*           (ALURE_APIENTRY *LPALUREGETPROCADDRESS)(const ALchar*);

#if defined(__cplusplus)
}  /* extern "C" */
#endif

#endif /* AL_ALURE_H */
