# Best Practices for Writing CSA Checkers Targeting FFmpeg

## 1. Tracking av_malloc / av_freep pairs

FFmpeg code allocates with `av_malloc`/`av_mallocz` and frees with either
`av_free` or `av_freep`. Use `checkPostCall` or `checkPreCall` to model these:

```cpp
// In checkPostCall: mark returned pointer as "owned" in program state
if (Call.getCalleeIdentifier()->getName() == "av_malloc" ||
    Call.getCalleeIdentifier()->getName() == "av_mallocz") {
    SVal RetVal = C.getSVal(Call.getOriginExpr());
    // track symbol as allocated
}

// In checkPreCall: mark symbol as freed when av_free / av_freep is called
if (Call.getCalleeIdentifier()->getName() == "av_freep") {
    // first arg is void**, dereference to get the freed pointer
}
```

## 2. Detecting Missing NULL Checks After Allocation

The most common FFmpeg bug: allocation result used without NULL check.

```cpp
void checkPostCall(const CallEvent &Call, CheckerContext &C) const {
    if (!isFFmpegAlloc(Call)) return;
    SVal RetVal = C.getSVal(Call.getOriginExpr());
    SymbolRef Sym = RetVal.getAsSymbol();
    if (!Sym) return;
    // Split state: assume non-null path continues, null path reports
    ProgramStateRef NonNull, Null;
    std::tie(NonNull, Null) = C.getState()->assume(
        RetVal.castAs<DefinedOrUnknownSVal>());
    if (Null) {
        // Report if caller immediately dereferences without checking
    }
}
```

## 3. goto fail Cleanup Paths

FFmpeg's `goto fail` pattern means cleanup happens in a block at the end of a
function. A checker should track state across jumps. Use `checkBranchCondition`
to detect when error paths skip cleanup:

- On error branch (`ret < 0`): verify all previously allocated resources have
  a matching free before the return or goto.
- On success branch: verify resources are not freed prematurely.

## 4. Reference-Counted Objects (AVFrame, AVPacket, AVBufferRef)

These are freed via `av_frame_free`, `av_packet_free`, `av_buffer_unref` — all
take a pointer-to-pointer and NULL it. Do NOT model them the same as `av_free`:

```cpp
// av_frame_free(&frame) — after this, frame == NULL
// Accessing frame->data[] afterwards is a use-after-free
```

Track the symbol stored in `frame` (the frame pointer value), not the address
of the `frame` variable.

## 5. Double-Free Detection

FFmpeg has several contexts where a high-level free (e.g. `avcodec_free_context`)
calls a low-level free (e.g. `avcodec_close`) internally, but callers also call
both:

```cpp
// BAD pattern
avcodec_close(avctx);
avcodec_free_context(&avctx);  // double-free: free_context calls close internally
```

Model these wrapper relationships explicitly — when `avcodec_free_context` is
seen, mark the codec context and its internal state as released.

## 6. AVIOContext Buffer Leak

A very common pattern: `avio_alloc_context` allocates an internal buffer. If
the caller then calls `avio_context_free` without first freeing `ctx->buffer`,
the buffer leaks. Detect this by checking if `av_freep(&ctx->buffer)` was
called before `avio_context_free(&ctx)`.

## 7. Macro Expansion

FFmpeg uses macros like `AVERROR`, `FF_ARRAY_ELEMS`, `av_assert0`. Use
`getNameAsString()` on callee decls rather than string comparison on source
text to avoid macro-expansion mismatches.

## 8. Checker Registration

Always use `custom.SAGenTestChecker` as the checker name and follow the
standard registration pattern from `template.md`. FFmpeg source files are C,
not C++, so the checker analyzes C translation units — stick to C-compatible
AST node types (no `CXXMemberCallExpr`, etc.).
