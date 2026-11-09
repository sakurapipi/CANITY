#ifndef CANITY_PERCEPTION_H
#define CANITY_PERCEPTION_H
#define SANITY_CLEAR_THRESHOLD .80f
#define SANITY_UNEASE_THRESHOLD .50f
#define SANITY_COLLAPSE_THRESHOLD .20f
#define PERCEPTION_GRAIN_MAX 13.0f
typedef struct { float ratio,stress,grain,vignette,flicker,distortion,contamination; unsigned seed; } PerceptionState;
extern PerceptionState perception;
PerceptionState PerceptionEvaluate(float ratio,double seconds);
void PerceptionUpdate(float ratio,double seconds);
#endif
