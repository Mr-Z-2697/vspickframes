#include <cstdlib>
#include <vector>
#include <memory>
#include "vapoursynth/VapourSynth4.h"
#include "vapoursynth/VSHelper4.h"

#define RETERROR(x) do { vsapi->mapSetError(out, (x)); return; } while (0)


template<typename T>
static void VS_CC filterFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    delete reinterpret_cast<T *>(instanceData);
}

template<typename T>
struct SingleNodeData : public T {
private:
    const VSAPI *vsapi;
public:
    VSNode *node = nullptr;

    explicit SingleNodeData(const VSAPI *vsapi) noexcept : T(), vsapi(vsapi) {
    }

    ~SingleNodeData() {
        vsapi->freeNode(node);
    }
};

typedef struct {
    std::vector<int> offsets;
    int cycle;
    int num;
} pickframesDataExtra;

typedef SingleNodeData<pickframesDataExtra> pickframesData;


static const VSFrame *VS_CC pickframesGetframe(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    pickframesData *d = reinterpret_cast<pickframesData *>(instanceData);

    if (activationReason == arInitial) {
        n = (n / d->num) * d->cycle + d->offsets[n % d->num];
        frameData[0] = reinterpret_cast<void *>(static_cast<intptr_t>(n));
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrame *src = vsapi->getFrameFilter(static_cast<int>(reinterpret_cast<intptr_t>(frameData[0])), d->node, frameCtx);
        return src;
    }

    return nullptr;
}

static void VS_CC pickframesCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<pickframesData> d(new pickframesData(vsapi));
    int err;

    d->node = vsapi->mapGetNode(in, "clip", 0, 0);
    VSVideoInfo vi = *vsapi->getVideoInfo(d->node);
    int inputnframes = vi.numFrames;

    d->cycle = inputnframes;
    d->num = vsapi->mapNumElements(in, "indices");
    d->offsets.resize(d->num);

    for (int i = 0; i < d->num; i++) {
        d->offsets[i] = vsapi->mapGetIntSaturated(in, "indices", i, 0);

        if (d->offsets[i] < 0 || d->offsets[i] >= d->cycle)
            RETERROR("pickframes: indices contains out-of-range value");
    }

    if (inputnframes)
        vi.numFrames = d->num;

    if (vi.numFrames == 0)
        RETERROR("pickframes: no frames to output, all indices outside available frames");

    VSFilterDependency deps[] = {{d->node, rpGeneral}};
    vsapi->createVideoFilter(out, "pickframes", &vi, pickframesGetframe, filterFree<pickframesData>, fmParallel, deps, 1, d.release(), core);
}


//////////////////////////////////////////
// SelectEvery

typedef struct {
    std::vector<int> offsets;
    int cycle;
    int num;
    bool modifyDuration;
} SelectEveryDataExtra;

typedef SingleNodeData<SelectEveryDataExtra> SelectEveryData;

static const VSFrame *VS_CC selectEveryGetframe(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    SelectEveryData *d = reinterpret_cast<SelectEveryData *>(instanceData);

    if (activationReason == arInitial) {
        n = (n / d->num) * d->cycle + d->offsets[n % d->num];
        frameData[0] = reinterpret_cast<void *>(static_cast<intptr_t>(n));
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrame *src = vsapi->getFrameFilter(static_cast<int>(reinterpret_cast<intptr_t>(frameData[0])), d->node, frameCtx);
        if (d->modifyDuration) {
            VSFrame *dst = vsapi->copyFrame(src, core);
            VSMap *dst_props = vsapi->getFramePropertiesRW(dst);
            int errNum, errDen;
            int64_t durationNum = vsapi->mapGetInt(dst_props, "_DurationNum", 0, &errNum);
            int64_t durationDen = vsapi->mapGetInt(dst_props, "_DurationDen", 0, &errDen);
            if (!errNum && !errDen) {
                vsh::muldivRational(&durationNum, &durationDen, d->cycle, d->num);
                vsapi->mapSetInt(dst_props, "_DurationNum", durationNum, maReplace);
                vsapi->mapSetInt(dst_props, "_DurationDen", durationDen, maReplace);
            }
            vsapi->freeFrame(src);
            return dst;
        } else {
            return src;
        }
    }

    return nullptr;
}

static void VS_CC selectEveryCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    std::unique_ptr<SelectEveryData> d(new SelectEveryData(vsapi));
    int err;

    d->cycle = vsapi->mapGetIntSaturated(in, "cycle", 0, 0);

    if (d->cycle <= 1)
        RETERROR("SelectEvery: invalid cycle size (must be greater than 1)");

    d->num = vsapi->mapNumElements(in, "offsets");
    d->modifyDuration = !!vsapi->mapGetInt(in, "modify_duration", 0, &err);
    if (err)
        d->modifyDuration = true;

    d->offsets.resize(d->num);

    for (int i = 0; i < d->num; i++) {
        d->offsets[i] = vsapi->mapGetIntSaturated(in, "offsets", i, 0);

        if (d->offsets[i] < 0 || d->offsets[i] >= d->cycle)
            RETERROR("SelectEvery: invalid offset specified");
    }

    d->node = vsapi->mapGetNode(in, "clip", 0, 0);

    VSVideoInfo vi = *vsapi->getVideoInfo(d->node);
    int inputnframes = vi.numFrames;
    if (inputnframes) {
        vi.numFrames = (inputnframes / d->cycle) * d->num;
        for (int i = 0; i < d->num; i++)
            if (d->offsets[i] < inputnframes % d->cycle)
                vi.numFrames++;
    }

    if (vi.numFrames == 0)
        RETERROR("SelectEvery: no frames to output, all offsets outside available frames");

    if (d->modifyDuration)
        vsh::muldivRational(&vi.fpsNum, &vi.fpsDen, d->num, d->cycle);

    VSFilterDependency deps[] = {{d->node, rpGeneral}};
    vsapi->createVideoFilter(out, "SelectEvery", &vi, selectEveryGetframe, filterFree<SelectEveryData>, fmParallel, deps, 1, d.release(), core);
}


VS_EXTERNAL_API(void) VapourSynthPluginInit2(VSPlugin *plugin, const VSPLUGINAPI *vspapi) {
    vspapi->configPlugin("void.pickframes", "pickframes", "vs pickframes modified from SelectEvery", VS_MAKE_VERSION(1, 0), VAPOURSYNTH_API_VERSION, 0, plugin);
    vspapi->registerFunction("pickframes", "clip:vnode;indices:int[];", "clip:vnode;", pickframesCreate, 0, plugin);
    vspapi->registerFunction("SelectEvery", "clip:vnode;cycle:int;offsets:int[];modify_duration:int:opt;", "clip:vnode;", selectEveryCreate, 0, plugin);
}
