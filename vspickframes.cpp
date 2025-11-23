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
            RETERROR("pickframes: invalid offset specified");
    }

    if (inputnframes)
        vi.numFrames = d->num;

    if (vi.numFrames == 0)
        RETERROR("pickframes: no frames to output, all offsets outside available frames");

    VSFilterDependency deps[] = {{d->node, rpGeneral}};
    vsapi->createVideoFilter(out, "pickframes", &vi, pickframesGetframe, filterFree<pickframesData>, fmParallel, deps, 1, d.release(), core);
}


VS_EXTERNAL_API(void) VapourSynthPluginInit2(VSPlugin *plugin, const VSPLUGINAPI *vspapi) {
    vspapi->configPlugin("void.pickframes", "pickframes", "vs pickframes modified from SelectEvery", VS_MAKE_VERSION(1, 0), VAPOURSYNTH_API_VERSION, 0, plugin);
    vspapi->registerFunction("pickframes", "clip:vnode;indices:int[];", "clip:vnode;", pickframesCreate, NULL, plugin);
}
