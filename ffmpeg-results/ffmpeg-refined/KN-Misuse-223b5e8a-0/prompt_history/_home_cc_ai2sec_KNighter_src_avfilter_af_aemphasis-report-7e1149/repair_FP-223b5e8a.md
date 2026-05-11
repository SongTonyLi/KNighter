# Role

You are an expert in developing and analyzing Clang Static Analyzer checkers, with decades of experience in the Clang project, particularly in the Static Analyzer plugin.

# Instruction

Please analyze this false positive case and propose fixes to the checker code to eliminate this specific false positive while maintaining detection of true positives.

Please help improve this checker to eliminate the false positive while maintaining its ability to detect actual issues. Your solution should:

1. Identify the root cause of the false positive
2. Propose specific fixes to the checker logic
3. Consider edge cases and possible regressions
4. Maintain compatibility with Clang-18 API

Note, the repaired checker needs to still **detect the target buggy code**.

## Suggestions

1. Use proper visitor patterns and state tracking
2. Handle corner cases gracefully
3. You could register a program state like `REGISTER_MAP_WITH_PROGRAMSTATE(...)` to track the information you need.
4. Follow Clang Static Analyzer best practices for checker development
5. DO NOT remove any existing `#include` in the checker code.

You could add some functions like `bool isFalsePositive(...)` to help you define and detect the false positive.

# Utility Functions

```cpp
// Going upward in an AST tree, and find the Stmt of a specific type
template <typename T>
const T* findSpecificTypeInParents(const Stmt *S, CheckerContext &C);

// Going downward in an AST tree, and find the Stmt of a secific type
// Only return one of the statements if there are many
template <typename T>
const T* findSpecificTypeInChildren(const Stmt *S);

bool EvaluateExprToInt(llvm::APSInt &EvalRes, const Expr *expr, CheckerContext &C) {
  Expr::EvalResult ExprRes;
  if (expr->EvaluateAsInt(ExprRes, C.getASTContext())) {
    EvalRes = ExprRes.Val.getInt();
    return true;
  }
  return false;
}

const llvm::APSInt *inferSymbolMaxVal(SymbolRef Sym, CheckerContext &C) {
  ProgramStateRef State = C.getState();
  const llvm::APSInt *maxVal = State->getConstraintManager().getSymMaxVal(State, Sym);
  return maxVal;
}

// The expression should be the DeclRefExpr of the array
bool getArraySizeFromExpr(llvm::APInt &ArraySize, const Expr *E) {
  if (const DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(E->IgnoreImplicit())) {
    if (const VarDecl *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
      QualType QT = VD->getType();
      if (const ConstantArrayType *ArrayType = dyn_cast<ConstantArrayType>(QT.getTypePtr())) {
        ArraySize = ArrayType->getSize();
        return true;
      }
    }
  }
  return false;
}

bool getStringSize(llvm::APInt &StringSize, const Expr *E) {
  if (const auto *SL = dyn_cast<StringLiteral>(E->IgnoreImpCasts())) {
    StringSize = llvm::APInt(32, SL->getLength());
    return true;
  }
  return false;
}

const MemRegion* getMemRegionFromExpr(const Expr* E, CheckerContext &C) {
  ProgramStateRef State = C.getState();
  return State->getSVal(E, C.getLocationContext()).getAsRegion();
}

struct KnownDerefFunction {
  const char *Name;                    ///< The function name.
  llvm::SmallVector<unsigned, 4> Params; ///< The parameter indices that get dereferenced.
};

/// \brief Determines if the given call is to a function known to dereference
///        certain pointer parameters.
///
/// This function looks up the call's callee name in a known table of functions
/// that definitely dereference one or more of their pointer parameters. If the
/// function is found, it appends the 0-based parameter indices that are dereferenced
/// into \p DerefParams and returns \c true. Otherwise, it returns \c false.
///
/// \param[in] Call        The function call to examine.
/// \param[out] DerefParams
///     A list of parameter indices that the function is known to dereference.
///
/// \return \c true if the function is found in the known-dereference table,
///         \c false otherwise.
bool functionKnownToDeref(const CallEvent &Call,
                                 llvm::SmallVectorImpl<unsigned> &DerefParams) {
  if (const IdentifierInfo *ID = Call.getCalleeIdentifier()) {
    StringRef FnName = ID->getName();

    for (const auto &Entry : DerefTable) {
      if (FnName.equals(Entry.Name)) {
        // We found the function in our table, copy its param indices
        DerefParams.append(Entry.Params.begin(), Entry.Params.end());
        return true;
      }
    }
  }
  return false;
}

/// \brief Determines if the source text of an expression contains a specified name.
bool ExprHasName(const Expr *E, StringRef Name, CheckerContext &C) {
  if (!E)
    return false;

  // Use const reference since getSourceManager() returns a const SourceManager.
  const SourceManager &SM = C.getSourceManager();
  const LangOptions &LangOpts = C.getLangOpts();
  // Retrieve the source text corresponding to the expression.
  CharSourceRange Range = CharSourceRange::getTokenRange(E->getSourceRange());
  StringRef ExprText = Lexer::getSourceText(Range, SM, LangOpts);

  // Check if the extracted text contains the specified name.
  return ExprText.contains(Name);
}
```

# Clang Check Functions

```cpp
void checkPreStmt (const ReturnStmt *DS, CheckerContext &C) const
 // Pre-visit the Statement.

void checkPostStmt (const DeclStmt *DS, CheckerContext &C) const
 // Post-visit the Statement.

void checkPreCall (const CallEvent &Call, CheckerContext &C) const
 // Pre-visit an abstract "call" event.

void checkPostCall (const CallEvent &Call, CheckerContext &C) const
 // Post-visit an abstract "call" event.

void checkBranchCondition (const Stmt *Condition, CheckerContext &Ctx) const
 // Pre-visit of the condition statement of a branch (such as IfStmt).


void checkLocation (SVal Loc, bool IsLoad, const Stmt *S, CheckerContext &) const
 // Called on a load from and a store to a location.

void checkBind (SVal Loc, SVal Val, const Stmt *S, CheckerContext &) const
 // Called on binding of a value to a location.


void checkBeginFunction (CheckerContext &Ctx) const
 // Called when the analyzer core starts analyzing a function, regardless of whether it is analyzed at the top level or is inlined.

void checkEndFunction (const ReturnStmt *RS, CheckerContext &Ctx) const
 // Called when the analyzer core reaches the end of a function being analyzed regardless of whether it is analyzed at the top level or is inlined.

void checkEndAnalysis (ExplodedGraph &G, BugReporter &BR, ExprEngine &Eng) const
 // Called after all the paths in the ExplodedGraph reach end of path.


bool evalCall (const CallEvent &Call, CheckerContext &C) const
 // Evaluates function call.

ProgramStateRef evalAssume (ProgramStateRef State, SVal Cond, bool Assumption) const
 // Handles assumptions on symbolic values.

ProgramStateRef checkRegionChanges (ProgramStateRef State, const InvalidatedSymbols *Invalidated, ArrayRef< const MemRegion * > ExplicitRegions, ArrayRef< const MemRegion * > Regions, const LocationContext *LCtx, const CallEvent *Call) const
 // Called when the contents of one or more regions change.

void checkASTDecl (const FunctionDecl *D, AnalysisManager &Mgr, BugReporter &BR) const
 // Check every declaration in the AST.

void checkASTCodeBody (const Decl *D, AnalysisManager &Mgr, BugReporter &BR) const
 // Check every declaration that has a statement body in the AST.
```


The following pattern is the checker designed to detect:

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

The patch that needs to be detected:

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


# False Positive Report

### Report Summary

File:| avfilter/af_aemphasis.c  
---|---  
Warning:| line 170, column 17  
division by possibly zero aggregate factor  
  
### Annotated Source Code


104   |  const double level_in = s->level_in;
105   |     ThreadData *td = arg;
106   |     AVFrame *out = td->out;
107   |     AVFrame *in = td->in;
108   |  const int start = (in->ch_layout.nb_channels * jobnr) / nb_jobs;
109   |  const int end = (in->ch_layout.nb_channels * (jobnr+1)) / nb_jobs;
110   |  
111   |  for (int ch = start; ch < end; ch++) {
112   |  const double *src = (const double *)in->extended_data[ch];
113   |  double *w = (double *)s->w->extended_data[ch];
114   |  double *dst = (double *)out->extended_data[ch];
115   |  
116   |  if (s->rc.use_brickw) {
117   |             biquad_process(&s->rc.brickw, dst, src, in->nb_samples, w + 2, level_in, 1.);
118   |             biquad_process(&s->rc.r1, dst, dst, in->nb_samples, w, 1., level_out);
119   |         } else {
120   |             biquad_process(&s->rc.r1, dst, src, in->nb_samples, w, level_in, level_out);
121   |         }
122   |     }
123   |  
124   |  return 0;
125   | }
126   |  
127   | static int filter_frame(AVFilterLink *inlink, AVFrame *in)
128   | {
129   |     AVFilterContext *ctx = inlink->dst;
130   |     AVFilterLink *outlink = ctx->outputs[0];
131   |     ThreadData td;
132   |     AVFrame *out;
133   |  
134   |  if (av_frame_is_writable(in)) {
135   |         out = in;
136   |     } else {
137   |         out = ff_get_audio_buffer(outlink, in->nb_samples);
138   |  if (!out) {
139   |             av_frame_free(&in);
140   |  return AVERROR(ENOMEM);
141   |         }
142   |         av_frame_copy_props(out, in);
143   |     }
144   |  
145   |     td.in = in; td.out = out;
146   |     ff_filter_execute(ctx, filter_channels, &td, NULL,
147   |  FFMIN(inlink->ch_layout.nb_channels, ff_filter_get_nb_threads(ctx)));
148   |  
149   |  if (in != out)
150   |         av_frame_free(&in);
151   |  return ff_filter_frame(outlink, out);
152   | }
153   |  
154   | static inline void set_highshelf_rbj(BiquadCoeffs *bq, double freq, double q, double peak, double sr)
155   | {
156   |  double A = sqrt(peak);
157   |  double w0 = freq * 2 * M_PI / sr;
158   |  double alpha = sin(w0) / (2 * q);
159   |  double cw0 = cos(w0);
160   |  double tmp = 2 * sqrt(A) * alpha;
161   |  double b0 = 0, ib0 = 0;
162   |  
163   |     bq->a0 =    A*( (A+1) + (A-1)*cw0 + tmp);
164   |     bq->a1 = -2*A*( (A-1) + (A+1)*cw0);
165   |     bq->a2 =    A*( (A+1) + (A-1)*cw0 - tmp);
166   |         b0 =        (A+1) - (A-1)*cw0 + tmp;
167   |     bq->b1 =    2*( (A-1) - (A+1)*cw0);
168   |  bq->b2 =        (A+1) - (A-1)*cw0 - tmp;
169   |  
170   |     ib0     = 1 / b0;
    16←division by possibly zero aggregate factor
171   |     bq->b1 *= ib0;
172   |     bq->b2 *= ib0;
173   |     bq->a0 *= ib0;
174   |     bq->a1 *= ib0;
175   |     bq->a2 *= ib0;
176   | }
177   |  
178   | static inline void set_lp_rbj(BiquadCoeffs *bq, double fc, double q, double sr, double gain)
179   | {
180   |  double omega = 2.0 * M_PI * fc / sr;
181   |  double sn = sin(omega);
182   |  double cs = cos(omega);
183   |  double alpha = sn/(2 * q);
184   |  double inv = 1.0/(1.0 + alpha);
185   |  
186   |     bq->a2 = bq->a0 = gain * inv * (1.0 - cs) * 0.5;
187   |     bq->a1 = bq->a0 + bq->a0;
188   |     bq->b1 = (-2.0 * cs * inv);
189   |     bq->b2 = ((1.0 - alpha) * inv);
190   | }
191   |  
192   | static double freq_gain(BiquadCoeffs *c, double freq, double sr)
193   | {
194   |  double zr, zi;
195   |  
196   |     freq *= 2.0 * M_PI / sr;
197   |     zr = cos(freq);
198   |     zi = -sin(freq);
199   |  
200   |  /* |(a0 + a1*z + a2*z^2)/(1 + b1*z + b2*z^2)| */
201   |  return hypot(c->a0 + c->a1*zr + c->a2*(zr*zr-zi*zi), c->a1*zi + 2*c->a2*zr*zi) /
202   |            hypot(1 + c->b1*zr + c->b2*(zr*zr-zi*zi), c->b1*zi + 2*c->b2*zr*zi);
203   | }
204   |  
205   | static int config_input(AVFilterLink *inlink)
206   | {
207   |  double i, j, k, g, t, a0, a1, a2, b1, b2, tau1, tau2, tau3;
208   |  double cutfreq, gain1kHz, gc, sr = inlink->sample_rate;
209   |     AVFilterContext *ctx = inlink->dst;
210   |     AudioEmphasisContext *s = ctx->priv;
211   |     BiquadCoeffs coeffs;
212   |  
213   |  if (!s->w)
    4←Assuming field 'w' is non-null→
    5←Taking false branch→
214   |         s->w = ff_get_audio_buffer(inlink, 4);
215   |  if (!s->w5.1Field 'w' is non-null)
    6←Taking false branch→
216   |  return AVERROR(ENOMEM);
217   |  
218   |  switch (s->type) {
    7←Control jumps to the 'default' case at line 235→
219   |  case 0: //"Columbia"
220   |         i = 100.;
221   |         j = 500.;
222   |         k = 1590.;
223   |  break;
224   |  case 1: //"EMI"
225   |         i = 70.;
226   |         j = 500.;
227   |         k = 2500.;
228   |  break;
229   |  case 2: //"BSI(78rpm)"
230   |         i = 50.;
231   |         j = 353.;
232   |         k = 3180.;
233   |  break;
234   |  case 3: //"RIAA"
235   |  default:
236   |  tau1 = 0.003180;
237   |  tau2 = 0.000318;
238   |         tau3 = 0.000075;
239   |         i = 1. / (2. * M_PI * tau1);
240   |         j = 1. / (2. * M_PI * tau2);
241   |         k = 1. / (2. * M_PI * tau3);
242   |  break;
    8← Execution continues on line 269→
243   |  case 4: //"CD Mastering"
244   |         tau1 = 0.000050;
245   |         tau2 = 0.000015;
246   |         tau3 = 0.0000001;// 1.6MHz out of audible range for null impact
247   |         i = 1. / (2. * M_PI * tau1);
248   |         j = 1. / (2. * M_PI * tau2);
249   |         k = 1. / (2. * M_PI * tau3);
250   |  break;
251   |  case 5: //"50µs FM (Europe)"
252   |         tau1 = 0.000050;
253   |         tau2 = tau1 / 20;// not used
254   |         tau3 = tau1 / 50;//
255   |         i = 1. / (2. * M_PI * tau1);
256   |         j = 1. / (2. * M_PI * tau2);
257   |         k = 1. / (2. * M_PI * tau3);
258   |  break;
259   |  case 6: //"75µs FM (US)"
260   |         tau1 = 0.000075;
261   |         tau2 = tau1 / 20;// not used
262   |         tau3 = tau1 / 50;//
263   |         i = 1. / (2. * M_PI * tau1);
264   |         j = 1. / (2. * M_PI * tau2);
265   |         k = 1. / (2. * M_PI * tau3);
266   |  break;
267   |     }
268   |  
269   |  i *= 2 * M_PI;
270   |     j *= 2 * M_PI;
271   |     k *= 2 * M_PI;
272   |  
273   |     t = 1. / sr;
274   |  
275   |  //swap a1 b1, a2 b2
276   |  if (s->type == 7 || s->type == 8) {
    9←Assuming field 'type' is equal to 7→
277   |  double tau = (s->type9.1Field 'type' is equal to 7 == 7 ? 0.000050 : 0.000075);
    10←'?' condition is true→
278   |  double f = 1.0 / (2 * M_PI * tau);
279   |  double nyq = sr * 0.5;
280   |  double gain = sqrt(1.0 + nyq * nyq / (f * f)); // gain at Nyquist
281   |  double cfreq = sqrt((gain - 1.0) * f * f); // frequency
282   |  double q = 1.0;
283   |  
284   |  if (s->type10.1Field 'type' is not equal to 8 == 8)
    11←Taking false branch→
285   |             q = pow((sr / 3269.0) + 19.5, -0.25); // somewhat poor curve-fit
286   |  if (s->type11.1Field 'type' is equal to 7 == 7)
    12←Taking true branch→
287   |  q = pow((sr / 4750.0) + 19.5, -0.25);
288   |  if (s->mode == 0)
    13←Assuming field 'mode' is not equal to 0→
    14←Taking false branch→
289   |             set_highshelf_rbj(&s->rc.r1, cfreq, q, 1. / gain, sr);
290   |  else
291   |  set_highshelf_rbj(&s->rc.r1, cfreq, q, gain, sr);
    15←Calling 'set_highshelf_rbj'→
292   |         s->rc.use_brickw = 0;
293   |     } else {
294   |         s->rc.use_brickw = 1;
295   |  if (s->mode == 0) { // Reproduction
296   |             g  = 1. / (4.+2.*i*t+2.*k*t+i*k*t*t);
297   |             a0 = (2.*t+j*t*t)*g;
298   |             a1 = (2.*j*t*t)*g;
299   |             a2 = (-2.*t+j*t*t)*g;
300   |             b1 = (-8.+2.*i*k*t*t)*g;
301   |             b2 = (4.-2.*i*t-2.*k*t+i*k*t*t)*g;
302   |         } else {  // Production
303   |             g  = 1. / (2.*t+j*t*t);
304   |             a0 = (4.+2.*i*t+2.*k*t+i*k*t*t)*g;
305   |             a1 = (-8.+2.*i*k*t*t)*g;
306   |             a2 = (4.-2.*i*t-2.*k*t+i*k*t*t)*g;
307   |             b1 = (2.*j*t*t)*g;
308   |             b2 = (-2.*t+j*t*t)*g;
309   |         }
310   |  
311   |         coeffs.a0 = a0;
312   |         coeffs.a1 = a1;
313   |         coeffs.a2 = a2;
314   |         coeffs.b1 = b1;
315   |         coeffs.b2 = b2;
316   |  
317   |  // the coeffs above give non-normalized value, so it should be normalized to produce 0dB at 1 kHz
318   |  // find actual gain
319   |  // Note: for FM emphasis, use 100 Hz for normalization instead
320   |         gain1kHz = freq_gain(&coeffs, 1000.0, sr);
321   |  // divide one filter's x[n-m] coefficients by that value
322   |         gc = 1.0 / gain1kHz;
323   |         s->rc.r1.a0 = coeffs.a0 * gc;
324   |         s->rc.r1.a1 = coeffs.a1 * gc;
325   |         s->rc.r1.a2 = coeffs.a2 * gc;
326   |         s->rc.r1.b1 = coeffs.b1;
327   |         s->rc.r1.b2 = coeffs.b2;
328   |     }
329   |  
330   |     cutfreq = FFMIN(0.45 * sr, 21000.);
331   |     set_lp_rbj(&s->rc.brickw, cutfreq, 0.707, sr, 1.);
332   |  
333   |  return 0;
334   | }
335   |  
336   | static int process_command(AVFilterContext *ctx, const char *cmd, const char *args,
337   |  char *res, int res_len, int flags)
338   | {
339   |  int ret;
340   |  
341   |     ret = ff_filter_process_command(ctx, cmd, args, res, res_len, flags);
342   |  if (ret < 0)
    1Assuming 'ret' is >= 0→
    2←Taking false branch→
343   |  return ret;
344   |  
345   |  return config_input(ctx->inputs[0]);
    3←Calling 'config_input'→
346   | }
347   |  
348   | static av_cold void uninit(AVFilterContext *ctx)
349   | {
350   |     AudioEmphasisContext *s = ctx->priv;
351   |  
352   |     av_frame_free(&s->w);
353   | }
354   |  
355   | static const AVFilterPad avfilter_af_aemphasis_inputs[] = {
356   |     {
357   |         .name         = "default",
358   |         .type         = AVMEDIA_TYPE_AUDIO,
359   |         .config_props = config_input,
360   |         .filter_frame = filter_frame,
361   |     },
362   | };
363   |  
364   | const FFFilter ff_af_aemphasis = {
365   |     .p.name        = "aemphasis",
366   |     .p.description = NULL_IF_CONFIG_SMALL("Audio emphasis."),
367   |     .p.priv_class  = &aemphasis_class,
368   |     .p.flags       = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC |
369   |  AVFILTER_FLAG_SLICE_THREADS,
370   |     .priv_size     = sizeof(AudioEmphasisContext),
371   |     .uninit        = uninit,
372   |  FILTER_INPUTS(avfilter_af_aemphasis_inputs),
373   |  FILTER_OUTPUTS(ff_audio_default_filterpad),
374   |  FILTER_SINGLE_SAMPLEFMT(AV_SAMPLE_FMT_DBLP),
375   |     .process_command = process_command,

Analysis:
- Decision: NotABug
- Reason: The report does describe a possible floating-point divide-by-zero at `ib0 = 1 / b0;`, but it does **not match the target bug pattern** and is not well-supported as a real bug from the shown path.

  First, the target pattern is specifically: **an aggregate normalization factor is accumulated from runtime data (e.g. summing per-band contributions) and later inverted without checking whether the aggregate is zero**. That is not what happens here. In `set_highshelf_rbj()`, `b0` is not an accumulated runtime normalization sum; it is a directly computed biquad denominator coefficient from analytic formulae:
  \[
  b0 = (A+1) - (A-1)\cos(w0) + 2\sqrt{A}\alpha
  \]
  where:
  - `A = sqrt(peak)`
  - `w0 = freq * 2π / sr`
  - `alpha = sin(w0) / (2*q)`

  So this is a coefficient formula, not the target “sum contributions then invert” pattern.

  Second, along the reported path, the inputs appear constrained so that `b0` is not actually zero:

  - The callsite is:
    ```c
    set_highshelf_rbj(&s->rc.r1, cfreq, q, gain, sr);
    ```
    or with `1./gain`, depending on mode.
  - `sr` is the input sample rate and must be positive in a valid audio filter configuration.
  - `tau` is fixed positive (`50µs` or `75µs`), so `f = 1/(2π tau) > 0`.
  - `nyq = sr * 0.5 > 0`, hence
    ```c
    gain = sqrt(1.0 + nyq * nyq / (f * f))
    ```
    gives `gain >= 1`, strictly `> 1` for positive `sr`.
  - Therefore `peak` passed to `set_highshelf_rbj()` is either `gain > 1` or `1/gain`, which is in `(0,1)`. In both cases `peak > 0`, so `A = sqrt(peak) > 0`.

  For zero-division, we would need `b0 = 0`. But with valid filter parameters:
  - `q` is assigned from `pow((sr / const) + 19.5, -0.25)`, so for positive `sr`, `q > 0`.
  - Thus `alpha = sin(w0)/(2q)` is finite.
  - More importantly, the standard RBJ high-shelf design normalizes by `b0`, and for positive `A` and valid `q`, this denominator is intended to remain nonzero for realizable filters. The analyzer has not shown a feasible parameter combination making it zero.

  We can also check the dangerous corner:
  - If `peak = 1`, then `A = 1`, and
    ```c
    b0 = 2 + 2*alpha
    ```
    which is zero only if `alpha = -1`.
    But on this path `gain > 1`, so `peak` is either `gain > 1` or `1/gain < 1`; exact `peak = 1` does not occur for valid positive `sr`.
  - Even aside from that, the call uses derived `cfreq` and positive `q`, not arbitrary attacker-controlled values.

  Finally, comparing against the required validation criterion: there is no indication of a pre-/post-patch style fix here that adds a zero check around an accumulated normalization factor. A “similar fix” to the target bug would be guarding a data-derived aggregate before inversion; that is not this code structure.

  So this is a **false positive relative to the target bug pattern**, and the report does not establish a real, feasible divide-by-zero in this context.

# Checker
```cpp
#include "clang/StaticAnalyzer/Core/BugReporter/BugReporter.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Checkers/Taint.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/Environment.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramState.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramStateTrait.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SymExpr.h"
#include "clang/StaticAnalyzer/Frontend/CheckerRegistry.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/StmtVisitor.h"
#include "llvm/Support/raw_ostream.h"
#include "clang/StaticAnalyzer/Checkers/utility.h"
#include "clang/AST/Expr.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/Decl.h"
#include "llvm/ADT/APSInt.h"

using namespace clang;
using namespace ento;
using namespace taint;

REGISTER_MAP_WITH_PROGRAMSTATE(AggregateDenomMap, const MemRegion *, bool)

namespace {

static bool isRealFloatingType(QualType QT) {
  return !QT.isNull() && QT->isRealFloatingType();
}

static bool isZeroLiteralExpr(const Expr *E, CheckerContext &C) {
  if (!E)
    return false;

  E = E->IgnoreParenImpCasts();
  if (!E)
    return false;

  if (const auto *FL = dyn_cast<FloatingLiteral>(E))
    return FL->getValue().isZero();

  if (const auto *IL = dyn_cast<IntegerLiteral>(E))
    return IL->getValue().isZero();

  llvm::APSInt Res;
  if (EvaluateExprToInt(Res, E, C))
    return Res == 0;

  return false;
}

static const MemRegion *getVarRegionFromDecl(const VarDecl *VD, CheckerContext &C) {
  if (!VD)
    return nullptr;

  const LocationContext *LCtx = C.getLocationContext();
  if (!LCtx)
    return nullptr;

  const MemRegion *MR = C.getState()->getRegion(VD, LCtx);
  if (!MR)
    return nullptr;

  MR = MR->getBaseRegion();
  return MR;
}

static const MemRegion *getRegionFromTrackedExpr(const Expr *E, CheckerContext &C) {
  if (!E)
    return nullptr;

  E = E->IgnoreParenImpCasts();
  if (!E)
    return nullptr;

  const MemRegion *MR = getMemRegionFromExpr(E, C);
  if (!MR)
    return nullptr;

  MR = MR->getBaseRegion();
  return MR;
}

static const MemRegion *getTrackedRegionFromZeroGuardCond(const Expr *Cond,
                                                          CheckerContext &C) {
  if (!Cond)
    return nullptr;

  Cond = Cond->IgnoreParenImpCasts();
  if (!Cond)
    return nullptr;

  if (const auto *DRE = dyn_cast<DeclRefExpr>(Cond)) {
    if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
      if (isRealFloatingType(VD->getType()))
        return getVarRegionFromDecl(VD, C);
    }
  }

  if (const auto *BO = dyn_cast<BinaryOperator>(Cond)) {
    if (BO->getOpcode() == BO_EQ || BO->getOpcode() == BO_NE) {
      const Expr *LHS = BO->getLHS()->IgnoreParenImpCasts();
      const Expr *RHS = BO->getRHS()->IgnoreParenImpCasts();

      if (isZeroLiteralExpr(LHS, C)) {
        if (const auto *DRE = dyn_cast<DeclRefExpr>(RHS)) {
          if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
            if (isRealFloatingType(VD->getType()))
              return getVarRegionFromDecl(VD, C);
          }
        }
      }

      if (isZeroLiteralExpr(RHS, C)) {
        if (const auto *DRE = dyn_cast<DeclRefExpr>(LHS)) {
          if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
            if (isRealFloatingType(VD->getType()))
              return getVarRegionFromDecl(VD, C);
          }
        }
      }
    }
  }

  return nullptr;
}

class SAGenTestChecker
    : public Checker<check::PostStmt<DeclStmt>,
                     check::Bind,
                     check::BranchCondition,
                     check::PreStmt<BinaryOperator>> {
  mutable std::unique_ptr<BugType> BT;

public:
  SAGenTestChecker()
      : BT(new BugType(this, "Possible division by zero",
                       "Possible division by zero")) {}

  void checkPostStmt(const DeclStmt *DS, CheckerContext &C) const;
  void checkBind(SVal Loc, SVal Val, const Stmt *S, CheckerContext &C) const;
  void checkBranchCondition(const Stmt *Condition, CheckerContext &C) const;
  void checkPreStmt(const BinaryOperator *BO, CheckerContext &C) const;

private:
  void reportBug(const BinaryOperator *BO, CheckerContext &C,
                 const MemRegion *MR) const;
};

void SAGenTestChecker::checkPostStmt(const DeclStmt *DS,
                                     CheckerContext &C) const {
  ProgramStateRef State = C.getState();
  bool Changed = false;

  for (const Decl *D : DS->decls()) {
    const auto *VD = dyn_cast<VarDecl>(D);
    if (!VD)
      continue;
    if (!VD->hasLocalStorage())
      continue;
    if (!isRealFloatingType(VD->getType()))
      continue;

    const Expr *Init = VD->getInit();
    if (!Init)
      continue;

    if (!isZeroLiteralExpr(Init, C))
      continue;

    const MemRegion *MR = getVarRegionFromDecl(VD, C);
    if (!MR)
      continue;

    State = State->set<AggregateDenomMap>(MR, true);
    Changed = true;
  }

  if (Changed)
    C.addTransition(State);
}

void SAGenTestChecker::checkBind(SVal Loc, SVal Val, const Stmt *S,
                                 CheckerContext &C) const {
  ProgramStateRef State = C.getState();

  const MemRegion *LHSReg = Loc.getAsRegion();
  if (!LHSReg)
    return;
  LHSReg = LHSReg->getBaseRegion();
  if (!LHSReg)
    return;

  const bool *Tracked = State->get<AggregateDenomMap>(LHSReg);
  if (!Tracked || !*Tracked)
    return;

  const Expr *BindExpr = dyn_cast_or_null<Expr>(S);
  if (!BindExpr)
    return;

  BindExpr = BindExpr->IgnoreParenImpCasts();
  if (!BindExpr)
    return;

  if (isZeroLiteralExpr(BindExpr, C)) {
    C.addTransition(State);
    return;
  }

  llvm::APFloat F(0.0);
  if (BindExpr->EvaluateAsFloat(F, C.getASTContext())) {
    if (!F.isZero()) {
      State = State->remove<AggregateDenomMap>(LHSReg);
      C.addTransition(State);
      return;
    }
  }

  C.addTransition(State);
}

void SAGenTestChecker::checkBranchCondition(const Stmt *Condition,
                                            CheckerContext &C) const {
  ProgramStateRef State = C.getState();
  const Expr *CondE = dyn_cast_or_null<Expr>(Condition);
  if (!CondE)
    return;

  const MemRegion *MR = getTrackedRegionFromZeroGuardCond(CondE, C);
  if (!MR)
    return;

  const bool *Tracked = State->get<AggregateDenomMap>(MR);
  if (!Tracked || !*Tracked)
    return;

  State = State->remove<AggregateDenomMap>(MR);
  C.addTransition(State);
}

void SAGenTestChecker::checkPreStmt(const BinaryOperator *BO,
                                    CheckerContext &C) const {
  if (!BO)
    return;
  if (BO->getOpcode() != BO_Div)
    return;

  QualType QT = BO->getType();
  bool IsFPDiv = (!QT.isNull() && QT->isRealFloatingType()) ||
                 (!BO->getLHS()->getType().isNull() &&
                  BO->getLHS()->getType()->isRealFloatingType()) ||
                 (!BO->getRHS()->getType().isNull() &&
                  BO->getRHS()->getType()->isRealFloatingType());
  if (!IsFPDiv)
    return;

  const Expr *Denom = BO->getRHS();
  if (!Denom)
    return;
  Denom = Denom->IgnoreParenImpCasts();

  const auto *DRE = dyn_cast<DeclRefExpr>(Denom);
  if (!DRE)
    return;

  const auto *VD = dyn_cast<VarDecl>(DRE->getDecl());
  if (!VD)
    return;
  if (!isRealFloatingType(VD->getType()))
    return;

  const MemRegion *MR = getVarRegionFromDecl(VD, C);
  if (!MR)
    return;

  const bool *Tracked = C.getState()->get<AggregateDenomMap>(MR);
  if (!Tracked || !*Tracked)
    return;

  reportBug(BO, C, MR);
}

void SAGenTestChecker::reportBug(const BinaryOperator *BO, CheckerContext &C,
                                 const MemRegion *MR) const {
  ExplodedNode *N = C.generateNonFatalErrorNode();
  if (!N)
    return;

  auto R = std::make_unique<PathSensitiveBugReport>(
      *BT, "division by possibly zero aggregate factor", N);
  R->addRange(BO->getSourceRange());
  C.emitReport(std::move(R));
}

} // end anonymous namespace

extern "C" void clang_registerCheckers(CheckerRegistry &registry) {
  registry.addChecker<SAGenTestChecker>(
      "custom.SAGenTestChecker",
      "Detects floating-point division by an aggregate factor that may remain zero",
      "");
}

extern "C" const char clang_analyzerAPIVersionString[] =
    CLANG_ANALYZER_API_VERSION_STRING;

```

# Formatting

Please provide the whole checker code after fixing the false positive.
The refined code must be surrounded by ```cpp and ```.
Your response should be like:

Refinment Plan:
XXX

Refined Code:
```cpp
{{fixed checker code here}}
```
