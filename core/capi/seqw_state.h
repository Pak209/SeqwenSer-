/* Plain-C state shared by the C++ sampler engine and the Swift app (imported through the bridging header).
   Keep this file C99-compatible: no C++ types, no bool. */
#ifndef SEQW_STATE_H
#define SEQW_STATE_H

#ifdef __cplusplus
extern "C" {
#endif

#define SQ_STEPS 16
#define SQ_BANKS 2
#define SQ_FX 6            /* pitch, grain, repeat, filter, space, drive */
#define SQ_MAX_SLICES 16

/* conditions */
enum { SQ_COND_ALWAYS = 0, SQ_COND_EVERY2, SQ_COND_EVERY4, SQ_COND_FIRST, SQ_COND_NOT_FIRST, SQ_COND_HALF, SQ_COND_COUNT };
enum { SQ_VEL_ANY = 0, SQ_VEL_GT50, SQ_VEL_LT50, SQ_VEL_COUNT };
/* play modes */
enum { SQ_MODE_FORWARD = 0, SQ_MODE_REVERSE, SQ_MODE_PINGPONG, SQ_MODE_RANDOM, SQ_MODE_COUNT };
/* rates: 1/4 1/8 1/16 1/32 triplet(1/16T) */
enum { SQ_RATE_4 = 0, SQ_RATE_8, SQ_RATE_16, SQ_RATE_32, SQ_RATE_TRIPLET, SQ_RATE_COUNT };
/* effect indices */
enum { SQ_FX_PITCH = 0, SQ_FX_GRAIN, SQ_FX_REPEAT, SQ_FX_FILTER, SQ_FX_SPACE, SQ_FX_DRIVE };

typedef struct SqStep {
    int active;
    int slice;           /* which slice this step plays */
    float velocity;      /* 0..1 */
    float prob;          /* 0..1, multiplied with the pattern probability */
    int cond;            /* SQ_COND_* */
    int velCond;         /* SQ_VEL_* */
    int reverse, lofi, stretch;
    int hasLock[SQ_FX];  /* parameter locks: when set, lock[] replaces the global value for this step */
    float lock[SQ_FX][2];
} SqStep;

typedef struct SqState {
    SqStep steps[SQ_BANKS][SQ_STEPS];
    int bank;            /* 0 = A, 1 = B */
    int length;          /* 1..16 */
    int playMode;        /* SQ_MODE_* */
    int rate;            /* SQ_RATE_* */
    float probability;   /* 0..1 pattern probability */
    float swing;         /* 0..1 */
    float bpm;
    int loop;            /* 1 = loop the pattern, 0 = play it once */
    float volume;        /* 0..1.5 */
    float pan;           /* -1..1 */
    float fx[SQ_FX][2];  /* global values, normalised 0..1 (see docs/ENGINE.md) */
    float trimStart, trimEnd;         /* 0..1 of the sample */
    int numSlices;                    /* 0 = the whole trim region is one slice */
    float sliceEdge[SQ_MAX_SLICES + 1]; /* normalised 0..1 of the sample, numSlices+1 entries */
} SqState;

typedef struct SqStatus {
    int playing;
    int step;            /* index of the step that sounded last */
    float playhead;      /* 0..1 of the sample, newest voice; <0 when silent */
    float level;         /* peak of the last block */
    int voices;
    int pass;            /* completed pattern passes since start */
} SqStatus;

#ifdef __cplusplus
}
#endif
#endif
