#include "pt_wake_engine.h"

#include <new>
#include <string.h>

#include "pt_features.h"
#include "tensorflow/lite/micro/micro_allocator.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_resource_variable.h"
#include "tensorflow/lite/schema/schema_generated.h"

namespace {

constexpr int kOps = 13;
constexpr int kMaxWindow = 16;
constexpr int kMaxStride = 8;
constexpr int kResourceVariables = 8;

using Resolver = tflite::MicroMutableOpResolver<kOps>;

struct Runner {
    alignas(tflite::MicroInterpreter) unsigned char storage[sizeof(tflite::MicroInterpreter)];
    tflite::MicroInterpreter *interp;
    pt_wake_arena_t arena;
    float cutoff;
    int window;
    int stride;
    int frames;
    float probs[kMaxWindow];
    int count;
    int next;
    float raw;
    float mean;
    float in_scale;
    int in_zero;
    float out_scale;
    int out_zero;
};

void unload(Runner *r)
{
    if (r->interp) r->interp->~MicroInterpreter();
    r->interp = nullptr;
}

void clear(Runner *r)
{
    r->frames = 0;
    r->count = 0;
    r->next = 0;
    r->raw = 0.0f;
    r->mean = 0.0f;
    if (r->interp) r->interp->Reset();
}

pt_err_t load(Runner *r, const pt_wake_model_t *m, const Resolver &resolver)
{
    unload(r);
    if (!m || !m->data || m->window < 1 || m->window > kMaxWindow) return PT_ERR_INVALID;
    const tflite::Model *model = tflite::GetModel(m->data);
    if (model->version() != TFLITE_SCHEMA_VERSION) return PT_ERR_INVALID;
    tflite::MicroAllocator *allocator = tflite::MicroAllocator::Create(r->arena.data, r->arena.size);
    if (!allocator) return PT_ERR_NOMEM;
    tflite::MicroResourceVariables *vars = tflite::MicroResourceVariables::Create(allocator, kResourceVariables);
    if (!vars) return PT_ERR_NOMEM;
    r->interp = new (r->storage) tflite::MicroInterpreter(model, resolver, allocator, vars);
    if (r->interp->AllocateTensors() != kTfLiteOk) {
        unload(r);
        return PT_ERR_NOMEM;
    }
    TfLiteTensor *in = r->interp->input(0);
    TfLiteTensor *out = r->interp->output(0);
    bool shape_ok = in && out && in->type == kTfLiteInt8 && in->dims->size == 3 && in->dims->data[0] == 1 &&
                    in->dims->data[1] >= 1 && in->dims->data[1] <= kMaxStride &&
                    in->dims->data[2] == PT_FEATURES_CHANNELS && out->type == kTfLiteUInt8 && out->bytes == 1;
    if (!shape_ok) {
        unload(r);
        return PT_ERR_INVALID;
    }
    r->stride = in->dims->data[1];
    r->in_scale = in->params.scale;
    r->in_zero = in->params.zero_point;
    r->out_scale = out->params.scale;
    r->out_zero = out->params.zero_point;
    r->cutoff = m->cutoff;
    r->window = m->window;
    clear(r);
    return PT_OK;
}

bool push(Runner *r, const uint16_t frame[PT_FEATURES_CHANNELS])
{
    if (!r->interp) return false;
    int8_t *in = r->interp->input(0)->data.int8 + r->frames * PT_FEATURES_CHANNELS;
    for (int i = 0; i < PT_FEATURES_CHANNELS; i++) in[i] = pt_features_quantize(frame[i], r->in_scale, r->in_zero);
    if (++r->frames < r->stride) return false;
    r->frames = 0;
    if (r->interp->Invoke() != kTfLiteOk) return false;
    r->raw = (float)((int)r->interp->output(0)->data.uint8[0] - r->out_zero) * r->out_scale;
    r->probs[r->next] = r->raw;
    r->next = (r->next + 1) % r->window;
    if (r->count < r->window) r->count++;
    float sum = 0.0f;
    for (int i = 0; i < r->count; i++) sum += r->probs[i];
    r->mean = r->count == r->window ? sum / (float)r->window : 0.0f;
    return true;
}

}

struct pt_wake_engine {
    pt_features_t features;
    Resolver resolver;
    Runner vad;
    Runner wake;
    int holdoff;
    int since_voice;
    float detected;
};

extern "C" {

size_t pt_wake_engine_size(void)
{
    return sizeof(pt_wake_engine);
}

void pt_wake_engine_reset(pt_wake_engine_t *e)
{
    pt_features_reset(&e->features);
    clear(&e->vad);
    clear(&e->wake);
    e->holdoff = PT_WAKE_HOLDOFF_FRAMES;
    e->since_voice = PT_WAKE_VAD_MEMORY + 1;
    e->detected = 0.0f;
}

pt_wake_engine_t *pt_wake_engine_create(void *mem, const pt_wake_model_t *vad, pt_wake_arena_t vad_arena,
                                        pt_wake_arena_t wake_arena)
{
    if (!mem) return nullptr;
    pt_wake_engine *e = new (mem) pt_wake_engine();
    e->resolver.AddAssignVariable();
    e->resolver.AddCallOnce();
    e->resolver.AddConcatenation();
    e->resolver.AddConv2D();
    e->resolver.AddDepthwiseConv2D();
    e->resolver.AddFullyConnected();
    e->resolver.AddLogistic();
    e->resolver.AddQuantize();
    e->resolver.AddReadVariable();
    e->resolver.AddReshape();
    e->resolver.AddSplitV();
    e->resolver.AddStridedSlice();
    e->resolver.AddVarHandle();
    e->vad.arena = vad_arena;
    e->wake.arena = wake_arena;
    if (pt_features_init(&e->features) != PT_OK || load(&e->vad, vad, e->resolver) != PT_OK) {
        pt_wake_engine_destroy(e);
        return nullptr;
    }
    pt_wake_engine_reset(e);
    return e;
}

void pt_wake_engine_destroy(pt_wake_engine_t *e)
{
    if (!e) return;
    unload(&e->vad);
    unload(&e->wake);
    e->~pt_wake_engine();
}

pt_err_t pt_wake_engine_load(pt_wake_engine_t *e, const pt_wake_model_t *wake)
{
    pt_err_t err = load(&e->wake, wake, e->resolver);
    pt_wake_engine_reset(e);
    return err;
}

bool pt_wake_engine_feed(pt_wake_engine_t *e, const int16_t *pcm, size_t n)
{
    if (!e->wake.interp) return false;
    bool detected = false;
    e->detected = 0.0f;
    uint16_t frame[PT_FEATURES_CHANNELS];
    while (pt_features_next(&e->features, &pcm, &n, frame)) {
        if (e->holdoff > 0) e->holdoff--;
        if (push(&e->vad, frame)) {
            if (e->vad.count == e->vad.window && e->vad.mean >= e->vad.cutoff) {
                e->since_voice = 0;
            } else if (e->since_voice <= PT_WAKE_VAD_MEMORY) {
                e->since_voice++;
            }
        }
        if (!push(&e->wake, frame) || e->wake.count < e->wake.window) continue;
        if (e->wake.mean >= e->wake.cutoff && e->holdoff == 0 && e->since_voice <= PT_WAKE_VAD_MEMORY) {
            float p = e->wake.mean;
            detected = true;
            pt_wake_engine_reset(e);
            e->detected = p;
        }
    }
    return detected;
}

float pt_wake_engine_probability(const pt_wake_engine_t *e)
{
    return e->detected > 0.0f ? e->detected : e->wake.mean;
}

float pt_wake_engine_raw(const pt_wake_engine_t *e, bool vad)
{
    return vad ? e->vad.raw : e->wake.raw;
}

size_t pt_wake_engine_arena_used(const pt_wake_engine_t *e, bool vad)
{
    const Runner *r = vad ? &e->vad : &e->wake;
    return r->interp ? r->interp->arena_used_bytes() : 0;
}
}
