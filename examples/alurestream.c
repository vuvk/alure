#include <stdio.h>

#include "AL/alure.h"

#define NUM_BUFS 3

int main(int argc, char **argv)
{
    alureStream *stream;
    ALuint src, buf[NUM_BUFS];

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

    stream = alureCreateStreamFromFile(argv[1], 19200, NUM_BUFS, buf);
    if(!stream)
    {
        fprintf(stderr, "Could not load %s: %s\n", argv[1], alureGetErrorString());
        alDeleteSources(1, &src);

        alureShutdownDevice();
        return 1;
    }

    alSourceQueueBuffers(src, NUM_BUFS, buf);
    alSourcePlay(src);
    if(alGetError() != AL_NO_ERROR)
    {
        fprintf(stderr, "Failed to start source!\n");
        alDeleteSources(1, &src);
        alureDestroyStream(stream, NUM_BUFS, buf);

        alureShutdownDevice();
        return 1;
    }


    do {
        ALint state = AL_PLAYING;
        ALint processed = 0;

        alureSleep(0.01);

        alGetSourcei(src, AL_SOURCE_STATE, &state);
        alGetSourcei(src, AL_BUFFERS_PROCESSED, &processed);
        if(processed > 0)
        {
            ALuint bufs[NUM_BUFS];
            alSourceUnqueueBuffers(src, processed, bufs);

            processed = alureBufferDataFromStream(stream, processed, bufs);
            if(processed <= 0)
            {
                do {
                    alureSleep(0.01);
                    alGetSourcei(src, AL_SOURCE_STATE, &state);
                } while(alGetError() == AL_NO_ERROR && state == AL_PLAYING);
                break;
            }
            alSourceQueueBuffers(src, processed, bufs);
        }
        if(state != AL_PLAYING)
            alSourcePlay(src);
    } while(alGetError() == AL_NO_ERROR);

    alDeleteSources(1, &src);
    alureDestroyStream(stream, NUM_BUFS, buf);

    alureShutdownDevice();
    return 0;
}
