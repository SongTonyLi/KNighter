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

File:| avfilter/af_aiir.c  
---|---  
Warning:| line 546, column 26  
division by possibly zero aggregate factor  
  
### Annotated Source Code


297   |  const double mix = s->mix;                                          \
298   |  ThreadData *td = arg;                                               \
299   |  AVFrame *in = td->in, *out = td->out;                               \
300   |  const type *src = (const type *)in->extended_data[ch];              \
301   |  double n0, n1, p0, *x = (double *)s->iir[ch].cache[0];              \
302   |  const int nb_stages = s->iir[ch].nb_ab[1];                          \
303   |  const double *v = s->iir[ch].ab[0];                                 \
304   |  const double *k = s->iir[ch].ab[1];                                 \
305   |  const double g = s->iir[ch].g;                                      \
306   |  int *clippings = &s->iir[ch].clippings;                             \
307   |  type *dst = (type *)out->extended_data[ch];                         \
308   |  int n;                                                              \
309   |  \
310   |  for (n = 0; n < in->nb_samples; n++) {                              \
311   |  const double in = src[n] * ig;                                  \
312   |  double out = 0.;                                                \
313   |  \
314   |  n1 = in;                                                        \
315   |  for (int i = nb_stages - 1; i >= 0; i--) {                      \
316   |  n0 = n1 - k[i] * x[i];                                      \
317   |  p0 = n0 * k[i] + x[i];                                      \
318   |  out += p0 * v[i+1];                                         \
319   |  x[i] = p0;                                                  \
320   |  n1 = n0;                                                    \
321   |  }                                                               \
322   |  \
323   |  out += n1 * v[0];                                               \
324   |  memmove(&x[1], &x[0], nb_stages * sizeof(*x));                  \
325   |  x[0] = n1;                                                      \
326   |  out *= og * g;                                                  \
327   |  out = out * mix + in * (1. - mix);                              \
328   |  if (need_clipping && out < min) {                               \
329   |  (*clippings)++;                                             \
330   |  dst[n] = min;                                               \
331   |  } else if (need_clipping && out > max) {                        \
332   |  (*clippings)++;                                             \
333   |  dst[n] = max;                                               \
334   |  } else {                                                        \
335   |  dst[n] = out;                                               \
336   |  }                                                               \
337   |  }                                                                   \
338   |  \
339   |  return 0;                                                           \
340   | }
341   |  
342   | LATTICE_IIR_CH(s16p, int16_t, INT16_MIN, INT16_MAX, 1)
343   | LATTICE_IIR_CH(s32p, int32_t, INT32_MIN, INT32_MAX, 1)
344   | LATTICE_IIR_CH(fltp, float,         -1.,        1., 0)
345   | LATTICE_IIR_CH(dblp, double,        -1.,        1., 0)
346   |  
347   | static void count_coefficients(char *item_str, int *nb_items)
348   | {
349   |  char *p;
350   |  
351   |  if (!item_str)
352   |  return;
353   |  
354   |     *nb_items = 1;
355   |  for (p = item_str; *p && *p != '|'; p++) {
356   |  if (*p == ' ')
357   |             (*nb_items)++;
358   |     }
359   | }
360   |  
361   | static int read_gains(AVFilterContext *ctx, char *item_str, int nb_items)
362   | {
363   |     AudioIIRContext *s = ctx->priv;
364   |  char *p, *arg, *old_str, *prev_arg = NULL, *saveptr = NULL;
365   |  int i;
366   |  
367   |     p = old_str = av_strdup(item_str);
368   |  if (!p)
369   |  return AVERROR(ENOMEM);
370   |  for (i = 0; i < nb_items; i++) {
371   |  if (!(arg = av_strtok(p, "|", &saveptr)))
372   |             arg = prev_arg;
373   |  
374   |  if (!arg) {
375   |             av_freep(&old_str);
376   |  return AVERROR(EINVAL);
377   |         }
378   |  
379   |         p = NULL;
380   |  if (av_sscanf(arg, "%lf", &s->iir[i].g) != 1) {
381   |             av_log(ctx, AV_LOG_ERROR, "Invalid gains supplied: %s\n", arg);
382   |             av_freep(&old_str);
383   |  return AVERROR(EINVAL);
384   |         }
385   |  
386   |         prev_arg = arg;
387   |     }
388   |  
389   |     av_freep(&old_str);
390   |  
391   |  return 0;
392   | }
393   |  
394   | static int read_tf_coefficients(AVFilterContext *ctx, char *item_str, int nb_items, double *dst)
395   | {
396   |  char *p, *arg, *old_str, *saveptr = NULL;
397   |  int i;
398   |  
399   |     p = old_str = av_strdup(item_str);
400   |  if (!p)
401   |  return AVERROR(ENOMEM);
402   |  for (i = 0; i < nb_items; i++) {
403   |  if (!(arg = av_strtok(p, " ", &saveptr)))
404   |  break;
405   |  
406   |         p = NULL;
407   |  if (av_sscanf(arg, "%lf", &dst[i]) != 1) {
408   |             av_log(ctx, AV_LOG_ERROR, "Invalid coefficients supplied: %s\n", arg);
409   |             av_freep(&old_str);
410   |  return AVERROR(EINVAL);
411   |         }
412   |     }
413   |  
414   |     av_freep(&old_str);
415   |  
416   |  return 0;
417   | }
418   |  
419   | static int read_zp_coefficients(AVFilterContext *ctx, char *item_str, int nb_items, double *dst, const char *format)
420   | {
421   |  char *p, *arg, *old_str, *saveptr = NULL;
422   |  int i;
423   |  
424   |     p = old_str = av_strdup(item_str);
425   |  if (!p)
426   |  return AVERROR(ENOMEM);
427   |  for (i = 0; i < nb_items; i++) {
428   |  if (!(arg = av_strtok(p, " ", &saveptr)))
429   |  break;
430   |  
431   |         p = NULL;
432   |  if (av_sscanf(arg, format, &dst[i*2], &dst[i*2+1]) != 2) {
433   |             av_log(ctx, AV_LOG_ERROR, "Invalid coefficients supplied: %s\n", arg);
434   |             av_freep(&old_str);
435   |  return AVERROR(EINVAL);
436   |         }
437   |     }
438   |  
439   |     av_freep(&old_str);
440   |  
441   |  return 0;
442   | }
443   |  
444   | static const char *const format[] = { "%lf", "%lf %lfi", "%lf %lfr", "%lf %lfd", "%lf %lfi" };
445   |  
446   | static int read_channels(AVFilterContext *ctx, int channels, uint8_t *item_str, int ab)
447   | {
448   |     AudioIIRContext *s = ctx->priv;
449   |  char *p, *arg, *old_str, *prev_arg = NULL, *saveptr = NULL;
450   |  int i, ret;
451   |  
452   |     p = old_str = av_strdup(item_str);
453   |  if (!p)
454   |  return AVERROR(ENOMEM);
455   |  for (i = 0; i < channels; i++) {
456   |         IIRChannel *iir = &s->iir[i];
457   |  
458   |  if (!(arg = av_strtok(p, "|", &saveptr)))
459   |             arg = prev_arg;
460   |  
461   |  if (!arg) {
462   |             av_freep(&old_str);
463   |  return AVERROR(EINVAL);
464   |         }
465   |  
466   |         count_coefficients(arg, &iir->nb_ab[ab]);
467   |  
468   |         p = NULL;
469   |         iir->cache[ab] = av_calloc(iir->nb_ab[ab] + 1, sizeof(double));
470   |         iir->ab[ab] = av_calloc(iir->nb_ab[ab] * (!!s->format + 1), sizeof(double));
471   |  if (!iir->ab[ab] || !iir->cache[ab]) {
472   |             av_freep(&old_str);
473   |  return AVERROR(ENOMEM);
474   |         }
475   |  
476   |  if (s->format > 0) {
477   |             ret = read_zp_coefficients(ctx, arg, iir->nb_ab[ab], iir->ab[ab], format[s->format]);
478   |         } else {
479   |             ret = read_tf_coefficients(ctx, arg, iir->nb_ab[ab], iir->ab[ab]);
480   |         }
481   |  if (ret < 0) {
482   |             av_freep(&old_str);
483   |  return ret;
484   |         }
485   |         prev_arg = arg;
486   |     }
487   |  
488   |     av_freep(&old_str);
489   |  
490   |  return 0;
491   | }
492   |  
493   | static void cmul(double re, double im, double re2, double im2, double *RE, double *IM)
494   | {
495   |     *RE = re * re2 - im * im2;
496   |     *IM = re * im2 + re2 * im;
497   | }
498   |  
499   | static int expand(AVFilterContext *ctx, double *pz, int n, double *coefs)
500   | {
501   |     coefs[2 * n] = 1.0;
502   |  
503   |  for (int i = 1; i <= n; i++) {
504   |  for (int j = n - i; j < n; j++) {
505   |  double re, im;
506   |  
507   |             cmul(coefs[2 * (j + 1)], coefs[2 * (j + 1) + 1],
508   |                  pz[2 * (i - 1)], pz[2 * (i - 1) + 1], &re, &im);
509   |  
510   |             coefs[2 * j]     -= re;
511   |             coefs[2 * j + 1] -= im;
512   |         }
513   |     }
514   |  
515   |  for (int i = 0; i < n + 1; i++) {
516   |  if (fabs(coefs[2 * i + 1]) > FLT_EPSILON) {
517   |             av_log(ctx, AV_LOG_ERROR, "coefs: %f of z^%d is not real; poles/zeros are not complex conjugates.\n",
518   |                    coefs[2 * i + 1], i);
519   |  return AVERROR(EINVAL);
520   |         }
521   |     }
522   |  
523   |  return 0;
524   | }
525   |  
526   | static void normalize_coeffs(AVFilterContext *ctx, int ch)
527   | {
528   |  AudioIIRContext *s = ctx->priv;
529   |     IIRChannel *iir = &s->iir[ch];
530   |  double sum_den = 0.;
531   |  
532   |  if (!s->normalize)
    32←Assuming field 'normalize' is not equal to 0→
    33←Taking false branch→
533   |  return;
534   |  
535   |  for (int i = 0; i < iir->nb_ab[1]; i++) {
    34←Assuming the condition is false→
    35←Loop condition is false. Execution continues on line 539→
536   |         sum_den += iir->ab[1][i];
537   |     }
538   |  
539   |  if (sum_den > 1e-6) {
    36←Assuming the condition is true→
    37←Taking true branch→
540   |  double factor, sum_num = 0.;
541   |  
542   |  for (int i = 0; i < iir->nb_ab[0]; i++) {
    38←Assuming the condition is false→
    39←Loop condition is false. Execution continues on line 546→
543   |             sum_num += iir->ab[0][i];
544   |         }
545   |  
546   |  factor = sum_num / sum_den;
    40←division by possibly zero aggregate factor
547   |  
548   |  for (int i = 0; i < iir->nb_ab[1]; i++) {
549   |             iir->ab[1][i] *= factor;
550   |         }
551   |     }
552   | }
553   |  
554   | static int convert_zp2tf(AVFilterContext *ctx, int channels)
555   | {
556   |  AudioIIRContext *s = ctx->priv;
557   |  int ch, i, j, ret = 0;
558   |  
559   |  for (ch = 0; ch < channels; ch++) {
    19←Loop condition is true.  Entering loop body→
560   |  IIRChannel *iir = &s->iir[ch];
561   |  double *topc, *botc;
562   |  
563   |         topc = av_calloc((iir->nb_ab[1] + 1) * 2, sizeof(*topc));
564   |         botc = av_calloc((iir->nb_ab[0] + 1) * 2, sizeof(*botc));
565   |  if (!topc || !botc) {
    20←Assuming 'topc' is non-null→
    21←Assuming 'botc' is non-null→
    22←Taking false branch→
566   |             ret = AVERROR(ENOMEM);
567   |  goto fail;
568   |         }
569   |  
570   |  ret = expand(ctx, iir->ab[0], iir->nb_ab[0], botc);
571   |  if (ret < 0) {
    23←Assuming 'ret' is >= 0→
    24←Taking false branch→
572   |  goto fail;
573   |         }
574   |  
575   |  ret = expand(ctx, iir->ab[1], iir->nb_ab[1], topc);
576   |  if (ret < 0) {
    25←Assuming 'ret' is >= 0→
    26←Taking false branch→
577   |  goto fail;
578   |         }
579   |  
580   |  for (j = 0, i = iir->nb_ab[1]; i >= 0; j++, i--) {
    27←Assuming 'i' is < 0→
    28←Loop condition is false. Execution continues on line 583→
581   |             iir->ab[1][j] = topc[2 * i];
582   |         }
583   |  iir->nb_ab[1]++;
584   |  
585   |  for (j = 0, i = iir->nb_ab[0]; i >= 0; j++, i--) {
    29←Assuming 'i' is < 0→
    30←Loop condition is false. Execution continues on line 588→
586   |             iir->ab[0][j] = botc[2 * i];
587   |         }
588   |  iir->nb_ab[0]++;
589   |  
590   |  normalize_coeffs(ctx, ch);
    31←Calling 'normalize_coeffs'→
591   |  
592   | fail:
593   |         av_free(topc);
594   |         av_free(botc);
595   |  if (ret < 0)
596   |  break;
597   |     }
598   |  
599   |  return ret;
600   | }
601   |  
602   | static int decompose_zp2biquads(AVFilterContext *ctx, int channels)
603   | {
604   |     AudioIIRContext *s = ctx->priv;
605   |  int ch, ret;
606   |  
607   |  for (ch = 0; ch < channels; ch++) {
608   |         IIRChannel *iir = &s->iir[ch];
609   |  int nb_biquads = (FFMAX(iir->nb_ab[0], iir->nb_ab[1]) + 1) / 2;
610   |  int current_biquad = 0;
611   |  
612   |         iir->biquads = av_calloc(nb_biquads, sizeof(BiquadContext));
613   |  if (!iir->biquads)
614   |  return AVERROR(ENOMEM);
615   |  
616   |  while (nb_biquads--) {
617   |             Pair outmost_pole = { -1, -1 };
618   |             Pair nearest_zero = { -1, -1 };
619   |  double zeros[4] = { 0 };
620   |  double poles[4] = { 0 };
1203  |  
1204  |  if (prev_ymag < 0)
1205  |             prev_ymag = ymag;
1206  |  if (prev_yphase < 0)
1207  |             prev_yphase = yphase;
1208  |  if (prev_ydelay < 0)
1209  |             prev_ydelay = ydelay;
1210  |  
1211  |         draw_line(out, i,   ymag, FFMAX(i - 1, 0),   prev_ymag, 0xFFFF00FF);
1212  |         draw_line(out, i, yphase, FFMAX(i - 1, 0), prev_yphase, 0xFF00FF00);
1213  |         draw_line(out, i, ydelay, FFMAX(i - 1, 0), prev_ydelay, 0xFF00FFFF);
1214  |  
1215  |         prev_ymag   = ymag;
1216  |         prev_yphase = yphase;
1217  |         prev_ydelay = ydelay;
1218  |     }
1219  |  
1220  |  if (s->w > 400 && s->h > 100) {
1221  |         drawtext(out, 2, 2, "Max Magnitude:", 0xDDDDDDDD);
1222  |         snprintf(text, sizeof(text), "%.2f", max);
1223  |         drawtext(out, 15 * 8 + 2, 2, text, 0xDDDDDDDD);
1224  |  
1225  |         drawtext(out, 2, 12, "Min Magnitude:", 0xDDDDDDDD);
1226  |         snprintf(text, sizeof(text), "%.2f", min);
1227  |         drawtext(out, 15 * 8 + 2, 12, text, 0xDDDDDDDD);
1228  |  
1229  |         drawtext(out, 2, 22, "Max Phase:", 0xDDDDDDDD);
1230  |         snprintf(text, sizeof(text), "%.2f", max_phase);
1231  |         drawtext(out, 15 * 8 + 2, 22, text, 0xDDDDDDDD);
1232  |  
1233  |         drawtext(out, 2, 32, "Min Phase:", 0xDDDDDDDD);
1234  |         snprintf(text, sizeof(text), "%.2f", min_phase);
1235  |         drawtext(out, 15 * 8 + 2, 32, text, 0xDDDDDDDD);
1236  |  
1237  |         drawtext(out, 2, 42, "Max Delay:", 0xDDDDDDDD);
1238  |         snprintf(text, sizeof(text), "%.2f", max_delay);
1239  |         drawtext(out, 11 * 8 + 2, 42, text, 0xDDDDDDDD);
1240  |  
1241  |         drawtext(out, 2, 52, "Min Delay:", 0xDDDDDDDD);
1242  |         snprintf(text, sizeof(text), "%.2f", min_delay);
1243  |         drawtext(out, 11 * 8 + 2, 52, text, 0xDDDDDDDD);
1244  |     }
1245  |  
1246  | end:
1247  |     av_free(delay);
1248  |     av_free(temp);
1249  |     av_free(phase);
1250  |     av_free(mag);
1251  | }
1252  |  
1253  | static int config_output(AVFilterLink *outlink)
1254  | {
1255  |  AVFilterContext *ctx = outlink->src;
1256  |     AudioIIRContext *s = ctx->priv;
1257  |     AVFilterLink *inlink = ctx->inputs[0];
1258  |  int ch, ret, i;
1259  |  
1260  |     s->channels = inlink->ch_layout.nb_channels;
1261  |     s->iir = av_calloc(s->channels, sizeof(*s->iir));
1262  |  if (!s->iir)
    1Assuming field 'iir' is non-null→
    2←Taking false branch→
1263  |  return AVERROR(ENOMEM);
1264  |  
1265  |  ret = read_gains(ctx, s->g_str, inlink->ch_layout.nb_channels);
1266  |  if (ret2.1'ret' is >= 0 < 0)
    3←Taking false branch→
1267  |  return ret;
1268  |  
1269  |  ret = read_channels(ctx, inlink->ch_layout.nb_channels, s->a_str, 0);
1270  |  if (ret3.1'ret' is >= 0 < 0)
    4←Taking false branch→
1271  |  return ret;
1272  |  
1273  |  ret = read_channels(ctx, inlink->ch_layout.nb_channels, s->b_str, 1);
1274  |  if (ret4.1'ret' is >= 0 < 0)
    5←Taking false branch→
1275  |  return ret;
1276  |  
1277  |  if (s->format == -1) {
    6←Assuming the condition is false→
    7←Taking false branch→
1278  |         convert_sf2tf(ctx, inlink->ch_layout.nb_channels);
1279  |         s->format = 0;
1280  |     } else if (s->format7.1Field 'format' is not equal to 2 == 2) {
    8←Taking false branch→
1281  |         convert_pr2zp(ctx, inlink->ch_layout.nb_channels);
1282  |     } else if (s->format8.1Field 'format' is not equal to 3 == 3) {
    9←Taking false branch→
1283  |         convert_pd2zp(ctx, inlink->ch_layout.nb_channels);
1284  |     } else if (s->format9.1Field 'format' is not equal to 4 == 4) {
    10←Taking false branch→
1285  |         convert_sp2zp(ctx, inlink->ch_layout.nb_channels);
1286  |     }
1287  |  if (s->format10.1Field 'format' is <= 0 > 0) {
    11←Taking false branch→
1288  |         check_stability(ctx, inlink->ch_layout.nb_channels);
1289  |     }
1290  |  
1291  |  av_frame_free(&s->video);
1292  |  if (s->response) {
    12←Assuming field 'response' is 0→
    13←Taking false branch→
1293  |         s->video = ff_get_video_buffer(ctx->outputs[1], s->w, s->h);
1294  |  if (!s->video)
1295  |  return AVERROR(ENOMEM);
1296  |  
1297  |         draw_response(ctx, s->video, inlink->sample_rate);
1298  |     }
1299  |  
1300  |  if (s->format == 0)
    14←Assuming field 'format' is not equal to 0→
1301  |         av_log(ctx, AV_LOG_WARNING, "transfer function coefficients format is not recommended for too high number of zeros/poles.\n");
1302  |  
1303  |  if (s->format > 0 && s->process == 0) {
    15←Assuming field 'format' is > 0→
    16←Assuming field 'process' is equal to 0→
    17←Taking true branch→
1304  |  av_log(ctx, AV_LOG_WARNING, "Direct processing is not recommended for zp coefficients format.\n");
1305  |  
1306  |  ret = convert_zp2tf(ctx, inlink->ch_layout.nb_channels);
    18←Calling 'convert_zp2tf'→
1307  |  if (ret < 0)
1308  |  return ret;
1309  |     } else if (s->format == -2 && s->process > 0) {
1310  |         av_log(ctx, AV_LOG_ERROR, "Only direct processing is implemented for lattice-ladder function.\n");
1311  |  return AVERROR_PATCHWELCOME;
1312  |     } else if (s->format <= 0 && s->process == 1) {
1313  |         av_log(ctx, AV_LOG_ERROR, "Serial processing is not implemented for transfer function.\n");
1314  |  return AVERROR_PATCHWELCOME;
1315  |     } else if (s->format <= 0 && s->process == 2) {
1316  |         av_log(ctx, AV_LOG_ERROR, "Parallel processing is not implemented for transfer function.\n");
1317  |  return AVERROR_PATCHWELCOME;
1318  |     } else if (s->format > 0 && s->process == 1) {
1319  |         ret = decompose_zp2biquads(ctx, inlink->ch_layout.nb_channels);
1320  |  if (ret < 0)
1321  |  return ret;
1322  |     } else if (s->format > 0 && s->process == 2) {
1323  |  if (s->precision > 1)
1324  |             av_log(ctx, AV_LOG_WARNING, "Parallel processing is not recommended for fixed-point precisions.\n");
1325  |         ret = decompose_zp2biquads(ctx, inlink->ch_layout.nb_channels);
1326  |  if (ret < 0)
1327  |  return ret;
1328  |         ret = convert_serial2parallel(ctx, inlink->ch_layout.nb_channels);
1329  |  if (ret < 0)
1330  |  return ret;
1331  |     }
1332  |  
1333  |  for (ch = 0; s->format == -2 && ch < inlink->ch_layout.nb_channels; ch++) {
1334  |         IIRChannel *iir = &s->iir[ch];
1335  |  
1336  |  if (iir->nb_ab[0] != iir->nb_ab[1] + 1) {

# Formatting

Please provide your answer in the following format:

- Decision: {Bug/NotABug}
- Reason: {Your reason here}
