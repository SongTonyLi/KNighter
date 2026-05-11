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

File:| avcodec/aaccoder.c  
---|---  
Warning:| line 617, column 42  
division by possibly zero aggregate factor  
  
### Annotated Source Code


439   |                                                           cb, 1.0f, INFINITY,
440   |                                                           &b, NULL, 0);
441   |                         bits += b;
442   |                     }
443   |                     dists[w*16+g] = dist - bits;
444   |  if (prev != -1) {
445   |                         bits += ff_aac_scalefactor_bits[sce->sf_idx[w*16+g] - prev + SCALE_DIFF_ZERO];
446   |                     }
447   |                     tbits += bits;
448   |                     start += sce->ics.swb_sizes[g];
449   |                     prev = sce->sf_idx[w*16+g];
450   |                 }
451   |             }
452   |  if (tbits > destbits) {
453   |  for (i = 0; i < 128; i++)
454   |  if (sce->sf_idx[i] < 218 - qstep)
455   |                         sce->sf_idx[i] += qstep;
456   |             } else {
457   |  for (i = 0; i < 128; i++)
458   |  if (sce->sf_idx[i] > 60 - qstep)
459   |                         sce->sf_idx[i] -= qstep;
460   |             }
461   |             qstep >>= 1;
462   |  if (!qstep && tbits > destbits*1.02 && sce->sf_idx[0] < 217)
463   |                 qstep = 1;
464   |         } while (qstep);
465   |  
466   |         fflag = 0;
467   |         minscaler = av_clip(minscaler, 60, 255 - SCALE_MAX_DIFF);
468   |  
469   |  for (w = 0; w < sce->ics.num_windows; w += sce->ics.group_len[w]) {
470   |  for (g = 0; g < sce->ics.num_swb; g++) {
471   |  int prevsc = sce->sf_idx[w*16+g];
472   |  if (dists[w*16+g] > uplims[w*16+g] && sce->sf_idx[w*16+g] > 60) {
473   |  if (find_min_book(maxvals[w*16+g], sce->sf_idx[w*16+g]-1))
474   |                         sce->sf_idx[w*16+g]--;
475   |  else //Try to make sure there is some energy in every band
476   |                         sce->sf_idx[w*16+g]-=2;
477   |                 }
478   |                 sce->sf_idx[w*16+g] = av_clip(sce->sf_idx[w*16+g], minscaler, minscaler + SCALE_MAX_DIFF);
479   |                 sce->sf_idx[w*16+g] = FFMIN(sce->sf_idx[w*16+g], 219);
480   |  if (sce->sf_idx[w*16+g] != prevsc)
481   |                     fflag = 1;
482   |                 sce->band_type[w*16+g] = find_min_book(maxvals[w*16+g], sce->sf_idx[w*16+g]);
483   |             }
484   |         }
485   |         its++;
486   |     } while (fflag && its < 10);
487   | }
488   |  
489   | static void search_for_pns(AACEncContext *s, AVCodecContext *avctx, SingleChannelElement *sce)
490   | {
491   |  FFPsyBand *band;
492   |  int w, g, w2, i;
493   |  int wlen = 1024 / sce->ics.num_windows;
494   |  int bandwidth, cutoff;
495   |  float *PNS = &s->scoefs[0*128], *PNS34 = &s->scoefs[1*128];
496   |  float *NOR34 = &s->scoefs[3*128];
497   |     uint8_t nextband[128];
498   |  const float lambda = s->lambda;
499   |  const float freq_mult = avctx->sample_rate*0.5f/wlen;
500   |  const float thr_mult = NOISE_LAMBDA_REPLACE*(100.0f/lambda);
501   |  const float spread_threshold = FFMIN(0.75f, NOISE_SPREAD_THRESHOLD*FFMAX(0.5f, lambda/100.f));
    1Assuming the condition is true→
    2←'?' condition is true→
    3←Assuming the condition is false→
    4←'?' condition is false→
502   |  const float dist_bias = av_clipf(4.f * 120 / lambda, 0.25f, 4.0f);
503   |  const float pns_transient_energy_r = FFMIN(0.7f, lambda / 140.f);
    5←Assuming the condition is false→
    6←'?' condition is false→
504   |  
505   |  int refbits = avctx->bit_rate * 1024.0 / avctx->sample_rate
506   |         / ((avctx->flags & AV_CODEC_FLAG_QSCALE) ? 2.0f : avctx->ch_layout.nb_channels)
    7←Assuming the condition is true→
    8←'?' condition is true→
507   |         * (lambda / 120.f);
508   |  
509   |  /** Keep this in sync with twoloop's cutoff selection */
510   |  float rate_bandwidth_multiplier = 1.5f;
511   |  int prev = -1000, prev_sf = -1;
512   |  int frame_bit_rate = (avctx->flags & AV_CODEC_FLAG_QSCALE)
    9←'?' condition is true→
513   |         ? (refbits * rate_bandwidth_multiplier * avctx->sample_rate / 1024)
514   |         : (avctx->bit_rate / avctx->ch_layout.nb_channels);
515   |  
516   |  frame_bit_rate *= 1.15f;
517   |  
518   |  if (avctx->cutoff > 0) {
    10←Assuming field 'cutoff' is > 0→
    11←Taking true branch→
519   |  bandwidth = avctx->cutoff;
520   |     } else {
521   |         bandwidth = FFMAX(3000, AAC_CUTOFF_FROM_BITRATE(frame_bit_rate, 1, avctx->sample_rate));
522   |     }
523   |  
524   |  cutoff = bandwidth * 2 * wlen / avctx->sample_rate;
525   |  
526   |     memcpy(sce->band_alt, sce->band_type, sizeof(sce->band_type));
527   |     ff_init_nextband_map(sce, nextband);
528   |  for (w = 0; w < sce->ics.num_windows; w += sce->ics.group_len[w]) {
    12←Assuming 'w' is < field 'num_windows'→
    13←Loop condition is true.  Entering loop body→
529   |  int wstart = w*128;
530   |  for (g = 0; g < sce->ics.num_swb; g++) {
    14←Assuming 'g' is < field 'num_swb'→
    15←Loop condition is true.  Entering loop body→
531   |  int noise_sfi;
532   |  float dist1 = 0.0f, dist2 = 0.0f, noise_amp;
533   |  float pns_energy = 0.0f, pns_tgt_energy, energy_ratio, dist_thresh;
534   |  float sfb_energy = 0.0f, threshold = 0.0f, spread = 2.0f;
535   |  float min_energy = -1.0f, max_energy = 0.0f;
536   |  const int start = wstart+sce->ics.swb_offset[g];
537   |  const float freq = (start-wstart)*freq_mult;
538   |  const float freq_boost = FFMAX(0.88f*freq/NOISE_LOW_LIMIT, 1.0f);
    16←Assuming the condition is false→
    17←'?' condition is false→
539   |  if (freq < NOISE_LOW_LIMIT || (start-wstart) >= cutoff) {
    18←Assuming 'freq' is >= NOISE_LOW_LIMIT→
    19←Assuming the condition is false→
    20←Taking false branch→
540   |  if (!sce->zeroes[w*16+g])
541   |                     prev_sf = sce->sf_idx[w*16+g];
542   |  continue;
543   |             }
544   |  for (w2 = 0; w2 < sce->ics.group_len[w]; w2++) {
    21←Assuming the condition is false→
    22←Loop condition is false. Execution continues on line 558→
545   |                 band = &s->psy.ch[s->cur_channel].psy_bands[(w+w2)*16+g];
546   |                 sfb_energy += band->energy;
547   |                 spread     = FFMIN(spread, band->spread);
548   |                 threshold  += band->threshold;
549   |  if (!w2) {
550   |                     min_energy = max_energy = band->energy;
551   |                 } else {
552   |                     min_energy = FFMIN(min_energy, band->energy);
553   |                     max_energy = FFMAX(max_energy, band->energy);
554   |                 }
555   |             }
556   |  
557   |  /* Ramps down at ~8000Hz and loosens the dist threshold */
558   |  dist_thresh = av_clipf(2.5f*NOISE_LOW_LIMIT/freq, 0.5f, 2.5f) * dist_bias;
559   |  
560   |  /* PNS is acceptable when all of these are true:
561   |  * 1. high spread energy (noise-like band)
562   |  * 2. near-threshold energy (high PE means the random nature of PNS content will be noticed)
563   |  * 3. on short window groups, all windows have similar energy (variations in energy would be destroyed by PNS)
564   |  *
565   |  * At this stage, point 2 is relaxed for zeroed bands near the noise threshold (hole avoidance is more important)
566   |  */
567   |  if ((!sce->zeroes[w*16+g] && !ff_sfdelta_can_remove_band(sce, nextband, prev_sf, w*16+g)) ||
    23←Assuming the condition is false→
    27←Taking false branch→
568   |                 ((sce->zeroes[w*16+g] || !sce->band_alt[w*16+g]) && sfb_energy < threshold*sqrtf(1.0f/freq_boost)) || spread < spread_threshold ||
    24←Assuming the condition is false→
    25←Assuming 'spread' is >= 'spread_threshold'→
569   |                 (!sce->zeroes[w*16+g] && sce->band_alt[w*16+g] && sfb_energy > threshold*thr_mult*freq_boost) ||
570   |  min_energy < pns_transient_energy_r * max_energy ) {
    26←Assuming the condition is false→
571   |                 sce->pns_ener[w*16+g] = sfb_energy;
572   |  if (!sce->zeroes[w*16+g])
573   |                     prev_sf = sce->sf_idx[w*16+g];
574   |  continue;
575   |             }
576   |  
577   |  pns_tgt_energy = sfb_energy*FFMIN(1.0f, spread*spread);
    28←Assuming the condition is false→
    29←'?' condition is false→
578   |             noise_sfi = av_clip(roundf(log2f(pns_tgt_energy)*2), -100, 155); /* Quantize */
579   |             noise_amp = -ff_aac_pow2sf_tab[noise_sfi + POW_SF2_ZERO];    /* Dequantize */
580   |  if (prev != -1000) {
    30←Taking false branch→
581   |  int noise_sfdiff = noise_sfi - prev + SCALE_DIFF_ZERO;
582   |  if (noise_sfdiff < 0 || noise_sfdiff > 2*SCALE_MAX_DIFF) {
583   |  if (!sce->zeroes[w*16+g])
584   |                         prev_sf = sce->sf_idx[w*16+g];
585   |  continue;
586   |                 }
587   |             }
588   |  for (w2 = 0; w2 < sce->ics.group_len[w]; w2++) {
589   |  float band_energy, scale, pns_senergy;
590   |  const int start_c = (w+w2)*128+sce->ics.swb_offset[g];
591   |                 band = &s->psy.ch[s->cur_channel].psy_bands[(w+w2)*16+g];
592   |  for (i = 0; i < sce->ics.swb_sizes[g]; i++) {
593   |                     s->random_state  = lcg_random(s->random_state);
594   |                     PNS[i] = s->random_state;
595   |                 }
596   |                 band_energy = s->fdsp->scalarproduct_float(PNS, PNS, sce->ics.swb_sizes[g]);
597   |                 scale = noise_amp/sqrtf(band_energy);
598   |                 s->fdsp->vector_fmul_scalar(PNS, PNS, scale, sce->ics.swb_sizes[g]);
599   |                 pns_senergy = s->fdsp->scalarproduct_float(PNS, PNS, sce->ics.swb_sizes[g]);
600   |                 pns_energy += pns_senergy;
601   |                 s->aacdsp.abs_pow34(NOR34, &sce->coeffs[start_c], sce->ics.swb_sizes[g]);
602   |                 s->aacdsp.abs_pow34(PNS34, PNS, sce->ics.swb_sizes[g]);
603   |                 dist1 += quantize_band_cost(s, &sce->coeffs[start_c],
604   |                                             NOR34,
605   |                                             sce->ics.swb_sizes[g],
606   |                                             sce->sf_idx[(w+w2)*16+g],
607   |                                             sce->band_alt[(w+w2)*16+g],
608   |                                             lambda/band->threshold, INFINITY, NULL, NULL);
609   |  /* Estimate rd on average as 5 bits for SF, 4 for the CB, plus spread energy * lambda/thr */
610   |                 dist2 += band->energy/(band->spread*band->spread)*lambda*dist_thresh/band->threshold;
611   |             }
612   |  if (g30.1'g' is 0 && sce->band_type[w*16+g-1] == NOISE_BT) {
613   |                 dist2 += 5;
614   |             } else {
615   |  dist2 += 9;
616   |             }
617   |             energy_ratio = pns_tgt_energy/pns_energy; /* Compensates for quantization error */
    31←division by possibly zero aggregate factor
618   |             sce->pns_ener[w*16+g] = energy_ratio*pns_tgt_energy;
619   |  if (sce->zeroes[w*16+g] || !sce->band_alt[w*16+g] || (energy_ratio > 0.85f && energy_ratio < 1.25f && dist2 < dist1)) {
620   |                 sce->band_type[w*16+g] = NOISE_BT;
621   |                 sce->zeroes[w*16+g] = 0;
622   |                 prev = noise_sfi;
623   |             } else {
624   |  if (!sce->zeroes[w*16+g])
625   |                     prev_sf = sce->sf_idx[w*16+g];
626   |             }
627   |         }
628   |     }
629   | }
630   |  
631   | static void mark_pns(AACEncContext *s, AVCodecContext *avctx, SingleChannelElement *sce)
632   | {
633   |     FFPsyBand *band;
634   |  int w, g, w2;
635   |  int wlen = 1024 / sce->ics.num_windows;
636   |  int bandwidth, cutoff;
637   |  const float lambda = s->lambda;
638   |  const float freq_mult = avctx->sample_rate*0.5f/wlen;
639   |  const float spread_threshold = FFMIN(0.75f, NOISE_SPREAD_THRESHOLD*FFMAX(0.5f, lambda/100.f));
640   |  const float pns_transient_energy_r = FFMIN(0.7f, lambda / 140.f);
641   |  
642   |  int refbits = avctx->bit_rate * 1024.0 / avctx->sample_rate
643   |         / ((avctx->flags & AV_CODEC_FLAG_QSCALE) ? 2.0f : avctx->ch_layout.nb_channels)
644   |         * (lambda / 120.f);
645   |  
646   |  /** Keep this in sync with twoloop's cutoff selection */
647   |  float rate_bandwidth_multiplier = 1.5f;

# Formatting

Please provide your answer in the following format:

- Decision: {Bug/NotABug}
- Reason: {Your reason here}
