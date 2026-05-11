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

## Patch Description

avcodec/aacpsy: Avoid floating point division by 0 of norm_fac

Fixes: Ticket7995
Fixes: CVE-2020-20446

Signed-off-by: Michael Niedermayer <michael@niedermayer.cc>

## Buggy Code

```c
// Function: psy_3gpp_analyze_channel in libavcodec/aacpsy.c
static void psy_3gpp_analyze_channel(FFPsyContext *ctx, int channel,
                                     const float *coefs, const FFPsyWindowInfo *wi)
{
    AacPsyContext *pctx = (AacPsyContext*) ctx->model_priv_data;
    AacPsyChannel *pch  = &pctx->ch[channel];
    int i, w, g;
    float desired_bits, desired_pe, delta_pe, reduction= NAN, spread_en[128] = {0};
    float a = 0.0f, active_lines = 0.0f, norm_fac = 0.0f;
    float pe = pctx->chan_bitrate > 32000 ? 0.0f : FFMAX(50.0f, 100.0f - pctx->chan_bitrate * 100.0f / 32000.0f);
    const int      num_bands   = ctx->num_bands[wi->num_windows == 8];
    const uint8_t *band_sizes  = ctx->bands[wi->num_windows == 8];
    AacPsyCoeffs  *coeffs      = pctx->psy_coef[wi->num_windows == 8];
    const float avoid_hole_thr = wi->num_windows == 8 ? PSY_3GPP_AH_THR_SHORT : PSY_3GPP_AH_THR_LONG;
    const int bandwidth        = ctx->cutoff ? ctx->cutoff : AAC_CUTOFF(ctx->avctx);
    const int cutoff           = bandwidth * 2048 / wi->num_windows / ctx->avctx->sample_rate;

    //calculate energies, initial thresholds and related values - 5.4.2 "Threshold Calculation"
    calc_thr_3gpp(wi, num_bands, pch, band_sizes, coefs, cutoff);

    //modify thresholds and energies - spread, threshold in quiet, pre-echo control
    for (w = 0; w < wi->num_windows*16; w += 16) {
        AacPsyBand *bands = &pch->band[w];

        /* 5.4.2.3 "Spreading" & 5.4.3 "Spread Energy Calculation" */
        spread_en[0] = bands[0].energy;
        for (g = 1; g < num_bands; g++) {
            bands[g].thr   = FFMAX(bands[g].thr,    bands[g-1].thr * coeffs[g].spread_hi[0]);
            spread_en[w+g] = FFMAX(bands[g].energy, spread_en[w+g-1] * coeffs[g].spread_hi[1]);
        }
        for (g = num_bands - 2; g >= 0; g--) {
            bands[g].thr   = FFMAX(bands[g].thr,   bands[g+1].thr * coeffs[g].spread_low[0]);
            spread_en[w+g] = FFMAX(spread_en[w+g], spread_en[w+g+1] * coeffs[g].spread_low[1]);
        }
        //5.4.2.4 "Threshold in quiet"
        for (g = 0; g < num_bands; g++) {
            AacPsyBand *band = &bands[g];

            band->thr_quiet = band->thr = FFMAX(band->thr, coeffs[g].ath);
            //5.4.2.5 "Pre-echo control"
            if (!(wi->window_type[0] == LONG_STOP_SEQUENCE || (!w && wi->window_type[1] == LONG_START_SEQUENCE)))
                band->thr = FFMAX(PSY_3GPP_RPEMIN*band->thr, FFMIN(band->thr,
                                  PSY_3GPP_RPELEV*pch->prev_band[w+g].thr_quiet));

            /* 5.6.1.3.1 "Preparatory steps of the perceptual entropy calculation" */
            pe += calc_pe_3gpp(band);
            a  += band->pe_const;
            active_lines += band->active_lines;

            /* 5.6.1.3.3 "Selection of the bands for avoidance of holes" */
            if (spread_en[w+g] * avoid_hole_thr > band->energy || coeffs[g].min_snr > 1.0f)
                band->avoid_holes = PSY_3GPP_AH_NONE;
            else
                band->avoid_holes = PSY_3GPP_AH_INACTIVE;
        }
    }

    /* 5.6.1.3.2 "Calculation of the desired perceptual entropy" */
    ctx->ch[channel].entropy = pe;
    if (ctx->avctx->flags & AV_CODEC_FLAG_QSCALE) {
        /* (2.5 * 120) achieves almost transparent rate, and we want to give
         * ample room downwards, so we make that equivalent to QSCALE=2.4
         */
        desired_pe = pe * (ctx->avctx->global_quality ? ctx->avctx->global_quality : 120) / (2 * 2.5f * 120.0f);
        desired_bits = FFMIN(2560, PSY_3GPP_PE_TO_BITS(desired_pe));
        desired_pe = PSY_3GPP_BITS_TO_PE(desired_bits); // reflect clipping

        /* PE slope smoothing */
        if (ctx->bitres.bits > 0) {
            desired_bits = FFMIN(2560, PSY_3GPP_PE_TO_BITS(desired_pe));
            desired_pe = PSY_3GPP_BITS_TO_PE(desired_bits); // reflect clipping
        }

        pctx->pe.max = FFMAX(pe, pctx->pe.max);
        pctx->pe.min = FFMIN(pe, pctx->pe.min);
    } else {
        desired_bits = calc_bit_demand(pctx, pe, ctx->bitres.bits, ctx->bitres.size, wi->num_windows == 8);
        desired_pe = PSY_3GPP_BITS_TO_PE(desired_bits);

        /* NOTE: PE correction is kept simple. During initial testing it had very
         *       little effect on the final bitrate. Probably a good idea to come
         *       back and do more testing later.
         */
        if (ctx->bitres.bits > 0)
            desired_pe *= av_clipf(pctx->pe.previous / PSY_3GPP_BITS_TO_PE(ctx->bitres.bits),
                                   0.85f, 1.15f);
    }
    pctx->pe.previous = PSY_3GPP_BITS_TO_PE(desired_bits);
    ctx->bitres.alloc = desired_bits;

    if (desired_pe < pe) {
        /* 5.6.1.3.4 "First Estimation of the reduction value" */
        for (w = 0; w < wi->num_windows*16; w += 16) {
            reduction = calc_reduction_3gpp(a, desired_pe, pe, active_lines);
            pe = 0.0f;
            a  = 0.0f;
            active_lines = 0.0f;
            for (g = 0; g < num_bands; g++) {
                AacPsyBand *band = &pch->band[w+g];

                band->thr = calc_reduced_thr_3gpp(band, coeffs[g].min_snr, reduction);
                /* recalculate PE */
                pe += calc_pe_3gpp(band);
                a  += band->pe_const;
                active_lines += band->active_lines;
            }
        }

        /* 5.6.1.3.5 "Second Estimation of the reduction value" */
        for (i = 0; i < 2; i++) {
            float pe_no_ah = 0.0f, desired_pe_no_ah;
            active_lines = a = 0.0f;
            for (w = 0; w < wi->num_windows*16; w += 16) {
                for (g = 0; g < num_bands; g++) {
                    AacPsyBand *band = &pch->band[w+g];

                    if (band->avoid_holes != PSY_3GPP_AH_ACTIVE) {
                        pe_no_ah += band->pe;
                        a        += band->pe_const;
                        active_lines += band->active_lines;
                    }
                }
            }
            desired_pe_no_ah = FFMAX(desired_pe - (pe - pe_no_ah), 0.0f);
            if (active_lines > 0.0f)
                reduction = calc_reduction_3gpp(a, desired_pe_no_ah, pe_no_ah, active_lines);

            pe = 0.0f;
            for (w = 0; w < wi->num_windows*16; w += 16) {
                for (g = 0; g < num_bands; g++) {
                    AacPsyBand *band = &pch->band[w+g];

                    if (active_lines > 0.0f)
                        band->thr = calc_reduced_thr_3gpp(band, coeffs[g].min_snr, reduction);
                    pe += calc_pe_3gpp(band);
                    if (band->thr > 0.0f)
                        band->norm_fac = band->active_lines / band->thr;
                    else
                        band->norm_fac = 0.0f;
                    norm_fac += band->norm_fac;
                }
            }
            delta_pe = desired_pe - pe;
            if (fabs(delta_pe) > 0.05f * desired_pe)
                break;
        }

        if (pe < 1.15f * desired_pe) {
            /* 6.6.1.3.6 "Final threshold modification by linearization" */
            norm_fac = 1.0f / norm_fac;
            for (w = 0; w < wi->num_windows*16; w += 16) {
                for (g = 0; g < num_bands; g++) {
                    AacPsyBand *band = &pch->band[w+g];

                    if (band->active_lines > 0.5f) {
                        float delta_sfb_pe = band->norm_fac * norm_fac * delta_pe;
                        float thr = band->thr;

                        thr *= exp2f(delta_sfb_pe / band->active_lines);
                        if (thr > coeffs[g].min_snr * band->energy && band->avoid_holes == PSY_3GPP_AH_INACTIVE)
                            thr = FFMAX(band->thr, coeffs[g].min_snr * band->energy);
                        band->thr = thr;
                    }
                }
            }
        } else {
            /* 5.6.1.3.7 "Further perceptual entropy reduction" */
            g = num_bands;
            while (pe > desired_pe && g--) {
                for (w = 0; w < wi->num_windows*16; w+= 16) {
                    AacPsyBand *band = &pch->band[w+g];
                    if (band->avoid_holes != PSY_3GPP_AH_NONE && coeffs[g].min_snr < PSY_SNR_1DB) {
                        coeffs[g].min_snr = PSY_SNR_1DB;
                        band->thr = band->energy * PSY_SNR_1DB;
                        pe += band->active_lines * 1.5f - band->pe;
                    }
                }
            }
            /* TODO: allow more holes (unused without mid/side) */
        }
    }

    for (w = 0; w < wi->num_windows*16; w += 16) {
        for (g = 0; g < num_bands; g++) {
            AacPsyBand *band     = &pch->band[w+g];
            FFPsyBand  *psy_band = &ctx->ch[channel].psy_bands[w+g];

            psy_band->threshold = band->thr;
            psy_band->energy    = band->energy;
            psy_band->spread    = band->active_lines * 2.0f / band_sizes[g];
            psy_band->bits      = PSY_3GPP_PE_TO_BITS(band->pe);
        }
    }

    memcpy(pch->prev_band, pch->band, sizeof(pch->band));
}
```

## Bug Fix Patch

```diff
diff --git a/libavcodec/aacpsy.c b/libavcodec/aacpsy.c
index 482113d427..e51d29750b 100644
--- a/libavcodec/aacpsy.c
+++ b/libavcodec/aacpsy.c
@@ -794,7 +794,7 @@ static void psy_3gpp_analyze_channel(FFPsyContext *ctx, int channel,
 
         if (pe < 1.15f * desired_pe) {
             /* 6.6.1.3.6 "Final threshold modification by linearization" */
-            norm_fac = 1.0f / norm_fac;
+            norm_fac = norm_fac ? 1.0f / norm_fac : 0;
             for (w = 0; w < wi->num_windows*16; w += 16) {
                 for (g = 0; g < num_bands; g++) {
                     AacPsyBand *band = &pch->band[w+g];
```


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

File:| avfilter/avf_avectorscope.c  
---|---  
Warning:| line 358, column 23  
division by possibly zero aggregate factor  
  
### Annotated Source Code


238   |  static const enum AVSampleFormat sample_fmts[] = { AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_FLT, AV_SAMPLE_FMT_NONE };
239   |  static const enum AVPixelFormat pix_fmts[] = { AV_PIX_FMT_RGBA, AV_PIX_FMT_NONE };
240   |  static const AVChannelLayout layouts[] = {
241   |  AV_CHANNEL_LAYOUT_STEREO,
242   |         { .nb_channels = 0 },
243   |     };
244   |  int ret;
245   |  
246   |     formats = ff_make_sample_format_list(sample_fmts);
247   |  if ((ret = ff_formats_ref         (formats, &cfg_in[0]->formats        )) < 0)
248   |  return ret;
249   |  
250   |     ret = ff_set_common_channel_layouts_from_list2(ctx, cfg_in, cfg_out, layouts);
251   |  if (ret < 0)
252   |  return ret;
253   |  
254   |     formats = ff_make_pixel_format_list(pix_fmts);
255   |  if ((ret = ff_formats_ref(formats, &cfg_out[0]->formats)) < 0)
256   |  return ret;
257   |  
258   |  return 0;
259   | }
260   |  
261   | static int config_input(AVFilterLink *inlink)
262   | {
263   |     AVFilterContext *ctx = inlink->dst;
264   |     AudioVectorScopeContext *s = ctx->priv;
265   |  
266   |     s->nb_samples = FFMAX(1, av_rescale(inlink->sample_rate, s->frame_rate.den, s->frame_rate.num));
267   |  
268   |  return 0;
269   | }
270   |  
271   | static int config_output(AVFilterLink *outlink)
272   | {
273   |     AudioVectorScopeContext *s = outlink->src->priv;
274   |     FilterLink *l = ff_filter_link(outlink);
275   |  
276   |     outlink->w = s->w;
277   |     outlink->h = s->h;
278   |     outlink->sample_aspect_ratio = (AVRational){1,1};
279   |     l->frame_rate = s->frame_rate;
280   |     outlink->time_base = av_inv_q(l->frame_rate);
281   |  
282   |     s->prev_x = s->hw = s->w / 2;
283   |     s->prev_y = s->hh = s->mode == POLAR ? s->h - 1 : s->h / 2;
284   |  
285   |  return 0;
286   | }
287   |  
288   | static int filter_frame(AVFilterLink *inlink, AVFrame *insamples)
289   | {
290   |  AVFilterContext *ctx = inlink->dst;
291   |     AVFilterLink *outlink = ctx->outputs[0];
292   |  const int16_t *samples = (const int16_t *)insamples->data[0];
293   |  const float *samplesf = (const float *)insamples->data[0];
294   |     AudioVectorScopeContext *s = ctx->priv;
295   |  const int hw = s->hw;
296   |  const int hh = s->hh;
297   |     AVFrame *clone;
298   |  unsigned x, y;
299   |  unsigned prev_x = s->prev_x, prev_y = s->prev_y;
300   |  double zoom = s->zoom;
301   |  int ret;
302   |  
303   |  if (!s->outpicref || s->outpicref->width  != outlink->w ||
    1Assuming field 'outpicref' is non-null→
    2←Assuming field 'width' is equal to field 'w'→
    4←Taking false branch→
304   |  s->outpicref->height != outlink->h) {
    3←Assuming field 'height' is equal to field 'h'→
305   |         av_frame_free(&s->outpicref);
306   |         s->outpicref = ff_get_video_buffer(outlink, outlink->w, outlink->h);
307   |  if (!s->outpicref) {
308   |             av_frame_free(&insamples);
309   |  return AVERROR(ENOMEM);
310   |         }
311   |  
312   |         s->outpicref->sample_aspect_ratio = (AVRational){1,1};
313   |  for (int i = 0; i < outlink->h; i++)
314   |             memset(s->outpicref->data[0] + i * s->outpicref->linesize[0], 0, outlink->w * 4);
315   |     }
316   |  s->outpicref->pts = av_rescale_q(insamples->pts, inlink->time_base, outlink->time_base);
317   |     s->outpicref->duration = 1;
318   |  
319   |     ret = ff_inlink_make_frame_writable(outlink, &s->outpicref);
320   |  if (ret < 0) {
    5←Assuming 'ret' is >= 0→
    6←Taking false branch→
321   |         av_frame_free(&insamples);
322   |  return ret;
323   |     }
324   |  ff_filter_execute(ctx, fade, NULL, NULL, FFMIN(outlink->h, ff_filter_get_nb_threads(ctx)));
    7←Assuming the condition is false→
    8←'?' condition is false→
325   |  
326   |  if (zoom < 1) {
    9←Assuming 'zoom' is < 1→
    10←Taking true branch→
327   |  float max = 0;
328   |  
329   |  switch (insamples->format) {
    11←Control jumps to the 'default' case at line 341→
330   |  case AV_SAMPLE_FMT_S16:
331   |  for (int i = 0; i < insamples->nb_samples * 2; i++) {
332   |  float sample = samples[i] / (float)INT16_MAX;
333   |                 max = FFMAX(FFABS(sample), max);
334   |             }
335   |  break;
336   |  case AV_SAMPLE_FMT_FLT:
337   |  for (int i = 0; i < insamples->nb_samples * 2; i++) {
338   |                 max = FFMAX(FFABS(samplesf[i]), max);
339   |             }
340   |  break;
341   |  default:
342   |  av_assert2(0);
343   |         }
344   |  
345   |  switch (s->scale) {
346   |  case SQRT:
347   |             max = sqrtf(max);
348   |  break;
349   |  case CBRT:
350   |             max = cbrtf(max);
351   |  break;
352   |  case LOG:
353   |             max = logf(1 + max) / logf(2);
354   |  break;
355   |         }
356   |  
357   |  if (max > 0.f)
    12←'Default' branch taken. Execution continues on line 357→
    13←Assuming the condition is true→
    14←Taking true branch→
358   |  zoom = 1. / max;
    15←division by possibly zero aggregate factor
359   |     }
360   |  
361   |  for (int i = 0; i < insamples->nb_samples; i++) {
362   |  float src[2];
363   |  
364   |  switch (insamples->format) {
365   |  case AV_SAMPLE_FMT_S16:
366   |             src[0] = samples[i*2+0] / (float)INT16_MAX;
367   |             src[1] = samples[i*2+1] / (float)INT16_MAX;
368   |  break;
369   |  case AV_SAMPLE_FMT_FLT:
370   |             src[0] = samplesf[i*2+0];
371   |             src[1] = samplesf[i*2+1];
372   |  break;
373   |  default:
374   |  av_assert2(0);
375   |         }
376   |  
377   |  switch (s->scale) {
378   |  case SQRT:
379   |             src[0] = FFSIGN(src[0]) * sqrtf(FFABS(src[0]));
380   |             src[1] = FFSIGN(src[1]) * sqrtf(FFABS(src[1]));
381   |  break;
382   |  case CBRT:
383   |             src[0] = FFSIGN(src[0]) * cbrtf(FFABS(src[0]));
384   |             src[1] = FFSIGN(src[1]) * cbrtf(FFABS(src[1]));
385   |  break;
386   |  case LOG:
387   |             src[0] = FFSIGN(src[0]) * logf(1 + FFABS(src[0])) / logf(2);
388   |             src[1] = FFSIGN(src[1]) * logf(1 + FFABS(src[1])) / logf(2);

# Formatting

Please provide your answer in the following format:

- Decision: {Bug/NotABug}
- Reason: {Your reason here}
