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
#else
 #define ALURE_APIENTRY
#endif

typedef struct alureStream alureStream;

/* Functions that can cause an error will return 0/AL_FALSE/AL_NONE or NULL,
   except where otherwise noted. Most functions will return with an error if an
   existing AL or ALC error is present when called.
   It is not necessary to call an init function to use any of these functions,
   however some functions will require an active context to work on. */

/* alureGetDeviceNames:
   Gets an array of device name strings from OpenAL. This encapsulates
   AL_ENUMERATE_ALL_EXT (if supported and 'all' is true) and standard
   enumeration, with 'count' being set to the number of returned device
   names. */
ALURE_API const ALCchar** ALURE_APIENTRY alureGetDeviceNames(ALCboolean all, ALCsizei *count);

/* alureFreeDeviceNames:
   Frees the device name array returned from alureGetDeviceNames. */
ALURE_API ALvoid ALURE_APIENTRY alureFreeDeviceNames(const ALCchar **names);


/* alureInitDevice:
   Opens the named device, creates a context with the given attributes, and
   sets that context as current. The name and attribute list would be the same
   as what's passed to alcOpenDevice and alcCreateContext respectively. */
ALURE_API ALboolean ALURE_APIENTRY alureInitDevice(const ALCchar *name, const ALCint *attribs);

/* alureShutdownDevice:
   Destroys the current context and closes its associated device. */
ALURE_API ALboolean ALURE_APIENTRY alureShutdownDevice(void);


/* alureGetErrorString:
   Returns a string describing the last error encountered. */
ALURE_API const ALchar* ALURE_APIENTRY alureGetErrorString(void);

/* alureSleep:
   Rests the calling thread for the given number of seconds. */
ALURE_API ALboolean ALURE_APIENTRY alureSleep(ALfloat duration);


/* alureCreateBufferFromFile:
   Loads the given file into an OpenAL buffer object. The formats supported
   depend on the options the library was compiled with, what libraries are
   available at runtime, and the installed decode callbacks. Requires an active
   context. */
ALURE_API ALuint ALURE_APIENTRY alureCreateBufferFromFile(const ALchar *fname);

/* alureCreateBufferFromMemory:
   Loads a file image from memory into an OpenAL buffer object, similar to
   alureCreateBufferFromFile. Requires an active context. */
ALURE_API ALuint ALURE_APIENTRY alureCreateBufferFromMemory(const ALubyte *data, ALsizei length);


/* alureCreateStreamFromFile:
   Opens a file and sets it up for streaming. The given chunkLength is the
   number of bytes each buffer will fill with. ALURE will optionally generate
   the specified number of buffer objects, fill them with the beginning of the
   data, then place the new IDs into the provided storage, before returning.
   Requires an active context. */
ALURE_API alureStream* ALURE_APIENTRY alureCreateStreamFromFile(const ALchar *fname, ALsizei chunkLength, ALsizei numBufs, ALuint *bufs);

/* alureCreateStreamFromMemory:
   Opens a file image from memory and sets it up for streaming, similar to
   alureCreateStreamFromFile. The given data buffer can be safely deleted after
   calling this function. Requires an active context. */
ALURE_API alureStream* ALURE_APIENTRY alureCreateStreamFromMemory(const ALubyte *data, ALuint length, ALsizei chunkLength, ALsizei numBufs, ALuint *bufs);

/* alureCreateStreamFromStaticMemory:
   Identical to alureCreateStreamFromMemory, except the given memory is used
   directly and not duplicated. As a consequence, the data buffer must remain
   valid while the stream is alive. Requires an active context. */
ALURE_API alureStream* ALURE_APIENTRY alureCreateStreamFromStaticMemory(const ALubyte *data, ALuint length, ALsizei chunkLength, ALsizei numBufs, ALuint *bufs);

/* alureBufferDataFromStream:
   Buffers the given buffer objects with the next chunks of data from the
   stream. The number of buffers with new data are returned (0 indicating the
   end of the stream), or -1 on error. The given buffer objects do not need to
   be ones given by the alureCreateStreamFrom* functions. Requires an active
   context. */
ALURE_API ALsizei ALURE_APIENTRY alureBufferDataFromStream(alureStream *stream, ALsizei numBufs, ALuint *bufs);

/* alureRewindStream:
   Rewinds the stream so that the next alureBufferDataFromStream call will
   restart from the beginning of the audio file. */
ALURE_API ALboolean ALURE_APIENTRY alureRewindStream(alureStream *stream);

/* alureDestroyStream:
   Closes an opened stream. For convenience, it will also delete the given
   buffer objects. The given buffer objects do not need to be ones given by the
   alureCreateStreamFrom* functions. Requires an active context. */
ALURE_API ALboolean ALURE_APIENTRY alureDestroyStream(alureStream *stream, ALsizei numBufs, ALuint *bufs);


/* alureInstallDecodeCallbacks:
   Installs callbacks to enable ALURE to handle more file types. The index is
   the order that each given set of callbacks will be tried, starting at the
   most negative number (INT_MIN) and going up. Negative indices will be tried
   before the built-in decoders, and positive indices will be tried after.
   Installing callbacks onto the same index multiple times will remove the
   previous callbacks, and removing old callbacks won't affect any opened files
   using them (they'll continue to use the old functions until properly closed,
   although newly opened files will use the new ones). Passing NULL for all
   callbacks is a valid way to remove an installed set, otherwise all callbacks
   must be specified. The expected behavior of each callback is described
   below. */
ALURE_API ALboolean ALURE_APIENTRY alureInstallDecodeCallbacks(ALint index,
    /* The open_file callback is expected to open the named file and prepare it
       for decoding. If the callbacks cannot decode the file, NULL should be
       returned to indicate failure. Upon success, a non-NULL handle must be
       returned, which will be used as a unique identifier for the decoder
       instance. */
    void*     (*open_file)(const char *filename),
    /* The open_mem callback behaves the same as open_file, except it takes a
       memory segment for input instead of a filename. The given memory will
       remain valid while the instance is open. */
    void*     (*open_mem)(const ALubyte *data, ALuint length),
    /* The get_format callback is used to retrieve the format of the decoded
       data for the given instance. If the format is set to 0, the returned
       channels and bytespersample will be used to figure it out, otherwise
       they are ignored. It is the responsibility if the function to make sure
       the returned format is valid for the current AL context (eg. don't
       return AL_FORMAT_QUAD16 if the AL_EXT_MCFORMATS extension isn't
       available). Returning 0 for blocksize will cause a failure. Returning
       AL_FALSE indicates failure. */
    ALboolean (*get_format)(void *instance, ALenum *format, ALuint *samplerate, ALuint *channels, ALuint *bytespersample, ALuint *blocksize),
    /* The decode callback is called to get more decoded data. Up to the
       specified amount of bytes should be written to the data pointer. The
       number of bytes written should be a multiple of the block size,
       otherwise an OpenAL error may occur during buffering. The function
       should return the number of bytes written. */
    ALuint    (*decode)(void *instance, ALubyte *data, ALuint bytes),
    /* The rewind callback is for rewinding the instance so that the next
       decode calls for it will get audio data from the start of the sound
       file. If the stream fails to rewind, AL_FALSE should be returned. */
    ALboolean (*rewind)(void *instance),
    /* The close callback is called at the end of processing for a particular
       instance. The handle will not be used further and any associated data
       may be deleted. */
    void      (*close)(void *instance));


typedef const ALCchar** (ALURE_APIENTRY *LPALUREGETDEVICENAMES)(ALCboolean,ALCsizei*);
typedef ALvoid          (ALURE_APIENTRY *LPALUREFREEDEVICENAMES)(const ALCchar**);
typedef ALboolean       (ALURE_APIENTRY *LPALUREINITDEVICE)(const ALCchar*,const ALCint*);
typedef ALboolean       (ALURE_APIENTRY *LPALURESHUTDOWNDEVICE)(void);
typedef const ALchar*   (ALURE_APIENTRY *LPALUREGETERRORSTRING)(void);
typedef ALboolean       (ALURE_APIENTRY *LPALURESLEEP)(ALfloat);
typedef ALuint          (ALURE_APIENTRY *LPALURECREATEBUFFERFROMFILE)(const ALchar*);
typedef ALuint          (ALURE_APIENTRY *LPALURECREATEBUFFERFROMMEMORY)(const ALubyte*,ALsizei);
typedef alureStream*    (ALURE_APIENTRY *LPALURECREATESTREAMFROMFILE)(const ALchar*,ALsizei,ALsizei,ALuint*);
typedef alureStream*    (ALURE_APIENTRY *LPALURECREATESTREAMFROMMEMORY)(const ALubyte*,ALuint,ALsizei,ALsizei,ALuint*);
typedef ALsizei         (ALURE_APIENTRY *LPALUREBUFFERDATAFROMSTREAM)(alureStream*,ALsizei,ALuint*);
typedef ALboolean       (ALURE_APIENTRY *LPALUREREWINDSTREAM)(alureStream*);
typedef ALboolean       (ALURE_APIENTRY *LPALUREDESTROYSTREAM)(alureStream*,ALsizei,ALuint*);
typedef ALboolean       (ALURE_APIENTRY *LPALUREINSTALLDECODECALLBACKS)(ALint,void*(*)(const char*),void*(*)(const ALubyte*,ALuint),ALboolean(*)(void*,ALenum*,ALuint*,ALuint*,ALuint*,ALuint*),ALuint(*)(void*,ALubyte*,ALuint),ALboolean(*)(void*),void(*)(void*));

#if defined(__cplusplus)
}  /* extern "C" */
#endif

#endif /* AL_ALURE_H */
