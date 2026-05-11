### Report Summary

File:| /home/cc/ai2sec/KNighter/ffmpeg/./libavcodec/get_bits.h  
---|---  
Warning:| line 394, column 26  
buffer read by avio_read may be partially uninitialized  
  
### Annotated Source Code


261   |  if (ret < 0)
262   |  return ret;
263   |  if (ret != obu_unit_size)
264   |  return AVERROR_INVALIDDATA;
265   |  
266   |     c->temporal_unit_size -= obu_unit_size + len;
267   |     c->frame_unit_size -= obu_unit_size + len;
268   |  
269   | end:
270   |     ret = av_bsf_send_packet(c->bsf, pkt);
271   |  if (ret < 0) {
272   |         av_log(s, AV_LOG_ERROR, "Failed to send packet to "
273   |  "av1_frame_merge filter\n");
274   |  return ret;
275   |     }
276   |  
277   |     ret = av_bsf_receive_packet(c->bsf, pkt);
278   |  if (ret < 0) {
279   |  if (ret == AVERROR(EAGAIN))
280   |  goto retry;
281   |  if (ret != AVERROR_EOF)
282   |             av_log(s, AV_LOG_ERROR, "av1_frame_merge filter failed to "
283   |  "send output packet\n");
284   |  return ret;
285   |     }
286   |  
287   |     pkt->pos = pos;
288   |  
289   |  return 0;
290   | }
291   |  
292   | const FFInputFormat ff_av1_demuxer = {
293   |     .p.name         = "av1",
294   |     .p.long_name    = NULL_IF_CONFIG_SMALL("AV1 Annex B"),
295   |     .p.extensions   = "obu",
296   |     .p.flags        = AVFMT_GENERIC_INDEX | AVFMT_NOTIMESTAMPS,
297   |     .p.priv_class   = &av1_demuxer_class,
298   |     .priv_data_size = sizeof(AV1DemuxContext),
299   |     .flags_internal = FF_INFMT_FLAG_INIT_CLEANUP,
300   |     .read_probe     = annexb_probe,
301   |     .read_header    = av1_read_header,
302   |     .read_packet    = annexb_read_packet,
303   |     .read_close     = av1_read_close,
304   | };
305   | #endif
306   |  
307   | #if CONFIG_OBU_DEMUXER
308   | //For low overhead obu, we can't foresee the obu size before we parsed the header.
309   | //So, we can't use parse_obu_header here, since it will check size <= buf_size
310   | //see c27c7b49dc for more details
311   | static int read_obu_with_size(const uint8_t *buf, int buf_size, int64_t *obu_size, int *type)
312   | {
313   |  GetBitContext gb;
314   |  int ret, extension_flag, start_pos;
315   |     int64_t size;
316   |  
317   |  ret = init_get_bits8(&gb, buf, FFMIN(buf_size, MAX_OBU_HEADER_SIZE));
    10←Assuming the condition is true→
    11←'?' condition is true→
318   |  if (ret11.1'ret' is >= 011.1'ret' is >= 0 < 0)
    12←Taking false branch→
319   |  return ret;
320   |  
321   |  if (get_bits1(&gb) != 0) // obu_forbidden_bit
    13←Calling 'get_bits1'→
322   |  return AVERROR_INVALIDDATA;
323   |  
324   |     *type      = get_bits(&gb, 4);
325   |     extension_flag = get_bits1(&gb);
326   |  if (!get_bits1(&gb))    // has_size_flag
327   |  return AVERROR_INVALIDDATA;
328   |     skip_bits1(&gb);        // obu_reserved_1bit
329   |  
330   |  if (extension_flag) {
331   |         get_bits(&gb, 3);   // temporal_id
332   |         get_bits(&gb, 2);   // spatial_id
333   |         skip_bits(&gb, 3);  // extension_header_reserved_3bits
334   |     }
335   |  
336   |     *obu_size  = get_leb128(&gb);
337   |  if (*obu_size > INT_MAX)
338   |  return AVERROR_INVALIDDATA;
339   |  
340   |  if (get_bits_left(&gb) < 0)
341   |  return AVERROR_INVALIDDATA;
342   |  
343   |     start_pos = get_bits_count(&gb) / 8;
344   |  
345   |     size = *obu_size + start_pos;
346   |  if (size > INT_MAX)
347   |  return AVERROR_INVALIDDATA;
348   |  return size;
349   | }
350   |  
351   | static int obu_probe(const AVProbeData *p)
352   | {
353   |     int64_t obu_size;
354   |  int seq = 0;
355   |  int ret, type, cnt;
356   |  
357   |  // Check that the first OBU is a Temporal Delimiter.
358   |     cnt = read_obu_with_size(p->buf, p->buf_size, &obu_size, &type);
359   |  if (cnt < 0 || type != AV1_OBU_TEMPORAL_DELIMITER || obu_size != 0)
360   |  return 0;
361   |  
362   |  while (1) {
363   |         ret = read_obu_with_size(p->buf + cnt, p->buf_size - cnt, &obu_size, &type);
364   |  if (ret < 0 || obu_size <= 0)
365   |  return 0;
366   |         cnt += FFMIN(ret, p->buf_size - cnt);
367   |  
368   |         ret = get_score(type, &seq);
369   |  if (ret >= 0)
370   |  return ret;
371   |     }
372   |  return 0;
373   | }
374   |  
375   | static int obu_get_packet(AVFormatContext *s, AVPacket *pkt)
376   | {
377   |  AV1DemuxContext *const c = s->priv_data;
378   |     uint8_t header[MAX_OBU_HEADER_SIZE + AV_INPUT_BUFFER_PADDING_SIZE];
379   |     int64_t obu_size;
380   |  int size;
381   |  int ret, len, type;
382   |  
383   |  if ((ret = ffio_ensure_seekback(s->pb, MAX_OBU_HEADER_SIZE)) < 0)
    5←Assuming the condition is false→
    6←Taking false branch→
384   |  return ret;
385   |  size = avio_read(s->pb, header, MAX_OBU_HEADER_SIZE);
386   |  if (size < 0)
    7←Assuming 'size' is >= 0→
    8←Taking false branch→
387   |  return size;
388   |  
389   |  memset(header + size, 0, AV_INPUT_BUFFER_PADDING_SIZE);
390   |  len = read_obu_with_size(header, size, &obu_size, &type);
    9←Calling 'read_obu_with_size'→
391   |  if (len < 0) {
392   |         av_log(c, AV_LOG_ERROR, "Failed to read obu\n");
393   |  return len;
394   |     }
395   |     avio_seek(s->pb, -size, SEEK_CUR);
396   |  
397   |     ret = av_get_packet(s->pb, pkt, len);
398   |  if (ret != len) {
399   |         av_log(c, AV_LOG_ERROR, "Failed to get packet for obu\n");
400   |  return ret < 0 ? ret : AVERROR_INVALIDDATA;
401   |     }
402   |  return 0;
403   | }
404   |  
405   | static int obu_read_packet(AVFormatContext *s, AVPacket *pkt)
406   | {
407   |  AV1DemuxContext *const c = s->priv_data;
408   |  int ret;
409   |  
410   |  if (s->io_repositioned) {
    1Assuming field 'io_repositioned' is 0→
    2←Taking false branch→
411   |         av_bsf_flush(c->bsf);
412   |         s->io_repositioned = 0;
413   |     }
414   |  while (1) {
    3←Loop condition is true.  Entering loop body→
415   |  ret = obu_get_packet(s, pkt);
    4←Calling 'obu_get_packet'→
416   |  /* In case of AVERROR_EOF we need to flush the BSF. Conveniently
417   |  * obu_get_packet() returns a blank pkt in this case which
418   |  * can be used to signal that the BSF should be flushed. */
419   |  if (ret < 0 && ret != AVERROR_EOF)
420   |  return ret;
421   |         ret = av_bsf_send_packet(c->bsf, pkt);
422   |  if (ret < 0) {
423   |             av_log(s, AV_LOG_ERROR, "Failed to send packet to "
424   |  "av1_frame_merge filter\n");
425   |  return ret;
426   |         }
427   |         ret = av_bsf_receive_packet(c->bsf, pkt);
428   |  if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
429   |             av_log(s, AV_LOG_ERROR, "av1_frame_merge filter failed to "
430   |  "send output packet\n");
431   |  if (ret != AVERROR(EAGAIN))
432   |  break;
433   |     }
434   |  
435   |  return ret;
436   | }
437   |  
438   | const FFInputFormat ff_obu_demuxer = {
439   |     .p.name         = "obu",
440   |     .p.long_name    = NULL_IF_CONFIG_SMALL("AV1 low overhead OBU"),
441   |     .p.extensions   = "obu",
442   |     .p.flags        = AVFMT_GENERIC_INDEX | AVFMT_NO_BYTE_SEEK | AVFMT_NOTIMESTAMPS,
443   |     .p.priv_class   = &av1_demuxer_class,
444   |     .priv_data_size = sizeof(AV1DemuxContext),
445   |     .flags_internal = FF_INFMT_FLAG_INIT_CLEANUP,
341   |  av_assert2(n>0 && n<=25);
342   |  UPDATE_CACHE(re, s);
343   |     tmp = SHOW_UBITS(re, s, n);
344   |  LAST_SKIP_BITS(re, s, n);
345   |  CLOSE_READER(re, s);
346   |  av_assert2(tmp < UINT64_C(1) << n);
347   |  return tmp;
348   | }
349   |  
350   | /**
351   |  * Read 0-25 bits.
352   |  */
353   | static av_always_inline int get_bitsz(GetBitContext *s, int n)
354   | {
355   |  return n ? get_bits(s, n) : 0;
356   | }
357   |  
358   | static inline unsigned int get_bits_le(GetBitContext *s, int n)
359   | {
360   |  register int tmp;
361   |  OPEN_READER(re, s);
362   |  av_assert2(n>0 && n<=25);
363   |  UPDATE_CACHE_LE(re, s);
364   |     tmp = SHOW_UBITS_LE(re, s, n);
365   |  LAST_SKIP_BITS(re, s, n);
366   |  CLOSE_READER(re, s);
367   |  return tmp;
368   | }
369   |  
370   | /**
371   |  * Show 1-25 bits.
372   |  */
373   | static inline unsigned int show_bits(GetBitContext *s, int n)
374   | {
375   |  register unsigned int tmp;
376   |  OPEN_READER_NOSIZE(re, s);
377   |  av_assert2(n>0 && n<=25);
378   |  UPDATE_CACHE(re, s);
379   |     tmp = SHOW_UBITS(re, s, n);
380   |  return tmp;
381   | }
382   |  
383   | static inline void skip_bits(GetBitContext *s, int n)
384   | {
385   |  OPEN_READER_NOSIZE_NOCACHE(re, s);
386   |  OPEN_READER_SIZE(re, s);
387   |  LAST_SKIP_BITS(re, s, n);
388   |  CLOSE_READER(re, s);
389   | }
390   |  
391   | static inline unsigned int get_bits1(GetBitContext *s)
392   | {
393   |  unsigned int index = s->index;
394   |  uint8_t result     = s->buffer[index >> 3];
    14←buffer read by avio_read may be partially uninitialized
395   | #ifdef BITSTREAM_READER_LE
396   |     result >>= index & 7;
397   |     result  &= 1;
398   | #else
399   |     result <<= index & 7;
400   |     result >>= 8 - 1;
401   | #endif
402   | #if !UNCHECKED_BITSTREAM_READER
403   |  if (s->index < s->size_in_bits_plus8)
404   | #endif
405   |         index++;
406   |     s->index = index;
407   |  
408   |  return result;
409   | }
410   |  
411   | static inline unsigned int show_bits1(GetBitContext *s)
412   | {
413   |  return show_bits(s, 1);
414   | }
415   |  
416   | static inline void skip_bits1(GetBitContext *s)
417   | {
418   |     skip_bits(s, 1);
419   | }
420   |  
421   | /**
422   |  * Read 0-32 bits.
423   |  */
424   | static inline unsigned int get_bits_long(GetBitContext *s, int n)
467   | #endif
468   |     }
469   | }
470   |  
471   | /**
472   |  * Read 0-32 bits as a signed integer.
473   |  */
474   | static inline int get_sbits_long(GetBitContext *s, int n)
475   | {
476   |  // sign_extend(x, 0) is undefined
477   |  if (!n)
478   |  return 0;
479   |  
480   |  return sign_extend(get_bits_long(s, n), n);
481   | }
482   |  
483   | /**
484   |  * Read 0-64 bits as a signed integer.
485   |  */
486   | static inline int64_t get_sbits64(GetBitContext *s, int n)
487   | {
488   |  // sign_extend(x, 0) is undefined
489   |  if (!n)
490   |  return 0;
491   |  
492   |  return sign_extend64(get_bits64(s, n), n);
493   | }
494   |  
495   | /**
496   |  * Show 0-32 bits.
497   |  */
498   | static inline unsigned int show_bits_long(GetBitContext *s, int n)
499   | {
500   |  if (n <= MIN_CACHE_BITS) {
501   |  return show_bits(s, n);
502   |     } else {
503   |         GetBitContext gb = *s;
504   |  return get_bits_long(&gb, n);
505   |     }
506   | }
507   |  
508   |  
509   | /**
510   |  * Initialize GetBitContext.
511   |  * @param buffer bitstream buffer, must be AV_INPUT_BUFFER_PADDING_SIZE bytes
512   |  *        larger than the actual read bits because some optimized bitstream
513   |  *        readers read 32 or 64 bit at once and could read over the end
514   |  * @param bit_size the size of the buffer in bits
515   |  * @return 0 on success, AVERROR_INVALIDDATA if the buffer_size would overflow.
516   |  */
517   | static inline int init_get_bits(GetBitContext *s, const uint8_t *buffer,
518   |  int bit_size)
519   | {
520   |  int ret = 0;
521   |  
522   |  if (bit_size >= INT_MAX - FFMAX(7, AV_INPUT_BUFFER_PADDING_SIZE*8) || bit_size < 0 || !buffer) {
523   |         bit_size    = 0;
524   |         buffer      = NULL;
525   |         ret         = AVERROR_INVALIDDATA;
526   |     }
527   |  
528   |     s->buffer             = buffer;
529   |     s->size_in_bits       = bit_size;
530   |     s->size_in_bits_plus8 = bit_size + 8;
531   |     s->index              = 0;
532   |  
533   |  return ret;
534   | }
535   |  
536   | /**
537   |  * Initialize GetBitContext.
538   |  * @param buffer bitstream buffer, must be AV_INPUT_BUFFER_PADDING_SIZE bytes
539   |  *        larger than the actual read bits because some optimized bitstream
540   |  *        readers read 32 or 64 bit at once and could read over the end
541   |  * @param byte_size the size of the buffer in bytes
542   |  * @return 0 on success, AVERROR_INVALIDDATA if the buffer_size would overflow.
543   |  */
544   | static inline int init_get_bits8(GetBitContext *s, const uint8_t *buffer,
545   |  int byte_size)
546   | {
547   |  if (byte_size > INT_MAX / 8 || byte_size < 0)
548   |         byte_size = -1;
549   |  return init_get_bits(s, buffer, byte_size * 8);
550   | }
551   |  
552   | static inline int init_get_bits8_le(GetBitContext *s, const uint8_t *buffer,
553   |  int byte_size)
554   | {
555   |  if (byte_size > INT_MAX / 8 || byte_size < 0)
556   |         byte_size = -1;
557   |  return init_get_bits(s, buffer, byte_size * 8);
558   | }
559   |  
560   | static inline const uint8_t *align_get_bits(GetBitContext *s)
561   | {
562   |  int n = -get_bits_count(s) & 7;
563   |  if (n)
564   |         skip_bits(s, n);
565   |  return s->buffer + (s->index >> 3);
566   | }
567   |  
568   | /**
569   |  * If the vlc code is invalid and max_depth=1, then no bits will be removed.
570   |  * If the vlc code is invalid and max_depth>1, then the number of bits removed
571   |  * is undefined.
572   |  */
573   | #define GET_VLC(code, name, gb, table, bits, max_depth)         \
574   |  do {                                                        \
575   |  unsigned idx_ = SHOW_UBITS(name, gb, bits);             \
576   |  code          = table[idx_].sym;                        \
577   |  int        n_ = table[idx_].len;                        \
578   |  \
579   |  if (max_depth > 1 && n_ < 0) {                          \