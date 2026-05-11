### Report Summary

File:| /home/cc/ai2sec/KNighter/ffmpeg/libavformat/iff.c  
---|---  
Warning:| line 791, column 35  
buffer read by avio_read may be partially uninitialized  
  
### Annotated Source Code


366   |         chunk_id = avio_rl32(pb);
367   |         data_size = iff->is_64bit ? avio_rb64(pb) : avio_rb32(pb);
368   |         data_pos = avio_tell(pb);
369   |  
370   |  if (data_size < 1 || data_size >= INT64_MAX)
371   |  return AVERROR_INVALIDDATA;
372   |  
373   |  switch (chunk_id) {
374   |  case ID_DSTF:
375   |  if (!pkt) {
376   |                 iff->body_pos  = avio_tell(pb) - (iff->is_64bit ? 12 : 8);
377   |                 iff->body_size = iff->body_end - iff->body_pos;
378   |  return 0;
379   |             }
380   |             ret = av_get_packet(pb, pkt, data_size);
381   |  if (ret < 0)
382   |  return ret;
383   |  if (data_size & 1)
384   |                 avio_skip(pb, 1);
385   |             pkt->flags |= AV_PKT_FLAG_KEY;
386   |             pkt->stream_index = 0;
387   |             pkt->duration = 588 * s->streams[0]->codecpar->sample_rate / 44100;
388   |             pkt->pos = chunk_pos;
389   |  
390   |             chunk_pos = avio_tell(pb);
391   |  if (chunk_pos >= iff->body_end)
392   |  return 0;
393   |  
394   |             avio_seek(pb, chunk_pos, SEEK_SET);
395   |  return 0;
396   |  
397   |  case ID_FRTE:
398   |  if (data_size < 4)
399   |  return AVERROR_INVALIDDATA;
400   |             s->streams[0]->duration = avio_rb32(pb) * 588LL * s->streams[0]->codecpar->sample_rate / 44100;
401   |  break;
402   |         }
403   |  
404   |         avio_skip(pb, data_size - (avio_tell(pb) - data_pos) + (data_size & 1));
405   |     }
406   |  
407   |  return ret;
408   | }
409   |  
410   | static const uint8_t deep_rgb24[] = {0, 0, 0, 3, 0, 1, 0, 8, 0, 2, 0, 8, 0, 3, 0, 8};
411   | static const uint8_t deep_rgba[]  = {0, 0, 0, 4, 0, 1, 0, 8, 0, 2, 0, 8, 0, 3, 0, 8};
412   | static const uint8_t deep_bgra[]  = {0, 0, 0, 4, 0, 3, 0, 8, 0, 2, 0, 8, 0, 1, 0, 8};
413   | static const uint8_t deep_argb[]  = {0, 0, 0, 4, 0,17, 0, 8, 0, 1, 0, 8, 0, 2, 0, 8};
414   | static const uint8_t deep_abgr[]  = {0, 0, 0, 4, 0,17, 0, 8, 0, 3, 0, 8, 0, 2, 0, 8};
415   |  
416   | static int iff_read_header(AVFormatContext *s)
417   | {
418   |  IffDemuxContext *iff = s->priv_data;
419   |     AVIOContext *pb = s->pb;
420   |     AVStream *st;
421   |     uint8_t *buf;
422   |     uint32_t chunk_id;
423   |     uint64_t data_size;
424   |     uint32_t screenmode = 0, num, den;
425   |  unsigned transparency = 0;
426   |  unsigned masking = 0; // no mask
427   |     uint8_t fmt[16];
428   |  int fmt_size;
429   |  
430   |     st = avformat_new_stream(s, NULL);
431   |  if (!st)
    1Assuming 'st' is non-null→
    2←Taking false branch→
432   |  return AVERROR(ENOMEM);
433   |  
434   |  st->codecpar->channels = 1;
435   |     st->codecpar->channel_layout = AV_CH_LAYOUT_MONO;
436   |  iff->is_64bit = avio_rl32(pb) == ID_FRM8;
    3←Assuming the condition is false→
437   |  avio_skip(pb, iff->is_64bit3.1Field 'is_64bit' is 0 ? 8 : 4);
    4←'?' condition is false→
438   |  // codec_tag used by ByteRun1 decoder to distinguish progressive (PBM) and interlaced (ILBM) content
439   |     st->codecpar->codec_tag = avio_rl32(pb);
440   |  if (st->codecpar->codec_tag == ID_ANIM) {
    5←Assuming the condition is false→
    6←Taking false branch→
441   |         avio_skip(pb, 12);
442   |     }
443   |  iff->bitmap_compression = -1;
444   |     iff->svx8_compression = -1;
445   |     iff->maud_bits = -1;
446   |     iff->maud_compression = -1;
447   |  
448   |  while(!avio_feof(pb)) {
    7←Assuming the condition is true→
    8←Loop condition is true.  Entering loop body→
    19←Assuming the condition is false→
    20←Loop condition is false. Execution continues on line 719→
449   |  uint64_t orig_pos;
450   |  int res;
451   |  const char *metadata_tag = NULL;
452   |  int version, nb_comments, i;
453   |  chunk_id = avio_rl32(pb);
454   |  data_size = iff->is_64bit8.1Field 'is_64bit' is 0 ? avio_rb64(pb) : avio_rb32(pb);
    9←'?' condition is false→
455   |         orig_pos = avio_tell(pb);
456   |  
457   |  if (data_size >= INT64_MAX)
    10←Assuming 'data_size' is < INT64_MAX→
    11←Taking false branch→
458   |  return AVERROR_INVALIDDATA;
459   |  
460   |  switch(chunk_id) {
    12←Control jumps to 'case 1128552020:'  at line 617→
461   |  case ID_VHDR:
462   |             st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
463   |  
464   |  if (data_size < 14)
465   |  return AVERROR_INVALIDDATA;
466   |             avio_skip(pb, 12);
467   |             st->codecpar->sample_rate = avio_rb16(pb);
468   |  if (data_size >= 16) {
469   |                 avio_skip(pb, 1);
470   |                 iff->svx8_compression = avio_r8(pb);
471   |             }
472   |  break;
473   |  
474   |  case ID_MHDR:
475   |             st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
476   |  
477   |  if (data_size < 32)
478   |  return AVERROR_INVALIDDATA;
479   |             avio_skip(pb, 4);
480   |             iff->maud_bits = avio_rb16(pb);
481   |             avio_skip(pb, 2);
482   |             num = avio_rb32(pb);
483   |             den = avio_rb16(pb);
484   |  if (!den)
485   |  return AVERROR_INVALIDDATA;
486   |             avio_skip(pb, 2);
487   |             st->codecpar->sample_rate = num / den;
488   |             st->codecpar->channels = avio_rb16(pb);
489   |             iff->maud_compression = avio_rb16(pb);
490   |  if (st->codecpar->channels == 1)
567   |  break;
568   |  
569   |  case ID_ANHD:
570   |  break;
571   |  
572   |  case ID_DPAN:
573   |             avio_skip(pb, 2);
574   |             st->duration = avio_rb16(pb);
575   |  break;
576   |  
577   |  case ID_DPEL:
578   |  if (data_size < 4 || (data_size & 3))
579   |  return AVERROR_INVALIDDATA;
580   |  if ((fmt_size = avio_read(pb, fmt, sizeof(fmt))) < 0)
581   |  return fmt_size;
582   |  if (fmt_size == sizeof(deep_rgb24) && !memcmp(fmt, deep_rgb24, sizeof(deep_rgb24)))
583   |                 st->codecpar->format = AV_PIX_FMT_RGB24;
584   |  else if (fmt_size == sizeof(deep_rgba) && !memcmp(fmt, deep_rgba, sizeof(deep_rgba)))
585   |                 st->codecpar->format = AV_PIX_FMT_RGBA;
586   |  else if (fmt_size == sizeof(deep_bgra) && !memcmp(fmt, deep_bgra, sizeof(deep_bgra)))
587   |                 st->codecpar->format = AV_PIX_FMT_BGRA;
588   |  else if (fmt_size == sizeof(deep_argb) && !memcmp(fmt, deep_argb, sizeof(deep_argb)))
589   |                 st->codecpar->format = AV_PIX_FMT_ARGB;
590   |  else if (fmt_size == sizeof(deep_abgr) && !memcmp(fmt, deep_abgr, sizeof(deep_abgr)))
591   |                 st->codecpar->format = AV_PIX_FMT_ABGR;
592   |  else {
593   |                 avpriv_request_sample(s, "color format %.16s", fmt);
594   |  return AVERROR_PATCHWELCOME;
595   |             }
596   |  break;
597   |  
598   |  case ID_DGBL:
599   |             st->codecpar->codec_type         = AVMEDIA_TYPE_VIDEO;
600   |  if (data_size < 8)
601   |  return AVERROR_INVALIDDATA;
602   |             st->codecpar->width              = avio_rb16(pb);
603   |             st->codecpar->height             = avio_rb16(pb);
604   |             iff->bitmap_compression          = avio_rb16(pb);
605   |             st->sample_aspect_ratio.num      = avio_r8(pb);
606   |             st->sample_aspect_ratio.den      = avio_r8(pb);
607   |             st->codecpar->bits_per_coded_sample = 24;
608   |  break;
609   |  
610   |  case ID_DLOC:
611   |  if (data_size < 4)
612   |  return AVERROR_INVALIDDATA;
613   |             st->codecpar->width  = avio_rb16(pb);
614   |             st->codecpar->height = avio_rb16(pb);
615   |  break;
616   |  
617   |  case ID_TVDC:
618   |  if (data_size < sizeof(iff->tvdc))
    13←Assuming the condition is false→
    14←Taking false branch→
619   |  return AVERROR_INVALIDDATA;
620   |  res = avio_read(pb, iff->tvdc, sizeof(iff->tvdc));
621   |  if (res < 0)
    15←Assuming 'res' is >= 0→
    16←Taking false branch→
622   |  return res;
623   |  break;
624   |  
625   |  case ID_ANNO:
626   |  case ID_TEXT:      metadata_tag = "comment";   break;
627   |  case ID_AUTH:      metadata_tag = "artist";    break;
628   |  case ID_COPYRIGHT: metadata_tag = "copyright"; break;
629   |  case ID_NAME:      metadata_tag = "title";     break;
630   |  
631   |  /* DSD tags */
632   |  
633   |  case MKTAG('F','V','E','R'):
634   |  if (data_size < 4)
635   |  return AVERROR_INVALIDDATA;
636   |             version = avio_rb32(pb);
637   |             av_log(s, AV_LOG_DEBUG, "DSIFF v%d.%d.%d.%d\n",version >> 24, (version >> 16) & 0xFF, (version >> 8) & 0xFF, version & 0xFF);
638   |             st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
639   |  break;
640   |  
641   |  case MKTAG('D','I','I','N'):
642   |             res = parse_dsd_diin(s, st, orig_pos + data_size);
643   |  if (res < 0)
644   |  return res;
645   |  break;
646   |  
647   |  case MKTAG('P','R','O','P'):
648   |  if (data_size < 4)
649   |  return AVERROR_INVALIDDATA;
650   |  if (avio_rl32(pb) != MKTAG('S','N','D',' ')) {
651   |                 avpriv_request_sample(s, "unknown property type");
652   |  break;
653   |             }
660   |  if (data_size < 2)
661   |  return AVERROR_INVALIDDATA;
662   |             nb_comments = avio_rb16(pb);
663   |  for (i = 0; i < nb_comments; i++) {
664   |  int year, mon, day, hour, min, type, ref;
665   |  char tmp[24];
666   |  const char *tag;
667   |  int metadata_size;
668   |  
669   |                 year = avio_rb16(pb);
670   |                 mon  = avio_r8(pb);
671   |                 day  = avio_r8(pb);
672   |                 hour = avio_r8(pb);
673   |                 min  = avio_r8(pb);
674   |                 snprintf(tmp, sizeof(tmp), "%04d-%02d-%02d %02d:%02d", year, mon, day, hour, min);
675   |                 av_dict_set(&st->metadata, "comment_time", tmp, 0);
676   |  
677   |                 type = avio_rb16(pb);
678   |                 ref  = avio_rb16(pb);
679   |  switch (type) {
680   |  case 1:
681   |  if (!i)
682   |                         tag = "channel_comment";
683   |  else {
684   |                         snprintf(tmp, sizeof(tmp), "channel%d_comment", ref);
685   |                         tag = tmp;
686   |                     }
687   |  break;
688   |  case 2:
689   |                     tag = ref < FF_ARRAY_ELEMS(dsd_source_comment) ? dsd_source_comment[ref] : "source_comment";
690   |  break;
691   |  case 3:
692   |                     tag = ref < FF_ARRAY_ELEMS(dsd_history_comment) ? dsd_history_comment[ref] : "file_history";
693   |  break;
694   |  default:
695   |                     tag = "comment";
696   |                 }
697   |  
698   |                 metadata_size  = avio_rb32(pb);
699   |  if ((res = get_metadata(s, tag, metadata_size)) < 0) {
700   |                     av_log(s, AV_LOG_ERROR, "cannot allocate metadata tag %s!\n", tag);
701   |  return res;
702   |                 }
703   |  
704   |  if (metadata_size & 1)
705   |                     avio_skip(pb, 1);
706   |             }
707   |  break;
708   |         }
709   |  
710   |  if (metadata_tag17.1'metadata_tag' is null) {
    17← Execution continues on line 710→
    18←Taking false branch→
711   |  if ((res = get_metadata(s, metadata_tag, data_size)) < 0) {
712   |                 av_log(s, AV_LOG_ERROR, "cannot allocate metadata tag %s!\n", metadata_tag);
713   |  return res;
714   |             }
715   |         }
716   |  avio_skip(pb, data_size - (avio_tell(pb) - orig_pos) + (data_size & 1));
717   |  }
718   |  
719   |  if (st->codecpar->codec_tag == ID_ANIM)
    21←Taking false branch→
720   |         avio_seek(pb, 12, SEEK_SET);
721   |  else
722   |  avio_seek(pb, iff->body_pos, SEEK_SET);
723   |  
724   |  switch(st->codecpar->codec_type) {
    22←Control jumps to 'case AVMEDIA_TYPE_VIDEO:'  at line 769→
725   |  case AVMEDIA_TYPE_AUDIO:
726   |         avpriv_set_pts_info(st, 32, 1, st->codecpar->sample_rate);
727   |  
728   |  if (st->codecpar->codec_tag == ID_16SV)
729   |             st->codecpar->codec_id = AV_CODEC_ID_PCM_S16BE_PLANAR;
730   |  else if (st->codecpar->codec_tag == ID_MAUD) {
731   |  if (iff->maud_bits == 8 && !iff->maud_compression) {
732   |                 st->codecpar->codec_id = AV_CODEC_ID_PCM_U8;
733   |             } else if (iff->maud_bits == 16 && !iff->maud_compression) {
734   |                 st->codecpar->codec_id = AV_CODEC_ID_PCM_S16BE;
735   |             } else if (iff->maud_bits ==  8 && iff->maud_compression == 2) {
736   |                 st->codecpar->codec_id = AV_CODEC_ID_PCM_ALAW;
737   |             } else if (iff->maud_bits ==  8 && iff->maud_compression == 3) {
738   |                 st->codecpar->codec_id = AV_CODEC_ID_PCM_MULAW;
739   |             } else {
740   |                 avpriv_request_sample(s, "compression %d and bit depth %d", iff->maud_compression, iff->maud_bits);
741   |  return AVERROR_PATCHWELCOME;
742   |             }
743   |         } else if (st->codecpar->codec_tag != ID_DSD &&
744   |                    st->codecpar->codec_tag != ID_DST) {
745   |  switch (iff->svx8_compression) {
746   |  case COMP_NONE:
747   |                 st->codecpar->codec_id = AV_CODEC_ID_PCM_S8_PLANAR;
748   |  break;
749   |  case COMP_FIB:
750   |                 st->codecpar->codec_id = AV_CODEC_ID_8SVX_FIB;
751   |  break;
752   |  case COMP_EXP:
753   |                 st->codecpar->codec_id = AV_CODEC_ID_8SVX_EXP;
754   |  break;
755   |  default:
756   |                 av_log(s, AV_LOG_ERROR,
757   |  "Unknown SVX8 compression method '%d'\n", iff->svx8_compression);
758   |  return -1;
759   |             }
760   |         }
761   |  
762   |         st->codecpar->bits_per_coded_sample = av_get_bits_per_sample(st->codecpar->codec_id);
763   |         st->codecpar->bit_rate = (int64_t)st->codecpar->channels * st->codecpar->sample_rate * st->codecpar->bits_per_coded_sample;
764   |         st->codecpar->block_align = st->codecpar->channels * st->codecpar->bits_per_coded_sample;
765   |  if ((st->codecpar->codec_tag == ID_DSD || st->codecpar->codec_tag == ID_MAUD) && st->codecpar->block_align <= 0)
766   |  return AVERROR_INVALIDDATA;
767   |  break;
768   |  
769   |  case AVMEDIA_TYPE_VIDEO:
770   |  iff->bpp          = st->codecpar->bits_per_coded_sample;
771   |  if (st->codecpar->codec_tag == ID_ANIM)
772   |             avpriv_set_pts_info(st, 32, 1, 60);
773   |  if ((screenmode & 0x800 /* Hold And Modify */) && iff->bpp <= 8) {
774   |             iff->ham      = iff->bpp > 6 ? 6 : 4;
775   |             st->codecpar->bits_per_coded_sample = 24;
776   |         }
777   |  iff->flags        = (screenmode & 0x80 /* Extra HalfBrite */) && iff->bpp <= 8;
778   |         iff->masking      = masking;
779   |         iff->transparency = transparency;
780   |  
781   |  if (!st->codecpar->extradata) {
    23←Assuming field 'extradata' is non-null→
    24←Taking false branch→
782   |  int ret = ff_alloc_extradata(st->codecpar, IFF_EXTRA_VIDEO_SIZE);
783   |  if (ret < 0)
784   |  return ret;
785   |         }
786   |  av_assert0(st->codecpar->extradata_size >= IFF_EXTRA_VIDEO_SIZE);
    25←Assuming field 'extradata_size' is >= 41→
    26←Taking false branch→
    27←Loop condition is false.  Exiting loop→
787   |  buf = st->codecpar->extradata;
788   |         bytestream_put_be16(&buf, IFF_EXTRA_VIDEO_SIZE);
789   |         bytestream_put_byte(&buf, iff->bitmap_compression);
790   |         bytestream_put_byte(&buf, iff->bpp);
791   |  bytestream_put_byte(&buf, iff->ham);
    28←buffer read by avio_read may be partially uninitialized
792   |         bytestream_put_byte(&buf, iff->flags);
793   |         bytestream_put_be16(&buf, iff->transparency);
794   |         bytestream_put_byte(&buf, iff->masking);
795   |         bytestream_put_buffer(&buf, iff->tvdc, sizeof(iff->tvdc));
796   |         st->codecpar->codec_id = AV_CODEC_ID_IFF_ILBM;
797   |  break;
798   |  default:
799   |  return -1;
800   |     }
801   |  
802   |  return 0;
803   | }
804   |  
805   | static unsigned get_anim_duration(uint8_t *buf, int size)
806   | {
807   |     GetByteContext gb;
808   |  
809   |     bytestream2_init(&gb, buf, size);
810   |     bytestream2_skip(&gb, 4);
811   |  while (bytestream2_get_bytes_left(&gb) > 8) {
812   |  unsigned chunk = bytestream2_get_le32(&gb);
813   |  unsigned size = bytestream2_get_be32(&gb);
814   |  
815   |  if (chunk == ID_ANHD) {
816   |  if (size < 40)
817   |  break;
818   |             bytestream2_skip(&gb, 14);
819   |  return bytestream2_get_be32(&gb);
820   |         } else {
821   |             bytestream2_skip(&gb, size + size & 1);