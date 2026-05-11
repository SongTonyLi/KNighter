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

File:| avfilter/af_aspectralstats.c  
---|---  
Warning:| line 397, column 16  
division by possibly zero aggregate factor  
  
### Annotated Source Code


331   |     den = logf(size);
332   |  if (den <= FLT_EPSILON)
333   |  return 1.f;
334   |  return -num / den;
335   | }
336   |  
337   | static float spectral_flatness(const float *const spectral, int size, int max_freq)
338   | {
339   |  float num = 0.f, den = 0.f;
340   |  
341   |  for (int n = 0; n < size; n++) {
342   |  float v = FLT_EPSILON + spectral[n];
343   |         num += logf(v);
344   |         den += v;
345   |     }
346   |  
347   |     num /= size;
348   |     den /= size;
349   |     num = expf(num);
350   |  if (den <= FLT_EPSILON)
351   |  return 0.f;
352   |  return num / den;
353   | }
354   |  
355   | static float spectral_crest(const float *const spectral, int size, int max_freq)
356   | {
357   |  float max = 0.f, mean = 0.f;
358   |  
359   |  for (int n = 0; n < size; n++) {
360   |         max = fmaxf(max, spectral[n]);
361   |         mean += spectral[n];
362   |     }
363   |  
364   |     mean /= size;
365   |  if (mean <= FLT_EPSILON)
366   |  return 0.f;
367   |  return max / mean;
368   | }
369   |  
370   | static float spectral_flux(const float *const spectral, const float *const prev_spectral,
371   |  int size, int max_freq)
372   | {
373   |  float sum = 0.f;
374   |  
375   |  for (int n = 0; n < size; n++)
376   |         sum += sqrf(spectral[n] - prev_spectral[n]);
377   |  
378   |  return sqrtf(sum);
379   | }
380   |  
381   | static float spectral_slope(const float *const spectral, int size, int max_freq)
382   | {
383   |  const float mean_freq = size * 0.5f;
384   |  float mean_spectral = 0.f, num = 0.f, den = 0.f;
385   |  
386   |  for (int n = 0; n30.1'n' is >= 'size' < size; n++)
    31←Loop condition is false. Execution continues on line 388→
387   |         mean_spectral += spectral[n];
388   |  mean_spectral /= size;
389   |  
390   |  for (int n = 0; n31.1'n' is >= 'size' < size; n++) {
    32←Loop condition is false. Execution continues on line 395→
391   |         num += ((n - mean_freq) / mean_freq) * (spectral[n] - mean_spectral);
392   |         den += sqrf((n - mean_freq) / mean_freq);
393   |     }
394   |  
395   |  if (fabsf(den) <= FLT_EPSILON)
    33←Assuming the condition is false→
    34←Taking false branch→
396   |  return 0.f;
397   |  return num / den;
    35←division by possibly zero aggregate factor
398   | }
399   |  
400   | static float spectral_decrease(const float *const spectral, int size, int max_freq)
401   | {
402   |  float num = 0.f, den = 0.f;
403   |  
404   |  for (int n = 1; n < size; n++) {
405   |         num += (spectral[n] - spectral[0]) / n;
406   |         den += spectral[n];
407   |     }
408   |  
409   |  if (den <= FLT_EPSILON)
410   |  return 0.f;
411   |  return num / den;
412   | }
413   |  
414   | static float spectral_rolloff(const float *const spectral, int size, int max_freq)
415   | {
416   |  const float scale = max_freq / (float)size;
417   |  float norm = 0.f, sum = 0.f;
418   |  int idx = 0.f;
419   |  
420   |  for (int n = 0; n < size; n++)
421   |         norm += spectral[n];
422   |     norm *= 0.85f;
423   |  
424   |  for (int n = 0; n < size; n++) {
425   |         sum += spectral[n];
426   |  if (sum >= norm) {
427   |             idx = n;
428   |  break;
429   |         }
430   |     }
431   |  
432   |  return idx * scale;
433   | }
434   |  
435   | static int filter_channel(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
436   | {
437   |  AudioSpectralStatsContext *s = ctx->priv;
438   |  const float *window_func_lut = s->window_func_lut;
439   |     AVFrame *in = arg;
440   |  const int channels = s->nb_channels;
441   |  const int start = (channels * jobnr) / nb_jobs;
442   |  const int end = (channels * (jobnr+1)) / nb_jobs;
443   |  const int offset = s->win_size - s->hop_size;
444   |  
445   |  for (int ch = start; ch < end; ch++) {
    1Assuming 'ch' is < 'end'→
    2←Loop condition is true.  Entering loop body→
446   |  float *window = (float *)s->window->extended_data[ch];
447   |         ChannelSpectralStats *stats = &s->stats[ch];
448   |         AVComplexFloat *fft_out = s->fft_out[ch];
449   |         AVComplexFloat *fft_in = s->fft_in[ch];
450   |  float *magnitude = s->magnitude[ch];
451   |  float *prev_magnitude = s->prev_magnitude[ch];
452   |  const float scale = 1.f / s->win_size;
453   |  
454   |         memmove(window, &window[s->hop_size], offset * sizeof(float));
455   |         memcpy(&window[offset], in->extended_data[ch], in->nb_samples * sizeof(float));
456   |         memset(&window[offset + in->nb_samples], 0, (s->hop_size - in->nb_samples) * sizeof(float));
457   |  
458   |  for (int n = 0; n < s->win_size; n++) {
    3←Assuming 'n' is >= field 'win_size'→
    4←Loop condition is false. Execution continues on line 463→
459   |             fft_in[n].re = window[n] * window_func_lut[n];
460   |             fft_in[n].im = 0;
461   |         }
462   |  
463   |  s->tx_fn(s->fft[ch], fft_out, fft_in, sizeof(*fft_in));
464   |  
465   |  for (int n = 0; n < s->win_size / 2; n++) {
    5←Assuming the condition is false→
    6←Loop condition is false. Execution continues on line 470→
466   |             fft_out[n].re *= scale;
467   |             fft_out[n].im *= scale;
468   |         }
469   |  
470   |  for (int n = 0; n < s->win_size / 2; n++)
    7←Loop condition is false. Execution continues on line 473→
471   |             magnitude[n] = hypotf(fft_out[n].re, fft_out[n].im);
472   |  
473   |  if (s->measure & (MEASURE_MEAN | MEASURE_VARIANCE))
    8←Assuming the condition is false→
    9←Taking false branch→
474   |             stats->mean     = spectral_mean(magnitude, s->win_size / 2, in->sample_rate / 2);
475   |  if (s->measure & MEASURE_VARIANCE)
    10←Assuming the condition is false→
    11←Taking false branch→
476   |             stats->variance = spectral_variance(magnitude, s->win_size / 2, in->sample_rate / 2, stats->mean);
477   |  if (s->measure & (MEASURE_SPREAD | MEASURE_KURTOSIS | MEASURE_SKEWNESS | MEASURE_CENTROID))
    12←Assuming the condition is false→
    13←Taking false branch→
478   |             stats->centroid = spectral_centroid(magnitude, s->win_size / 2, in->sample_rate / 2);
479   |  if (s->measure & (MEASURE_SPREAD | MEASURE_KURTOSIS | MEASURE_SKEWNESS))
    14←Assuming the condition is false→
    15←Taking false branch→
480   |             stats->spread   = spectral_spread(magnitude, s->win_size / 2, in->sample_rate / 2, stats->centroid);
481   |  if (s->measure & MEASURE_SKEWNESS)
    16←Assuming the condition is false→
    17←Taking false branch→
482   |             stats->skewness = spectral_skewness(magnitude, s->win_size / 2, in->sample_rate / 2, stats->centroid, stats->spread);
483   |  if (s->measure & MEASURE_KURTOSIS)
    18←Assuming the condition is false→
    19←Taking false branch→
484   |             stats->kurtosis = spectral_kurtosis(magnitude, s->win_size / 2, in->sample_rate / 2, stats->centroid, stats->spread);
485   |  if (s->measure & MEASURE_ENTROPY)
    20←Assuming the condition is false→
    21←Taking false branch→
486   |             stats->entropy  = spectral_entropy(magnitude, s->win_size / 2, in->sample_rate / 2);
487   |  if (s->measure & MEASURE_FLATNESS)
    22←Assuming the condition is false→
    23←Taking false branch→
488   |             stats->flatness = spectral_flatness(magnitude, s->win_size / 2, in->sample_rate / 2);
489   |  if (s->measure & MEASURE_CREST)
    24←Assuming the condition is false→
    25←Taking false branch→
490   |             stats->crest    = spectral_crest(magnitude, s->win_size / 2, in->sample_rate / 2);
491   |  if (s->measure & MEASURE_FLUX)
    26←Assuming the condition is false→
    27←Taking false branch→
492   |             stats->flux     = spectral_flux(magnitude, prev_magnitude, s->win_size / 2, in->sample_rate / 2);
493   |  if (s->measure & MEASURE_SLOPE)
    28←Assuming the condition is true→
    29←Taking true branch→
494   |  stats->slope    = spectral_slope(magnitude, s->win_size / 2, in->sample_rate / 2);
    30←Calling 'spectral_slope'→
495   |  if (s->measure & MEASURE_DECREASE)
496   |             stats->decrease = spectral_decrease(magnitude, s->win_size / 2, in->sample_rate / 2);
497   |  if (s->measure & MEASURE_ROLLOFF)
498   |             stats->rolloff  = spectral_rolloff(magnitude, s->win_size / 2, in->sample_rate / 2);
499   |  
500   |         memcpy(prev_magnitude, magnitude, s->win_size * sizeof(float));
501   |     }
502   |  
503   |  return 0;
504   | }
505   |  
506   | static int filter_frame(AVFilterLink *inlink, AVFrame *in)
507   | {
508   |     AVFilterContext *ctx = inlink->dst;
509   |     AVFilterLink *outlink = ctx->outputs[0];
510   |     AudioSpectralStatsContext *s = ctx->priv;
511   |     AVDictionary **metadata;
512   |     AVFrame *out;
513   |  int ret;
514   |  
515   |  if (av_frame_is_writable(in)) {
516   |         out = in;
517   |     } else {
518   |         out = ff_get_audio_buffer(outlink, in->nb_samples);
519   |  if (!out) {
520   |             av_frame_free(&in);
521   |  return AVERROR(ENOMEM);
522   |         }
523   |         ret = av_frame_copy_props(out, in);
524   |  if (ret < 0)

# Formatting

Please provide your answer in the following format:

- Decision: {Bug/NotABug}
- Reason: {Your reason here}
