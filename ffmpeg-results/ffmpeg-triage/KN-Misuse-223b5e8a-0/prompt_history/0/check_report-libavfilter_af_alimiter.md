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

File:| avfilter/af_alimiter.c  
---|---  
Warning:| line 184, column 35  
division by possibly zero aggregate factor  
  
### Annotated Source Code


59    |  int buffer_size;
60    |  int pos;
61    |  int *nextpos;
62    |  double *nextdelta;
63    |  
64    |  int in_trim;
65    |  int out_pad;
66    |     int64_t next_in_pts;
67    |     int64_t next_out_pts;
68    |  int latency;
69    |  
70    |     AVFifo *fifo;
71    |  
72    |  double delta;
73    |  int nextiter;
74    |  int nextlen;
75    |  int asc_changed;
76    | } AudioLimiterContext;
77    |  
78    | #define OFFSET(x) offsetof(AudioLimiterContext, x)
79    | #define AF AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_RUNTIME_PARAM
80    |  
81    | static const AVOption alimiter_options[] = {
82    |     { "level_in",  "set input level",  OFFSET(level_in),     AV_OPT_TYPE_DOUBLE, {.dbl=1},.015625,   64, AF },
83    |     { "level_out", "set output level", OFFSET(level_out),    AV_OPT_TYPE_DOUBLE, {.dbl=1},.015625,   64, AF },
84    |     { "limit",     "set limit",        OFFSET(limit),        AV_OPT_TYPE_DOUBLE, {.dbl=1}, 0.0625,    1, AF },
85    |     { "attack",    "set attack",       OFFSET(attack),       AV_OPT_TYPE_DOUBLE, {.dbl=5},    0.1,   80, AF },
86    |     { "release",   "set release",      OFFSET(release),      AV_OPT_TYPE_DOUBLE, {.dbl=50},     1, 8000, AF },
87    |     { "asc",       "enable asc",       OFFSET(auto_release), AV_OPT_TYPE_BOOL,   {.i64=0},      0,    1, AF },
88    |     { "asc_level", "set asc level",    OFFSET(asc_coeff),    AV_OPT_TYPE_DOUBLE, {.dbl=0.5},    0,    1, AF },
89    |     { "level",     "auto level",       OFFSET(auto_level),   AV_OPT_TYPE_BOOL,   {.i64=1},      0,    1, AF },
90    |     { "latency",   "compensate delay", OFFSET(latency),      AV_OPT_TYPE_BOOL,   {.i64=0},      0,    1, AF },
91    |     { NULL }
92    | };
93    |  
94    | AVFILTER_DEFINE_CLASS(alimiter);
95    |  
96    | static av_cold int init(AVFilterContext *ctx)
97    | {
98    |     AudioLimiterContext *s = ctx->priv;
99    |  
100   |     s->attack   /= 1000.;
101   |     s->release  /= 1000.;
102   |     s->att       = 1.;
103   |     s->asc_pos   = -1;
104   |     s->asc_coeff = pow(0.5, s->asc_coeff - 0.5) * 2 * -1;
105   |  
106   |  return 0;
107   | }
108   |  
109   | static double get_rdelta(AudioLimiterContext *s, double release, int sample_rate,
110   |  double peak, double limit, double patt, int asc)
111   | {
112   |  double rdelta = (1.0 - patt) / (sample_rate * release);
113   |  
114   |  if (asc && s->auto_release && s->asc_c > 0) {
115   |  double a_att = limit / (s->asc_coeff * s->asc) * (double)s->asc_c;
116   |  
117   |  if (a_att > patt) {
118   |  double delta = FFMAX((a_att - patt) / (sample_rate * release), rdelta / 10);
119   |  
120   |  if (delta < rdelta)
121   |                 rdelta = delta;
122   |         }
123   |     }
124   |  
125   |  return rdelta;
126   | }
127   |  
128   | static int filter_frame(AVFilterLink *inlink, AVFrame *in)
129   | {
130   |  AVFilterContext *ctx = inlink->dst;
131   |     AudioLimiterContext *s = ctx->priv;
132   |     AVFilterLink *outlink = ctx->outputs[0];
133   |  const double *src = (const double *)in->data[0];
134   |  const int channels = inlink->ch_layout.nb_channels;
135   |  const int buffer_size = s->buffer_size;
136   |  double *dst, *buffer = s->buffer;
137   |  const double release = s->release;
138   |  const double limit = s->limit;
139   |  double *nextdelta = s->nextdelta;
140   |  double level = s->auto_level ? 1 / limit : 1;
    1Assuming field 'auto_level' is 0→
    2←'?' condition is false→
141   |  const double level_out = s->level_out;
142   |  const double level_in = s->level_in;
143   |  int *nextpos = s->nextpos;
144   |     AVFrame *out;
145   |  double *buf;
146   |  int n, c, i;
147   |  int new_out_samples;
148   |     int64_t out_duration;
149   |     int64_t in_duration;
150   |     int64_t in_pts;
151   |     MetaItem meta;
152   |  
153   |  if (av_frame_is_writable(in)) {
    3←Assuming the condition is true→
    4←Taking true branch→
154   |  out = in;
155   |     } else {
156   |         out = ff_get_audio_buffer(outlink, in->nb_samples);
157   |  if (!out) {
158   |             av_frame_free(&in);
159   |  return AVERROR(ENOMEM);
160   |         }
161   |         av_frame_copy_props(out, in);
162   |     }
163   |  dst = (double *)out->data[0];
164   |  
165   |  for (n = 0; n < in->nb_samples; n++) {
    5←Assuming 'n' is < field 'nb_samples'→
    6←Loop condition is true.  Entering loop body→
166   |  double peak = 0;
167   |  
168   |  for (c = 0; c < channels; c++) {
    7←Assuming 'c' is >= 'channels'→
169   |  double sample = src[c] * level_in;
170   |  
171   |             buffer[s->pos + c] = sample;
172   |             peak = FFMAX(peak, fabs(sample));
173   |         }
174   |  
175   |  if (s->auto_release && peak > limit) {
    8←Assuming field 'auto_release' is 0→
176   |             s->asc += peak;
177   |             s->asc_c++;
178   |         }
179   |  
180   |  if (peak > limit) {
    9←Assuming 'peak' is > 'limit'→
181   |  double patt = FFMIN(limit / peak, 1.);
    10←Taking true branch→
    11←Assuming the condition is true→
    12←'?' condition is true→
182   |  double rdelta = get_rdelta(s, release, inlink->sample_rate,
183   |                                        peak, limit, patt, 0);
184   |  double delta = (limit / peak - s->att) / buffer_size * channels;
    13←division by possibly zero aggregate factor
185   |  int found = 0;
186   |  
187   |  if (delta < s->delta) {
188   |                 s->delta = delta;
189   |                 nextpos[0] = s->pos;
190   |                 nextpos[1] = -1;
191   |                 nextdelta[0] = rdelta;
192   |                 s->nextlen = 1;
193   |                 s->nextiter= 0;
194   |             } else {
195   |  for (i = s->nextiter; i < s->nextiter + s->nextlen; i++) {
196   |  int j = i % buffer_size;
197   |  double ppeak = 0, pdelta;
198   |  
199   |  if (nextpos[j] >= 0)
200   |  for (c = 0; c < channels; c++) {
201   |                             ppeak = FFMAX(ppeak, fabs(buffer[nextpos[j] + c]));
202   |                         }
203   |                     pdelta = (limit / peak - limit / ppeak) / (((buffer_size - nextpos[j] + s->pos) % buffer_size) / channels);
204   |  if (pdelta < nextdelta[j]) {
205   |                         nextdelta[j] = pdelta;
206   |                         found = 1;
207   |  break;
208   |                     }
209   |                 }
210   |  if (found) {
211   |                     s->nextlen = i - s->nextiter + 1;
212   |                     nextpos[(s->nextiter + s->nextlen) % buffer_size] = s->pos;
213   |                     nextdelta[(s->nextiter + s->nextlen) % buffer_size] = rdelta;
214   |                     nextpos[(s->nextiter + s->nextlen + 1) % buffer_size] = -1;

# Formatting

Please provide your answer in the following format:

- Decision: {Bug/NotABug}
- Reason: {Your reason here}
