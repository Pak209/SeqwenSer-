/* C API of the Seqwenser engine, used by the Swift app (and by tests). Thread rules:
   - create, destroy, load, set_sample, peaks, auto_slice: main thread only
   - set_state/set_playing/trigger_pad/status: any thread
   - render: audio thread only (no allocation, no locks) */
#ifndef SEQW_ENGINE_H
#define SEQW_ENGINE_H

#include "seqw_state.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SqEngine SqEngine;

SqEngine* sq_create(double sampleRate, int maxBlock);
void sq_destroy(SqEngine*);
SqState sq_default_state(void);

/* Sample data. PCM is copied. right may be NULL (mono). Returns 1 on success. */
int sq_load_pcm(SqEngine*, const float* left, const float* right, long frames, double sampleRate);
int sq_load_demo(SqEngine*);
void sq_clear_sample(SqEngine*);
long sq_sample_frames(const SqEngine*);
/* Copies the loaded PCM out (buffers of sq_sample_frames() floats). Returns frames copied. */
long sq_copy_pcm(const SqEngine*, float* left, float* right);
double sq_sample_rate(const SqEngine*);
/* Peaks for drawing: fills mins/maxs (columns each) over [from,to] normalised. */
void sq_peaks(const SqEngine*, float from, float to, int columns, float* mins, float* maxs);
/* Rough tempo of the loaded sample (0 when unknown); confidence 0..1. */
double sq_detect_bpm(const SqEngine*, float* confidence);
/* Finds slice edges inside [trimStart, trimEnd]; writes numSlices/sliceEdge into state. */
void sq_auto_slice(const SqEngine*, SqState* state, float sensitivity);
void sq_equal_slices(SqState* state, int count);

/* Accessors so Swift can fill the state without importing C arrays as tuples. */
SqStep* sq_state_step(SqState*, int bank, int index);
float* sq_state_fx(SqState*, int fx);              /* 2 floats */
float* sq_step_lock(SqStep*, int fx);              /* 2 floats */
int* sq_step_has_lock(SqStep*, int fx);
float* sq_state_slice_edges(SqState*);             /* SQ_MAX_SLICES + 1 floats */

void sq_set_state(SqEngine*, const SqState*);
void sq_set_playing(SqEngine*, int on);
void sq_trigger_pad(SqEngine*, int slice, float velocity);
void sq_render(SqEngine*, float* left, float* right, int frames);
SqStatus sq_status(const SqEngine*);
void sq_collect_garbage(SqEngine*);

/* Offline bounce of the current pattern: plays `passes` passes (loop off), then `tailSeconds` of tail.
   Allocates *outFrames*2 floats via malloc (left block then right block); free with sq_free. Returns frames or 0. */
long sq_bounce(const SqEngine*, const SqState*, int passes, double tailSeconds, float** outInterleavedLR);
void sq_free(void*);

#ifdef __cplusplus
}
#endif
#endif
