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

#include <mpg123.h>


void *mp123_handle;

#define MAKE_FUNC(x) typeof(x)* p##x
MAKE_FUNC(mpg123_read);
MAKE_FUNC(mpg123_init);
MAKE_FUNC(mpg123_open_feed);
MAKE_FUNC(mpg123_new);
MAKE_FUNC(mpg123_delete);
MAKE_FUNC(mpg123_feed);
MAKE_FUNC(mpg123_exit);
MAKE_FUNC(mpg123_getformat);
MAKE_FUNC(mpg123_format_none);
MAKE_FUNC(mpg123_decode);
MAKE_FUNC(mpg123_format);
#undef MAKE_FUNC


struct mp3Stream : public alureStream {
    mpg123_handle *mp3File;
    long samplerate;
    int channels;
    ALenum format;
    std::ios::pos_type dataStart;
    std::ios::pos_type dataEnd;

    virtual bool IsValid()
    { return mp3File != NULL; }

    virtual bool GetFormat(ALenum *fmt, ALuint *frequency, ALuint *blockalign)
    {
        *fmt = format;
        *frequency = samplerate;
        *blockalign = channels*2;
        return true;
    }

    virtual ALuint GetData(ALubyte *data, ALuint bytes)
    {
        ALuint amt = 0;
        while(bytes > 0)
        {
            size_t got = 0;
            int ret = pmpg123_read(mp3File, data, bytes, &got);

            bytes -= got;
            data += got;
            amt += got;

            if(ret == MPG123_NEW_FORMAT)
            {
                long newrate;
                int newchans, enc;
                pmpg123_getformat(mp3File, &newrate, &newchans, &enc);
                continue;
            }
            if(ret == MPG123_NEED_MORE)
            {
                unsigned char data[4096];
                ALint insize = std::min<ALint>(sizeof(data),
                                               (dataEnd-fstream->tellg()));
                if(insize > 0)
                {
                    fstream->read((char*)data, insize);
                    insize = fstream->gcount();
                }
                if(insize > 0 && pmpg123_feed(mp3File, data, insize) == MPG123_OK)
                    continue;
            }
            if(got == 0)
                break;
        }
        return amt;
    }

    virtual bool Rewind()
    {
        fstream->clear();
        std::istream::pos_type oldpos = fstream->tellg();
        fstream->seekg(dataStart);

        mpg123_handle *newFile = pmpg123_new(NULL, NULL);
        if(pmpg123_open_feed(newFile) == MPG123_OK)
        {
            unsigned char data[4096];
            long newrate;
            int newchans;
            int enc;

            ALuint amt, total = 0;
            int ret = MPG123_OK;
            do {
                amt = std::min<ALint>(sizeof(data),
                                      (dataEnd-fstream->tellg()));
                fstream->read((char*)data, amt);
                amt = fstream->gcount();
                if(amt == 0)  break;
                total += amt;
                ret = pmpg123_decode(newFile, data, amt, NULL, 0, NULL);
            } while(ret == MPG123_NEED_MORE && total < 64*1024);

            if(ret == MPG123_NEW_FORMAT &&
               pmpg123_getformat(newFile, &newrate, &newchans, &enc) == MPG123_OK)
            {
                if(pmpg123_format_none(newFile) == MPG123_OK &&
                   pmpg123_format(newFile, samplerate, channels, MPG123_ENC_SIGNED_16) == MPG123_OK)
                {
                    // All OK
                    pmpg123_delete(mp3File);
                    mp3File = newFile;
                    return true;
                }
            }
            pmpg123_delete(newFile);
        }

        fstream->seekg(oldpos);
        SetError("Restart failed");
        return false;
    }

    mp3Stream(std::istream *_fstream)
      : alureStream(_fstream), mp3File(NULL), format(AL_NONE),
        dataStart(0), dataEnd(0)
    {
        if(!mp123_handle) return;

        if(!FindDataChunk())
            return;

        mp3File = pmpg123_new(NULL, NULL);
        if(pmpg123_open_feed(mp3File) == MPG123_OK)
        {
            unsigned char data[4096];
            int enc;

            ALuint amt, total = 0;
            int ret = MPG123_OK;
            do {
                amt = std::min<ALint>(sizeof(data),
                                      (dataEnd-fstream->tellg()));
                fstream->read((char*)data, amt);
                amt = fstream->gcount();
                if(amt == 0)  break;
                total += amt;
                ret = pmpg123_decode(mp3File, data, amt, NULL, 0, NULL);
            } while(ret == MPG123_NEED_MORE && total < 64*1024);

            if(ret == MPG123_NEW_FORMAT &&
               pmpg123_getformat(mp3File, &samplerate, &channels, &enc) == MPG123_OK)
            {
                format = GetSampleFormat(channels, 16, false);
                if(pmpg123_format_none(mp3File) == MPG123_OK &&
                   pmpg123_format(mp3File, samplerate, channels, MPG123_ENC_SIGNED_16) == MPG123_OK)
                {
                    // All OK
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

private:
    bool FindDataChunk()
    {
        ALubyte buffer[25];
        int length;

        if(!fstream->read(reinterpret_cast<char*>(buffer), 12))
            return false;

        if(memcmp(buffer, "RIFF", 4) != 0 || memcmp(buffer+8, "WAVE", 4) != 0)
        {
            dataStart = 0;

            // Check for an ID3v2 tag, and skip it
            if(memcmp(buffer, "ID3", 3) == 0 &&
               buffer[3] <= 4 && buffer[4] != 0xff &&
               (buffer[5]&0x0f) == 0 && (buffer[6]&0x80) == 0 &&
               (buffer[7]&0x80) == 0 && (buffer[8]&0x80) == 0 &&
               (buffer[9]&0x80) == 0)
            {
                dataStart = (buffer[6]<<21) | (buffer[7]<<14) |
                            (buffer[8]<< 7) | (buffer[9]    );
                dataStart += ((buffer[5]&0x10) ? 20 : 10);
            }

            if(fstream->seekg(0, std::ios_base::end))
            {
                dataEnd = fstream->tellg();
                fstream->seekg(dataStart);
            }
            return true;
        }

        int type = 0;
        while(1)
        {
            char tag[4];
            if(!fstream->read(tag, 4))
                break;

            /* read chunk length */
            length = read_le32(fstream);

            if(memcmp(tag, "fmt ", 4) == 0 && length >= 16)
            {
                /* Data type (should be 0x0050 or 0x0055 for MP3 data) */
                type = read_le16(fstream);
                if(type != 0x0050 && type != 0x0055)
                    break;
                length -= 2;
                /* Ignore the rest of the chunk. Everything we need is in the
                 * data stream */
            }
            else if(memcmp(tag, "data", 4) == 0)
            {
                if(type == 0x0050 || type == 0x0055)
                {
                    dataStart = fstream->tellg();
                    dataEnd = dataStart;
                    dataEnd += length;
                    return true;
                }
            }

            fstream->seekg(length, std::ios_base::cur);
        }

        return false;
    }
};
static DecoderDecl<mp3Stream> mp3Stream_decoder;