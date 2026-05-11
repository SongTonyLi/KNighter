### Report Summary

File:| /home/cc/ai2sec/KNighter/ffmpeg/libavformat/wavdec.c  
---|---  
Warning:| line 684, column 14  
buffer read by avio_read may be partially uninitialized  
  
### Annotated Source Code


624   |  /* G.729 hack (for Ticket4577)
625   |  * FIXME: Come up with cleaner, more general solution */
626   |  if (st->codecpar->codec_id == AV_CODEC_ID_G729 && sample_count && (data_size << 3) > sample_count) {
627   |         av_log(s, AV_LOG_WARNING, "ignoring wrong sample_count %"PRId64"\n", sample_count);
628   |         sample_count = 0;
629   |     }
630   |  
631   |  if (!sample_count || av_get_exact_bits_per_sample(st->codecpar->codec_id) > 0)
632   |  if (   st->codecpar->channels
633   |             && data_size
634   |             && av_get_bits_per_sample(st->codecpar->codec_id)
635   |             && wav->data_end <= avio_size(pb))
636   |             sample_count = (data_size << 3)
637   |                                   /
638   |                 (st->codecpar->channels * (uint64_t)av_get_bits_per_sample(st->codecpar->codec_id));
639   |  
640   |  if (sample_count)
641   |         st->duration = sample_count;
642   |  
643   |  if (st->codecpar->codec_id == AV_CODEC_ID_PCM_S32LE &&
644   |         st->codecpar->block_align == st->codecpar->channels * 4 &&
645   |         st->codecpar->bits_per_coded_sample == 32 &&
646   |         st->codecpar->extradata_size == 2 &&
647   |  AV_RL16(st->codecpar->extradata) == 1) {
648   |         st->codecpar->codec_id = AV_CODEC_ID_PCM_F16LE;
649   |         st->codecpar->bits_per_coded_sample = 16;
650   |     } else if (st->codecpar->codec_id == AV_CODEC_ID_PCM_S24LE &&
651   |                st->codecpar->block_align == st->codecpar->channels * 4 &&
652   |                st->codecpar->bits_per_coded_sample == 24) {
653   |         st->codecpar->codec_id = AV_CODEC_ID_PCM_F24LE;
654   |     } else if (st->codecpar->codec_id == AV_CODEC_ID_XMA1 ||
655   |                st->codecpar->codec_id == AV_CODEC_ID_XMA2) {
656   |         st->codecpar->block_align = 2048;
657   |     } else if (st->codecpar->codec_id == AV_CODEC_ID_ADPCM_MS && st->codecpar->channels > 2 &&
658   |                st->codecpar->block_align < INT_MAX / st->codecpar->channels) {
659   |         st->codecpar->block_align *= st->codecpar->channels;
660   |     }
661   |  
662   |     ff_metadata_conv_ctx(s, NULL, wav_metadata_conv);
663   |     ff_metadata_conv_ctx(s, NULL, ff_riff_info_conv);
664   |  
665   |     set_spdif(s, wav);
666   |  
667   |  return 0;
668   | }
669   |  
670   | /**
671   |  * Find chunk with w64 GUID by skipping over other chunks.
672   |  * @return the size of the found chunk
673   |  */
674   | static int64_t find_guid(AVIOContext *pb, const uint8_t guid1[16])
675   | {
676   |  uint8_t guid[16];
677   |     int64_t size;
678   |  
679   |  while (!avio_feof(pb)) {
    11←Assuming the condition is true→
    12←Loop condition is true.  Entering loop body→
680   |  avio_read(pb, guid, 16);
681   |         size = avio_rl64(pb);
682   |  if (size <= 24 || size > INT64_MAX - 8)
    13←Assuming 'size' is > 24→
    14←Assuming the condition is false→
    15←Taking false branch→
683   |  return AVERROR_INVALIDDATA;
684   |  if (!memcmp(guid, guid1, 16))
    16←buffer read by avio_read may be partially uninitialized
685   |  return size;
686   |         avio_skip(pb, FFALIGN(size, INT64_C(8)) - 24);
687   |     }
688   |  return AVERROR_EOF;
689   | }
690   |  
691   | static int wav_read_packet(AVFormatContext *s, AVPacket *pkt)
692   | {
693   |  int ret, size;
694   |     int64_t left;
695   |     WAVDemuxContext *wav = s->priv_data;
696   |     AVStream *st = s->streams[0];
697   |  
698   |  if (CONFIG_SPDIF_DEMUXER && wav->spdif == 1)
    1Assuming field 'spdif' is not equal to 1→
    2←Taking false branch→
699   |  return ff_spdif_read_packet(s, pkt);
700   |  
701   |  if (wav->smv_data_ofs > 0) {
    3←Assuming field 'smv_data_ofs' is <= 0→
    4←Taking false branch→
702   |         int64_t audio_dts, video_dts;
703   |         AVStream *vst = wav->vst;
704   | smv_retry:
705   |         audio_dts = (int32_t)st->cur_dts;
706   |         video_dts = (int32_t)vst->cur_dts;
707   |  
708   |  if (audio_dts != AV_NOPTS_VALUE && video_dts != AV_NOPTS_VALUE) {
709   |  /*We always return a video frame first to get the pixel format first*/
710   |             wav->smv_last_stream = wav->smv_given_first ?
711   |                 av_compare_ts(video_dts, vst->time_base,
712   |                               audio_dts,  st->time_base) > 0 : 0;
713   |             wav->smv_given_first = 1;
714   |         }
715   |         wav->smv_last_stream = !wav->smv_last_stream;
716   |         wav->smv_last_stream |= wav->audio_eof;
717   |         wav->smv_last_stream &= !wav->smv_eof;
718   |  if (wav->smv_last_stream) {
719   |             uint64_t old_pos = avio_tell(s->pb);
720   |             uint64_t new_pos = wav->smv_data_ofs +
721   |                 wav->smv_block * wav->smv_block_size;
722   |  if (avio_seek(s->pb, new_pos, SEEK_SET) < 0) {
723   |                 ret = AVERROR_EOF;
724   |  goto smv_out;
725   |             }
726   |             size = avio_rl24(s->pb);
727   |             ret  = av_get_packet(s->pb, pkt, size);
728   |  if (ret < 0)
729   |  goto smv_out;
730   |             pkt->pos -= 3;
731   |             pkt->pts = wav->smv_block * wav->smv_frames_per_jpeg;
732   |             pkt->duration = wav->smv_frames_per_jpeg;
733   |             wav->smv_block++;
734   |  
735   |             pkt->stream_index = vst->index;
736   | smv_out:
737   |             avio_seek(s->pb, old_pos, SEEK_SET);
738   |  if (ret == AVERROR_EOF) {
739   |                 wav->smv_eof = 1;
740   |  goto smv_retry;
741   |             }
742   |  return ret;
743   |         }
744   |     }
745   |  
746   |  left = wav->data_end - avio_tell(s->pb);
747   |  if (wav->ignore_length)
    5←Assuming field 'ignore_length' is 0→
    6←Taking false branch→
748   |         left = INT_MAX;
749   |  if (left <= 0) {
    7←Assuming 'left' is <= 0→
750   |  if (CONFIG_W64_DEMUXER && wav->w64)
    8←Assuming field 'w64' is not equal to 0→
    9←Taking true branch→
751   |  left = find_guid(s->pb, ff_w64_guid_data) - 24;
    10←Calling 'find_guid'→
752   |  else
753   |             left = find_tag(wav, s->pb, MKTAG('d', 'a', 't', 'a'));
754   |  if (left < 0) {
755   |             wav->audio_eof = 1;
756   |  if (wav->smv_data_ofs > 0 && !wav->smv_eof)
757   |  goto smv_retry;
758   |  return AVERROR_EOF;
759   |         }
760   |         wav->data_end = avio_tell(s->pb) + left;
761   |     }
762   |  
763   |     size = wav->max_size;
764   |  if (st->codecpar->block_align > 1) {
765   |  if (size < st->codecpar->block_align)
766   |             size = st->codecpar->block_align;
767   |         size = (size / st->codecpar->block_align) * st->codecpar->block_align;
768   |     }
769   |     size = FFMIN(size, left);
770   |     ret  = av_get_packet(s->pb, pkt, size);
771   |  if (ret < 0)
772   |  return ret;
773   |     pkt->stream_index = 0;
774   |  
775   |  return ret;
776   | }
777   |  
778   | static int wav_read_seek(AVFormatContext *s,
779   |  int stream_index, int64_t timestamp, int flags)
780   | {
781   |     WAVDemuxContext *wav = s->priv_data;