#include "config.h"

#include "main.h"

#include <string.h>
#include <errno.h>
#include <time.h>
#ifdef HAVE_WINDOWS_H
#include <windows.h>
#endif

#include <vector>

const ALchar *last_error = "No error";

#define MAKE_FUNC(x) typeof(x) * p##x
#ifdef HAS_SNDFILE
void *sndfile_handle;
MAKE_FUNC(sf_open);
MAKE_FUNC(sf_open_virtual);
MAKE_FUNC(sf_close);
MAKE_FUNC(sf_readf_short);
MAKE_FUNC(sf_seek);
#endif
#ifdef HAS_VORBISFILE
void *vorbisfile_handle;
MAKE_FUNC(ov_open_callbacks);
MAKE_FUNC(ov_clear);
MAKE_FUNC(ov_info);
MAKE_FUNC(ov_read);
MAKE_FUNC(ov_pcm_seek);
#endif
#ifdef HAS_MPG123
void *mpg123_hdl;
MAKE_FUNC(mpg123_init);
MAKE_FUNC(mpg123_new);
MAKE_FUNC(mpg123_open_64);
MAKE_FUNC(mpg123_open_feed);
MAKE_FUNC(mpg123_delete);
MAKE_FUNC(mpg123_decode);
MAKE_FUNC(mpg123_read);
MAKE_FUNC(mpg123_format_none);
MAKE_FUNC(mpg123_getformat);
MAKE_FUNC(mpg123_format);
MAKE_FUNC(mpg123_seek_64);
#endif
#undef MAKE_FUNC


static void init_libs()
{
#if defined(HAVE_WINDOWS_H) && defined(HAS_LOADLIBRARY)
# define LOAD_FUNC(x, f) do { \
    p##f = reinterpret_cast<typeof(f)*>(GetProcAddress((HMODULE)x, #f)); \
    if(!(p##f)) \
        fprintf(stderr, "Could not load "#f"\n"); \
} while(0)

#ifdef HAS_SNDFILE
    sndfile_handle = LoadLibrary("sndfile.dll");
#endif
#ifdef HAS_VORBISFILE
    vorbisfile_handle = LoadLibrary("vorbisfile.dll");
#endif
#ifdef HAS_MPG123
    mpg123_hdl = LoadLibrary("mpg123.dll");
#endif

#elif defined(HAS_DLOPEN)
# define LOAD_FUNC(x, f) do { \
    p##f = reinterpret_cast<typeof(f)*>(dlsym(x, #f)); \
    if((err=dlerror()) != NULL) { \
        fprintf(stderr, "Could not load "#f": %s\n", err); \
        p##f = NULL; \
    } \
} while(0)

#ifdef __APPLE__
# define VER_PREFIX
# define VER_POSTFIX ".dylib"
#else
# define VER_PREFIX ".so"
# define VER_POSTFIX
#endif

    const char *err;
#ifdef HAS_SNDFILE
    sndfile_handle = dlopen("libsndfile"VER_PREFIX".1"VER_POSTFIX, RTLD_NOW);
#endif
#ifdef HAS_VORBISFILE
    vorbisfile_handle = dlopen("libvorbisfile"VER_PREFIX".3"VER_POSTFIX, RTLD_NOW);
#endif
#ifdef HAS_MPG123
    mpg123_hdl = dlopen("libmpg123"VER_PREFIX".0"VER_POSTFIX, RTLD_NOW);
#endif

#undef VER_PREFIX
#undef VER_POSTFIX

#else
# define LOAD_FUNC(m, x) (p##x = x)

#ifdef HAS_SNDFILE
    sndfile_handle = (void*)0xDECAFBAD;
#endif
#ifdef HAS_VORBISFILE
    vorbisfile_handle = (void*)0xDEADBEEF;
#endif
#ifdef HAS_MPG123
    mpg123_hdl = (void*)0xD00FBA11;
#endif
#endif


#ifdef HAS_SNDFILE
    if(sndfile_handle)
    {
        LOAD_FUNC(sndfile_handle, sf_open);
        LOAD_FUNC(sndfile_handle, sf_open_virtual);
        LOAD_FUNC(sndfile_handle, sf_close);
        LOAD_FUNC(sndfile_handle, sf_readf_short);
        LOAD_FUNC(sndfile_handle, sf_seek);
        if(!psf_open || !psf_open_virtual || !psf_close || !psf_readf_short ||
           !psf_seek)
            sndfile_handle = NULL;
    }
#endif
#ifdef HAS_VORBISFILE
    if(vorbisfile_handle)
    {
        LOAD_FUNC(vorbisfile_handle, ov_open_callbacks);
        LOAD_FUNC(vorbisfile_handle, ov_clear);
        LOAD_FUNC(vorbisfile_handle, ov_info);
        LOAD_FUNC(vorbisfile_handle, ov_read);
        LOAD_FUNC(vorbisfile_handle, ov_pcm_seek);
        if(!pov_open_callbacks || !pov_clear || !pov_info || !pov_read ||
           !pov_pcm_seek)
            vorbisfile_handle = NULL;
    }
#endif
#ifdef HAS_MPG123
    if(mpg123_hdl)
    {
        LOAD_FUNC(mpg123_hdl, mpg123_init);
        LOAD_FUNC(mpg123_hdl, mpg123_new);
        LOAD_FUNC(mpg123_hdl, mpg123_open_64);
        LOAD_FUNC(mpg123_hdl, mpg123_open_feed);
        LOAD_FUNC(mpg123_hdl, mpg123_decode);
        LOAD_FUNC(mpg123_hdl, mpg123_read);
        LOAD_FUNC(mpg123_hdl, mpg123_getformat);
        LOAD_FUNC(mpg123_hdl, mpg123_format_none);
        LOAD_FUNC(mpg123_hdl, mpg123_format);
        LOAD_FUNC(mpg123_hdl, mpg123_delete);
        LOAD_FUNC(mpg123_hdl, mpg123_seek_64);
        if(!pmpg123_init || !pmpg123_new || !pmpg123_open_64 ||
           !pmpg123_open_feed || !pmpg123_decode || !pmpg123_read ||
           !pmpg123_getformat || !pmpg123_format_none || !pmpg123_format ||
           !pmpg123_delete || !pmpg123_seek_64 || pmpg123_init() != MPG123_OK)
            mpg123_hdl = NULL;
    }
#endif

#undef LOAD_FUNC
}

void init_alure()
{
    static bool done = false;
    if(done) return;
    done = true;

    init_libs();
}

extern "C" {

/* Function: alureGetErrorString
 *
 * Returns a string describing the last error encountered.
 */
ALURE_API const ALchar* ALURE_APIENTRY alureGetErrorString(void)
{
    const ALchar *ret = last_error;
    last_error = "No error";
    return ret;
}


/* Function: alureGetDeviceNames
 *
 * Gets an array of device name strings from OpenAL. This encapsulates
 * AL_ENUMERATE_ALL_EXT (if supported and 'all' is true) and standard
 * enumeration, with 'count' being set to the number of returned device
 * names. Returns NULL on error.
 *
 * See Also: <alureFreeDeviceNames>
 */
ALURE_API const ALCchar** ALURE_APIENTRY alureGetDeviceNames(ALCboolean all, ALCsizei *count)
{
    init_alure();

    const ALCchar *list = NULL;
    if(all && alcIsExtensionPresent(NULL, "ALC_ENUMERATE_ALL_EXT"))
        list = alcGetString(NULL, ALC_ALL_DEVICES_SPECIFIER);
    else
        list = alcGetString(NULL, ALC_DEVICE_SPECIFIER);
    if(!list)
    {
        alcGetError(NULL);
        last_error = "No device names found";
        return NULL;
    }

    const ALCchar *cur = list;
    ALuint retlistLen = 0;
    while(*cur)
    {
        cur += strlen(cur)+1;
        retlistLen++;
    }

    const ALCchar **retlist = new const ALCchar*[retlistLen];
    retlistLen = 0;
    cur = list;
    while(*cur)
    {
        retlist[retlistLen] = cur;
        cur += strlen(cur)+1;
        retlistLen++;
    }

    *count = retlistLen;
    return retlist;
}

/* Function: alureFreeDeviceNames
 *
 * Frees the device name array returned from alureGetDeviceNames.
 *
 * See Also: <alureGetDeviceNames>
 */
ALURE_API ALvoid ALURE_APIENTRY alureFreeDeviceNames(const ALCchar **names)
{
    init_alure();

    delete[] names;
}


/* Function: alureInitDevice
 *
 * Opens the named device, creates a context with the given attributes, and
 * sets that context as current. The name and attribute list would be the same
 * as what's passed to alcOpenDevice and alcCreateContext respectively. Returns
 * AL_FALSE on error.
 *
 * See Also: <alureShutdownDevice>
 */
ALURE_API ALboolean ALURE_APIENTRY alureInitDevice(const ALCchar *name, const ALCint *attribs)
{
    init_alure();

    ALCdevice *device = alcOpenDevice(name);
    if(!device)
    {
        alcGetError(NULL);

        last_error = "Device open failed";
        return AL_FALSE;
    }

    ALCcontext *context = alcCreateContext(device, attribs);
    if(alcGetError(device) != ALC_NO_ERROR || !context)
    {
        alcCloseDevice(device);

        last_error = "Context creation failed";
        return AL_FALSE;
    }

    alcMakeContextCurrent(context);
    if(alcGetError(device) != AL_NO_ERROR)
    {
        alcDestroyContext(context);
        alcCloseDevice(device);

        last_error = "Context setup failed";
        return AL_FALSE;
    }

    return AL_TRUE;
}

/* Function: alureShutdownDevice
 *
 * Destroys the current context and closes its associated device. Returns
 * AL_FALSE on error.
 *
 * See Also: <alureInitDevice>
 */
ALURE_API ALboolean ALURE_APIENTRY alureShutdownDevice(void)
{
    init_alure();

    ALCcontext *context = alcGetCurrentContext();
    ALCdevice *device = alcGetContextsDevice(context);
    if(alcGetError(device) != ALC_NO_ERROR || !device)
    {
        last_error = "Failed to get current device";
        return AL_FALSE;
    }

    alcMakeContextCurrent(NULL);
    alcDestroyContext(context);
    alcCloseDevice(device);
    alcGetError(NULL);

    return AL_TRUE;
}


/* Function: alureSleep
 *
 * Rests the calling thread for the given number of seconds. Returns AL_FALSE
 * on error.
 */
ALURE_API ALboolean ALURE_APIENTRY alureSleep(ALfloat duration)
{
    init_alure();

    if(duration < 0.0f)
    {
        last_error = "Invalid duration";
        return AL_FALSE;
    }

    ALuint seconds = (ALuint)duration;
    ALfloat rest = duration - (ALfloat)seconds;

#ifdef HAVE_NANOSLEEP

    struct timespec t, remainingTime;
    t.tv_sec = (time_t)seconds;
    t.tv_nsec = (long)(rest*1000000000);

    while(nanosleep(&t, &remainingTime) < 0 && errno == EINTR)
        t = remainingTime;

#elif defined(HAVE_WINDOWS_H)

    while(seconds > 0)
    {
        Sleep(1000);
        seconds--;
    }
    Sleep((DWORD)(rest * 1000));

#endif

    return AL_TRUE;
}


} // extern "C"
