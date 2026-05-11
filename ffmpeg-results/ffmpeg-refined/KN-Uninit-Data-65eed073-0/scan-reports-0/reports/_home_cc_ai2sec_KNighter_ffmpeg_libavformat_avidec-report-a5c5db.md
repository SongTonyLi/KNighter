### Report Summary

File:| /home/cc/ai2sec/KNighter/ffmpeg/libavformat/avidec.c  
---|---  
Warning:| line 346, column 16  
buffer read by avio_read may be partially uninitialized  
  
### Annotated Source Code


283   |         AVIStream *ast = st->priv_data;
284   |  int n          = st->internal->nb_index_entries;
285   |  int max        = ast->sample_size;
286   |         int64_t pos, size, ts;
287   |  
288   |  if (n != 1 || ast->sample_size == 0)
289   |  continue;
290   |  
291   |  while (max < 1024)
292   |             max += max;
293   |  
294   |         pos  = st->internal->index_entries[0].pos;
295   |         size = st->internal->index_entries[0].size;
296   |         ts   = st->internal->index_entries[0].timestamp;
297   |  
298   |  for (j = 0; j < size; j += max)
299   |             av_add_index_entry(st, pos + j, ts + j, FFMIN(max, size - j), 0,
300   |  AVINDEX_KEYFRAME);
301   |     }
302   | }
303   |  
304   | static int avi_read_tag(AVFormatContext *s, AVStream *st, uint32_t tag,
305   |                         uint32_t size)
306   | {
307   |     AVIOContext *pb = s->pb;
308   |  char key[5]     = { 0 };
309   |  char *value;
310   |  
311   |     size += (size & 1);
312   |  
313   |  if (size == UINT_MAX)
314   |  return AVERROR(EINVAL);
315   |     value = av_malloc(size + 1);
316   |  if (!value)
317   |  return AVERROR(ENOMEM);
318   |  if (avio_read(pb, value, size) != size) {
319   |         av_freep(&value);
320   |  return AVERROR_INVALIDDATA;
321   |     }
322   |     value[size] = 0;
323   |  
324   |  AV_WL32(key, tag);
325   |  
326   |  return av_dict_set(st ? &st->metadata : &s->metadata, key, value,
327   |  AV_DICT_DONT_STRDUP_VAL);
328   | }
329   |  
330   | static const char months[12][4] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
331   |  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
332   |  
333   | static void avi_metadata_creation_time(AVDictionary **metadata, char *date)
334   | {
335   |  char month[4], time[9], buffer[64];
336   |  int i, day, year;
337   |  /* parse standard AVI date format (ie. "Mon Mar 10 15:04:43 2003") */
338   |  if (sscanf(date, "%*3s%*[ ]%3s%*[ ]%2d%*[ ]%8s%*[ ]%4d",
    13←Assuming the condition is false→
339   |  month, &day, time, &year) == 4) {
340   |  for (i = 0; i < 12; i++)
341   |  if (!av_strcasecmp(month, months[i])) {
342   |                 snprintf(buffer, sizeof(buffer), "%.4d-%.2d-%.2d %s",
343   |                          year, i + 1, day, time);
344   |                 av_dict_set(metadata, "creation_time", buffer, 0);
345   |             }
346   |     } else if (date[4] == '/' && date[7] == '/') {
    14←buffer read by avio_read may be partially uninitialized
347   |         date[4] = date[7] = '-';
348   |         av_dict_set(metadata, "creation_time", date, 0);
349   |     }
350   | }
351   |  
352   | static void avi_read_nikon(AVFormatContext *s, uint64_t end)
353   | {
354   |  while (avio_tell(s->pb) < end && !avio_feof(s->pb)) {
355   |         uint32_t tag  = avio_rl32(s->pb);
356   |         uint32_t size = avio_rl32(s->pb);
357   |  switch (tag) {
358   |  case MKTAG('n', 'c', 't', 'g'):  /* Nikon Tags */
359   |         {
360   |             uint64_t tag_end = avio_tell(s->pb) + size;
361   |  while (avio_tell(s->pb) < tag_end && !avio_feof(s->pb)) {
362   |                 uint16_t tag     = avio_rl16(s->pb);
363   |                 uint16_t size    = avio_rl16(s->pb);
364   |  const char *name = NULL;
365   |  char buffer[64]  = { 0 };
366   |                 size = FFMIN(size, tag_end - avio_tell(s->pb));
367   |                 size -= avio_read(s->pb, buffer,
368   |  FFMIN(size, sizeof(buffer) - 1));
369   |  switch (tag) {
370   |  case 0x03:
371   |                     name = "maker";
372   |  break;
373   |  case 0x04:
374   |                     name = "model";
375   |  break;
376   |  case 0x13:
428   |     }
429   |  
430   |  return 0;
431   | }
432   |  
433   | static int calculate_bitrate(AVFormatContext *s)
434   | {
435   |     AVIContext *avi = s->priv_data;
436   |  int i, j;
437   |     int64_t lensum = 0;
438   |     int64_t maxpos = 0;
439   |  
440   |  for (i = 0; i<s->nb_streams; i++) {
441   |         int64_t len = 0;
442   |         AVStream *st = s->streams[i];
443   |  
444   |  if (!st->internal->nb_index_entries)
445   |  continue;
446   |  
447   |  for (j = 0; j < st->internal->nb_index_entries; j++)
448   |             len += st->internal->index_entries[j].size;
449   |         maxpos = FFMAX(maxpos, st->internal->index_entries[j-1].pos);
450   |         lensum += len;
451   |     }
452   |  if (maxpos < av_rescale(avi->io_fsize, 9, 10)) // index does not cover the whole file
453   |  return 0;
454   |  if (lensum*9/10 > maxpos || lensum < maxpos*9/10) // frame sum and filesize mismatch
455   |  return 0;
456   |  
457   |  for (i = 0; i<s->nb_streams; i++) {
458   |         int64_t len = 0;
459   |         AVStream *st = s->streams[i];
460   |         int64_t duration;
461   |         int64_t bitrate;
462   |  
463   |  for (j = 0; j < st->internal->nb_index_entries; j++)
464   |             len += st->internal->index_entries[j].size;
465   |  
466   |  if (st->internal->nb_index_entries < 2 || st->codecpar->bit_rate > 0)
467   |  continue;
468   |         duration = st->internal->index_entries[j-1].timestamp - st->internal->index_entries[0].timestamp;
469   |         bitrate = av_rescale(8*len, st->time_base.den, duration * st->time_base.num);
470   |  if (bitrate > 0) {
471   |             st->codecpar->bit_rate = bitrate;
472   |         }
473   |     }
474   |  return 1;
475   | }
476   |  
477   | #define RETURN_ERROR(code) do { ret = (code); goto fail; } while (0)
478   | static int avi_read_header(AVFormatContext *s)
479   | {
480   |  AVIContext *avi = s->priv_data;
481   |     AVIOContext *pb = s->pb;
482   |  unsigned int tag, tag1, handler;
483   |  int codec_type, stream_index, frame_period;
484   |  unsigned int size;
485   |  int i;
486   |     AVStream *st;
487   |     AVIStream *ast      = NULL;
488   |  int avih_width      = 0, avih_height = 0;
489   |  int amv_file_format = 0;
490   |     uint64_t list_end   = 0;
491   |     int64_t pos;
492   |  int ret;
493   |     AVDictionaryEntry *dict_entry;
494   |  
495   |     avi->stream_index = -1;
496   |  
497   |     ret = get_riff(s, pb);
498   |  if (ret < 0)
    1Assuming 'ret' is >= 0→
    2←Taking false branch→
499   |  return ret;
500   |  
501   |  av_log(avi, AV_LOG_DEBUG, "use odml:%d\n", avi->use_odml);
502   |  
503   |     avi->io_fsize = avi->fsize = avio_size(pb);
504   |  if (avi->fsize <= 0 || avi->fsize < avi->riff_end)
    3←Assuming field 'fsize' is > 0→
    4←Assuming field 'fsize' is >= field 'riff_end'→
    5←Taking false branch→
505   |         avi->fsize = avi->riff_end == 8 ? INT64_MAX : avi->riff_end;
506   |  
507   |  /* first list tag */
508   |  stream_index = -1;
509   |     codec_type   = -1;
510   |     frame_period = 0;
511   |  for (;;) {
    6←Loop condition is true.  Entering loop body→
512   |  if (avio_feof(pb))
    7←Assuming the condition is false→
    8←Taking false branch→
513   |  RETURN_ERROR(AVERROR_INVALIDDATA);
514   |  tag  = avio_rl32(pb);
515   |         size = avio_rl32(pb);
516   |  
517   |  print_tag(s, "tag", tag, size);
518   |  
519   |  switch (tag) {
    9←Control jumps to 'case 1414087753:'  at line 541→
520   |  case MKTAG('L', 'I', 'S', 'T'):
521   |             list_end = avio_tell(pb) + size;
522   |  /* Ignored, except at start of video packets. */
523   |             tag1 = avio_rl32(pb);
524   |  
525   |  print_tag(s, "list", tag1, 0);
526   |  
527   |  if (tag1 == MKTAG('m', 'o', 'v', 'i')) {
528   |                 avi->movi_list = avio_tell(pb) - 4;
529   |  if (size)
530   |                     avi->movi_end = avi->movi_list + size + (size & 1);
531   |  else
532   |                     avi->movi_end = avi->fsize;
533   |                 av_log(s, AV_LOG_TRACE, "movi end=%"PRIx64"\n", avi->movi_end);
534   |  goto end_of_header;
535   |             } else if (tag1 == MKTAG('I', 'N', 'F', 'O'))
536   |                 ff_read_riff_info(s, size - 4);
537   |  else if (tag1 == MKTAG('n', 'c', 'd', 't'))
538   |                 avi_read_nikon(s, list_end);
539   |  
540   |  break;
541   |  case MKTAG('I', 'D', 'I', 'T'):
542   |         {
543   |  unsigned char date[64] = { 0 };
544   |             size += (size & 1);
545   |  size -= avio_read(pb, date, FFMIN(size, sizeof(date) - 1));
    10←Assuming the condition is true→
    11←'?' condition is true→
546   |             avio_skip(pb, size);
547   |  avi_metadata_creation_time(&s->metadata, date);
    12←Calling 'avi_metadata_creation_time'→
548   |  break;
549   |         }
550   |  case MKTAG('d', 'm', 'l', 'h'):
551   |             avi->is_odml = 1;
552   |             avio_skip(pb, size + (size & 1));
553   |  break;
554   |  case MKTAG('a', 'm', 'v', 'h'):
555   |             amv_file_format = 1;
556   |  case MKTAG('a', 'v', 'i', 'h'):
557   |  /* AVI header */
558   |  /* using frame_period is bad idea */
559   |             frame_period = avio_rl32(pb);
560   |             avio_rl32(pb); /* max. bytes per second */
561   |             avio_rl32(pb);
562   |             avi->non_interleaved |= avio_rl32(pb) & AVIF_MUSTUSEINDEX;
563   |  
564   |             avio_skip(pb, 2 * 4);
565   |             avio_rl32(pb);
566   |             avio_rl32(pb);
567   |             avih_width  = avio_rl32(pb);
568   |             avih_height = avio_rl32(pb);
569   |  
570   |             avio_skip(pb, size - 10 * 4);
571   |  break;
572   |  case MKTAG('s', 't', 'r', 'h'):
573   |  /* stream header */
574   |  
575   |             tag1    = avio_rl32(pb);
576   |             handler = avio_rl32(pb); /* codec tag */
577   |  