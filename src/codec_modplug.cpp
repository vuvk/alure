/*
 * ALURE  OpenAL utility library
 * Copyright (c) 2009-2010 by Chris Robinson.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "config.h"

#include "main.h"

#include <string.h>
#include <assert.h>

#include <istream>

#include <modplug.h>


void *mod_handle;

#define MAKE_FUNC(x) typeof(x)* p##x
MAKE_FUNC(ModPlug_Load);
MAKE_FUNC(ModPlug_Unload);
MAKE_FUNC(ModPlug_Read);
MAKE_FUNC(ModPlug_SeekOrder);
#undef MAKE_FUNC


struct modStream : public alureStream {
    ModPlugFile *modFile;
    int lastOrder;

    virtual bool IsValid()
    { return modFile != NULL; }

    virtual bool GetFormat(ALenum *fmt, ALuint *frequency, ALuint *blockalign)
    {
        *fmt = AL_FORMAT_STEREO16;
        *frequency = 44100;
        *blockalign = 2 * sizeof(ALshort);
        return true;
    }

    virtual ALuint GetData(ALubyte *data, ALuint bytes)
    {
        int ret = pModPlug_Read(modFile, data, bytes);
        if(ret < 0) return 0;
        return ret;
    }

    virtual bool Rewind()
    { return SetOrder(lastOrder); }

    virtual bool SetOrder(ALuint order)
    {
        std::vector<char> data(16384);
        ALuint total = 0;
        while(total < 2*1024*1024)
        {
            fstream->read(&data[total], data.size()-total);
            if(fstream->gcount() == 0) break;
            total += fstream->gcount();
            data.resize(total*2);
        }
        data.resize(total);

        ModPlugFile *newMod = pModPlug_Load(&data[0], data.size());
        if(!newMod)
        {
            SetError("Could not reload data");
            return false;
        }
        pModPlug_Unload(modFile);
        modFile = newMod;

        // There seems to be no way to tell if the seek succeeds
        pModPlug_SeekOrder(modFile, order);
        lastOrder = order;

        return true;
    }

    modStream(std::istream *_fstream)
      : alureStream(_fstream), modFile(NULL), lastOrder(0)
    {
        if(!mod_handle) return;

        std::vector<char> data(16384);
        ALuint total = 0;
        while(total < 2*1024*1024)
        {
            fstream->read(&data[total], data.size()-total);
            if(fstream->gcount() == 0) break;
            total += fstream->gcount();
            data.resize(total*2);
        }
        data.resize(total);

        modFile = pModPlug_Load(&data[0], data.size());
    }

    virtual ~modStream()
    {
        if(modFile)
            pModPlug_Unload(modFile);
        modFile = NULL;
    }
};
static DecoderDecl<modStream> modStream_decoder;