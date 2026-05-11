# FFmpeg-Specific Utility Knowledge for CSA Checker Development

## Key FFmpeg Memory Management APIs

FFmpeg uses its own allocator family. These are the primary allocation and
deallocation functions you will encounter in bug patterns:

```c
// Allocation (may return NULL on failure)
void *av_malloc(size_t size);
void *av_mallocz(size_t size);          // zero-initialized
void *av_realloc(void *ptr, size_t size);
void *av_calloc(size_t nmemb, size_t size);
char *av_strdup(const char *s);
char *av_strndup(const char *s, size_t len);

// Deallocation
void av_free(void *ptr);
void av_freep(void **ptr);              // frees and NULLs the pointer

// Buffer references (ref-counted)
AVBufferRef *av_buffer_alloc(size_t size);
AVBufferRef *av_buffer_ref(AVBufferRef *buf);
void        av_buffer_unref(AVBufferRef **buf);  // NULLs *buf after free

// Packet lifecycle
void av_packet_unref(AVPacket *pkt);
AVPacket *av_packet_alloc(void);
void av_packet_free(AVPacket **pkt);

// Frame lifecycle
AVFrame *av_frame_alloc(void);
void av_frame_free(AVFrame **frame);    // NULLs *frame after free
void av_frame_unref(AVFrame *frame);
int  av_frame_ref(AVFrame *dst, const AVFrame *src);
```

## Error Handling Convention

FFmpeg functions return negative AVERROR codes on failure:

```c
ret = some_function(...);
if (ret < 0)          // generic failure check
    goto fail;

if (!ptr)             // allocation failure (returns NULL)
    return AVERROR(ENOMEM);
```

Common error codes: `AVERROR(ENOMEM)`, `AVERROR(EINVAL)`, `AVERROR_EOF`,
`AVERROR(EAGAIN)`, `AVERROR_INVALIDDATA`.

## AVCodecContext Lifetime

```c
AVCodecContext *avctx = avcodec_alloc_context3(codec);
// ... configure avctx ...
avcodec_open2(avctx, codec, &opts);
// ... use avctx ...
avcodec_free_context(&avctx);   // also closes codec; NULLs avctx
```

`avcodec_free_context` calls `avcodec_close` internally; calling both is a
double-free pattern.

## AVFormatContext Lifetime

```c
AVFormatContext *fmt_ctx = avformat_alloc_context();
avformat_open_input(&fmt_ctx, url, NULL, NULL);
// ...
avformat_close_input(&fmt_ctx);   // NULLs fmt_ctx
// OR (if open_input was never called):
avformat_free_context(fmt_ctx);
```

Calling `avformat_free_context` after `avformat_close_input` is a double-free.

## AVIOContext

```c
AVIOContext *avio = avio_alloc_context(buf, buf_size, ...);
// ...
av_freep(&avio->buffer);   // must free internal buffer first
avio_context_free(&avio);  // then free context
```

Forgetting to free `avio->buffer` before `avio_context_free` leaks memory.

## RefStruct API (newer FFmpeg)

```c
void *ff_refstruct_alloc_ext(size_t size, unsigned flags,
                              void *opaque, void (*free_cb)(FFRefStructOpaque, void *));
void ff_refstruct_ref(void *obj);
void ff_refstruct_unref(void *objp);
```

## CSA Modeling Tips

- **av_freep** is equivalent to `free(*ptr); *ptr = NULL;`. Model the symbol as
  released after the call.
- **av_buffer_unref / av_frame_free / av_packet_free** all NULL their argument
  after releasing; the pointer itself is safe to use after the call (it's NULL),
  but the old pointee is gone.
- **NULL return from av_malloc / av_frame_alloc**: always check for NULL before
  use. Missing NULL checks are a common Null-Pointer-Dereference source.
- **goto fail** patterns: FFmpeg heavily uses `goto fail` for cleanup. A checker
  looking for resource leaks should track allocations across goto paths.
- **AVERROR(ENOMEM) propagation**: when an allocation fails, code should return
  AVERROR(ENOMEM) upward. Bugs occur when the error is swallowed and the NULL
  pointer is used anyway.
