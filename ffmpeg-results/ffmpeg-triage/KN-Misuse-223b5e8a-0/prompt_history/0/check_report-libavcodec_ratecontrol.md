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

File:| avcodec/ratecontrol.c  
---|---  
Warning:| line 883, column 30  
division by possibly zero aggregate factor  
  
### Annotated Source Code


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
25    |  * Rate control for video encoders.
26    |  */
27    |  
28    | #include "libavutil/attributes.h"
29    | #include "libavutil/internal.h"
30    | #include "libavutil/mem.h"
31    |  
32    | #include "avcodec.h"
33    | #include "ratecontrol.h"
34    | #include "mpegvideoenc.h"
35    | #include "libavutil/eval.h"
36    |  
37    | void ff_write_pass1_stats(MPVMainEncContext *const m)
38    | {
39    |  const MPVEncContext *const s = &m->s;
40    |     snprintf(s->c.avctx->stats_out, 256,
41    |  "in:%d out:%d type:%d q:%d itex:%d ptex:%d mv:%d misc:%d "
42    |  "fcode:%d bcode:%d mc-var:%"PRId64" var:%"PRId64" icount:%d hbits:%d;\n",
43    |              s->c.cur_pic.ptr->display_picture_number,
44    |              s->c.cur_pic.ptr->coded_picture_number,
45    |              s->c.pict_type,
46    |              s->c.cur_pic.ptr->f->quality,
47    |              s->i_tex_bits,
48    |              s->p_tex_bits,
49    |              s->mv_bits,
50    |              s->misc_bits,
51    |              s->f_code,
52    |              s->b_code,
53    |              m->mc_mb_var_sum,
54    |              m->mb_var_sum,
55    |              s->i_count,
56    |              m->header_bits);
57    | }
58    |  
59    | static AVRational get_fpsQ(AVCodecContext *avctx)
60    | {
61    |  if (avctx->framerate.num > 0 && avctx->framerate.den > 0)
62    |  return avctx->framerate;
63    |  
64    |  return av_inv_q(avctx->time_base);
65    | }
66    |  
67    | static double get_fps(AVCodecContext *avctx)
68    | {
69    |  return av_q2d(get_fpsQ(avctx));
70    | }
71    |  
72    | static inline double qp2bits(const RateControlEntry *rce, double qp)
73    | {
74    |  if (qp <= 0.0) {
75    |         av_log(NULL, AV_LOG_ERROR, "qp<=0.0\n");
76    |     }
77    |  return rce->qscale * (double)(rce->i_tex_bits + rce->p_tex_bits + 1) / qp;
78    | }
79    |  
80    | static double qp2bits_cb(void *rce, double qp)
81    | {
82    |  return qp2bits(rce, qp);
83    | }
84    |  
85    | static inline double bits2qp(const RateControlEntry *rce, double bits)
86    | {
87    |  if (bits < 0.9) {
88    |         av_log(NULL, AV_LOG_ERROR, "bits<0.9\n");
89    |     }
90    |  return rce->qscale * (double)(rce->i_tex_bits + rce->p_tex_bits + 1) / bits;
91    | }
92    |  
93    | static double bits2qp_cb(void *rce, double qp)
94    | {
95    |  return bits2qp(rce, qp);
96    | }
97    |  
98    | static double get_diff_limited_q(MPVMainEncContext *m, const RateControlEntry *rce, double q)
99    | {
100   |     MPVEncContext      *const   s = &m->s;
101   |     RateControlContext *const rcc = &m->rc_context;
102   |     AVCodecContext     *const   a = s->c.avctx;
103   |  const int pict_type       = rce->new_pict_type;
104   |  const double last_p_q     = rcc->last_qscale_for[AV_PICTURE_TYPE_P];
105   |  const double last_non_b_q = rcc->last_qscale_for[rcc->last_non_b_pict_type];
106   |  
107   |  if (pict_type == AV_PICTURE_TYPE_I &&
108   |         (a->i_quant_factor > 0.0 || rcc->last_non_b_pict_type == AV_PICTURE_TYPE_P))
109   |         q = last_p_q * FFABS(a->i_quant_factor) + a->i_quant_offset;
110   |  else if (pict_type == AV_PICTURE_TYPE_B &&
111   |              a->b_quant_factor > 0.0)
112   |         q = last_non_b_q * a->b_quant_factor + a->b_quant_offset;
113   |  if (q < 1)
114   |         q = 1;
115   |  
116   |  /* last qscale / qdiff stuff */
117   |  if (rcc->last_non_b_pict_type == pict_type || pict_type != AV_PICTURE_TYPE_I) {
118   |  double last_q     = rcc->last_qscale_for[pict_type];
119   |  const int maxdiff = FF_QP2LAMBDA * a->max_qdiff;
120   |  
121   |  if (q > last_q + maxdiff)
122   |             q = last_q + maxdiff;
123   |  else if (q < last_q - maxdiff)
124   |             q = last_q - maxdiff;
125   |     }
126   |  
127   |     rcc->last_qscale_for[pict_type] = q; // Note we cannot do that after blurring
128   |  
129   |  if (pict_type != AV_PICTURE_TYPE_B)
130   |         rcc->last_non_b_pict_type = pict_type;
131   |  
132   |  return q;
133   | }
134   |  
135   | /**
136   |  * Get the qmin & qmax for pict_type.
137   |  */
138   | static void get_qminmax(int *qmin_ret, int *qmax_ret, MPVMainEncContext *const m, int pict_type)
139   | {
140   |     MPVEncContext *const s = &m->s;
141   |  int qmin = m->lmin;
142   |  int qmax = m->lmax;
143   |  
144   |  av_assert0(qmin <= qmax);
145   |  
146   |  switch (pict_type) {
147   |  case AV_PICTURE_TYPE_B:
148   |         qmin = (int)(qmin * FFABS(s->c.avctx->b_quant_factor) + s->c.avctx->b_quant_offset + 0.5);
149   |         qmax = (int)(qmax * FFABS(s->c.avctx->b_quant_factor) + s->c.avctx->b_quant_offset + 0.5);
150   |  break;
151   |  case AV_PICTURE_TYPE_I:
152   |         qmin = (int)(qmin * FFABS(s->c.avctx->i_quant_factor) + s->c.avctx->i_quant_offset + 0.5);
153   |         qmax = (int)(qmax * FFABS(s->c.avctx->i_quant_factor) + s->c.avctx->i_quant_offset + 0.5);
154   |  break;
155   |     }
156   |  
157   |     qmin = av_clip(qmin, 1, FF_LAMBDA_MAX);
158   |     qmax = av_clip(qmax, 1, FF_LAMBDA_MAX);
159   |  
160   |  if (qmax < qmin)
161   |         qmax = qmin;
162   |  
163   |     *qmin_ret = qmin;
164   |     *qmax_ret = qmax;
165   | }
166   |  
167   | static double modify_qscale(MPVMainEncContext *const m, const RateControlEntry *rce,
168   |  double q, int frame_num)
169   | {
170   |     MPVEncContext      *const   s = &m->s;
171   |     RateControlContext *const rcc = &m->rc_context;
172   |  const double buffer_size = s->c.avctx->rc_buffer_size;
173   |  const double fps         = get_fps(s->c.avctx);
174   |  const double min_rate    = s->c.avctx->rc_min_rate / fps;
175   |  const double max_rate    = s->c.avctx->rc_max_rate / fps;
176   |  const int pict_type      = rce->new_pict_type;
177   |  int qmin, qmax;
178   |  
179   |     get_qminmax(&qmin, &qmax, m, pict_type);
180   |  
181   |  /* modulation */
182   |  if (rcc->qmod_freq &&
183   |         frame_num % rcc->qmod_freq == 0 &&
184   |         pict_type == AV_PICTURE_TYPE_P)
185   |         q *= rcc->qmod_amp;
186   |  
187   |  /* buffer overflow/underflow protection */
188   |  if (buffer_size) {
189   |  double expected_size = rcc->buffer_index;
190   |  double q_limit;
191   |  
192   |  if (min_rate) {
193   |  double d = 2 * (buffer_size - expected_size) / buffer_size;
194   |  if (d > 1.0)
195   |                 d = 1.0;
196   |  else if (d < 0.0001)
197   |                 d = 0.0001;
198   |             q *= pow(d, 1.0 / rcc->buffer_aggressivity);
199   |  
200   |             q_limit = bits2qp(rce,
201   |  FFMAX((min_rate - buffer_size + rcc->buffer_index) *
202   |  s->c.avctx->rc_min_vbv_overflow_use, 1));
203   |  
204   |  if (q > q_limit) {
205   |  if (s->c.avctx->debug & FF_DEBUG_RC)
206   |                     av_log(s->c.avctx, AV_LOG_DEBUG,
207   |  "limiting QP %f -> %f\n", q, q_limit);
208   |                 q = q_limit;
209   |             }
210   |         }
211   |  
212   |  if (max_rate) {
213   |  double d = 2 * expected_size / buffer_size;
214   |  if (d > 1.0)
215   |                 d = 1.0;
216   |  else if (d < 0.0001)
217   |                 d = 0.0001;
218   |             q /= pow(d, 1.0 / rcc->buffer_aggressivity);
219   |  
220   |             q_limit = bits2qp(rce,
221   |  FFMAX(rcc->buffer_index *
222   |  s->c.avctx->rc_max_available_vbv_use,
223   |  1));
224   |  if (q < q_limit) {
225   |  if (s->c.avctx->debug & FF_DEBUG_RC)
226   |                     av_log(s->c.avctx, AV_LOG_DEBUG,
227   |  "limiting QP %f -> %f\n", q, q_limit);
228   |                 q = q_limit;
229   |             }
230   |         }
231   |     }
232   |  ff_dlog(s->c.avctx, "q:%f max:%f min:%f size:%f index:%f agr:%f\n",
233   |  q, max_rate, min_rate, buffer_size, rcc->buffer_index,
234   |  rcc->buffer_aggressivity);
235   |  if (rcc->qsquish == 0.0 || qmin == qmax) {
236   |  if (q < qmin)
237   |             q = qmin;
238   |  else if (q > qmax)
239   |             q = qmax;
240   |     } else {
241   |  double min2 = log(qmin);
242   |  double max2 = log(qmax);
243   |  
244   |         q  = log(q);
245   |         q  = (q - min2) / (max2 - min2) - 0.5;
246   |         q *= -4.0;
247   |         q  = 1.0 / (1.0 + exp(q));
248   |         q  = q * (max2 - min2) + min2;
249   |  
250   |         q = exp(q);
251   |     }
252   |  
253   |  return q;
254   | }
255   |  
256   | /**
257   |  * Modify the bitrate curve from pass1 for one frame.
258   |  */
259   | static double get_qscale(MPVMainEncContext *const m, RateControlEntry *rce,
260   |  double rate_factor, int frame_num)
261   | {
262   |     MPVEncContext  *const s = &m->s;
263   |     RateControlContext *rcc = &m->rc_context;
264   |     AVCodecContext *const avctx = s->c.avctx;
265   |  const int pict_type     = rce->new_pict_type;
266   |  const double mb_num     = s->c.mb_num;
267   |  double q, bits;
268   |  int i;
269   |  
270   |  double const_values[] = {
271   |  M_PI,
272   |  M_E,
273   |         rce->i_tex_bits * rce->qscale,
274   |         rce->p_tex_bits * rce->qscale,
275   |         (rce->i_tex_bits + rce->p_tex_bits) * (double)rce->qscale,
276   |         rce->mv_bits / mb_num,
277   |         rce->pict_type == AV_PICTURE_TYPE_B ? (rce->f_code + rce->b_code) * 0.5 : rce->f_code,
278   |         rce->i_count / mb_num,
279   |         rce->mc_mb_var_sum / mb_num,
280   |         rce->mb_var_sum / mb_num,
281   |         rce->pict_type == AV_PICTURE_TYPE_I,
282   |         rce->pict_type == AV_PICTURE_TYPE_P,
283   |         rce->pict_type == AV_PICTURE_TYPE_B,
284   |         rcc->qscale_sum[pict_type] / (double)rcc->frame_count[pict_type],
285   |         avctx->qcompress,
286   |         rcc->i_cplx_sum[AV_PICTURE_TYPE_I] / (double)rcc->frame_count[AV_PICTURE_TYPE_I],
287   |         rcc->i_cplx_sum[AV_PICTURE_TYPE_P] / (double)rcc->frame_count[AV_PICTURE_TYPE_P],
288   |         rcc->p_cplx_sum[AV_PICTURE_TYPE_P] / (double)rcc->frame_count[AV_PICTURE_TYPE_P],
289   |         rcc->p_cplx_sum[AV_PICTURE_TYPE_B] / (double)rcc->frame_count[AV_PICTURE_TYPE_B],
290   |         (rcc->i_cplx_sum[pict_type] + rcc->p_cplx_sum[pict_type]) / (double)rcc->frame_count[pict_type],
291   |         0
292   |     };
293   |  
294   |     bits = av_expr_eval(rcc->rc_eq_eval, const_values, rce);
295   |  if (isnan(bits)) {
296   |         av_log(avctx, AV_LOG_ERROR, "Error evaluating rc_eq \"%s\"\n", rcc->rc_eq);
297   |  return -1;
298   |     }
299   |  
300   |     rcc->pass1_rc_eq_output_sum += bits;
301   |     bits *= rate_factor;
302   |  if (bits < 0.0)
303   |         bits = 0.0;
304   |     bits += 1.0; // avoid 1/0 issues
305   |  
306   |  /* user override */
307   |  for (i = 0; i < avctx->rc_override_count; i++) {
308   |         RcOverride *rco = avctx->rc_override;
309   |  if (rco[i].start_frame > frame_num)
310   |  continue;
311   |  if (rco[i].end_frame < frame_num)
312   |  continue;
313   |  
314   |  if (rco[i].qscale)
315   |             bits = qp2bits(rce, rco[i].qscale);  // FIXME move at end to really force it?
316   |  else
317   |             bits *= rco[i].quality_factor;
318   |     }
319   |  
320   |     q = bits2qp(rce, bits);
321   |  
322   |  /* I/B difference */
323   |  if (pict_type == AV_PICTURE_TYPE_I && avctx->i_quant_factor < 0.0)
324   |         q = -q * avctx->i_quant_factor + avctx->i_quant_offset;
325   |  else if (pict_type == AV_PICTURE_TYPE_B && avctx->b_quant_factor < 0.0)
326   |         q = -q * avctx->b_quant_factor + avctx->b_quant_offset;
327   |  if (q < 1)
328   |         q = 1;
329   |  
330   |  return q;
331   | }
332   |  
333   | static int init_pass2(MPVMainEncContext *const m)
334   | {
335   |     RateControlContext *const rcc = &m->rc_context;
336   |     MPVEncContext      *const   s = &m->s;
337   |     AVCodecContext     *const avctx = s->c.avctx;
338   |  int i, toobig;
339   |     AVRational fps         = get_fpsQ(avctx);
340   |  double complexity[5]   = { 0 }; // approximate bits at quant=1
341   |     uint64_t const_bits[5] = { 0 }; // quantizer independent bits
342   |     uint64_t all_const_bits;
343   |     uint64_t all_available_bits = av_rescale_q(m->bit_rate,
344   |                                                (AVRational){rcc->num_entries,1},
345   |                                                fps);
346   |  double rate_factor          = 0;
347   |  double step;
348   |  const int filter_size = (int)(avctx->qblur * 4) | 1;
349   |  double expected_bits = 0; // init to silence gcc warning
350   |  double *qscale, *blurred_qscale, qscale_sum;
351   |  
352   |  /* find complexity & const_bits & decide the pict_types */
353   |  for (i = 0; i < rcc->num_entries; i++) {
354   |         RateControlEntry *rce = &rcc->entry[i];
355   |  
356   |         rce->new_pict_type                = rce->pict_type;
357   |         rcc->i_cplx_sum[rce->pict_type]  += rce->i_tex_bits * rce->qscale;
358   |         rcc->p_cplx_sum[rce->pict_type]  += rce->p_tex_bits * rce->qscale;
359   |         rcc->mv_bits_sum[rce->pict_type] += rce->mv_bits;
360   |         rcc->frame_count[rce->pict_type]++;
712   |     av_expr_free(rcc->rc_eq_eval);
713   |     rcc->rc_eq_eval = NULL;
714   |     av_freep(&rcc->entry);
715   |     av_freep(&rcc->cplx_tab);
716   | }
717   |  
718   | int ff_vbv_update(MPVMainEncContext *m, int frame_size)
719   | {
720   |     MPVEncContext      *const   s = &m->s;
721   |     RateControlContext *const rcc = &m->rc_context;
722   |     AVCodecContext   *const avctx = s->c.avctx;
723   |  const double fps      = get_fps(avctx);
724   |  const int buffer_size = avctx->rc_buffer_size;
725   |  const double min_rate = avctx->rc_min_rate / fps;
726   |  const double max_rate = avctx->rc_max_rate / fps;
727   |  
728   |  ff_dlog(avctx, "%d %f %d %f %f\n",
729   |  buffer_size, rcc->buffer_index, frame_size, min_rate, max_rate);
730   |  
731   |  if (buffer_size) {
732   |  int left;
733   |  
734   |         rcc->buffer_index -= frame_size;
735   |  if (rcc->buffer_index < 0) {
736   |             av_log(avctx, AV_LOG_ERROR, "rc buffer underflow\n");
737   |  if (frame_size > max_rate && s->c.qscale == avctx->qmax) {
738   |                 av_log(avctx, AV_LOG_ERROR, "max bitrate possibly too small or try trellis with large lmax or increase qmax\n");
739   |             }
740   |             rcc->buffer_index = 0;
741   |         }
742   |  
743   |         left = buffer_size - rcc->buffer_index - 1;
744   |         rcc->buffer_index += av_clip(left, min_rate, max_rate);
745   |  
746   |  if (rcc->buffer_index > buffer_size) {
747   |  int stuffing = ceil((rcc->buffer_index - buffer_size) / 8);
748   |  
749   |  if (stuffing < 4 && s->c.codec_id == AV_CODEC_ID_MPEG4)
750   |                 stuffing = 4;
751   |             rcc->buffer_index -= 8 * stuffing;
752   |  
753   |  if (avctx->debug & FF_DEBUG_RC)
754   |                 av_log(avctx, AV_LOG_DEBUG, "stuffing %d bytes\n", stuffing);
755   |  
756   |  return stuffing;
757   |         }
758   |     }
759   |  return 0;
760   | }
761   |  
762   | static double predict_size(Predictor *p, double q, double var)
763   | {
764   |  return p->coeff * var / (q * p->count);
765   | }
766   |  
767   | static void update_predictor(Predictor *p, double q, double var, double size)
768   | {
769   |  double new_coeff = size * q / (var + 1);
770   |  if (var < 10)
771   |  return;
772   |  
773   |     p->count *= p->decay;
774   |     p->coeff *= p->decay;
775   |     p->count++;
776   |     p->coeff += new_coeff;
777   | }
778   |  
779   | static void adaptive_quantization(RateControlContext *const rcc,
780   |                                   MPVMainEncContext *const m, double q)
781   | {
782   |  MPVEncContext *const s = &m->s;
783   |  const float lumi_masking         = s->c.avctx->lumi_masking / (128.0 * 128.0);
784   |  const float dark_masking         = s->c.avctx->dark_masking / (128.0 * 128.0);
785   |  const float temp_cplx_masking    = s->c.avctx->temporal_cplx_masking;
786   |  const float spatial_cplx_masking = s->c.avctx->spatial_cplx_masking;
787   |  const float p_masking            = s->c.avctx->p_masking;
788   |  const float border_masking       = m->border_masking;
789   |  float bits_sum                   = 0.0;
790   |  float cplx_sum                   = 0.0;
791   |  float *cplx_tab                  = rcc->cplx_tab;
792   |  float *bits_tab                  = rcc->bits_tab;
793   |  const int qmin                   = s->c.avctx->mb_lmin;
794   |  const int qmax                   = s->c.avctx->mb_lmax;
795   |  const int mb_width               = s->c.mb_width;
796   |  const int mb_height              = s->c.mb_height;
797   |  
798   |  for (int i = 0; i < s->c.mb_num; i++) {
    41←Assuming 'i' is < field 'mb_num'→
    42←Loop condition is true.  Entering loop body→
    61←Assuming 'i' is >= field 'mb_num'→
    62←Loop condition is false. Execution continues on line 857→
799   |  const int mb_xy = s->c.mb_index2xy[i];
800   |  float temp_cplx = sqrt(s->mc_mb_var[mb_xy]); // FIXME merge in pow()
801   |  float spat_cplx = sqrt(s->mb_var[mb_xy]);
802   |  const int lumi  = s->mb_mean[mb_xy];
803   |  float bits, cplx, factor;
804   |  int mb_x = mb_xy % s->c.mb_stride;
805   |  int mb_y = mb_xy / s->c.mb_stride;
806   |  int mb_distance;
807   |  float mb_factor = 0.0;
808   |  if (spat_cplx < 4)
    43←Assuming 'spat_cplx' is >= 4→
    44←Taking false branch→
809   |             spat_cplx = 4;              // FIXME fine-tune
810   |  if (temp_cplx < 4)
    45←Assuming 'temp_cplx' is >= 4→
    46←Taking false branch→
811   |             temp_cplx = 4;              // FIXME fine-tune
812   |  
813   |  if ((s->mb_type[mb_xy] & CANDIDATE_MB_TYPE_INTRA)) { // FIXME hq mode
    47←Assuming the condition is true→
    48←Taking true branch→
814   |  cplx   = spat_cplx;
815   |  factor = 1.0 + p_masking;
816   |         } else {
817   |             cplx   = temp_cplx;
818   |             factor = pow(temp_cplx, -temp_cplx_masking);
819   |         }
820   |  factor *= pow(spat_cplx, -spatial_cplx_masking);
821   |  
822   |  if (lumi > 127)
    49←Assuming 'lumi' is <= 127→
    50←Taking false branch→
823   |             factor *= (1.0 - (lumi - 128) * (lumi - 128) * lumi_masking);
824   |  else
825   |  factor *= (1.0 - (lumi - 128) * (lumi - 128) * dark_masking);
826   |  
827   |  if (mb_x < mb_width / 5) {
    51←Assuming the condition is false→
    52←Taking false branch→
828   |             mb_distance = mb_width / 5 - mb_x;
829   |             mb_factor   = (float)mb_distance / (float)(mb_width / 5);
830   |         } else if (mb_x > 4 * mb_width / 5) {
    53←Assuming the condition is false→
    54←Taking false branch→
831   |             mb_distance = mb_x - 4 * mb_width / 5;
832   |             mb_factor   = (float)mb_distance / (float)(mb_width / 5);
833   |         }
834   |  if (mb_y < mb_height / 5) {
    55←Assuming the condition is false→
    56←Taking false branch→
835   |             mb_distance = mb_height / 5 - mb_y;
836   |             mb_factor   = FFMAX(mb_factor,
837   |  (float)mb_distance / (float)(mb_height / 5));
838   |         } else if (mb_y > 4 * mb_height / 5) {
    57←Assuming the condition is false→
    58←Taking false branch→
839   |             mb_distance = mb_y - 4 * mb_height / 5;
840   |             mb_factor   = FFMAX(mb_factor,
841   |  (float)mb_distance / (float)(mb_height / 5));
842   |         }
843   |  
844   |  factor *= 1.0 - border_masking * mb_factor;
845   |  
846   |  if (factor < 0.00001)
    59←Assuming the condition is false→
    60←Taking false branch→
847   |             factor = 0.00001;
848   |  
849   |  bits        = cplx * factor;
850   |         cplx_sum   += cplx;
851   |         bits_sum   += bits;
852   |         cplx_tab[i] = cplx;
853   |  bits_tab[i] = bits;
854   |  }
855   |  
856   |  /* handle qmin/qmax clipping */
857   |  if (s->mpv_flags & FF_MPV_FLAG_NAQ) {
    63←Assuming the condition is true→
    64←Taking true branch→
858   |  float factor = bits_sum / cplx_sum;
859   |  for (int i = 0; i < s->c.mb_num; i++) {
    65←Loop condition is true.  Entering loop body→
    70←Loop condition is false. Execution continues on line 871→
860   |  float newq = q * cplx_tab[i] / bits_tab[i];
861   |             newq *= factor;
862   |  
863   |  if (newq > qmax) {
    66←Assuming 'newq' is <= 'qmax'→
    67←Taking false branch→
864   |                 bits_sum -= bits_tab[i];
865   |                 cplx_sum -= cplx_tab[i] * q / qmax;
866   |             } else if (newq < qmin) {
    68←Assuming 'newq' is >= 'qmin'→
    69←Taking false branch→
867   |                 bits_sum -= bits_tab[i];
868   |                 cplx_sum -= cplx_tab[i] * q / qmin;
869   |             }
870   |  }
871   |  if (bits_sum < 0.001)
    71←Assuming the condition is false→
    72←Taking false branch→
872   |             bits_sum = 0.001;
873   |  if (cplx_sum < 0.001)
    73←Assuming the condition is false→
    74←Taking false branch→
874   |             cplx_sum = 0.001;
875   |     }
876   |  
877   |  for (int i = 0; i < s->c.mb_num; i++) {
    75←Loop condition is true.  Entering loop body→
878   |  const int mb_xy = s->c.mb_index2xy[i];
879   |  float newq      = q * cplx_tab[i] / bits_tab[i];
880   |  int intq;
881   |  
882   |  if (s->mpv_flags & FF_MPV_FLAG_NAQ) {
    76←Taking true branch→
883   |  newq *= bits_sum / cplx_sum;
    77←division by possibly zero aggregate factor
884   |         }
885   |  
886   |         intq = (int)(newq + 0.5);
887   |  
888   |  if (intq > qmax)
889   |             intq = qmax;
890   |  else if (intq < qmin)
891   |             intq = qmin;
892   |         s->lambda_table[mb_xy] = intq;
893   |     }
894   | }
895   |  
896   | void ff_get_2pass_fcode(MPVMainEncContext *const m)
897   | {
898   |     MPVEncContext *const s = &m->s;
899   |  const RateControlContext *rcc = &m->rc_context;
900   |  const RateControlEntry   *rce = &rcc->entry[s->picture_number];
901   |  
902   |     s->f_code = rce->f_code;
903   |     s->b_code = rce->b_code;
904   | }
905   |  
906   | // FIXME rd or at least approx for dquant
907   |  
908   | float ff_rate_estimate_qscale(MPVMainEncContext *const m, int dry_run)
909   | {
910   |  MPVEncContext  *const s = &m->s;
911   |     RateControlContext *rcc = &m->rc_context;
912   |     AVCodecContext *const a = s->c.avctx;
913   |  float q;
914   |  int qmin, qmax;
915   |  float br_compensation;
916   |  double diff;
917   |  double short_term_q;
918   |  double fps;
919   |  int picture_number = s->picture_number;
920   |     int64_t wanted_bits;
921   |     RateControlEntry local_rce, *rce;
922   |  double bits;
923   |  double rate_factor;
924   |     int64_t var;
925   |  const int pict_type = s->c.pict_type;
926   |  
927   |     get_qminmax(&qmin, &qmax, m, pict_type);
928   |  
929   |     fps = get_fps(s->c.avctx);
930   |  /* update predictors */
931   |  if (picture_number > 2 && !dry_run) {
    1Assuming 'picture_number' is <= 2→
932   |  const int64_t last_var =
933   |             m->last_pict_type == AV_PICTURE_TYPE_I ? rcc->last_mb_var_sum
934   |                                                    : rcc->last_mc_mb_var_sum;
935   |  av_assert1(m->frame_bits >= m->stuffing_bits);
936   |         update_predictor(&rcc->pred[m->last_pict_type],
937   |                          rcc->last_qscale,
938   |                          sqrt(last_var),
939   |                          m->frame_bits - m->stuffing_bits);
940   |     }
941   |  
942   |  if (s->c.avctx->flags & AV_CODEC_FLAG_PASS2) {
    2←Assuming the condition is false→
    3←Taking false branch→
943   |  av_assert0(picture_number >= 0);
944   |  if (picture_number >= rcc->num_entries) {
945   |             av_log(s->c.avctx, AV_LOG_ERROR, "Input is longer than 2-pass log file\n");
946   |  return -1;
947   |         }
948   |         rce         = &rcc->entry[picture_number];
949   |         wanted_bits = rce->expected_bits;
950   |     } else {
951   |  const MPVPicture *dts_pic;
952   |  double wanted_bits_double;
953   |  rce = &local_rce;
954   |  
955   |  /* FIXME add a dts field to AVFrame and ensure it is set and use it
956   |  * here instead of reordering but the reordering is simpler for now
957   |  * until H.264 B-pyramid must be handled. */
958   |  if (s->c.pict_type3.1Field 'pict_type' is not equal to AV_PICTURE_TYPE_B == AV_PICTURE_TYPE_B || s->c.low_delay)
    4←Assuming field 'low_delay' is 0→
    5←Taking false branch→
959   |             dts_pic = s->c.cur_pic.ptr;
960   |  else
961   |  dts_pic = s->c.last_pic.ptr;
962   |  
963   |  if (!dts_pic || dts_pic->f->pts == AV_NOPTS_VALUE)
    6←Assuming 'dts_pic' is non-null→
    7←Assuming field 'pts' is not equal to AV_NOPTS_VALUE→
    8←Taking false branch→
964   |             wanted_bits_double = m->bit_rate * (double)picture_number / fps;
965   |  else
966   |  wanted_bits_double = m->bit_rate * (double)dts_pic->f->pts / fps;
967   |  if (wanted_bits_double > INT64_MAX) {
    9←Assuming 'wanted_bits_double' is <= INT64_MAX→
    10←Taking false branch→
968   |             av_log(s->c.avctx, AV_LOG_WARNING, "Bits exceed 64bit range\n");
969   |             wanted_bits = INT64_MAX;
970   |         } else
971   |  wanted_bits = (int64_t)wanted_bits_double;
972   |     }
973   |  
974   |  diff = m->total_bits - wanted_bits;
975   |     br_compensation = (a->bit_rate_tolerance - diff) / a->bit_rate_tolerance;
976   |  if (br_compensation <= 0.0)
    11←Assuming the condition is false→
977   |         br_compensation = 0.001;
978   |  
979   |  var = pict_type12.1'pict_type' is not equal to AV_PICTURE_TYPE_I == AV_PICTURE_TYPE_I ? m->mb_var_sum : m->mc_mb_var_sum;
    12←Taking false branch→
    13←'?' condition is false→
980   |  
981   |  short_term_q = 0; /* avoid warning */
982   |  if (s->c.avctx->flags & AV_CODEC_FLAG_PASS2) {
    14←Taking false branch→
983   |  if (pict_type != AV_PICTURE_TYPE_I)
984   |  av_assert0(pict_type == rce->new_pict_type);
985   |  
986   |         q = rce->new_qscale / br_compensation;
987   |  ff_dlog(s->c.avctx, "%f %f %f last:%d var:%"PRId64" type:%d//\n", q, rce->new_qscale,
988   |  br_compensation, m->frame_bits, var, pict_type);
989   |     } else {
990   |  rce->pict_type     =
991   |         rce->new_pict_type = pict_type;
992   |  rce->mc_mb_var_sum = m->mc_mb_var_sum;
993   |         rce->mb_var_sum    = m->mb_var_sum;
994   |         rce->qscale        = FF_QP2LAMBDA * 2;
995   |         rce->f_code        = s->f_code;
996   |         rce->b_code        = s->b_code;
997   |         rce->misc_bits     = 1;
998   |  
999   |         bits = predict_size(&rcc->pred[pict_type], rce->qscale, sqrt(var));
1000  |  if (pict_type14.1'pict_type' is not equal to AV_PICTURE_TYPE_I == AV_PICTURE_TYPE_I) {
    15←Taking false branch→
1001  |             rce->i_count    = s->c.mb_num;
1002  |             rce->i_tex_bits = bits;
1003  |             rce->p_tex_bits = 0;
1004  |             rce->mv_bits    = 0;
1005  |         } else {
1006  |  rce->i_count    = 0;    // FIXME we do know this approx
1007  |             rce->i_tex_bits = 0;
1008  |             rce->p_tex_bits = bits * 0.9;
1009  |  rce->mv_bits    = bits * 0.1;
1010  |         }
1011  |  rcc->i_cplx_sum[pict_type]  += rce->i_tex_bits * rce->qscale;
1012  |         rcc->p_cplx_sum[pict_type]  += rce->p_tex_bits * rce->qscale;
1013  |         rcc->mv_bits_sum[pict_type] += rce->mv_bits;
1014  |         rcc->frame_count[pict_type]++;
1015  |  
1016  |         rate_factor = rcc->pass1_wanted_bits /
1017  |                       rcc->pass1_rc_eq_output_sum * br_compensation;
1018  |  
1019  |         q = get_qscale(m, rce, rate_factor, picture_number);
1020  |  if (q < 0)
    16←Assuming 'q' is >= 0→
    17←Taking false branch→
1021  |  return -1;
1022  |  
1023  |  av_assert0(q > 0.0);
    18←Assuming the condition is false→
    19←Taking false branch→
    20←Loop condition is false.  Exiting loop→
1024  |  q = get_diff_limited_q(m, rce, q);
1025  |  av_assert0(q > 0.0);
    21←Assuming the condition is false→
    22←Taking false branch→
1026  |  
1027  |  // FIXME type dependent blur like in 2-pass
1028  |  if (pict_type23.1'pict_type' is not equal to AV_PICTURE_TYPE_P == AV_PICTURE_TYPE_P || m->intra_only) {
    23←Loop condition is false.  Exiting loop→
    24←Assuming field 'intra_only' is 0→
    25←Taking false branch→
1029  |             rcc->short_term_qsum   *= a->qblur;
1030  |             rcc->short_term_qcount *= a->qblur;
1031  |  
1032  |             rcc->short_term_qsum += q;
1033  |             rcc->short_term_qcount++;
1034  |             q = short_term_q = rcc->short_term_qsum / rcc->short_term_qcount;
1035  |         }
1036  |  av_assert0(q > 0.0);
    26←Assuming the condition is false→
    27←Taking false branch→
    28←Loop condition is false.  Exiting loop→
1037  |  
1038  |  q = modify_qscale(m, rce, q, picture_number);
1039  |  
1040  |  rcc->pass1_wanted_bits += m->bit_rate / fps;
1041  |  
1042  |  av_assert0(q > 0.0);
    29←Assuming the condition is false→
    30←Taking false branch→
    31←Loop condition is false.  Exiting loop→
1043  |     }
1044  |  
1045  |  if (s->c.avctx->debug & FF_DEBUG_RC) {
    32←Assuming the condition is false→
    33←Taking false branch→
1046  |         av_log(s->c.avctx, AV_LOG_DEBUG,
1047  |  "%c qp:%d<%2.1f<%d %d want:%"PRId64" total:%"PRId64" comp:%f st_q:%2.2f "
1048  |  "size:%d var:%"PRId64"/%"PRId64" br:%"PRId64" fps:%d\n",
1049  |                av_get_picture_type_char(pict_type),
1050  |                qmin, q, qmax, picture_number,
1051  |                wanted_bits / 1000, m->total_bits / 1000,
1052  |                br_compensation, short_term_q, m->frame_bits,
1053  |                m->mb_var_sum, m->mc_mb_var_sum,
1054  |                m->bit_rate / 1000, (int)fps);
1055  |     }
1056  |  
1057  |  if (q < qmin)
    34←Assuming 'q' is >= 'qmin'→
    35←Taking false branch→
1058  |         q = qmin;
1059  |  else if (q > qmax)
    36←Assuming 'q' is <= 'qmax'→
    37←Taking false branch→
1060  |         q = qmax;
1061  |  
1062  |  if (s->adaptive_quant)
    38←Assuming field 'adaptive_quant' is not equal to 0→
    39←Taking true branch→
1063  |  adaptive_quantization(rcc, m, q);
    40←Calling 'adaptive_quantization'→
1064  |  else
1065  |         q = (int)(q + 0.5);
1066  |  
1067  |  if (!dry_run) {
1068  |         rcc->last_qscale        = q;
1069  |         rcc->last_mc_mb_var_sum = m->mc_mb_var_sum;
1070  |         rcc->last_mb_var_sum    = m->mb_var_sum;
1071  |     }
1072  |  return q;
1073  | }

# Formatting

Please provide your answer in the following format:

- Decision: {Bug/NotABug}
- Reason: {Your reason here}
