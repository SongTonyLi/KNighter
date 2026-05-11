### Report Summary

File:| format/ty.c  
---|---  
Warning:| line 365, column 57  
buffer read by avio_read may be partially uninitialized  
  
### Annotated Source Code


286   |  
287   |     ty->first_audio_pts = AV_NOPTS_VALUE;
288   |     ty->last_audio_pts = AV_NOPTS_VALUE;
289   |     ty->last_video_pts = AV_NOPTS_VALUE;
290   |  
291   |  for (i = 0; i < CHUNK_PEEK_COUNT; i++) {
292   |         avio_read(pb, ty->chunk, CHUNK_SIZE);
293   |  
294   |         ret = analyze_chunk(s, ty->chunk);
295   |  if (ret < 0)
296   |  return ret;
297   |  if (ty->tivo_series != TIVO_SERIES_UNKNOWN &&
298   |             ty->audio_type  != TIVO_AUDIO_UNKNOWN &&
299   |             ty->tivo_type   != TIVO_TYPE_UNKNOWN)
300   |  break;
301   |     }
302   |  
303   |  if (ty->tivo_series == TIVO_SERIES_UNKNOWN ||
304   |         ty->audio_type == TIVO_AUDIO_UNKNOWN ||
305   |         ty->tivo_type == TIVO_TYPE_UNKNOWN)
306   |  return AVERROR_INVALIDDATA;
307   |  
308   |     st = avformat_new_stream(s, NULL);
309   |  if (!st)
310   |  return AVERROR(ENOMEM);
311   |     st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
312   |     st->codecpar->codec_id   = AV_CODEC_ID_MPEG2VIDEO;
313   |     ffstream(st)->need_parsing = AVSTREAM_PARSE_FULL_RAW;
314   |     avpriv_set_pts_info(st, 64, 1, 90000);
315   |  
316   |     ast = avformat_new_stream(s, NULL);
317   |  if (!ast)
318   |  return AVERROR(ENOMEM);
319   |     ast->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
320   |  
321   |  if (ty->audio_type == TIVO_AUDIO_MPEG) {
322   |         ast->codecpar->codec_id = AV_CODEC_ID_MP2;
323   |         ffstream(ast)->need_parsing = AVSTREAM_PARSE_FULL_RAW;
324   |     } else {
325   |         ast->codecpar->codec_id = AV_CODEC_ID_AC3;
326   |     }
327   |     avpriv_set_pts_info(ast, 64, 1, 90000);
328   |  
329   |     ty->first_chunk = 1;
330   |  
331   |     avio_seek(pb, 0, SEEK_SET);
332   |  
333   |  return 0;
334   | }
335   |  
336   | static int get_chunk(AVFormatContext *s)
337   | {
338   |  TYDemuxContext *ty = s->priv_data;
339   |     AVIOContext *pb = s->pb;
340   |  int read_size, num_recs;
341   |  
342   |  ff_dlog(s, "parsing ty chunk #%d\n", ty->cur_chunk);
    5←Taking false branch→
    6←Loop condition is false.  Exiting loop→
343   |  
344   |  /* if we have left-over filler space from the last chunk, get that */
345   |  if (avio_feof(pb))
    7←Assuming the condition is false→
    8←Taking false branch→
346   |  return AVERROR_EOF;
347   |  
348   |  /* read the TY packet header */
349   |  read_size = avio_read(pb, ty->chunk, CHUNK_SIZE);
350   |     ty->cur_chunk++;
351   |  
352   |  if ((read_size < 4) || (AV_RB32(ty->chunk) == 0)) {
    9←Assuming 'read_size' is >= 4→
    10←Assuming the condition is false→
    11←Taking false branch→
353   |  return AVERROR_EOF;
354   |     }
355   |  
356   |  /* check if it's a PART Header */
357   |  if (AV_RB32(ty->chunk) == TIVO_PES_FILEID) {
    12←Assuming the condition is false→
    13←Taking false branch→
358   |  /* skip master chunk and read new chunk */
359   |  return get_chunk(s);
360   |     }
361   |  
362   |  /* number of records in chunk (8- or 16-bit number) */
363   |  if (ty->chunk[3] & 0x80) {
    14←Assuming the condition is true→
    15←Taking true branch→
364   |  /* 16 bit rec cnt */
365   |  ty->num_recs = num_recs = (ty->chunk[1] << 8) + ty->chunk[0];
    16←buffer read by avio_read may be partially uninitialized
366   |     } else {
367   |  /* 8 bit reclen - TiVo 1.3 format */
368   |         ty->num_recs = num_recs = ty->chunk[0];
369   |     }
370   |     ty->cur_rec = 0;
371   |     ty->first_chunk = 0;
372   |  
373   |  ff_dlog(s, "chunk has %d records\n", num_recs);
374   |     ty->cur_chunk_pos = 4;
375   |  
376   |     av_freep(&ty->rec_hdrs);
377   |  
378   |  if (num_recs * 16 >= CHUNK_SIZE - 4)
379   |  return AVERROR_INVALIDDATA;
380   |  
381   |     ty->rec_hdrs = parse_chunk_headers(ty->chunk + 4, num_recs);
382   |  if (!ty->rec_hdrs)
383   |  return AVERROR(ENOMEM);
384   |     ty->cur_chunk_pos += 16 * num_recs;
385   |  
386   |  return 0;
387   | }
388   |  
389   | static int demux_video(AVFormatContext *s, TyRecHdr *rec_hdr, AVPacket *pkt)
390   | {
391   |     TYDemuxContext *ty = s->priv_data;
392   |  const int subrec_type = rec_hdr->subrec_type;
393   |  const int64_t rec_size = rec_hdr->rec_size;
394   |  int es_offset1, ret;
395   |  int got_packet = 0;
605   |  if (check_sync_pes(s, pkt, es_offset1, rec_size) == -1) {
606   |  /* partial PES header found, nothing else.
607   |  * we're done. */
608   |             av_packet_unref(pkt);
609   |  return 0;
610   |         }
611   |     } else if (subrec_type == 0x04) {
612   |  /* SA Audio with no PES Header                      */
613   |  /* ================================================ */
614   |  if ((ret = av_new_packet(pkt, rec_size)) < 0)
615   |  return ret;
616   |         memcpy(pkt->data, ty->chunk + ty->cur_chunk_pos, rec_size);
617   |         ty->cur_chunk_pos += rec_size;
618   |         pkt->stream_index = 1;
619   |         pkt->pts = ty->last_audio_pts;
620   |     } else if (subrec_type == 0x09) {
621   |  if ((ret = av_new_packet(pkt, rec_size)) < 0)
622   |  return ret;
623   |         memcpy(pkt->data, ty->chunk + ty->cur_chunk_pos, rec_size);
624   |         ty->cur_chunk_pos += rec_size ;
625   |         pkt->stream_index = 1;
626   |  
627   |  /* DTiVo AC3 Audio Data with PES Header             */
628   |  /* ================================================ */
629   |         es_offset1 = find_es_header(ty_AC3AudioPacket, pkt->data, 5);
630   |  
631   |  /* Check for complete PES */
632   |  if (check_sync_pes(s, pkt, es_offset1, rec_size) == -1) {
633   |  /* partial PES header found, nothing else.  we're done. */
634   |             av_packet_unref(pkt);
635   |  return 0;
636   |         }
637   |  /* S2 DTivo has invalid long AC3 packets */
638   |  if (ty->tivo_series == TIVO_SERIES2) {
639   |  if (pkt->size > AC3_PKT_LENGTH) {
640   |                 pkt->size -= 2;
641   |                 ty->ac3_pkt_size = 0;
642   |             } else {
643   |                 ty->ac3_pkt_size = pkt->size;
644   |             }
645   |         }
646   |     } else {
647   |  /* Unsupported/Unknown */
648   |         ty->cur_chunk_pos += rec_size;
649   |  return 0;
650   |     }
651   |  
652   |  return 1;
653   | }
654   |  
655   | static int ty_read_packet(AVFormatContext *s, AVPacket *pkt)
656   | {
657   |  TYDemuxContext *ty = s->priv_data;
658   |     AVIOContext *pb = s->pb;
659   |     TyRecHdr *rec;
660   |     int64_t rec_size = 0;
661   |  int ret = 0;
662   |  
663   |  if (avio_feof(pb))
    1Assuming the condition is false→
    2←Taking false branch→
664   |  return AVERROR_EOF;
665   |  
666   |  while (ret <= 0) {
667   |  if (!ty->rec_hdrs || ty->first_chunk || ty->cur_rec >= ty->num_recs) {
    3←Assuming field 'rec_hdrs' is null→
668   |  if (get_chunk(s) < 0 || ty->num_recs <= 0)
    4←Calling 'get_chunk'→
669   |  return AVERROR_EOF;
670   |         }
671   |  
672   |         rec = &ty->rec_hdrs[ty->cur_rec];
673   |         rec_size = rec->rec_size;
674   |         ty->cur_rec++;
675   |  
676   |  if (rec_size <= 0)
677   |  continue;
678   |  
679   |  if (ty->cur_chunk_pos + rec->rec_size > CHUNK_SIZE)
680   |  return AVERROR_INVALIDDATA;
681   |  
682   |  if (avio_feof(pb))
683   |  return AVERROR_EOF;
684   |  
685   |  switch (rec->rec_type) {
686   |  case VIDEO_ID:
687   |             ret = demux_video(s, rec, pkt);
688   |  break;
689   |  case AUDIO_ID:
690   |             ret = demux_audio(s, rec, pkt);
691   |  break;
692   |  default:
693   |  ff_dlog(s, "Invalid record type 0x%02x\n", rec->rec_type);
694   |  case 0x01:
695   |  case 0x02:
696   |  case 0x03: /* TiVo data services */
697   |  case 0x05: /* unknown, but seen regularly */
698   |             ty->cur_chunk_pos += rec->rec_size;