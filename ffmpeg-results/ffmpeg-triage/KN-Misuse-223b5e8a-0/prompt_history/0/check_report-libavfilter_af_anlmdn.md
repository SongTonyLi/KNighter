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

File:| avfilter/af_anlmdn.c  
---|---  
Warning:| line 259, column 41  
division by possibly zero aggregate factor  
  
### Annotated Source Code


148   |         }
149   |     }
150   |  if (!s->cache)
151   |  return AVERROR(ENOMEM);
152   |  
153   |  if (!s->window || s->window->nb_samples < newN) {
154   |         AVFrame *new_window = ff_get_audio_buffer(outlink, newN);
155   |  if (new_window) {
156   |  if (s->window)
157   |                 av_samples_copy(new_window->extended_data, s->window->extended_data, 0, 0,
158   |                                 s->window->nb_samples, new_window->ch_layout.nb_channels, new_window->format);
159   |             av_frame_free(&s->window);
160   |             s->window = new_window;
161   |         } else {
162   |  return AVERROR(ENOMEM);
163   |         }
164   |     }
165   |  if (!s->window)
166   |  return AVERROR(ENOMEM);
167   |  
168   |     s->pdiff_lut_scale = 1.f / s->m * WEIGHT_LUT_SIZE;
169   |  for (int i = 0; i < WEIGHT_LUT_SIZE; i++) {
170   |  float w = -i / s->pdiff_lut_scale;
171   |  
172   |         s->weight_lut[i] = expf(w);
173   |     }
174   |  
175   |     s->K = newK;
176   |     s->S = newS;
177   |     s->H = newH;
178   |     s->N = newN;
179   |  
180   |  return 0;
181   | }
182   |  
183   | static int config_output(AVFilterLink *outlink)
184   | {
185   |     AVFilterContext *ctx = outlink->src;
186   |     AudioNLMeansContext *s = ctx->priv;
187   |  int ret;
188   |  
189   |     ret = config_filter(ctx);
190   |  if (ret < 0)
191   |  return ret;
192   |  
193   |     ff_anlmdn_init(&s->dsp);
194   |  
195   |  return 0;
196   | }
197   |  
198   | static int filter_channel(AVFilterContext *ctx, void *arg, int ch, int nb_jobs)
199   | {
200   |  AudioNLMeansContext *s = ctx->priv;
201   |     AVFrame *out = arg;
202   |  const int S = s->S;
203   |  const int K = s->K;
204   |  const int N = s->N;
205   |  const int H = s->H;
206   |  const int om = s->om;
207   |  const float *f = (const float *)(s->window->extended_data[ch]) + K;
208   |  float *cache = (float *)s->cache->extended_data[ch];
209   |  const float sw = (65536.f / (4 * K + 2)) / sqrtf(s->a);
210   |  float *dst = (float *)out->extended_data[ch];
211   |  const float *const weight_lut = s->weight_lut;
212   |  const float pdiff_lut_scale = s->pdiff_lut_scale;
213   |  const float smooth = fminf(s->m, WEIGHT_LUT_SIZE / pdiff_lut_scale);
214   |  const int offset = N - H;
215   |  float *src = (float *)s->window->extended_data[ch];
216   |  const AVFrame *const in = s->in;
217   |  
218   |     memmove(src, &src[H], offset * sizeof(float));
219   |     memcpy(&src[offset], in->extended_data[ch], in->nb_samples * sizeof(float));
220   |     memset(&src[offset + in->nb_samples], 0, (H - in->nb_samples) * sizeof(float));
221   |  
222   |  for (int i = S; i < H + S; i++) {
    1Assuming the condition is true→
    2←Loop condition is true.  Entering loop body→
223   |  float P = 0.f, Q = 0.f;
224   |  int v = 0;
225   |  
226   |  if (i2.1'i' is equal to 'S' == S) {
    3←Taking true branch→
227   |  for (int j = i - S; j <= i + S; j++) {
    4←Assuming the condition is false→
    5←Loop condition is false. Execution continues on line 237→
228   |  if (i == j)
229   |  continue;
230   |                 cache[v++] = s->dsp.compute_distance_ssd(f + i, f + j, K);
231   |             }
232   |         } else {
233   |             s->dsp.compute_cache(cache, f, S, K, i, i - S);
234   |             s->dsp.compute_cache(cache + S, f, S, K, i, i + 1);
235   |         }
236   |  
237   |  for (int j = 0; j < 2 * S && !ctx->is_disabled; j++) {
    6←Assuming the condition is false→
238   |  float distance = cache[j];
239   |  unsigned weight_lut_idx;
240   |  float w;
241   |  
242   |  if (distance < 0.f)
243   |                 cache[j] = distance = 0.f;
244   |             w = distance * sw;
245   |  if (w >= smooth)
246   |  continue;
247   |             weight_lut_idx = w * pdiff_lut_scale;
248   |  av_assert2(weight_lut_idx < WEIGHT_LUT_SIZE);
249   |             w = weight_lut[weight_lut_idx];
250   |             P += w * f[i - S + j + (j >= S)];
251   |             Q += w;
252   |         }
253   |  
254   |  P += f[i];
255   |         Q += 1.f;
256   |  
257   |  switch (om) {
    7←Control jumps to 'case OUT_MODE:'  at line 259→
258   |  case IN_MODE:    dst[i - S] = f[i];           break;
259   |  case OUT_MODE:   dst[i - S] = P / Q;          break;
    8←division by possibly zero aggregate factor
260   |  case NOISE_MODE: dst[i - S] = f[i] - (P / Q); break;
261   |         }
262   |     }
263   |  
264   |  return 0;
265   | }
266   |  
267   | static int filter_frame(AVFilterLink *inlink, AVFrame *in)
268   | {
269   |     AVFilterContext *ctx = inlink->dst;
270   |     AVFilterLink *outlink = ctx->outputs[0];
271   |     AudioNLMeansContext *s = ctx->priv;
272   |     AVFrame *out;
273   |  
274   |  if (av_frame_is_writable(in)) {
275   |         out = in;
276   |     } else {
277   |         out = ff_get_audio_buffer(outlink, in->nb_samples);
278   |  if (!out) {
279   |             av_frame_free(&in);
280   |  return AVERROR(ENOMEM);
281   |         }
282   |  
283   |         out->pts = in->pts;
284   |     }
285   |  
286   |     s->in = in;
287   |     ff_filter_execute(ctx, filter_channel, out, NULL, inlink->ch_layout.nb_channels);
288   |  
289   |  if (out != in)

# Formatting

Please provide your answer in the following format:

- Decision: {Bug/NotABug}
- Reason: {Your reason here}
