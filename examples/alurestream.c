#include <stdio.h>

#include "AL/alure.h"

volatile int isdone = 0;
static void eos_callback(void *unused)
{
    isdone = 1;
    (void)unused;
}

#define NUM_BUFS 3

int main(int argc, char **argv)
{
    alureStream *stream;
    ALuint src;

    if(argc < 2)
    {
        fprintf(stderr, "Usage %s <soundfile>\n", argv[0]);
        return 1;
    }

    if(!alureInitDevice(NULL, NULL))
    {
        fprintf(stderr, "Failed to open OpenAL device: %s\n", alureGetErrorString());
        return 1;
    }

    alGenSources(1, &src);
    if(alGetError() != AL_NO_ERROR)
    {
        fprintf(stderr, "Failed to create OpenAL source!\n");
        alureShutdownDevice();
        return 1;
    }

    stream = alureCreateStreamFromFile(argv[1], 19200, 0, NULL);
    if(!stream)
    {
        fprintf(stderr, "Could not load %s: %s\n", argv[1], alureGetErrorString());
        alDeleteSources(1, &src);

        alureShutdownDevice();
        return 1;
    }

    if(!alurePlayStreamAsync(stream, src, NUM_BUFS, 0, eos_callback, NULL))
    {
        fprintf(stderr, "Failed to play stream: %s\n", alureGetErrorString());
        isdone = 1;
    }

    while(!isdone)
        alureSleep(0.125);
    alureStopStream(stream, AL_FALSE);

    alDeleteSources(1, &src);
    alureDestroyStream(stream, 0, NULL);

    alureShutdownDevice();
    return 0;
}
