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

Analysis:
- Decision: NotABug
- Reason: The report points to `energy_ratio = pns_tgt_energy / pns_energy` and claims `pns_energy` is a “possibly zero aggregate factor.” While this is superficially a division-by-zero pattern, it does **not** match the provided target bug pattern closely, and the shown path does not establish a real bug.

  First, the target pattern is specifically:
  1. accumulate a normalization factor from runtime contributions, e.g. `norm_fac += ...`,
  2. later invert/divide by that aggregate,
  3. without checking whether the aggregate remained zero.

  Here, `pns_energy` is not a normalization factor built in the same way as the target pattern. It is accumulated from synthesized noise-band energies:
  ```c
  pns_senergy = scalarproduct_float(PNS, PNS, ...);
  pns_energy += pns_senergy;
  ```
  and then used as:
  ```c
  energy_ratio = pns_tgt_energy / pns_energy;
  ```
  So this is a generic possible zero-denominator warning, not the same “aggregate normalization factor later inverted” bug pattern.

  More importantly, the feasibility is weak from the code shown:

  - The accumulation loop executes over `w2 < sce->ics.group_len[w]`. In valid AAC block grouping, `group_len[w]` is expected to be positive for each group start, otherwise the outer loop
    ```c
    for (w = 0; w < sce->ics.num_windows; w += sce->ics.group_len[w])
    ```
    would not progress. So at least one iteration is expected here.

  - Inside that loop, `PNS` is filled with pseudo-random values:
    ```c
    s->random_state = lcg_random(s->random_state);
    PNS[i] = s->random_state;
    ```
    then
    ```c
    band_energy = scalarproduct_float(PNS, PNS, sce->ics.swb_sizes[g]);
    scale = noise_amp / sqrtf(band_energy);
    vector_fmul_scalar(PNS, PNS, scale, ...);
    pns_senergy = scalarproduct_float(PNS, PNS, ...);
    pns_energy += pns_senergy;
    ```
    For `pns_energy` to remain exactly zero, each `pns_senergy` would need to be zero. Given this construction, that would require degenerate conditions such as zero-length bands (`swb_sizes[g] == 0`) or all generated samples being zero. The latter is not realistic for an LCG-based random generator; the former would be a malformed/invalid band layout rather than a normal runtime case.

  - There is also earlier filtering that skips unsuitable bands entirely, so the division is only reached for bands deemed usable for PNS. That further reduces the chance of a degenerate zero-energy case.

  - Even if `pns_tgt_energy` could become zero (e.g. zero source energy), that does not by itself make this a bug; the relevant question is whether the denominator `pns_energy` can be zero on a real path. The shown code does not support that under valid codec state.

  Finally, the instruction says to compare against the pre-/post-patch behavior, but no fix patch is provided here showing that this exact site was changed to guard `pns_energy == 0`. Without such evidence, and with no concrete trigger under valid invariants, this should be classified conservatively as a false positive.



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
