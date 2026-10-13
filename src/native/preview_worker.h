#ifndef AWM_PREVIEW_WORKER_H
#define AWM_PREVIEW_WORKER_H
#include "preview.h"
typedef struct PreviewWorker PreviewWorker;
typedef struct PreviewStats {uint64_t presented,skipped,copy_misses,last_source_frame;double render_ms,last_gap_ms;} PreviewStats;
PreviewWorker *preview_worker_create(HWND canvas,HWND notify);
void preview_worker_update(PreviewWorker *w,const wchar_t *mapping,bool enabled,unsigned scaler,unsigned framing);
void preview_worker_redraw(PreviewWorker *w);
bool preview_worker_live(PreviewWorker *w);
bool preview_worker_failed(PreviewWorker *w);
void preview_worker_stats(PreviewWorker *w,PreviewStats *stats);
void preview_worker_close(PreviewWorker *w);
bool preview_worker_closed(PreviewWorker *w);
void preview_worker_destroy(PreviewWorker *w);
#define PREVIEW_UPDATE (WM_APP+18)
#endif
