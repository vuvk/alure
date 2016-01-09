#ifndef SOURCEGROUP_H
#define SOURCEGROUP_H

#include "main.h"


namespace alure
{

class ALContext;
class ALSource;

class ALSourceGroup : public SourceGroup {
    ALContext *const mContext;

    ALfloat mGain;

    std::vector<ALSource*> mSources;

public:
    ALSourceGroup(ALContext *context);
    virtual ~ALSourceGroup () { }

    virtual void addSource(Source *source) final;
    virtual void addSources(const std::vector<Source*> &sources) final;

    virtual void removeSource(Source *source) final;
    virtual void removeSources(const std::vector<Source*> &sources) final;

    virtual std::vector<Source*> getSources() final;

    virtual void setGain(ALfloat gain) final;
    virtual ALfloat getGain() const final { return mGain; }

    virtual void release() final;
};

} // namespace alure2

#endif /* SOURCEGROUP_H */