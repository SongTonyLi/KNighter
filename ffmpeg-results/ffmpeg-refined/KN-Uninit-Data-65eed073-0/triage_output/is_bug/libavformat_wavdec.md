### Report Summary

File:| format/wavdec.c  
---|---  
Warning:| line 701, column 14  
buffer read by avio_read may be partially uninitialized  
  
### Annotated Source Code


641   |  * FIXME: Come up with cleaner, more general solution */
642   |  if (st->codecpar->codec_id == AV_CODEC_ID_G729 && sample_count && (data_size << 3) > sample_count) {
643   |         av_log(s, AV_LOG_WARNING, "ignoring wrong sample_count %"PRId64"\n", sample_count);
644   |         sample_count = 0;
645   |     }
646   |  
647   |  if (!sample_count || av_get_exact_bits_per_sample(st->codecpar->codec_id) > 0)
648   |  if (   st->codecpar->ch_layout.nb_channels
649   |             && data_size
650   |             && av_get_bits_per_sample(st->codecpar->codec_id)
651   |             && wav->data_end <= avio_size(pb))
652   |             sample_count = (data_size << 3)
653   |                                   /
654   |                 (st->codecpar->ch_layout.nb_channels * (uint64_t)av_get_bits_per_sample(st->codecpar->codec_id));
655   |  
656   |  if (sample_count)
657   |         st->duration = sample_count;
658   |  
659   |  if (st->codecpar->codec_id == AV_CODEC_ID_PCM_S32LE &&
660   |         st->codecpar->block_align == st->codecpar->ch_layout.nb_channels * 4 &&
661   |         st->codecpar->bits_per_coded_sample == 32 &&
662   |         st->codecpar->extradata_size == 2 &&
663   |  AV_RL16(st->codecpar->extradata) == 1) {
664   |         st->codecpar->codec_id = AV_CODEC_ID_PCM_F16LE;
665   |         st->codecpar->bits_per_coded_sample = 16;
666   |     } else if (st->codecpar->codec_id == AV_CODEC_ID_PCM_S24LE &&
667   |                st->codecpar->block_align == st->codecpar->ch_layout.nb_channels * 4 &&
668   |                st->codecpar->bits_per_coded_sample == 24) {
669   |         st->codecpar->codec_id = AV_CODEC_ID_PCM_F24LE;
670   |     } else if (st->codecpar->codec_id == AV_CODEC_ID_XMA1 ||
671   |                st->codecpar->codec_id == AV_CODEC_ID_XMA2) {
672   |         st->codecpar->block_align = 2048;
673   |     } else if (st->codecpar->codec_id == AV_CODEC_ID_ADPCM_MS && st->codecpar->ch_layout.nb_channels > 2 &&
674   |                st->codecpar->block_align < INT_MAX / st->codecpar->ch_layout.nb_channels) {
675   |         st->codecpar->block_align *= st->codecpar->ch_layout.nb_channels;
676   |     }
677   |  
678   |     ff_metadata_conv_ctx(s, NULL, wav_metadata_conv);
679   |     ff_metadata_conv_ctx(s, NULL, ff_riff_info_conv);
680   |  
681   |     set_spdif(s, wav);
682   |     set_max_size(st, wav);
683   |  
684   |  return 0;
685   | }
686   |  
687   | /**
688   |  * Find chunk with w64 GUID by skipping over other chunks.
689   |  * @return the size of the found chunk
690   |  */
691   | static int64_t find_guid(AVIOContext *pb, const uint8_t guid1[16])
692   | {
693   |  uint8_t guid[16];
694   |     int64_t size;
695   |  
696   |  while (!avio_feof(pb)) {
    11←Assuming the condition is true→
    12←Loop condition is true.  Entering loop body→
697   |  avio_read(pb, guid, 16);
698   |         size = avio_rl64(pb);
699   |  if (size <= 24 || size > INT64_MAX - 8)
    13←Assuming 'size' is > 24→
    14←Assuming the condition is false→
    15←Taking false branch→
700   |  return AVERROR_INVALIDDATA;
701   |  if (!memcmp(guid, guid1, 16))
    16←buffer read by avio_read may be partially uninitialized
702   |  return size;
703   |         avio_skip(pb, FFALIGN(size, INT64_C(8)) - 24);
704   |     }
705   |  return AVERROR_EOF;
706   | }
707   |  
708   | static int wav_read_packet(AVFormatContext *s, AVPacket *pkt)
709   | {
710   |  int ret, size;
711   |     int64_t left;
712   |     WAVDemuxContext *wav = s->priv_data;
713   |     AVStream *st = s->streams[0];
714   |  
715   |  if (CONFIG_SPDIF_DEMUXER && wav->spdif == 1)
    1Assuming field 'spdif' is not equal to 1→
    2←Taking false branch→
716   |  return ff_spdif_read_packet(s, pkt);
717   |  
718   |  if (wav->smv_data_ofs > 0) {
    3←Assuming field 'smv_data_ofs' is <= 0→
    4←Taking false branch→
719   |         int64_t audio_dts, video_dts;
720   |         AVStream *vst = wav->vst;
721   | smv_retry:
722   |         audio_dts = (int32_t)ffstream( st)->cur_dts;
723   |         video_dts = (int32_t)ffstream(vst)->cur_dts;
724   |  
725   |  if (audio_dts != AV_NOPTS_VALUE && video_dts != AV_NOPTS_VALUE) {
726   |  /*We always return a video frame first to get the pixel format first*/
727   |             wav->smv_last_stream = wav->smv_given_first ?
728   |                 av_compare_ts(video_dts, vst->time_base,
729   |                               audio_dts,  st->time_base) > 0 : 0;
730   |             wav->smv_given_first = 1;
731   |         }
732   |         wav->smv_last_stream = !wav->smv_last_stream;
733   |         wav->smv_last_stream |= wav->audio_eof;
734   |         wav->smv_last_stream &= !wav->smv_eof;
735   |  if (wav->smv_last_stream) {
736   |             uint64_t old_pos = avio_tell(s->pb);
737   |             uint64_t new_pos = wav->smv_data_ofs +
738   |                 wav->smv_block * (int64_t)wav->smv_block_size;
739   |  if (avio_seek(s->pb, new_pos, SEEK_SET) < 0) {
740   |                 ret = AVERROR_EOF;
741   |  goto smv_out;
742   |             }
743   |             size = avio_rl24(s->pb);
744   |  if (size > wav->smv_block_size) {
745   |                 ret = AVERROR_EOF;
746   |  goto smv_out;
747   |             }
748   |             ret  = av_get_packet(s->pb, pkt, size);
749   |  if (ret < 0)
750   |  goto smv_out;
751   |             pkt->pos -= 3;
752   |             pkt->pts = wav->smv_block * wav->smv_frames_per_jpeg;
753   |             pkt->duration = wav->smv_frames_per_jpeg;
754   |             wav->smv_block++;
755   |  
756   |             pkt->stream_index = vst->index;
757   | smv_out:
758   |             avio_seek(s->pb, old_pos, SEEK_SET);
759   |  if (ret == AVERROR_EOF) {
760   |                 wav->smv_eof = 1;
761   |  goto smv_retry;
762   |             }
763   |  return ret;
764   |         }
765   |     }
766   |  
767   |  left = wav->data_end - avio_tell(s->pb);
768   |  if (wav->ignore_length)
    5←Assuming field 'ignore_length' is 0→
    6←Taking false branch→
769   |         left = INT_MAX;
770   |  if (left <= 0) {
    7←Assuming 'left' is <= 0→
771   |  if (CONFIG_W64_DEMUXER && wav->w64)
    8←Assuming field 'w64' is not equal to 0→
    9←Taking true branch→
772   |  left = find_guid(s->pb, ff_w64_guid_data) - 24;
    10←Calling 'find_guid'→
773   |  else
774   |             left = find_tag(wav, s->pb, MKTAG('d', 'a', 't', 'a'));
775   |  if (left < 0) {
776   |             wav->audio_eof = 1;
777   |  if (wav->smv_data_ofs > 0 && !wav->smv_eof)
778   |  goto smv_retry;
779   |  return AVERROR_EOF;
780   |         }
781   |  if (INT64_MAX - left < avio_tell(s->pb))
782   |  return AVERROR_INVALIDDATA;
783   |         wav->data_end = avio_tell(s->pb) + left;
784   |     }
785   |  
786   |     size = wav->max_size;
787   |  if (st->codecpar->block_align > 1) {
788   |  if (size < st->codecpar->block_align)
789   |             size = st->codecpar->block_align;
790   |         size = (size / st->codecpar->block_align) * st->codecpar->block_align;
791   |     }
792   |     size = FFMIN(size, left);
793   |     ret  = av_get_packet(s->pb, pkt, size);
794   |  if (ret < 0)
795   |  return ret;
796   |     pkt->stream_index = 0;
797   |  
798   |  return ret;
799   | }
800   |  
801   | static int wav_read_seek(AVFormatContext *s,
802   |  int stream_index, int64_t timestamp, int flags)