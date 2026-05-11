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

The bug pattern is **calling a partial/short-read I/O API (`avio_read()`) and then using the destination buffer as if it were fully initialized, without verifying that the requested number of bytes was actually read**.

This commonly happens when:
- data is read into a stack or heap buffer,
- the return value of the read function is ignored,
- the buffer is then used for:
  - parsing fields / offset calculations,
  - `memcmp()` or other comparisons,
  - string/metadata handling,
  - control-flow decisions.

If the input is truncated or the read is otherwise short, part of the buffer remains uninitialized, causing undefined behavior and potentially incorrect parsing or memory-safety issues. The correct pattern is to check for a full read (or use a helper like `ffio_read_size()` that guarantees exact-size reads or returns an error) before consuming the buffer.

The patch that needs to be detected:

## Patch Description

avformat: check avio_read() return values in dss/dtshd/mlv

Multiple demuxers call avio_read() without checking its return
value. When input is truncated, destination buffers remain
uninitialized but are still used for offset calculations, memcmp,
and metadata handling. This results in undefined behavior
(detectable with Valgrind/MSan).

Fix this by checking the return value of avio_read() in:
- dss.c: dss_read_seek() — check before using header buffer
- dtshddec.c: FILEINFO chunk — check before using value buffer
- mlvdec.c: check_file_header() — check before memcmp on version

Fixes: #21520

## Buggy Code

```c
// Function: dss_read_seek in libavformat/dss.c
static int dss_read_seek(AVFormatContext *s, int stream_index,
                         int64_t timestamp, int flags)
{
    DSSDemuxContext *ctx = s->priv_data;
    int64_t ret, seekto;
    uint8_t header[DSS_AUDIO_BLOCK_HEADER_SIZE];
    int offset;

    if (ctx->audio_codec == DSS_ACODEC_DSS_SP)
        seekto = timestamp / 264 * 41 / 506 * 512;
    else
        seekto = timestamp / 240 * ctx->packet_size / 506 * 512;

    if (seekto < 0)
        seekto = 0;

    seekto += ctx->dss_header_size;

    ret = avio_seek(s->pb, seekto, SEEK_SET);
    if (ret < 0)
        return ret;

    avio_read(s->pb, header, DSS_AUDIO_BLOCK_HEADER_SIZE);
    ctx->swap = !!(header[0] & 0x80);
    offset = 2*header[1] + 2*ctx->swap;
    if (offset < DSS_AUDIO_BLOCK_HEADER_SIZE)
        return AVERROR_INVALIDDATA;
    if (offset == DSS_AUDIO_BLOCK_HEADER_SIZE) {
        ctx->counter = 0;
        offset = avio_skip(s->pb, -DSS_AUDIO_BLOCK_HEADER_SIZE);
    } else {
        ctx->counter = DSS_BLOCK_SIZE - offset;
        offset = avio_skip(s->pb, offset - DSS_AUDIO_BLOCK_HEADER_SIZE);
    }
    ctx->dss_sp_swap_byte = -1;
    return 0;
}
```

```c
// Function: check_file_header in libavformat/mlvdec.c
static int check_file_header(AVIOContext *pb, uint64_t guid)
{
    unsigned int size;
    uint8_t version[8];

    avio_skip(pb, 4);
    size = avio_rl32(pb);
    if (size < 52)
        return AVERROR_INVALIDDATA;
    avio_read(pb, version, 8);
    if (memcmp(version, MLV_VERSION, 5) || avio_rl64(pb) != guid)
        return AVERROR_INVALIDDATA;
    avio_skip(pb, size - 24);
    return 0;
}
```

```c
// Function: dtshd_read_header in libavformat/dtshddec.c
static int dtshd_read_header(AVFormatContext *s)
{
    DTSHDDemuxContext *dtshd = s->priv_data;
    AVIOContext *pb = s->pb;
    uint64_t chunk_type, chunk_size;
    int64_t duration, orig_nb_samples, data_start;
    AVStream *st;
    FFStream *sti;
    int ret;
    char *value;

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);
    st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
    st->codecpar->codec_id   = AV_CODEC_ID_DTS;
    sti = ffstream(st);
    sti->need_parsing = AVSTREAM_PARSE_FULL_RAW;

    for (;;) {
        chunk_type = avio_rb64(pb);
        chunk_size = avio_rb64(pb);

        if (avio_feof(pb))
            break;

        if (chunk_size < 4) {
            av_log(s, AV_LOG_ERROR, "chunk size too small\n");
            return AVERROR_INVALIDDATA;
        }
        if (chunk_size > ((uint64_t)1 << 61)) {
            av_log(s, AV_LOG_ERROR, "chunk size too big\n");
            return AVERROR_INVALIDDATA;
        }

        switch (chunk_type) {
        case STRMDATA:
            data_start = avio_tell(pb);
            dtshd->data_end = data_start + chunk_size;
            if (dtshd->data_end <= chunk_size)
                return AVERROR_INVALIDDATA;
            if (!(pb->seekable & AVIO_SEEKABLE_NORMAL))
                goto break_loop;
            goto skip;
            break;
        case AUPR_HDR:
            if (chunk_size < 21)
                return AVERROR_INVALIDDATA;
            avio_skip(pb, 3);
            st->codecpar->sample_rate = avio_rb24(pb);
            if (!st->codecpar->sample_rate)
                return AVERROR_INVALIDDATA;
            duration  = avio_rb32(pb); // num_frames
            duration *= avio_rb16(pb); // samples_per_frames
            st->duration = duration;
            orig_nb_samples  = avio_rb32(pb);
            orig_nb_samples <<= 8;
            orig_nb_samples |= avio_r8(pb);
            st->codecpar->ch_layout.nb_channels = ff_dca_count_chs_for_mask(avio_rb16(pb));
            st->codecpar->initial_padding = avio_rb16(pb);
            st->codecpar->trailing_padding = FFMAX(st->duration - orig_nb_samples - st->codecpar->initial_padding, 0);
            st->start_time =
            sti->start_skip_samples = st->codecpar->initial_padding;
            sti->first_discard_sample = orig_nb_samples + st->codecpar->initial_padding;
            sti->last_discard_sample = st->duration;
            avio_skip(pb, chunk_size - 21);
            break;
        case FILEINFO:
            if (chunk_size > INT_MAX)
                goto skip;
            value = av_malloc(chunk_size);
            if (!value)
                goto skip;
            avio_read(pb, value, chunk_size);
            value[chunk_size - 1] = 0;
            av_dict_set(&s->metadata, "fileinfo", value,
                        AV_DICT_DONT_STRDUP_VAL);
            break;
        default:
skip:
            ret = avio_skip(pb, chunk_size);
            if (ret < 0)
                return ret;
        };
    }

    if (!dtshd->data_end)
        return AVERROR_EOF;

    avio_seek(pb, data_start, SEEK_SET);

break_loop:
    if (st->codecpar->sample_rate)
        avpriv_set_pts_info(st, 64, 1, st->codecpar->sample_rate);

    return 0;
}
```

## Bug Fix Patch

```diff
diff --git a/libavformat/dss.c b/libavformat/dss.c
index 6cabdb5421..ce86b32d6f 100644
--- a/libavformat/dss.c
+++ b/libavformat/dss.c
@@ -26,6 +26,7 @@
 #include "avformat.h"
 #include "demux.h"
 #include "internal.h"
+#include "avio_internal.h"
 
 #define DSS_HEAD_OFFSET_AUTHOR        0xc
 #define DSS_AUTHOR_SIZE               16
@@ -339,7 +340,9 @@ static int dss_read_seek(AVFormatContext *s, int stream_index,
     if (ret < 0)
         return ret;
 
-    avio_read(s->pb, header, DSS_AUDIO_BLOCK_HEADER_SIZE);
+    ret = ffio_read_size(s->pb, header, DSS_AUDIO_BLOCK_HEADER_SIZE);
+    if (ret < 0)
+        return ret;
     ctx->swap = !!(header[0] & 0x80);
     offset = 2*header[1] + 2*ctx->swap;
     if (offset < DSS_AUDIO_BLOCK_HEADER_SIZE)
diff --git a/libavformat/dtshddec.c b/libavformat/dtshddec.c
index b980fde6a9..843fd5460d 100644
--- a/libavformat/dtshddec.c
+++ b/libavformat/dtshddec.c
@@ -26,6 +26,7 @@
 #include "avformat.h"
 #include "demux.h"
 #include "internal.h"
+#include "avio_internal.h"
 
 #define AUPR_HDR 0x415550522D484452
 #define AUPRINFO 0x41555052494E464F
@@ -125,7 +126,11 @@ static int dtshd_read_header(AVFormatContext *s)
             value = av_malloc(chunk_size);
             if (!value)
                 goto skip;
-            avio_read(pb, value, chunk_size);
+            ret = ffio_read_size(pb, value, chunk_size);
+            if (ret < 0) {
+                av_free(value);
+                goto skip;
+            }
             value[chunk_size - 1] = 0;
             av_dict_set(&s->metadata, "fileinfo", value,
                         AV_DICT_DONT_STRDUP_VAL);
diff --git a/libavformat/mlvdec.c b/libavformat/mlvdec.c
index fa35bc9c45..2c1fe001c7 100644
--- a/libavformat/mlvdec.c
+++ b/libavformat/mlvdec.c
@@ -36,6 +36,7 @@
 #include "avformat.h"
 #include "demux.h"
 #include "internal.h"
+#include "avio_internal.h"
 #include "riff.h"
 
 #define MLV_VERSION "v2.0"
@@ -74,12 +75,15 @@ static int check_file_header(AVIOContext *pb, uint64_t guid)
 {
     unsigned int size;
     uint8_t version[8];
+    int ret;
 
     avio_skip(pb, 4);
     size = avio_rl32(pb);
     if (size < 52)
         return AVERROR_INVALIDDATA;
-    avio_read(pb, version, 8);
+    ret = ffio_read_size(pb, version, 8);
+    if (ret < 0)
+        return ret;
     if (memcmp(version, MLV_VERSION, 5) || avio_rl64(pb) != guid)
         return AVERROR_INVALIDDATA;
     avio_skip(pb, size - 24);
```


# False Positive Report

### Report Summary

File:| format/concat.c  
---|---  
Warning:| line 245, column 14  
buffer read by avio_read may be partially uninitialized  
  
### Annotated Source Code


162   | {
163   |     int64_t result;
164   |  struct concat_data  *data  = h->priv_data;
165   |  struct concat_nodes *nodes = data->nodes;
166   |     size_t i;
167   |  
168   |  if ((whence & AVSEEK_SIZE))
169   |  return data->total_size;
170   |  switch (whence) {
171   |  case SEEK_END:
172   |  for (i = data->length - 1; i && pos < -nodes[i].size; i--)
173   |             pos += nodes[i].size;
174   |  break;
175   |  case SEEK_CUR:
176   |  /* get the absolute position */
177   |  for (i = 0; i != data->current; i++)
178   |             pos += nodes[i].size;
179   |         pos += ffurl_seek(nodes[i].uc, 0, SEEK_CUR);
180   |         whence = SEEK_SET;
181   |  /* fall through with the absolute position */
182   |  case SEEK_SET:
183   |  for (i = 0; i != data->length - 1 && pos >= nodes[i].size; i++)
184   |             pos -= nodes[i].size;
185   |  break;
186   |  default:
187   |  return AVERROR(EINVAL);
188   |     }
189   |  
190   |     result = ffurl_seek(nodes[i].uc, pos, whence);
191   |  if (result >= 0) {
192   |         data->current = i;
193   |  while (i)
194   |             result += nodes[--i].size;
195   |     }
196   |  return result;
197   | }
198   |  
199   | #if CONFIG_CONCAT_PROTOCOL
200   | const URLProtocol ff_concat_protocol = {
201   |     .name           = "concat",
202   |     .url_open       = concat_open,
203   |     .url_read       = concat_read,
204   |     .url_seek       = concat_seek,
205   |     .url_close      = concat_close,
206   |     .priv_data_size = sizeof(struct concat_data),
207   |     .default_whitelist = "concat,file,subfile",
208   | };
209   | #endif
210   |  
211   | #if CONFIG_CONCATF_PROTOCOL
212   | static av_cold int concatf_open(URLContext *h, const char *uri, int flags)
213   | {
214   |  AVBPrint bp;
215   |  struct concat_data *data = h->priv_data;
216   |     AVIOContext *in = NULL;
217   |  const char *cursor;
218   |     int64_t total_size = 0;
219   |  unsigned int nodes_size = 0;
220   |     size_t i = 0;
221   |  int err;
222   |  
223   |  if (!av_strstart(uri, "concatf:", &uri)) {
    1Assuming the condition is false→
    2←Taking false branch→
224   |         av_log(h, AV_LOG_ERROR, "URL %s lacks prefix\n", uri);
225   |  return AVERROR(EINVAL);
226   |     }
227   |  
228   |  /* handle input */
229   |  if (!*uri)
    3←Assuming the condition is false→
    4←Taking false branch→
230   |  return AVERROR(ENOENT);
231   |  
232   |  err = ffio_open_whitelist(&in, uri, AVIO_FLAG_READ, &h->interrupt_callback,
233   |  NULL, h->protocol_whitelist, h->protocol_blacklist);
234   |  if (err < 0)
    5←Assuming 'err' is >= 0→
    6←Taking false branch→
235   |  return err;
236   |  
237   |  av_bprint_init(&bp, 0, AV_BPRINT_SIZE_UNLIMITED);
238   |     err = avio_read_to_bprint(in, &bp, SIZE_MAX);
239   |     avio_closep(&in);
240   |  if (err < 0) {
    7←Assuming 'err' is >= 0→
    8←Taking false branch→
241   |         av_bprint_finalize(&bp, NULL);
242   |  return err;
243   |     }
244   |  
245   |  cursor = bp.str;
    9←buffer read by avio_read may be partially uninitialized
246   |  while (*cursor) {
247   |  struct concat_nodes *nodes;
248   |         URLContext *uc;
249   |  char *node_uri;
250   |         int64_t size;
251   |         size_t len = i;
252   |  int leading_spaces = strspn(cursor, " \n\t\r");
253   |  
254   |  if (!cursor[leading_spaces])
255   |  break;
256   |  
257   |         node_uri = av_get_token(&cursor, "\r\n");
258   |  if (!node_uri) {
259   |             err = AVERROR(ENOMEM);
260   |  break;
261   |         }
262   |  if (*cursor)
263   |             cursor++;
264   |  
265   |  if (++len == SIZE_MAX / sizeof(*nodes)) {
266   |             av_free(node_uri);
267   |             err = AVERROR(ENAMETOOLONG);
268   |  break;
269   |         }
270   |  
271   |  /* creating URLContext */
272   |         err = ffurl_open_whitelist(&uc, node_uri, flags,
273   |                                    &h->interrupt_callback, NULL, h->protocol_whitelist, h->protocol_blacklist, h);
274   |         av_free(node_uri);
275   |  if (err < 0)

Analysis:
- Decision: NotABug
- Reason: The report does not match the target bug pattern, and the shown code is not a real instance of the specified short-read/uninitialized-buffer bug.

  The target pattern is specifically:

  1. call a partial-read API like `avio_read()`,
  2. fail to verify that the requested byte count was fully read,
  3. then consume the destination buffer as if fully initialized.

  That is not what this code is doing.

  In `concatf_open()`, the relevant operations are:

  - `err = avio_read_to_bprint(in, &bp, SIZE_MAX);`
  - `avio_closep(&in);`
  - `if (err < 0) ... return err;`
  - `cursor = bp.str;`
  - parse `bp.str` as a NUL-terminated string.

  Key points:

  1. **This is not `avio_read()` into a fixed raw buffer.**  
     The code uses `avio_read_to_bprint()`, which appends data into an `AVBPrint` string builder. The target bug pattern is about a caller requesting N bytes into a buffer and then assuming all N bytes were written even if the read was short. That exact pattern is absent here.

  2. **The return value is checked for error before use.**  
     The code explicitly tests `if (err < 0)` and bails out. So it is not ignoring the I/O result.

  3. **`bp.str` is treated as text accumulated by the bprint helper, not as a fully-sized binary buffer requiring exact fill.**  
     The subsequent code walks a NUL-terminated string (`while (*cursor)`, `strspn`, `av_get_token`). This is consistent with `AVBPrint` usage: consume the bytes actually collected, not some assumed fixed-size read extent.

  4. **A short read here is not inherently a bug.**  
     Reading until EOF into a growable string buffer is expected behavior for this helper. EOF/shortness just means fewer characters are present in the resulting string. Unlike the target bug pattern, there is no partially initialized tail of a fixed buffer later examined by `memcmp`, field parsing, offset extraction, etc.

  5. **The suggested root cause does not align with the patch pattern described in the bug spec.**  
     A real match would typically be fixed by replacing unchecked `avio_read()` with an exact-size helper like `ffio_read_size()` or by verifying `ret == requested_size` before using the buffer. Nothing in the shown code suggests such a fix would be relevant.

  So this is a **false positive** relative to the target pattern: the analyzer appears to generalize “data came from avio_*” into “buffer may be partially uninitialized,” but the actual API and usage pattern here are different.

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

REGISTER_MAP_WITH_PROGRAMSTATE(ShortReadDstMap, const MemRegion *, const CallExpr *)

namespace {

class SAGenTestChecker
    : public Checker<check::PostCall,
                     check::BranchCondition,
                     check::PreCall,
                     check::Location> {
  mutable std::unique_ptr<BugType> BT;

public:
  SAGenTestChecker()
      : BT(new BugType(this, "Unchecked avio_read result", "API")) {}

  void checkPostCall(const CallEvent &Call, CheckerContext &C) const;
  void checkBranchCondition(const Stmt *Condition, CheckerContext &C) const;
  void checkPreCall(const CallEvent &Call, CheckerContext &C) const;
  void checkLocation(SVal Loc, bool IsLoad, const Stmt *S,
                     CheckerContext &C) const;

private:
  bool isAvioReadCall(const CallExpr *CE, CheckerContext &C) const;
  bool isAvioReadCall(const CallEvent &Call, CheckerContext &C) const;
  bool isMemcmpCall(const CallEvent &Call, CheckerContext &C) const;

  const MemRegion *getTrackedRegionFromExpr(const Expr *E,
                                            CheckerContext &C) const;
  const MemRegion *getTrackedRegionFromAvioReadCall(const CallExpr *CE,
                                                    CheckerContext &C) const;
  const CallExpr *getAvioReadCallFromRetVar(const Expr *CondE,
                                            CheckerContext &C) const;

  bool isSubRegionOf(const MemRegion *Inner, const MemRegion *Outer) const;
  bool regionsOverlapOrSubregion(const MemRegion *A, const MemRegion *B) const;
  bool isTrackedUse(const MemRegion *UseMR, const MemRegion *TrackedMR) const;
  bool isFalsePositive(const MemRegion *UseMR, const MemRegion *TrackedMR,
                       const Stmt *S, CheckerContext &C) const;

  void clearTrackedRegion(const MemRegion *MR, CheckerContext &C) const;
  void reportBug(const MemRegion *MR, const Stmt *S, CheckerContext &C,
                 StringRef Msg) const;
};

bool SAGenTestChecker::isAvioReadCall(const CallExpr *CE,
                                      CheckerContext &C) const {
  if (!CE)
    return false;
  return ExprHasName(CE, "avio_read", C) && !ExprHasName(CE, "ffio_read_size", C);
}

bool SAGenTestChecker::isAvioReadCall(const CallEvent &Call,
                                      CheckerContext &C) const {
  const Expr *OriginExpr = Call.getOriginExpr();
  if (!OriginExpr)
    return false;
  return ExprHasName(OriginExpr, "avio_read", C) &&
         !ExprHasName(OriginExpr, "ffio_read_size", C);
}

bool SAGenTestChecker::isMemcmpCall(const CallEvent &Call,
                                    CheckerContext &C) const {
  const Expr *OriginExpr = Call.getOriginExpr();
  if (!OriginExpr)
    return false;
  return ExprHasName(OriginExpr, "memcmp", C);
}

const MemRegion *
SAGenTestChecker::getTrackedRegionFromExpr(const Expr *E,
                                           CheckerContext &C) const {
  if (!E)
    return nullptr;

  const MemRegion *MR = getMemRegionFromExpr(E, C);
  if (!MR)
    return nullptr;

  return MR;
}

const MemRegion *
SAGenTestChecker::getTrackedRegionFromAvioReadCall(const CallExpr *CE,
                                                   CheckerContext &C) const {
  if (!CE)
    return nullptr;
  if (!isAvioReadCall(CE, C))
    return nullptr;
  if (CE->getNumArgs() != 3)
    return nullptr;

  return getTrackedRegionFromExpr(CE->getArg(1), C);
}

const CallExpr *
SAGenTestChecker::getAvioReadCallFromRetVar(const Expr *CondE,
                                            CheckerContext &C) const {
  if (!CondE)
    return nullptr;

  const DeclRefExpr *DRE = findSpecificTypeInChildren<DeclRefExpr>(CondE);
  if (!DRE)
    return nullptr;

  const VarDecl *VD = dyn_cast<VarDecl>(DRE->getDecl());
  if (!VD)
    return nullptr;

  if (const Expr *Init = VD->getInit()) {
    if (const CallExpr *CE = findSpecificTypeInChildren<CallExpr>(Init)) {
      if (isAvioReadCall(CE, C))
        return CE;
    }
    if (const CallExpr *CE = dyn_cast<CallExpr>(Init->IgnoreParenImpCasts())) {
      if (isAvioReadCall(CE, C))
        return CE;
    }
  }

  return nullptr;
}

bool SAGenTestChecker::isSubRegionOf(const MemRegion *Inner,
                                     const MemRegion *Outer) const {
  if (!Inner || !Outer)
    return false;

  const MemRegion *Cur = Inner;
  while (Cur) {
    if (Cur == Outer)
      return true;
    const SubRegion *SR = dyn_cast<SubRegion>(Cur);
    if (!SR)
      break;
    Cur = SR->getSuperRegion();
  }
  return false;
}

bool SAGenTestChecker::regionsOverlapOrSubregion(const MemRegion *A,
                                                 const MemRegion *B) const {
  if (!A || !B)
    return false;

  if (A == B)
    return true;

  return isSubRegionOf(A, B) || isSubRegionOf(B, A);
}

bool SAGenTestChecker::isTrackedUse(const MemRegion *UseMR,
                                    const MemRegion *TrackedMR) const {
  return regionsOverlapOrSubregion(UseMR, TrackedMR);
}

bool SAGenTestChecker::isFalsePositive(const MemRegion *UseMR,
                                       const MemRegion *TrackedMR,
                                       const Stmt *S,
                                       CheckerContext &C) const {
  if (!UseMR || !TrackedMR || !S)
    return true;

  // The key false-positive pattern: same aggregate base, but different sibling
  // fields/subregions (e.g. ape->md5 tracked, but ape->frames[i].size used).
  if (!regionsOverlapOrSubregion(UseMR, TrackedMR))
    return true;

  return false;
}

void SAGenTestChecker::clearTrackedRegion(const MemRegion *MR,
                                          CheckerContext &C) const {
  if (!MR)
    return;

  ProgramStateRef State = C.getState();
  if (!State->get<ShortReadDstMap>(MR))
    return;

  State = State->remove<ShortReadDstMap>(MR);
  C.addTransition(State);
}

void SAGenTestChecker::reportBug(const MemRegion *MR, const Stmt *S,
                                 CheckerContext &C, StringRef Msg) const {
  if (!MR || !S)
    return;

  ExplodedNode *N = C.generateNonFatalErrorNode();
  if (!N)
    return;

  auto R = std::make_unique<PathSensitiveBugReport>(*BT, Msg, N);
  R->addRange(S->getSourceRange());
  C.emitReport(std::move(R));
}

void SAGenTestChecker::checkPostCall(const CallEvent &Call,
                                     CheckerContext &C) const {
  if (!isAvioReadCall(Call, C))
    return;

  const Expr *OriginExpr = Call.getOriginExpr();
  if (!OriginExpr)
    return;

  const CallExpr *CE = dyn_cast<CallExpr>(OriginExpr->IgnoreParenImpCasts());
  if (!CE)
    return;

  if (CE->getNumArgs() != 3)
    return;

  const MemRegion *DstMR = getTrackedRegionFromExpr(CE->getArg(1), C);
  if (!DstMR)
    return;

  llvm::APSInt RequestedSize;
  (void)EvaluateExprToInt(RequestedSize, CE->getArg(2), C);

  ProgramStateRef State = C.getState();
  State = State->set<ShortReadDstMap>(DstMR, CE);
  C.addTransition(State);
}

void SAGenTestChecker::checkBranchCondition(const Stmt *Condition,
                                            CheckerContext &C) const {
  if (!Condition)
    return;

  const CallExpr *DirectCall = findSpecificTypeInChildren<CallExpr>(Condition);
  if (DirectCall && isAvioReadCall(DirectCall, C)) {
    const MemRegion *DstMR = getTrackedRegionFromAvioReadCall(DirectCall, C);
    if (DstMR)
      clearTrackedRegion(DstMR, C);
    return;
  }

  const Expr *CondE = dyn_cast<Expr>(Condition);
  if (!CondE)
    return;

  const CallExpr *TrackedCall = getAvioReadCallFromRetVar(CondE, C);
  if (!TrackedCall)
    return;

  const MemRegion *DstMR = getTrackedRegionFromAvioReadCall(TrackedCall, C);
  if (!DstMR)
    return;

  clearTrackedRegion(DstMR, C);
}

void SAGenTestChecker::checkPreCall(const CallEvent &Call,
                                    CheckerContext &C) const {
  if (!isMemcmpCall(Call, C))
    return;

  if (Call.getNumArgs() < 2)
    return;

  ProgramStateRef State = C.getState();

  for (unsigned Idx = 0; Idx < 2; ++Idx) {
    const Expr *ArgE = Call.getArgExpr(Idx);
    if (!ArgE)
      continue;

    const MemRegion *UseMR = getTrackedRegionFromExpr(ArgE, C);
    if (!UseMR)
      continue;

    for (const auto &Entry : State->get<ShortReadDstMap>()) {
      const MemRegion *TrackedMR = Entry.first;
      if (!TrackedMR)
        continue;

      if (!isTrackedUse(UseMR, TrackedMR))
        continue;

      if (isFalsePositive(UseMR, TrackedMR, Call.getOriginExpr(), C))
        continue;

      reportBug(TrackedMR, Call.getOriginExpr(),
                C, "buffer read by avio_read may be partially uninitialized");
      return;
    }
  }
}

void SAGenTestChecker::checkLocation(SVal Loc, bool IsLoad, const Stmt *S,
                                     CheckerContext &C) const {
  ProgramStateRef State = C.getState();

  const MemRegion *UseMR = Loc.getAsRegion();
  if (!UseMR)
    return;

  for (const auto &Entry : State->get<ShortReadDstMap>()) {
    const MemRegion *TrackedMR = Entry.first;
    if (!TrackedMR)
      continue;

    if (!isTrackedUse(UseMR, TrackedMR))
      continue;

    if (isFalsePositive(UseMR, TrackedMR, S, C))
      continue;

    if (IsLoad) {
      reportBug(TrackedMR, S, C,
                "buffer read by avio_read may be partially uninitialized");
      return;
    }

    if (const auto *ASE = dyn_cast<ArraySubscriptExpr>(S)) {
      if (!ASE->getBase())
        return;

      const MemRegion *BaseMR = getTrackedRegionFromExpr(ASE->getBase(), C);
      if (!BaseMR)
        return;

      if (isTrackedUse(BaseMR, TrackedMR) &&
          !isFalsePositive(BaseMR, TrackedMR, S, C)) {
        reportBug(TrackedMR, S, C,
                  "buffer read by avio_read may be partially uninitialized");
        return;
      }
    }
  }
}

} // end anonymous namespace

extern "C" void clang_registerCheckers(CheckerRegistry &registry) {
  registry.addChecker<SAGenTestChecker>(
      "custom.SAGenTestChecker",
      "Detects uses of buffers filled by avio_read() without checking for short reads",
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
