/* Title: istream and streambuf overrides */

#include "config.h"

#include "main.h"


MemStreamBuf::int_type MemStreamBuf::underflow()
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

MemStreamBuf::pos_type MemStreamBuf::seekoff(off_type offset, std::ios_base::seekdir whence, std::ios_base::openmode mode)
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

MemStreamBuf::pos_type MemStreamBuf::seekpos(pos_type pos, std::ios_base::openmode mode)
{
    if((mode&std::ios_base::out))
        return pos_type(off_type(-1));

    if(pos < 0 || pos > pos_type(memInfo.Length))
        return pos_type(off_type(-1));
    memInfo.Pos = pos;

    setg(0, 0, 0);
    return pos;
}
