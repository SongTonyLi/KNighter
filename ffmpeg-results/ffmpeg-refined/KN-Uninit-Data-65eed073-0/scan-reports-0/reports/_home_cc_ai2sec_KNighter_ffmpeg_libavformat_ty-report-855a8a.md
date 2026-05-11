### Report Summary

File:| /home/cc/ai2sec/KNighter/ffmpeg/libavformat/ty.c  
---|---  
Warning:| line 361, column 9  
buffer read by avio_read may be partially uninitialized  
  
### Annotated Source Code


284   |  
285   |     ty->first_audio_pts = AV_NOPTS_VALUE;
286   |     ty->last_audio_pts = AV_NOPTS_VALUE;
287   |     ty->last_video_pts = AV_NOPTS_VALUE;
288   |  
289   |  for (i = 0; i < CHUNK_PEEK_COUNT; i++) {
290   |         avio_read(pb, ty->chunk, CHUNK_SIZE);
291   |  
292   |         ret = analyze_chunk(s, ty->chunk);
293   |  if (ret < 0)
294   |  return ret;
295   |  if (ty->tivo_series != TIVO_SERIES_UNKNOWN &&
296   |             ty->audio_type  != TIVO_AUDIO_UNKNOWN &&
297   |             ty->tivo_type   != TIVO_TYPE_UNKNOWN)
298   |  break;
299   |     }
300   |  
301   |  if (ty->tivo_series == TIVO_SERIES_UNKNOWN ||
302   |         ty->audio_type == TIVO_AUDIO_UNKNOWN ||
303   |         ty->tivo_type == TIVO_TYPE_UNKNOWN)
304   |  return AVERROR(EIO);
305   |  
306   |     st = avformat_new_stream(s, NULL);
307   |  if (!st)
308   |  return AVERROR(ENOMEM);
309   |     st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
310   |     st->codecpar->codec_id   = AV_CODEC_ID_MPEG2VIDEO;
311   |     st->internal->need_parsing         = AVSTREAM_PARSE_FULL_RAW;
312   |     avpriv_set_pts_info(st, 64, 1, 90000);
313   |  
314   |     ast = avformat_new_stream(s, NULL);
315   |  if (!ast)
316   |  return AVERROR(ENOMEM);
317   |     ast->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
318   |  
319   |  if (ty->audio_type == TIVO_AUDIO_MPEG) {
320   |         ast->codecpar->codec_id = AV_CODEC_ID_MP2;
321   |         ast->internal->need_parsing       = AVSTREAM_PARSE_FULL_RAW;
322   |     } else {
323   |         ast->codecpar->codec_id = AV_CODEC_ID_AC3;
324   |     }
325   |     avpriv_set_pts_info(ast, 64, 1, 90000);
326   |  
327   |     ty->first_chunk = 1;
328   |  
329   |     avio_seek(pb, 0, SEEK_SET);
330   |  
331   |  return 0;
332   | }
333   |  
334   | static int get_chunk(AVFormatContext *s)
335   | {
336   |  TYDemuxContext *ty = s->priv_data;
337   |     AVIOContext *pb = s->pb;
338   |  int read_size, num_recs;
339   |  
340   |  ff_dlog(s, "parsing ty chunk #%d\n", ty->cur_chunk);
    5←Taking false branch→
    6←Loop condition is false.  Exiting loop→
341   |  
342   |  /* if we have left-over filler space from the last chunk, get that */
343   |  if (avio_feof(pb))
    7←Assuming the condition is false→
    8←Taking false branch→
344   |  return AVERROR_EOF;
345   |  
346   |  /* read the TY packet header */
347   |  read_size = avio_read(pb, ty->chunk, CHUNK_SIZE);
348   |     ty->cur_chunk++;
349   |  
350   |  if ((read_size < 4) || (AV_RB32(ty->chunk) == 0)) {
    9←Assuming 'read_size' is >= 4→
    10←Assuming the condition is false→
    11←Taking false branch→
351   |  return AVERROR_EOF;
352   |     }
353   |  
354   |  /* check if it's a PART Header */
355   |  if (AV_RB32(ty->chunk) == TIVO_PES_FILEID) {
    12←Assuming the condition is false→
    13←Taking false branch→
356   |  /* skip master chunk and read new chunk */
357   |  return get_chunk(s);
358   |     }
359   |  
360   |  /* number of records in chunk (8- or 16-bit number) */
361   |  if (ty->chunk[3] & 0x80) {
    14←buffer read by avio_read may be partially uninitialized
362   |  /* 16 bit rec cnt */
363   |         ty->num_recs = num_recs = (ty->chunk[1] << 8) + ty->chunk[0];
364   |     } else {
365   |  /* 8 bit reclen - TiVo 1.3 format */
366   |         ty->num_recs = num_recs = ty->chunk[0];
367   |     }
368   |     ty->cur_rec = 0;
369   |     ty->first_chunk = 0;
370   |  
371   |  ff_dlog(s, "chunk has %d records\n", num_recs);
372   |     ty->cur_chunk_pos = 4;
373   |  
374   |     av_freep(&ty->rec_hdrs);
375   |  
376   |  if (num_recs * 16 >= CHUNK_SIZE - 4)
377   |  return AVERROR_INVALIDDATA;
378   |  
379   |     ty->rec_hdrs = parse_chunk_headers(ty->chunk + 4, num_recs);
380   |  if (!ty->rec_hdrs)
381   |  return AVERROR(ENOMEM);
382   |     ty->cur_chunk_pos += 16 * num_recs;
383   |  
384   |  return 0;
385   | }
386   |  
387   | static int demux_video(AVFormatContext *s, TyRecHdr *rec_hdr, AVPacket *pkt)
388   | {
389   |     TYDemuxContext *ty = s->priv_data;
390   |  const int subrec_type = rec_hdr->subrec_type;
391   |  const int64_t rec_size = rec_hdr->rec_size;
603   |  if (check_sync_pes(s, pkt, es_offset1, rec_size) == -1) {
604   |  /* partial PES header found, nothing else.
605   |  * we're done. */
606   |             av_packet_unref(pkt);
607   |  return 0;
608   |         }
609   |     } else if (subrec_type == 0x04) {
610   |  /* SA Audio with no PES Header                      */
611   |  /* ================================================ */
612   |  if ((ret = av_new_packet(pkt, rec_size)) < 0)
613   |  return ret;
614   |         memcpy(pkt->data, ty->chunk + ty->cur_chunk_pos, rec_size);
615   |         ty->cur_chunk_pos += rec_size;
616   |         pkt->stream_index = 1;
617   |         pkt->pts = ty->last_audio_pts;
618   |     } else if (subrec_type == 0x09) {
619   |  if ((ret = av_new_packet(pkt, rec_size)) < 0)
620   |  return ret;
621   |         memcpy(pkt->data, ty->chunk + ty->cur_chunk_pos, rec_size);
622   |         ty->cur_chunk_pos += rec_size ;
623   |         pkt->stream_index = 1;
624   |  
625   |  /* DTiVo AC3 Audio Data with PES Header             */
626   |  /* ================================================ */
627   |         es_offset1 = find_es_header(ty_AC3AudioPacket, pkt->data, 5);
628   |  
629   |  /* Check for complete PES */
630   |  if (check_sync_pes(s, pkt, es_offset1, rec_size) == -1) {
631   |  /* partial PES header found, nothing else.  we're done. */
632   |             av_packet_unref(pkt);
633   |  return 0;
634   |         }
635   |  /* S2 DTivo has invalid long AC3 packets */
636   |  if (ty->tivo_series == TIVO_SERIES2) {
637   |  if (pkt->size > AC3_PKT_LENGTH) {
638   |                 pkt->size -= 2;
639   |                 ty->ac3_pkt_size = 0;
640   |             } else {
641   |                 ty->ac3_pkt_size = pkt->size;
642   |             }
643   |         }
644   |     } else {
645   |  /* Unsupported/Unknown */
646   |         ty->cur_chunk_pos += rec_size;
647   |  return 0;
648   |     }
649   |  
650   |  return 1;
651   | }
652   |  
653   | static int ty_read_packet(AVFormatContext *s, AVPacket *pkt)
654   | {
655   |  TYDemuxContext *ty = s->priv_data;
656   |     AVIOContext *pb = s->pb;
657   |     TyRecHdr *rec;
658   |     int64_t rec_size = 0;
659   |  int ret = 0;
660   |  
661   |  if (avio_feof(pb))
    1Assuming the condition is false→
    2←Taking false branch→
662   |  return AVERROR_EOF;
663   |  
664   |  while (ret <= 0) {
665   |  if (!ty->rec_hdrs || ty->first_chunk || ty->cur_rec >= ty->num_recs) {
    3←Assuming field 'rec_hdrs' is null→
666   |  if (get_chunk(s) < 0 || ty->num_recs <= 0)
    4←Calling 'get_chunk'→
667   |  return AVERROR_EOF;
668   |         }
669   |  
670   |         rec = &ty->rec_hdrs[ty->cur_rec];
671   |         rec_size = rec->rec_size;
672   |         ty->cur_rec++;
673   |  
674   |  if (rec_size <= 0)
675   |  continue;
676   |  
677   |  if (ty->cur_chunk_pos + rec->rec_size > CHUNK_SIZE)
678   |  return AVERROR_INVALIDDATA;
679   |  
680   |  if (avio_feof(pb))
681   |  return AVERROR_EOF;
682   |  
683   |  switch (rec->rec_type) {
684   |  case VIDEO_ID:
685   |             ret = demux_video(s, rec, pkt);
686   |  break;
687   |  case AUDIO_ID:
688   |             ret = demux_audio(s, rec, pkt);
689   |  break;
690   |  default:
691   |  ff_dlog(s, "Invalid record type 0x%02x\n", rec->rec_type);
692   |  case 0x01:
693   |  case 0x02:
694   |  case 0x03: /* TiVo data services */
695   |  case 0x05: /* unknown, but seen regularly */
696   |             ty->cur_chunk_pos += rec->rec_size;