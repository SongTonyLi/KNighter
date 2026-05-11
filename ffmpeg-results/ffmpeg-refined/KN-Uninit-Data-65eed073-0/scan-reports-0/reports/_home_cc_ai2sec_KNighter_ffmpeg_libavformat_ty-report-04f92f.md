### Report Summary

File:| /home/cc/ai2sec/KNighter/ffmpeg/libavformat/ty.c  
---|---  
Warning:| line 295, column 13  
buffer read by avio_read may be partially uninitialized  
  
### Annotated Source Code


228   |     }
229   |  if (num_9c0 > 0) {
230   |  ff_dlog(s, "detected AC-3 Audio (DTivo)\n");
231   |         ty->audio_type = TIVO_AUDIO_AC3;
232   |         ty->tivo_type = TIVO_TYPE_DTIVO;
233   |         ty->pts_offset = AC3_PTS_OFFSET;
234   |         ty->pes_length = AC3_PES_LENGTH;
235   |     } else if (num_3c0 > 0) {
236   |         ty->audio_type = TIVO_AUDIO_MPEG;
237   |  ff_dlog(s, "detected MPEG Audio\n");
238   |     }
239   |  
240   |  /* if tivo_type still unknown, we can check PTS location
241   |  * in MPEG packets to determine tivo_type */
242   |  if (ty->tivo_type == TIVO_TYPE_UNKNOWN) {
243   |         uint32_t data_offset = 16 * num_recs;
244   |  
245   |  for (i = 0; i < num_recs; i++) {
246   |  if (data_offset + hdrs[i].rec_size > CHUNK_SIZE)
247   |  break;
248   |  
249   |  if ((hdrs[i].subrec_type << 8 | hdrs[i].rec_type) == 0x3c0 && hdrs[i].rec_size > 15) {
250   |  /* first make sure we're aligned */
251   |  int pes_offset = find_es_header(ty_MPEGAudioPacket,
252   |                         &chunk[data_offset], 5);
253   |  if (pes_offset >= 0) {
254   |  /* pes found. on SA, PES has hdr data at offset 6, not PTS. */
255   |  if ((chunk[data_offset + 6 + pes_offset] & 0x80) == 0x80) {
256   |  /* S1SA or S2(any) Mpeg Audio (PES hdr, not a PTS start) */
257   |  if (ty->tivo_series == TIVO_SERIES1)
258   |  ff_dlog(s, "detected Stand-Alone Tivo\n");
259   |                         ty->tivo_type = TIVO_TYPE_SA;
260   |                         ty->pts_offset = SA_PTS_OFFSET;
261   |                     } else {
262   |  if (ty->tivo_series == TIVO_SERIES1)
263   |  ff_dlog(s, "detected DirecTV Tivo\n");
264   |                         ty->tivo_type = TIVO_TYPE_DTIVO;
265   |                         ty->pts_offset = DTIVO_PTS_OFFSET;
266   |                     }
267   |  break;
268   |                 }
269   |             }
270   |             data_offset += hdrs[i].rec_size;
271   |         }
272   |     }
273   |     av_free(hdrs);
274   |  
275   |  return 0;
276   | }
277   |  
278   | static int ty_read_header(AVFormatContext *s)
279   | {
280   |  TYDemuxContext *ty = s->priv_data;
281   |     AVIOContext *pb = s->pb;
282   |     AVStream *st, *ast;
283   |  int i, ret = 0;
284   |  
285   |     ty->first_audio_pts = AV_NOPTS_VALUE;
286   |     ty->last_audio_pts = AV_NOPTS_VALUE;
287   |     ty->last_video_pts = AV_NOPTS_VALUE;
288   |  
289   |  for (i = 0; i < CHUNK_PEEK_COUNT; i++) {
    1Loop condition is true.  Entering loop body→
290   |  avio_read(pb, ty->chunk, CHUNK_SIZE);
291   |  
292   |         ret = analyze_chunk(s, ty->chunk);
293   |  if (ret < 0)
    2←Assuming 'ret' is >= 0→
294   |  return ret;
295   |  if (ty->tivo_series != TIVO_SERIES_UNKNOWN &&
    3←buffer read by avio_read may be partially uninitialized
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