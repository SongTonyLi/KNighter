# Instruction

Determine whether the static analyzer report is a real bug in the Linux kernel and matches the target bug pattern

Your analysis should:
- **Compare the report against the provided target bug pattern specification,** using the **buggy function (pre-patch)** and the **fix patch** as the reference.
- Explain your reasoning for classifying this as either:
  - **A true positive** (matches the target bug pattern **and** is a real bug), or
  - **A false positive** (does **not** match the target bug pattern **or** is **not** a real bug).

Please evaluate thoroughly using the following process:

- **First, understand** the reported code pattern and its control/data flow.
- **Then, compare** it against the target bug pattern characteristics.
- **Finally, validate** against the **pre-/post-patch** behavior:
  - The reported case demonstrates the same root cause pattern as the target bug pattern/function and would be addressed by a similar fix.

- **Numeric / bounds feasibility** (if applicable):
  - Infer tight **min/max** ranges for all involved variables from types, prior checks, and loop bounds.
  - Show whether overflow/underflow or OOB is actually triggerable (compute the smallest/largest values that violate constraints).

- **Null-pointer dereference feasibility** (if applicable):
  1. **Identify the pointer source** and return convention of the producing function(s) in this path (e.g., returns **NULL**, **ERR_PTR**, negative error code via cast, or never-null).
  2. **Check real-world feasibility in this specific driver/socket/filesystem/etc.**:
     - Enumerate concrete conditions under which the producer can return **NULL/ERR_PTR** here (e.g., missing DT/ACPI property, absent PCI device/function, probe ordering, hotplug/race, Kconfig options, chip revision/quirks).
     - Verify whether those conditions can occur given the driver’s init/probe sequence and the kernel helpers used.
  3. **Lifetime & concurrency**: consider teardown paths, RCU usage, refcounting (`get/put`), and whether the pointer can become invalid/NULL across yields or callbacks.
  4. If the producer is provably non-NULL in this context (by spec or preceding checks), classify as **false positive**.

If there is any uncertainty in the classification, **err on the side of caution and classify it as a false positive**. Your analysis will be used to improve the static analyzer's accuracy.

## Bug Pattern

The bug pattern is **performing floating-point division using an accumulated or computed denominator without checking whether it is zero**.

In this code, `norm_fac` is built by summing per-band contributions:

```c
norm_fac += band->norm_fac;
```

and is later used as a divisor:

```c
norm_fac = 1.0f / norm_fac;
```

If all contributions are zero, `norm_fac` remains `0.0f`, causing a divide-by-zero. This commonly happens when a normalization/scaling factor is derived from runtime data and the code assumes it must be nonzero, but valid inputs can make the sum/product remain zero.

So the specific bug pattern is:

- compute an aggregate normalization factor from data-dependent values,
- later invert or divide by it,
- **without guarding against the aggregate being zero**.

## Bug Pattern

The bug pattern is **performing floating-point division using an accumulated or computed denominator without checking whether it is zero**.

In this code, `norm_fac` is built by summing per-band contributions:

```c
norm_fac += band->norm_fac;
```

and is later used as a divisor:

```c
norm_fac = 1.0f / norm_fac;
```

If all contributions are zero, `norm_fac` remains `0.0f`, causing a divide-by-zero. This commonly happens when a normalization/scaling factor is derived from runtime data and the code assumes it must be nonzero, but valid inputs can make the sum/product remain zero.

So the specific bug pattern is:

- compute an aggregate normalization factor from data-dependent values,
- later invert or divide by it,
- **without guarding against the aggregate being zero**.

# Report

### Report Summary

File:| avfilter/vf_vif.c  
---|---  
Warning:| line 389, column 55  
division by possibly zero aggregate factor  
  
### Annotated Source Code


234   |                 }
235   |             } else {
236   |  for (int filt_i = 0; filt_i < filt_w; filt_i++) {
237   |  const float filt_coeff = filter[filt_i];
238   |  int ii = i - filt_w / 2 + filt_i;
239   |  float img_coeff;
240   |  
241   |                     ii = ii < 0 ? -ii : (ii >= h ? 2 * h - ii - 1 : ii);
242   |  
243   |                     img_coeff = src[ii * src_stride + j];
244   |                     sum += filt_coeff * img_coeff;
245   |                 }
246   |             }
247   |  
248   |             temp[j] = sum;
249   |         }
250   |  
251   |  /** Horizontal pass. */
252   |  for (int j = 0; j < w; j++) {
253   |  float sum = 0.f;
254   |  
255   |  if (j >= filt_w / 2 && j < w - filt_w / 2 - 1) {
256   |  for (int filt_j = 0; filt_j < filt_w; filt_j++) {
257   |  const float filt_coeff = filter[filt_j];
258   |  int jj = j - filt_w / 2 + filt_j;
259   |  float img_coeff;
260   |  
261   |                     img_coeff = temp[jj];
262   |                     sum += filt_coeff * img_coeff;
263   |                 }
264   |             } else {
265   |  for (int filt_j = 0; filt_j < filt_w; filt_j++) {
266   |  const float filt_coeff = filter[filt_j];
267   |  int jj = j - filt_w / 2 + filt_j;
268   |  float img_coeff;
269   |  
270   |                     jj = jj < 0 ? -jj : (jj >= w ? 2 * w - jj - 1 : jj);
271   |  
272   |                     img_coeff = temp[jj];
273   |                     sum += filt_coeff * img_coeff;
274   |                 }
275   |             }
276   |  
277   |             dst[i * dst_stride + j] = sum;
278   |         }
279   |     }
280   |  
281   |  return 0;
282   | }
283   |  
284   | static int compute_vif2(AVFilterContext *ctx,
285   |  const float *ref, const float *main, int w, int h,
286   |  int ref_stride, int main_stride, float *score,
287   |  float *const data_buf[NUM_DATA_BUFS], float **temp,
288   |  int gnb_threads)
289   | {
290   |  ThreadData td;
291   |  float *ref_scale = data_buf[0];
292   |  float *main_scale = data_buf[1];
293   |  float *ref_sq = data_buf[2];
294   |  float *main_sq = data_buf[3];
295   |  float *ref_main = data_buf[4];
296   |  float *mu1 = data_buf[5];
297   |  float *mu2 = data_buf[6];
298   |  float *mu1_sq = data_buf[7];
299   |  float *mu2_sq = data_buf[8];
300   |  float *mu1_mu2 = data_buf[9];
301   |  float *ref_sq_filt = data_buf[10];
302   |  float *main_sq_filt = data_buf[11];
303   |  float *ref_main_filt = data_buf[12];
304   |  
305   |  const float *curr_ref_scale  = ref;
306   |  const float *curr_main_scale = main;
307   |  int curr_ref_stride = ref_stride;
308   |  int curr_main_stride = main_stride;
309   |  
310   |  float num = 0.f;
311   |  float den = 0.f;
312   |  
313   |  for (int scale = 0; scale < 4; scale++) {
    10←Loop condition is true.  Entering loop body→
314   |  const float *filter = vif_filter1d_table[scale];
315   |  int filter_width = vif_filter1d_width1[scale];
316   |  const int nb_threads = FFMIN(h, gnb_threads);
    11←Assuming 'h' is > 'gnb_threads'→
    12←'?' condition is true→
317   |  int buf_valid_w = w;
318   |  int buf_valid_h = h;
319   |  
320   |         td.filter = filter;
321   |         td.filter_width = filter_width;
322   |  
323   |  if (scale12.1'scale' is <= 0 > 0) {
    13←Taking false branch→
324   |             td.src = curr_ref_scale;
325   |             td.dst = mu1;
326   |             td.w = w;
327   |             td.h = h;
328   |             td.src_stride = curr_ref_stride;
329   |             td.dst_stride = w;
330   |             td.temp = temp;
331   |             ff_filter_execute(ctx, vif_filter1d, &td, NULL, nb_threads);
332   |  
333   |             td.src = curr_main_scale;
334   |             td.dst = mu2;
335   |             td.src_stride = curr_main_stride;
336   |             ff_filter_execute(ctx, vif_filter1d, &td, NULL, nb_threads);
337   |  
338   |             vif_dec2(mu1, ref_scale, buf_valid_w, buf_valid_h, w, w);
339   |             vif_dec2(mu2, main_scale, buf_valid_w, buf_valid_h, w, w);
340   |  
341   |             w = buf_valid_w / 2;
342   |             h = buf_valid_h / 2;
343   |  
344   |             buf_valid_w = w;
345   |             buf_valid_h = h;
346   |  
347   |             curr_ref_scale = ref_scale;
348   |             curr_main_scale = main_scale;
349   |  
350   |             curr_ref_stride = w;
351   |             curr_main_stride = w;
352   |         }
353   |  
354   |  td.src = curr_ref_scale;
355   |         td.dst = mu1;
356   |         td.w = w;
357   |         td.h = h;
358   |         td.src_stride = curr_ref_stride;
359   |         td.dst_stride = w;
360   |         td.temp = temp;
361   |         ff_filter_execute(ctx, vif_filter1d, &td, NULL, nb_threads);
362   |  
363   |         td.src = curr_main_scale;
364   |         td.dst = mu2;
365   |         td.src_stride = curr_main_stride;
366   |         ff_filter_execute(ctx, vif_filter1d, &td, NULL, nb_threads);
367   |  
368   |         vif_xx_yy_xy(mu1, mu2, mu1_sq, mu2_sq, mu1_mu2, w, h);
369   |  
370   |         vif_xx_yy_xy(curr_ref_scale, curr_main_scale, ref_sq, main_sq, ref_main, w, h);
371   |  
372   |         td.src = ref_sq;
373   |         td.dst = ref_sq_filt;
374   |         td.src_stride = w;
375   |         ff_filter_execute(ctx, vif_filter1d, &td, NULL, nb_threads);
376   |  
377   |         td.src = main_sq;
378   |         td.dst = main_sq_filt;
379   |         td.src_stride = w;
380   |         ff_filter_execute(ctx, vif_filter1d, &td, NULL, nb_threads);
381   |  
382   |         td.src = ref_main;
383   |         td.dst = ref_main_filt;
384   |         ff_filter_execute(ctx, vif_filter1d, &td, NULL, nb_threads);
385   |  
386   |         vif_statistic(mu1_sq, mu2_sq, mu1_mu2, ref_sq_filt, main_sq_filt,
387   |                       ref_main_filt, &num, &den, w, h);
388   |  
389   |  score[scale] = den <= FLT_EPSILON ? 1.f : num / den;
    14←Assuming 'den' is > FLT_EPSILON→
    15←'?' condition is false→
    16←division by possibly zero aggregate factor
390   |     }
391   |  
392   |  return 0;
393   | }
394   |  
395   | #define offset_fn(type, bits)                            \
396   | static void offset_##bits##bit(VIFContext *s,            \
397   |  const AVFrame *ref,       \
398   |  AVFrame *main, int stride)\
399   | {                                                        \
400   |  int w = s->width;                                    \
401   |  int h = s->height;                                   \
402   |  \
403   |  int ref_stride = ref->linesize[0];                   \
404   |  int main_stride = main->linesize[0];                 \
405   |  \
406   |  const type *ref_ptr = (const type *) ref->data[0];   \
407   |  const type *main_ptr = (const type *) main->data[0]; \
408   |  \
409   |  const float factor = s->factor;         \
410   |  \
411   |  float *ref_ptr_data = s->ref_data;      \
412   |  float *main_ptr_data = s->main_data;    \
413   |  \
414   |  for (int i = 0; i < h; i++) {           \
415   |  for (int j = 0; j < w; j++) {       \
416   |  ref_ptr_data[j] = ref_ptr[j] * factor - 128.f;   \
417   |  main_ptr_data[j] = main_ptr[j] * factor - 128.f; \
418   |  }                                   \
419   |  ref_ptr += ref_stride / sizeof(type);   \
420   |  ref_ptr_data += w;                      \
421   |  main_ptr += main_stride / sizeof(type); \
422   |  main_ptr_data += w;                     \
423   |  } \
424   | }
425   |  
426   | offset_fn(uint8_t, 8)
427   | offset_fn(uint16_t, 16)
428   |  
429   | static void set_meta(AVDictionary **metadata, const char *key, float d)
430   | {
431   |  char value[257];
432   |     snprintf(value, sizeof(value), "%f", d);
433   |     av_dict_set(metadata, key, value, 0);
434   | }
435   |  
436   | static AVFrame *do_vif(AVFilterContext *ctx, AVFrame *main, const AVFrame *ref)
437   | {
438   |  VIFContext *s = ctx->priv;
439   |     AVDictionary **metadata = &main->metadata;
440   |  float score[4];
441   |  
442   |  s->factor = 1.f / (1 << (s->desc->comp[0].depth - 8));
    6←Assuming right operand of bit shift is non-negative but less than 32→
443   |  if (s->desc->comp[0].depth <= 8) {
    7←Assuming field 'depth' is > 8→
    8←Taking false branch→
444   |         offset_8bit(s, ref, main, s->width);
445   |     } else {
446   |  offset_16bit(s, ref, main, s->width);
447   |     }
448   |  
449   |  compute_vif2(ctx, s->ref_data, s->main_data,
    9←Calling 'compute_vif2'→
450   |  s->width, s->height, s->width, s->width,
451   |  score, s->data_buf, s->temp, s->nb_threads);
452   |  
453   |     set_meta(metadata, "lavfi.vif.scale.0", score[0]);
454   |     set_meta(metadata, "lavfi.vif.scale.1", score[1]);
455   |     set_meta(metadata, "lavfi.vif.scale.2", score[2]);
456   |     set_meta(metadata, "lavfi.vif.scale.3", score[3]);
457   |  
458   |  for (int i = 0; i < 4; i++) {
459   |         s->vif_min[i]  = FFMIN(s->vif_min[i], score[i]);
460   |         s->vif_max[i]  = FFMAX(s->vif_max[i], score[i]);
461   |         s->vif_sum[i] += score[i];
462   |     }
463   |  
464   |     s->nb_frames++;
465   |  
466   |  return main;
467   | }
468   |  
469   | static const enum AVPixelFormat pix_fmts[] = {
470   |     AV_PIX_FMT_GRAY8, AV_PIX_FMT_GRAY9, AV_PIX_FMT_GRAY10,
471   |  AV_PIX_FMT_GRAY12, AV_PIX_FMT_GRAY14, AV_PIX_FMT_GRAY16,
472   |     AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV422P, AV_PIX_FMT_YUV444P,
473   |     AV_PIX_FMT_YUV440P, AV_PIX_FMT_YUV411P, AV_PIX_FMT_YUV410P,
474   |     AV_PIX_FMT_YUVJ411P, AV_PIX_FMT_YUVJ420P, AV_PIX_FMT_YUVJ422P,
475   |     AV_PIX_FMT_YUVJ440P, AV_PIX_FMT_YUVJ444P,
476   | #define PF(suf) AV_PIX_FMT_YUV420##suf,  AV_PIX_FMT_YUV422##suf,  AV_PIX_FMT_YUV444##suf
477   |  PF(P9), PF(P10), PF(P12), PF(P14), PF(P16),
478   |     AV_PIX_FMT_NONE
479   | };
480   |  
481   | static int config_input_ref(AVFilterLink *inlink)
482   | {
483   |     AVFilterContext *ctx  = inlink->dst;
484   |     VIFContext *s = ctx->priv;
485   |  
486   |  if (ctx->inputs[0]->w != ctx->inputs[1]->w ||
487   |         ctx->inputs[0]->h != ctx->inputs[1]->h) {
488   |         av_log(ctx, AV_LOG_ERROR, "Width and height of input videos must be same.\n");
489   |  return AVERROR(EINVAL);
490   |     }
491   |  
492   |     s->desc = av_pix_fmt_desc_get(inlink->format);
493   |     s->width = ctx->inputs[0]->w;
494   |     s->height = ctx->inputs[0]->h;
495   |     s->nb_threads = ff_filter_get_nb_threads(ctx);
496   |  
497   |  for (int i = 0; i < 4; i++) {
498   |         s->vif_min[i] =  DBL_MAX;
499   |         s->vif_max[i] = -DBL_MAX;
500   |     }
501   |  
502   |  for (int i = 0; i < NUM_DATA_BUFS; i++) {
503   |  if (!(s->data_buf[i] = av_calloc(s->width, s->height * sizeof(float))))
504   |  return AVERROR(ENOMEM);
505   |     }
506   |  
507   |  if (!(s->ref_data = av_calloc(s->width, s->height * sizeof(float))))
508   |  return AVERROR(ENOMEM);
509   |  
510   |  if (!(s->main_data = av_calloc(s->width, s->height * sizeof(float))))
511   |  return AVERROR(ENOMEM);
512   |  
513   |  if (!(s->temp = av_calloc(s->nb_threads, sizeof(s->temp[0]))))
514   |  return AVERROR(ENOMEM);
515   |  
516   |  for (int i = 0; i < s->nb_threads; i++) {
517   |  if (!(s->temp[i] = av_calloc(s->width, sizeof(float))))
518   |  return AVERROR(ENOMEM);
519   |     }
520   |  
521   |  return 0;
522   | }
523   |  
524   | static int process_frame(FFFrameSync *fs)
525   | {
526   |  AVFilterContext *ctx = fs->parent;
527   |     VIFContext *s = fs->opaque;
528   |     AVFilterLink *outlink = ctx->outputs[0];
529   |     AVFrame *out_frame, *main_frame = NULL, *ref_frame = NULL;
530   |  int ret;
531   |  
532   |     ret = ff_framesync_dualinput_get(fs, &main_frame, &ref_frame);
533   |  if (ret < 0)
    1Assuming 'ret' is >= 0→
534   |  return ret;
535   |  
536   |  if (ctx->is_disabled || !ref_frame) {
    2←Assuming field 'is_disabled' is 0→
    3←Assuming 'ref_frame' is non-null→
    4←Taking false branch→
537   |         out_frame = main_frame;
538   |     } else {
539   |  out_frame = do_vif(ctx, main_frame, ref_frame);
    5←Calling 'do_vif'→
540   |     }
541   |  
542   |     out_frame->pts = av_rescale_q(s->fs.pts, s->fs.time_base, outlink->time_base);
543   |  
544   |  return ff_filter_frame(outlink, out_frame);
545   | }
546   |  
547   |  
548   | static int config_output(AVFilterLink *outlink)
549   | {
550   |     AVFilterContext *ctx = outlink->src;
551   |     VIFContext *s = ctx->priv;
552   |     AVFilterLink *mainlink = ctx->inputs[0];
553   |     FilterLink *il = ff_filter_link(mainlink);
554   |     FilterLink *ol = ff_filter_link(outlink);
555   |     FFFrameSyncIn *in;
556   |  int ret;
557   |  
558   |     outlink->w = mainlink->w;
559   |     outlink->h = mainlink->h;
560   |     outlink->time_base = mainlink->time_base;
561   |     outlink->sample_aspect_ratio = mainlink->sample_aspect_ratio;
562   |     ol->frame_rate = il->frame_rate;
563   |  if ((ret = ff_framesync_init(&s->fs, ctx, 2)) < 0)
564   |  return ret;
565   |  
566   |     in = s->fs.in;
567   |     in[0].time_base = mainlink->time_base;
568   |     in[1].time_base = ctx->inputs[1]->time_base;
569   |     in[0].sync   = 2;

# Formatting

Please provide your answer in the following format:

- Decision: {Bug/NotABug}
- Reason: {Your reason here}
