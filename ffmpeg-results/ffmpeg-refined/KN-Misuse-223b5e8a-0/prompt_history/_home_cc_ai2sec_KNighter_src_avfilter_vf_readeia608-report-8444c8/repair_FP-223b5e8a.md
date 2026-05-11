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

File:| avfilter/vf_readeia608.c  
---|---  
Warning:| line 396, column 32  
division by possibly zero aggregate factor  
  
### Annotated Source Code


309   |     av_log(ctx, AV_LOG_DEBUG, "%d:", item);
310   |  for (int i = 0; i < len; i++) {
311   |         av_log(ctx, AV_LOG_DEBUG, " %03d", scan->code[i].size);
312   |     }
313   |     av_log(ctx, AV_LOG_DEBUG, "\n");
314   | }
315   |  
316   | #define READ_LINE(type, name)                                                 \
317   | static void read_##name(AVFrame *in, int nb_line, LineItem *line, int lp, int w) \
318   | {                                                                             \
319   |  const type *src = (const type *)(&in->data[0][nb_line * in->linesize[0]]);\
320   |  \
321   |  if (lp) {                                                                 \
322   |  for (int i = 0; i < w; i++) {                                         \
323   |  int a = FFMAX(i - 3, 0);                                          \
324   |  int b = FFMAX(i - 2, 0);                                          \
325   |  int c = FFMAX(i - 1, 0);                                          \
326   |  int d = FFMIN(i + 3, w-1);                                        \
327   |  int e = FFMIN(i + 2, w-1);                                        \
328   |  int f = FFMIN(i + 1, w-1);                                        \
329   |  \
330   |  line[LAG + i].input = (src[a] + src[b] + src[c] + src[i] +        \
331   |  src[d] + src[e] + src[f] + 6) / 7;         \
332   |  }                                                                     \
333   |  } else {                                                                  \
334   |  for (int i = 0; i < w; i++) {                                         \
335   |  line[LAG + i].input = src[i];                                     \
336   |  }                                                                     \
337   |  }                                                                         \
338   | }
339   |  
340   | READ_LINE(uint8_t, byte)
341   | READ_LINE(uint16_t, word)
342   |  
343   | static int config_input(AVFilterLink *inlink)
344   | {
345   |     AVFilterContext *ctx = inlink->dst;
346   |     ReadEIA608Context *s = ctx->priv;
347   |  const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
348   |  
349   |  if (!desc)
350   |  return AVERROR_BUG;
351   |     s->depth = desc->comp[0].depth;
352   |     s->max = (1 << desc->comp[0].depth) - 1;
353   |     s->read_line[0] = read_byte;
354   |     s->read_line[1] = read_word;
355   |  
356   |  return config_filter(ctx, s->start, s->end);
357   | }
358   |  
359   | static void extract_line(AVFilterContext *ctx, AVFrame *in, ScanItem *scan, int w, int nb_line)
360   | {
361   |  ReadEIA608Context *s = ctx->priv;
362   |     LineItem *line = scan->line;
363   |  int i, j, ch, len;
364   |     uint8_t codes[19] = { 0 };
365   |  float bit_size = 0.f;
366   |  int parity;
367   |  
368   |     memset(line, 0, (w + LAG) * sizeof(*line));
369   |     scan->byte[0] = scan->byte[1] = 0;
370   |     scan->found = 0;
371   |  
372   |  s->read_line[s->depth > 8](in, nb_line, line, s->lp, w);
    4←Assuming field 'depth' is <= 8→
373   |  
374   |     build_histogram(s, scan, line, w);
375   |     find_black_and_white(s, scan);
376   |  if (scan->white - scan->black < 5)
    5←Assuming the condition is false→
    6←Taking false branch→
377   |  return;
378   |  
379   |  thresholding(s, scan, line, LAG, 1, 0, w);
380   |     len = periods(line, scan->code, w);
381   |     dump_code(ctx, scan, len, nb_line);
382   |  if (len < 15 ||
    7←Assuming 'len' is >= 15→
    11←Taking false branch→
383   |  scan->code[14].bit != 0 ||
    8←Assuming field 'bit' is equal to 0→
384   |  w / (float)scan->code[14].size < SYNC_BITSIZE_MIN ||
    9←Assuming the condition is false→
385   |  w / (float)scan->code[14].size > SYNC_BITSIZE_MAX) {
    10←Assuming the condition is false→
386   |  return;
387   |     }
388   |  
389   |  for (i = 14; i11.1'i' is < 'len' < len; i++) {
    12←Loop condition is true.  Entering loop body→
    13←Assuming 'i' is >= 'len'→
    14←Loop condition is false. Execution continues on line 393→
390   |  bit_size += scan->code[i].size;
391   |  }
392   |  
393   |  bit_size /= 19.f;
394   |  for (i = 1; i < 14; i++) {
395   |  if (scan->code[i].size / bit_size > CLOCK_BITSIZE_MAX ||
    15←Assuming the condition is false→
396   |  scan->code[i].size / bit_size < CLOCK_BITSIZE_MIN) {
    16←division by possibly zero aggregate factor
397   |  return;
398   |         }
399   |     }
400   |  
401   |  if (scan->code[15].size / bit_size < 0.45f) {
402   |  return;
403   |     }
404   |  
405   |  for (j = 0, i = 14; i < len; i++) {
406   |  int run, bit;
407   |  
408   |         run = lrintf(scan->code[i].size / bit_size);
409   |         bit = scan->code[i].bit;
410   |  
411   |  for (int k = 0; j < 19 && k < run; k++) {
412   |             codes[j++] = bit;
413   |         }
414   |  
415   |  if (j >= 19)
416   |  break;
417   |     }
418   |  
419   |  for (ch = 0; ch < 2; ch++) {
420   |  for (parity = 0, i = 0; i < 8; i++) {
421   |  int b = codes[3 + ch * 8 + i];
422   |  
423   |  if (b == 255) {
424   |                 parity++;
425   |                 b = 1;
426   |             } else {
427   |                 b = 0;
428   |             }
429   |             scan->byte[ch] |= b << i;
430   |         }
431   |  
432   |  if (s->chp) {
433   |  if (!(parity & 1)) {
434   |                 scan->byte[ch] = 0x7F;
435   |             }
436   |         }
437   |     }
438   |  
439   |     scan->nb_line = nb_line;
440   |     scan->found = 1;
441   | }
442   |  
443   | static int extract_lines(AVFilterContext *ctx, void *arg,
444   |  int job, int nb_jobs)
445   | {
446   |  ReadEIA608Context *s = ctx->priv;
447   |     AVFilterLink *inlink = ctx->inputs[0];
448   |  const int h = s->end - s->start + 1;
449   |  const int start = (h * job) / nb_jobs;
450   |  const int end   = (h * (job+1)) / nb_jobs;
451   |     AVFrame *in = arg;
452   |  
453   |  for (int i = start; i < end; i++) {
    1Assuming 'i' is < 'end'→
    2←Loop condition is true.  Entering loop body→
454   |  ScanItem *scan = &s->scan[i];
455   |  
456   |  extract_line(ctx, in, scan, inlink->w, s->start + i);
    3←Calling 'extract_line'→
457   |     }
458   |  
459   |  return 0;
460   | }
461   |  
462   | static int filter_frame(AVFilterLink *inlink, AVFrame *in)
463   | {
464   |     AVFilterContext *ctx  = inlink->dst;
465   |     AVFilterLink *outlink = ctx->outputs[0];
466   |     ReadEIA608Context *s = ctx->priv;
467   |  int nb_found;
468   |  
469   |     ff_filter_execute(ctx, extract_lines, in, NULL,
470   |  FFMIN(FFMAX(s->end - s->start + 1, 1), ff_filter_get_nb_threads(ctx)));
471   |  
472   |     nb_found = 0;
473   |  for (int i = 0; i < s->end - s->start + 1; i++) {
474   |         ScanItem *scan = &s->scan[i];
475   |         uint8_t key[128], value[128];
476   |  
477   |  if (!scan->found)
478   |  continue;
479   |  
480   |  //snprintf(key, sizeof(key), "lavfi.readeia608.%d.bits", nb_found);
481   |  //snprintf(value, sizeof(value), "0b%d%d%d%d%d%d%d%d 0b%d%d%d%d%d%d%d%d", codes[3]==255,codes[4]==255,codes[5]==255,codes[6]==255,codes[7]==255,codes[8]==255,codes[9]==255,codes[10]==255,codes[11]==255,codes[12]==255,codes[13]==255,codes[14]==255,codes[15]==255,codes[16]==255,codes[17]==255,codes[18]==255);
482   |  //av_dict_set(&in->metadata, key, value, 0);
483   |  
484   |         snprintf(key, sizeof(key), "lavfi.readeia608.%d.cc", nb_found);
485   |         snprintf(value, sizeof(value), "0x%02X%02X", scan->byte[0], scan->byte[1]);
486   |         av_dict_set(&in->metadata, key, value, 0);

Analysis:
- Decision: NotABug
- Reason: The report does describe a possible floating-point divide by zero at:

  ```c
  scan->code[i].size / bit_size
  ```

  where `bit_size` is computed as:

  ```c
  for (i = 14; i < len; i++) {
      bit_size += scan->code[i].size;
  }
  bit_size /= 19.f;
  ```

  so at first glance it resembles the target pattern: an accumulated denominator is later used in division without an explicit zero check.

  However, when compared carefully against the target bug pattern and the actual control/data flow, this is not a convincing real bug.

  1. **It does not match the target pattern well enough.**  
     The target pattern is specifically about a normalization factor accumulated from runtime contributions that may all legitimately be zero, e.g.:

     ```c
     norm_fac += band->norm_fac;
     norm_fac = 1.0f / norm_fac;
     ```

     The essence is: valid inputs can leave the aggregate at exactly `0.0f`, and there is no guard before inversion.

     Here, `bit_size` is not an arbitrary normalization sum over optional contributions. It is a sum of `scan->code[i].size` values representing detected run lengths in a decoded line. These are sizes of periods/runs, i.e. structural lengths produced by `periods()`, not unconstrained floating data that can naturally all be zero.

  2. **Feasibility of `bit_size == 0` is not established by the report.**  
     Before the division, the code enforces:

     ```c
     if (len < 15 ||
         scan->code[14].bit != 0 ||
         w / (float)scan->code[14].size < SYNC_BITSIZE_MIN ||
         w / (float)scan->code[14].size > SYNC_BITSIZE_MAX) {
         return;
     }
     ```

     For execution to reach line 396, the condition must be false, so in particular both divisions by `scan->code[14].size` must be evaluated safely in real execution. That strongly implies `scan->code[14].size != 0`; otherwise `w / 0.0f` would already occur earlier. More importantly, index 14 is included in the accumulation loop:

     ```c
     for (i = 14; i < len; i++) {
         bit_size += scan->code[i].size;
     }
     ```

     Since `len >= 15`, the loop includes at least `i = 14`. Therefore:

     - minimum iterations: 1
     - one included term: `scan->code[14].size`
     - if `scan->code[14].size > 0`, then `bit_size > 0`
     - thus after `bit_size /= 19.f`, `bit_size > 0`

     So the only way `bit_size` can be zero is if `scan->code[14].size` is zero and all later sizes sum to zero too. But that would already make the earlier `w / (float)scan->code[14].size` expression problematic, and such a zero-sized run is inconsistent with the intended semantics of a detected period/run length.

  3. **Range reasoning supports nonzero denominator.**  
     From the path conditions:
     - `len >= 15`
     - accumulation includes element 14
     - `scan->code[14].size` is used as a divisor in a preceding guard

     If `size` is a run length/count, its practical domain is nonnegative integer, and successful period extraction should produce positive lengths. Hence:
     - `scan->code[14].size >= 1`
     - `bit_size >= 1`
     - `bit_size / 19.f >= 1/19 > 0`

     Therefore the denominator at line 396 is positive, not zero.

  4. **No evidence from a fix patch is provided showing this was a real bug.**  
     The instructions say to validate against pre-/post-patch behavior and whether a similar fix would address it. There is no patch here indicating developers added a `bit_size == 0` check or changed the logic because this path was reachable. Without such confirmation, and with the control-flow evidence above pointing to nonzero `bit_size`, this should be classified conservatively as a false positive.

  In short: although the analyzer sees an accumulated value used as a divisor, the specific zero-denominator scenario is not realistically reachable here given the preceding checks and the inclusion of `scan->code[14].size` in the sum. So this is **not a real instance of the target bug pattern**.

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
