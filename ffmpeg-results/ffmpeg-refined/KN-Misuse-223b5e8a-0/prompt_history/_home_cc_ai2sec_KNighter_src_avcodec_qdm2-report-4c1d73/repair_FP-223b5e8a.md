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

File:| avcodec/qdm2.c  
---|---  
Warning:| line 833, column 66  
division by possibly zero aggregate factor  
  
### Annotated Source Code


613   |                         tmp = 0;
614   |  else if (comp <= 10)
615   |                         tmp = 10;
616   |  else if (comp <= 16)
617   |                         tmp = 16;
618   |  else if (comp <= 24)
619   |                         tmp = -1;
620   |  else
621   |                         tmp = 0;
622   |                     coding_method[ch][sb][j] = ((tmp & 0xfffa) + 30 )& 0xff;
623   |                 }
624   |  for (sb = 0; sb < 30; sb++)
625   |             fix_coding_method_array(sb, nb_channels, coding_method);
626   |  for (ch = 0; ch < nb_channels; ch++)
627   |  for (sb = 0; sb < 30; sb++)
628   |  for (j = 0; j < 64; j++)
629   |  if (sb >= 10) {
630   |  if (coding_method[ch][sb][j] < 10)
631   |                             coding_method[ch][sb][j] = 10;
632   |                     } else {
633   |  if (sb >= 2) {
634   |  if (coding_method[ch][sb][j] < 16)
635   |                                 coding_method[ch][sb][j] = 16;
636   |                         } else {
637   |  if (coding_method[ch][sb][j] < 30)
638   |                                 coding_method[ch][sb][j] = 30;
639   |                         }
640   |                     }
641   | #endif
642   |     } else { // superblocktype_2_3 != 0
643   |  for (ch = 0; ch < nb_channels; ch++)
644   |  for (sb = 0; sb < 30; sb++)
645   |  for (j = 0; j < 64; j++)
646   |                     coding_method[ch][sb][j] = coding_method_table[cm_table_select][sb];
647   |     }
648   |  return 0;
649   | }
650   |  
651   | /**
652   |  * Called by process_subpacket_11 to process more data from subpacket 11
653   |  * with sb 0-8.
654   |  * Called by process_subpacket_12 to process data from subpacket 12 with
655   |  * sb 8-sb_used.
656   |  *
657   |  * @param q         context
658   |  * @param gb        bitreader context
659   |  * @param length    packet length in bits
660   |  * @param sb_min    lower subband processed (sb_min included)
661   |  * @param sb_max    higher subband processed (sb_max excluded)
662   |  */
663   | static int synthfilt_build_sb_samples(QDM2Context *q, GetBitContext *gb,
664   |  int length, int sb_min, int sb_max)
665   | {
666   |  int sb, j, k, n, ch, run, channels;
667   |  int joined_stereo, zero_encoding;
668   |  int type34_first;
669   |  float type34_div = 0;
670   |  float type34_predictor;
671   |  float samples[10];
672   |  int sign_bits[16] = {0};
673   |  
674   |  if (length == 0) {
    1Assuming 'length' is not equal to 0→
    2←Taking false branch→
675   |  // If no data use noise
676   |  for (sb=sb_min; sb < sb_max; sb++) {
677   |  int ret = build_sb_samples_from_noise(q, sb);
678   |  if (ret < 0)
679   |  return ret;
680   |         }
681   |  
682   |  return 0;
683   |     }
684   |  
685   |  for (sb = sb_min; sb < sb_max; sb++) {
    3←Assuming 'sb' is < 'sb_max'→
    4←Loop condition is true.  Entering loop body→
686   |  channels = q->nb_channels;
687   |  
688   |  if (q->nb_channels <= 1 || sb < 12)
    5←Assuming field 'nb_channels' is > 1→
    6←Assuming 'sb' is < 12→
    7←Taking true branch→
689   |  joined_stereo = 0;
690   |  else if (sb >= 24)
691   |             joined_stereo = 1;
692   |  else
693   |             joined_stereo = (get_bits_left(gb) >= 1) ? get_bits1(gb) : 0;
694   |  
695   |  if (joined_stereo7.1'joined_stereo' is 0) {
    8←Taking false branch→
696   |  if (get_bits_left(gb) >= 16)
697   |  for (j = 0; j < 16; j++)
698   |                     sign_bits[j] = get_bits1(gb);
699   |  
700   |  for (j = 0; j < 64; j++)
701   |  if (q->coding_method[1][sb][j] > q->coding_method[0][sb][j])
702   |                     q->coding_method[0][sb][j] = q->coding_method[1][sb][j];
703   |  
704   |  if (fix_coding_method_array(sb, q->nb_channels,
705   |                                             q->coding_method)) {
706   |                 av_log(NULL, AV_LOG_ERROR, "coding method invalid\n");
707   |  int ret = build_sb_samples_from_noise(q, sb);
708   |  if (ret < 0)
709   |  return ret;
710   |  continue;
711   |             }
712   |             channels = 1;
713   |         }
714   |  
715   |  for (ch = 0; ch8.1'ch' is < 'channels' < channels; ch++) {
    9←Loop condition is true.  Entering loop body→
716   |  FIX_NOISE_IDX(q->noise_idx);
    10←Assuming field 'noise_idx' is < 3840→
717   |  zero_encoding = (get_bits_left(gb) >= 1) ? get_bits1(gb) : 0;
    11←Assuming the condition is true→
    12←'?' condition is true→
718   |             type34_predictor = 0.0;
719   |             type34_first = 1;
720   |  
721   |  for (j = 0; j < 128; ) {
    13←Loop condition is true.  Entering loop body→
    24←Loop condition is true.  Entering loop body→
722   |  switch (q->coding_method[ch][sb][j / 2]) {
    14←Control jumps to 'case 34:'  at line 820→
    25←Control jumps to 'case 34:'  at line 820→
723   |  case 8:
724   |  if (get_bits_left(gb) >= 10) {
725   |  if (zero_encoding) {
726   |  for (k = 0; k < 5; k++) {
727   |  if ((j + 2 * k) >= 128)
728   |  break;
729   |                                     samples[2 * k] = get_bits1(gb) ? dequant_1bit[joined_stereo][2 * get_bits1(gb)] : 0;
730   |                                 }
731   |                             } else {
732   |                                 n = get_bits(gb, 8);
733   |  if (n >= 243) {
734   |                                     av_log(NULL, AV_LOG_ERROR, "Invalid 8bit codeword\n");
735   |  return AVERROR_INVALIDDATA;
736   |                                 }
737   |  
738   |  for (k = 0; k < 5; k++)
739   |                                     samples[2 * k] = dequant_1bit[joined_stereo][random_dequant_index[n][k]];
740   |                             }
741   |  for (k = 0; k < 5; k++)
742   |                                 samples[2 * k + 1] = SB_DITHERING_NOISE(sb,q->noise_idx);
743   |                         } else {
744   |  for (k = 0; k < 10; k++)
745   |                                 samples[k] = SB_DITHERING_NOISE(sb,q->noise_idx);
746   |                         }
747   |                         run = 10;
748   |  break;
749   |  
750   |  case 10:
751   |  if (get_bits_left(gb) >= 1) {
752   |  float f = 0.81;
770   |                                     samples[k] = (get_bits1(gb) == 0) ? 0 : dequant_1bit[joined_stereo][2 * get_bits1(gb)];
771   |                                 }
772   |                             } else {
773   |                                 n = get_bits (gb, 8);
774   |  if (n >= 243) {
775   |                                     av_log(NULL, AV_LOG_ERROR, "Invalid 8bit codeword\n");
776   |  return AVERROR_INVALIDDATA;
777   |                                 }
778   |  
779   |  for (k = 0; k < 5; k++)
780   |                                     samples[k] = dequant_1bit[joined_stereo][random_dequant_index[n][k]];
781   |                             }
782   |                         } else {
783   |  for (k = 0; k < 5; k++)
784   |                                 samples[k] = SB_DITHERING_NOISE(sb,q->noise_idx);
785   |                         }
786   |                         run = 5;
787   |  break;
788   |  
789   |  case 24:
790   |  if (get_bits_left(gb) >= 7) {
791   |                             n = get_bits(gb, 7);
792   |  if (n >= 125) {
793   |                                 av_log(NULL, AV_LOG_ERROR, "Invalid 7bit codeword\n");
794   |  return AVERROR_INVALIDDATA;
795   |                             }
796   |  
797   |  for (k = 0; k < 3; k++)
798   |                                 samples[k] = (random_dequant_type24[n][k] - 2.0) * 0.5;
799   |                         } else {
800   |  for (k = 0; k < 3; k++)
801   |                                 samples[k] = SB_DITHERING_NOISE(sb,q->noise_idx);
802   |                         }
803   |                         run = 3;
804   |  break;
805   |  
806   |  case 30:
807   |  if (get_bits_left(gb) >= 4) {
808   |  unsigned index = qdm2_get_vlc(gb, &vlc_tab_type30, 0, 1);
809   |  if (index >= FF_ARRAY_ELEMS(type30_dequant)) {
810   |                                 av_log(NULL, AV_LOG_ERROR, "index %d out of type30_dequant array\n", index);
811   |  return AVERROR_INVALIDDATA;
812   |                             }
813   |                             samples[0] = type30_dequant[index];
814   |                         } else
815   |                             samples[0] = SB_DITHERING_NOISE(sb,q->noise_idx);
816   |  
817   |                         run = 1;
818   |  break;
819   |  
820   |  case 34:
821   |  if (get_bits_left(gb) >= 7) {
    15←Assuming the condition is true→
    16←Taking true branch→
    26←Assuming the condition is true→
    27←Taking true branch→
822   |  if (type34_first16.1'type34_first' is 127.1'type34_first' is 0) {
    17←Taking true branch→
    28←Taking false branch→
823   |  type34_div = (float)(1 << get_bits(gb, 2));
    18←Assuming right operand of bit shift is less than 32→
824   |                                 samples[0] = ((float)get_bits(gb, 5) - 16.0) / 15.0;
825   |                                 type34_predictor = samples[0];
826   |  type34_first = 0;
827   |                             } else {
828   |  unsigned index = qdm2_get_vlc(gb, &vlc_tab_type34, 0, 1);
829   |  if (index >= FF_ARRAY_ELEMS(type34_delta)) {
    29←Assuming the condition is false→
    30←Taking false branch→
830   |                                     av_log(NULL, AV_LOG_ERROR, "index %d out of type34_delta array\n", index);
831   |  return AVERROR_INVALIDDATA;
832   |                                 }
833   |  samples[0] = type34_delta[index] / type34_div + type34_predictor;
    31←division by possibly zero aggregate factor
834   |                                 type34_predictor = samples[0];
835   |                             }
836   |                         } else {
837   |                             samples[0] = SB_DITHERING_NOISE(sb,q->noise_idx);
838   |                         }
839   |  run = 1;
840   |  break;
841   |  
842   |  default:
843   |                         samples[0] = SB_DITHERING_NOISE(sb,q->noise_idx);
844   |                         run = 1;
845   |  break;
846   |                 }
847   |  
848   |  if (joined_stereo19.1'joined_stereo' is 0) {
    19← Execution continues on line 848→
    20←Taking false branch→
849   |  for (k = 0; k < run && j + k < 128; k++) {
850   |                         q->sb_samples[0][j + k][sb] =
851   |                             q->tone_level[0][sb][(j + k) / 2] * samples[k];
852   |  if (q->nb_channels == 2) {
853   |  if (sign_bits[(j + k) / 8])
854   |                                 q->sb_samples[1][j + k][sb] =
855   |                                     q->tone_level[1][sb][(j + k) / 2] * -samples[k];
856   |  else
857   |                                 q->sb_samples[1][j + k][sb] =
858   |                                     q->tone_level[1][sb][(j + k) / 2] * samples[k];
859   |                         }
860   |                     }
861   |                 } else {
862   |  for (k = 0; k < run; k++)
    21←Loop condition is true.  Entering loop body→
    23←Loop condition is false. Execution continues on line 867→
863   |  if ((j + k) < 128)
    22←Taking true branch→
864   |  q->sb_samples[ch][j + k][sb] = q->tone_level[ch][sb][(j + k)/2] * samples[k];
865   |                 }
866   |  
867   |  j += run;
868   |  } // j loop
869   |         } // channel loop
870   |     } // subband loop
871   |  return 0;
872   | }
873   |  
874   | /**
875   |  * Init the first element of a channel in quantized_coeffs with data
876   |  * from packet 10 (quantized_coeffs[ch][0]).
877   |  * This is similar to process_subpacket_9, but for a single channel
878   |  * and for element [0]
879   |  * same VLC tables as process_subpacket_9 are used.
880   |  *
881   |  * @param quantized_coeffs    pointer to quantized_coeffs[ch][0]
882   |  * @param gb        bitreader context
883   |  */
884   | static int init_quantized_coeffs_elem0(int8_t *quantized_coeffs,
885   |                                         GetBitContext *gb)
886   | {
887   |  int i, k, run, level, diff;
888   |  
889   |  if (get_bits_left(gb) < 16)
890   |  return AVERROR_INVALIDDATA;
891   |     level = qdm2_get_vlc(gb, &vlc_tab_level, 0, 2);
892   |  
893   |     quantized_coeffs[0] = level;
894   |  
895   |  for (i = 0; i < 7; ) {
896   |  if (get_bits_left(gb) < 16)
897   |  return AVERROR_INVALIDDATA;
898   |         run = qdm2_get_vlc(gb, &vlc_tab_run, 0, 1) + 1;

Analysis:
- Decision: NotABug
- Reason: The report does identify a possible division by zero at:

  ```c
  samples[0] = type34_delta[index] / type34_div + type34_predictor;
  ```

  but it does **not** match the target bug pattern, and in the shown pre-patch control flow it is not a real bug.

  The target pattern is specifically:

  1. accumulate a denominator from runtime data,
  2. later divide by its aggregate value,
  3. without checking whether the aggregate stayed zero.

  That is the `norm_fac += ...; 1.0f / norm_fac;` style bug. This code is different. `type34_div` is **not** an accumulated normalization factor; it is explicitly initialized in the first `case 34` occurrence:

  ```c
  float type34_div = 0;
  int type34_first;
  ...
  type34_first = 1;
  ...
  case 34:
      if (get_bits_left(gb) >= 7) {
          if (type34_first) {
              type34_div = (float)(1 << get_bits(gb, 2));
              ...
              type34_first = 0;
          } else {
              ...
              samples[0] = type34_delta[index] / type34_div + type34_predictor;
          }
      }
  ```

  The key feasibility point is that the division is only reached in the `else` branch when `type34_first == 0`. On every path that sets `type34_first` from 1 to 0, `type34_div` is assigned:

  ```c
  type34_div = (float)(1 << get_bits(gb, 2));
  ```

  Since `get_bits(gb, 2)` returns a 2-bit unsigned value, its range is exactly **0..3**. Therefore:

  - minimum `1 << 0` = **1**
  - maximum `1 << 3` = **8**

  So after initialization, `type34_div ∈ {1.0, 2.0, 4.0, 8.0}`, never zero.

  The analyzer path appears to rely on entering `case 34` twice, first taking the `type34_first` branch and then the non-first branch. That path actually proves safety rather than danger: the first branch necessarily initializes `type34_div` to a nonzero value before the second branch can divide by it.

  Also, if the first `case 34` occurrence does **not** execute the initialization because `get_bits_left(gb) < 7`, then `type34_first` remains 1, so later `case 34` executions still cannot reach the division branch until a successful initialization occurs. Thus there is no feasible path to `type34_delta[index] / type34_div` with `type34_div == 0`.

  So this is a **false positive** both because:
  - it does not match the specified aggregate-normalization bug pattern, and
  - the denominator is provably nonzero on all feasible paths reaching the division.

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
