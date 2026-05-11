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

File:| swresample/resample.c  
---|---  
Warning:| line 104, column 90  
division by possibly zero aggregate factor  
  
### Annotated Source Code


1     | /*
2     |  * audio resampling
3     |  * Copyright (c) 2004-2012 Michael Niedermayer <michaelni@gmx.at>
4     |  * bessel function: Copyright (c) 2006 Xiaogang Zhang
5     |  *
6     |  * This file is part of FFmpeg.
7     |  *
8     |  * FFmpeg is free software; you can redistribute it and/or
9     |  * modify it under the terms of the GNU Lesser General Public
10    |  * License as published by the Free Software Foundation; either
11    |  * version 2.1 of the License, or (at your option) any later version.
12    |  *
13    |  * FFmpeg is distributed in the hope that it will be useful,
14    |  * but WITHOUT ANY WARRANTY; without even the implied warranty of
15    |  * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
16    |  * Lesser General Public License for more details.
17    |  *
18    |  * You should have received a copy of the GNU Lesser General Public
19    |  * License along with FFmpeg; if not, write to the Free Software
20    |  * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
21    |  */
22    |  
23    | /**
24    |  * @file
25    |  * audio resampling
26    |  * @author Michael Niedermayer <michaelni@gmx.at>
27    |  */
28    |  
29    | #include "libavutil/avassert.h"
30    | #include "libavutil/mem.h"
31    | #include "resample.h"
32    |  
33    | /**
34    |  * builds a polyphase filterbank.
35    |  * @param factor resampling factor
36    |  * @param scale wanted sum of coefficients for each filter
37    |  * @param filter_type  filter type
38    |  * @param kaiser_beta  kaiser window beta
39    |  * @return 0 on success, negative on error
40    |  */
41    | static int build_filter(ResampleContext *c, void *filter, double factor, int tap_count, int alloc, int phase_count, int scale,
42    |  int filter_type, double kaiser_beta){
43    |  int ph, i;
44    |  int ph_nb = phase_count % 2 ? phase_count : phase_count / 2 + 1;
    15←Assuming the condition is false→
    16←'?' condition is false→
45    |  double x, y, w, t, s;
46    |  double *tab = av_malloc_array(tap_count+1,  sizeof(*tab));
47    |  double *sin_lut = av_malloc_array(ph_nb, sizeof(*sin_lut));
48    |  const int center= (tap_count-1)/2;
49    |  double norm = 0;
50    |  int ret = AVERROR(ENOMEM);
51    |  
52    |  if (!tab || !sin_lut)
    17←Assuming 'tab' is non-null→
    18←Assuming 'sin_lut' is non-null→
53    |  goto fail;
54    |  
55    |  av_assert0(tap_count == 1 || tap_count % 2 == 0);
    19←Taking false branch→
    20←Assuming 'tap_count' is equal to 1→
    21←Taking false branch→
    22←Loop condition is false.  Exiting loop→
56    |  
57    |  /* if upsampling, only need to interpolate, no filter */
58    |  if (factor > 1.0)
    23←Assuming the condition is false→
    24←Taking false branch→
59    |         factor = 1.0;
60    |  
61    |  if (factor == 1.0) {
    25←Assuming the condition is false→
    26←Taking false branch→
62    |  for (ph = 0; ph < ph_nb; ph++)
63    |             sin_lut[ph] = sin(M_PI * ph / phase_count) * (center & 1 ? 1 : -1);
64    |     }
65    |  for(ph = 0; ph < ph_nb; ph++) {
    27←Assuming 'ph' is < 'ph_nb'→
    28←Loop condition is true.  Entering loop body→
66    |  s = sin_lut[ph];
67    |  for(i=0;i<tap_count;i++) {
    29←Loop condition is true.  Entering loop body→
    37←Loop condition is false. Execution continues on line 101→
68    |  x = M_PI * ((double)(i - center) - (double)ph / phase_count) * factor;
69    |  if (x == 0) y = 1.0;
    30←Assuming 'x' is equal to 0→
    31←Taking true branch→
70    |  else if (factor == 1.0)
71    |                 y = s / x;
72    |  else
73    |                 y = sin(x) / x;
74    |  switch(filter_type){
    32←Control jumps to 'case SWR_FILTER_TYPE_KAISER:'  at line 86→
75    |  case SWR_FILTER_TYPE_CUBIC:{
76    |  const float d= -0.5; //first order derivative = -0.5
77    |                 x = fabs(((double)(i - center) - (double)ph / phase_count) * factor);
78    |  if(x<1.0) y= 1 - 3*x*x + 2*x*x*x + d*(            -x*x + x*x*x);
79    |  else      y=                       d*(-4 + 8*x - 5*x*x + x*x*x);
80    |  break;}
81    |  case SWR_FILTER_TYPE_BLACKMAN_NUTTALL:
82    |                 w = 2.0*x / (factor*tap_count);
83    |                 t = -cos(w);
84    |                 y *= 0.3635819 - 0.4891775 * t + 0.1365995 * (2*t*t-1) - 0.0106411 * (4*t*t*t - 3*t);
85    |  break;
86    |  case SWR_FILTER_TYPE_KAISER:
87    |  w = 2.0*x / (factor*tap_count*M_PI);
88    |  y *= av_bessel_i0(kaiser_beta*sqrt(FFMAX(1-w*w, 0)));
    33←Assuming the condition is false→
    34←'?' condition is false→
89    |  break;
    35← Execution continues on line 94→
90    |  default:
91    |  av_assert0(0);
92    |             }
93    |  
94    |  tab[i] = y;
95    |             s = -s;
96    |  if (!ph35.1'ph' is 0)
    36←Taking true branch→
97    |  norm += y;
98    |  }
99    |  
100   |  /* normalize so that an uniform color remains the same */
101   |  switch(c->format){
    38←Control jumps to 'case AV_SAMPLE_FMT_S16P:'  at line 102→
102   |  case AV_SAMPLE_FMT_S16P:
103   |  for(i=0;i<tap_count;i++)
    39←Loop condition is true.  Entering loop body→
104   |  ((int16_t*)filter)[ph * alloc + i] = av_clip_int16(lrintf(tab[i] * scale / norm));
    40←division by possibly zero aggregate factor
105   |  if (phase_count % 2) break;
106   |  for (i = 0; i < tap_count; i++)
107   |                 ((int16_t*)filter)[(phase_count-ph) * alloc + tap_count-1-i] = ((int16_t*)filter)[ph * alloc + i];
108   |  break;
109   |  case AV_SAMPLE_FMT_S32P:
110   |  for(i=0;i<tap_count;i++)
111   |                 ((int32_t*)filter)[ph * alloc + i] = av_clipl_int32(llrint(tab[i] * scale / norm));
112   |  if (phase_count % 2) break;
113   |  for (i = 0; i < tap_count; i++)
114   |                 ((int32_t*)filter)[(phase_count-ph) * alloc + tap_count-1-i] = ((int32_t*)filter)[ph * alloc + i];
115   |  break;
116   |  case AV_SAMPLE_FMT_FLTP:
117   |  for(i=0;i<tap_count;i++)
118   |                 ((float*)filter)[ph * alloc + i] = tab[i] * scale / norm;
119   |  if (phase_count % 2) break;
120   |  for (i = 0; i < tap_count; i++)
121   |                 ((float*)filter)[(phase_count-ph) * alloc + tap_count-1-i] = ((float*)filter)[ph * alloc + i];
122   |  break;
123   |  case AV_SAMPLE_FMT_DBLP:
124   |  for(i=0;i<tap_count;i++)
125   |                 ((double*)filter)[ph * alloc + i] = tab[i] * scale / norm;
126   |  if (phase_count % 2) break;
127   |  for (i = 0; i < tap_count; i++)
128   |                 ((double*)filter)[(phase_count-ph) * alloc + tap_count-1-i] = ((double*)filter)[ph * alloc + i];
129   |  break;
130   |         }
131   |     }
132   | #if 0
133   |     {
134   | #define LEN 1024
230   |  default:
231   |             av_log(NULL, AV_LOG_ERROR, "Unsupported sample format\n");
232   |  av_assert0(0);
233   |         }
234   |  
235   |  if (filter_size/factor > INT32_MAX/256) {
236   |             av_log(NULL, AV_LOG_ERROR, "Filter length too large\n");
237   |  goto error;
238   |         }
239   |  
240   |         c->phase_count   = phase_count;
241   |         c->linear        = linear;
242   |         c->factor        = factor;
243   |         c->filter_length = filter_length;
244   |         c->filter_alloc  = FFALIGN(c->filter_length, 8);
245   |         c->filter_bank   = av_calloc(c->filter_alloc, (phase_count+1)*c->felem_size);
246   |         c->filter_type   = filter_type;
247   |         c->kaiser_beta   = kaiser_beta;
248   |         c->phase_count_compensation = phase_count_compensation;
249   |  if (!c->filter_bank)
250   |  goto error;
251   |  if (build_filter(c, (void*)c->filter_bank, factor, c->filter_length, c->filter_alloc, phase_count, 1<<c->filter_shift, filter_type, kaiser_beta))
252   |  goto error;
253   |         memcpy(c->filter_bank + (c->filter_alloc*phase_count+1)*c->felem_size, c->filter_bank, (c->filter_alloc-1)*c->felem_size);
254   |         memcpy(c->filter_bank + (c->filter_alloc*phase_count  )*c->felem_size, c->filter_bank + (c->filter_alloc - 1)*c->felem_size, c->felem_size);
255   |     }
256   |  
257   |     c->compensation_distance= 0;
258   |  if(!av_reduce(&c->src_incr, &c->dst_incr, out_rate, in_rate * (int64_t)phase_count, INT32_MAX/2))
259   |  goto error;
260   |  while (c->dst_incr < (1<<20) && c->src_incr < (1<<20)) {
261   |         c->dst_incr *= 2;
262   |         c->src_incr *= 2;
263   |     }
264   |     c->ideal_dst_incr = c->dst_incr;
265   |     c->dst_incr_div   = c->dst_incr / c->src_incr;
266   |     c->dst_incr_mod   = c->dst_incr % c->src_incr;
267   |  
268   |     c->index= -phase_count*((c->filter_length-1)/2);
269   |     c->frac= 0;
270   |  
271   |     swri_resample_dsp_init(c);
272   |  
273   |  return c;
274   | error:
275   |     av_freep(&c->filter_bank);
276   |     av_free(c);
277   |  return NULL;
278   | }
279   |  
280   | static int rebuild_filter_bank_with_compensation(ResampleContext *c)
281   | {
282   |  uint8_t *new_filter_bank;
283   |  int new_src_incr, new_dst_incr;
284   |  int phase_count = c->phase_count_compensation;
285   |  int ret;
286   |  
287   |  if (phase_count == c->phase_count)
    5←Assuming 'phase_count' is not equal to field 'phase_count'→
288   |  return 0;
289   |  
290   |  av_assert0(!c->frac && !c->dst_incr_mod);
    6←Taking false branch→
    7←Assuming field 'frac' is 0→
    8←Assuming field 'dst_incr_mod' is 0→
    9←Taking false branch→
    10←Loop condition is false.  Exiting loop→
291   |  
292   |  new_filter_bank = av_calloc(c->filter_alloc, (phase_count + 1) * c->felem_size);
293   |  if (!new_filter_bank)
    11←Assuming 'new_filter_bank' is non-null→
    12←Taking false branch→
294   |  return AVERROR(ENOMEM);
295   |  
296   |  ret = build_filter(c, new_filter_bank, c->factor, c->filter_length, c->filter_alloc,
    14←Calling 'build_filter'→
297   |  phase_count, 1 << c->filter_shift, c->filter_type, c->kaiser_beta);
    13←Assuming right operand of bit shift is non-negative but less than 32→
298   |  if (ret < 0) {
299   |         av_freep(&new_filter_bank);
300   |  return ret;
301   |     }
302   |     memcpy(new_filter_bank + (c->filter_alloc*phase_count+1)*c->felem_size, new_filter_bank, (c->filter_alloc-1)*c->felem_size);
303   |     memcpy(new_filter_bank + (c->filter_alloc*phase_count  )*c->felem_size, new_filter_bank + (c->filter_alloc - 1)*c->felem_size, c->felem_size);
304   |  
305   |  if (!av_reduce(&new_src_incr, &new_dst_incr, c->src_incr,
306   |                    c->dst_incr * (int64_t)(phase_count/c->phase_count), INT32_MAX/2))
307   |     {
308   |         av_freep(&new_filter_bank);
309   |  return AVERROR(EINVAL);
310   |     }
311   |  
312   |     c->src_incr = new_src_incr;
313   |     c->dst_incr = new_dst_incr;
314   |  while (c->dst_incr < (1<<20) && c->src_incr < (1<<20)) {
315   |         c->dst_incr *= 2;
316   |         c->src_incr *= 2;
317   |     }
318   |     c->ideal_dst_incr = c->dst_incr;
319   |     c->dst_incr_div   = c->dst_incr / c->src_incr;
320   |     c->dst_incr_mod   = c->dst_incr % c->src_incr;
321   |     c->index         *= phase_count / c->phase_count;
322   |     c->phase_count    = phase_count;
323   |     av_freep(&c->filter_bank);
324   |     c->filter_bank = new_filter_bank;
325   |  return 0;
326   | }
327   |  
328   | static int set_compensation(ResampleContext *c, int sample_delta, int compensation_distance){
329   |  int ret;
330   |  
331   |  if (compensation_distance && sample_delta) {
    1Assuming 'compensation_distance' is not equal to 0→
    2←Assuming 'sample_delta' is not equal to 0→
    3←Taking true branch→
332   |  ret = rebuild_filter_bank_with_compensation(c);
    4←Calling 'rebuild_filter_bank_with_compensation'→
333   |  if (ret < 0)
334   |  return ret;
335   |     }
336   |  
337   |     c->compensation_distance= compensation_distance;
338   |  if (compensation_distance)
339   |         c->dst_incr = c->ideal_dst_incr - c->ideal_dst_incr * (int64_t)sample_delta / compensation_distance;
340   |  else
341   |         c->dst_incr = c->ideal_dst_incr;
342   |  
343   |     c->dst_incr_div   = c->dst_incr / c->src_incr;
344   |     c->dst_incr_mod   = c->dst_incr % c->src_incr;
345   |  
346   |  return 0;
347   | }
348   |  
349   | static int multiple_resample(ResampleContext *c, AudioData *dst, int dst_size, AudioData *src, int src_size, int *consumed){
350   |  int i;
351   |     int64_t max_src_size = (INT64_MAX/2 / c->phase_count) / c->src_incr;
352   |  
353   |  if (c->compensation_distance)
354   |         dst_size = FFMIN(dst_size, c->compensation_distance);
355   |     src_size = FFMIN(src_size, max_src_size);
356   |  
357   |     *consumed = 0;
358   |  
359   |  if (c->filter_length == 1 && c->phase_count == 1) {
360   |         int64_t index2= (1LL<<32)*c->frac/c->src_incr + (1LL<<32)*c->index + 1;
361   |         int64_t incr= (1LL<<32) * c->dst_incr / c->src_incr + 1;
362   |  int new_size = (src_size * (int64_t)c->src_incr - c->frac + c->dst_incr - 1) / c->dst_incr;

# Formatting

Please provide your answer in the following format:

- Decision: {Bug/NotABug}
- Reason: {Your reason here}
